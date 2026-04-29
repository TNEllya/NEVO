/**
 * @file NatTraversal.cpp
 * @brief STUN/TURN NAT 穿透模块实现
 *
 * 实现简化版 STUN 协议 (RFC 5389) 的 NAT 类型探测、
 * UDP 打洞和 TURN 中继分配功能。
 *
 * NAT 类型探测算法（经典方法，简化实现）：
 *   Step 1: 向 STUN Server 1 发送 Binding Request → 获得 Mapped Address 1
 *   Step 2: 向 STUN Server 2 发送 Binding Request → 获得 Mapped Address 2
 *   Step 3: 判断逻辑：
 *     - 无响应 → Blocked
 *     - Mapped Address == Local Address → Open
 *     - Mapped Address 1 == Mapped Address 2 → Cone NAT (FullCone/Restricted/PortRestricted)
 *       简化实现默认为 FullCone（精确区分需要更多往返测试）
 *     - Mapped Address 1 != Mapped Address 2 → Symmetric NAT
 */

#include "nevo/network/NatTraversal.h"

#include <cstring>
#include <random>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/udp.hpp>

#include "nevo/core/common/Logger.h"

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

NatTraversal::NatTraversal() {
    NEVO_LOG_DEBUG("network", "NatTraversal created");
}

NatTraversal::~NatTraversal() {
    NEVO_LOG_DEBUG("network", "NatTraversal destroyed");
}

// ============================================================
// NAT 探测
// ============================================================

boost::asio::awaitable<NatInfo> NatTraversal::probeStun(
    const std::string& stun_host,
    uint16_t stun_port,
    const std::string& stun_host2,
    uint16_t stun_port2)
{
    NEVO_LOG_INFO("network", "probeStun: starting NAT detection via {}:{}",
                  stun_host, stun_port);

    NatInfo result;
    result.type = NatType::Blocked;
    result.udp_reachable = false;

    auto executor = co_await boost::asio::this_coro::executor;

    // ---- Step 1: 向第一台 STUN 服务器发送 Binding Request ----
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id1{};
    auto request1 = encodeBindingRequest(tx_id1);

    auto response1 = co_await sendAndReceive(stun_host, stun_port, request1);

    if (!response1.has_value()) {
        NEVO_LOG_WARN("network", "probeStun: no response from first STUN server, NAT type = Blocked");
        co_return result;
    }

    // 解析第一个响应
    auto msg1 = decodeStunMessage(response1->data(), response1->size());
    if (!msg1.has_value()) {
        NEVO_LOG_WARN("network", "probeStun: failed to parse first STUN response");
        co_return result;
    }

    // 提取映射地址 1
    auto mapped1 = extractMappedAddress(*msg1, tx_id1);
    if (!mapped1.has_value()) {
        NEVO_LOG_WARN("network", "probeStun: no mapped address in first STUN response");
        co_return result;
    }

    result.mapped_endpoint = *mapped1;
    result.udp_reachable = true;

    NEVO_LOG_INFO("network", "probeStun: mapped address 1 = {}:{}",
                  mapped1->address().to_string(), mapped1->port());

    // ---- 检查是否为 Open（映射地址与本地地址相同） ----
    // 注意：这里简化判断——如果映射地址是公网 IP 且端口与本地绑定端口相同，
    // 则认为是 Open。完整实现需要比较本地 socket 绑定的端点。
    // 简化实现：我们通过是否提供了第二台服务器来进一步判断
    // 如果没有第二台服务器，默认为 FullCone（已通过一次 STUN 测试）

    if (stun_host2.empty() || stun_port2 == 0) {
        // 只有单台 STUN 服务器，无法区分对称 NAT
        // 简化：假设为 FullCone（比 Blocked 更乐观）
        result.type = NatType::FullCone;
        NEVO_LOG_INFO("network", "probeStun: single STUN server, assuming NAT type = FullCone");
        co_return result;
    }

    // ---- Step 2: 向第二台 STUN 服务器发送 Binding Request ----
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id2{};
    auto request2 = encodeBindingRequest(tx_id2);

    auto response2 = co_await sendAndReceive(stun_host2, stun_port2, request2);

    if (!response2.has_value()) {
        // 第二台服务器无响应，但第一台有响应，保守估计为 PortRestricted
        result.type = NatType::PortRestricted;
        NEVO_LOG_WARN("network", "probeStun: no response from second STUN server, assuming PortRestricted");
        co_return result;
    }

    auto msg2 = decodeStunMessage(response2->data(), response2->size());
    if (!msg2.has_value()) {
        result.type = NatType::PortRestricted;
        NEVO_LOG_WARN("network", "probeStun: failed to parse second STUN response, assuming PortRestricted");
        co_return result;
    }

    auto mapped2 = extractMappedAddress(*msg2, tx_id2);
    if (!mapped2.has_value()) {
        result.type = NatType::PortRestricted;
        NEVO_LOG_WARN("network", "probeStun: no mapped address in second STUN response, assuming PortRestricted");
        co_return result;
    }

    NEVO_LOG_INFO("network", "probeStun: mapped address 2 = {}:{}",
                  mapped2->address().to_string(), mapped2->port());

    // ---- Step 3: 比较两个映射地址 ----
    if (mapped1->address() == mapped2->address() && mapped1->port() == mapped2->port()) {
        // 相同映射地址 → Cone NAT（FullCone / Restricted / PortRestricted）
        // 精确区分需要 "Change IP" 和 "Change Port" 测试（RFC 3489 经典方法），
        // 但多数现代 STUN 服务器不支持 Change Request 属性。
        // 简化实现：默认为 PortRestricted（最保守的锥形 NAT），
        // 因为假设 FullCone 会导致 P2P 连接尝试在受限 NAT 下失败。
        result.type = NatType::PortRestricted;
        NEVO_LOG_WARN("network", "probeStun: same mapped address, NAT type = PortRestricted "
                       "(simplified — actual type may be FullCone or Restricted)");
    } else {
        // 不同映射地址 → Symmetric NAT
        result.type = NatType::Symmetric;
        NEVO_LOG_INFO("network", "probeStun: different mapped addresses, NAT type = Symmetric");
    }

    NEVO_LOG_INFO("network", "probeStun: result type={}, udp_reachable={}, mapped={}:{}",
                  natTypeToString(result.type), result.udp_reachable,
                  result.mapped_endpoint.address().to_string(),
                  result.mapped_endpoint.port());

    co_return result;
}

// ============================================================
// UDP 打洞
// ============================================================

boost::asio::awaitable<bool> NatTraversal::punchUdp(
    boost::asio::ip::udp::socket& socket,
    const boost::asio::ip::udp::endpoint& server_endpoint)
{
    NEVO_LOG_INFO("network", "punchUdp: sending {} pings to {}:{}",
                  UDP_HOLE_PUNCH_COUNT,
                  server_endpoint.address().to_string(),
                  server_endpoint.port());

    auto executor = co_await boost::asio::this_coro::executor;

    // UDP 打洞数据包：简单的 "NEVO_PING" 标识 + 序号
    // 确保数据包足够大避免被某些 NAT 吞掉，但不超过 MTU
    uint8_t ping_data[64];
    std::memcpy(ping_data, "NEVO_PING", 9);

    // 设置接收超时
    boost::asio::steady_timer timeout_timer(executor);
    bool received_response = false;

    // 发送一系列 ping 包
    for (uint32_t i = 0; i < UDP_HOLE_PUNCH_COUNT; ++i) {
        // 写入序号
        uint32_t seq = htonl(i);
        std::memcpy(ping_data + 9, &seq, sizeof(seq));

        boost::system::error_code ec;
        auto bytes_sent = socket.send_to(
            boost::asio::buffer(ping_data, 13), server_endpoint, 0, ec);

        if (ec) {
            NEVO_LOG_WARN("network", "punchUdp: send failed at seq {}: {}", i, ec.message());
        } else {
            NEVO_LOG_TRACE("network", "punchUdp: sent {} bytes, seq={}", bytes_sent, i);
        }

        // 每次发送后短暂等待，检查是否有响应
        if (i == 0) {
            // 第一次发送后开始异步等待响应
            uint8_t recv_buf[256];
            boost::asio::ip::udp::endpoint sender_endpoint;
            auto recv_result = socket.receive_from(
                boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);

            if (!ec && recv_result > 0) {
                // 检查是否为打洞响应
                if (recv_result >= 9 && std::memcmp(recv_buf, "NEVO_PONG", 9) == 0) {
                    received_response = true;
                    NEVO_LOG_INFO("network", "punchUdp: received PONG from {}",
                                  sender_endpoint.address().to_string());
                    break;
                }
            }
        }

        // 间隔等待
        timeout_timer.expires_after(std::chrono::milliseconds(UDP_HOLE_PUNCH_INTERVAL_MS));
        co_await timeout_timer.async_wait(boost::asio::use_awaitable);
    }

    // 最后再等待一段时间检查是否有延迟响应
    if (!received_response) {
        uint8_t recv_buf[256];
        boost::asio::ip::udp::endpoint sender_endpoint;
        boost::system::error_code ec;

        // 非阻塞检查
        socket.non_blocking(true, ec);
        auto recv_result = socket.receive_from(
            boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);

        if (!ec && recv_result > 0 &&
            recv_result >= 9 && std::memcmp(recv_buf, "NEVO_PONG", 9) == 0) {
            received_response = true;
            NEVO_LOG_INFO("network", "punchUdp: received delayed PONG from {}",
                          sender_endpoint.address().to_string());
        }

        // 恢复阻塞模式
        socket.non_blocking(false, ec);
    }

    NEVO_LOG_INFO("network", "punchUdp: result = {}", received_response ? "success" : "no response");
    co_return received_response;
}

// ============================================================
// TURN 中继分配
// ============================================================

boost::asio::awaitable<Result<TurnRelayInfo>> NatTraversal::allocateTurnRelay(
    const std::string& turn_host,
    uint16_t turn_port,
    const TurnCredentials& credentials)
{
    NEVO_LOG_INFO("network", "allocateTurnRelay: requesting relay from {}:{}",
                  turn_host, turn_port);

    // ---- 第一次 Allocate Request（可能收到 401 Unauthorized） ----
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id1{};
    auto request1 = encodeAllocateRequest(tx_id1, credentials);

    auto response1 = co_await sendAndReceive(turn_host, turn_port, request1);

    if (!response1.has_value()) {
        NEVO_LOG_ERROR("network", "allocateTurnRelay: no response from TURN server");
        co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                     "No response from TURN server");
    }

    auto msg1 = decodeStunMessage(response1->data(), response1->size());

    if (!msg1.has_value()) {
        NEVO_LOG_ERROR("network", "allocateTurnRelay: failed to parse TURN response");
        co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                     "Failed to parse TURN response");
    }

    // 检查是否为错误响应（类型高位第 8 位为 1 表示错误）
    uint16_t msg_type = msg1->header.type;
    bool is_error = (msg_type & 0x0100) != 0;

    if (is_error) {
        // 401 Unauthorized 是正常流程，需要用服务器提供的 realm/nonce 重新认证
        // 简化实现：如果提供了完整的凭据（含 realm 和 nonce），直接重试
        if (!credentials.realm.empty() && !credentials.nonce.empty()) {
            NEVO_LOG_INFO("network", "allocateTurnRelay: got error, retrying with credentials");

            std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id2{};
            auto request2 = encodeAllocateRequest(tx_id2, credentials);

            auto response2 = co_await sendAndReceive(turn_host, turn_port, request2);

            if (!response2.has_value()) {
                NEVO_LOG_ERROR("network", "allocateTurnRelay: no response on retry");
                co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                             "No response from TURN server on retry");
            }

            auto msg2 = decodeStunMessage(response2->data(), response2->size());
            if (!msg2.has_value()) {
                co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                             "Failed to parse TURN retry response");
            }

            msg_type = msg2->header.type;
            is_error = (msg_type & 0x0100) != 0;

            if (!is_error && msg_type == STUN_ALLOCATE_RESPONSE) {
                // 成功获取中继
                auto relayed = extractRelayedAddress(*msg2, tx_id2);
                if (!relayed.has_value()) {
                    co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                                 "No relayed address in TURN response");
                }

                // 提取 LIFETIME 属性
                uint32_t lifetime = 600; // 默认 10 分钟
                for (const auto& attr : msg2->attributes) {
                    if (attr.type == STUN_ATTR_LIFETIME && attr.value.size() >= 4) {
                        uint32_t lt;
                        std::memcpy(&lt, attr.value.data(), sizeof(lt));
                        lifetime = ntohl(lt);
                        break;
                    }
                }

                TurnRelayInfo info;
                info.relayed_endpoint = *relayed;
                info.lifetime = lifetime;

                NEVO_LOG_INFO("network",
                              "allocateTurnRelay: success, relay={}:{}, lifetime={}s",
                              relayed->address().to_string(), relayed->port(), lifetime);

                co_return Ok(std::move(info));
            }
        }

        NEVO_LOG_ERROR("network", "allocateTurnRelay: TURN server returned error, type=0x{:04X}",
                        msg_type);
        co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                     "TURN server returned error response");
    }

    // ---- 成功响应 ----
    if (msg_type == STUN_ALLOCATE_RESPONSE) {
        auto relayed = extractRelayedAddress(msg1.value(), tx_id1);
        if (!relayed.has_value()) {
            co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                         "No relayed address in TURN response");
        }

        uint32_t lifetime = 600;
        for (const auto& attr : msg1->attributes) {
            if (attr.type == STUN_ATTR_LIFETIME && attr.value.size() >= 4) {
                uint32_t lt;
                std::memcpy(&lt, attr.value.data(), sizeof(lt));
                lifetime = ntohl(lt);
                break;
            }
        }

        TurnRelayInfo info;
        info.relayed_endpoint = *relayed;
        info.lifetime = lifetime;

        NEVO_LOG_INFO("network",
                      "allocateTurnRelay: success (no auth needed), relay={}:{}, lifetime={}s",
                      relayed->address().to_string(), relayed->port(), lifetime);

        co_return Ok(std::move(info));
    }

    co_return Err<TurnRelayInfo>(ResultCode::NatTraversalFailed,
                                 "Unexpected TURN response type");
}

// ============================================================
// STUN 消息编解码
// ============================================================

std::vector<uint8_t> NatTraversal::encodeBindingRequest(
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id)
{
    generateTransactionId(transaction_id);

    // SOFTWARE 属性："NEVO 0.1"
    const char* software = "NEVO 0.1";
    size_t software_len = std::strlen(software);
    // 属性值需要 4 字节对齐
    size_t padded_software_len = (software_len + 3) & ~size_t(3);

    // 消息长度 = SOFTWARE 属性（4 字节头 + padded_software_len）
    uint16_t message_length = static_cast<uint16_t>(
        STUN_ATTR_HEADER_SIZE + padded_software_len);

    std::vector<uint8_t> buffer(STUN_HEADER_SIZE + message_length, 0);

    // ---- 消息头（20 字节） ----
    uint16_t msg_type = htons(STUN_BINDING_REQUEST);
    uint16_t msg_len = htons(message_length);
    uint32_t magic = htonl(STUN_MAGIC_COOKIE);

    std::memcpy(buffer.data() + 0, &msg_type, 2);
    std::memcpy(buffer.data() + 2, &msg_len, 2);
    std::memcpy(buffer.data() + 4, &magic, 4);
    std::memcpy(buffer.data() + 8, transaction_id.data(), STUN_TRANSACTION_ID_SIZE);

    // ---- SOFTWARE 属性 ----
    size_t offset = STUN_HEADER_SIZE;
    uint16_t attr_type = htons(STUN_ATTR_SOFTWARE);
    uint16_t attr_len = htons(static_cast<uint16_t>(software_len));

    std::memcpy(buffer.data() + offset, &attr_type, 2);
    offset += 2;
    std::memcpy(buffer.data() + offset, &attr_len, 2);
    offset += 2;
    std::memcpy(buffer.data() + offset, software, software_len);
    // padding 部分已初始化为 0

    return buffer;
}

std::optional<StunMessage> NatTraversal::decodeStunMessage(
    const uint8_t* data,
    size_t data_len)
{
    if (data_len < STUN_HEADER_SIZE) {
        NEVO_LOG_DEBUG("network", "decodeStunMessage: data too short ({})", data_len);
        return std::nullopt;
    }

    StunMessage message;

    // ---- 解析消息头 ----
    uint16_t msg_type_be, msg_len_be;
    uint32_t magic_be;

    std::memcpy(&msg_type_be, data + 0, 2);
    std::memcpy(&msg_len_be, data + 2, 2);
    std::memcpy(&magic_be, data + 4, 4);

    message.header.type = ntohs(msg_type_be);
    message.header.length = ntohs(msg_len_be);
    message.header.magic_cookie = ntohl(magic_be);
    std::memcpy(message.header.transaction_id, data + 8, STUN_TRANSACTION_ID_SIZE);

    // 验证 Magic Cookie
    if (message.header.magic_cookie != STUN_MAGIC_COOKIE) {
        NEVO_LOG_DEBUG("network", "decodeStunMessage: invalid magic cookie 0x{:08X}",
                        message.header.magic_cookie);
        return std::nullopt;
    }

    // 验证消息长度
    size_t total_len = STUN_HEADER_SIZE + message.header.length;
    if (data_len < total_len) {
        NEVO_LOG_DEBUG("network",
                        "decodeStunMessage: incomplete message, expected {} got {}",
                        total_len, data_len);
        return std::nullopt;
    }

    // ---- 解析属性 ----
    size_t offset = STUN_HEADER_SIZE;
    size_t end = STUN_HEADER_SIZE + message.header.length;

    while (offset + STUN_ATTR_HEADER_SIZE <= end) {
        uint16_t attr_type_be, attr_len_be;
        std::memcpy(&attr_type_be, data + offset, 2);
        std::memcpy(&attr_len_be, data + offset + 2, 2);

        StunAttribute attr;
        attr.type = ntohs(attr_type_be);
        attr.length = ntohs(attr_len_be);

        offset += STUN_ATTR_HEADER_SIZE;

        // 检查属性值是否在消息范围内
        if (offset + attr.length > end) {
            NEVO_LOG_DEBUG("network",
                            "decodeStunMessage: attribute overflows message, attr_type=0x{:04X}",
                            attr.type);
            break;
        }

        if (attr.length > 0) {
            attr.value.assign(data + offset, data + offset + attr.length);
        }

        message.attributes.push_back(std::move(attr));

        // 属性值按 4 字节对齐前进
        offset += (attr.length + 3) & ~size_t(3);
    }

    return message;
}

std::optional<boost::asio::ip::udp::endpoint> NatTraversal::extractMappedAddress(
    const StunMessage& message,
    const std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id)
{
    // 优先查找 XOR-MAPPED-ADDRESS (RFC 5389)
    for (const auto& attr : message.attributes) {
        if (attr.type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
            return decodeAddressAttribute(attr.value, true, transaction_id);
        }
    }

    // 回退到 MAPPED-ADDRESS (RFC 3489 兼容)
    for (const auto& attr : message.attributes) {
        if (attr.type == STUN_ATTR_MAPPED_ADDRESS) {
            return decodeAddressAttribute(attr.value, false, transaction_id);
        }
    }

    NEVO_LOG_DEBUG("network", "extractMappedAddress: no mapped address attribute found");
    return std::nullopt;
}

std::vector<uint8_t> NatTraversal::encodeAllocateRequest(
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id,
    const TurnCredentials& credentials,
    uint32_t requested_lifetime)
{
    generateTransactionId(transaction_id);

    // 计算属性总长度
    // LIFETIME 属性：4 字节头 + 4 字节值
    size_t lifetime_attr_size = STUN_ATTR_HEADER_SIZE + 4;

    // SOFTWARE 属性
    const char* software = "NEVO 0.1";
    size_t software_len = std::strlen(software);
    size_t padded_software_len = (software_len + 3) & ~size_t(3);
    size_t software_attr_size = STUN_ATTR_HEADER_SIZE + padded_software_len;

    uint16_t message_length = static_cast<uint16_t>(lifetime_attr_size + software_attr_size);

    std::vector<uint8_t> buffer(STUN_HEADER_SIZE + message_length, 0);

    // ---- 消息头 ----
    uint16_t msg_type = htons(STUN_ALLOCATE_REQUEST);
    uint16_t msg_len = htons(message_length);
    uint32_t magic = htonl(STUN_MAGIC_COOKIE);

    std::memcpy(buffer.data() + 0, &msg_type, 2);
    std::memcpy(buffer.data() + 2, &msg_len, 2);
    std::memcpy(buffer.data() + 4, &magic, 4);
    std::memcpy(buffer.data() + 8, transaction_id.data(), STUN_TRANSACTION_ID_SIZE);

    // ---- LIFETIME 属性 ----
    size_t offset = STUN_HEADER_SIZE;
    uint16_t attr_type = htons(STUN_ATTR_LIFETIME);
    uint16_t attr_len = htons(4);
    uint32_t lifetime_be = htonl(requested_lifetime);

    std::memcpy(buffer.data() + offset, &attr_type, 2);
    offset += 2;
    std::memcpy(buffer.data() + offset, &attr_len, 2);
    offset += 2;
    std::memcpy(buffer.data() + offset, &lifetime_be, 4);
    offset += 4;

    // ---- SOFTWARE 属性 ----
    attr_type = htons(STUN_ATTR_SOFTWARE);
    attr_len = htons(static_cast<uint16_t>(software_len));

    std::memcpy(buffer.data() + offset, &attr_type, 2);
    offset += 2;
    std::memcpy(buffer.data() + offset, &attr_len, 2);
    offset += 2;
    std::memcpy(buffer.data() + offset, software, software_len);

    // 注意：完整实现还需要 USERNAME, REALM, NONCE, MESSAGE-INTEGRITY 属性
    // 简化实现省略了 MESSAGE-INTEGRITY HMAC 计算
    if (!credentials.username.empty()) {
        NEVO_LOG_DEBUG("network",
                        "encodeAllocateRequest: credentials provided but MESSAGE-INTEGRITY "
                        "not implemented in simplified version");
    }

    return buffer;
}

std::optional<boost::asio::ip::udp::endpoint> NatTraversal::extractRelayedAddress(
    const StunMessage& message,
    const std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id)
{
    for (const auto& attr : message.attributes) {
        if (attr.type == STUN_ATTR_XOR_RELAYED_ADDRESS) {
            return decodeAddressAttribute(attr.value, true, transaction_id);
        }
    }

    NEVO_LOG_DEBUG("network", "extractRelayedAddress: no relayed address attribute found");
    return std::nullopt;
}

// ============================================================
// 内部辅助方法
// ============================================================

void NatTraversal::generateTransactionId(
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id)
{
    // 使用 random_device 生成加密安全的随机事务 ID
    std::random_device rd;
    for (size_t i = 0; i < STUN_TRANSACTION_ID_SIZE; i += sizeof(unsigned int)) {
        unsigned int val = rd();
        size_t copy_len = std::min(sizeof(unsigned int), STUN_TRANSACTION_ID_SIZE - i);
        std::memcpy(transaction_id.data() + i, &val, copy_len);
    }
}

std::optional<boost::asio::ip::udp::endpoint> NatTraversal::decodeAddressAttribute(
    const std::vector<uint8_t>& value,
    bool is_xor,
    const std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id)
{
    // MAPPED-ADDRESS / XOR-MAPPED-ADDRESS 格式：
    //   Byte 0: 保留 (0x00)
    //   Byte 1: 地址族 (0x01 = IPv4, 0x02 = IPv6)
    //   Byte 2-3: 端口（XOR 编码时与 Magic Cookie 高 16 位异或）
    //   Byte 4+: IP 地址（IPv4: 4 字节, IPv6: 16 字节）

    if (value.size() < 4) {
        NEVO_LOG_DEBUG("network", "decodeAddressAttribute: value too short ({})", value.size());
        return std::nullopt;
    }

    uint8_t family = value[1];
    uint16_t port;

    // 解码端口
    std::memcpy(&port, value.data() + 2, 2);
    if (is_xor) {
        // 端口与 Magic Cookie 高 16 位异或
        port ^= static_cast<uint16_t>(STUN_MAGIC_COOKIE >> 16);
    }
    port = ntohs(port);

    if (family == 0x01) {
        // IPv4
        if (value.size() < 8) {
            NEVO_LOG_DEBUG("network", "decodeAddressAttribute: IPv4 value too short");
            return std::nullopt;
        }

        uint32_t addr;
        std::memcpy(&addr, value.data() + 4, 4);

        if (is_xor) {
            // IPv4 地址与 Magic Cookie 异或
            addr ^= htonl(STUN_MAGIC_COOKIE);
        }

        boost::asio::ip::address_v4 ipv4_addr(ntohl(addr));
        return boost::asio::ip::udp::endpoint(ipv4_addr, port);

    } else if (family == 0x02) {
        // IPv6
        if (value.size() < 20) {
            NEVO_LOG_DEBUG("network", "decodeAddressAttribute: IPv6 value too short");
            return std::nullopt;
        }

        std::array<uint8_t, 16> addr_bytes;
        std::memcpy(addr_bytes.data(), value.data() + 4, 16);

        if (is_xor) {
            // IPv6 地址与 Magic Cookie (4 字节) + Transaction ID (12 字节) 异或
            const uint8_t xor_key[16] = {
                0x21, 0x12, 0xA4, 0x42,  // Magic Cookie
            };
            // 前 4 字节与 Magic Cookie 异或
            for (int i = 0; i < 4; ++i) {
                addr_bytes[i] ^= xor_key[i];
            }
            // 后 12 字节与 Transaction ID 异或
            for (int i = 0; i < 12; ++i) {
                addr_bytes[4 + i] ^= transaction_id[i];
            }
        }

        boost::asio::ip::address_v6::bytes_type v6_bytes;
        std::copy(addr_bytes.begin(), addr_bytes.end(), v6_bytes.begin());
        boost::asio::ip::address_v6 ipv6_addr(v6_bytes);
        return boost::asio::ip::udp::endpoint(ipv6_addr, port);

    } else {
        NEVO_LOG_DEBUG("network", "decodeAddressAttribute: unknown address family {}", family);
        return std::nullopt;
    }
}

boost::asio::awaitable<std::optional<std::vector<uint8_t>>>
NatTraversal::sendAndReceive(
    const std::string& host,
    uint16_t port,
    const std::vector<uint8_t>& request)
{
    auto executor = co_await boost::asio::this_coro::executor;

    try {
        // 解析主机名
        boost::asio::ip::udp::resolver resolver(executor);
        auto endpoints = co_await resolver.async_resolve(
            host, std::to_string(port), boost::asio::use_awaitable);

        boost::asio::ip::udp::endpoint server_endpoint;
        for (auto it = endpoints.begin(); it != endpoints.end(); ++it) {
            server_endpoint = *it;
            break; // 使用第一个解析结果
        }

        // 创建 UDP socket
        boost::asio::ip::udp::socket socket(executor,
            boost::asio::ip::udp::v4());

        // 绑定到任意端口
        socket.open(boost::asio::ip::udp::v4());
        socket.bind(boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(), 0));

        // 发送请求
        co_await socket.async_send_to(
            boost::asio::buffer(request), server_endpoint,
            boost::asio::use_awaitable);

        NEVO_LOG_DEBUG("network", "sendAndReceive: sent {} bytes to {}:{}",
                       request.size(), host, port);

        // 等待响应（带超时）
        boost::asio::steady_timer timeout_timer(executor);
        timeout_timer.expires_after(std::chrono::milliseconds(STUN_TIMEOUT_MS));

        uint8_t recv_buffer[STUN_MAX_MESSAGE_SIZE];
        boost::asio::ip::udp::endpoint sender_endpoint;
        std::optional<std::vector<uint8_t>> result;
        bool timed_out = false;

        // 使用 cancel_after（Boost 1.82+）或手动超时
        // 为兼容性使用 async_wait + cancel 模式
        boost::system::error_code recv_ec;

        // 启动异步接收
        socket.async_receive_from(
            boost::asio::buffer(recv_buffer), sender_endpoint,
            [&](boost::system::error_code ec, size_t bytes_recvd) {
                recv_ec = ec;
                if (!ec && bytes_recvd > 0) {
                    result = std::vector<uint8_t>(
                        recv_buffer, recv_buffer + bytes_recvd);
                }
                timeout_timer.cancel();
            });

        // 启动超时
        timeout_timer.async_wait(
            [&](boost::system::error_code ec) {
                if (!ec) {
                    timed_out = true;
                    socket.cancel();
                }
            });

        // 等待任一完成（此时两个回调都已注册）
        co_await socket.async_wait(
            boost::asio::ip::udp::socket::wait_read,
            boost::asio::use_awaitable);

        if (timed_out || recv_ec) {
            NEVO_LOG_DEBUG("network", "sendAndReceive: timeout or error from {}:{}",
                           host, port);
            co_return std::nullopt;
        }

        if (result.has_value()) {
            NEVO_LOG_DEBUG("network", "sendAndReceive: received {} bytes from {}:{}",
                           result->size(), host, port);
        }

        co_return result;

    } catch (const std::exception& e) {
        NEVO_LOG_ERROR("network", "sendAndReceive: exception: {}", e.what());
        co_return std::nullopt;
    }
}

} // namespace nevo
