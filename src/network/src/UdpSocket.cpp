/**
 * @file UdpSocket.cpp
 * @brief UDP 套接字封装实现
 *
 * 使用 C++20 协程和 Boost.Asio 实现异步 UDP 通信。
 * 主要用于 VoIP 场景下的语音数据传输，每个 UDP 包为一个完整的
 * 语音帧（Protobuf 头 + 加密 Opus 数据），不需要分片重组。
 *
 * 性能说明：
 *   - 接收缓冲区大小为 UDP_MAX_PACKET_SIZE (1400)
 *   - 发送数据超过 getMaxPayloadSize() 时会截断并记录警告
 *   - 使用 strand 保证回调不会并发执行
 */

#include "nevo/network/UdpSocket.h"
#include "nevo/core/common/Logger.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

UdpSocket::UdpSocket(boost::asio::io_context& io_ctx)
    : socket_(io_ctx)
    , strand_(boost::asio::make_strand(io_ctx))
    , recv_buffer_(128 * 1024) // 预分配 128KB 接收缓冲区（支持语音 1.4KB 和视频 73KB+ NAL）
{
    NEVO_LOG_DEBUG("network", "UdpSocket constructed");
}

UdpSocket::~UdpSocket()
{
    close();
    NEVO_LOG_DEBUG("network", "UdpSocket destroyed");
}

// ============================================================
// bind - 绑定本地端口
// ============================================================

boost::system::error_code UdpSocket::bind(uint16_t local_port)
{
    boost::system::error_code ec;

    socket_.open(boost::asio::ip::udp::v6(), ec);
    if (!ec) {
        boost::asio::ip::v6_only v6_only_opt(false);
        socket_.set_option(v6_only_opt, ec);
        if (ec) {
            NEVO_LOG_WARN("network", "Failed to set IPV6_V6ONLY=0: {}", ec.message());
            ec.clear();
        }

        auto bind_endpoint = boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v6(), local_port);
        socket_.bind(bind_endpoint, ec);
        if (!ec) {
            NEVO_LOG_INFO("network", "UDP socket bound to port {} (dual-stack)", local_port);
            goto configure_socket;
        }
        NEVO_LOG_WARN("network", "IPv6 UDP bind failed ({}), falling back to IPv4", ec.message());
        socket_.close(ec);
    } else {
        NEVO_LOG_WARN("network", "IPv6 UDP not available ({}), falling back to IPv4", ec.message());
    }

    socket_.open(boost::asio::ip::udp::v4(), ec);
    if (ec) {
        NEVO_LOG_ERROR("network", "Failed to open UDP socket: {}", ec.message());
        return ec;
    }

    {
        auto bind_endpoint = boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(), local_port);
        socket_.bind(bind_endpoint, ec);
        if (ec) {
            NEVO_LOG_ERROR("network", "Failed to bind UDP socket to port {}: {}",
                           local_port, ec.message());
            socket_.close();
            return ec;
        }
    }

configure_socket:
    boost::asio::socket_base::receive_buffer_size recv_buf_opt(256 * 1024);
    socket_.set_option(recv_buf_opt, ec);
    if (ec) {
        NEVO_LOG_WARN("network", "Failed to set UDP receive buffer size: {}",
                      ec.message());
    }

    open_.store(true);

    NEVO_LOG_INFO("network", "UDP socket bound to port {}", localPort());
    return boost::system::error_code{};
}

// ============================================================
// localPort - 获取实际绑定端口
// ============================================================

uint16_t UdpSocket::localPort() const
{
    boost::system::error_code ec;
    auto endpoint = socket_.local_endpoint(ec);
    if (ec) {
        return 0;
    }
    return endpoint.port();
}

// ============================================================
// asyncReceiveFrom - 协程式接收循环
// ============================================================

boost::asio::awaitable<void> UdpSocket::asyncReceiveFrom()
{
    if (!open_.load()) {
        NEVO_LOG_ERROR("network", "Cannot start receive loop: socket not bound");
        co_return;
    }

    NEVO_LOG_INFO("network", "UDP receive loop started on port {}", localPort());

    while (open_.load()) {
        boost::asio::ip::udp::endpoint sender_endpoint;

        NEVO_LOG_DEBUG("network", "Calling async_receive_from on port {}", localPort());

        auto [ec, bytes_read] = co_await socket_.async_receive_from(
            boost::asio::buffer(recv_buffer_.data(), recv_buffer_.size()),
            sender_endpoint,
            boost::asio::as_tuple(boost::asio::use_awaitable));

        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                NEVO_LOG_DEBUG("network", "UDP receive loop aborted (socket closing)");
                break;
            }
            if (ec == boost::asio::error::message_size) {
                recv_buffer_.resize(recv_buffer_.size() * 2);
                NEVO_LOG_WARN("network", "UDP packet too large for buffer ({}), resizing to {}",
                              recv_buffer_.size() / 2, recv_buffer_.size());
                continue;
            }
            NEVO_LOG_ERROR("network", "UDP receive error: {}", ec.message());
            continue;
        }

        NEVO_LOG_INFO("network", "UDP received {} bytes from {}:{}",
                       bytes_read,
                       sender_endpoint.address().to_string(),
                       sender_endpoint.port());

        if (onPacket && bytes_read > 0) {
            onPacket(recv_buffer_.data(),
                     static_cast<uint32_t>(bytes_read),
                     sender_endpoint);
        } else {
            NEVO_LOG_WARN("network", "UDP received {} bytes but onPacket is null or bytes_read=0", bytes_read);
        }
    }

    NEVO_LOG_INFO("network", "UDP receive loop ended");
}

// ============================================================
// asyncSendTo - 协程式发送
// ============================================================

boost::asio::awaitable<boost::system::error_code>
UdpSocket::asyncSendTo(const uint8_t* data, uint32_t size,
                       const boost::asio::ip::udp::endpoint& endpoint)
{
    if (!open_.load()) {
        NEVO_LOG_WARN("network", "Attempted send on unbound UDP socket");
        co_return boost::asio::error::not_connected;
    }

    // 检查数据大小是否超过 MTU 安全值
    if (size > UDP_MAX_PACKET_SIZE) {
        NEVO_LOG_WARN("network",
                       "UDP packet size {} exceeds max payload {}, truncating",
                       size, UDP_MAX_PACKET_SIZE);
        size = UDP_MAX_PACKET_SIZE; // 截断到安全值
    }

    auto [ec, bytes_sent] = co_await socket_.async_send_to(
        boost::asio::buffer(data, size),
        endpoint,
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
        NEVO_LOG_ERROR("network",
                       "UDP send to {}:{} failed: {}",
                       endpoint.address().to_string(),
                       endpoint.port(),
                       ec.message());
    } else {
        NEVO_LOG_TRACE("network",
                       "UDP sent {} bytes to {}:{}",
                       bytes_sent,
                       endpoint.address().to_string(),
                       endpoint.port());
        NEVO_LOG_DEBUG("network", "UDP sent {} bytes", bytes_sent);
    }

    co_return ec;
}

boost::asio::awaitable<boost::system::error_code>
UdpSocket::asyncSendTo(const std::vector<uint8_t>& payload,
                       const boost::asio::ip::udp::endpoint& endpoint)
{
    co_return co_await asyncSendTo(payload.data(),
                                   static_cast<uint32_t>(payload.size()),
                                   endpoint);
}

// ============================================================
// close - 关闭套接字
// ============================================================

void UdpSocket::close()
{
    if (!open_.load()) {
        return; // 已关闭
    }

    open_.store(false);

    // 通过 strand 发布关闭操作，确保与异步读写操作串行执行，
    // 避免从非 io_context 线程直接操作 socket 导致的数据竞争。
    boost::asio::post(strand_, [this]() {
        boost::system::error_code ec;
        socket_.close(ec);
        if (ec) {
            NEVO_LOG_WARN("network", "UDP socket close error: {}", ec.message());
        }
        NEVO_LOG_DEBUG("network", "UDP socket closed (via strand)");
    });
}

// ============================================================
// isOpen - 检查套接字状态
// ============================================================

bool UdpSocket::isOpen() const
{
    return open_.load();
}

} // namespace nevo
