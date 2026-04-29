#pragma once
/**
 * @file ConnectionManager.h
 * @brief 连接管理器
 *
 * 管理所有活跃的 TCP 连接，提供添加、删除、查找功能。
 * 支持心跳超时检测：定期检查每个连接的最近活跃时间，
 * 超时未收到心跳则自动断开连接。
 *
 * 使用方式：
 *   ConnectionManager mgr(io_ctx);
 *   mgr.add(session_id, &conn);
 *   conn.onMessage = [&](auto& data) { mgr.updateActivity(session_id); };
 *   mgr.startHeartbeatCheck(30); // 30秒超时
 *
 * 线程安全：所有公共方法通过 strand 保护。
 */

#include <boost/asio.hpp>

#include <unordered_map>
#include <chrono>
#include <mutex>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

#include "nevo/core/common/Logger.h"
#include "nevo/core/common/Types.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {

// 前向声明，避免头文件循环依赖
class TcpConnection;

// ============================================================
// 连接管理器类
// ============================================================

class ConnectionManager {
public:
    /// 心跳超时回调类型
    /// @param session_id 超时的会话 ID
    using TimeoutCallback = std::function<void(SessionId session_id)>;

    /// 构造函数
    /// @param io_ctx Boost.Asio I/O 上下文引用
    explicit ConnectionManager(boost::asio::io_context& io_ctx);

    /// 析构函数
    ~ConnectionManager();

    // 禁止拷贝
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    // ============================================================
    // 连接管理
    // ============================================================

    /// 添加连接到管理器
    /// @param session_id 会话唯一标识
    /// @param conn TCP 连接指针（不获取所有权）
    void add(SessionId session_id, TcpConnection* conn);

    /// 移除连接
    /// @param session_id 会话唯一标识
    /// @return true 成功移除
    bool remove(SessionId session_id);

    /// 获取连接指针
    /// @param session_id 会话唯一标识
    /// @return 连接指针，不存在则返回 nullptr
    TcpConnection* get(SessionId session_id) const;

    /// 获取当前活跃连接数
    /// @return 连接数量
    size_t size() const;

    // ============================================================
    // 心跳超时检测
    // ============================================================

    /// 更新连接的最近活跃时间
    /// 每次收到该连接的消息时应调用此方法。
    /// @param session_id 会话唯一标识
    void updateActivity(SessionId session_id);

    /// 启动心跳超时检测
    /// @param timeout_sec 超时时间（秒），无活动超过此时间则断开
    /// @param check_interval_sec 检测间隔（秒），默认为超时时间的一半
    void startHeartbeatCheck(uint32_t timeout_sec,
                             uint32_t check_interval_sec = 0);

    /// 停止心跳超时检测
    void stopHeartbeatCheck();

    /// 设置超时回调
    /// 当连接超时时调用此回调，用于清理资源。
    /// @param callback 回调函数
    void setTimeoutCallback(TimeoutCallback callback);

    // ============================================================
    // 批量操作
    // ============================================================

    /// 关闭所有连接
    void closeAll();

private:
    // ============================================================
    // 内部类型
    // ============================================================

    /// 连接信息
    struct ConnectionEntry {
        TcpConnection* connection = nullptr;           ///< TCP 连接指针
        std::chrono::steady_clock::time_point last_activity; ///< 最近活跃时间
    };

    // ============================================================
    // 内部方法
    // ============================================================

    /// 定时检查心跳超时
    void checkHeartbeatTimeout();

    // ============================================================
    // 成员变量
    // ============================================================

    /// 连接映射表
    std::unordered_map<SessionId, ConnectionEntry> connections_;

    /// 互斥锁，保护 connections_
    mutable std::mutex mutex_;

    /// 心跳超时时间
    uint32_t heartbeat_timeout_sec_ = 30;

    /// 心跳检测定时器
    std::unique_ptr<boost::asio::steady_timer> heartbeat_timer_;

    /// 心跳检测是否运行中
    bool heartbeat_running_ = false;

    /// Strand，保护定时器操作
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    /// 超时回调
    TimeoutCallback timeout_callback_;
};

} // namespace nevo
