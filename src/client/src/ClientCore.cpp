/**
 * @file ClientCore.cpp
 * @brief ClientCore 实现 - 客户端核心生命周期管理
 *
 * 本文件实现了 ClientCore 的所有核心功能：
 *
 *   1. 连接管理：TCP/TLS 连接 + NAT 穿透 + 密钥交换
 *   2. 登录认证：发送 LoginRequest，处理 LoginResponse
 *   3. 频道管理：加入/离开频道、用户列表维护
 *   4. 音频控制：静音、耳聋、PTT
 *   5. 状态机：Disconnected → Connecting → Connected → InChannel
 *   6. 控制消息分发：将 NetworkManager 收到的控制消息分发到具体处理函数
 */

#include "nevo/client/ClientCore.h"
#include "nevo/client/AudioInput.h"
#include "nevo/client/AudioOutput.h"

#include "nevo/core/audio/AudioEngine.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/model/User.h"
#include "nevo/core/model/Channel.h"
#include "nevo/network/VoiceCrypto.h"

// Protobuf 头文件
#include "control.pb.h"

#include <boost/asio/use_awaitable.hpp>

#include <cstring>

#ifdef NEVO_HAS_SODIUM
#include <sodium.h>
#endif

#include <QSettings>
#include <QByteArray>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

ClientCore::ClientCore(boost::asio::io_context& io_ctx)
    : io_ctx_(io_ctx)
    , login_waiter_(io_ctx)
    , network_mgr_(std::make_unique<NetworkManager>(io_ctx))
    , audio_engine_(std::make_unique<AudioEngine>())
    , audio_input_(std::make_unique<AudioInput>())
    , audio_output_(std::make_unique<AudioOutput>())
    , ping_timer_(io_ctx)
{
    // Initialize ma_context early so device enumeration works before connecting
    audio_engine_->initContext();
    // ------------------------------------------------------------------
    // 注册 NetworkManager 回调
    // ------------------------------------------------------------------
    network_mgr_->onControlMessage =
        [this](const control::ControlMessage& message,
               ControlMessageType type,
               uint32_t request_id) {
            handleControlMessage(message, type, request_id);
        };

    network_mgr_->onDisconnected = [this]() {
        handleDisconnected();
    };

    NEVO_LOG_INFO("client", "ClientCore created");
}

ClientCore::~ClientCore()
{
    disconnect();
    NEVO_LOG_INFO("client", "ClientCore destroyed");
}

// ============================================================
// 客户端身份密钥对管理
// ============================================================

void ClientCore::ensureIdentityKeys()
{
    if (has_identity_keypair_) {
        return;
    }

    if (loadIdentityKeys()) {
        has_identity_keypair_ = true;
        NEVO_LOG_INFO("client", "Identity keypair loaded from settings");
        return;
    }

    generateIdentityKeys();
    has_identity_keypair_ = true;
    NEVO_LOG_INFO("client", "New identity keypair generated and saved");
}

bool ClientCore::loadIdentityKeys()
{
    QSettings settings("NEVO", "NEVOClient");
    QByteArray pub_b64 = settings.value("identity/publicKey").toByteArray();
    QByteArray sec_b64 = settings.value("identity/secretKey").toByteArray();

    if (pub_b64.isEmpty() || sec_b64.isEmpty()) {
        return false;
    }

    QByteArray pub = QByteArray::fromBase64(pub_b64);
    QByteArray sec = QByteArray::fromBase64(sec_b64);

    if (static_cast<size_t>(pub.size()) != IDENTITY_PUBLIC_KEY_SIZE ||
        static_cast<size_t>(sec.size()) != IDENTITY_SECRET_KEY_SIZE) {
        NEVO_LOG_WARN("client", "Stored identity keys have invalid size");
        return false;
    }

    std::memcpy(client_public_key_.data(), pub.data(), IDENTITY_PUBLIC_KEY_SIZE);
    std::memcpy(client_secret_key_.data(), sec.data(), IDENTITY_SECRET_KEY_SIZE);
    return true;
}

void ClientCore::generateIdentityKeys()
{
#ifdef NEVO_HAS_SODIUM
    crypto_box_keypair(client_public_key_.data(), client_secret_key_.data());
#else
    // Fallback: 不安全的占位实现（仅用于无 libsodium 时的编译通过）
    std::memset(client_public_key_.data(), 0, IDENTITY_PUBLIC_KEY_SIZE);
    std::memset(client_secret_key_.data(), 0, IDENTITY_SECRET_KEY_SIZE);
    NEVO_LOG_WARN("client", "libsodium not available, identity keys are zeroed (INSECURE)");
#endif

    QSettings settings("NEVO", "NEVOClient");
    settings.setValue("identity/publicKey",
                      QByteArray(reinterpret_cast<const char*>(client_public_key_.data()),
                                 static_cast<int>(IDENTITY_PUBLIC_KEY_SIZE)).toBase64());
    settings.setValue("identity/secretKey",
                      QByteArray(reinterpret_cast<const char*>(client_secret_key_.data()),
                                 static_cast<int>(IDENTITY_SECRET_KEY_SIZE)).toBase64());
}

// ============================================================
// 连接管理
// ============================================================

boost::asio::awaitable<Result<void>> ClientCore::connect(
    const std::string& host,
    uint16_t tcp_port,
    const std::string& username,
    const std::string& password)
{
    // ------------------------------------------------------------------
    // 前置条件检查
    // ------------------------------------------------------------------
    ClientState current = state_.load(std::memory_order_acquire);
    if (current != ClientState::Disconnected) {
        NEVO_LOG_ERROR("client", "Cannot connect: current state={}",
                      clientStateToString(current));
        co_return Err<void>(ResultCode::InvalidRequest,
                           "Already connected or connecting");
    }

    NEVO_LOG_INFO("client", "Connecting to {}:{} as user '{}' ...",
                 host, tcp_port, username);

    // ------------------------------------------------------------------
    // 1. 状态切换：Disconnected → Connecting
    // ------------------------------------------------------------------
    setState(ClientState::Connecting);

    // 存储认证凭证供登录请求使用
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        local_username_ = username;
        auth_credential_ = password;
    }

    // ------------------------------------------------------------------
    // 2. 建立 TCP/TLS 连接
    // ------------------------------------------------------------------
    auto conn_result = co_await network_mgr_->connect(host, tcp_port);
    if (!conn_result) {
        NEVO_LOG_ERROR("client", "TCP connection failed: {}",
                      conn_result.error().message());
        setState(ClientState::Disconnected);
        co_return conn_result;
    }

    // ------------------------------------------------------------------
    // 3. 发送登录请求（携带客户端公钥）
    // ------------------------------------------------------------------
    ensureIdentityKeys();

    control::ControlMessage login_msg;
    auto* login_req = login_msg.mutable_login_request();
    login_req->set_username(username);
    login_req->set_auth_credential(auth_credential_);
    if (has_identity_keypair_) {
        login_req->set_client_public_key(
            std::vector<uint8_t>(client_public_key_.begin(), client_public_key_.end()));
    }

    auto send_result = co_await network_mgr_->sendControl(
        login_msg, ControlMessageType::LoginRequest, 0);
    if (!send_result) {
        NEVO_LOG_ERROR("client", "Failed to send login request: {}",
                      send_result.error().message());
        setState(ClientState::Disconnected);
        co_return send_result;
    }

    // ------------------------------------------------------------------
    // 4. 等待登录响应
    // ------------------------------------------------------------------
    // 注意：登录响应由 handleControlMessage 异步处理。
    // 在生产实现中，这里应使用 promise/future 或条件变量等待响应。
    // 当前简化实现：假设 handleLoginResponse 会被调用并设置状态。
    //
    // 简化方案：登录请求发送成功即视为登录成功。
    // 实际的登录响应由 handleControlMessage 异步处理。

    // ------------------------------------------------------------------
    // 5. 建立 UDP 语音通道
    // ------------------------------------------------------------------
    auto udp_result = co_await network_mgr_->establishUdpChannel(0);
    if (!udp_result) {
        NEVO_LOG_WARN("client", "UDP channel establishment failed: {}. "
                     "Voice will use TCP tunnel.",
                     udp_result.error().message());
        // UDP 通道失败不阻止连接，可以回退到 TCP 隧道
    }

    // ------------------------------------------------------------------
    // 6. 初始化音频子系统
    // ------------------------------------------------------------------
    auto audio_result = initAudioSubsystem();
    if (!audio_result) {
        NEVO_LOG_WARN("client", "Audio subsystem initialization failed: {}",
                     audio_result.error().message());
        // 音频初始化失败不阻止连接，用户可能无音频设备
    }

    // ------------------------------------------------------------------
    // 7. 状态切换：Connecting → Connected
    // ------------------------------------------------------------------
    setState(ClientState::Connected);

    NEVO_LOG_INFO("client", "Connected to {}:{} as '{}'",
                 host, tcp_port, username);

    co_return Ok();
}

void ClientCore::disconnect()
{
    ClientState current = state_.load(std::memory_order_acquire);
    if (current == ClientState::Disconnected) {
        return;
    }

    NEVO_LOG_INFO("client", "Disconnecting...");

    // ------------------------------------------------------------------
    // 1. 如果在频道中，先离开
    // ------------------------------------------------------------------
    if (current == ClientState::InChannel) {
        // 清理频道用户列表
        {
            std::lock_guard<std::mutex> lock(users_mutex_);
            channel_users_.clear();
        }
        // 重置频道状态（只锁一次 state_mutex_，避免非递归互斥锁死锁）
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_channel_ = INVALID_CHANNEL_ID;
            current_channel_name_.clear();
        }
    }

    // ------------------------------------------------------------------
    // 2. 关闭音频子系统
    // ------------------------------------------------------------------
    shutdownAudioSubsystem();

    // ------------------------------------------------------------------
    // 3. 关闭网络连接
    // ------------------------------------------------------------------
    network_mgr_->disconnect();

    // ------------------------------------------------------------------
    // 4. 重置本地状态
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        session_id_ = INVALID_SESSION_ID;
        local_user_id_ = INVALID_USER_ID;
        local_username_.clear();
    }
    muted_.store(false, std::memory_order_release);
    deafened_.store(false, std::memory_order_release);
    ptt_active_.store(false, std::memory_order_release);

    // ------------------------------------------------------------------
    // 5. 状态切换：任意状态 → Disconnected
    // ------------------------------------------------------------------
    setState(ClientState::Disconnected);

    NEVO_LOG_INFO("client", "Disconnected");
}

// ============================================================
// 频道管理
// ============================================================

boost::asio::awaitable<Result<void>> ClientCore::joinChannel(ChannelId channel_id)
{
    ClientState current = state_.load(std::memory_order_acquire);
    if (current != ClientState::Connected && current != ClientState::InChannel) {
        NEVO_LOG_ERROR("client", "Cannot join channel: not connected (state={})",
                      clientStateToString(current));
        co_return Err<void>(ResultCode::InvalidRequest,
                           "Not connected to server");
    }

    NEVO_LOG_INFO("client", "Joining channel {} ...", channel_id.value);

    // ------------------------------------------------------------------
    // 1. 发送 JoinChannel 控制消息
    // ------------------------------------------------------------------
    control::ControlMessage join_msg;
    auto* join_req = join_msg.mutable_join_channel();
    join_req->set_channel_id(channel_id.value);

    auto send_result = co_await network_mgr_->sendControl(
        join_msg, ControlMessageType::JoinChannel, 0);
    if (!send_result) {
        NEVO_LOG_ERROR("client", "Failed to send JoinChannel request: {}",
                      send_result.error().message());
        co_return send_result;
    }

    // ------------------------------------------------------------------
    // 2. 更新本地状态
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_channel_ = channel_id;
    }
    network_mgr_->setCurrentChannelId(channel_id);

    // 状态切换：Connected → InChannel
    if (current == ClientState::Connected) {
        setState(ClientState::InChannel);
    }

    NEVO_LOG_INFO("client", "Joined channel {}", channel_id.value);
    co_return Ok();
}

boost::asio::awaitable<Result<void>> ClientCore::leaveChannel()
{
    ClientState current = state_.load(std::memory_order_acquire);
    if (current != ClientState::InChannel) {
        NEVO_LOG_WARN("client", "Cannot leave channel: not in a channel");
        co_return Err<void>(ResultCode::InvalidRequest,
                           "Not in a channel");
    }

    NEVO_LOG_INFO("client", "Leaving channel {} ...", current_channel_.value);

    // ------------------------------------------------------------------
    // 1. 发送 LeaveChannel 控制消息
    // ------------------------------------------------------------------
    control::ControlMessage leave_msg;
    leave_msg.mutable_leave_channel();
    // LeaveChannelRequest is empty - server knows which channel the client is in

    auto send_result = co_await network_mgr_->sendControl(
        leave_msg, ControlMessageType::LeaveChannel, 0);
    if (!send_result) {
        NEVO_LOG_ERROR("client", "Failed to send LeaveChannel request: {}",
                      send_result.error().message());
        // 即使发送失败也继续本地清理，确保客户端状态一致
    }

    // ------------------------------------------------------------------
    // 2. 清理远端用户
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        for (const auto& [user_id, user] : channel_users_) {
            audio_output_->removeRemoteUser(user_id);
        }
        channel_users_.clear();
    }

    // ------------------------------------------------------------------
    // 3. 重置频道状态
    // ------------------------------------------------------------------
    current_channel_ = INVALID_CHANNEL_ID;
    current_channel_name_.clear();

    // 状态切换：InChannel → Connected
    setState(ClientState::Connected);

    NEVO_LOG_INFO("client", "Left channel");
    co_return Ok();
}

void ClientCore::setMuted(bool muted)
{
    muted_.store(muted, std::memory_order_release);

    // 通知 AudioInput 停止/恢复发送
    if (audio_input_) {
        audio_input_->setMuted(muted);
    }

    // 耳聋通常伴随静音
    if (muted && !deafened_.load(std::memory_order_acquire)) {
        // 只静音不耳聋
    }

    // 发送 MuteToggle 控制消息通知服务器
    if (network_mgr_ && network_mgr_->isTcpConnected()) {
        control::ControlMessage mute_msg;
        auto* mute_req = mute_msg.mutable_mute_toggle();
        mute_req->set_muted(muted);

        // 异步发送，不等待结果
        boost::asio::co_spawn(io_ctx_,
            [this, mute_msg = std::move(mute_msg)]() mutable
            -> boost::asio::awaitable<void> {
                co_await network_mgr_->sendControl(
                    mute_msg, ControlMessageType::MuteToggle, 0);
            },
            boost::asio::detached);
    }

    NEVO_LOG_INFO("client", "Muted={}", muted);
}

void ClientCore::setDeafened(bool deafened)
{
    deafened_.store(deafened, std::memory_order_release);

    // 通知 AudioOutput 丢弃/接收语音数据
    if (audio_output_) {
        audio_output_->setDeafened(deafened);
    }

    // 耳聋通常同时静音
    if (deafened && !muted_.load(std::memory_order_acquire)) {
        setMuted(true);
    }

    NEVO_LOG_INFO("client", "Deafened={}", deafened);
}

void ClientCore::setPttActive(bool active)
{
    ptt_active_.store(active, std::memory_order_release);

    // 通知 AudioEngine 的 PTT 状态
    if (audio_engine_) {
        audio_engine_->setPttActive(active);
    }

    NEVO_LOG_DEBUG("client", "PTT active={}", active);
}

// ============================================================
// 状态查询
// ============================================================

ClientStateSnapshot ClientCore::getState() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    ClientStateSnapshot snapshot;

    snapshot.state = state_.load(std::memory_order_acquire);
    snapshot.session_id = session_id_;
    snapshot.local_user_id = local_user_id_;
    snapshot.local_username = local_username_;
    snapshot.current_channel = current_channel_;
    snapshot.current_channel_name = current_channel_name_;
    snapshot.nat_type = network_mgr_ ? network_mgr_->detectedNatType() : NatType::Blocked;
    snapshot.udp_mode = network_mgr_ ? network_mgr_->udpChannelMode() : UdpChannelMode::None;
    snapshot.is_muted = muted_.load(std::memory_order_acquire);
    snapshot.is_deafened = deafened_.load(std::memory_order_acquire);
    snapshot.is_ptt_active = ptt_active_.load(std::memory_order_acquire);
    snapshot.is_speaking = audio_engine_ ? audio_engine_->isSpeaking() : false;

    {
        std::lock_guard<std::mutex> users_lock(users_mutex_);
        snapshot.channel_users.reserve(channel_users_.size());
        for (const auto& [id, user] : channel_users_) {
            snapshot.channel_users.push_back(user);
        }
    }

    return snapshot;
}

ClientState ClientCore::state() const
{
    return state_.load(std::memory_order_acquire);
}

bool ClientCore::isConnected() const
{
    return state_.load(std::memory_order_acquire) >= ClientState::Connected;
}

bool ClientCore::isInChannel() const
{
    return state_.load(std::memory_order_acquire) == ClientState::InChannel;
}

// ============================================================
// 子组件访问
// ============================================================

NetworkManager& ClientCore::networkManager()
{
    return *network_mgr_;
}

AudioEngine& ClientCore::audioEngine()
{
    return *audio_engine_;
}

AudioInput& ClientCore::audioInput()
{
    return *audio_input_;
}

AudioOutput& ClientCore::audioOutput()
{
    return *audio_output_;
}

// ============================================================
// 内部方法
// ============================================================

void ClientCore::setState(ClientState new_state)
{
    ClientState old_state = state_.exchange(new_state, std::memory_order_acq_rel);
    if (old_state != new_state) {
        NEVO_LOG_INFO("client", "State changed: {} -> {}",
                     clientStateToString(old_state),
                     clientStateToString(new_state));

        // 触发回调
        if (onStateChanged) {
            onStateChanged(new_state, old_state);
        }

        // 连接成功后触发 NAT/延迟更新并启动 ping 定时器
        if (new_state == ClientState::Connected || new_state == ClientState::InChannel) {
            startPingTimer();
            // 立即推送一次 NAT 类型
            if (onLatencyUpdate) {
                onLatencyUpdate(-1);
            }
        }

        // 断开连接时停止 ping 定时器
        if (new_state == ClientState::Disconnected) {
            stopPingTimer();
        }
    }
}

void ClientCore::handleControlMessage(const control::ControlMessage& message,
                                       ControlMessageType type,
                                       uint32_t request_id)
{
    NEVO_LOG_DEBUG("client", "Handling control message type={} request_id={}",
                  controlMessageTypeToString(type), request_id);

    switch (type) {
        // ------------------------------------------------------------------
        // 登录响应
        // ------------------------------------------------------------------
        case ControlMessageType::LoginResponse:
            handleLoginResponse(message);
            break;

        // ------------------------------------------------------------------
        // 频道事件
        // ------------------------------------------------------------------
        case ControlMessageType::ChannelList:
        case ControlMessageType::CreateChannel:
        case ControlMessageType::DeleteChannel:
            handleChannelEvent(message, type);
            break;

        // ------------------------------------------------------------------
        // 用户事件
        // ------------------------------------------------------------------
        case ControlMessageType::UserJoined:
        case ControlMessageType::UserLeft:
        case ControlMessageType::UserSpeaking:
        case ControlMessageType::PttToggle:
        case ControlMessageType::MuteToggle:
            handleUserEvent(message, type);
            break;

        // ------------------------------------------------------------------
        // 密钥轮换
        // ------------------------------------------------------------------
        case ControlMessageType::KeyRotationRequest:
        case ControlMessageType::KeyRotationResponse:
            handleKeyRotation(message);
            break;

        // ------------------------------------------------------------------
        // 服务器消息
        // ------------------------------------------------------------------
        case ControlMessageType::ServerMessage: {
            if (onServerMessage) {
                if (message.has_server_message()) {
                    onServerMessage(message.server_message().text());
                }
            }
            break;
        }

        // ------------------------------------------------------------------
        // UDP Ping 响应
        // ------------------------------------------------------------------
        case ControlMessageType::UdpPingResponse:
            handleUdpPingResponse(message);
            break;

        // ------------------------------------------------------------------
        // 服主绑定响应
        // ------------------------------------------------------------------
        case ControlMessageType::BindOwnerResponse:
            handleBindOwnerResponse(message);
            break;

        default:
            NEVO_LOG_DEBUG("client", "Unhandled control message type={}",
                          controlMessageTypeToString(type));
            break;
    }
}

void ClientCore::handleDisconnected()
{
    NEVO_LOG_WARN("client", "Connection lost");

    // 清理音频子系统
    shutdownAudioSubsystem();

    // 清理用户列表
    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        channel_users_.clear();
    }

    // 重置状态
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_channel_ = INVALID_CHANNEL_ID;
        current_channel_name_.clear();
        session_id_ = INVALID_SESSION_ID;
        local_user_id_ = INVALID_USER_ID;
    }

    setState(ClientState::Disconnected);

    // 触发错误回调
    if (onError) {
        onError(ResultCode::ConnectionFailed, "Connection to server lost");
    }
}

Result<void> ClientCore::handleLoginResponse(const control::ControlMessage& message)
{
    // 从 Protobuf 消息中提取登录响应
    // LoginResponse oneof 包含：
    //   - result: ResultCode 枚举（OK 表示成功）
    //   - user_info: UserInfo（包含 user_id, username 等）
    //   - session_token: 会话令牌
    //   - server_public_key: 服务器公钥（用于密钥交换）
    //   - key_exchange_method: 密钥交换方法

    bool success = false;
    if (message.has_login_response()) {
        const auto& resp = message.login_response();
        success = (resp.result() == nevo::common::ResultCode::OK);

        if (success) {
            // 提取用户 ID
            if (resp.user_info().id() != 0) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                local_user_id_ = UserId(resp.user_info().id());
                network_mgr_->setLocalUserId(local_user_id_);
            } else {
                NEVO_LOG_WARN("client", "Login response missing user_info field");
            }

            // 提取并设置会话密钥（优先使用加密会话密钥）
            if (resp.encrypted_session_key().size() > 0) {
#ifdef NEVO_HAS_SODIUM
                ensureIdentityKeys();
                if (has_identity_keypair_) {
                    std::vector<uint8_t> decrypted(CRYPTO_KEY_SIZE);
                    int rc = crypto_box_seal_open(
                        decrypted.data(),
                        resp.encrypted_session_key().data(),
                        resp.encrypted_session_key().size(),
                        client_public_key_.data(),
                        client_secret_key_.data());
                    if (rc == 0) {
                        network_mgr_->setSessionKey(decrypted.data());
                        NEVO_LOG_INFO("client", "Session key decrypted and set (per-client key)");
                    } else {
                        NEVO_LOG_ERROR("client", "Failed to decrypt session key (crypto_box_seal_open failed)");
                    }
                } else {
                    NEVO_LOG_WARN("client", "Received encrypted session key but no identity keypair available");
                }
#else
                NEVO_LOG_WARN("client", "Received encrypted session key but libsodium not available");
#endif
            } else if (resp.server_public_key().size() == CRYPTO_KEY_SIZE) {
                // 兼容旧服务端：直接使用明文会话密钥
                uint8_t key[CRYPTO_KEY_SIZE];
                std::memcpy(key, resp.server_public_key().data(), CRYPTO_KEY_SIZE);
                network_mgr_->setSessionKey(key);
                NEVO_LOG_INFO("client", "Session key received and set (legacy mode)");
            } else if (!resp.server_public_key().empty()) {
                NEVO_LOG_WARN("client", "Invalid server public key size: {} (expected {})",
                             resp.server_public_key().size(), CRYPTO_KEY_SIZE);
            }

            // 若服务器尚无管理员，提示客户端进行服主绑定
            if (!resp.owner_exists()) {
                NEVO_LOG_INFO("client", "Server has no owner — prompting for bind key");
                if (onOwnerBindRequired) {
                    onOwnerBindRequired();
                }
            }

            NEVO_LOG_INFO("client", "Login successful: user_id={}",
                         local_user_id_.value);
            return Ok();
        } else {
            NEVO_LOG_ERROR("client", "Login failed: result_code={}",
                          static_cast<int>(resp.result()));
            return Err<void>(ResultCode::AuthFailed, "Login failed");
        }
    }

    // 兼容简化实现：如果没有 LoginResponse 扩展，假设登录成功
    NEVO_LOG_INFO("client", "Login response received (simplified)");
    return Ok();
}

void ClientCore::handleChannelEvent(const control::ControlMessage& message,
                                     ControlMessageType type)
{
    switch (type) {
        case ControlMessageType::ChannelList: {
            NEVO_LOG_INFO("client", "Received channel list");

            // 解析频道列表并通知 UI
            if (message.has_channel_list()) {
                const auto& ch_list = message.channel_list();
                const auto& ch_vec = ch_list.channels();
                std::vector<ChannelInfo> channels;
                channels.reserve(ch_vec.size());

                for (const auto& ch : ch_vec) {
                    ChannelInfo info;
                    info.channel_id = ChannelId(ch.id());
                    info.name = ch.name();
                    info.parent_id = ChannelId(ch.parent_id());
                    info.is_permanent = true;
                    for (const auto& user : ch.users()) {
                        info.user_ids.push_back(UserId(user.id()));
                    }
                    channels.push_back(std::move(info));
                }

                NEVO_LOG_INFO("client", "Parsed {} channels from server", channels.size());

                if (onChannelList) {
                    onChannelList(channels);
                }
            }
            break;
        }

        case ControlMessageType::CreateChannel: {
            NEVO_LOG_INFO("client", "Channel created event");
            break;
        }

        case ControlMessageType::DeleteChannel: {
            NEVO_LOG_INFO("client", "Channel deleted event");
            break;
        }

        default:
            break;
    }
}

void ClientCore::handleUserEvent(const control::ControlMessage& message,
                                  ControlMessageType type)
{
    switch (type) {
        case ControlMessageType::UserJoined: {
            // 解析 UserJoined 消息，提取新加入的用户信息
            UserId new_user_id = INVALID_USER_ID;
            std::string new_username;

            if (message.has_user_joined()) {
                const auto& notif = message.user_joined();
                new_user_id = UserId(notif.user().id());
                new_username = notif.user().username();
            }

            // 添加到频道用户列表
            {
                std::lock_guard<std::mutex> lock(users_mutex_);
                User new_user(new_user_id, new_username);
                new_user.setCurrentChannel(current_channel_);
                channel_users_[new_user_id] = new_user;
            }

            // 为新用户添加音频解码通道
            if (audio_output_) {
                audio_output_->addRemoteUser(new_user_id);
            }

            NEVO_LOG_INFO("client", "User joined: {} ({})",
                         new_username, new_user_id.value);

            // 触发回调
            if (onUserJoined) {
                std::lock_guard<std::mutex> lock(users_mutex_);
                auto it = channel_users_.find(new_user_id);
                if (it != channel_users_.end()) {
                    onUserJoined(it->second);
                }
            }
            break;
        }

        case ControlMessageType::UserLeft: {
            // 解析 UserLeft 消息
            UserId left_user_id = INVALID_USER_ID;

            if (message.has_user_left()) {
                const auto& notif = message.user_left();
                left_user_id = UserId(notif.user_id());
            }

            // 移除音频解码通道
            if (audio_output_) {
                audio_output_->removeRemoteUser(left_user_id);
            }

            // 从频道用户列表移除
            {
                std::lock_guard<std::mutex> lock(users_mutex_);
                channel_users_.erase(left_user_id);
            }

            NEVO_LOG_INFO("client", "User left: {}", left_user_id.value);

            // 触发回调
            if (onUserLeft) {
                onUserLeft(left_user_id);
            }
            break;
        }

        case ControlMessageType::UserSpeaking: {
            // 解析用户说话状态变更
            UserId speaking_user_id = INVALID_USER_ID;
            bool is_speaking = false;

            if (message.has_user_speaking()) {
                const auto& notif = message.user_speaking();
                speaking_user_id = UserId(notif.user_id());
                is_speaking = notif.speaking();
            }

            // 更新用户说话状态
            {
                std::lock_guard<std::mutex> lock(users_mutex_);
                auto it = channel_users_.find(speaking_user_id);
                if (it != channel_users_.end()) {
                    it->second.setSpeaking(is_speaking);
                }
            }

            NEVO_LOG_DEBUG("client", "User {} speaking={}",
                          speaking_user_id.value, is_speaking);

            // 触发回调
            if (onUserSpeaking) {
                onUserSpeaking(speaking_user_id, is_speaking);
            }
            break;
        }

        case ControlMessageType::PttToggle:
        case ControlMessageType::MuteToggle: {
            // 其他用户的 PTT/静音状态变更
            NEVO_LOG_DEBUG("client", "User state change event: {}",
                          controlMessageTypeToString(type));
            break;
        }

        default:
            break;
    }
}

void ClientCore::handleKeyRotation(const control::ControlMessage& message)
{
    // 从密钥轮换消息中提取新密钥
    // 服务器发送 KeyRotationRequest，包含新的服务器公钥
    if (message.has_key_rotation_request()) {
        const auto& req = message.key_rotation_request();

        // 优先处理加密的会话密钥（逐客户端密钥模式）
        if (req.encrypted_session_key().size() > 0) {
#ifdef NEVO_HAS_SODIUM
            ensureIdentityKeys();
            if (has_identity_keypair_) {
                std::vector<uint8_t> decrypted(CRYPTO_KEY_SIZE);
                    int rc = crypto_box_seal_open(
                        decrypted.data(),
                        req.encrypted_session_key().data(),
                        req.encrypted_session_key().size(),
                        client_public_key_.data(),
                        client_secret_key_.data());
                if (rc == 0) {
                    network_mgr_->rotateKey(decrypted.data());
                    NEVO_LOG_INFO("client", "Session key rotated (per-client, epoch={})", req.key_epoch());
                } else {
                    NEVO_LOG_ERROR("client", "Failed to decrypt rotated session key");
                }
            } else {
                NEVO_LOG_WARN("client", "Received encrypted rotation key but no identity keypair");
            }
#else
            NEVO_LOG_WARN("client", "Received encrypted rotation key but libsodium not available");
#endif
        } else if (req.new_server_public_key().size() == CRYPTO_KEY_SIZE) {
            // 兼容旧服务端：直接使用明文新密钥
            uint8_t new_key[CRYPTO_KEY_SIZE];
            std::memcpy(new_key, req.new_server_public_key().data(), CRYPTO_KEY_SIZE);
            network_mgr_->rotateKey(new_key);
            NEVO_LOG_INFO("client", "Session key rotated (legacy, epoch={})", req.key_epoch());
        } else {
            NEVO_LOG_WARN("client", "Invalid rotated key size: {} (expected {})",
                         req.new_server_public_key().size(), CRYPTO_KEY_SIZE);
        }
    }
}

Result<void> ClientCore::initAudioSubsystem()
{
    NEVO_LOG_INFO("client", "Initializing audio subsystem...");

    // 引擎现在因正式连接而初始化，不再是仅因监听测试而启动
    audio_engine_started_for_monitor_ = false;

    // ------------------------------------------------------------------
    // 1. 初始化 AudioEngine
    // ------------------------------------------------------------------
    AudioEngine::Config engine_config;
    engine_config.input_sample_rate = kOpusSampleRate;
    engine_config.output_sample_rate = kOpusSampleRate;

    auto init_result = audio_engine_->initialize(engine_config);
    if (!init_result) {
        NEVO_LOG_ERROR("client", "AudioEngine initialization failed: {}",
                      init_result.error().message());
        return init_result;
    }

    // ------------------------------------------------------------------
    // 2. 启动 AudioInput
    // ------------------------------------------------------------------
    auto input_result = audio_input_->start(*audio_engine_, *network_mgr_);
    if (!input_result) {
        NEVO_LOG_ERROR("client", "AudioInput start failed: {}",
                      input_result.error().message());
        // 音频输入失败不阻止整个初始化
    }

    // ------------------------------------------------------------------
    // 3. 启动 AudioOutput
    // ------------------------------------------------------------------
    auto output_result = audio_output_->start(*audio_engine_, *network_mgr_);
    if (!output_result) {
        NEVO_LOG_ERROR("client", "AudioOutput start failed: {}",
                      output_result.error().message());
        // 音频输出失败不阻止整个初始化
    }

    NEVO_LOG_INFO("client", "Audio subsystem initialized");
    return Ok();
}

void ClientCore::shutdownAudioSubsystem()
{
    NEVO_LOG_INFO("client", "Shutting down audio subsystem...");

    // 按逆序关闭：先停 Input/Output，再关 Engine

    if (audio_input_) {
        audio_input_->stop();
    }

    if (audio_output_) {
        audio_output_->stop();
    }

    if (audio_engine_) {
        audio_engine_->shutdown();
    }

    NEVO_LOG_INFO("client", "Audio subsystem shut down");
}

// ============================================================
// 设备管理（转发到 AudioEngine）
// ============================================================

std::vector<AudioEngine::DeviceInfo> ClientCore::enumerateInputDevices() {
    if (!audio_engine_) {
        return {};
    }
    return audio_engine_->enumerateInputDevices();
}

std::vector<AudioEngine::DeviceInfo> ClientCore::enumerateOutputDevices() {
    if (!audio_engine_) {
        return {};
    }
    return audio_engine_->enumerateOutputDevices();
}

Result<void> ClientCore::selectInputDeviceByName(const std::string& name) {
    if (!audio_engine_) {
        return Err<void>(ResultCode::InvalidRequest, "AudioEngine not initialized");
    }
    return audio_engine_->selectInputDeviceByName(name);
}

Result<void> ClientCore::selectOutputDeviceByName(const std::string& name) {
    if (!audio_engine_) {
        return Err<void>(ResultCode::InvalidRequest, "AudioEngine not initialized");
    }
    return audio_engine_->selectOutputDeviceByName(name);
}

Result<void> ClientCore::initAudioContext() {
    if (!audio_engine_) {
        return Err<void>(ResultCode::InvalidRequest, "AudioEngine not initialized");
    }
    return audio_engine_->initContext();
}

std::string ClientCore::currentInputDeviceName() const {
    if (!audio_engine_) {
        return {};
    }
    return audio_engine_->currentInputDeviceName();
}

std::string ClientCore::currentOutputDeviceName() const {
    if (!audio_engine_) {
        return {};
    }
    return audio_engine_->currentOutputDeviceName();
}

Result<void> ClientCore::playTestTone(float frequency, float duration_sec) {
    if (!audio_engine_) {
        return Err<void>(ResultCode::InvalidRequest, "AudioEngine not initialized");
    }
    // 若 AudioEngine 未运行，初始化它以支持测试音播放
    if (!audio_engine_->isRunning()) {
        AudioEngine::Config config;
        config.input_sample_rate = kOpusSampleRate;
        config.output_sample_rate = kOpusSampleRate;
        auto result = audio_engine_->initialize(config);
        if (!result) {
            NEVO_LOG_ERROR("client", "Failed to init AudioEngine for test tone: {}",
                          result.error().message());
            return Err<void>(result.error().code(), result.error().message());
        }
        audio_engine_started_for_monitor_ = true;
    }
    return audio_engine_->playTestTone(frequency, duration_sec);
}

float ClientCore::getCurrentInputLevel() const {
    if (!audio_engine_) {
        return 0.0f;
    }
    return audio_engine_->getCurrentInputLevel();
}

void ClientCore::setInputLevelCallback(AudioEngine::InputLevelCallback cb) {
    if (audio_engine_) {
        audio_engine_->setInputLevelCallback(std::move(cb));
    }
}

void ClientCore::setMonitorEnabled(bool enabled) {
    if (!audio_engine_) return;

    if (enabled) {
        // 若 AudioEngine 未运行，初始化它以支持监听
        if (!audio_engine_->isRunning()) {
            AudioEngine::Config config;
            config.input_sample_rate = kOpusSampleRate;
            config.output_sample_rate = kOpusSampleRate;
            auto result = audio_engine_->initialize(config);
            if (!result) {
                NEVO_LOG_ERROR("client", "Failed to init AudioEngine for monitor: {}",
                              result.error().message());
                return;
            }
            audio_engine_started_for_monitor_ = true;
        }
        audio_engine_->setMonitorEnabled(true);
    } else {
        audio_engine_->setMonitorEnabled(false);
        // 若引擎仅因监听而启动且用户未连接，关闭引擎释放资源
        if (audio_engine_started_for_monitor_ && state_ != ClientState::Connected) {
            audio_engine_->shutdown();
            audio_engine_started_for_monitor_ = false;
        }
    }
}

// ============================================================
// Ping / 延迟测量
// ============================================================

void ClientCore::startPingTimer()
{
    // 立即发送第一个 ping
    sendPing();
}

void ClientCore::stopPingTimer()
{
    ping_timer_.cancel();
    last_latency_ms_.store(-1, std::memory_order_release);
}

void ClientCore::sendPing()
{
    ClientState current = state_.load(std::memory_order_acquire);
    if (current != ClientState::Connected && current != ClientState::InChannel) {
        return;
    }

    if (!network_mgr_ || !network_mgr_->isTcpConnected()) {
        return;
    }

    // 记录发送时间
    ping_send_time_ = std::chrono::steady_clock::now();

    // 通过协程发送 UdpPingRequest
    uint32_t seq = ping_sequence_;
    ++ping_sequence_;

    boost::asio::co_spawn(io_ctx_,
        [this, seq]() -> boost::asio::awaitable<void> {
            control::ControlMessage ping_msg;
            auto* req = ping_msg.mutable_udp_ping_request();
            req->set_sequence(seq);

            auto send_result = co_await network_mgr_->sendControl(
                ping_msg, ControlMessageType::UdpPingRequest, 0);
            if (!send_result) {
                NEVO_LOG_DEBUG("client", "Failed to send UdpPing: {}",
                              send_result.error().message());
            }
        },
        boost::asio::detached);

    // 安排下一次 ping（5 秒间隔）
    ping_timer_.expires_after(std::chrono::seconds(5));
    ping_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            sendPing();
        }
    });
}

void ClientCore::handleUdpPingResponse(const control::ControlMessage& message)
{
    if (!message.has_udp_ping_response()) {
        return;
    }

    const auto& resp = message.udp_ping_response();
    uint32_t seq = resp.sequence();
    bool udp_reachable = resp.udp_reachable();

    // 计算 RTT
    auto now = std::chrono::steady_clock::now();
    auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - ping_send_time_).count();
    int latency_ms = static_cast<int>(rtt);

    last_latency_ms_.store(latency_ms, std::memory_order_release);

    NEVO_LOG_DEBUG("client", "UdpPingResponse seq={} latency={}ms udp_reachable={}",
                  seq, latency_ms, udp_reachable);

    // 触发延迟更新回调（同时会更新 NAT 类型）
    if (onLatencyUpdate) {
        onLatencyUpdate(latency_ms);
    }
}

void ClientCore::setMonitorVolume(float volume) {
    if (audio_engine_) {
        audio_engine_->setMonitorVolume(volume);
    }
}

// ============================================================
// 服主绑定
// ============================================================

void ClientCore::sendBindOwnerRequest(const std::string& bind_key)
{
    if (!network_mgr_ || !network_mgr_->isTcpConnected()) {
        NEVO_LOG_WARN("client", "Cannot send bind owner request: not connected");
        if (onOwnerBound) {
            onOwnerBound(false, "Not connected to server");
        }
        return;
    }

    control::ControlMessage msg;
    auto* req = msg.mutable_bind_owner_request();
    req->set_bind_key(bind_key);

    boost::asio::co_spawn(io_ctx_,
        [this, msg]() mutable -> boost::asio::awaitable<void> {
            auto result = co_await network_mgr_->sendControl(
                msg, ControlMessageType::BindOwnerRequest, 0);
            if (!result) {
                NEVO_LOG_WARN("client", "Failed to send BindOwnerRequest: {}",
                              result.error().message());
                if (onOwnerBound) {
                    onOwnerBound(false, result.error().message());
                }
            }
        },
        boost::asio::detached);
}

void ClientCore::handleBindOwnerResponse(const control::ControlMessage& message)
{
    if (!message.has_bind_owner_response()) {
        return;
    }

    const auto& resp = message.bind_owner_response();
    bool success = (resp.result() == nevo::common::ResultCode::OK);

    NEVO_LOG_INFO("client", "BindOwnerResponse: success={}, owner_user_id={}, message='{}'",
                  success, resp.owner_user_id(), resp.message());

    if (onOwnerBound) {
        onOwnerBound(success, resp.message());
    }
}

} // namespace nevo
