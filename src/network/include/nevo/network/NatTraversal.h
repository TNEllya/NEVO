#pragma once
/**
 * @file NatTraversal.h
 * @brief STUN/TURN NAT 穿透模块
 *
 * 提供 NAT 类型探测、UDP 打洞、TURN 中继分配功能。
 * 基于 RFC 5389 (STUN) 简化实现，使用 Boost.Asio awaitable 协程。
 *
 * NAT 类型探测算法：
 *   1. 从同一本地端口向两个不同的 STUN 服务器发送 Binding Request
 *   2. 比较两次返回的映射地址是否一致
 *   3. 结合额外测试判断 Full Cone / Restricted / Port Restricted / Symmetric
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <array>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "nevo/core/common/Result.h"

namespace nevo {

// ============================================================
// STUN 协议常量 (RFC 5389)
// ============================================================

/// STUN Binding Request 类型
inline constexpr uint16_t STUN_BINDING_REQUEST = 0x0001;

/// STUN Binding Response 类型
inline constexpr uint16_t STUN_BINDING_RESPONSE = 0x0101;

/// STUN Binding Indication 类型
inline constexpr uint16_t STUN_BINDING_INDICATION = 0x0011;

/// STUN Allocate Request 类型 (TURN, RFC 5766)
inline constexpr uint16_t STUN_ALLOCATE_REQUEST = 0x0003;

/// STUN Allocate Response 类型 (TURN)
inline constexpr uint16_t STUN_ALLOCATE_RESPONSE = 0x0103;

/// STUN 属性：MAPPED-ADDRESS
inline constexpr uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;

/// STUN 属性：XOR-MAPPED-ADDRESS (RFC 5389)
inline constexpr uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;

/// STUN 属性：SOFTWARE
inline constexpr uint16_t STUN_ATTR_SOFTWARE = 0x8022;

/// STUN 属性：MESSAGE-INTEGRITY
inline constexpr uint16_t STUN_ATTR_MESSAGE_INTEGRITY = 0x0008;

/// STUN 属性：FINGERPRINT
inline constexpr uint16_t STUN_ATTR_FINGERPRINT = 0x8028;

/// STUN 属性：LIFETIME (TURN)
inline constexpr uint16_t STUN_ATTR_LIFETIME = 0x000D;

/// STUN 属性：XOR-RELAYED-ADDRESS (TURN)
inline constexpr uint16_t STUN_ATTR_XOR_RELAYED_ADDRESS = 0x0016;

/// STUN Magic Cookie (RFC 5389)
inline constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

/// STUN 消息头固定长度（20 字节）
inline constexpr size_t STUN_HEADER_SIZE = 20;

/// STUN 事务 ID 长度（12 字节，96 位）
inline constexpr size_t STUN_TRANSACTION_ID_SIZE = 12;

/// STUN 最大消息大小
inline constexpr size_t STUN_MAX_MESSAGE_SIZE = 576;

/// STUN 属性头大小（4 字节：2 字节类型 + 2 字节长度）
inline constexpr size_t STUN_ATTR_HEADER_SIZE = 4;

/// STUN 响应超时（毫秒）
inline constexpr uint32_t STUN_TIMEOUT_MS = 3000;

/// STUN 最大重试次数
inline constexpr uint32_t STUN_MAX_RETRIES = 3;

/// UDP 打洞 ping 次数
inline constexpr uint32_t UDP_HOLE_PUNCH_COUNT = 10;

/// UDP 打洞间隔（毫秒）
inline constexpr uint32_t UDP_HOLE_PUNCH_INTERVAL_MS = 50;

// ============================================================
// NAT 类型枚举
// ============================================================

/// NAT 类型分类（从开放到最严格）
enum class NatType {
    Open,            ///< 无 NAT，公网 IP，UDP 直连
    FullCone,        ///< 全锥形 NAT：任何外部主机均可通过映射端口访问
    Restricted,      ///< 受限锥形 NAT：仅当内部曾向该 IP 发送过包时才放行
    PortRestricted,  ///< 端口受限锥形 NAT：受限锥形 + 端口也须匹配
    Symmetric,       ///< 对称 NAT：不同目标分配不同映射端口，无法直接打洞
    Blocked,         ///< UDP 完全被阻断
};

/// NAT 类型转字符串（用于日志和调试）
inline const char* natTypeToString(NatType type) {
    switch (type) {
        case NatType::Open:           return "Open";
        case NatType::FullCone:       return "FullCone";
        case NatType::Restricted:     return "Restricted";
        case NatType::PortRestricted: return "PortRestricted";
        case NatType::Symmetric:      return "Symmetric";
        case NatType::Blocked:        return "Blocked";
        default:                      return "Unknown";
    }
}

// ============================================================
// NAT 信息结构
// ============================================================

/// NAT 探测结果
struct NatInfo {
    NatType type = NatType::Blocked;                ///< 检测到的 NAT 类型
    boost::asio::ip::udp::endpoint mapped_endpoint; ///< STUN 返回的映射端点（公网 IP:Port）
    bool udp_reachable = false;                      ///< UDP 是否可达（能收到 STUN 响应）
};

// ============================================================
// STUN 消息编解码辅助
// ============================================================

/// STUN 消息头部（20 字节）
struct StunMessageHeader {
    uint16_t type;           ///< 消息类型
    uint16_t length;         ///< 消息长度（不含 20 字节头）
    uint32_t magic_cookie;   ///< Magic Cookie = 0x2112A442
    uint8_t  transaction_id[STUN_TRANSACTION_ID_SIZE]; ///< 事务 ID（96 位）
};

/// STUN 属性（TLV）
struct StunAttribute {
    uint16_t type;           ///< 属性类型
    uint16_t length;         ///< 属性值长度
    std::vector<uint8_t> value; ///< 属性值
};

/// 解析后的 STUN 消息
struct StunMessage {
    StunMessageHeader header;              ///< 消息头
    std::vector<StunAttribute> attributes; ///< 属性列表
};

// ============================================================
// TURN 中继凭据
// ============================================================

/// TURN 服务器认证凭据
struct TurnCredentials {
    std::string username;  ///< 用户名（RFC 5389 长期认证）
    std::string password;  ///< 密码
    std::string realm;     ///< 认证域（通常由服务器在 401 响应中提供）
    std::string nonce;     ///< 服务器随机数（通常由服务器在 401 响应中提供）
};

/// TURN 中继分配结果
struct TurnRelayInfo {
    boost::asio::ip::udp::endpoint relayed_endpoint; ///< 中继分配的转发端点
    uint32_t lifetime = 0;                           ///< 分配生命周期（秒）
};

// ============================================================
// NatTraversal 类
// ============================================================

/**
 * @class NatTraversal
 * @brief STUN/TURN NAT 穿透器
 *
 * 提供以下核心功能：
 * 1. probeStun() —— 向 STUN 服务器发送 Binding Request，探测 NAT 类型
 * 2. punchUdp()   —— UDP 打洞，尝试建立 P2P 直连
 * 3. allocateTurnRelay() —— 在 TURN 服务器上分配中继端口
 *
 * 所有网络操作均使用 Boost.Asio awaitable 协程，可在协程上下文中
 * 直接 co_await 调用。
 *
 * 典型流程：
 * @code
 *   auto nat_info = co_await nat.probeStun("stun.l.google.com", 19302);
 *   if (nat_info.type == NatType::Symmetric || nat_info.type == NatType::Blocked) {
 *       // 对称 NAT 或 UDP 被阻断，使用 TURN 中继
 *       auto relay = co_await nat.allocateTurnRelay("turn.example.com", 3478, creds);
 *   } else {
 *       // 尝试 UDP 打洞
 *       co_await nat.punchUdp(socket, peer_endpoint);
 *   }
 * @endcode
 */
class NatTraversal {
public:
    /// 构造函数
    NatTraversal();

    /// 析构函数
    ~NatTraversal();

    // ----- 禁止拷贝，允许移动 -----
    NatTraversal(const NatTraversal&) = delete;
    NatTraversal& operator=(const NatTraversal&) = delete;
    NatTraversal(NatTraversal&&) noexcept = default;
    NatTraversal& operator=(NatTraversal&&) noexcept = default;

    // ============================================================
    // NAT 探测
    // ============================================================

    /**
     * @brief 探测 NAT 类型
     *
     * 向两个不同的 STUN 服务器发送 Binding Request，比较映射地址判断 NAT 类型。
     * 算法步骤（简化 RFC 3489 经典方法 + RFC 5389 扩展）：
     *   1. 向 stun_host:stun_port 发送 Binding Request → 获得 mapped_addr_1
     *   2. 向第二个 STUN 服务器发送 Binding Request → 获得 mapped_addr_2
     *   3. 若无响应 → Blocked
     *   4. 若 mapped_addr 与本地地址相同 → Open
     *   5. 若 mapped_addr_1 == mapped_addr_2 → FullCone/Restricted/PortRestricted
     *      (进一步测试区分，简化实现默认为 FullCone)
     *   6. 若 mapped_addr_1 != mapped_addr_2 → Symmetric
     *
     * @param stun_host   第一台 STUN 服务器主机名或 IP
     * @param stun_port   第一台 STUN 服务器端口
     * @param stun_host2  第二台 STUN 服务器主机名或 IP（可选，为空则仅测试第一台）
     * @param stun_port2  第二台 STUN 服务器端口（可选）
     * @return awaitable<NatInfo> NAT 探测结果
     */
    boost::asio::awaitable<NatInfo> probeStun(
        const std::string& stun_host,
        uint16_t stun_port,
        const std::string& stun_host2 = "",
        uint16_t stun_port2 = 0);

    // ============================================================
    // UDP 打洞
    // ============================================================

    /**
     * @brief UDP 打洞（Hole Punching）
     *
     * 向 server_endpoint 发送一系列 UDP ping 包，尝试在 NAT 上打开映射端口。
     * 适用于 FullCone / Restricted / PortRestricted 类型的 NAT。
     * 对 Symmetric NAT 无效，需使用 TURN 中继。
     *
     * @param socket          已绑定的 UDP socket（需提前 bind 到本地端口）
     * @param server_endpoint 目标端点（对端公网映射地址）
     * @return awaitable<bool> 打洞是否成功（收到了对端的响应）
     */
    boost::asio::awaitable<bool> punchUdp(
        boost::asio::ip::udp::socket& socket,
        const boost::asio::ip::udp::endpoint& server_endpoint);

    // ============================================================
    // TURN 中继分配
    // ============================================================

    /**
     * @brief 分配 TURN 中继
     *
     * 向 TURN 服务器发送 Allocate Request，获取中继转发端点。
     * 首次请求通常会收到 401 Unauthorized（需要长期凭据认证），
     * 此方法会自动处理 401 重试。
     *
     * @param turn_host   TURN 服务器主机名或 IP
     * @param turn_port   TURN 服务器端口
     * @param credentials TURN 认证凭据
     * @return awaitable<Result<TurnRelayInfo>> 中继信息或错误
     */
    boost::asio::awaitable<Result<TurnRelayInfo>> allocateTurnRelay(
        const std::string& turn_host,
        uint16_t turn_port,
        const TurnCredentials& credentials);

    // ============================================================
    // STUN 消息编解码
    // ============================================================

    /**
     * @brief 编码 STUN Binding Request
     *
     * 构造 20 字节消息头 + SOFTWARE 属性，生成随机事务 ID。
     *
     * @param transaction_id [out] 生成的事务 ID（12 字节）
     * @return std::vector<uint8_t> 编码后的 STUN 消息
     */
    static std::vector<uint8_t> encodeBindingRequest(
        std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id);

    /**
     * @brief 解码 STUN 响应消息
     *
     * 解析消息头和属性列表，验证 Magic Cookie 和 FINGERPRINT（可选）。
     *
     * @param data     接收到的原始数据
     * @param data_len 数据长度
     * @return std::optional<StunMessage> 解析成功返回消息结构，失败返回 nullopt
     */
    static std::optional<StunMessage> decodeStunMessage(
        const uint8_t* data,
        size_t data_len);

    /**
     * @brief 从 STUN 响应中提取映射地址
     *
     * 优先解析 XOR-MAPPED-ADDRESS (RFC 5389)，若不存在则回退到 MAPPED-ADDRESS。
     * XOR 解码使用 transaction_id 与 Magic Cookie。
     *
     * @param message        解析后的 STUN 消息
     * @param transaction_id 原始请求的事务 ID（用于 XOR 解码）
     * @return std::optional<boost::asio::ip::udp::endpoint> 映射端点，失败返回 nullopt
     */
    static std::optional<boost::asio::ip::udp::endpoint> extractMappedAddress(
        const StunMessage& message,
        const std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id);

    /**
     * @brief 编码 TURN Allocate Request
     *
     * @param transaction_id [out] 生成的事务 ID
     * @param credentials    认证凭据（可能需要 realm/nonce，首次请求可为空）
     * @param requested_lifetime 请求的中继生命周期（秒），默认 600
     * @return std::vector<uint8_t> 编码后的 STUN 消息
     */
    static std::vector<uint8_t> encodeAllocateRequest(
        std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id,
        const TurnCredentials& credentials = {},
        uint32_t requested_lifetime = 600);

    /**
     * @brief 从 TURN Allocate Response 中提取中继地址
     *
     * 解析 XOR-RELAYED-ADDRESS 属性。
     *
     * @param message        解析后的 STUN 消息
     * @param transaction_id 原始请求的事务 ID
     * @return std::optional<boost::asio::ip::udp::endpoint> 中继端点，失败返回 nullopt
     */
    static std::optional<boost::asio::ip::udp::endpoint> extractRelayedAddress(
        const StunMessage& message,
        const std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id);

private:
    /**
     * @brief 生成随机事务 ID
     * @param transaction_id [out] 12 字节事务 ID
     */
    static void generateTransactionId(
        std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id);

    /**
     * @brief 解码 MAPPED-ADDRESS 或 XOR-MAPPED-ADDRESS 属性值
     *
     * 内部辅助：处理 IPv4/IPv6 地址族，XOR 解码逻辑。
     *
     * @param value          属性值字节
     * @param is_xor         是否为 XOR 编码
     * @param transaction_id 事务 ID（XOR 解码需要）
     * @return std::optional<boost::asio::ip::udp::endpoint> 解码后的端点
     */
    static std::optional<boost::asio::ip::udp::endpoint> decodeAddressAttribute(
        const std::vector<uint8_t>& value,
        bool is_xor,
        const std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>& transaction_id);

    /**
     * @brief 向指定 STUN/TURN 服务器发送请求并等待响应
     *
     * @param host 服务器主机名
     * @param port 服务器端口
     * @param request 发送的数据
     * @return awaitable<std::optional<std::vector<uint8_t>>> 响应数据，超时返回 nullopt
     */
    boost::asio::awaitable<std::optional<std::vector<uint8_t>>> sendAndReceive(
        const std::string& host,
        uint16_t port,
        const std::vector<uint8_t>& request);
};

} // namespace nevo
