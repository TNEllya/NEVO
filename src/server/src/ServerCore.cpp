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
#include "nevo/server/VideoRelay.h"
#include "nevo/core/common/Logger.h"
#include "nevo/network/VoiceCrypto.h"
#include "nevo/core/protocol/PacketTypes.h"

// Protobuf generated headers
#include "control.pb.h"

#ifdef NEVO_HAS_SODIUM
#include <sodium.h>
#endif

#include <random>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace nevo {

// Type alias for readability
using SessionPtr = std::shared_ptr<ClientSession>;

// ============================================================
// Construction / Destruction
// ============================================================

ServerCore::ServerCore(boost::asio::io_context& io_ctx,
                       uint16_t tcp_port,
                       uint16_t udp_port)
    : io_ctx_(io_ctx)
    , tcp_port_(tcp_port)
    , udp_port_(udp_port)
    , video_udp_port_(udp_port + 1)
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

    video_relay_ = std::make_shared<VideoRelay>();
    video_relay_->setChannelManager(channel_mgr_);
    video_relay_->setIoContext(io_ctx_);

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

    // Derive password file path from db_path directory
    std::filesystem::path db_dir = std::filesystem::path(db_path).parent_path();
    if (db_dir.empty()) {
        db_dir = ".";
    }
    password_file_path_ = (db_dir / "nevo_admin.dat").string();

    // Load persisted admin password hash
    loadAdminPassword();

    // Load SSL/TLS configuration from database
    auto ssl_enabled_str = db_->getConfig("ssl_enabled");
    if (ssl_enabled_str && ssl_enabled_str.value() == "1") {
        ssl_enabled_ = true;

        auto cert_str = db_->getConfig("ssl_cert_file");
        auto key_str = db_->getConfig("ssl_key_file");
        auto ca_str = db_->getConfig("ssl_ca_file");

        if (cert_str) ssl_cert_file_ = cert_str.value();
        if (key_str) ssl_key_file_ = key_str.value();
        if (ca_str) ssl_ca_file_ = ca_str.value();

        NEVO_LOG_INFO("server", "SSL/TLS configuration loaded from database (enabled={}, cert={}, key={})",
            ssl_enabled_, ssl_cert_file_, ssl_key_file_);
    }

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

    video_udp_socket_ = std::make_shared<UdpSocket>(io_ctx_);
    auto ec = video_udp_socket_->bind(video_udp_port_);
    if (ec) {
        NEVO_LOG_CRITICAL("server", "FAILED to bind video UDP port {}: {}", video_udp_port_, ec.message());
        throw std::runtime_error("Failed to bind video UDP port " + std::to_string(video_udp_port_) + ": " + ec.message());
    }
    NEVO_LOG_INFO("server", "Video UDP relay bound on port {}", video_udp_port_);
    video_relay_->setUdpSocket(video_udp_socket_);
    video_relay_->setSessionKeyQuery(
        [this](UserId user_id) -> const uint8_t* {
            return this->getClientSessionKey(user_id);
        });

    boost::asio::co_spawn(io_ctx_,
        [this]() -> boost::asio::awaitable<void> {
            try {
                co_await receiveVideoUdpLoop();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server", "Video UDP receive loop exception: {}", e.what());
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

    // Collect local IP addresses
    auto [ipv4, ipv6] = collectLocalAddresses();
    snapshot.ipv4_address = ipv4;
    snapshot.ipv6_address = ipv6;

    return snapshot;
}

std::pair<std::string, std::string> ServerCore::collectLocalAddresses() const {
    std::string ipv4, ipv6;

#ifdef _WIN32
    ULONG family = AF_UNSPEC;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG buf_len = 15000;
    std::vector<uint8_t> buf(buf_len);

    ULONG ret = GetAdaptersAddresses(family, flags, nullptr,
                                     reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buf.resize(buf_len);
        ret = GetAdaptersAddresses(family, flags, nullptr,
                                   reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &buf_len);
    }
    if (ret != ERROR_SUCCESS) {
        return {ipv4, ipv6};
    }

    auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    for (; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (adapter->OperStatus != IfOperStatusUp) continue;

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            auto* sa = unicast->Address.lpSockaddr;
            if (!sa) continue;

            char addr_str[INET6_ADDRSTRLEN] = {};
            if (sa->sa_family == AF_INET && ipv4.empty()) {
                auto* sin = reinterpret_cast<sockaddr_in*>(sa);
                inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
                ipv4 = addr_str;
            } else if (sa->sa_family == AF_INET6 && ipv6.empty()) {
                auto* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
                inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
                ipv6 = addr_str;
            }
            if (!ipv4.empty() && !ipv6.empty()) break;
        }
        if (!ipv4.empty() && !ipv6.empty()) break;
    }
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (std::string(ifa->ifa_name) == "lo" || std::string(ifa->ifa_name).find("loopback") != std::string::npos)
                continue;

            char addr_str[INET6_ADDRSTRLEN] = {};
            if (ifa->ifa_addr->sa_family == AF_INET && ipv4.empty()) {
                auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
                inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
                ipv4 = addr_str;
            } else if (ifa->ifa_addr->sa_family == AF_INET6 && ipv6.empty()) {
                auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
                inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
                ipv6 = addr_str;
            }
            if (!ipv4.empty() && !ipv6.empty()) break;
        }
        freeifaddrs(ifaddr);
    }
#endif
    return {ipv4, ipv6};
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

        // 检查用户数量限制
        int authenticated_count = 0;
        for (const auto& [sid, sess] : sessions_) {
            if (sess && sess->isAuthenticated()) {
                ++authenticated_count;
            }
        }
        if (max_users_ > 0 && authenticated_count >= max_users_) {
            NEVO_LOG_WARN("server", "Rejecting connection: max users limit reached ({}/{})",
                          authenticated_count, max_users_);
            // 在锁外断开连接以避免死锁
            boost::asio::post(io_ctx_, [session]() {
                session->disconnect();
            });
            return;
        }

        uid = session->userId();
        sid = session->sessionId();

        // Remove from AudioRelay mapping
        if (audio_relay_ && uid) {
            audio_relay_->removeClientMapping(uid);
        }

        if (video_relay_) {
            video_relay_->removeClientMapping(uid);
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

#ifdef NEVO_HAS_SODIUM
    std::array<uint8_t, CRYPTO_KEY_SIZE> session_key{};
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
    NEVO_LOG_ERROR("server", "libsodium not available, cannot generate session key!");
    return {};
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

void ServerCore::setClientSessionKey(UserId user_id, const uint8_t* key, size_t key_size) {
    if (!key || key_size != CRYPTO_KEY_SIZE) {
        NEVO_LOG_WARN("server", "Invalid key size for setClientSessionKey: {} (expected {})",
                     key_size, CRYPTO_KEY_SIZE);
        return;
    }
    std::lock_guard<std::mutex> lock(client_keys_mutex_);
    std::memcpy(client_session_keys_[user_id].data(), key, CRYPTO_KEY_SIZE);
    NEVO_LOG_DEBUG("server", "Set session key for user_id={}", user_id.value);
}

// ============================================================
// 管理员管理
// ============================================================

Result<void> ServerCore::authenticateAdmin(UserId user_id, const std::string& password) {
    if (admin_password_hash_.empty()) {
        return Error(ResultCode::Unknown, "Admin password not set on server");
    }

#ifdef NEVO_HAS_SODIUM
    if (crypto_pwhash_str_verify(admin_password_hash_.c_str(), password.c_str(), password.size()) != 0) {
        return Error(ResultCode::AuthFailed, "Incorrect admin password");
    }
#else
    NEVO_LOG_ERROR("server", "Cannot verify admin password: libsodium not available!");
    return Error(ResultCode::Unknown, "Cannot verify admin password");
#endif

    auto session = getClientSession(user_id);
    if (!session) {
        return Error(ResultCode::UserNotFound, "User session not found");
    }

    session->updateUserGroupId(GROUP_ADMIN);
    return {};
}

Result<void> ServerCore::setServerName(const std::string& server_name) {
    server_name_ = server_name;
    return {};
}

std::string ServerCore::serverName() const {
    return server_name_;
}

void ServerCore::setAdminPassword(const std::string& password) {
    if (password.empty()) {
        NEVO_LOG_INFO("server", "Admin password cleared");
        admin_password_hash_.clear();
        // Delete the persisted file
        std::error_code ec;
        std::filesystem::remove(password_file_path_, ec);
        return;
    }

#ifdef NEVO_HAS_SODIUM
    char hashed_password[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hashed_password,
                         password.c_str(), password.size(),
                         crypto_pwhash_OPSLIMIT_MODERATE,
                         crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
        NEVO_LOG_ERROR("server", "Failed to hash admin password");
        admin_password_hash_.clear();
        return;
    }
    admin_password_hash_ = hashed_password;

    // Persist encrypted hash to disk
    saveAdminPassword();
#else
    NEVO_LOG_ERROR("server", "Cannot set admin password: libsodium not available!");
    admin_password_hash_.clear();
#endif
}

// Remove adminPassword() function since we don't store plaintext anymore

bool ServerCore::isAdminPasswordSet() const {
    return !admin_password_hash_.empty();
}

void ServerCore::saveAdminPassword() {
    if (admin_password_hash_.empty()) {
        return;
    }
#ifdef NEVO_HAS_SODIUM
    static constexpr const char* APP_SECRET = "NEVO_ADMIN_VAULT_SECRET_KEY_2024_v1";
    uint8_t key[crypto_secretbox_KEYBYTES];
    crypto_generichash(key, sizeof(key),
                       reinterpret_cast<const uint8_t*>(APP_SECRET), strlen(APP_SECRET),
                       nullptr, 0);

    uint8_t nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    std::vector<uint8_t> ciphertext(admin_password_hash_.size() + crypto_secretbox_MACBYTES);
    crypto_secretbox_easy(ciphertext.data(),
                          reinterpret_cast<const uint8_t*>(admin_password_hash_.data()),
                          admin_password_hash_.size(),
                          nonce, key);

    std::ofstream ofs(password_file_path_, std::ios::binary);
    if (!ofs) {
        NEVO_LOG_ERROR("server", "Failed to open admin password file for writing: {}", password_file_path_);
        return;
    }
    ofs.write(reinterpret_cast<const char*>(nonce), sizeof(nonce));
    ofs.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext.size());
    ofs.close();

    NEVO_LOG_INFO("server", "Admin password encrypted and saved to {}", password_file_path_);
#else
    NEVO_LOG_ERROR("server", "Cannot save admin password: libsodium not available!");
#endif
}

void ServerCore::loadAdminPassword() {
#ifdef NEVO_HAS_SODIUM
    std::error_code ec;
    if (!std::filesystem::exists(password_file_path_, ec)) {
        NEVO_LOG_INFO("server", "No persisted admin password file found at {}", password_file_path_);
        return;
    }

    std::ifstream ifs(password_file_path_, std::ios::binary);
    if (!ifs) {
        NEVO_LOG_ERROR("server", "Failed to open admin password file for reading: {}", password_file_path_);
        return;
    }

    // Read nonce (24 bytes)
    uint8_t nonce[crypto_secretbox_NONCEBYTES];
    ifs.read(reinterpret_cast<char*>(nonce), sizeof(nonce));
    if (ifs.gcount() != static_cast<std::streamsize>(sizeof(nonce))) {
        NEVO_LOG_ERROR("server", "Admin password file is corrupted (nonce too short)");
        return;
    }

    // Read the rest (ciphertext)
    std::vector<uint8_t> ciphertext(
        (std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>());
    ifs.close();

    if (ciphertext.size() < crypto_secretbox_MACBYTES) {
        NEVO_LOG_ERROR("server", "Admin password file is corrupted (ciphertext too short)");
        return;
    }

    static constexpr const char* APP_SECRET = "NEVO_ADMIN_VAULT_SECRET_KEY_2024_v1";
    uint8_t key[crypto_secretbox_KEYBYTES];
    crypto_generichash(key, sizeof(key),
                       reinterpret_cast<const uint8_t*>(APP_SECRET), strlen(APP_SECRET),
                       nullptr, 0);

    std::vector<uint8_t> plaintext(ciphertext.size() - crypto_secretbox_MACBYTES);
    if (crypto_secretbox_open_easy(plaintext.data(),
                                   ciphertext.data(), ciphertext.size(),
                                   nonce, key) != 0) {
        NEVO_LOG_ERROR("server", "Failed to decrypt admin password file — data may be tampered or corrupted");
        return;
    }

    admin_password_hash_.assign(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
    NEVO_LOG_INFO("server", "Admin password loaded from encrypted file {}", password_file_path_);
#else
    NEVO_LOG_INFO("server", "Cannot load admin password: libsodium not available");
#endif
}

ServerConfig ServerCore::config() const {
    ServerConfig cfg;
    cfg.max_users = max_users_;
    cfg.welcome_message = welcome_message_;
    cfg.server_name = server_name_;
    return cfg;
}

// ============================================================
// Coroutine Methods
// ============================================================

boost::asio::awaitable<void> ServerCore::acceptTcpLoop() {

    boost::system::error_code ec;

    tcp_acceptor_.open(boost::asio::ip::tcp::v6(), ec);
    if (!ec) {
        boost::asio::ip::v6_only v6_only_opt(false);
        tcp_acceptor_.set_option(v6_only_opt, ec);
        if (ec) {
            NEVO_LOG_WARN("server", "Failed to set IPV6_V6ONLY=0: {}", ec.message());
            ec.clear();
        }

        tcp_acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);

        boost::asio::ip::tcp::endpoint tcp_endpoint(
            boost::asio::ip::tcp::v6(), tcp_port_);
        tcp_acceptor_.bind(tcp_endpoint, ec);
        if (!ec) {
            NEVO_LOG_INFO("server", "TCP acceptor listening on port {} (dual-stack)", tcp_port_);
            goto start_accept;
        }
        NEVO_LOG_WARN("server", "IPv6 TCP bind failed ({}), falling back to IPv4", ec.message());
        tcp_acceptor_.close(ec);
    } else {
        NEVO_LOG_WARN("server", "IPv6 TCP not available ({}), falling back to IPv4", ec.message());
    }

    tcp_acceptor_.open(boost::asio::ip::tcp::v4(), ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to open TCP acceptor: {}", ec.message());
        co_return;
    }

    tcp_acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);

    {
        boost::asio::ip::tcp::endpoint tcp_endpoint(
            boost::asio::ip::tcp::v4(), tcp_port_);
        tcp_acceptor_.bind(tcp_endpoint, ec);
        if (ec) {
            NEVO_LOG_ERROR("server", "Failed to bind TCP acceptor: {}", ec.message());
            co_return;
        }
    }

start_accept:
    tcp_acceptor_.listen(boost::asio::ip::tcp::acceptor::max_listen_connections, ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to listen on TCP acceptor: {}", ec.message());
        co_return;
    }

    NEVO_LOG_INFO("server", "TCP acceptor listening on port {}", tcp_port_);

    // ---- Initialize SSL context if TLS is enabled ----
    if (ssl_enabled_) {
        if (ssl_cert_file_.empty() || ssl_key_file_.empty()) {
            NEVO_LOG_ERROR("server",
                "TLS enabled but certificate/key files not configured. "
                "Use setSslCertificateFile() and setSslPrivateKeyFile() before start().");
            co_return;
        }

        ssl_ctx_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);

        // Set minimum TLS version to TLS 1.2
        SSL_CTX_set_min_proto_version(ssl_ctx_->native_handle(), TLS1_2_VERSION);

        // Load server certificate
        boost::system::error_code cert_ec;
        ssl_ctx_->use_certificate_file(ssl_cert_file_,
            boost::asio::ssl::context::pem, cert_ec);
        if (cert_ec) {
            NEVO_LOG_ERROR("server", "Failed to load TLS certificate '{}': {}",
                ssl_cert_file_, cert_ec.message());
            co_return;
        }

        // Load server private key
        ssl_ctx_->use_private_key_file(ssl_key_file_,
            boost::asio::ssl::context::pem, cert_ec);
        if (cert_ec) {
            NEVO_LOG_ERROR("server", "Failed to load TLS private key '{}': {}",
                ssl_key_file_, cert_ec.message());
            co_return;
        }

        // Load CA certificate for client verification (mTLS, optional)
        if (!ssl_ca_file_.empty()) {
            ssl_ctx_->load_verify_file(ssl_ca_file_, cert_ec);
            if (cert_ec) {
                NEVO_LOG_ERROR("server", "Failed to load CA cert '{}': {}",
                    ssl_ca_file_, cert_ec.message());
                co_return;
            }
            // Require client certificate verification
            ssl_ctx_->set_verify_mode(
                boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
            NEVO_LOG_INFO("server", "mTLS enabled: client certificates required");
        }

        // Disable insecure cipher suites
        SSL_CTX_set_cipher_list(ssl_ctx_->native_handle(),
            "HIGH:!aNULL:!MD5:!DSS");

        NEVO_LOG_INFO("server", "TLS initialized (cert={}, key={})",
            ssl_cert_file_, ssl_key_file_);
    }

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

        // ---- Perform server-side TLS handshake if enabled ----
        if (ssl_enabled_ && ssl_ctx_) {
            NEVO_LOG_INFO("server", "Starting TLS handshake for {}",
                tcp_conn->remoteEndpointString());

            auto ssl_ec = co_await tcp_conn->asyncSslServerHandshake(*ssl_ctx_);
            if (ssl_ec) {
                NEVO_LOG_ERROR("server", "TLS handshake failed for {}: {}",
                    tcp_conn->remoteEndpointString(), ssl_ec.message());
                // Handshake failed, skip this connection
                continue;
            }

            NEVO_LOG_INFO("server", "TLS handshake succeeded for {}",
                tcp_conn->remoteEndpointString());
        }

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

// ============================================================
// SSL/TLS configuration
// ============================================================

void ServerCore::setSslEnabled(bool enabled) {
#ifndef NEVO_HAS_OPENSSL
    if (enabled) {
        NEVO_LOG_ERROR("server", "Cannot enable TLS: OpenSSL not available at build time");
        return;
    }
#endif
    ssl_enabled_ = enabled;
    NEVO_LOG_INFO("server", "TLS {}", enabled ? "enabled" : "disabled");
}

bool ServerCore::isSslEnabled() const {
    return ssl_enabled_;
}

void ServerCore::setSslCertificateFile(const std::string& cert_path) {
    ssl_cert_file_ = cert_path;
    NEVO_LOG_INFO("server", "TLS certificate file set to {}", cert_path);
}

void ServerCore::setSslPrivateKeyFile(const std::string& key_path) {
    ssl_key_file_ = key_path;
    NEVO_LOG_INFO("server", "TLS private key file set to {}", key_path);
}

void ServerCore::setSslCaFile(const std::string& ca_path) {
    ssl_ca_file_ = ca_path;
    NEVO_LOG_INFO("server", "TLS CA file set to {}", ca_path);
}

void ServerCore::setControlPort(uint16_t port) {
    control_port_ = port;
}

ControlServer* ServerCore::controlServer() {
    return control_server_.get();
}

void ServerCore::updateAudioRelayChannel(UserId user_id, ChannelId channel_id) {
    if (audio_relay_) {
        audio_relay_->updateClientChannel(user_id, channel_id);
    }

    if (video_relay_) {
        video_relay_->updateClientChannel(user_id, channel_id);
    }
}

void ServerCore::relayTcpVoicePacket(const uint8_t* data, uint32_t size, UserId sender_id) {
    if (audio_relay_ && data && size > 0) {
        boost::asio::ip::udp::endpoint dummy_endpoint;
        audio_relay_->handleVoicePacket(data, size, dummy_endpoint, sender_id);
    }
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
            if (auto session = getClientSession(uid)) {
                ui->set_username(session->user().username());
                ui->set_muted(session->user().isMuted());
                ui->set_deafened(session->user().isDeafened());
                ui->set_group_id(session->user().groupId().value);
            }
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

void ServerCore::broadcastChatMessage(UserId sender_id,
                                       const std::string& sender_name,
                                       ChannelId channel_id,
                                       const std::string& text) {
    control::ControlMessage msg;
    auto* chat = msg.mutable_chat_broadcast();
    chat->set_sender_id(sender_id.value);
    chat->set_sender_name(sender_name);
    chat->set_channel_id(channel_id.value);
    chat->set_text(text);
    chat->set_timestamp(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [sid, session] : sessions_) {
        if (session->isAuthenticated()) {
            // 只广播给同频道的用户
            const auto& user = session->user();
            if (user.currentChannel() == channel_id) {
                session->sendControl(msg, ControlMessageType::ChatBroadcast, 0);
            }
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
    std::vector<std::pair<UserId, SessionPtr>> authenticated_sessions;
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

boost::asio::awaitable<void> ServerCore::receiveVideoUdpLoop() {
    NEVO_LOG_INFO("server", "Video UDP receive loop starting on port {}", video_udp_port_);

    std::atomic<uint64_t> video_udp_pkt_count{0};
    std::string last_sender;

    video_udp_socket_->onPacket = [this, &video_udp_pkt_count, &last_sender](const uint8_t* data, uint32_t size,
                                          const boost::asio::ip::udp::endpoint& sender) {
        uint64_t count = ++video_udp_pkt_count;
        auto sender_str = sender.address().to_string() + ":" + std::to_string(sender.port());
        last_sender = sender_str;

        if (count <= 5 || count % 50 == 0) {
            NEVO_LOG_INFO("server", "VIDEO UDP: Packet #{} received from {} (size={})", count, sender_str, size);
        }
        if (count % 100 == 0) {
            NEVO_LOG_INFO("server", "VIDEO UDP: {} packets received, last from {} (size={}), "
                           "relay stats: received={} relayed={} dropped={}",
                           count, sender_str, size,
                           video_relay_ ? video_relay_->packetsReceived() : 0,
                           video_relay_ ? video_relay_->packetsRelayed() : 0,
                           video_relay_ ? video_relay_->packetsDropped() : 0);
        }

        if (video_relay_) {
            video_relay_->handleVideoPacket(data, size, sender);
        } else {
            NEVO_LOG_WARN("server", "VIDEO UDP: received {} bytes from {} but video_relay is NULL!", size, sender_str);
        }
    };

    co_await video_udp_socket_->asyncReceiveFrom();

    NEVO_LOG_INFO("server", "Video UDP receive loop exited (total packets={}, last sender={})",
                  video_udp_pkt_count.load(), last_sender);
}

void ServerCore::addVideoRelayMapping(UserId user_id,
                                       const boost::asio::ip::udp::endpoint& ep,
                                       ChannelId channel_id) {
    if (video_relay_) {
        video_relay_->addClientMapping(user_id, ep, channel_id);
    }
}

void ServerCore::removeVideoRelayMapping(UserId user_id) {
    if (video_relay_) {
        video_relay_->removeClientMapping(user_id);
    }
}

void ServerCore::updateVideoRelayChannel(UserId user_id, ChannelId channel_id) {
    if (video_relay_) {
        video_relay_->updateClientChannel(user_id, channel_id);
    }
}

} // namespace nevo
