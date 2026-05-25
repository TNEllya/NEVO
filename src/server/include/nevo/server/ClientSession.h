#pragma once
/**
 * @file ClientSession.h
 * @brief 客户端会话
 *
 * 代表服务端与单个客户端的连接状态。每个已连接客户端对应一个 ClientSession 对象，
 * 由 ServerCore 在 TCP 连接建立时创建，连接断开时销毁。
 *
 * ClientSession 负责：
 *   - 管理 TCP 控制消息的收发
 *   - 维护客户端的 UDP 端点映射
 *   - 跟踪用户认证状态和 NAT 信息
 *   - 分发控制消息到对应的处理方法
 *
 * 生命周期：
 *   1. ServerCore::acceptTcpLoop() 接受新连接 -> 创建 ClientSession
 *   2. ClientSession::start() 启动 TCP 读取循环
 *   3. 客户端发送 LoginRequest -> handleLogin() 认证
 *   4. 认证后正常交互（加入频道、说话等）
 *   5. 连接断开 -> disconnect() -> ServerCore 清理
 *
 * 线程安全说明：
 *   - 所有方法通过 strand 保证在 io_context 线程上串行执行
 *   - 外部不应直接调用非公开方法
 */

#include "nevo/core/common/Types.h"
#include "nevo/core/common/Result.h"
#include "nevo/core/model/User.h"
#include "nevo/network/NatTraversal.h"
#include "nevo/network/TcpConnection.h"
#include "nevo/core/protocol/PacketTypes.h"

#include <boost/asio.hpp>

#include <memory>
#include <optional>
#include <string>
#include <atomic>

// Protobuf 生成代码的前向声明
namespace nevo::control { class ControlMessage; }

namespace nevo {

// ============================================================
// 前向声明
// ============================================================

class Database;
class ChannelManager;
class ServerCore;

// ============================================================
// ClientSession 类
// ============================================================

/**
 * @class ClientSession
 * @brief 客户端会话
 *
 * 继承 enable_shared_from_this 以支持在异步回调中安全持有自身引用。
 * 使用 shared_ptr 管理 ClientSession 生命周期，确保异步操作完成前不会被销毁。
 */
class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    /**
     * @brief 构造函数
     * @param conn        TCP 连接（shared_ptr）
     * @param server_core ServerCore 的原始指针（不拥有所有权）
     * @param db          数据库指针
     * @param channel_mgr 频道管理器指针
     */
    ClientSession(std::shared_ptr<TcpConnection> conn,
                  ServerCore* server_core,
                  std::shared_ptr<Database> db,
                  std::shared_ptr<ChannelManager> channel_mgr);

    /// 析构函数
    ~ClientSession();

    // 禁止拷贝
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    // ============================================================
    // 会话生命周期
    // ============================================================

    /**
     * @brief 启动会话
     *
     * 设置 TCP 连接的消息回调和断开回调，启动读取循环。
     * 此方法应在创建 ClientSession 后立即调用。
     */
    void start();

    /**
     * @brief 断开会话
     *
     * 关闭 TCP 连接，清理频道状态，通知 ServerCore 客户端已断开。
     */
    void disconnect();

    // ============================================================
    // 控制消息发送
    // ============================================================

    /**
     * @brief 发送控制消息到客户端
     *
     * 使用 TcpConnection 的异步发送功能。
     *
     * @param message 控制消息（Protobuf）
     * @param type    消息类型
     * @param request_id 关联的请求 ID（默认 0，表示通知）
     */
    void sendControl(const control::ControlMessage& message,
                     ControlMessageType type,
                     uint32_t request_id = 0);

    // ============================================================
    // UDP 端点管理
    // ============================================================

    /**
     * @brief 设置客户端的 UDP 端点
     *
     * 当客户端通过 UDP 发送首个语音包或 UdpPing 时调用。
     *
     * @param endpoint 客户端的 UDP 端点
     */
    void setUdpEndpoint(const boost::asio::ip::udp::endpoint& endpoint);

    /**
     * @brief 获取客户端的 UDP 端点
     * @return UDP 端点，未设置返回 std::nullopt
     */
    const std::optional<boost::asio::ip::udp::endpoint>& udpEndpoint() const;

    // ============================================================
    // 状态访问
    // ============================================================

    /**
     * @brief 检查用户是否已认证
     * @return true 表示已成功登录
     */
    bool isAuthenticated() const;

    /**
     * @brief 获取用户信息
     * @return 用户对象的常量引用（未认证时为默认 User）
     */
    const User& user() const;

    /**
     * @brief 获取用户 ID
     * @return 用户 ID（未认证时为 INVALID_USER_ID）
     */
    UserId userId() const;

    /**
     * @brief 获取会话 ID
     * @return 会话 ID
     */
    SessionId sessionId() const;

    /**
     * @brief 获取客户端远端地址字符串（用于日志和封禁检查）
     * @return IP:Port 格式字符串
     */
    std::string remoteAddress() const;

    /**
     * @brief 获取 NAT 信息
     * @return NAT 探测结果
     */
    const NatInfo& natInfo() const;

    /**
     * @brief 设置 NAT 信息
     * @param info NAT 探测结果
     */
    void setNatInfo(const NatInfo& info);

    /**
     * @brief 更新用户的权限组 ID
     * @param group_id 新的权限组 ID
     */
    void updateUserGroupId(GroupId group_id);

    /**
     * @brief 更新用户当前所在频道
     * @param channel_id 新频道 ID
     */
    void updateUserChannel(ChannelId channel_id);

private:
    // ============================================================
    // 消息处理
    // ============================================================

    /**
     * @brief 处理收到的控制消息
     *
     * 解析 Protobuf 消息，分发到对应的处理方法。
     *
     * @param data 原始载荷数据（帧头之后的 Protobuf 字节流）
     * @param msg_type 消息类型（从帧头中解析）
     * @param request_id 请求ID（从帧头中解析）
     */
    void handleControlMessage(std::vector<uint8_t> data, uint32_t msg_type, uint32_t request_id);

    /**
     * @brief 处理登录请求
     * @param msg 控制消息（包含 LoginRequest）
     * @param request_id 请求 ID
     */
    void handleLogin(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理加入频道请求
     * @param msg 控制消息（包含 JoinChannelRequest）
     * @param request_id 请求 ID
     */
    void handleJoinChannel(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理离开频道请求
     * @param msg 控制消息（包含 LeaveChannelRequest）
     * @param request_id 请求 ID
     */
    void handleLeaveChannel(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理创建频道请求
     * @param msg 控制消息（包含 CreateChannelRequest）
     * @param request_id 请求 ID
     */
    void handleCreateChannel(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理删除频道请求
     * @param msg 控制消息（包含 DeleteChannelRequest）
     * @param request_id 请求 ID
     */
    void handleDeleteChannel(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理重命名频道请求
     * @param msg 控制消息（包含 RenameChannelRequest）
     * @param request_id 请求 ID
     */
    void handleRenameChannel(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理 PTT 切换
     * @param msg 控制消息（包含 PttToggle）
     * @param request_id 请求 ID
     */
    void handlePttToggle(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理静音切换
     * @param msg 控制消息（包含 UserMuteToggle）
     * @param request_id 请求 ID
     */
    void handleMuteToggle(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理 UDP Ping 请求
     * @param msg 控制消息（包含 UdpPingRequest）
     * @param request_id 请求 ID
     */
    void handleUdpPing(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理管理员认证请求
     * @param msg 控制消息（包含 AdminAuthRequest）
     * @param request_id 请求 ID
     */
    void handleAdminAuth(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理设置管理员请求
     * @param msg 控制消息（包含 SetAdminRequest）
     * @param request_id 请求 ID
     */
    void handleSetAdmin(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理踢出用户请求
     * @param msg 控制消息（包含 KickUserRequest）
     * @param request_id 请求 ID
     */
    void handleKickUser(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理封禁用户请求
     * @param msg 控制消息（包含 BanUserRequest）
     * @param request_id 请求 ID
     */
    void handleBanUser(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理移动用户请求
     * @param msg 控制消息（包含 MoveUserRequest）
     * @param request_id 请求 ID
     */
    void handleMoveUser(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理聊天消息发送请求
     *
     * 将聊天消息广播到同频道的所有用户。
     *
     * @param msg 控制消息（包含 ChatSendRequest）
     * @param request_id 请求 ID
     */
    void handleChatSend(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理 STUN 绑定请求
     *
     * 返回客户端的公网映射地址和 NAT 类型。
     *
     * @param msg 控制消息（包含 StunBindRequest）
     * @param request_id 请求 ID
     */
    void handleStunBind(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理密钥轮换响应
     *
     * 客户端确认密钥轮换成功。
     *
     * @param msg 控制消息（包含 KeyRotationResponse）
     * @param request_id 请求 ID
     */
    void handleKeyRotationResponse(const control::ControlMessage& msg, uint32_t request_id);

    /**
     * @brief 处理设置服务器名称请求
     * @param msg 控制消息（包含 SetServerNameRequest）
     * @param request_id 请求 ID
     */
    void handleSetServerName(const control::ControlMessage& msg, uint32_t request_id);

    void handleFileListRequest(const control::ControlMessage& msg, uint32_t request_id);
    void handleFileUploadRequest(const control::ControlMessage& msg, uint32_t request_id);
    void handleFileDeleteRequest(const control::ControlMessage& msg, uint32_t request_id);

    // ============================================================
    // 成员变量
    // ============================================================

    /// TCP 连接
    std::shared_ptr<TcpConnection> tcp_conn_;

    /// ServerCore 的原始指针（不拥有所有权）
    ServerCore* server_core_;

    /// 数据库
    std::shared_ptr<Database> db_;

    /// 频道管理器
    std::shared_ptr<ChannelManager> channel_mgr_;

    /// 用户对象（未认证时为默认状态）
    User user_;

    /// 会话 ID
    SessionId session_id_;

    /// 是否已认证
    std::atomic<bool> authenticated_{false};

    /// 客户端的 UDP 端点（可选，由 UDP 语音包或 UdpPing 设置）
    std::optional<boost::asio::ip::udp::endpoint> udp_endpoint_;

    /// NAT 信息
    NatInfo nat_info_;

    /// 会话是否活跃（未断开）
    std::atomic<bool> active_{true};

    /// 全局会话计数器（用于生成唯一 SessionId）
    static std::atomic<uint64_t> session_counter_;
};

} // namespace nevo
