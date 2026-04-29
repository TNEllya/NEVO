#pragma once
/**
 * @file ServerCore.h
 * @brief NEVO Server Core
 *
 * ServerCore is the main entry class for the NEVO VoIP server, responsible for:
 *   - TCP connection acceptance (control channel)
 *   - UDP voice packet reception and relay
 *   - Client session lifecycle management
 *   - Server startup and graceful shutdown
 *
 * Architecture:
 *   - TCP control channel: for control messages (login, channel management, key exchange)
 *   - UDP voice channel: for real-time voice data transmission (low latency)
 *   - Both channels are linked through ClientSession (shared user state)
 *
 * Threading model:
 *   - io_context runs on a multi-threaded thread pool
 *   - All async operations are sequenced through strand
 *   - Shared state is protected by mutex
 *
 * Usage:
 * @code
 *   boost::asio::io_context io_ctx;
 *   nevo::ServerCore server(io_ctx, 24800, 24801);
 *   server.initialize("nevo_server.db");
 *   server.start();
 *   // Run io_context thread pool...
 *   server.shutdown();
 * @endcode
 */

#include "nevo/core/common/Types.h"
#include "nevo/core/common/Result.h"
#include "nevo/core/model/Permission.h"
#include "nevo/network/TcpConnection.h"
#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"

// Protobuf generated
#include "common.pb.h"

#include <boost/asio.hpp>

#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include <optional>

namespace nevo {

// ============================================================
// Forward declarations
// ============================================================

class Database;
class ChannelManager;
class ClientSession;
class AudioRelay;
class ControlServer;

// ============================================================
// Status snapshots (for UI display)
// ============================================================

/// Session information snapshot
struct SessionSnapshot {
    uint64_t session_id = 0;            ///< Session ID
    uint64_t user_id = 0;               ///< User ID (0 if not authenticated)
    std::string username;               ///< Username
    std::string remote_address;         ///< Remote address
    std::string current_channel;        ///< Current channel name
    bool is_authenticated = false;      ///< Whether authenticated
    bool is_speaking = false;           ///< Whether currently speaking
    bool is_muted = false;              ///< Whether muted
};

/// Channel information snapshot
struct ChannelSnapshot {
    uint64_t channel_id = 0;            ///< Channel ID
    std::string channel_name;           ///< Channel name
    uint64_t parent_id = 0;             ///< Parent channel ID
    uint32_t user_count = 0;            ///< Number of users in channel
};

/// Server overall status snapshot
struct ServerStatusSnapshot {
    bool is_running = false;                           ///< Whether running
    uint16_t tcp_port = 0;                             ///< TCP port
    uint16_t udp_port = 0;                             ///< UDP port
    size_t active_sessions = 0;                        ///< Active session count
    size_t authenticated_users = 0;                    ///< Authenticated user count
    size_t total_channels = 0;                         ///< Total channel count
    uint64_t packets_relayed = 0;                      ///< Packets relayed
    uint64_t packets_dropped = 0;                      ///< Packets dropped
    uint64_t uptime_seconds = 0;                       ///< Uptime (seconds)
    std::vector<SessionSnapshot> sessions;             ///< Session list
    std::vector<ChannelSnapshot> channels;             ///< Channel list
};

// ============================================================
// ServerCore class
// ============================================================

/**
 * @class ServerCore
 * @brief NEVO Server Core
 *
 * Main server class, coordinating all subsystems.
 * Non-copyable, non-movable.
 */
class ServerCore {
public:
    /**
     * @brief Constructor
     * @param io_ctx    Boost.Asio I/O context reference
     * @param tcp_port  TCP listening port (control channel)
     * @param udp_port  UDP listening port (voice channel)
     */
    ServerCore(boost::asio::io_context& io_ctx,
               uint16_t tcp_port,
               uint16_t udp_port);

    /// Destructor
    ~ServerCore();

    // Non-copyable and non-movable
    ServerCore(const ServerCore&) = delete;
    ServerCore& operator=(const ServerCore&) = delete;
    ServerCore(ServerCore&&) = delete;
    ServerCore& operator=(ServerCore&&) = delete;

    // ============================================================
    // Lifecycle management
    // ============================================================

    /**
     * @brief Initialize the server
     *
     * Initializes database, channel manager, audio relay, and other subsystems.
     *
     * @param db_path Database file path
     * @return Result<void> Success or error
     */
    Result<void> initialize(const std::string& db_path);

    /**
     * @brief Start the server
     *
     * Starts TCP accept loop and UDP receive loop.
     * This method is non-blocking; the server runs on the io_context thread pool.
     */
    void start();

    /**
     * @brief Gracefully shut down the server
     *
     * Steps:
     *   1. Stop accepting new connections
     *   2. Disconnect all existing clients
     *   3. Close UDP socket
     *   4. Close TCP acceptor
     */
    void shutdown();

    /**
     * @brief Check if the server is running
     * @return true if the server has started and not yet shut down
     */
    bool isRunning() const;

    // ============================================================
    // Client event callbacks
    // ============================================================

    /**
     * @brief Client connected callback
     *
     * Called by ClientSession after successful user login.
     * Adds the session to the active session list.
     *
     * @param session Client session
     */
    void onClientConnected(std::shared_ptr<ClientSession> session);

    /**
     * @brief Client disconnected callback
     *
     * Called by ClientSession when disconnecting.
     * Removes from the active session list and cleans up AudioRelay mappings.
     *
     * @param session Client session
     */
    void onClientDisconnected(std::shared_ptr<ClientSession> session);

    // ============================================================
    // Status snapshots (for UI polling)
    // ============================================================

    /**
     * @brief Get the current server status snapshot
     *
     * Thread-safe, can be called from any thread. Returns a complete copy of the current state.
     */
    ServerStatusSnapshot getStatusSnapshot() const;

    /**
     * @brief Get the list of active sessions
     *
     * Returns session information for all connected clients.
     */
    std::vector<SessionSnapshot> getActiveSessions() const;

    // ============================================================
    // Simple stat queries
    // ============================================================

    /// Get total connected client count
    int totalClients() const;

    /// Get authenticated client count
    int authenticatedClients() const;

    /// Get total channel count
    int totalChannels() const;

    /// Get total packets relayed
    uint64_t packetsRelayed() const;

    /// Get total packets dropped
    uint64_t packetsDropped() const;

    /// Get server start time (ms since epoch), 0 if not started
    uint64_t startTimeMs() const;

    // ============================================================
    // UI callback hooks (optional, set by GUI layer)
    // ============================================================

    /// Client connect/disconnect callback function type
    using ClientEventCallback = std::function<void(const SessionSnapshot&, bool connected)>;

    /// Server status change callback function type
    using StatusCallback = std::function<void(const ServerStatusSnapshot&)>;

    /// Set client connect/disconnect event callback (thread-safe)
    void setClientEventCallback(ClientEventCallback callback);

    /// Set server status change callback (thread-safe)
    void setStatusCallback(StatusCallback callback);

    /// Trigger status update (for internal use)
    void notifyStatusUpdate() const;

    // Direct callback members (simpler alternative for GUI injection)
    std::function<void(const SessionSnapshot&, bool connected)> onClientEvent;
    std::function<void(bool running)> onServerStateChanged;
    std::function<void(const std::string& level, const std::string& message)> onLogMessage;
    std::function<void(UserId user_id, const std::string& username)> onOwnerBound;

    // ============================================================
    // Hot-applicable configuration setters
    // ============================================================

    /// Set max users limit (takes effect immediately)
    void setMaxUsers(int max_users);

    /// Get current max users limit
    int maxUsers() const;

    /// Set welcome message (takes effect immediately for new connections)
    void setWelcomeMessage(const std::string& message);

    /// Get current welcome message
    std::string welcomeMessage() const;

    /// Set log level (takes effect immediately)
    void setLogLevel(const std::string& level);

    /// Get current log level
    std::string logLevel() const;

    // ============================================================
    // Control server (for Python GUI IPC)
    // ============================================================

    /// Set the control port for the IPC control server (must call before start())
    void setControlPort(uint16_t port);

    /// Get the control server (nullptr if not started)
    ControlServer* controlServer();

    // ============================================================
    // Queries
    // ============================================================

    /**
     * @brief Get database pointer
     * @return Database shared_ptr
     */
    std::shared_ptr<Database> database();

    /**
     * @brief Get channel manager pointer
     * @return Channel manager shared_ptr
     */
    std::shared_ptr<ChannelManager> channelManager();

    /**
     * @brief Get the server session key for voice encryption (legacy shared key)
     * @return Pointer to 32-byte session key
     */
    const uint8_t* serverSessionKey() const { return server_session_key_.data(); }

    /**
     * @brief 为指定客户端生成独立的会话密钥，并用其公钥加密
     *
     * @param user_id          用户 ID
     * @param client_public_key 客户端 Curve25519 公钥（32 字节）
     * @return 加密后的会话密钥（crypto_box_seal 密文），失败返回空 vector
     */
    std::vector<uint8_t> generateSessionKeyForClient(
        UserId user_id,
        const std::vector<uint8_t>& client_public_key);

    /**
     * @brief 获取指定客户端的明文会话密钥
     * @param user_id 用户 ID
     * @return 32 字节密钥指针，不存在返回 nullptr
     */
    const uint8_t* getClientSessionKey(UserId user_id) const;

    /**
     * @brief 移除客户端的会话密钥（断开连接时调用）
     * @param user_id 用户 ID
     */
    void removeClientSessionKey(UserId user_id);

    /**
     * @brief Broadcast channel list update to all connected clients
     *
     * Sends a ChannelListUpdate protobuf message to every authenticated
     * client session. Should be called after any channel modification
     * (create, delete, rename, user join/leave).
     */
    void broadcastChannelListUpdate();

    /**
     * @brief Broadcast user joined channel notification to all clients
     * @param user_info User info of the joined user
     * @param channel_id Channel the user joined
     */
    void broadcastUserJoined(const common::UserInfo& user_info, ChannelId channel_id);

    /**
     * @brief Broadcast user left channel notification to all clients
     * @param user_id User ID of the left user
     * @param channel_id Channel the user left
     */
    void broadcastUserLeft(UserId user_id, ChannelId channel_id);

    /**
     * @brief Broadcast user speaking state change to all clients
     * @param user_id User ID
     * @param speaking Whether the user is speaking
     */
    void broadcastUserSpeaking(UserId user_id, bool speaking);

    /**
     * @brief Perform a key rotation and notify all clients
     *
     * Generates a new server session key, moves the current key to old,
     * and sends KeyRotationRequest to all authenticated clients.
     * Called periodically by the key rotation timer.
     */
    void rotateSessionKey();

    /**
     * @brief Get the permission manager
     * @return PermissionManager pointer
     */
    PermissionManager* permissionManager();

    /**
     * @brief Find client session by user ID
     * @param user_id User ID
     * @return Client session, nullptr if not found
     */
    std::shared_ptr<ClientSession> getClientSession(UserId user_id);

    // ============================================================
    // 服主与管理员管理
    // ============================================================

    /**
     * @brief 生成服主绑定密钥
     *
     * 生成 64 字符 hex 随机密钥，存入 server_config 表。
     * 仅在无服主时可生成。密钥一次性使用，绑定成功后自动清空。
     *
     * @return 生成的密钥字符串，失败返回空字符串
     */
    std::string generateOwnerBindKey();

    /**
     * @brief 获取当前服主用户 ID
     * @return 服主用户 ID，无服主时返回 UserId(0)
     */
    UserId getOwnerUserId();

    /**
     * @brief 检查指定用户是否为服主
     * @param user_id 用户 ID
     * @return true 表示该用户为服主
     */
    bool isOwner(UserId user_id);

    /**
     * @brief 绑定服主身份
     *
     * 验证客户端提供的绑定密钥，若与数据库中存储的 owner_bind_key 匹配，
     * 则将当前用户设为服主，并清空绑定密钥（一次性使用）。
     *
     * @param user_id  申请绑定的用户 ID
     * @param bind_key 客户端提供的绑定密钥
     * @return Result<void> 成功或错误信息
     */
    Result<void> bindOwner(UserId user_id, const std::string& bind_key);

private:
    // ============================================================
    // Coroutine methods
    // ============================================================

    /**
     * @brief TCP accept loop (coroutine)
     *
     * Continuously accepts new TCP connections, creating a ClientSession for each.
     * Each new connection's TCP socket is managed by TcpConnection.
     */
    boost::asio::awaitable<void> acceptTcpLoop();

    /**
     * @brief UDP receive loop (coroutine)
     *
     * Continuously receives UDP voice data packets and forwards them to AudioRelay.
     */
    boost::asio::awaitable<void> receiveUdpLoop();

    /**
     * @brief Start the periodic key rotation timer
     *
     * Every KEY_ROTATION_INTERVAL_SEC seconds, generates a new session key
     * and sends KeyRotationRequest to all authenticated clients.
     */
    void startKeyRotationTimer();

    /**
     * @brief Stop the key rotation timer
     */
    void stopKeyRotationTimer();

    // ============================================================
    // Member variables
    // ============================================================

    /// I/O context reference
    boost::asio::io_context& io_ctx_;

    /// TCP listening port
    uint16_t tcp_port_;

    /// UDP listening port
    uint16_t udp_port_;

    /// TCP acceptor
    boost::asio::ip::tcp::acceptor tcp_acceptor_;

    /// UDP socket (voice channel)
    std::shared_ptr<UdpSocket> udp_socket_;

    /// Database
    std::shared_ptr<Database> db_;

    /// Channel manager
    std::shared_ptr<ChannelManager> channel_mgr_;

    /// Permission manager
    std::unique_ptr<PermissionManager> perm_mgr_;

    /// Audio relay
    std::shared_ptr<AudioRelay> audio_relay_;

    /// Active client sessions: SessionId -> ClientSession
    std::unordered_map<SessionId, std::shared_ptr<ClientSession>> sessions_;

    /// User ID -> SessionId mapping (for fast lookup)
    std::unordered_map<UserId, SessionId> user_session_map_;

    /// Mutex (protects session list)
    mutable std::mutex sessions_mutex_;

    /// Server running state
    std::atomic<bool> running_{false};

    /// Server shutdown flag
    std::atomic<bool> shutdown_requested_{false};

    /// Server start time (ms since epoch)
    uint64_t start_time_ms_ = 0;

    /// Client event callback
    ClientEventCallback client_event_cb_;

    /// Status callback
    StatusCallback status_cb_;

    /// Server session key for voice encryption (32 bytes) — 兼容旧模式
    std::array<uint8_t, CRYPTO_KEY_SIZE> server_session_key_{};

    /// 每客户端独立的会话密钥
    std::unordered_map<UserId, std::array<uint8_t, CRYPTO_KEY_SIZE>> client_session_keys_;
    mutable std::mutex client_keys_mutex_;

    /// Hot-applicable config fields (protected by sessions_mutex_)
    int max_users_ = 100;
    std::string welcome_message_ = "Welcome to the NEVO server!";
    std::string log_level_ = "info";

    /// Control server for Python GUI IPC
    uint16_t control_port_ = 24432;
    std::unique_ptr<ControlServer> control_server_;

    /// Key rotation timer
    std::unique_ptr<boost::asio::steady_timer> key_rotation_timer_;

    /// Key epoch counter (incremented on each rotation)
    uint64_t key_epoch_ = 0;
};

} // namespace nevo
