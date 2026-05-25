/**
 * @file NetworkManager.cpp
 * @brief NetworkManager 实现 - NAT 穿透级联、语音加解密、控制消息序列化
 *
 * 本文件实现了 NetworkManager 的所有核心功能：
 *
 *   1. TCP/TLS 连接管理
 *   2. NAT 穿透级联策略：UDP 直连 → STUN → UDP 打洞 → TURN → TCP 隧道
 *   3. 语音数据加密/解密（通过 VoiceCrypto）
 *   4. 控制消息序列化/反序列化（通过 PacketCodec）
 *   5. TCP 语音隧道回退（当 UDP 不可用时）
 */

#include "nevo/client/NetworkManager.h"

#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"

// Protobuf 头文件
#include "control.pb.h"
#include "voice.pb.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ssl.hpp>

#include <chrono>

#include <cstring>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

NetworkManager::NetworkManager(boost::asio::io_context& io_ctx)
    : io_ctx_(io_ctx)
    , nat_traversal_(std::make_unique<NatTraversal>())
    , ssl_wrapper_(std::make_unique<SslWrapper>(io_ctx))
{
    // 设置 TCP 语音隧道的语音帧回调
    tcp_voice_tunnel_.onVoiceFrame = [this](const uint8_t* data, size_t size) {
        handleTunnelVoiceFrame(data, size);
    };

    NEVO_LOG_INFO("network", "NetworkManager created");
}

NetworkManager::~NetworkManager()
{
    disconnect();
    NEVO_LOG_INFO("network", "NetworkManager destroyed");
}

void NetworkManager::setVoiceServerUdpPort(uint16_t udp_port)
{
    if (udp_port == 0 || !tcp_conn_) {
        return;
    }
    boost::system::error_code ec;
    auto remote_ep = tcp_conn_->socket().remote_endpoint(ec);
    if (ec) {
        NEVO_LOG_WARN("network", "Cannot get TCP remote endpoint for voice_server_endpoint: {}",
                      ec.message());
        return;
    }
    config_.voice_server_endpoint = boost::asio::ip::udp::endpoint(
        remote_ep.address(), udp_port);
    NEVO_LOG_INFO("network", "Voice server endpoint set to {}:{}",
                  remote_ep.address().to_string(), udp_port);
}

boost::asio::awaitable<void> NetworkManager::sendUdpRegistrationPacket()
{
    if (!udp_socket_ || config_.voice_server_endpoint == boost::asio::ip::udp::endpoint{}) {
        NEVO_LOG_WARN("network", "Cannot send UDP registration: socket or endpoint not ready");
        co_return;
    }

    voice::VoicePacketHeader header;
    header.set_sequence_number(voice_sequence_.fetch_add(1, std::memory_order_relaxed));
    header.set_sender_id(local_user_id_.value);
    header.set_channel_id(current_channel_id_.value);
    header.set_timestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    header.set_tcp_tunnel(false);

    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    header.SerializeToArray(header_buf.data(), static_cast<int>(header_size));

    uint8_t dummy = 0;
    std::vector<uint8_t> encrypted = voice_crypto_.encrypt(
        &dummy, 1,
        header_buf.data(), header_buf.size());

    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::vector<uint8_t> full_packet;
    full_packet.reserve(2 + header_buf.size() + encrypted.size());
    full_packet.insert(full_packet.end(),
                       reinterpret_cast<const uint8_t*>(&header_len),
                       reinterpret_cast<const uint8_t*>(&header_len) + 2);
    full_packet.insert(full_packet.end(), header_buf.begin(), header_buf.end());
    full_packet.insert(full_packet.end(), encrypted.begin(), encrypted.end());

    auto ec = co_await udp_socket_->asyncSendTo(
        full_packet.data(), static_cast<uint32_t>(full_packet.size()),
        config_.voice_server_endpoint);

    if (ec) {
        NEVO_LOG_WARN("network", "UDP registration packet send failed: {}", ec.message());
    } else {
        NEVO_LOG_INFO("network", "UDP registration packet sent to {}",
                      config_.voice_server_endpoint.address().to_string());
    }
}

// ============================================================
// 连接管理
// ============================================================

boost::asio::awaitable<Result<void>> NetworkManager::connect(
    const std::string& host,
    uint16_t tcp_port)
{
    NEVO_LOG_INFO("network", "Connecting to {}:{} ...", host, tcp_port);

    // ------------------------------------------------------------------
    // 1. 创建 TcpConnection 并异步连接
    // ------------------------------------------------------------------
    tcp_conn_ = std::make_shared<TcpConnection>(io_ctx_);

    auto ec = co_await tcp_conn_->asyncConnect(host, tcp_port);
    if (ec) {
        NEVO_LOG_ERROR("network", "TCP connection failed: {}", ec.message());
        co_return Err<void>(ResultCode::ConnectionFailed,
                           "TCP connection to " + host + ":" + std::to_string(tcp_port) +
                           " failed: " + ec.message());
    }

    // ------------------------------------------------------------------
    // 2. TLS 握手（仅当 use_tls=true 时）
    // ------------------------------------------------------------------
    // 客户端和服务器需同时启用 TLS，否则使用明文 TCP。
    // 当 use_tls=true 且编译时启用了 OpenSSL 时，将已连接的 TCP socket
    // 升级为 TLS 连接。使用 SslWrapper 提供的安全加固 SSL 上下文。
#ifdef NEVO_HAS_OPENSSL
    if (config_.use_tls) {
        if (config_.ssl_options.hostname.empty()) {
            config_.ssl_options.hostname = host;  // SNI hostname
        }

        // 使用 SslWrapper 的安全加固 SSL 上下文（而非裸 ssl::context）
        // SslWrapper 会设置 TLS 1.2 最低版本、安全密码套件、CA 证书等
        if (config_.skip_tls_verify) {
            config_.ssl_options.verify_mode = SslWrapper::VerifyMode::SkipVerify;
        } else {
            config_.ssl_options.verify_mode = SslWrapper::VerifyMode::FullVerify;
        }
        ssl_wrapper_ = std::make_unique<SslWrapper>(io_ctx_, config_.ssl_options);

        NEVO_LOG_INFO("network", "Performing TLS handshake (SNI={}, verify={})",
                      config_.ssl_options.hostname,
                      config_.skip_tls_verify ? "skip" : "full");

        auto ssl_ec = co_await tcp_conn_->asyncSslHandshake(
            ssl_wrapper_->sslContext(),
            config_.ssl_options.hostname,
            config_.skip_tls_verify);

        if (ssl_ec) {
            NEVO_LOG_ERROR("network", "TLS handshake failed: {}", ssl_ec.message());
            co_return Err<void>(ResultCode::ConnectionFailed,
                               "TLS handshake to " + host + ":" + std::to_string(tcp_port) +
                               " failed: " + ssl_ec.message());
        }

        NEVO_LOG_INFO("network", "TLS connection established to {}:{}", host, tcp_port);
    } else {
        NEVO_LOG_INFO("network", "TLS not requested, using plain TCP to {}:{}", host, tcp_port);
    }
#else
    if (config_.use_tls) {
        NEVO_LOG_WARN("network", "TLS requested but OpenSSL not available, using plain TCP");
    }
    NEVO_LOG_INFO("network", "Connected to {}:{} (plain TCP)", host, tcp_port);
#endif

    // ------------------------------------------------------------------
    // 3. 设置 TCP 消息回调，启动读取循环
    // ------------------------------------------------------------------
    tcp_conn_->onMessage = [this](std::vector<uint8_t> data, uint32_t msg_type, uint32_t request_id) {
        handleTcpMessage(std::move(data), msg_type, request_id);
    };

    tcp_conn_->onDisconnected = [this]() {
        NEVO_LOG_WARN("network", "TCP connection lost");
        tcp_connected_.store(false, std::memory_order_release);
        if (onDisconnected) {
            onDisconnected();
        }
    };

    // 在独立协程中启动 TCP 读取循环
    boost::asio::co_spawn(io_ctx_,
        [conn = tcp_conn_]() -> boost::asio::awaitable<void> {
            co_await conn->asyncReadLoop();
        },
        boost::asio::detached);

    tcp_connected_.store(true, std::memory_order_release);
    NEVO_LOG_INFO("network", "Connected to {}:{}", host, tcp_port);

    co_return Ok();
}

void NetworkManager::disconnect()
{
    NEVO_LOG_INFO("network", "Disconnecting...");

    // 1. 关闭 UDP 接收（关闭 socket 即可停止接收循环协程）
    if (udp_socket_ && udp_socket_->isOpen()) {
        udp_socket_->close();
        NEVO_LOG_INFO("network", "UDP socket closed");
    }

    // 2. 关闭 TCP 连接
    if (tcp_conn_ && tcp_conn_->isConnected()) {
        tcp_conn_->close();
        NEVO_LOG_INFO("network", "TCP connection closed");
    }

    // 3. 重置状态
    tcp_connected_.store(false, std::memory_order_release);
    udp_channel_mode_.store(UdpChannelMode::None, std::memory_order_release);
    mapped_endpoint_.reset();
    turn_relay_endpoint_.reset();

    // 4. 重置 TCP 语音隧道
    tcp_voice_tunnel_.reset();

    NEVO_LOG_INFO("network", "Disconnected");
}

// ============================================================
// UDP 语音通道建立 - NAT 穿透级联
// ============================================================

boost::asio::awaitable<Result<void>> NetworkManager::establishUdpChannel(
    uint16_t local_port)
{
    NEVO_LOG_INFO("network", "Establishing UDP voice channel on local port {} ...", local_port);

    // ------------------------------------------------------------------
    // 阶段 1：STUN 探测
    // ------------------------------------------------------------------
    auto probe_result = co_await stunProbe();
    if (!probe_result) {
        NEVO_LOG_WARN("network", "STUN probe failed: {}", probe_result.error().message());
        // STUN 探测失败并不意味着无法通信，继续尝试
    }

    NatType nat_type = nat_info_.type;
    NEVO_LOG_INFO("network", "NAT type detected: {}", natTypeToString(nat_type));

    // ------------------------------------------------------------------
    // 阶段 2：根据 NAT 类型选择穿透策略
    // ------------------------------------------------------------------
    switch (nat_type) {
        case NatType::Open:
        case NatType::FullCone: {
            // 开放网络或 Full Cone NAT：UDP 直连即可
            NEVO_LOG_INFO("network", "NAT type allows direct UDP, attempting direct connection");

            // 绑定 UDP socket
            udp_socket_ = std::make_shared<UdpSocket>(io_ctx_);
            auto bind_ec = udp_socket_->bind(local_port);
            if (bind_ec) {
                NEVO_LOG_ERROR("network", "UDP bind failed: {}", bind_ec.message());
                // 绑定失败，尝试 TCP 隧道
                auto tunnel_result = co_await fallbackToTcpTunnel();
                co_return tunnel_result;
            }

            // 对于 Open/FullCone，直接发送 UDP ping 到服务器语音端点
            if (config_.voice_server_endpoint != boost::asio::ip::udp::endpoint{}) {
                auto punch_ok = co_await holePunching();
                if (punch_ok) {
                    udp_channel_mode_.store(UdpChannelMode::DirectUdp, std::memory_order_release);
                    NEVO_LOG_INFO("network", "Direct UDP channel established");

                    // 启动 UDP 接收循环
                    boost::asio::co_spawn(io_ctx_,
                        [this]() -> boost::asio::awaitable<void> {
                            co_await udpReceiveLoop();
                        },
                        boost::asio::detached);

                    co_await sendUdpRegistrationPacket();
                    co_return Ok();
                }
            }

            // 直接连接失败，尝试打洞
            NEVO_LOG_INFO("network", "Direct UDP failed, trying hole punching");
            [[fallthrough]];
        }

        case NatType::Restricted:
        case NatType::PortRestricted: {
            // 受限 NAT：尝试 UDP 打洞
            if (udp_channel_mode_.load(std::memory_order_acquire) == UdpChannelMode::DirectUdp) {
                co_return Ok();
            }

            if (!udp_socket_) {
                udp_socket_ = std::make_shared<UdpSocket>(io_ctx_);
                auto bind_ec = udp_socket_->bind(local_port);
                if (bind_ec) {
                    NEVO_LOG_ERROR("network", "UDP bind failed: {}", bind_ec.message());
                    auto tunnel_result = co_await fallbackToTcpTunnel();
                    co_return tunnel_result;
                }
            }

            auto punch_ok = co_await holePunching();
            if (punch_ok) {
                udp_channel_mode_.store(UdpChannelMode::HolePunched, std::memory_order_release);
                NEVO_LOG_INFO("network", "UDP hole punching succeeded");

                // 启动 UDP 接收循环
                boost::asio::co_spawn(io_ctx_,
                    [this]() -> boost::asio::awaitable<void> {
                        co_await udpReceiveLoop();
                    },
                    boost::asio::detached);

                co_await sendUdpRegistrationPacket();
                co_return Ok();
            }

            // 打洞失败，尝试 TURN
            NEVO_LOG_INFO("network", "UDP hole punching failed, trying TURN relay");
            [[fallthrough]];
        }

        case NatType::Symmetric: {
            // 对称 NAT：直接使用 TURN 中继
            if (udp_channel_mode_.load(std::memory_order_acquire) != UdpChannelMode::None) {
                co_return Ok();  // 已经在上一阶段成功
            }

            auto turn_result = co_await fallbackToTurn();
            if (turn_result) {
                co_return turn_result;
            }

            // TURN 也失败，回退到 TCP 隧道
            NEVO_LOG_WARN("network", "TURN relay failed, falling back to TCP tunnel");
            [[fallthrough]];
        }

        case NatType::Blocked: {
            // UDP 被完全阻断或所有 UDP 方式均失败
            if (udp_channel_mode_.load(std::memory_order_acquire) != UdpChannelMode::None) {
                co_return Ok();  // 已经在上一阶段成功
            }

            auto tunnel_result = co_await fallbackToTcpTunnel();
            co_return tunnel_result;
        }
    }

    co_return Err<void>(ResultCode::NatTraversalFailed,
                       "All NAT traversal methods failed");
}

// ============================================================
// NAT 穿透级联 - 各阶段实现
// ============================================================

boost::asio::awaitable<Result<void>> NetworkManager::stunProbe()
{
    NEVO_LOG_INFO("network", "Starting STUN probe...");

    // 至少需要一个 STUN 服务器配置
    if (config_.stun_servers.empty()) {
        NEVO_LOG_WARN("network", "No STUN servers configured, skipping probe");
        nat_info_.type = NatType::Blocked;
        nat_info_.udp_reachable = false;
        co_return Err<void>(ResultCode::NatTraversalFailed,
                           "No STUN servers configured");
    }

    const auto& [stun_host, stun_port] = config_.stun_servers[0];

    // 如果有第二个 STUN 服务器，使用双服务器探测以提高准确性
    std::string stun_host2;
    uint16_t stun_port2 = 0;
    if (config_.stun_servers.size() > 1) {
        stun_host2 = config_.stun_servers[1].first;
        stun_port2 = config_.stun_servers[1].second;
    }

    try {
        nat_info_ = co_await nat_traversal_->probeStun(
            stun_host, stun_port, stun_host2, stun_port2);

        if (nat_info_.udp_reachable && nat_info_.mapped_endpoint !=
            boost::asio::ip::udp::endpoint{}) {
            mapped_endpoint_ = nat_info_.mapped_endpoint;
            NEVO_LOG_INFO("network", "STUN probe result: NAT type={}, mapped={}:{}",
                         natTypeToString(nat_info_.type),
                         nat_info_.mapped_endpoint.address().to_string(),
                         nat_info_.mapped_endpoint.port());
        } else {
            NEVO_LOG_WARN("network", "STUN probe: UDP not reachable");
        }

        co_return Ok();
    } catch (const std::exception& e) {
        NEVO_LOG_ERROR("network", "STUN probe exception: {}", e.what());
        nat_info_.type = NatType::Blocked;
        nat_info_.udp_reachable = false;
        co_return Err<void>(ResultCode::NatTraversalFailed,
                           std::string("STUN probe failed: ") + e.what());
    }
}

boost::asio::awaitable<bool> NetworkManager::holePunching()
{
    NEVO_LOG_INFO("network", "Attempting UDP hole punching...");

    if (!udp_socket_ || !udp_socket_->isOpen()) {
        NEVO_LOG_ERROR("network", "UDP socket not available for hole punching");
        co_return false;
    }

    // 向服务器语音端点发送打洞 ping
    const auto& target = config_.voice_server_endpoint;
    if (target == boost::asio::ip::udp::endpoint{}) {
        NEVO_LOG_WARN("network", "No voice server endpoint configured for hole punching");
        co_return false;
    }

    try {
        bool success = co_await nat_traversal_->punchUdp(
            udp_socket_->socket(), target);

        if (success) {
            NEVO_LOG_INFO("network", "UDP hole punching succeeded");
        } else {
            NEVO_LOG_WARN("network", "UDP hole punching failed");
        }

        co_return success;
    } catch (const std::exception& e) {
        NEVO_LOG_ERROR("network", "Hole punching exception: {}", e.what());
        co_return false;
    }
}

boost::asio::awaitable<Result<void>> NetworkManager::fallbackToTurn()
{
    NEVO_LOG_INFO("network", "Falling back to TURN relay...");

    // 需要至少一个 TURN 服务器配置
    if (config_.turn_servers.empty()) {
        NEVO_LOG_WARN("network", "No TURN servers configured");
        co_return Err<void>(ResultCode::NatTraversalFailed,
                           "No TURN servers configured");
    }

    // 确保 UDP socket 已创建并绑定
    if (!udp_socket_ || !udp_socket_->isOpen()) {
        udp_socket_ = std::make_shared<UdpSocket>(io_ctx_);
        auto bind_ec = udp_socket_->bind(0);
        if (bind_ec) {
            NEVO_LOG_ERROR("network", "UDP bind for TURN failed: {}", bind_ec.message());
            co_return Err<void>(ResultCode::NatTraversalFailed,
                               "Failed to bind UDP socket for TURN: " + bind_ec.message());
        }
    }

    // 逐个尝试 TURN 服务器
    for (const auto& turn_server : config_.turn_servers) {
        NEVO_LOG_INFO("network", "Trying TURN server {}:{}",
                     turn_server.host, turn_server.port);

        try {
            auto result = co_await nat_traversal_->allocateTurnRelay(
                turn_server.host,
                turn_server.port,
                turn_server.credentials);

            if (result) {
                const auto& relay_info = result.value();
                turn_relay_endpoint_ = relay_info.relayed_endpoint;

                NEVO_LOG_INFO("network", "TURN relay allocated: {}:{} (lifetime={}s)",
                             relay_info.relayed_endpoint.address().to_string(),
                             relay_info.relayed_endpoint.port(),
                             relay_info.lifetime);

                udp_channel_mode_.store(UdpChannelMode::TurnRelay, std::memory_order_release);

                // 启动 UDP 接收循环
                boost::asio::co_spawn(io_ctx_,
                    [this]() -> boost::asio::awaitable<void> {
                        co_await udpReceiveLoop();
                    },
                    boost::asio::detached);

                co_return Ok();
            } else {
                NEVO_LOG_WARN("network", "TURN allocation failed: {}",
                             result.error().message());
            }
        } catch (const std::exception& e) {
            NEVO_LOG_ERROR("network", "TURN exception for {}:{}: {}",
                          turn_server.host, turn_server.port, e.what());
        }
    }

    NEVO_LOG_ERROR("network", "All TURN servers failed");
    co_return Err<void>(ResultCode::NatTraversalFailed,
                       "All TURN relay allocations failed");
}

boost::asio::awaitable<Result<void>> NetworkManager::fallbackToTcpTunnel()
{
    NEVO_LOG_INFO("network", "Falling back to TCP voice tunnel...");

    // TCP 隧道需要已有的 TCP 控制连接
    if (!tcp_conn_ || !tcp_conn_->isConnected()) {
        NEVO_LOG_ERROR("network", "No TCP connection available for voice tunnel");
        co_return Err<void>(ResultCode::ConnectionFailed,
                           "No TCP connection available for voice tunnel");
    }

    // 重置 TCP 语音隧道状态
    tcp_voice_tunnel_.reset();

    // TCP 隧道模式下，语音数据通过已有的 TCP 控制连接传输
    // 发送端：使用 TcpVoiceTunnel.sendVoiceFrame() 封装 → TcpConnection.asyncSend()
    // 接收端：从 TCP 消息流中识别语音帧 → TcpVoiceTunnel.onTcpDataReceived() 重组
    udp_channel_mode_.store(UdpChannelMode::TcpTunnel, std::memory_order_release);

    NEVO_LOG_INFO("network", "TCP voice tunnel established");

    co_return Ok();
}

// ============================================================
// 数据发送
// ============================================================

boost::asio::awaitable<Result<void>> NetworkManager::sendControl(
    const control::ControlMessage& message,
    ControlMessageType type,
    uint32_t request_id)
{
    if (!tcp_conn_ || !tcp_conn_->isConnected()) {
        NEVO_LOG_ERROR("network", "Cannot send control message: TCP not connected");
        co_return Err<void>(ResultCode::ConnectionFailed,
                           "TCP not connected");
    }

    // 序列化 ControlMessage 为裸 Protobuf 载荷
    // TcpConnection::asyncSend() 负责帧头封装，此处只传裸载荷避免双重封装
    const size_t payload_size = message.ByteSizeLong();
    std::vector<uint8_t> payload(payload_size);
    if (!message.SerializeToArray(payload.data(), static_cast<int>(payload_size))) {
        NEVO_LOG_ERROR("network", "Failed to serialize control message");
        co_return Err<void>(ResultCode::Unknown, "Serialization failed");
    }

    // 通过 TCP 连接发送（asyncSend 会添加帧头）
    auto ec = co_await tcp_conn_->asyncSend(payload, static_cast<uint32_t>(type), request_id);
    if (ec) {
        NEVO_LOG_ERROR("network", "Failed to send control message: {}", ec.message());
        co_return Err<void>(ResultCode::ConnectionFailed,
                           "Failed to send control message: " + ec.message());
    }

    NEVO_LOG_DEBUG("network", "Sent control message type={} request_id={}",
                  controlMessageTypeToString(type), request_id);

    co_return Ok();
}

boost::asio::awaitable<Result<void>> NetworkManager::sendVoicePacket(
    const uint8_t* data,
    uint32_t size)
{
    UdpChannelMode mode = udp_channel_mode_.load(std::memory_order_acquire);

    // ------------------------------------------------------------------
    // 1. 构建语音包头（用作 AAD 和接收端解析）
    // ------------------------------------------------------------------
    voice::VoicePacketHeader header;
    header.set_sequence_number(voice_sequence_.fetch_add(1, std::memory_order_relaxed));
    header.set_sender_id(local_user_id_.value);
    header.set_channel_id(current_channel_id_.value);
    header.set_timestamp(static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()));
    header.set_tcp_tunnel(mode == UdpChannelMode::TcpTunnel);

    // 序列化包头
    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    if (!header.SerializeToArray(header_buf.data(), static_cast<int>(header_size))) {
        NEVO_LOG_ERROR("network", "Failed to serialize voice packet header");
        co_return Err<void>(ResultCode::Unknown, "Voice header serialization failed");
    }

    // ------------------------------------------------------------------
    // 2. 加密语音数据（使用包头作为 AAD）
    // ------------------------------------------------------------------
    std::vector<uint8_t> encrypted = voice_crypto_.encrypt(
        data, size,
        header_buf.data(), header_buf.size());

    // ------------------------------------------------------------------
    // 3. 组装完整语音包：[2-byte prefix][header][encrypted_payload]
    //     2-byte prefix = uint16_t header_size (little-endian)
    //     与 PacketCodec::encodeVoicePacket 格式一致，确保服务端
    //     decodeVoicePacketHeader 能正确解析
    // ------------------------------------------------------------------
    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::vector<uint8_t> full_packet;
    full_packet.reserve(2 + header_buf.size() + encrypted.size());
    full_packet.insert(full_packet.end(),
                       reinterpret_cast<const uint8_t*>(&header_len),
                       reinterpret_cast<const uint8_t*>(&header_len) + 2);
    full_packet.insert(full_packet.end(), header_buf.begin(), header_buf.end());
    full_packet.insert(full_packet.end(), encrypted.begin(), encrypted.end());

    // ------------------------------------------------------------------
    // 2. 根据通道模式选择发送方式（循环重试，替代递归调用）
    // ------------------------------------------------------------------
    const int MAX_SEND_RETRIES = 2;
    for (int attempt = 0; attempt < MAX_SEND_RETRIES; ++attempt) {
        UdpChannelMode current_mode = udp_channel_mode_.load(std::memory_order_acquire);

        switch (current_mode) {
            case UdpChannelMode::DirectUdp:
            case UdpChannelMode::HolePunched: {
                // 直接通过 UDP 发送到服务器语音端点
                if (!udp_socket_ || !udp_socket_->isOpen()) {
                    NEVO_LOG_ERROR("network", "UDP socket not available for voice send");
                    co_return Err<void>(ResultCode::ConnectionFailed,
                                       "UDP socket not available");
                }

                const auto& target = config_.voice_server_endpoint;
                auto ec = co_await udp_socket_->asyncSendTo(full_packet, target);
                if (ec) {
                    NEVO_LOG_WARN("network", "UDP voice send failed: {}, falling back to TCP tunnel (attempt {}/{})",
                                 ec.message(), attempt + 1, MAX_SEND_RETRIES);
                    // UDP 发送失败，切换到 TCP 隧道并重试
                    udp_channel_mode_.store(UdpChannelMode::TcpTunnel, std::memory_order_release);
                    continue;  // 循环重试（下次迭代走 TcpTunnel 分支）
                }
                co_return Ok();
            }

            case UdpChannelMode::TurnRelay: {
                // 通过 TURN 中继端点发送
                if (!udp_socket_ || !udp_socket_->isOpen()) {
                    NEVO_LOG_ERROR("network", "UDP socket not available for TURN send");
                    co_return Err<void>(ResultCode::ConnectionFailed,
                                       "UDP socket not available for TURN");
                }

                if (!turn_relay_endpoint_.has_value()) {
                    NEVO_LOG_ERROR("network", "TURN relay endpoint not available");
                    co_return Err<void>(ResultCode::NatTraversalFailed,
                                       "TURN relay endpoint not available");
                }

                auto ec = co_await udp_socket_->asyncSendTo(full_packet, *turn_relay_endpoint_);
                if (ec) {
                    NEVO_LOG_WARN("network", "TURN voice send failed: {}", ec.message());
                    co_return Err<void>(ResultCode::ConnectionFailed,
                                       "TURN voice send failed: " + ec.message());
                }
                co_return Ok();
            }

            case UdpChannelMode::TcpTunnel: {
                // 通过 TCP 语音隧道发送
                if (!tcp_conn_ || !tcp_conn_->isConnected()) {
                    NEVO_LOG_ERROR("network", "TCP not connected for voice tunnel send");
                    co_return Err<void>(ResultCode::ConnectionFailed,
                                       "TCP not connected for voice tunnel");
                }

                // 封装为 TCP 语音帧
                std::vector<uint8_t> tunnel_frame =
                    tcp_voice_tunnel_.sendVoiceFrame(full_packet.data(), full_packet.size());

                // 通过 TCP 连接发送（使用语音帧类型 0xFF）
                auto ec = co_await tcp_conn_->asyncSend(
                    tunnel_frame,
                    TCP_VOICE_FRAME_TYPE,
                    0);  // voice frames don't use request_id
                if (ec) {
                    NEVO_LOG_ERROR("network", "TCP tunnel voice send failed: {}", ec.message());
                    co_return Err<void>(ResultCode::ConnectionFailed,
                                       "TCP tunnel voice send failed: " + ec.message());
                }
                co_return Ok();
            }

            case UdpChannelMode::None: {
                NEVO_LOG_WARN("network", "No voice channel available, dropping voice packet");
                co_return Err<void>(ResultCode::NatTraversalFailed,
                                   "No voice channel established");
            }
        }
    }

    co_return Err<void>(ResultCode::ConnectionFailed, "Voice send failed after retries");
}

// ============================================================
// 密钥管理
// ============================================================

void NetworkManager::setSessionKey(const uint8_t key[CRYPTO_KEY_SIZE])
{
    voice_crypto_.setSessionKey(key);
    NEVO_LOG_INFO("network", "Session key set");
}

void NetworkManager::rotateKey(const uint8_t new_key[CRYPTO_KEY_SIZE])
{
    voice_crypto_.rotateKey(new_key);
    NEVO_LOG_INFO("network", "Session key rotated");
}

// ============================================================
// 状态查询
// ============================================================

NatType NetworkManager::detectedNatType() const
{
    return nat_info_.type;
}

boost::asio::io_context& NetworkManager::ioContext()
{
    return io_ctx_;
}

bool NetworkManager::isUdpAvailable() const
{
    auto mode = udp_channel_mode_.load(std::memory_order_acquire);
    return mode != UdpChannelMode::None && mode != UdpChannelMode::TcpTunnel;
}

bool NetworkManager::isTcpConnected() const
{
    return tcp_connected_.load(std::memory_order_acquire);
}

UdpChannelMode NetworkManager::udpChannelMode() const
{
    return udp_channel_mode_.load(std::memory_order_acquire);
}

VoiceCrypto& NetworkManager::voiceCrypto()
{
    return voice_crypto_;
}

const VoiceCrypto& NetworkManager::voiceCrypto() const
{
    return voice_crypto_;
}

NetworkManagerConfig& NetworkManager::config()
{
    return config_;
}

const NetworkManagerConfig& NetworkManager::config() const
{
    return config_;
}

const std::optional<boost::asio::ip::udp::endpoint>&
NetworkManager::turnRelayEndpoint() const
{
    return turn_relay_endpoint_;
}

const std::optional<boost::asio::ip::udp::endpoint>&
NetworkManager::mappedEndpoint() const
{
    return mapped_endpoint_;
}

// ============================================================
// 内部处理
// ============================================================

void NetworkManager::handleTcpMessage(std::vector<uint8_t> data,
                                       uint32_t msg_type,
                                       uint32_t request_id)
{
    // TcpConnection::asyncReadLoop 已剥离帧头，data 为裸 Protobuf 载荷
    // msg_type 和 request_id 已从帧头中解析并传递

    // 检查是否为 TCP 语音帧
    if (msg_type == TCP_VOICE_FRAME_TYPE) {
        tcp_voice_tunnel_.onTcpDataReceived(data.data(), data.size());
        return;
    }

    // 控制消息：直接反序列化 Protobuf 载荷
    control::ControlMessage message;
    if (!message.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        NEVO_LOG_WARN("network", "Failed to parse control message payload");
        return;
    }

    ControlMessageType type = static_cast<ControlMessageType>(msg_type);

    NEVO_LOG_DEBUG("network", "Received control message type={} request_id={}",
                  controlMessageTypeToString(type), request_id);

    // 触发控制消息回调
    if (onControlMessage) {
        onControlMessage(message, type, request_id);
    }
}

void NetworkManager::handleUdpPacket(
    const uint8_t* data,
    uint32_t size,
    const boost::asio::ip::udp::endpoint& sender)
{
    if (size == 0) {
        return;
    }

    // ------------------------------------------------------------------
    // 尝试解析为 STUN 响应（NAT 探测期间的响应包）
    // ------------------------------------------------------------------
    if (size >= STUN_HEADER_SIZE) {
        // 检查 Magic Cookie 判断是否为 STUN 消息
        uint32_t magic_cookie = 0;
        if (size >= 4) {
            std::memcpy(&magic_cookie, data + 4, 4);
            // 转换为主机字节序（STUN 使用大端序）
            magic_cookie = ntohl(magic_cookie);
        }
        if (magic_cookie == STUN_MAGIC_COOKIE) {
            // STUN 响应，由 NatTraversal 处理，此处忽略
            NEVO_LOG_TRACE("network", "Received STUN response from {}",
                          sender.address().to_string());
            return;
        }
    }

    // ------------------------------------------------------------------
    // 解析为语音包
    // ------------------------------------------------------------------
    // 尝试解码 UDP 语音包头
    uint32_t header_size = 0;
    auto voice_header = decodeVoicePacketHeader(data, size, header_size);

    if (voice_header.has_value()) {
        // 成功解析语音包头，提取 sender_id
        UserId sender_id(voice_header->sender_id());

        // 提取加密载荷
        auto [payload_ptr, payload_size] = getVoicePayload(
            data, header_size, size);

        if (payload_ptr && payload_size > 0) {
            decryptAndDeliverVoicePacket(
                payload_ptr, payload_size,
                data + 2, header_size - 2,
                sender_id);
        }
    } else {
        decryptAndDeliverVoicePacket(
            data, size,
            nullptr, 0,
            UserId(0));
    }
}

void NetworkManager::handleTunnelVoiceFrame(const uint8_t* data, size_t size)
{
    // TCP 语音隧道重组出的帧，需要解密后投递
    // 隧道帧格式与 UDP 相同：[header][encrypted_payload]
    if (size == 0) {
        return;
    }

    // 尝试解析语音包头
    uint32_t header_size = 0;
    auto voice_header = decodeVoicePacketHeader(
        data, static_cast<uint32_t>(size), header_size);

    if (voice_header.has_value()) {
        UserId sender_id(voice_header->sender_id());
        auto [payload_ptr, payload_size] = getVoicePayload(
            data, header_size, static_cast<uint32_t>(size));

        if (payload_ptr && payload_size > 0) {
            decryptAndDeliverVoicePacket(
                payload_ptr, payload_size,
                data + 2, header_size - 2,
                sender_id);
        }
    } else {
        decryptAndDeliverVoicePacket(
            data, static_cast<uint32_t>(size),
            nullptr, 0,
            UserId(0));
    }
}

void NetworkManager::decryptAndDeliverVoicePacket(
    const uint8_t* encrypted_data,
    uint32_t encrypted_size,
    const uint8_t* header_aad,
    uint32_t aad_size,
    UserId sender_id)
{
    // 加密帧格式：[nonce (24 bytes)][ciphertext][auth tag (16 bytes)]
    constexpr size_t nonce_size = XCHACHA_NONCE_SIZE;

    if (encrypted_size <= nonce_size + POLY1305_TAG_SIZE) {
        NEVO_LOG_WARN("network", "Voice packet too short for decryption: {} bytes",
                     encrypted_size);
        return;
    }

    const uint8_t* nonce = encrypted_data;
    const uint8_t* ciphertext = encrypted_data + nonce_size;
    size_t ct_len = encrypted_size - nonce_size;

    // 解密
    auto plaintext = voice_crypto_.decrypt(
        ciphertext, ct_len,
        nonce, nonce_size,
        header_aad, aad_size);

    if (!plaintext.has_value()) {
        NEVO_LOG_WARN("network", "Voice packet decryption failed (auth failure)");
        return;
    }

    // 定期清理过期旧密钥
    voice_crypto_.purgeExpiredOldKey();

    // 触发语音包回调（已解密，携带发送者 ID）
    if (onVoicePacket) {
        onVoicePacket(plaintext->data(),
                     static_cast<uint32_t>(plaintext->size()),
                     sender_id);
    }
}

boost::asio::awaitable<void> NetworkManager::udpReceiveLoop()
{
    NEVO_LOG_INFO("network", "UDP receive loop started");

    // 设置 UDP 包接收回调
    if (udp_socket_) {
        udp_socket_->onPacket = [this](const uint8_t* data, uint32_t size,
                                        const boost::asio::ip::udp::endpoint& sender) {
            handleUdpPacket(data, size, sender);
        };

        // 启动 UdpSocket 的接收循环
        co_await udp_socket_->asyncReceiveFrom();
    }

    NEVO_LOG_INFO("network", "UDP receive loop ended");
}

} // namespace nevo
