/**
 * @file ConnectionManager.cpp
 * @brief 连接管理器实现
 *
 * 管理所有活跃的 TCP 连接，支持：
 *   - 连接的添加、删除、查找
 *   - 心跳超时检测（基于定时器）
 *   - 批量关闭所有连接
 *
 * 线程安全说明：
 *   - connections_ 由 mutex_ 保护
 *   - 心跳定时器由 strand_ 保护
 *   - 所有公共方法均可安全跨线程调用
 */

#include "nevo/network/ConnectionManager.h"
#include "nevo/network/TcpConnection.h"

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

ConnectionManager::ConnectionManager(boost::asio::io_context& io_ctx)
    : strand_(boost::asio::make_strand(io_ctx))
{
    NEVO_LOG_DEBUG("network", "ConnectionManager constructed");
}

ConnectionManager::~ConnectionManager()
{
    stopHeartbeatCheck();
    closeAll();
    NEVO_LOG_DEBUG("network", "ConnectionManager destroyed");
}

// ============================================================
// add - 添加连接
// ============================================================

void ConnectionManager::add(SessionId session_id, TcpConnection* conn)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否已存在
    auto it = connections_.find(session_id);
    if (it != connections_.end()) {
        NEVO_LOG_WARN("network",
                       "Session {} already exists in ConnectionManager, replacing",
                       session_id.value);
        // 关闭旧连接
        if (it->second.connection) {
            it->second.connection->close();
        }
    }

    // 添加新条目
    ConnectionEntry entry;
    entry.connection = conn;
    entry.last_activity = std::chrono::steady_clock::now();
    connections_[session_id] = entry;

    NEVO_LOG_INFO("network",
                  "Connection added: session_id={}, total connections={}",
                  session_id.value, connections_.size());
}

// ============================================================
// remove - 移除连接
// ============================================================

bool ConnectionManager::remove(SessionId session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(session_id);
    if (it == connections_.end()) {
        NEVO_LOG_DEBUG("network",
                       "Session {} not found in ConnectionManager",
                       session_id.value);
        return false;
    }

    connections_.erase(it);

    NEVO_LOG_INFO("network",
                  "Connection removed: session_id={}, remaining connections={}",
                  session_id.value, connections_.size());
    return true;
}

// ============================================================
// get - 获取连接
// ============================================================

TcpConnection* ConnectionManager::get(SessionId session_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(session_id);
    if (it == connections_.end()) {
        return nullptr;
    }

    return it->second.connection;
}

// ============================================================
// size - 获取连接数
// ============================================================

size_t ConnectionManager::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

// ============================================================
// updateActivity - 更新活跃时间
// ============================================================

void ConnectionManager::updateActivity(SessionId session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(session_id);
    if (it != connections_.end()) {
        it->second.last_activity = std::chrono::steady_clock::now();
        NEVO_LOG_TRACE("network",
                       "Activity updated for session {}",
                       session_id.value);
    }
}

// ============================================================
// startHeartbeatCheck - 启动心跳检测
// ============================================================

void ConnectionManager::startHeartbeatCheck(uint32_t timeout_sec,
                                             uint32_t check_interval_sec)
{
    heartbeat_timeout_sec_ = timeout_sec;

    // 如果未指定检测间隔，使用超时时间的一半
    uint32_t interval = check_interval_sec > 0 ? check_interval_sec : timeout_sec / 2;
    if (interval == 0) {
        interval = 1; // 至少 1 秒
    }

    heartbeat_running_ = true;

    // 创建定时器
    heartbeat_timer_ = std::make_unique<boost::asio::steady_timer>(
        strand_,
        std::chrono::seconds(interval));

    // 启动首次检测（由 checkHeartbeatTimeout 负责重新调度）
    heartbeat_timer_->async_wait(
        [this](boost::system::error_code ec) {
            if (ec || !heartbeat_running_) {
                return;
            }
            checkHeartbeatTimeout();
        });

    NEVO_LOG_INFO("network",
                  "Heartbeat check started: timeout={}s, interval={}s",
                  timeout_sec, interval);
}

// ============================================================
// stopHeartbeatCheck - 停止心跳检测
// ============================================================

void ConnectionManager::stopHeartbeatCheck()
{
    heartbeat_running_ = false;

    if (heartbeat_timer_) {
        heartbeat_timer_->cancel();
        heartbeat_timer_.reset();
    }

    NEVO_LOG_DEBUG("network", "Heartbeat check stopped");
}

// ============================================================
// setTimeoutCallback - 设置超时回调
// ============================================================

void ConnectionManager::setTimeoutCallback(TimeoutCallback callback)
{
    timeout_callback_ = std::move(callback);
}

// ============================================================
// closeAll - 关闭所有连接
// ============================================================

void ConnectionManager::closeAll()
{
    std::lock_guard<std::mutex> lock(mutex_);

    NEVO_LOG_INFO("network",
                  "Closing all connections (count={})",
                  connections_.size());

    for (auto& [session_id, entry] : connections_) {
        if (entry.connection) {
            NEVO_LOG_DEBUG("network",
                           "Closing connection for session {}",
                           session_id.value);
            entry.connection->close();
        }
    }

    connections_.clear();
    NEVO_LOG_INFO("network", "All connections closed");
}

// ============================================================
// checkHeartbeatTimeout - 检查心跳超时
// ============================================================

void ConnectionManager::checkHeartbeatTimeout()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<SessionId> timed_out;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& [session_id, entry] : connections_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - entry.last_activity).count();

            if (elapsed > heartbeat_timeout_sec_) {
                NEVO_LOG_WARN("network",
                              "Session {} heartbeat timeout: {}s > {}s",
                              session_id.value,
                              elapsed,
                              heartbeat_timeout_sec_);
                timed_out.push_back(session_id);
            }
        }
    }

    // 在锁外处理超时连接，避免回调中可能的死锁
    for (auto session_id : timed_out) {
        // 调用超时回调
        if (timeout_callback_) {
            timeout_callback_(session_id);
        }

        // 关闭超时连接并移除
        TcpConnection* conn = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = connections_.find(session_id);
            if (it != connections_.end()) {
                conn = it->second.connection;
                connections_.erase(it);
            }
        }

        if (conn) {
            conn->close();
        }
    }

    // 如果心跳检测仍在运行，重新调度下一次检测
    if (heartbeat_running_ && heartbeat_timer_) {
        uint32_t interval = heartbeat_timeout_sec_ / 2;
        if (interval == 0) {
            interval = 1;
        }

        heartbeat_timer_->expires_after(std::chrono::seconds(interval));
        heartbeat_timer_->async_wait(
            [this](boost::system::error_code ec) {
                if (!ec && heartbeat_running_) {
                    checkHeartbeatTimeout();
                }
            });
    }
}

} // namespace nevo
