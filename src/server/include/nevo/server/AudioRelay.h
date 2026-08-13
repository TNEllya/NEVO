#pragma once
/**
 * @file AudioRelay.h
 * @brief UDP 语音包转发器
 *
 * AudioRelay 负责服务端的语音数据中继：
 *   - 接收来自客户端的加密语音包
 *   - 解析语音包头，识别发送者和目标频道
 *   - 将语音包转发给同一频道内的其他用户
 *   - 管理用户 ID 与 UDP 端点的映射关系
 *
 * 语音包处理流程：
 *   1. ServerCore 的 UDP 接收循环收到原始数据
 *   2. 调用 AudioRelay::handleVoicePacket()
 *   3. 解析 VoicePacketHeader，获取 sender_id 和 channel_id
 *   4. 查找目标频道内的其他用户
 *   5. 使用 VoiceCrypto 解密并重新加密（可选，取决于密钥策略）
 *   6. 通过 UDP 套接字转发给其他用户
 *
 * 密钥策略说明：
 *   - 当前实现采用服务端中继模式：服务端不解密语音内容，
 *     仅读取包头信息并原样转发密文。
 *   - 这意味着客户端间的语音加密端到端进行，
 *     服务端仅作为无感知的转发节点。
 *   - 如需服务端混音等高级功能，需要解密/重加密流程。
 *
 * 线程安全说明：
 *   - 所有公开方法通过 mutex 保护
 *   - handleVoicePacket 在 io_context 线程上调用，但可能并发
 */

#include "nevo/core/common/Types.h"
#include "nevo/network/VoiceCrypto.h"

#include <boost/asio.hpp>

#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

namespace nevo {

// ============================================================
// 前向声明
// ============================================================

class ChannelManager;

// ============================================================
// 客户端 UDP 映射信息
// ============================================================

/// 客户端 UDP 端点映射记录
struct ClientUdpMapping {
    UserId user_id;                                      ///< 用户 ID
    boost::asio::ip::udp::endpoint udp_endpoint;         ///< UDP 端点
    ChannelId current_channel;                           ///< 当前所在频道
};

// ============================================================
// AudioRelay 类
// ============================================================

/**
 * @class AudioRelay
 * @brief UDP 语音包转发器
 *
 * 管理用户 UDP 端点映射，转发语音包到同频道用户。
 * 在中继模式下不解密语音载荷，仅转发密文。
 */
class AudioRelay {
public:
    /// 构造函数
    AudioRelay();

    /// 析构函数
    ~AudioRelay();

    // 禁止拷贝
    AudioRelay(const AudioRelay&) = delete;
    AudioRelay& operator=(const AudioRelay&) = delete;

    // ============================================================
    // 语音包处理
    // ============================================================

    /**
     * @brief 处理收到的 UDP 语音包
     *
     * 解析语音包头，识别发送者所在频道，
     * 将语音包转发给同频道内的其他用户。
     *
     * @param data            原始 UDP 数据
     * @param size            数据字节数
     * @param sender_endpoint 发送者端点
     */
    void handleVoicePacket(const uint8_t* data, uint32_t size,
                           const boost::asio::ip::udp::endpoint& sender_endpoint);

    /// 处理来自 TCP 隧道的语音包
    /// @param data       原始语音包数据
    /// @param size       数据字节数
    /// @param sender_id  发送者用户 ID（从 TCP 会话中获取）
    void handleVoicePacket(const uint8_t* data, uint32_t size,
                           const boost::asio::ip::udp::endpoint& sender_endpoint,
                           UserId sender_id);

    // ============================================================
    // 客户端映射管理
    // ============================================================

    // ============================================================
    // 统计
    // ============================================================

    /// 获取已转发的语音包数量
    uint64_t packetsRelayed() const;

    /// 获取已丢弃的语音包数量
    uint64_t packetsDropped() const;

    // ============================================================
    // 客户端映射管理
    // ============================================================

    /**
     * @brief 添加客户端 UDP 映射
     *
     * 将用户 ID 与其 UDP 端点关联。
     * 在用户认证后、UDP 通路建立时调用。
     *
     * @param user_id      用户 ID
     * @param udp_endpoint 客户端的 UDP 端点
     */
    void addClientMapping(UserId user_id,
                          const boost::asio::ip::udp::endpoint& udp_endpoint);

    /**
     * @brief 更新客户端所在频道
     *
     * 在用户切换频道时调用，更新映射中的频道信息。
     *
     * @param user_id    用户 ID
     * @param channel_id 新频道 ID
     */
    void updateClientChannel(UserId user_id, ChannelId channel_id);

    /**
     * @brief 移除客户端 UDP 映射
     *
     * 在用户断开连接时调用。若同一账号存在多个设备映射，
     * 将移除该账号的全部映射。
     *
     * @param user_id 用户 ID
     */
    void removeClientMapping(UserId user_id);

    /**
     * @brief 移除指定 UDP 端点对应的客户端映射
     *
     * 用于同一账号多设备场景下，仅移除断开设备对应的端点，
     * 不影响其他仍在线的设备。
     *
     * @param user_id       用户 ID
     * @param udp_endpoint  要移除的 UDP 端点
     */
    void removeClientMapping(UserId user_id,
                             const boost::asio::ip::udp::endpoint& udp_endpoint);

    // ============================================================
    // 配置
    // ============================================================

    /**
     * @brief 设置频道管理器
     *
     * AudioRelay 需要通过 ChannelManager 获取频道内的用户列表。
     *
     * @param channel_mgr 频道管理器指针
     */
    void setChannelManager(std::shared_ptr<ChannelManager> channel_mgr);

    /**
     * @brief 设置 UDP 发送套接字
     *
     * AudioRelay 通过此套接字发送转发后的语音包。
     *
     * @param socket UDP 套接字
     */
    void setUdpSocket(std::shared_ptr<class UdpSocket> socket);

    /**
     * @brief 设置 I/O 上下文
     *
     * 用于在 handleVoicePacket 中启动异步发送协程。
     *
     * @param io_ctx I/O 上下文引用
     */
    void setIoContext(boost::asio::io_context& io_ctx);

    /**
     * @brief 设置会话密钥查询回调
     *
     * AudioRelay 需要为每个客户端查询其独立的会话密钥，
     * 以实现逐客户端的解密和重加密。
     *
     * @param query 回调函数：传入 UserId，返回 32 字节密钥指针或 nullptr
     */
    using SessionKeyQuery = std::function<const uint8_t*(UserId)>;
    void setSessionKeyQuery(SessionKeyQuery query);

    void rotateClientKey(UserId user_id, const uint8_t* key);

    /**
     * @brief 设置 TCP 语音下发回调
     *
     * 外网/NAT 场景（frp 等内网穿透）下 UDP 回程不可靠，
     * 中继优先通过接收者的 TCP 控制连接下发语音帧
     * （帧类型 TCP_VOICE_FRAME_TYPE，由 ServerCore 实现）。
     *
     * @param sender 回调：传入接收者 UserId 与已加密的语音帧载荷，
     *               返回 true 表示已通过 TCP 下发（无需 UDP 兜底）
     */
    using VoiceTcpSender = std::function<bool(UserId, const std::vector<uint8_t>&)>;
    void setTcpSenderCallback(VoiceTcpSender sender);

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /**
     * @brief 根据 UDP 端点查找用户 ID（加锁版本，可在未持有 mutex_ 时调用）
     * @param endpoint UDP 端点
     * @return 用户 ID，未找到返回 INVALID_USER_ID
     */
    UserId findUserByEndpoint(const boost::asio::ip::udp::endpoint& endpoint) const;

    /**
     * @brief 根据 UDP 端点查找用户 ID（调用者必须已持有 mutex_）
     * @param endpoint UDP 端点
     * @return 用户 ID，未找到返回 INVALID_USER_ID
     */
    UserId findUserByEndpointLocked(const boost::asio::ip::udp::endpoint& endpoint) const;

    /**
     * @brief 获取同频道内的所有其他用户的 UDP 端点
     * @param sender_id            发送者用户 ID
     * @param channel_id           频道 ID
     * @param sender_endpoint_key  发送者自身端点的键（用于排除自身）
     * @return 其他用户的 UDP 端点列表
     */
    std::vector<boost::asio::ip::udp::endpoint> getChannelPeers(
        UserId sender_id, ChannelId channel_id,
        const std::string& sender_endpoint_key) const;

    /**
     * @brief 获取同频道内的所有其他用户的 UDP 端点（无锁版本，调用者必须已持有 mutex_）
     * @param sender_id            发送者用户 ID
     * @param channel_id           频道 ID
     * @param sender_endpoint_key  发送者自身端点的键（用于排除自身）
     * @return 其他用户的 UDP 端点列表
     */
    std::vector<boost::asio::ip::udp::endpoint> getChannelPeersLocked(
        UserId sender_id, ChannelId channel_id,
        const std::string& sender_endpoint_key) const;

    /**
     * @brief 统一的语音包中继入口（供两个公开重载共用）
     *
     * 安全要求（fail-closed）：
     *   - UDP 路径：发送者必须已在映射表中（认证后由 addClientMapping 建立），
     *     不接受"任意端点自称任意用户"
     *   - TCP 隧道路径：身份来自已认证的 TCP 会话，允许自动建立映射
     *   - 发送者必须是所声明频道的成员（频道管理器可用时强制校验）
     *   - 发送者/接收者缺少加密上下文时丢弃包，绝不"原样转发"
     *
     * @param data             原始语音包数据
     * @param size             数据字节数
     * @param sender_endpoint  发送者端点
     * @param known_sender_id  已知发送者 ID（TCP 隧道）；INVALID_USER_ID 表示从端点解析
     */
    void relayVoicePacket(const uint8_t* data, uint32_t size,
                          const boost::asio::ip::udp::endpoint& sender_endpoint,
                          UserId known_sender_id);

    /**
     * @brief 获取或创建指定用户的 VoiceCrypto 实例（调用者必须已持有 mutex_）
     * @param user_id 用户 ID
     * @return VoiceCrypto 实例（shared_ptr，持有引用计数防止并发销毁）
     */
    std::shared_ptr<VoiceCrypto> getOrCreateCryptoLocked(UserId user_id);

    // ============================================================
    // 成员变量
    // ============================================================

    /// 用户 ID -> (端点键 -> UDP 映射)（支持同一账号多设备并存）
    std::unordered_map<UserId,
        std::unordered_map<std::string, ClientUdpMapping>> client_map_;

    /// UDP 端点 -> 用户 ID 的反向映射（用于快速查找发送者）
    std::unordered_map<std::string, UserId> endpoint_to_user_;

    /// 频道管理器
    std::shared_ptr<ChannelManager> channel_mgr_;

    /// UDP 发送套接字
    std::shared_ptr<class UdpSocket> udp_socket_;

    /// I/O 上下文指针（用于启动异步发送协程）
    boost::asio::io_context* io_ctx_ = nullptr;

    /// 会话密钥查询回调（从 ServerCore 获取每客户端密钥）
    SessionKeyQuery session_key_query_;

    /// TCP 语音下发回调（接收者有活跃 TCP 会话时优先走 TCP，回程可靠）
    VoiceTcpSender tcp_sender_callback_;

    /// 每客户端 VoiceCrypto 实例（用于加密时的 nonce 管理）
    /// 使用 shared_ptr：中继在锁外持有引用，防止用户断线时对象被并发销毁（use-after-free）
    std::unordered_map<UserId, std::shared_ptr<VoiceCrypto>> client_cryptos_;

    /// 互斥锁
    mutable std::mutex mutex_;

    /// 统计：已处理的语音包数量
    uint64_t packets_relayed_ = 0;

    /// 统计：已丢弃的语音包数量
    uint64_t packets_dropped_ = 0;
};

} // namespace nevo
