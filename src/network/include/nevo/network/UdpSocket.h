#pragma once
/**
 * @file UdpSocket.h
 * @brief UDP 套接字封装
 *
 * 基于 Boost.Asio 和 C++20 协程的 UDP 套接字类。
 * 主要用于 VoIP 语音数据传输，支持指定远端端点发送和接收。
 *
 * 使用方式：
 *   auto udp = std::make_shared<UdpSocket>(io_ctx);
 *   udp->onPacket = [](const uint8_t* data, uint32_t size,
 *                      const boost::asio::ip::udp::endpoint& sender) { ... };
 *   udp->bind(0);  // OS 自动分配端口
 *   co_await udp->asyncReceiveFrom();
 *   co_await udp->asyncSendTo(buf, len, remote_endpoint);
 */

#include <boost/asio.hpp>

#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>

#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {

// ============================================================
// UDP 套接字类
// ============================================================

class UdpSocket : public std::enable_shared_from_this<UdpSocket> {
public:
    /// 构造函数
    /// @param io_ctx Boost.Asio I/O 上下文引用
    explicit UdpSocket(boost::asio::io_context& io_ctx);

    /// 析构函数
    ~UdpSocket();

    // 禁止拷贝
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // ============================================================
    // 配置与绑定
    // ============================================================

    /// 绑定到本地端口
    /// @param local_port 本地端口号，0 表示由操作系统自动分配
    /// @return 错误码，成功返回 0
    boost::system::error_code bind(uint16_t local_port = 0);

    /// 获取实际绑定的本地端口号
    /// @return 端口号，未绑定时返回 0
    uint16_t localPort() const;

    /// 获取底层 UDP socket 引用（用于 NAT 穿透等需要直接操作 socket 的场景）
    /// @return UDP socket 引用
    boost::asio::ip::udp::socket& socket() { return socket_; }

    // ============================================================
    // 协程式异步操作
    // ============================================================

    /// 启动异步接收循环（协程）
    /// 持续接收 UDP 数据包，每个包触发 onPacket 回调。
    /// @return 协程退出时的错误码
    boost::asio::awaitable<void> asyncReceiveFrom();

    /// 异步发送数据到指定端点（协程）
    /// @param data 待发送数据指针
    /// @param size 数据字节数
    /// @param endpoint 目标端点
    /// @return 错误码，成功返回 0
    boost::asio::awaitable<boost::system::error_code>
    asyncSendTo(const uint8_t* data, uint32_t size,
                const boost::asio::ip::udp::endpoint& endpoint);

    /// 异步发送数据到指定端点（vector 便捷重载）
    /// @param payload 待发送数据
    /// @param endpoint 目标端点
    /// @return 错误码
    boost::asio::awaitable<boost::system::error_code>
    asyncSendTo(const std::vector<uint8_t>& payload,
                const boost::asio::ip::udp::endpoint& endpoint);

    // ============================================================
    // 连接管理
    // ============================================================

    /// 关闭 UDP 套接字
    void close();

    /// 检查套接字是否已打开
    /// @return true 表示套接字已打开
    bool isOpen() const;

    // ============================================================
    // 常量
    // ============================================================

    /// 获取 UDP 最大载荷大小（MTU 安全值）
    /// @return 1400 字节
    static constexpr uint32_t getMaxPayloadSize() { return UDP_MAX_PACKET_SIZE; }

    // ============================================================
    // 回调设置
    // ============================================================

    /// 数据包到达回调
    /// @param data 数据指针
    /// @param size 数据字节数
    /// @param sender_endpoint 发送方端点
    std::function<void(const uint8_t* data, uint32_t size,
                       const boost::asio::ip::udp::endpoint& sender_endpoint)> onPacket;

private:
    // ============================================================
    // 成员变量
    // ============================================================

    /// UDP socket
    boost::asio::ip::udp::socket socket_;

    /// Strand，保证异步操作串行执行
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    /// 接收缓冲区
    std::vector<uint8_t> recv_buffer_;

    /// 套接字是否已打开
    std::atomic<bool> open_{false};
};

} // namespace nevo
