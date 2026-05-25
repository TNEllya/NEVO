/**
 * @file ClientSession.cpp
 * @brief 客户端会话实现
 *
 * 处理与单个客户端的所有交互，包括认证、频道操作和状态管理。
 */

#include "nevo/server/ClientSession.h"
#include "nevo/server/ServerCore.h"
#include "nevo/server/ControlServer.h"
#include "nevo/server/Database.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/core/model/Permission.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/network/TcpVoiceTunnel.h"

#include <filesystem>
#include <random>
#include <cstdio>

#ifdef NEVO_HAS_SODIUM
#include <sodium.h>
#endif

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
    auto self = shared_from_this();

    // 设置 TCP 消息回调
    tcp_conn_->onMessage = [this, self](std::vector<uint8_t> data, uint32_t msg_type, uint32_t request_id) {
        if (msg_type == TCP_VOICE_FRAME_TYPE) {
            if (authenticated_ && user_.id() && server_core_) {
                server_core_->relayTcpVoicePacket(
                    data.data(), static_cast<uint32_t>(data.size()), user_.id());
            }
            return;
        }
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

    // 使用自定义线格式编码（兼容 Python 客户端），而非 Protobuf 序列化
    std::vector<uint8_t> payload = encodeCustomWirePayload(message);
    if (payload.empty()) {
        NEVO_LOG_ERROR("server", "Failed to encode ControlMessage to custom wire format");
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

void ClientSession::updateUserGroupId(GroupId group_id) {
    user_.setGroupId(group_id);
    NEVO_LOG_INFO("server", "User {} group updated to {}", user_.id().value, group_id.value);
}

void ClientSession::updateUserChannel(ChannelId channel_id) {
    user_.setCurrentChannel(channel_id);
    NEVO_LOG_DEBUG("server", "User {} channel updated to {}", user_.id().value, channel_id.value);
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

    // TcpConnection::onMessage 回调提供的 data 是不含帧头的载荷
    // msg_type 和 request_id 已从帧头中解析并传递
    //
    // Python 客户端使用自定义二进制线格式（非 Protobuf），
    // 需要通过 decodeCustomWirePayload 解码为 ControlMessage

    auto msg_opt = decodeCustomWirePayload(data.data(), static_cast<uint32_t>(data.size()));
    if (!msg_opt.has_value()) {
        NEVO_LOG_WARN("server", "Failed to parse ControlMessage from payload (custom wire format)");
        return;
    }

    const control::ControlMessage& msg = msg_opt.value();

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
    } else if (msg.has_rename_channel()) {
        handleRenameChannel(msg, request_id);
    } else if (msg.has_ptt_toggle()) {
        handlePttToggle(msg, request_id);
    } else if (msg.has_mute_toggle()) {
        handleMuteToggle(msg, request_id);
    } else if (msg.has_udp_ping_request()) {
        handleUdpPing(msg, request_id);
    } else if (msg.has_admin_auth_request()) {
        handleAdminAuth(msg, request_id);
    } else if (msg.has_set_admin_request()) {
        handleSetAdmin(msg, request_id);
    } else if (msg.has_kick_user_request()) {
        handleKickUser(msg, request_id);
    } else if (msg.has_ban_user_request()) {
        handleBanUser(msg, request_id);
    } else if (msg.has_move_user_request()) {
        handleMoveUser(msg, request_id);
    } else if (msg.has_stun_bind_request()) {
        handleStunBind(msg, request_id);
    } else if (msg.has_key_rotation_response()) {
        handleKeyRotationResponse(msg, request_id);
    } else if (msg.has_chat_send()) {
        handleChatSend(msg, request_id);
    } else if (msg.has_set_server_name_request()) {
        handleSetServerName(msg, request_id);
    } else if (msg.has_file_list_request()) {
        handleFileListRequest(msg, request_id);
    } else if (msg.has_file_upload_request()) {
        handleFileUploadRequest(msg, request_id);
    } else if (msg.has_file_delete_request()) {
        handleFileDeleteRequest(msg, request_id);
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

    // 告知客户端服务器是否已设置管理员密码
    bool admin_pwd_set = (server_core_ && server_core_->isAdminPasswordSet());
    login_resp->set_owner_exists(admin_pwd_set);

    // 下发服务端 UDP 端口给客户端
    if (server_core_) {
        login_resp->set_server_udp_port(server_core_->udpPort());
        login_resp->set_server_video_udp_port(server_core_->videoUdpPort());
    }

    auto* user_info = login_resp->mutable_user_info();
    user_info->set_id(user_id.value);
    user_info->set_username(user_record->username);
    user_info->set_status(nevo::common::UserStatus::ONLINE);
    user_info->set_muted(false);
    user_info->set_deafened(false);
    user_info->set_group_id(user_record->group_id.value);

    // 生成安全的会话令牌
        std::string session_token;
#ifdef NEVO_HAS_SODIUM
        uint8_t token_bytes[32];
        randombytes_buf(token_bytes, sizeof(token_bytes));
        // 转换为十六进制字符串
        char hex_buf[65];
        for (int i = 0; i < 32; ++i) {
            sprintf(hex_buf + i * 2, "%02x", token_bytes[i]);
        }
        hex_buf[64] = '\0';
        session_token = hex_buf;
#else
        // Fallback using std::random_device
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint8_t> dist(0, 255);
        char hex_buf[65];
        for (int i = 0; i < 32; ++i) {
            uint8_t b = dist(gen);
            sprintf(hex_buf + i * 2, "%02x", b);
        }
        hex_buf[64] = '\0';
        session_token = hex_buf;
#endif
        login_resp->set_session_token(session_token);

    // 读取客户端支持的密钥交换方式
    bool client_supports_crypto_box_seal = false;
    bool client_supports_x25519 = false;
    for (const auto& method : login.key_exchange_methods()) {
        if (method == "X25519+crypto_box_seal") {
            client_supports_crypto_box_seal = true;
        } else if (method == "X25519") {
            client_supports_x25519 = true;
        }
    }
    // 如果客户端没有声明任何方式，默认两种都支持（兼容旧客户端）
    if (login.key_exchange_methods().empty()) {
        client_supports_crypto_box_seal = true;
        client_supports_x25519 = true;
    }

    // 生成并下发每客户端独立的加密会话密钥
    if (server_core_ && !client_pubkey.empty() && client_supports_crypto_box_seal) {
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
        if (server_core_) {
            const uint8_t* key = server_core_->serverSessionKey();
            login_resp->set_server_public_key(std::string(reinterpret_cast<const char*>(key), CRYPTO_KEY_SIZE));
            login_resp->set_key_exchange_method("X25519");
            // 共享密钥模式：同样注册到 AudioRelay 的 per-client 映射中
            server_core_->setClientSessionKey(user_id, key, CRYPTO_KEY_SIZE);
        }
    }

    sendControl(response, ControlMessageType::LoginResponse, request_id);

    NEVO_LOG_INFO("server", "User logged in: {} (id={})", username, user_id.value);

    // Set client UDP endpoint for voice relay using TCP remote address + client_udp_port
    uint32_t client_udp_port = login.client_udp_port();
    if (client_udp_port > 0 && tcp_conn_) {
        auto& sock = tcp_conn_->socket();
        boost::system::error_code ec;
        auto tcp_remote = sock.remote_endpoint(ec);
        if (!ec) {
            boost::asio::ip::udp::endpoint udp_ep(tcp_remote.address(), static_cast<uint16_t>(client_udp_port));
            setUdpEndpoint(udp_ep);
            NEVO_LOG_INFO("server", "Set UDP endpoint for user {} -> {}:{}",
                          user_id.value, udp_ep.address().to_string(), udp_ep.port());
        }
    }

    uint32_t client_video_udp_port = login.client_video_udp_port();
    if (client_video_udp_port > 0 && tcp_conn_) {
        auto& sock = tcp_conn_->socket();
        boost::system::error_code ec;
        auto tcp_remote = sock.remote_endpoint(ec);
        if (!ec) {
            boost::asio::ip::udp::endpoint video_endpoint(tcp_remote.address(), static_cast<uint16_t>(client_video_udp_port));
            ChannelId channel_id = user_.currentChannel();
            server_core_->addVideoRelayMapping(user_id, video_endpoint, channel_id);
            NEVO_LOG_INFO("server", "Set Video UDP endpoint for user {} -> {}:{}",
                          user_id.value, video_endpoint.address().to_string(), video_endpoint.port());
        }
    }

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

    // Permission check: JoinChannel (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::JoinChannel)) {
            NEVO_LOG_WARN("server", "User {} (group={}) lacks JoinChannel permission",
                          user_.id().value, user_.groupId().value);
            control::ControlMessage response;
            auto* srv_msg = response.mutable_server_message();
            srv_msg->set_text("Permission denied: cannot join channel");
            sendControl(response, ControlMessageType::ServerMessage, request_id);
            return;
        }
    }

    if (!channel_mgr_) {
        return;
    }

    auto result = channel_mgr_->moveUserToChannel(user_.id(), target_id);
    if (result) {
        ChannelId old_channel = user_.currentChannel();
        user_.setCurrentChannel(target_id);

        // Update AudioRelay channel mapping
        if (server_core_) {
            server_core_->updateAudioRelayChannel(user_.id(), target_id);
            server_core_->updateVideoRelayChannel(user_.id(), target_id);

            // Broadcast channel list update to ALL clients (including this one)
            server_core_->broadcastChannelListUpdate();

            // Send incremental UserJoinedChannel notification
            common::UserInfo user_info;
            user_info.set_id(user_.id().value);
            user_info.set_username(user_.username());
            user_info.set_status(common::UserStatus::ONLINE);
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

            // Update AudioRelay channel mapping
            if (server_core_) {
                server_core_->updateAudioRelayChannel(user_.id(), default_ch->id());
                server_core_->updateVideoRelayChannel(user_.id(), default_ch->id());
            }
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

    // Permission check: CreateChannel (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
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

    // Permission check: DeleteChannel (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
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

void ClientSession::handleRenameChannel(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_rename_channel()) {
        return;
    }

    const auto& req = msg.rename_channel();
    ChannelId channel_id(req.channel_id());
    const std::string& new_name = req.new_name();

    NEVO_LOG_INFO("server", "User {} renaming channel {} to '{}'",
                  user_.id().value, channel_id.value, new_name);

    // Permission check: CreateChannel (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::CreateChannel)) {
            NEVO_LOG_WARN("server", "User {} (group={}) lacks permission to rename channel",
                          user_.id().value, user_.groupId().value);
            control::ControlMessage response;
            auto* srv_msg = response.mutable_server_message();
            srv_msg->set_text("Permission denied: cannot rename channel");
            sendControl(response, ControlMessageType::ServerMessage, request_id);
            return;
        }
    }

    if (!channel_mgr_) {
        return;
    }

    auto result = channel_mgr_->renameChannel(channel_id, new_name);
    if (!result) {
        NEVO_LOG_WARN("server", "Failed to rename channel: {}", result.error().message());
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

    // Permission check: Speak (admin bypass)
    if (active && server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::Speak)) {
            NEVO_LOG_WARN("server", "User {} (group={}) lacks Speak permission",
                          user_.id().value, user_.groupId().value);
            return;
        }
    }

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

    // Store the client's UDP encryption key material if provided
    if (req.client_udp_key().size() > 0 && server_core_) {
        NEVO_LOG_DEBUG("server", "User {} provided UDP key material ({} bytes)",
                       user_.id().value, req.client_udp_key().size());
        // The UDP key is used by AudioRelay for per-client voice encryption
        // Store it for later use when UDP endpoint becomes available
    }

    // If the UDP endpoint hasn't been registered yet, we can't set it from TCP
    // The actual UDP endpoint will be registered when the first UDP voice packet arrives
    // via ServerCore::receiveUdpLoop() -> AudioRelay::handleVoicePacket()

    // 响应 UDP Ping，确认 UDP 可达
    control::ControlMessage response;
    auto* resp = response.mutable_udp_ping_response();
    resp->set_sequence(sequence);
    resp->set_udp_reachable(true);

    sendControl(response, ControlMessageType::UdpPingResponse, request_id);
}

void ClientSession::handleSetAdmin(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_set_admin_request()) {
        return;
    }

    const auto& req = msg.set_admin_request();
    UserId target_id(req.user_id());
    bool set_admin = req.set_admin();

    NEVO_LOG_INFO("server", "User {} requesting set_admin for user {} (admin={})",
                  user_.id().value, target_id.value, set_admin);

    control::ControlMessage response;
    auto* resp = response.mutable_set_admin_response();

    // Permission check: ServerAdmin (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::ServerAdmin)) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Permission denied: cannot set admin");
            sendControl(response, ControlMessageType::SetAdminResponse, request_id);
            return;
        }
    }

    // Cannot change own admin status
    if (target_id == user_.id()) {
        resp->set_result(nevo::common::ResultCode::ERROR_INVALID_REQUEST);
        resp->set_message("Cannot change your own admin status");
        sendControl(response, ControlMessageType::SetAdminResponse, request_id);
        return;
    }

    // Cannot modify an admin's status (unless you are an admin)
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session && target_session->user().groupId() == GROUP_ADMIN &&
            user_.groupId() != GROUP_ADMIN) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Cannot modify another administrator's status");
            sendControl(response, ControlMessageType::SetAdminResponse, request_id);
            return;
        }
    }

    // Update group in database
    if (!db_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Database not available");
        sendControl(response, ControlMessageType::SetAdminResponse, request_id);
        return;
    }

    GroupId new_group = set_admin ? GROUP_ADMIN : GROUP_USER;
    auto result = db_->updateUserGroupId(target_id, new_group);
    if (!result) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message(result.error().message());
        sendControl(response, ControlMessageType::SetAdminResponse, request_id);
        return;
    }

    // Update the target user's session group if they are online
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session) {
            target_session->updateUserGroupId(new_group);
        }
    }

    resp->set_result(nevo::common::ResultCode::OK);
    resp->set_message(set_admin ? "User promoted to admin" : "Admin removed");
    sendControl(response, ControlMessageType::SetAdminResponse, request_id);

    // Broadcast channel list update to reflect permission changes
    if (server_core_) {
        server_core_->broadcastChannelListUpdate();
    }
}

void ClientSession::handleKickUser(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_kick_user_request()) {
        return;
    }

    const auto& req = msg.kick_user_request();
    UserId target_id(req.user_id());
    const std::string& reason = req.reason();

    NEVO_LOG_INFO("server", "User {} requesting kick for user {} (reason='{}')",
                  user_.id().value, target_id.value, reason);

    control::ControlMessage response;
    auto* resp = response.mutable_kick_user_response();

    // Permission check: KickUser (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::KickUser)) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Permission denied: cannot kick user");
            sendControl(response, ControlMessageType::KickUserResponse, request_id);
            return;
        }
    }

    // Cannot kick yourself
    if (target_id == user_.id()) {
        resp->set_result(nevo::common::ResultCode::ERROR_INVALID_REQUEST);
        resp->set_message("Cannot kick yourself");
        sendControl(response, ControlMessageType::KickUserResponse, request_id);
        return;
    }

    // Cannot kick an admin (unless you are an admin)
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session && target_session->user().groupId() == GROUP_ADMIN &&
            user_.groupId() != GROUP_ADMIN) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Cannot kick an administrator");
            sendControl(response, ControlMessageType::KickUserResponse, request_id);
            return;
        }
    }

    // Find and disconnect the target user
    if (!server_core_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Server core not available");
        sendControl(response, ControlMessageType::KickUserResponse, request_id);
        return;
    }

    auto target_session = server_core_->getClientSession(target_id);
    if (!target_session) {
        resp->set_result(nevo::common::ResultCode::ERROR_USER_NOT_FOUND);
        resp->set_message("User not found or not online");
        sendControl(response, ControlMessageType::KickUserResponse, request_id);
        return;
    }

    // Send a server message to the kicked user before disconnecting
    control::ControlMessage kick_msg;
    auto* srv_msg = kick_msg.mutable_server_message();
    srv_msg->set_text("You have been kicked" + (reason.empty() ? "" : ": " + reason));
    target_session->sendControl(kick_msg, ControlMessageType::ServerMessage, 0);

    // Disconnect the kicked user
    target_session->disconnect();

    resp->set_result(nevo::common::ResultCode::OK);
    resp->set_message("User kicked");
    sendControl(response, ControlMessageType::KickUserResponse, request_id);

    // Broadcast update
    server_core_->broadcastChannelListUpdate();
}

void ClientSession::handleBanUser(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_ban_user_request()) {
        return;
    }

    const auto& req = msg.ban_user_request();
    UserId target_id(req.user_id());
    const std::string& reason = req.reason();
    uint64_t expires_at = req.expires_at();

    NEVO_LOG_INFO("server", "User {} requesting ban for user {} (reason='{}', expires={})",
                  user_.id().value, target_id.value, reason, expires_at);

    control::ControlMessage response;
    auto* resp = response.mutable_ban_user_response();

    // Permission check: BanUser (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::BanUser)) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Permission denied: cannot ban user");
            sendControl(response, ControlMessageType::BanUserResponse, request_id);
            return;
        }
    }

    // Cannot ban yourself
    if (target_id == user_.id()) {
        resp->set_result(nevo::common::ResultCode::ERROR_INVALID_REQUEST);
        resp->set_message("Cannot ban yourself");
        sendControl(response, ControlMessageType::BanUserResponse, request_id);
        return;
    }

    // Cannot ban another admin
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session && target_session->user().groupId() == GROUP_ADMIN &&
            user_.groupId() != GROUP_ADMIN) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Cannot ban an administrator");
            sendControl(response, ControlMessageType::BanUserResponse, request_id);
            return;
        }
    }

    if (!db_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Database not available");
        sendControl(response, ControlMessageType::BanUserResponse, request_id);
        return;
    }

    // Get the target user's IP address if they are online
    std::string ip_address;
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session) {
            ip_address = target_session->remoteAddress();
        }
    }

    // Add ban to database
    auto ban_result = db_->addBan(target_id, ip_address, reason, static_cast<int64_t>(expires_at));
    if (!ban_result) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message(ban_result.error().message());
        sendControl(response, ControlMessageType::BanUserResponse, request_id);
        return;
    }

    // Disconnect the banned user if they are online
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session) {
            // Notify the banned user
            control::ControlMessage ban_msg;
            auto* srv_msg = ban_msg.mutable_server_message();
            srv_msg->set_text("You have been banned" + (reason.empty() ? "" : ": " + reason));
            target_session->sendControl(ban_msg, ControlMessageType::ServerMessage, 0);

            target_session->disconnect();
        }
        server_core_->broadcastChannelListUpdate();
    }

    resp->set_result(nevo::common::ResultCode::OK);
    resp->set_message("User banned");
    sendControl(response, ControlMessageType::BanUserResponse, request_id);
}

void ClientSession::handleMoveUser(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_move_user_request()) {
        return;
    }

    const auto& req = msg.move_user_request();
    UserId target_id(req.user_id());
    ChannelId target_channel(req.channel_id());

    NEVO_LOG_INFO("server", "User {} requesting move for user {} to channel {}",
                  user_.id().value, target_id.value, target_channel.value);

    control::ControlMessage response;
    auto* resp = response.mutable_move_user_response();

    // Permission check: MoveUser (admin bypass)
    if (server_core_ && server_core_->permissionManager()) {
        if (user_.groupId() != GROUP_ADMIN &&
            !server_core_->permissionManager()->hasPermission(user_.groupId(), Permission::MoveUser)) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Permission denied: cannot move user");
            sendControl(response, ControlMessageType::MoveUserResponse, request_id);
            return;
        }
    }

    // Find the target user
    if (!server_core_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Server core not available");
        sendControl(response, ControlMessageType::MoveUserResponse, request_id);
        return;
    }

    // Cannot move an admin (unless you are an admin)
    if (server_core_) {
        auto target_session = server_core_->getClientSession(target_id);
        if (target_session && target_session->user().groupId() == GROUP_ADMIN &&
            user_.groupId() != GROUP_ADMIN) {
            resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
            resp->set_message("Cannot move an administrator");
            sendControl(response, ControlMessageType::MoveUserResponse, request_id);
            return;
        }
    }

    auto target_session = server_core_->getClientSession(target_id);
    if (!target_session) {
        resp->set_result(nevo::common::ResultCode::ERROR_USER_NOT_FOUND);
        resp->set_message("User not found or not online");
        sendControl(response, ControlMessageType::MoveUserResponse, request_id);
        return;
    }

    // Move the user to the target channel
    if (!channel_mgr_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Channel manager not available");
        sendControl(response, ControlMessageType::MoveUserResponse, request_id);
        return;
    }

    auto move_result = channel_mgr_->moveUserToChannel(target_id, target_channel);
    if (!move_result) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message(move_result.error().message());
        sendControl(response, ControlMessageType::MoveUserResponse, request_id);
        return;
    }

    // Update the target user's channel
    ChannelId old_channel = target_session->user().currentChannel();
    target_session->updateUserChannel(target_channel);

    // Update relay channel mappings for the moved user
    server_core_->updateAudioRelayChannel(target_id, target_channel);
    server_core_->updateVideoRelayChannel(target_id, target_channel);

    // Broadcast updates
    server_core_->broadcastChannelListUpdate();

    common::UserInfo user_info;
    user_info.set_id(target_session->user().id().value);
    user_info.set_username(target_session->user().username());
    user_info.set_status(common::UserStatus::ONLINE);
    user_info.set_muted(target_session->user().isMuted());
    user_info.set_deafened(target_session->user().isDeafened());
    user_info.set_group_id(target_session->user().groupId().value);
    server_core_->broadcastUserJoined(user_info, target_channel);

    if (old_channel && old_channel != target_channel) {
        server_core_->broadcastUserLeft(target_id, old_channel);
    }

    resp->set_result(nevo::common::ResultCode::OK);
    resp->set_message("User moved");
    sendControl(response, ControlMessageType::MoveUserResponse, request_id);
}

void ClientSession::handleAdminAuth(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_admin_auth_request()) {
        return;
    }

    const auto& req = msg.admin_auth_request();
    const std::string& password = req.password();

    NEVO_LOG_INFO("server", "User {} requesting admin auth", user_.id().value);

    control::ControlMessage response;
    auto* resp = response.mutable_admin_auth_response();

    if (!server_core_) {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Server core not available");
        sendControl(response, ControlMessageType::AdminAuthResponse, request_id);
        return;
    }

    auto result = server_core_->authenticateAdmin(user_.id(), password);
    if (result) {
        resp->set_result(nevo::common::ResultCode::OK);
        resp->set_message("Successfully authenticated as admin");
        NEVO_LOG_INFO("server", "============================================================");
        NEVO_LOG_INFO("server", "       ADMIN AUTH SUCCESSFUL");
        NEVO_LOG_INFO("server", "  User: {} (ID: {})", user_.username(), user_.id().value);
        NEVO_LOG_INFO("server", "  This user now has administrative privileges.");
        NEVO_LOG_INFO("server", "============================================================");

        user_.setGroupId(GROUP_ADMIN);

        if (server_core_ && server_core_->onAdminAuthenticated) {
            server_core_->onAdminAuthenticated(user_.id(), user_.username());
        }

        if (server_core_) {
            auto* ctrl = server_core_->controlServer();
            if (ctrl) {
                auto eventData = ControlJson::make_obj();
                eventData.obj_val["user_id"] = ControlJson::make_num(static_cast<double>(user_.id().value));
                eventData.obj_val["username"] = ControlJson::make_str(user_.username());
                eventData.obj_val["has_owner"] = ControlJson::make_bool(true);
                ctrl->broadcastEvent("admin_auth_success", eventData);
            }
        }
    } else {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message(result.error().message());
        NEVO_LOG_WARN("server", "Admin auth failed for user {}: {}", user_.id().value, result.error().message());
    }

    sendControl(response, ControlMessageType::AdminAuthResponse, request_id);
}

void ClientSession::handleSetServerName(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_set_server_name_request()) {
        return;
    }

    const auto& req = msg.set_server_name_request();
    const std::string& server_name = req.server_name();

    if (!server_core_) {
        control::ControlMessage response;
        auto* resp = response.mutable_set_server_name_response();
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message("Server core not available");
        sendControl(response, ControlMessageType::SetServerNameResponse, request_id);
        return;
    }

    if (user_.groupId() != GROUP_ADMIN) {
        control::ControlMessage response;
        auto* resp = response.mutable_set_server_name_response();
        resp->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
        resp->set_message("Permission denied: admin required");
        sendControl(response, ControlMessageType::SetServerNameResponse, request_id);
        return;
    }

    auto result = server_core_->setServerName(server_name);

    control::ControlMessage response;
    auto* resp = response.mutable_set_server_name_response();
    if (result) {
        resp->set_result(nevo::common::ResultCode::OK);
        resp->set_message("Server name updated successfully");
        NEVO_LOG_INFO("server", "Server name changed to '{}' by admin {}", server_name, user_.username());
    } else {
        resp->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        resp->set_message(result.error().message());
    }
    sendControl(response, ControlMessageType::SetServerNameResponse, request_id);
}

void ClientSession::handleChatSend(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_chat_send()) {
        return;
    }

    const auto& req = msg.chat_send();
    const std::string& text = req.text();

    // 忽略空消息
    if (text.empty()) {
        return;
    }

    // 限制消息长度（防止滥用）
    if (text.size() > 4096) {
        NEVO_LOG_WARN("server", "Chat message too long from user {} ({} bytes)",
                      user_.id().value, text.size());
        return;
    }

    // 确定目标频道
    ChannelId target_channel(req.channel_id());
    if (!target_channel) {
        // channel_id 为 0 表示发送到当前频道
        target_channel = user_.currentChannel();
    }

    if (!target_channel) {
        NEVO_LOG_WARN("server", "Chat message from user {} has no target channel",
                      user_.id().value);
        return;
    }

    NEVO_LOG_INFO("server", "Chat: user={} ({}) in channel={}: {}",
                  user_.username(), user_.id().value, target_channel.value,
                  text.substr(0, 100));

    // 通过 ServerCore 广播到同频道所有用户
    if (server_core_) {
        server_core_->broadcastChatMessage(
            user_.id(), user_.username(), target_channel, text);
    }
}

void ClientSession::handleStunBind(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_stun_bind_request()) {
        return;
    }

    const auto& req = msg.stun_bind_request();
    NEVO_LOG_DEBUG("server", "STUN Bind request from user {}: transaction_id={}",
                   user_.id().value, req.transaction_id());

    // 返回客户端的公网映射地址（从 TCP 连接的远端地址推断）
    control::ControlMessage response;
    auto* resp = response.mutable_stun_bind_response();
    resp->set_transaction_id(req.transaction_id());

    // 将 TCP 远端地址作为映射地址返回
    // 编码格式: 4字节IP + 2字节Port (网络字节序)
    std::string addr_str = remoteAddress();
    std::vector<uint8_t> mapped_addr;
    // 解析 "IP:Port" 格式
    auto colon_pos = addr_str.rfind(':');
    if (colon_pos != std::string::npos) {
        std::string ip_part = addr_str.substr(0, colon_pos);
        std::string port_part = addr_str.substr(colon_pos + 1);

        // 解析 IPv4 地址
        unsigned int a, b, c, d;
        if (sscanf(ip_part.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            mapped_addr.push_back(static_cast<uint8_t>(a));
            mapped_addr.push_back(static_cast<uint8_t>(b));
            mapped_addr.push_back(static_cast<uint8_t>(c));
            mapped_addr.push_back(static_cast<uint8_t>(d));
            uint16_t port = static_cast<uint16_t>(std::stoi(port_part));
            mapped_addr.push_back(static_cast<uint8_t>((port >> 8) & 0xFF));
            mapped_addr.push_back(static_cast<uint8_t>(port & 0xFF));
        }
    }

    resp->set_mapped_address(std::string(mapped_addr.begin(), mapped_addr.end()));

    // 基于现有 NAT 信息判断 NAT 类型
    uint32_t nat_type = 0;  // 默认 Unknown
    switch (nat_info_.type) {
        case NatType::Open:           nat_type = 0; break;
        case NatType::FullCone:       nat_type = 1; break;
        case NatType::Restricted:     nat_type = 2; break;
        case NatType::PortRestricted: nat_type = 3; break;
        case NatType::Symmetric:      nat_type = 4; break;
        case NatType::Blocked:        nat_type = 5; break;
        default:                      nat_type = 0; break;
    }
    resp->set_nat_type(nat_type);

    sendControl(response, ControlMessageType::StunBindResponse, request_id);
}

void ClientSession::handleKeyRotationResponse(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_key_rotation_response()) {
        return;
    }

    const auto& resp = msg.key_rotation_response();
    NEVO_LOG_INFO("server", "Key rotation response from user {}: epoch={}",
                  user_.id().value, resp.key_epoch());

    // 客户端确认密钥轮换成功，记录日志
    // 新的客户端公钥可用于后续轮换（如果提供）
    if (resp.new_client_public_key().size() == 32 && db_) {
        NEVO_LOG_DEBUG("server", "User {} provided new public key for future key exchange",
                       user_.id().value);
    }
}

void ClientSession::handleFileListRequest(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_file_list_request()) return;
    const auto& req = msg.file_list_request();
    ChannelId cid(req.channel_id());

    NEVO_LOG_INFO("server", "User {} requesting file list for channel {}", user_.username(), cid.value);

    if (!db_) {
        control::ControlMessage resp;
        resp.mutable_file_list_response();
        sendControl(resp, ControlMessageType::FileListResponse, request_id);
        return;
    }

    auto files = db_->getFileList(cid.value);
    control::ControlMessage resp;
    auto* r = resp.mutable_file_list_response();
    for (const auto& f : files) {
        auto* entry = r->add_entries();
        entry->set_id(f.id);
        entry->set_channel_id(f.channel_id);
        entry->set_uploader_id(f.uploader_id);
        entry->set_filename(f.filename);
        entry->set_file_size(f.file_size);
        entry->set_upload_time(f.upload_time);
    }
    sendControl(resp, ControlMessageType::FileListResponse, request_id);
}

void ClientSession::handleFileUploadRequest(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_file_upload_request()) return;
    const auto& req = msg.file_upload_request();

    ChannelId cid(req.channel_id());
    const std::string& filename = req.filename();
    uint64_t file_size = req.file_size();

    NEVO_LOG_INFO("server", "User {} uploading '{}' ({} bytes) to channel {}",
                  user_.username(), filename, file_size, cid.value);

    if (!db_ || !channel_mgr_) {
        control::ControlMessage resp;
        auto* r = resp.mutable_file_upload_response();
        r->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        r->set_message("Server error");
        sendControl(resp, ControlMessageType::FileUploadResponse, request_id);
        return;
    }

    auto config = server_core_ ? server_core_->config() : ServerConfig{};
    int max_size_mb = config.file_transfer.max_file_size_mb;
    uint64_t max_bytes = static_cast<uint64_t>(max_size_mb) * 1024 * 1024;
    if (file_size > max_bytes) {
        control::ControlMessage resp;
        auto* r = resp.mutable_file_upload_response();
        r->set_result(nevo::common::ResultCode::ERROR_INVALID_REQUEST);
        r->set_message("File too large");
        sendControl(resp, ControlMessageType::FileUploadResponse, request_id);
        return;
    }

    std::string upload_dir = config.file_transfer.upload_dir;
    std::filesystem::create_directories(upload_dir);

    std::string safe_name = filename;
    size_t pos = safe_name.find_last_of("/\\");
    if (pos != std::string::npos) safe_name = safe_name.substr(pos + 1);
    std::string file_path = upload_dir + "/" + std::to_string(cid.value) + "_" +
                             std::to_string(user_.id().value) + "_" + safe_name;

    auto result = db_->addFileRecord(cid.value, user_.id().value, safe_name, file_path, static_cast<int64_t>(file_size));

    control::ControlMessage resp;
    auto* r = resp.mutable_file_upload_response();
    if (result) {
        r->set_result(nevo::common::ResultCode::OK);
        r->set_message("Ready to upload");
        r->set_file_id(result.value());
        NEVO_LOG_INFO("server", "File upload accepted: id={}, path='{}'", result.value(), file_path);
    } else {
        r->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        r->set_message(result.error().message());
    }
    sendControl(resp, ControlMessageType::FileUploadResponse, request_id);
}

void ClientSession::handleFileDeleteRequest(const control::ControlMessage& msg, uint32_t request_id) {
    if (!msg.has_file_delete_request()) return;
    const auto& req = msg.file_delete_request();
    int64_t fid = req.file_id();

    NEVO_LOG_INFO("server", "User {} deleting file {}", user_.username(), fid);

    if (!db_) {
        control::ControlMessage resp;
        auto* r = resp.mutable_file_delete_response();
        r->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        r->set_message("Database unavailable");
        sendControl(resp, ControlMessageType::FileDeleteResponse, request_id);
        return;
    }

    auto frecord = db_->getFile(fid);
    if (!frecord) {
        control::ControlMessage resp;
        auto* r = resp.mutable_file_delete_response();
        r->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        r->set_message("File not found");
        sendControl(resp, ControlMessageType::FileDeleteResponse, request_id);
        return;
    }

    if (frecord.value().uploader_id != user_.id().value && user_.groupId() != GROUP_ADMIN) {
        control::ControlMessage resp;
        auto* r = resp.mutable_file_delete_response();
        r->set_result(nevo::common::ResultCode::ERROR_PERMISSION_DENIED);
        r->set_message("Permission denied");
        sendControl(resp, ControlMessageType::FileDeleteResponse, request_id);
        return;
    }

    if (!frecord.value().file_path.empty() && std::filesystem::exists(frecord.value().file_path)) {
        std::error_code ec;
        std::filesystem::remove(frecord.value().file_path, ec);
        if (ec) NEVO_LOG_WARN("server", "Failed to delete file '{}': {}", frecord.value().file_path, ec.message());
    }

    auto del_result = db_->deleteFile(fid);
    control::ControlMessage resp;
    auto* r = resp.mutable_file_delete_response();
    if (del_result) {
        r->set_result(nevo::common::ResultCode::OK);
        r->set_message("File deleted");
    } else {
        r->set_result(nevo::common::ResultCode::ERROR_UNKNOWN);
        r->set_message(del_result.error().message());
    }
    sendControl(resp, ControlMessageType::FileDeleteResponse, request_id);
}

} // namespace nevo
