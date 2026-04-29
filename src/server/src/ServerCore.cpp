/**
 * @file ServerCore.cpp
 * @brief NEVO Server Core Implementation
 *
 * Implements server main loop, connection acceptance, UDP receive,
 * and client session management using Boost.Asio C++20 coroutines.
 */

#include "nevo/server/ServerCore.h"
#include "nevo/server/ClientSession.h"
#include "nevo/server/ControlServer.h"
#include "nevo/server/Database.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/server/AudioRelay.h"
#include "nevo/core/common/Logger.h"
#include "nevo/network/VoiceCrypto.h"
#include "nevo/core/protocol/PacketTypes.h"

// Protobuf generated headers
#include "control.pb.h"

#ifdef NEVO_HAS_SODIUM
#include <sodium.h>
#endif

#include <random>

namespace nevo {

// ============================================================
// Construction / Destruction
// ============================================================

ServerCore::ServerCore(boost::asio::io_context& io_ctx,
                       uint16_t tcp_port,
                       uint16_t udp_port)
    : io_ctx_(io_ctx)
    , tcp_port_(tcp_port)
    , udp_port_(udp_port)
    , tcp_acceptor_(io_ctx)
{
    NEVO_LOG_INFO("server", "ServerCore created (tcp={}, udp={})", tcp_port, udp_port);
}

ServerCore::~ServerCore() {
    if (running_) {
        shutdown();
    }
    NEVO_LOG_INFO("server", "ServerCore destroyed");
}

// ============================================================
// Lifecycle Management
// ============================================================

Result<void> ServerCore::initialize(const std::string& db_path) {
    NEVO_LOG_INFO("server", "Initializing ServerCore with db: {}", db_path);

    // Initialize database
    db_ = std::make_shared<Database>();
    auto db_result = db_->initialize(db_path);
    if (!db_result) {
        NEVO_LOG_ERROR("server", "Failed to initialize database: {}", db_result.error().message());
        return db_result;
    }

    // Initialize channel manager
    channel_mgr_ = std::make_shared<ChannelManager>(db_);
    auto channel_result = channel_mgr_->initialize();
    if (!channel_result) {
        NEVO_LOG_ERROR("server", "Failed to initialize ChannelManager: {}", channel_result.error().message());
        return channel_result;
    }

    // Initialize audio relay
    audio_relay_ = std::make_shared<AudioRelay>();
    audio_relay_->setChannelManager(channel_mgr_);
    audio_relay_->setIoContext(io_ctx_);

    // Initialize permission manager
    perm_mgr_ = std::make_unique<PermissionManager>();

    // Initialize UDP socket
    udp_socket_ = std::make_shared<UdpSocket>(io_ctx_);
    auto udp_bind_result = udp_socket_->bind(udp_port_);
    if (udp_bind_result) {
        NEVO_LOG_ERROR("server", "Failed to bind UDP socket: {}", udp_bind_result.message());
        return Err<void>(ResultCode::ConnectionFailed,
                         "Failed to bind UDP socket: " + udp_bind_result.message());
    }

    // Set AudioRelay's UDP socket and session key query
    audio_relay_->setUdpSocket(udp_socket_);
    audio_relay_->setSessionKeyQuery(
        [this](UserId user_id) -> const uint8_t* {
            return this->getClientSessionKey(user_id);
        });

    // Generate server session key for voice encryption
#ifdef NEVO_HAS_SODIUM
    randombytes_buf(server_session_key_.data(), CRYPTO_KEY_SIZE);
    NEVO_LOG_INFO("server", "Server session key generated (libsodium random)");
#else
    // Fallback: use std::random_device for key generation
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist;
    for (size_t i = 0; i < CRYPTO_KEY_SIZE; i += sizeof(uint32_t)) {
        uint32_t val = dist(rd);
        std::memcpy(server_session_key_.data() + i, &val,
                    std::min(sizeof(uint32_t), CRYPTO_KEY_SIZE - i));
    }
    NEVO_LOG_WARN("server", "Server session key generated (std::random_device, libsodium not available)");
#endif

    NEVO_LOG_INFO("server", "ServerCore initialized successfully");
    return Ok();
}

void ServerCore::start() {
    if (running_) {
        NEVO_LOG_WARN("server", "ServerCore already running");
        return;
    }

    running_ = true;
    shutdown_requested_ = false;
    start_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    NEVO_LOG_INFO("server", "Starting ServerCore");

    // Start TCP accept loop coroutine
    boost::asio::co_spawn(io_ctx_,
        [this]() -> boost::asio::awaitable<void> {
            try {
                co_await acceptTcpLoop();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server", "TCP accept loop exception: {}", e.what());
            }
        },
        boost::asio::detached);

    // Start UDP receive loop coroutine
    boost::asio::co_spawn(io_ctx_,
        [this]() -> boost::asio::awaitable<void> {
            try {
                co_await receiveUdpLoop();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server", "UDP receive loop exception: {}", e.what());
            }
        },
        boost::asio::detached);

    NEVO_LOG_INFO("server", "ServerCore started on TCP:{} UDP:{}", tcp_port_, udp_port_);

    // Start control server for Python GUI IPC
    control_server_ = std::make_unique<ControlServer>(io_ctx_, control_port_, this);
    control_server_->start();

    // Start key rotation timer
    startKeyRotationTimer();

    if (onServerStateChanged) {
        onServerStateChanged(true);
    }

    // Broadcast server started event to GUI clients
    if (control_server_) {
        auto eventData = ControlJson::make_obj();
        eventData.obj_val["running"] = ControlJson::make_bool(true);
        control_server_->broadcastEvent("status_changed", eventData);
    }

    notifyStatusUpdate();
}

void ServerCore::shutdown() {
    if (!running_) {
        return;
    }

    NEVO_LOG_INFO("server", "Shutting down ServerCore");
    shutdown_requested_ = true;
    running_ = false;
    start_time_ms_ = 0;

    // Close TCP acceptor, stop accepting new connections
    boost::system::error_code ec;
    tcp_acceptor_.close(ec);
    if (ec) {
        NEVO_LOG_WARN("server", "Error closing TCP acceptor: {}", ec.message());
    }

    // Disconnect all existing clients
    // NOTE: We must NOT call session->disconnect() while holding sessions_mutex_,
    // because disconnect() calls onClientDisconnected() which tries to re-acquire
    // sessions_mutex_ on the same thread, causing a deadlock with std::mutex.
    // Instead, copy the sessions, clear the maps under the lock, then disconnect
    // each session outside the lock.
    {
        std::vector<std::shared_ptr<ClientSession>> sessions_to_disconnect;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto& [sid, session] : sessions_) {
                sessions_to_disconnect.push_back(session);
            }

            // Clean up AudioRelay mappings while still under the lock
            if (audio_relay_) {
                for (auto& [uid, sid] : user_session_map_) {
                    audio_relay_->removeClientMapping(uid);
                }
            }

            sessions_.clear();
            user_session_map_.clear();
        }

        // Now disconnect each session outside the lock — onClientDisconnected()
        // can safely acquire sessions_mutex_ without deadlock. The maps are already
        // cleared, so erase operations will be no-ops.
        for (auto& session : sessions_to_disconnect) {
            session->disconnect();
        }
    }

    // Close UDP socket
    if (udp_socket_) {
        udp_socket_->close();
    }

    // Stop key rotation timer
    stopKeyRotationTimer();

    // Broadcast server stopping event to GUI clients before stopping control server
    if (control_server_) {
        auto eventData = ControlJson::make_obj();
        eventData.obj_val["running"] = ControlJson::make_bool(false);
        control_server_->broadcastEvent("status_changed", eventData);
        control_server_->stop();
        control_server_.reset();
    }

    NEVO_LOG_INFO("server", "ServerCore shutdown complete");

    if (onServerStateChanged) {
        onServerStateChanged(false);
    }
    notifyStatusUpdate();
}

bool ServerCore::isRunning() const {
    return running_;
}

// ============================================================
// Status Snapshots (for GUI use)
// ============================================================

ServerStatusSnapshot ServerCore::getStatusSnapshot() const {
    ServerStatusSnapshot snapshot;
    snapshot.is_running = running_.load();
    snapshot.tcp_port = tcp_port_;
    snapshot.udp_port = udp_port_;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        snapshot.active_sessions = sessions_.size();

        for (const auto& [sid, session] : sessions_) {
            SessionSnapshot ss;
            ss.session_id = sid.value;
            ss.user_id = session->userId().value;
            ss.username = session->user().username();
            ss.remote_address = session->remoteAddress();
            ss.is_authenticated = session->isAuthenticated();
            ss.is_speaking = session->user().isSpeaking();
            ss.is_muted = session->user().isMuted();
            snapshot.sessions.push_back(std::move(ss));
        }
    }

    snapshot.authenticated_users = 0;
    for (const auto& s : snapshot.sessions) {
        if (s.is_authenticated) ++snapshot.authenticated_users;
    }

    if (channel_mgr_) {
        auto channels = channel_mgr_->getChannelsWithUsers();
        snapshot.total_channels = channels.size();
        for (const auto& ch : channels) {
            ChannelSnapshot cs;
            cs.channel_id = ch.channel_id.value;
            cs.channel_name = ch.channel_name;
            cs.parent_id = ch.parent_id.value;
            cs.user_count = static_cast<uint32_t>(ch.user_ids.size());
            snapshot.channels.push_back(std::move(cs));
        }
    }

    if (audio_relay_) {
        snapshot.packets_relayed = audio_relay_->packetsRelayed();
        snapshot.packets_dropped = audio_relay_->packetsDropped();
    }

    if (start_time_ms_ > 0) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        snapshot.uptime_seconds = static_cast<uint64_t>((now - start_time_ms_) / 1000);
    }

    return snapshot;
}

std::vector<SessionSnapshot> ServerCore::getActiveSessions() const {
    std::vector<SessionSnapshot> result;
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    for (const auto& [sid, session] : sessions_) {
        SessionSnapshot ss;
        ss.session_id = sid.value;
        ss.user_id = session->userId().value;
        ss.username = session->user().username();
        ss.remote_address = session->remoteAddress();
        ss.is_authenticated = session->isAuthenticated();
        ss.is_speaking = session->user().isSpeaking();
        ss.is_muted = session->user().isMuted();
        result.push_back(std::move(ss));
    }

    return result;
}

int ServerCore::totalClients() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return static_cast<int>(sessions_.size());
}

int ServerCore::authenticatedClients() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    int count = 0;
    for (const auto& [sid, session] : sessions_) {
        if (session->isAuthenticated()) {
            ++count;
        }
    }
    return count;
}

int ServerCore::totalChannels() const {
    if (!channel_mgr_) return 0;
    return static_cast<int>(channel_mgr_->getAllChannels().size());
}

uint64_t ServerCore::packetsRelayed() const {
    if (!audio_relay_) return 0;
    return audio_relay_->packetsRelayed();
}

uint64_t ServerCore::packetsDropped() const {
    if (!audio_relay_) return 0;
    return audio_relay_->packetsDropped();
}

uint64_t ServerCore::startTimeMs() const {
    return start_time_ms_;
}

// ============================================================
// UI Callbacks
// ============================================================

void ServerCore::setClientEventCallback(ClientEventCallback callback) {
    client_event_cb_ = std::move(callback);
}

void ServerCore::setStatusCallback(StatusCallback callback) {
    status_cb_ = std::move(callback);
}

void ServerCore::notifyStatusUpdate() const {
    if (status_cb_) {
        status_cb_(getStatusSnapshot());
    }
}

// ============================================================
// Client Event Callbacks
// ============================================================

void ServerCore::onClientConnected(std::shared_ptr<ClientSession> session) {
    UserId uid;
    SessionId sid;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        uid = session->userId();
        sid = session->sessionId();

        sessions_[sid] = session;
        if (uid) {
            user_session_map_[uid] = sid;
        }

        NEVO_LOG_INFO("server", "Client connected: user={} session={} (total clients: {})",
                      uid.value, sid.value, sessions_.size());

        // Add to AudioRelay mapping if UDP endpoint is available
        if (audio_relay_ && session->udpEndpoint()) {
            audio_relay_->addClientMapping(uid, *session->udpEndpoint());
        }
    }

    // Trigger UI callbacks outside the lock
    SessionSnapshot ss;
    ss.session_id = sid.value;
    ss.user_id = uid.value;
    ss.username = session->user().username();
    ss.remote_address = session->remoteAddress();
    ss.is_authenticated = session->isAuthenticated();

    if (client_event_cb_) {
        client_event_cb_(ss, true);
    }
    if (onClientEvent) {
        onClientEvent(ss, true);
    }

    // Broadcast event to connected GUI clients via ControlServer
    if (control_server_) {
        auto eventData = ControlJson::make_obj();
        eventData.obj_val["session_id"] = ControlJson::make_num(static_cast<double>(ss.session_id));
        eventData.obj_val["username"] = ControlJson::make_str(ss.username);
        eventData.obj_val["address"] = ControlJson::make_str(ss.remote_address);
        control_server_->broadcastEvent("client_connected", eventData);
    }

    notifyStatusUpdate();
}

void ServerCore::onClientDisconnected(std::shared_ptr<ClientSession> session) {
    UserId uid;
    SessionId sid;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        uid = session->userId();
        sid = session->sessionId();

        // Remove from AudioRelay mapping
        if (audio_relay_ && uid) {
            audio_relay_->removeClientMapping(uid);
        }

        // Remove from session list
        sessions_.erase(sid);
        user_session_map_.erase(uid);

        // 移除客户端独立会话密钥
        removeClientSessionKey(uid);

        NEVO_LOG_INFO("server", "Client disconnected: user={} session={} (total clients: {})",
                      uid.value, sid.value, sessions_.size());
    }

    // Trigger UI callbacks outside the lock
    SessionSnapshot ss;
    ss.session_id = sid.value;
    ss.user_id = uid.value;
    ss.username = session->user().username();
    ss.remote_address = session->remoteAddress();
    ss.is_authenticated = session->isAuthenticated();

    if (client_event_cb_) {
        client_event_cb_(ss, false);
    }
    if (onClientEvent) {
        onClientEvent(ss, false);
    }

    // Broadcast event to connected GUI clients via ControlServer
    if (control_server_) {
        auto eventData = ControlJson::make_obj();
        eventData.obj_val["session_id"] = ControlJson::make_num(static_cast<double>(ss.session_id));
        eventData.obj_val["username"] = ControlJson::make_str(ss.username);
        eventData.obj_val["address"] = ControlJson::make_str(ss.remote_address);
        control_server_->broadcastEvent("client_disconnected", eventData);
    }

    notifyStatusUpdate();
}

// ============================================================
// Queries
// ============================================================

std::shared_ptr<Database> ServerCore::database() {
    return db_;
}

std::shared_ptr<ChannelManager> ServerCore::channelManager() {
    return channel_mgr_;
}

PermissionManager* ServerCore::permissionManager() {
    return perm_mgr_.get();
}

std::shared_ptr<ClientSession> ServerCore::getClientSession(UserId user_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    auto it = user_session_map_.find(user_id);
    if (it == user_session_map_.end()) {
        return nullptr;
    }

    auto session_it = sessions_.find(it->second);
    if (session_it == sessions_.end()) {
        return nullptr;
    }

    return session_it->second;
}

// ============================================================
// 每客户端会话密钥管理
// ============================================================

// libsodium crypto_box 公钥长度（Curve25519）
static constexpr size_t CRYPTO_BOX_PUBLICKEYBYTES = 32;
// libsodium crypto_box_seal 附加长度
static constexpr size_t CRYPTO_BOX_SEALBYTES = 48;

std::vector<uint8_t> ServerCore::generateSessionKeyForClient(
    UserId user_id,
    const std::vector<uint8_t>& client_public_key)
{
    if (client_public_key.size() != CRYPTO_BOX_PUBLICKEYBYTES) {
        NEVO_LOG_WARN("server", "Invalid client public key size: {} (expected {})",
                     client_public_key.size(), CRYPTO_BOX_PUBLICKEYBYTES);
        return {};
    }

    std::array<uint8_t, CRYPTO_KEY_SIZE> session_key{};
#ifdef NEVO_HAS_SODIUM
    randombytes_buf(session_key.data(), CRYPTO_KEY_SIZE);

    // 使用 crypto_box_seal 加密会话密钥
    std::vector<uint8_t> encrypted(CRYPTO_KEY_SIZE + CRYPTO_BOX_SEALBYTES);
    int rc = crypto_box_seal(
        encrypted.data(),
        session_key.data(),
        CRYPTO_KEY_SIZE,
        client_public_key.data());
    if (rc != 0) {
        NEVO_LOG_ERROR("server", "crypto_box_seal failed for user_id={}", user_id.value);
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(client_keys_mutex_);
        client_session_keys_[user_id] = session_key;
    }

    NEVO_LOG_INFO("server", "Generated per-client session key for user_id={}", user_id.value);
    return encrypted;
#else
    // Fallback: 不使用加密，直接返回明文（仅用于编译通过，生产环境不安全）
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist;
    for (size_t i = 0; i < CRYPTO_KEY_SIZE; i += sizeof(uint32_t)) {
        uint32_t val = dist(rd);
        std::memcpy(session_key.data() + i, &val,
                    std::min(sizeof(uint32_t), CRYPTO_KEY_SIZE - i));
    }

    {
        std::lock_guard<std::mutex> lock(client_keys_mutex_);
        client_session_keys_[user_id] = session_key;
    }

    NEVO_LOG_WARN("server", "libsodium not available, session key sent in plaintext (INSECURE)");
    return std::vector<uint8_t>(session_key.begin(), session_key.end());
#endif
}

const uint8_t* ServerCore::getClientSessionKey(UserId user_id) const {
    std::lock_guard<std::mutex> lock(client_keys_mutex_);
    auto it = client_session_keys_.find(user_id);
    if (it != client_session_keys_.end()) {
        return it->second.data();
    }
    return nullptr;
}

void ServerCore::removeClientSessionKey(UserId user_id) {
    std::lock_guard<std::mutex> lock(client_keys_mutex_);
    auto it = client_session_keys_.find(user_id);
    if (it != client_session_keys_.end()) {
#ifdef NEVO_HAS_SODIUM
        sodium_memzero(it->second.data(), it->second.size());
#else
        std::memset(it->second.data(), 0, it->second.size());
#endif
        client_session_keys_.erase(it);
        NEVO_LOG_DEBUG("server", "Removed session key for user_id={}", user_id.value);
    }
}

// ============================================================
// 服主与管理员管理
// ============================================================

std::string ServerCore::generateOwnerBindKey() {
    if (!db_) {
        NEVO_LOG_ERROR("server", "Cannot generate bind key: database not available");
        return "";
    }

    // Check if owner already exists
    auto owner_id_str = db_->getConfig("owner_user_id");
    if (owner_id_str && !owner_id_str.value().empty() && owner_id_str.value() != "0") {
        NEVO_LOG_WARN("server", "Cannot generate bind key: owner already exists (user_id={})",
                      owner_id_str.value());
        return "";
    }

    // Generate 32 bytes of random data as hex string (64 characters)
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    std::string hex_key;
    for (int i = 0; i < 4; ++i) {
        uint64_t val = dist(gen);
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(val));
        hex_key += buf;
    }

    // Store in database
    auto result = db_->setConfig("owner_bind_key", hex_key);
    if (!result) {
        NEVO_LOG_ERROR("server", "Failed to store bind key in database");
        return "";
    }

    NEVO_LOG_INFO("server", "Generated owner bind key (length={})", hex_key.size());
    return hex_key;
}

UserId ServerCore::getOwnerUserId() {
    if (!db_) {
        return UserId(0);
    }

    auto owner_id_str = db_->getConfig("owner_user_id");
    if (!owner_id_str || owner_id_str.value().empty() || owner_id_str.value() == "0") {
        return UserId(0);
    }

    try {
        uint64_t uid = std::stoull(owner_id_str.value());
        return UserId(uid);
    } catch (const std::exception& e) {
        NEVO_LOG_ERROR("server", "Failed to parse owner_user_id '{}': {}",
                       owner_id_str.value(), e.what());
        return UserId(0);
    }
}

bool ServerCore::isOwner(UserId user_id) {
    return getOwnerUserId() == user_id;
}

Result<void> ServerCore::bindOwner(UserId user_id, const std::string& bind_key) {
    if (!db_) {
        return Err<void>(ResultCode::DatabaseError, "Database not available");
    }

    if (!user_id) {
        return Err<void>(ResultCode::InvalidRequest, "Invalid user ID");
    }

    if (bind_key.empty()) {
        return Err<void>(ResultCode::InvalidRequest, "Bind key is empty");
    }

    // Check if owner already exists
    auto existing_owner = getOwnerUserId();
    if (existing_owner) {
        return Err<void>(ResultCode::PermissionDenied, "Server owner already exists");
    }

    // Retrieve stored bind key
    auto stored_key_opt = db_->getConfig("owner_bind_key");
    if (!stored_key_opt || stored_key_opt.value().empty()) {
        return Err<void>(ResultCode::InvalidRequest, "No owner bind key available");
    }

    // Compare keys (constant-time comparison not strictly necessary for hex keys,
    // but we use simple string comparison here)
    if (bind_key != stored_key_opt.value()) {
        return Err<void>(ResultCode::AuthFailed, "Invalid bind key");
    }

    // Store owner user ID
    auto set_result = db_->setConfig("owner_user_id", std::to_string(user_id.value));
    if (!set_result) {
        return Err<void>(ResultCode::DatabaseError,
                         "Failed to store owner user ID: " + set_result.error().message());
    }

    // Clear the bind key (one-time use)
    auto clear_result = db_->setConfig("owner_bind_key", "");
    if (!clear_result) {
        NEVO_LOG_WARN("server", "Failed to clear owner bind key after successful bind");
    }

    // Promote owner to Admin group
    auto group_result = db_->updateUserGroupId(user_id, GROUP_ADMIN);
    if (!group_result) {
        NEVO_LOG_WARN("server", "Failed to promote owner to admin group: {}",
                      group_result.error().message());
    }

    NEVO_LOG_INFO("server", "User {} bound as server owner", user_id.value);
    return Ok();
}

// ============================================================
// Coroutine Methods
// ============================================================

boost::asio::awaitable<void> ServerCore::acceptTcpLoop() {
    // Open TCP acceptor
    boost::asio::ip::tcp::endpoint tcp_endpoint(
        boost::asio::ip::tcp::v4(), tcp_port_);

    boost::system::error_code ec;
    tcp_acceptor_.open(tcp_endpoint.protocol(), ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to open TCP acceptor: {}", ec.message());
        co_return;
    }

    // Set SO_REUSEADDR option
    tcp_acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);

    tcp_acceptor_.bind(tcp_endpoint, ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to bind TCP acceptor: {}", ec.message());
        co_return;
    }

    tcp_acceptor_.listen(boost::asio::ip::tcp::acceptor::max_listen_connections, ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to listen on TCP acceptor: {}", ec.message());
        co_return;
    }

    NEVO_LOG_INFO("server", "TCP acceptor listening on port {}", tcp_port_);

    // Loop accepting connections
    while (!shutdown_requested_) {
        // Create new TCP connection object
        auto tcp_conn = std::make_shared<TcpConnection>(io_ctx_);

        // Wait for new connection
        boost::system::error_code accept_ec;
        co_await tcp_acceptor_.async_accept(
            tcp_conn->socket(), boost::asio::redirect_error(boost::asio::use_awaitable, accept_ec));

        if (accept_ec) {
            if (shutdown_requested_) {
                break;
            }
            NEVO_LOG_ERROR("server", "TCP accept error: {}", accept_ec.message());
            continue;
        }

        // Mark connection as established
        tcp_conn->setConnected(true);

        NEVO_LOG_INFO("server", "New TCP connection from: {}",
                      tcp_conn->remoteEndpointString());

        // Create ClientSession
        auto session = std::make_shared<ClientSession>(
            tcp_conn, this, db_, channel_mgr_);

        // Start session (begin read loop)
        session->start();
    }

    NEVO_LOG_INFO("server", "TCP accept loop exited");
}

boost::asio::awaitable<void> ServerCore::receiveUdpLoop() {
    NEVO_LOG_INFO("server", "UDP receive loop starting on port {}", udp_port_);

    // Set UDP packet callback
    udp_socket_->onPacket = [this](const uint8_t* data, uint32_t size,
                                    const boost::asio::ip::udp::endpoint& sender) {
        // Forward voice packet to AudioRelay for processing
        if (audio_relay_) {
            audio_relay_->handleVoicePacket(data, size, sender);
        }
    };

    // Start UDP receive loop
    co_await udp_socket_->asyncReceiveFrom();

    NEVO_LOG_INFO("server", "UDP receive loop exited");
}

// ============================================================
// Hot-applicable configuration setters
// ============================================================

void ServerCore::setMaxUsers(int max_users) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    max_users_ = max_users;
    NEVO_LOG_INFO("server", "Max users updated to {}", max_users);
}

int ServerCore::maxUsers() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return max_users_;
}

void ServerCore::setWelcomeMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    welcome_message_ = message;
    NEVO_LOG_INFO("server", "Welcome message updated");
}

std::string ServerCore::welcomeMessage() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return welcome_message_;
}

void ServerCore::setLogLevel(const std::string& level) {
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        log_level_ = level;
    }
    // Apply log level immediately
    if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    }
    NEVO_LOG_INFO("server", "Log level updated to {}", level);
}

std::string ServerCore::logLevel() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return log_level_;
}

void ServerCore::setControlPort(uint16_t port) {
    control_port_ = port;
}

ControlServer* ServerCore::controlServer() {
    return control_server_.get();
}

void ServerCore::broadcastChannelListUpdate() {
    if (!channel_mgr_) return;

    // Build the channel list protobuf message
    control::ControlMessage msg;
    auto* channel_list = msg.mutable_channel_list();
    auto channels = channel_mgr_->getChannelsWithUsers();
    for (const auto& ch : channels) {
        auto* ci = channel_list->add_channels();
        ci->set_id(ch.channel_id.value);
        ci->set_name(ch.channel_name);
        ci->set_parent_id(ch.parent_id.value);
        for (const auto& uid : ch.user_ids) {
            auto* ui = ci->add_users();
            ui->set_id(uid.value);
        }
    }

    // Send to all authenticated sessions
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [sid, session] : sessions_) {
        if (session->isAuthenticated()) {
            session->sendControl(msg, ControlMessageType::ChannelList, 0);
        }
    }
}

void ServerCore::broadcastUserJoined(const common::UserInfo& user_info, ChannelId channel_id) {
    control::ControlMessage msg;
    auto* user_joined = msg.mutable_user_joined();
    auto* ui = user_joined->mutable_user();
    ui->set_id(user_info.id());
    ui->set_username(user_info.username());
    ui->set_status(user_info.status());
    ui->set_muted(user_info.muted());
    ui->set_deafened(user_info.deafened());
    ui->set_group_id(user_info.group_id());
    user_joined->set_channel_id(channel_id.value);

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [sid, session] : sessions_) {
        if (session->isAuthenticated()) {
            session->sendControl(msg, ControlMessageType::UserJoined, 0);
        }
    }
}

void ServerCore::broadcastUserLeft(UserId user_id, ChannelId channel_id) {
    control::ControlMessage msg;
    auto* user_left = msg.mutable_user_left();
    user_left->set_user_id(user_id.value);
    user_left->set_channel_id(channel_id.value);

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [sid, session] : sessions_) {
        if (session->isAuthenticated()) {
            session->sendControl(msg, ControlMessageType::UserLeft, 0);
        }
    }
}

void ServerCore::broadcastUserSpeaking(UserId user_id, bool speaking) {
    control::ControlMessage msg;
    auto* user_speaking = msg.mutable_user_speaking();
    user_speaking->set_user_id(user_id.value);
    user_speaking->set_speaking(speaking);

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [sid, session] : sessions_) {
        if (session->isAuthenticated()) {
            session->sendControl(msg, ControlMessageType::UserSpeaking, 0);
        }
    }
}

// ============================================================
// Key Rotation
// ============================================================

void ServerCore::rotateSessionKey() {
    NEVO_LOG_INFO("server", "Rotating per-client session keys (epoch {} -> {})",
                  key_epoch_, key_epoch_ + 1);

    ++key_epoch_;

    // 获取所有已认证会话的快照
    std::vector<std::pair<UserId, std::shared_ptr<ClientSession>>> authenticated_sessions;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (const auto& [sid, session] : sessions_) {
            if (session->isAuthenticated()) {
                authenticated_sessions.emplace_back(session->userId(), session);
            }
        }
    }

    size_t notified_count = 0;
    for (const auto& [user_id, session] : authenticated_sessions) {
        if (!user_id) continue;

        // 从数据库获取客户端公钥
        std::vector<uint8_t> client_pubkey;
        if (db_) {
            auto user_rec = db_->getUser(user_id);
            if (user_rec) {
                client_pubkey = std::move(user_rec->public_key);
            }
        }

        if (client_pubkey.empty()) {
            NEVO_LOG_WARN("server", "Key rotation skipped for user_id={}: no public key", user_id.value);
            continue;
        }

        // 生成新密钥并加密
        auto encrypted_key = generateSessionKeyForClient(user_id, client_pubkey);
        if (encrypted_key.empty()) {
            NEVO_LOG_ERROR("server", "Key rotation failed for user_id={}: encryption failed", user_id.value);
            continue;
        }

        // 发送个体 KeyRotationRequest
        control::ControlMessage msg;
        auto* req = msg.mutable_key_rotation_request();
        req->set_key_epoch(key_epoch_);
        req->set_encrypted_session_key(
            std::string(reinterpret_cast<const char*>(encrypted_key.data()), encrypted_key.size()));
        session->sendControl(msg, ControlMessageType::KeyRotationRequest, 0);
        ++notified_count;
    }

    NEVO_LOG_INFO("server", "Key rotation complete, notified {} / {} clients",
                  notified_count, authenticated_sessions.size());
}

void ServerCore::startKeyRotationTimer() {
    if (!running_) return;

    key_rotation_timer_ = std::make_unique<boost::asio::steady_timer>(io_ctx_);
    key_rotation_timer_->expires_after(std::chrono::seconds(KEY_ROTATION_INTERVAL_SEC));

    key_rotation_timer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) {
            NEVO_LOG_DEBUG("server", "Key rotation timer cancelled");
            return;
        }

        // Perform rotation
        rotateSessionKey();

        // Reschedule
        startKeyRotationTimer();
    });

    NEVO_LOG_INFO("server", "Key rotation timer started (interval={}s)", KEY_ROTATION_INTERVAL_SEC);
}

void ServerCore::stopKeyRotationTimer() {
    if (key_rotation_timer_) {
        key_rotation_timer_->cancel();
        key_rotation_timer_.reset();
        NEVO_LOG_DEBUG("server", "Key rotation timer stopped");
    }
}

} // namespace nevo
