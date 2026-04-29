/**
 * @file ClientSession.cpp
 * @brief 客户端会话实现
 *
 * 处理与单个客户端的所有交互，包括认证、频道操作和状态管理。
 */

#include "nevo/server/ClientSession.h"
#include "nevo/server/ServerCore.h"
#include "nevo/server/Database.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/core/model/Permission.h"
#include "nevo/core/common/Logger.h"

// Protobuf 生成头文件
#include "common.pb.h"
#include "control.pb.h"

namespace nevo {

// ============================================================
// 静态成员初始化
// ============================================================

std::atomic<uint64_t> ClientSession::session_counter_{0};

// ============================================================
// 构造 / 析构
// ============================================================

ClientSession::ClientSession(std::shared_ptr<TcpConnection> conn,
                             ServerCore* server_core,
                             std::shared_ptr<Database> db,
                             std::shared_ptr<ChannelManager> channel_mgr)
    : tcp_conn_(std::move(conn))
    , server_core_(server_core)
    , db_(std::move(db))
    , channel_mgr_(std::move(channel_mgr))
{
    // 分配唯一会话 ID
    session_id_ = SessionId(++session_counter_);

    NEVO_LOG_INFO("server", "ClientSession created (session_id={})", session_id_.value);
}

ClientSession::~ClientSession() {
    NEVO_LOG_INFO("server", "ClientSession destroyed (session_id={}, user={})",
                  session_id_.value, user_.username());
}

// ============================================================
// 会话生命周期
// ============================================================

void ClientSession::start() {
    // 获取 shared_from_this() 以保持会话在异步操作期间存活
    auto self = shared_from_this();

    // 设置 TCP 消息回调
    tcp_conn_->onMessage = [this, self](std::vector<uint8_t> data, uint32_t msg_type, uint32_t request_id) {
        handleControlMessage(std::move(data), msg_type, request_id);
    };

    // 设置 TCP 断开回调
    tcp_conn_->onDisconnected = [this, self]() {
        NEVO_LOG_INFO("server", "Client disconnected (session_id={})", session_id_.value);
        disconnect();
    };

    // 启动 TCP 读取循环（协程方式）
    // 通过 TcpConnection 的 socket 获取 executor 启动协程
    auto& io_ctx = static_cast<boost::asio::io_context&>(
        tcp_conn_->socket().get_executor().context());

    boost::asio::co_spawn(io_ctx,
        [self]() -> boost::asio::awaitable<void> {
            try {
                co_await self->tcp_conn_->asyncReadLoop();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server", "TCP read loop exception: {}", e.what());
            }
            self->disconnect();
        },
        boost::asio::detached);

    NEVO_LOG_INFO("server", "ClientSession started (session_id={}, remote={})",
                  session_id_.value, tcp_conn_->remoteEndpointString());
}

void ClientSession::disconnect() {
    // 防止重复断开
    bool expected = true;
    if (!active_.compare_exchange_strong(expected, false)) {
        return; // 已经断开
    }

    NEVO_LOG_INFO("server", "Disconnecting session (session_id={}, user={})",
                  session_id_.value, user_.username());

    // 从频道中移除用户
    if (authenticated_ && channel_mgr_) {
        channel_mgr_->removeUserFromChannel(user_.id());
    }

    // 关闭 TCP 连接
    if (tcp_conn_) {
        tcp_conn_->close();
    }

    // 通知 ServerCore 客户端已断开
    if (server_core_) {
        server_core_->onClientDisconnected(shared_from_this());
    }
}

// ============================================================
// 控制消息发送
// ============================================================

void ClientSession::sendControl(const control::ControlMessage& message,
                                 ControlMessageType type,
                                 uint32_t request_id) {
    if (!tcp_conn_ || !tcp_conn_->isConnected()) {
        NEVO_LOG_WARN("server", "Cannot send control message: TCP not connected");
        return;
    }

    // 编码 Protobuf 消息
    std::vector<uint8_t> payload(message.ByteSizeLong());
    if (!message.SerializeToArray(payload.data(), static_cast<int>(payload.size()))) {
        NEVO_LOG_ERROR("server", "Failed to serialize ControlMessage");
        return;
    }

    // 异步发送（TcpConnection::asyncSend 会添加帧头）
    auto& io_ctx = static_cast<boost::asio::io_context&>(
        tcp_conn_->socket().get_executor().context());

    auto self = shared_from_this();
    boost::asio::co_spawn(io_ctx,
        [self, payload = std::move(payload), type, request_id]() -> boost::asio::awaitable<void> {
            auto ec = co_await self->tcp_conn_->asyncSend(payload,
                static_cast<uint32_t>(type), request_id);
            if (ec) {
                NEVO_LOG_ERROR("server", "Failed to send control message: {}", ec.message());
            }
        },
        boost::asio::detached);
}

// ============================================================
// UDP 端点管理
// ============================================================

void ClientSession::setUdpEndpoint(const boost::asio::ip::udp::endpoint& endpoint) {
    udp_endpoint_ = endpoint;
    NEVO_LOG_DEBUG("server", "UDP endpoint set for user {}: {}:{}",
                   user_.id().value,
                   endpoint.address().to_string(),
                   endpoint.port());
}

const std::optional<boost::asio::ip::udp::endpoint>& ClientSession::udpEndpoint() const {
    return udp_endpoint_;
}

// ============================================================
// 状态访问
// ============================================================

bool ClientSession::isAuthenticated() const {
    return authenticated_;
}

const User& ClientSession::user() const {
    return user_;
}

UserId ClientSession::userId() const {
    return user_.id();
}

SessionId ClientSession::sessionId() const {
    return session_id_;
}

std::string ClientSession::remoteAddress() const {
    return tcp_conn_ ? tcp_conn_->remoteEndpointString() : "unknown";
}

const NatInfo& ClientSession::natInfo() const {
    return nat_info_;
}

void ClientSession::setNatInfo(const NatInfo& info) {
    nat_info_ = info;
}

// ============================================================
// 消息处理
// ============================================================

void ClientSession::handleControlMessage(std::vector<uint8_t> data,
                                          uint32_t msg_type,
                                          uint32_t request_id) {
    if (data.empty()) {
        NEVO_LOG_WARN("server", "Received empty control message");
        return;
    }

    // TcpConnection::onMessage 回调提供的 data 是不含帧头的 Protobuf 载荷
    // msg_type 和 request_id 已从帧头中解析并传递

    control::ControlMessage msg;
    if (!msg.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        NEVO_LOG_WARN("server", "Failed to parse ControlMessage from payload");
        return;
    }

    // 如果用户未认证，只允许登录请求
    if (!authenticated_ && !msg.has_login_request()) {
        NEVO_LOG_WARN("server", "Unauthenticated message from session {}", session_id_.value);
        return;
    }

    // 根据 oneof 字段分发到对应的处理方法
    if (msg.has_login_request()) {
        handleLogin(msg, request_id);
    } else if (msg.has_join_channel()) {
        handleJoinChannel(msg, request_id);
    } else if (msg.has_leave_channel()) {
        handleLeaveChannel(msg, request_id);
    } else if (msg.has_create_channel()) {
        handleCreateChannel(msg, request_id);
    } else if (msg.has_delete_channel()) {
        handleDeleteChannel(msg, request_id);
    } else if (msg.has_ptt_toggle()) {
        handlePttToggle(msg, request_id);
    } else if (msg.has_mute_toggle()) {
        handleMuteToggle(msg, request_id);
    } else if (msg.has_udp_ping_request()) {
        handleUdpPing(msg, request_id);
    } else if (msg.has_bind_owner_request()) {
        handleBindOwner(msg, request_id);
    } else {
        NEVO_LOG_WARN("server", "Received ControlMessage with unknown payload type");
    }
}

void ClientSession::handleLogin(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_login_request()) {
        NEVO_LOG_WARN("server", "Login message missing login_request payload");
        return;
    }

    const auto& login = msg.login_request();
    const std::string& username = login.username();
    const std::string& credential = login.auth_credential();

    NEVO_LOG_INFO("server", "Login attempt: username={}", username);

    // 检查封禁状态
    if (db_ && db_->isBanned(UserId(0), remoteAddress())) {
        NEVO_LOG_WARN("server", "Login rejected: IP banned ({})", remoteAddress());

        control::ControlMessage response;
        auto* login_resp = response.mutable_login_response();
        login_resp->set_result(nevo::common::ResultCode::ERROR_AUTH_FAILED);
        sendControl(response, ControlMessageType::LoginResponse, request_id);
        return;
    }

    // 验证数据库可用
    if (!db_) {
        NEVO_LOG_ERROR("server", "Database not available for authentication");
        control::ControlMessage response;
        auto* login_resp = response.mutable_login_response();
        login_resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        sendControl(response, ControlMessageType::LoginResponse, request_id);
        return;
    }

    // 按用户名查找用户
    auto user_record = db_->getUserByName(username);
    UserId user_id;

    if (user_record) {
        // 用户已存在——验证密码
        auto verify_result = db_->verifyUser(username, credential);
        if (!verify_result) {
            NEVO_LOG_WARN("server", "Login rejected: wrong password for user '{}'", username);
            control::ControlMessage response;
            auto* login_resp = response.mutable_login_response();
            login_resp->set_result(nevo::common::ResultCode::ERROR_AUTH_FAILED);
            sendControl(response, ControlMessageType::LoginResponse, request_id);
            return;
        }
        user_id = verify_result.value();
        NEVO_LOG_INFO("server", "User logged in: {} (id={})", username, user_id.value);
    } else {
        // 用户不存在，自动注册（使用提供的密码）
        auto create_result = db_->createUser(username, credential);
        if (!create_result) {
            NEVO_LOG_ERROR("server", "Auto-register failed for user: {}", username);
            control::ControlMessage response;
            auto* login_resp = response.mutable_login_response();
            login_resp->set_result(nevo::common::ResultCode::ERROR_AUTH_FAILED);
            sendControl(response, ControlMessageType::LoginResponse, request_id);
            return;
        }
        user_id = create_result.value();
        user_record = db_->getUser(user_id);
        NEVO_LOG_INFO("server", "Auto-registered and logged in: {} (id={})", username, user_id.value);
    }

    if (!user_record) {
        NEVO_LOG_ERROR("server", "User record not found after lookup: id={}", user_id.value);
        control::ControlMessage response;
        auto* login_resp = response.mutable_login_response();
        login_resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        sendControl(response, ControlMessageType::LoginResponse, request_id);
        return;
    }

    // 检查用户级封禁
    if (db_->isBanned(user_id, remoteAddress())) {
        NEVO_LOG_WARN("server", "Login rejected: user banned (id={})", user_id.value);
        control::ControlMessage response;
        auto* login_resp = response.mutable_login_response();
        login_resp->set_result(nevo::common::ResultCode::ERROR_AUTH_FAILED);
        sendControl(response, ControlMessageType::LoginResponse, request_id);
        return;
    }

    // 更新会话状态
    user_ = User(user_id, user_record->username, user_record->group_id);
    user_.setStatus(UserStatus::Online);
    authenticated_ = true;

    // 将用户加入默认频道
    if (channel_mgr_) {
        Channel* default_ch = channel_mgr_->getDefaultChannel();
        if (default_ch) {
            channel_mgr_->moveUserToChannel(user_id, default_ch->id());
            user_.setCurrentChannel(default_ch->id());
        }
    }

    // 提取并保存客户端公钥（Curve25519 公钥长度为 32 字节）
    static constexpr size_t CRYPTO_BOX_PUBLICKEYBYTES = 32;
    std::vector<uint8_t> client_pubkey;
    if (msg.login_request().client_public_key().size() == CRYPTO_BOX_PUBLICKEYBYTES) {
        client_pubkey.assign(
            msg.login_request().client_public_key().begin(),
            msg.login_request().client_public_key().end());
        if (db_) {
            db_->updateUserPublicKey(user_id, client_pubkey);
        }
    }

    // 构建登录响应
    control::ControlMessage response;
    auto* login_resp = response.mutable_login_response();
    login_resp->set_result(nevo::common::ResultCode::OK);

    // 告知客户端服务器是否已有管理员
    bool owner_exists = (server_core_ && server_core_->getOwnerUserId().value != 0);
    login_resp->set_owner_exists(owner_exists);

    auto* user_info = login_resp->mutable_user_info();
    user_info->set_id(user_id.value);
    user_info->set_username(user_record->username);
    user_info->set_status(nevo::common::PbUserStatus::ONLINE);
    user_info->set_muted(false);
    user_info->set_deafened(false);
    user_info->set_group_id(user_record->group_id.value);

    // 生成会话令牌（简化实现：使用 session_id 的十六进制表示）
    login_resp->set_session_token(std::to_string(session_id_.value));

    // 生成并下发每客户端独立的加密会话密钥
    if (server_core_ && !client_pubkey.empty()) {
        auto encrypted_key = server_core_->generateSessionKeyForClient(user_id, client_pubkey);
        if (!encrypted_key.empty()) {
            login_resp->set_encrypted_session_key(
                std::string(reinterpret_cast<const char*>(encrypted_key.data()), encrypted_key.size()));
            login_resp->set_key_exchange_method("X25519+crypto_box_seal");
            NEVO_LOG_INFO("server", "Sent encrypted session key to user_id={}", user_id.value);
        } else {
            NEVO_LOG_WARN("server", "Failed to generate encrypted session key for user_id={}", user_id.value);
        }
    } else {
        // 兼容旧客户端：发送共享服务器会话密钥
        if (server_core_) {
            const uint8_t* key = server_core_->serverSessionKey();
            login_resp->set_server_public_key(std::vector<uint8_t>(key, key + CRYPTO_KEY_SIZE));
            login_resp->set_key_exchange_method("X25519");
        }
    }

    sendControl(response, ControlMessageType::LoginResponse, request_id);

    NEVO_LOG_INFO("server", "User logged in: {} (id={})", username, user_id.value);

    // 通知 ServerCore 新客户端连接
    if (server_core_) {
        server_core_->onClientConnected(shared_from_this());

        // 登录成功后向所有客户端发送频道列表更新，
        // 使新客户端能收到初始频道数据
        server_core_->broadcastChannelListUpdate();
    }

    // 发送欢迎消息
    if (server_core_) {
        const auto& welcome = server_core_->welcomeMessage();
        if (!welcome.empty()) {
            control::ControlMessage welcome_msg;
            auto* server_msg = welcome_msg.mutable_server_message();
            server_msg->set_text(welcome);
            sendControl(welcome_msg, ControlMessageType::ServerMessage, 0);
        }
    }
}

void ClientSession::handleJoinChannel(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_join_channel()) {
        return;
    }

    const auto& req = msg.join_channel();
    ChannelId target_id(req.channel_id());

    NEVO_LOG_DEBUG("server", "User {} joining channel {}", user_.id().value, target_id.value);

    if (!channel_mgr_) {
        return;
    }

    auto result = channel_mgr_->moveUserToChannel(user_.id(), target_id);
    if (result) {
        ChannelId old_channel = user_.currentChannel();
        user_.setCurrentChannel(target_id);

        // Broadcast channel list update to ALL clients (including this one)
        if (server_core_) {
            server_core_->broadcastChannelListUpdate();

            // Send incremental UserJoinedChannel notification
            common::UserInfo user_info;
            user_info.set_id(user_.id().value);
            user_info.set_username(user_.username());
            user_info.set_status(common::PbUserStatus::ONLINE);
            user_info.set_muted(user_.isMuted());
            user_info.set_deafened(user_.isDeafened());
            user_info.set_group_id(user_.groupId().value);
            server_core_->broadcastUserJoined(user_info, target_id);

            // If user was in another channel, also notify left
            if (old_channel && old_channel != target_id) {
                server_core_->broadcastUserLeft(user_.id(), old_channel);
            }
        }
    }
}

void ClientSession::handleLeaveChannel(const control::ControlMessage& msg, uint32_t request_id) {
    NEVO_LOG_DEBUG("server", "User {} leaving channel", user_.id().value);

    ChannelId old_channel = user_.currentChannel();

    if (channel_mgr_) {
        // 离开当前频道，移回默认频道
        Channel* default_ch = channel_mgr_->getDefaultChannel();
        if (default_ch) {
            channel_mgr_->moveUserToChannel(user_.id(), default_ch->id());
            user_.setCurrentChannel(default_ch->id());
        }
    }

    // Broadcast user left notification
    if (server_core_ && old_channel) {
        server_core_->broadcastUserLeft(user_.id(), old_channel);
        server_core_->broadcastChannelListUpdate();
    }
}

void ClientSession::handleCreateChannel(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_create_channel()) {
        return;
    }

    const auto& req = msg.create_channel();
    ChannelId parent_id(req.parent_id());
    const std::string& name = req.name();

    NEVO_LOG_INFO("server", "User {} creating channel '{}' under parent {}",
                  user_.id().value, name, parent_id.value);

    // Permission check: CreateChannel (owner bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (!server_core_->isOwner(user_.id()) &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::CreateChannel)) {
            NEVO_LOG_WARN("server", "User {} (group={}) lacks CreateChannel permission",
                          user_.id().value, user_.groupId().value);
            control::ControlMessage response;
            auto* srv_msg = response.mutable_server_message();
            srv_msg->set_text("Permission denied: cannot create channel");
            sendControl(response, ControlMessageType::ServerMessage, request_id);
            return;
        }
    }

    if (!channel_mgr_) {
        return;
    }

    auto result = channel_mgr_->createChannel(parent_id, name, user_.id());
    if (!result) {
        NEVO_LOG_WARN("server", "Failed to create channel: {}", result.error().message());
    } else {
        // Notify all clients about the channel change
        if (server_core_) {
            server_core_->broadcastChannelListUpdate();
        }
    }
}

void ClientSession::handleDeleteChannel(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_delete_channel()) {
        return;
    }

    const auto& req = msg.delete_channel();
    ChannelId channel_id(req.channel_id());

    NEVO_LOG_INFO("server", "User {} deleting channel {}",
                  user_.id().value, channel_id.value);

    // Permission check: DeleteChannel (owner bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (!server_core_->isOwner(user_.id()) &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::DeleteChannel)) {
            NEVO_LOG_WARN("server", "User {} (group={}) lacks DeleteChannel permission",
                          user_.id().value, user_.groupId().value);
            control::ControlMessage response;
            auto* srv_msg = response.mutable_server_message();
            srv_msg->set_text("Permission denied: cannot delete channel");
            sendControl(response, ControlMessageType::ServerMessage, request_id);
            return;
        }
    }

    if (!channel_mgr_) {
        return;
    }

    auto result = channel_mgr_->deleteChannel(channel_id);
    if (!result) {
        NEVO_LOG_WARN("server", "Failed to delete channel: {}", result.error().message());
    } else {
        // Notify all clients about the channel change
        if (server_core_) {
            server_core_->broadcastChannelListUpdate();
        }
    }
}

void ClientSession::handlePttToggle(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_ptt_toggle()) {
        return;
    }

    bool active = msg.ptt_toggle().active();
    user_.setSpeaking(active);

    NEVO_LOG_DEBUG("server", "User {} PTT: {}", user_.id().value, active ? "ON" : "OFF");

    // Broadcast speaking state to all clients
    if (server_core_) {
        server_core_->broadcastUserSpeaking(user_.id(), active);
    }
}

void ClientSession::handleMuteToggle(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_mute_toggle()) {
        return;
    }

    bool muted = msg.mute_toggle().muted();
    user_.setMuted(muted);

    NEVO_LOG_DEBUG("server", "User {} muted: {}", user_.id().value, muted);

    // Broadcast channel list update to reflect mute state change
    if (server_core_) {
        server_core_->broadcastChannelListUpdate();
    }
}

void ClientSession::handleUdpPing(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_udp_ping_request()) {
        return;
    }

    const auto& req = msg.udp_ping_request();
    uint32_t sequence = req.sequence();

    NEVO_LOG_TRACE("server", "UDP Ping from user {}: seq={}", user_.id().value, sequence);

    // 响应 UDP Ping，确认 UDP 可达
    control::ControlMessage response;
    auto* resp = response.mutable_udp_ping_response();
    resp->set_sequence(sequence);
    resp->set_udp_reachable(true);

    sendControl(response, ControlMessageType::UdpPingResponse, request_id);
}

void ClientSession::handleBindOwner(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_bind_owner_request()) {
        return;
    }

    const auto& req = msg.bind_owner_request();
    const std::string& bind_key = req.bind_key();

    NEVO_LOG_INFO("server", "User {} requesting owner bind", user_.id().value);

    control::ControlMessage response;
    auto* resp = response.mutable_bind_owner_response();

    if (!server_core_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Server core not available");
        sendControl(response, ControlMessageType::BindOwnerResponse, request_id);
        return;
    }

    auto result = server_core_->bindOwner(user_.id(), bind_key);
    if (result) {
        resp->set_result(nevo::common::ResultCode::OK);
        resp->set_owner_user_id(user_.id().value);
        resp->set_message("Successfully bound as server owner");
        NEVO_LOG_INFO("server", "User {} successfully bound as server owner", user_.id().value);

        // 同步提升当前会话权限组
        user_.setGroupId(GROUP_ADMIN);

        // 通知服务端 GUI 显示绑定成功弹窗
        if (server_core_ && server_core_->onOwnerBound) {
            server_core_->onOwnerBound(user_.id(), user_.username());
        }
    } else {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_owner_user_id(0);
        resp->set_message(result.error().message());
        NEVO_LOG_WARN("server", "User {} owner bind failed: {}", user_.id().value, result.error().message());
    }

    sendControl(response, ControlMessageType::BindOwnerResponse, request_id);
}

} // namespace nevo
