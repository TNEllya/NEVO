#pragma once
/**
 * @file NetworkManager.h
 * @brief 客户端网络管理器
 *
 * NetworkManager 是 NEVO VoIP 客户端的网络核心，管理 TCP 控制通道和
 * UDP 语音通道的完整生命周期。核心设计原则：
 *
 *   1. NAT 穿透级联：UDP 直连 → STUN 探测 → UDP 打洞 → TURN 中继 → TCP 隧道回退
 *      逐级尝试，确保在任何网络环境下均能建立语音通道。
 *   2. 双通道分离：TCP 用于控制信令（登录、频道切换等），UDP 用于实时语音传输。
 *   3. 端到端加密：所有语音数据经由 VoiceCrypto 加密后才发送，即使 TURN 中继
 *      也无法获取明文。
 *   4. 协程驱动：所有异步网络操作使用 Boost.Asio C++20 协程，避免回调地狱。
 *
 * 连接建立流程：
 *
 *   [客户端] ──TCP/TLS──> [服务器]           控制信令
 *       |
 *       ├── STUN probe → 检测 NAT 类型
 *       ├── UDP hole punching → 尝试 P2P 直连
 *       ├── TURN relay → 中继转发（对称 NAT / 受限 NAT）
 *       └── TCP tunnel → 最终回退（UDP 完全阻断）
 *
 * 语音数据流：
 *
 *   [AudioInput] ──encode──> VoiceCrypto.encrypt ──> UdpSocket.asyncSendTo
 *                                                       |
 *                                                       └─(UDP blocked)──> TcpVoiceTunnel
 *
 *   [UdpSocket.onPacket] ──> VoiceCrypto.decrypt ──> [AudioOutput]
 *        |
 *        └─(TCP tunnel)──> TcpVoiceTunnel.onVoiceFrame ──> VoiceCrypto.decrypt ──> [AudioOutput]
 *
 * 密钥交换：
 *   登录成功后，服务器通过 TLS 控制通道下发会话密钥，
 *   NetworkManager 将密钥存入 VoiceCrypto，后续所有语音加解密使用该密钥。
 */

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

#include "nevo/core/common/Result.h"
#include "nevo/core/common/Types.h"
#include "nevo/core/protocol/PacketTypes.h"
#include "nevo/network/TcpConnection.h"
#include "nevo/network/UdpSocket.h"
#include "nevo/network/NatTraversal.h"
#include "nevo/network/VoiceCrypto.h"
#include "nevo/network/TcpVoiceTunnel.h"
#include "nevo/network/SslWrapper.h"

// Protobuf 前向声明
namespace nevo::control { class ControlMessage; }
namespace nevo::voice   { class VoicePacketHeader; }

namespace nevo {

// ============================================================
// UDP 通道模式
// ============================================================

/// UDP 语音通道的当前传输模式
enum class UdpChannelMode {
    None,           ///< 未建立 UDP 通道
    DirectUdp,      ///< UDP 直连（公网 IP / Full Cone NAT）
    HolePunched,    ///< UDP 打洞成功（Port Restricted NAT 等）
    TurnRelay,      ///< TURN 中继转发（Symmetric NAT）
    TcpTunnel,      ///< TCP 隧道回退（UDP 完全被阻断）
};

/// UDP 通道模式转字符串（用于日志和调试）
inline const char* udpChannelModeToString(UdpChannelMode mode) {
    switch (mode) {
        case UdpChannelMode::None:        return "None";
        case UdpChannelMode::DirectUdp:   return "DirectUdp";
        case UdpChannelMode::HolePunched: return "HolePunched";
        case UdpChannelMode::TurnRelay:   return "TurnRelay";
        case UdpChannelMode::TcpTunnel:   return "TcpTunnel";
        default:                          return "Unknown";
    }
}

// ============================================================
// 回调类型定义
// ============================================================

/// 控制消息回调
/// @param message  反序列化后的 ControlMessage
/// @param type     消息类型
/// @param request_id 请求关联 ID
using ControlMessageCallback = std::function<void(
    const control::ControlMessage& message,
    ControlMessageType type,
    uint32_t request_id)>;

/// 语音包回调（已解密）
/// @param data    解密后的语音数据指针
/// @param size    数据大小
/// @param sender  发送方端点
using VoicePacketCallback = std::function<void(
    const uint8_t* data,
    uint32_t size,
    const boost::asio::ip::udp::endpoint& sender)>;

/// 断开连接回调
using DisconnectedCallback = std::function<void()>;

// ============================================================
// NetworkManager 配置
// ============================================================

/// NetworkManager 配置选项
struct NetworkManagerConfig {
    /// STUN 服务器列表（用于 NAT 探测）
    std::vector<std::pair<std::string, uint16_t>> stun_servers = {
        {"stun.l.google.com", 19302},
        {"stun1.l.google.com", 19302},
    };

    /// TURN 服务器配置
    struct TurnServer {
        std::string host;
        uint16_t port = 3478;
        TurnCredentials credentials;
    };
    std::vector<TurnServer> turn_servers;

    /// TLS 配置
    SslWrapper::Options ssl_options;

    /// UDP 语音远端端点（服务器语音端口，由服务器在登录响应中告知）
    boost::asio::ip::udp::endpoint voice_server_endpoint;

    /// 是否跳过 TLS 证书验证（仅开发环境使用）
    bool skip_tls_verify = false;
};

// ============================================================
// NetworkManager 类
// ============================================================

/**
 * @class NetworkManager
 * @brief 客户端网络管理器
 *
 * 管理 TCP 控制连接和 UDP 语音通道，提供 NAT 穿透级联策略。
 * 所有异步操作均使用 Boost.Asio C++20 协程。
 *
 * 线程安全说明：
 *   - connect()/disconnect()/establishUdpChannel() 应在主线程或 io_context 线程调用
 *   - sendControl()/sendVoicePacket() 线程安全
 *   - 回调在 io_context 线程中触发
 *
 * 典型用法：
 * @code
 *   NetworkManager netmgr(io_ctx);
 *   netmgr.onControlMessage = [&](auto& msg, auto type, auto rid) { ... };
 *   netmgr.onVoicePacket = [&](auto* data, auto size, auto& sender) { ... };
 *   netmgr.onDisconnected = [&]() { ... };
 *
 *   co_await netmgr.connect("server.example.com", 8080);
 *   // ... 登录成功后设置密钥 ...
 *   co_await netmgr.establishUdpChannel(0);
 * @endcode
 */
class NetworkManager {
public:
    /// 构造函数
    /// @param io_ctx Boost.Asio I/O 上下文引用
    explicit NetworkManager(boost::asio::io_context& io_ctx);

    /// 析构函数：确保所有连接关闭
    ~NetworkManager();

    // 禁止拷贝和移动
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    NetworkManager(NetworkManager&&) = delete;
    NetworkManager& operator=(NetworkManager&&) = delete;

    // ============================================================
    // 连接管理
    // ============================================================

    /**
     * @brief 建立 TCP 连接并完成 TLS 握手
     *
     * 连接流程：
     *   1. 创建 TcpConnection 并异步连接到 host:tcp_port
     *   2. 使用 SslWrapper 进行 TLS 握手
     *   3. 启动 TCP 读取循环，监听控制消息
     *
     * @param host     服务器主机名或 IP
     * @param tcp_port 服务器 TCP 端口
     * @return awaitable<Result<void>> 连接结果
     */
    boost::asio::awaitable<Result<void>> connect(
        const std::string& host,
        uint16_t tcp_port);

    /**
     * @brief 关闭所有连接
     *
     * 关闭顺序：
     *   1. 停止 UDP 接收循环
     *   2. 关闭 UdpSocket
     *   3. 关闭 TcpConnection
     *   4. 重置所有状态
     */
    void disconnect();

    /**
     * @brief 建立 UDP 语音通道
     *
     * NAT 穿透级联策略：
     *   1. stunProbe():     向 STUN 服务器探测 NAT 类型
     *   2. holePunching():  根据 NAT 类型尝试 UDP 打洞
     *   3. fallbackToTurn(): TURN 中继回退（对称 NAT / 打洞失败）
     *   4. fallbackToTcpTunnel(): TCP 隧道回退（UDP 完全被阻断）
     *
     * 每一级成功即停止，失败则自动尝试下一级。
     *
     * @param local_port 本地 UDP 端口（0 = OS 自动分配）
     * @return awaitable<Result<void>> 通道建立结果
     */
    boost::asio::awaitable<Result<void>> establishUdpChannel(uint16_t local_port = 0);

    // ============================================================
    // 数据发送
    // ============================================================

    /**
     * @brief 发送控制消息（TCP 通道）
     *
     * 将 ControlMessage 通过 PacketCodec 序列化为 TCP 帧，
     * 经 TLS 加密后发送。
     *
     * @param message    控制消息（Protobuf 对象）
     * @param type       消息类型
     * @param request_id 请求关联 ID
     * @return awaitable<Result<void>> 发送结果
     */
    boost::asio::awaitable<Result<void>> sendControl(
        const control::ControlMessage& message,
        ControlMessageType type,
        uint32_t request_id = 0);

    /**
     * @brief 发送语音数据包
     *
     * 优先通过 UDP 通道发送（加密后），若 UDP 不可用则
     * 通过 TCP 语音隧道发送。
     *
     * 发送流程：
     *   1. VoiceCrypto.encrypt() 加密语音数据
     *   2. 若 UDP 可用 → UdpSocket.asyncSendTo()
     *   3. 若 UDP 不可用 → TcpVoiceTunnel.sendVoiceFrame()
     *
     * @param data 语音数据指针（Opus 编码后的明文）
     * @param size 数据大小
     * @return awaitable<Result<void>> 发送结果
     */
    boost::asio::awaitable<Result<void>> sendVoicePacket(
        const uint8_t* data,
        uint32_t size);

    // ============================================================
    // 密钥管理
    // ============================================================

    /**
     * @brief 设置会话密钥
     *
     * 登录成功后由服务器通过 TLS 通道下发密钥，存入 VoiceCrypto。
     * 后续所有语音加解密使用该密钥。
     *
     * @param key 32 字节会话密钥
     */
    void setSessionKey(const uint8_t key[CRYPTO_KEY_SIZE]);

    /**
     * @brief 密钥轮换
     *
     * 服务器周期性发起密钥轮换，新密钥下发后调用此方法。
     * 旧密钥保留一段重叠期以处理过渡期数据包。
     *
     * @param new_key 32 字节新密钥
     */
    void rotateKey(const uint8_t new_key[CRYPTO_KEY_SIZE]);

    // ============================================================
    // 状态查询
    // ============================================================

    /// 获取检测到的 NAT 类型
    /// @return NAT 类型，未探测时返回 NatType::Blocked
    NatType detectedNatType() const;

    /// 查询 UDP 通道是否可用
    /// @return true 表示 UDP 语音通道已建立
    bool isUdpAvailable() const;

    /// 查询 TCP 连接是否已建立
    /// @return true 表示 TCP 控制通道已连接
    bool isTcpConnected() const;

    /// 获取当前 UDP 通道模式
    /// @return UDP 通道传输模式
    UdpChannelMode udpChannelMode() const;

    /// 获取 VoiceCrypto 引用（用于外部加解密）
    VoiceCrypto& voiceCrypto();

    /// 获取 VoiceCrypto 常引用
    const VoiceCrypto& voiceCrypto() const;

    /// 获取配置
    NetworkManagerConfig& config();

    /// 获取配置（只读）
    const NetworkManagerConfig& config() const;

    /// 获取关联的 I/O 上下文引用（用于派发协程任务）
    boost::asio::io_context& ioContext();

    /// 获取 TURN 中继端点（已分配时有效）
    const std::optional<boost::asio::ip::udp::endpoint>& turnRelayEndpoint() const;

    /// 获取 STUN 映射端点（已探测时有效）
    const std::optional<boost::asio::ip::udp::endpoint>& mappedEndpoint() const;

    /// 设置本地用户 ID（登录成功后由 ClientCore 调用）
    void setLocalUserId(UserId user_id) { local_user_id_ = user_id; }

    /// 设置当前频道 ID（加入频道后由 ClientCore 调用）
    void setCurrentChannelId(ChannelId channel_id) { current_channel_id_ = channel_id; }

    // ============================================================
    // 回调设置
    // ============================================================

    /// 控制消息到达回调
    ControlMessageCallback onControlMessage;

    /// 语音包到达回调（已解密）
    VoicePacketCallback onVoicePacket;

    /// 断开连接回调
    DisconnectedCallback onDisconnected;

private:
    // ============================================================
    // NAT 穿透级联协程
    // ============================================================

    /**
     * @brief STUN 探测阶段
     *
     * 向配置的 STUN 服务器发送 Binding Request，探测 NAT 类型。
     * 探测结果保存到 nat_info_，用于决定后续穿透策略。
     *
     * @return awaitable<Result<void>> 探测结果
     */
    boost::asio::awaitable<Result<void>> stunProbe();

    /**
     * @brief UDP 打洞阶段
     *
     * 对于 FullCone / Restricted / PortRestricted 类型的 NAT，
     * 尝试向服务器语音端点发送 UDP ping 进行打洞。
     *
     * @return awaitable<bool> 打洞是否成功
     */
    boost::asio::awaitable<bool> holePunching();

    /**
     * @brief TURN 中继回退阶段
     *
     * 当 UDP 打洞失败或 NAT 类型为 Symmetric 时，
     * 向 TURN 服务器分配中继端口，通过中继转发语音数据。
     *
     * @return awaitable<Result<void>> TURN 分配结果
     */
    boost::asio::awaitable<Result<void>> fallbackToTurn();

    /**
     * @brief TCP 隧道回退阶段
     *
     * 当 UDP 完全被阻断（NatType::Blocked）或 TURN 不可用时，
     * 通过已有的 TCP 控制连接传输语音数据（封装为 TCP 语音帧）。
     *
     * @return awaitable<Result<void>> 隧道建立结果
     */
    boost::asio::awaitable<Result<void>> fallbackToTcpTunnel();

    // ============================================================
    // 内部处理
    // ============================================================

    /// 处理 TCP 控制消息（从 TcpConnection 读取循环中触发）
    /// @param data 裸 Protobuf 载荷（帧头已由 TcpConnection 剥离）
    /// @param msg_type 消息类型（从帧头中解析）
    /// @param request_id 请求ID（从帧头中解析）
    void handleTcpMessage(std::vector<uint8_t> data, uint32_t msg_type, uint32_t request_id);

    /// 处理 UDP 原始数据包（从 UdpSocket 接收循环中触发）
    void handleUdpPacket(const uint8_t* data, uint32_t size,
                         const boost::asio::ip::udp::endpoint& sender);

    /// 处理 TCP 隧道语音帧（从 TcpVoiceTunnel 重组后触发）
    void handleTunnelVoiceFrame(const uint8_t* data, size_t size);

    /// 解密语音包并触发 onVoicePacket 回调
    void decryptAndDeliverVoicePacket(const uint8_t* encrypted_data, uint32_t encrypted_size,
                                      const uint8_t* header_aad, uint32_t aad_size,
                                      const boost::asio::ip::udp::endpoint& sender);

    /// 启动 UDP 接收循环协程
    boost::asio::awaitable<void> udpReceiveLoop();

    // ============================================================
    // 数据成员
    // ============================================================

    // --- I/O 上下文 ---
    boost::asio::io_context& io_ctx_;

    // --- TCP 控制通道 ---
    std::shared_ptr<TcpConnection> tcp_conn_;           ///< TCP 连接（控制信令）
    std::unique_ptr<SslWrapper> ssl_wrapper_;           ///< TLS 包装器

    // --- UDP 语音通道 ---
    std::shared_ptr<UdpSocket> udp_socket_;             ///< UDP 套接字（语音数据）

    // --- NAT 穿透 ---
    std::unique_ptr<NatTraversal> nat_traversal_;       ///< NAT 穿透器
    NatInfo nat_info_;                                   ///< NAT 探测结果
    std::optional<boost::asio::ip::udp::endpoint> mapped_endpoint_;  ///< STUN 映射端点
    std::optional<boost::asio::ip::udp::endpoint> turn_relay_endpoint_;  ///< TURN 中继端点

    // --- 语音加密 ---
    VoiceCrypto voice_crypto_;                           ///< 语音加解密器

    // --- TCP 语音隧道 ---
    TcpVoiceTunnel tcp_voice_tunnel_;                    ///< TCP 语音隧道（UDP 不可用时）

    // --- 通道状态 ---
    std::atomic<UdpChannelMode> udp_channel_mode_{UdpChannelMode::None};
    std::atomic<bool> tcp_connected_{false};

    // --- 配置 ---
    NetworkManagerConfig config_;

    // --- 保护并发访问 ---
    std::mutex send_mutex_;                              ///< 保护 sendControl/sendVoicePacket

    // --- 语音包序列 ---
    std::atomic<uint32_t> voice_sequence_{0};            ///< 语音包序列号计数器
    UserId local_user_id_;                               ///< 本地用户 ID（登录后设置）
    ChannelId current_channel_id_;                       ///< 当前频道 ID（加入频道后设置）
};

} // namespace nevo
