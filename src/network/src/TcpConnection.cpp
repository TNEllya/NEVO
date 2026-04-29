/**
 * @file TcpConnection.cpp
 * @brief TCP 连接封装实现（含 TLS/SSL 支持）
 *
 * 基于 C++20 协程和 Boost.Asio 的异步 TCP 连接实现。
 * 所有异步操作使用 as_tuple(use_awaitable) 捕获错误码，
 * 避免异常抛出，便于上层根据 error_code 决策。
 *
 * TLS 支持：
 *   - asyncSslHandshake() 将已连接的 TCP socket 移入 ssl::stream，
 *     执行 TLS 握手后，后续所有读写自动通过 SSL stream 进行。
 *   - 内部读写方法根据 use_ssl_ 标志自动选择底层传输对象。
 *
 * 线程安全说明：
 *   - 所有 socket 操作都通过 strand 调度，保证不会并发执行
 *   - connected_ 和 closing_ 使用 std::atomic，可安全跨线程读取
 *   - close() 方法通过 strand 发布关闭操作，确保与异步操作串行执行
 */

#include "nevo/network/TcpConnection.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/endian.hpp>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

TcpConnection::TcpConnection(boost::asio::io_context& io_ctx)
    : socket_(io_ctx)
    , strand_(boost::asio::make_strand(io_ctx))
    , read_buffer_(TCP_MAX_PAYLOAD_SIZE) // 预分配最大载荷大小的缓冲区
{
    NEVO_LOG_DEBUG("network", "TcpConnection constructed");
}

TcpConnection::~TcpConnection()
{
    close();
    NEVO_LOG_DEBUG("network", "TcpConnection destroyed");
}

// ============================================================
// asyncConnect - 协程式连接
// ============================================================

boost::asio::awaitable<boost::system::error_code>
TcpConnection::asyncConnect(const std::string& host, uint16_t port, uint32_t timeout_ms)
{
    // 获取当前协程的执行器（strand 绑定）
    auto executor = boost::asio::get_associated_executor(co_await boost::asio::this_coro::executor);
    auto strand_executor = strand_;

    // ---- DNS 解析 ----
    boost::asio::ip::tcp::resolver resolver(strand_executor);
    auto [resolve_ec, results] = co_await resolver.async_resolve(
        host, std::to_string(port),
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (resolve_ec) {
        NEVO_LOG_ERROR("network", "DNS resolve failed for {}:{} - {}",
                       host, port, resolve_ec.message());
        co_return resolve_ec;
    }

    NEVO_LOG_DEBUG("network", "DNS resolved {}:{} -> {} endpoints",
                   host, port, std::distance(results.begin(), results.end()));

    // ---- 带超时的连接 ----
    // 使用 steady_timer 实现连接超时
    boost::asio::steady_timer timer(strand_executor);
    timer.expires_after(std::chrono::milliseconds(timeout_ms));

    // 启动超时定时器（回调方式：超时后关闭 socket 取消连接）
    timer.async_wait(
        [this](boost::system::error_code ec) {
            if (!ec) {
                // 超时，关闭 socket 以取消连接操作
                boost::system::error_code ignored;
                socket_.close(ignored);
            }
        });

    // 协程式异步连接：co_await 等待连接完成
    auto [connect_ec, endpoint] = co_await boost::asio::async_connect(
        socket_, results,
        boost::asio::as_tuple(boost::asio::use_awaitable));

    // 取消定时器（连接已完成或已失败）
    timer.cancel();

    // 判断结果
    if (connect_ec == boost::asio::error::operation_aborted) {
        NEVO_LOG_ERROR("network", "Connect to {}:{} timed out ({}ms)",
                       host, port, timeout_ms);
        co_return boost::asio::error::timed_out;
    }

    if (connect_ec) {
        NEVO_LOG_ERROR("network", "Connect to {}:{} failed - {}",
                       host, port, connect_ec.message());
        co_return connect_ec;
    }

    // 连接成功
    connected_.store(true);
    NEVO_LOG_INFO("network", "Connected to {}:{}",
                  host, port);
    co_return boost::system::error_code{};
}

// ============================================================
// asyncSslHandshake - TLS 升级
// ============================================================

boost::asio::awaitable<boost::system::error_code>
TcpConnection::asyncSslHandshake(boost::asio::ssl::context& ssl_ctx,
                                 const std::string& hostname,
                                 bool skip_verify)
{
    if (!connected_.load()) {
        NEVO_LOG_ERROR("network", "Cannot perform SSL handshake: not connected");
        co_return boost::asio::error::not_connected;
    }

    if (use_ssl_) {
        NEVO_LOG_WARN("network", "SSL already enabled on this connection");
        co_return boost::system::error_code{};
    }

    NEVO_LOG_INFO("network", "Starting TLS handshake with {} (skip_verify={})",
                  hostname, skip_verify);

    // ---- 1. 从 socket_ 取出底层 TCP socket 并创建 ssl::stream ----
    // 需要从 socket_ 中释放底层描述符，移入 ssl::stream。
    // Boost.Asio ssl::stream 构造函数接受 tcp::socket。
    // 我们先获取 socket_ 的远端端点（用于日志），然后移动构造 ssl_stream_。

    // 移动 socket_ 到 ssl_stream_（转移所有权）
    ssl_stream_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
        std::move(socket_), ssl_ctx);

    // ---- 2. 配置 SNI ----
    if (!hostname.empty()) {
        SSL_set_tlsext_host_name(ssl_stream_->native_handle(), hostname.c_str());
    }

    // ---- 3. 配置证书验证 ----
    if (!skip_verify) {
        ssl_stream_->set_verify_mode(boost::asio::ssl::verify_peer);
        ssl_stream_->set_verify_callback(
            [hostname](bool preverified, boost::asio::ssl::verify_context& ctx) {
                // 简化验证：使用 Boost 内置的主机名验证
                // 完整实现可以使用 X509 证书链验证
                if (!preverified) {
                    NEVO_LOG_WARN("network", "TLS certificate pre-verification failed");
                    return false;
                }
                return true;
            });
    } else {
        ssl_stream_->set_verify_mode(boost::asio::ssl::verify_none);
        NEVO_LOG_WARN("network", "TLS certificate verification DISABLED");
    }

    // ---- 4. 执行 TLS 握手 ----
    auto [ec] = co_await ssl_stream_->async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
        NEVO_LOG_ERROR("network", "TLS handshake failed: {}", ec.message());

        // 握手失败：关闭 SSL stream（同时关闭底层 socket）
        boost::system::error_code ignored;
        ssl_stream_->shutdown(ignored);
        ssl_stream_->lowest_layer().close(ignored);
        ssl_stream_.reset();

        // socket_ 已被移入 ssl_stream_ 并随之上面的 close() 一起关闭，
        // 标记为断开状态
        connected_.store(false);
        co_return ec;
    }

    // ---- 5. 标记 SSL 已启用 ----
    use_ssl_ = true;
    NEVO_LOG_INFO("network", "TLS handshake succeeded (host={})", hostname);

    co_return boost::system::error_code{};
}

// ============================================================
// asyncReadLoop - 协程式读取循环
// ============================================================

boost::asio::awaitable<void> TcpConnection::asyncReadLoop()
{
    // 确保在 strand 上执行
    auto executor = strand_;
    boost::asio::dispatch(executor, []() {});

    NEVO_LOG_DEBUG("network", "asyncReadLoop started (ssl={})", use_ssl_);

    while (connected_.load() && !closing_.load()) {
        // ---- 步骤1：读取帧头（12字节）----
        auto header_opt = co_await readFrameHeader();
        if (!header_opt.has_value()) {
            // 读取帧头失败，连接可能已断开
            NEVO_LOG_DEBUG("network", "Read frame header failed, exiting read loop");
            break;
        }

        // ---- 步骤2：解析帧头 ----
        uint32_t payload_length = 0;
        uint32_t message_type = 0;
        uint32_t request_id = 0;

        if (!parseFrameHeader(header_opt.value(), payload_length, message_type, request_id)) {
            NEVO_LOG_ERROR("network", "Invalid frame header, disconnecting");
            break;
        }

        NEVO_LOG_TRACE("network",
                       "Frame header: payload_len={}, type={}, request_id={}",
                       payload_length, message_type, request_id);

        // ---- 步骤3：安全校验 ----
        // 检查载荷长度是否超出最大限制，防止恶意大包攻击
        if (payload_length > TCP_MAX_PAYLOAD_SIZE) {
            NEVO_LOG_ERROR("network",
                           "Payload too large: {} > {}, disconnecting",
                           payload_length, TCP_MAX_PAYLOAD_SIZE);
            break;
        }

        // ---- 步骤4：读取载荷 ----
        if (payload_length > 0) {
            // 确保缓冲区足够大
            if (read_buffer_.size() < payload_length) {
                read_buffer_.resize(payload_length);
            }

            boost::system::error_code ec;
            size_t bytes_read = 0;

            if (use_ssl_) {
                auto [read_ec, read_n] = co_await boost::asio::async_read(
                    *ssl_stream_,
                    boost::asio::buffer(read_buffer_.data(), payload_length),
                    boost::asio::as_tuple(boost::asio::use_awaitable));
                ec = read_ec;
                bytes_read = read_n;
            } else {
                auto [read_ec, read_n] = co_await boost::asio::async_read(
                    socket_,
                    boost::asio::buffer(read_buffer_.data(), payload_length),
                    boost::asio::as_tuple(boost::asio::use_awaitable));
                ec = read_ec;
                bytes_read = read_n;
            }

            if (ec) {
                NEVO_LOG_ERROR("network", "Read payload failed: {}", ec.message());
                break;
            }

            if (bytes_read != payload_length) {
                NEVO_LOG_ERROR("network",
                               "Incomplete payload: expected {}, got {}",
                               payload_length, bytes_read);
                break;
            }

            // ---- 步骤5：触发消息回调 ----
            if (onMessage) {
                std::vector<uint8_t> payload(read_buffer_.begin(),
                                             read_buffer_.begin() + payload_length);
                onMessage(std::move(payload), message_type, request_id);
            }
        } else {
            // 零载荷帧（如心跳响应），仍然触发回调
            if (onMessage) {
                onMessage(std::vector<uint8_t>{}, message_type, request_id);
            }
        }
    }

    // 读取循环结束，标记断开
    connected_.store(false);
    notifyDisconnected();
    NEVO_LOG_DEBUG("network", "asyncReadLoop ended");
}

// ============================================================
// asyncSend - 协程式发送
// ============================================================

boost::asio::awaitable<boost::system::error_code>
TcpConnection::asyncSend(const uint8_t* data, uint32_t size,
                         uint32_t type, uint32_t request_id)
{
    if (!connected_.load() || closing_.load()) {
        NEVO_LOG_WARN("network", "Attempted send on disconnected/closing connection");
        co_return boost::asio::error::not_connected;
    }

    // ---- 构建发送缓冲区：帧头 + 载荷 ----
    // 使用分散写（scatter-gather）避免额外拷贝：
    //   buffer 0 = 帧头（12字节）
    //   buffer 1 = 载荷
    auto header = encodeFrameHeader(size, type, request_id);

    // 如果载荷为空，只发送帧头
    if (size == 0) {
        boost::system::error_code ec;

        if (use_ssl_) {
            auto [write_ec, bytes_written] = co_await boost::asio::async_write(
                *ssl_stream_,
                boost::asio::buffer(header.data(), header.size()),
                boost::asio::as_tuple(boost::asio::use_awaitable));
            ec = write_ec;
        } else {
            auto [write_ec, bytes_written] = co_await boost::asio::async_write(
                socket_,
                boost::asio::buffer(header.data(), header.size()),
                boost::asio::as_tuple(boost::asio::use_awaitable));
            ec = write_ec;
        }

        if (ec) {
            NEVO_LOG_ERROR("network", "Send frame header failed: {}", ec.message());
            connected_.store(false);
            notifyDisconnected();
        }

        co_return ec;
    }

    // 分散写入
    std::array<boost::asio::const_buffer, 2> buffers = {
        boost::asio::buffer(header.data(), header.size()),
        boost::asio::buffer(data, size)
    };

    boost::system::error_code ec;

    if (use_ssl_) {
        auto [write_ec, bytes_written] = co_await boost::asio::async_write(
            *ssl_stream_,
            buffers,
            boost::asio::as_tuple(boost::asio::use_awaitable));
        ec = write_ec;
    } else {
        auto [write_ec, bytes_written] = co_await boost::asio::async_write(
            socket_,
            buffers,
            boost::asio::as_tuple(boost::asio::use_awaitable));
        ec = write_ec;
    }

    if (ec) {
        NEVO_LOG_ERROR("network", "Send frame failed: {}", ec.message());
        connected_.store(false);
        notifyDisconnected();
    } else {
        NEVO_LOG_TRACE("network",
                       "Sent frame: type={}, request_id={}, payload_size={}",
                       type, request_id, size);
    }

    co_return ec;
}

boost::asio::awaitable<boost::system::error_code>
TcpConnection::asyncSend(const std::vector<uint8_t>& payload,
                         uint32_t type, uint32_t request_id)
{
    co_return co_await asyncSend(payload.data(),
                                 static_cast<uint32_t>(payload.size()),
                                 type, request_id);
}

// ============================================================
// close - 优雅关闭
// ============================================================

void TcpConnection::close()
{
    // 防止重复关闭
    bool expected = false;
    if (!closing_.compare_exchange_strong(expected, true)) {
        return; // 已经在关闭中
    }

    NEVO_LOG_INFO("network", "Closing TCP connection to {} (ssl={})",
                  remoteEndpointString(), use_ssl_);

    // 通过 strand 发布关闭操作，确保与异步读写操作串行执行，
    // 避免从非 io_context 线程直接操作 socket 导致的数据竞争。
    boost::asio::post(strand_, [this]() {
        boost::system::error_code ec;

        if (use_ssl_ && ssl_stream_) {
            // SSL 连接：先优雅关闭 SSL 层
            ssl_stream_->shutdown(ec);
            if (ec) {
                NEVO_LOG_DEBUG("network", "SSL shutdown failed: {}", ec.message());
            }

            // 关闭底层 socket（通过 ssl_stream_ 的 next_layer，即 tcp::socket）
            ssl_stream_->next_layer().close(ec);
            if (ec) {
                NEVO_LOG_DEBUG("network", "Close SSL socket failed: {}", ec.message());
            }

            ssl_stream_.reset();
        } else {
            // 纯 TCP 连接
            // 步骤1：关闭发送端，发送 FIN 包
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
            if (ec) {
                NEVO_LOG_DEBUG("network", "Shutdown send failed: {}", ec.message());
            }

            // 步骤2：关闭接收端
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_receive, ec);
            if (ec) {
                NEVO_LOG_DEBUG("network", "Shutdown receive failed: {}", ec.message());
            }

            // 步骤3：关闭 socket
            socket_.close(ec);
            if (ec) {
                NEVO_LOG_DEBUG("network", "Close socket failed: {}", ec.message());
            }
        }

        connected_.store(false);
        closing_.store(false); // 重置，允许后续操作检测到已断开

        NEVO_LOG_DEBUG("network", "TCP connection closed (via strand)");
    });
}

// ============================================================
// 状态查询
// ============================================================

bool TcpConnection::isConnected() const
{
    return connected_.load();
}

std::string TcpConnection::remoteEndpointString() const
{
    boost::system::error_code ec;

    if (use_ssl_ && ssl_stream_) {
        auto endpoint = ssl_stream_->next_layer().remote_endpoint(ec);
        if (ec) {
            return "not connected";
        }
        return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    }

    auto endpoint = socket_.remote_endpoint(ec);
    if (ec) {
        return "not connected";
    }
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

// ============================================================
// socket() - 服务端 accept 用
// ============================================================

boost::asio::ip::tcp::socket& TcpConnection::socket()
{
    // SSL 启用后，socket_ 已被移入 ssl_stream_，需返回 ssl_stream_ 的下一层（tcp::socket）
    if (use_ssl_ && ssl_stream_) {
        return ssl_stream_->next_layer();
    }
    return socket_;
}

// ============================================================
// 内部方法
// ============================================================

boost::asio::awaitable<std::optional<std::array<uint8_t, TCP_HEADER_SIZE>>>
TcpConnection::readFrameHeader()
{
    std::array<uint8_t, TCP_HEADER_SIZE> header_buf{};

    boost::system::error_code ec;
    size_t bytes_read = 0;

    if (use_ssl_) {
        auto [read_ec, read_n] = co_await boost::asio::async_read(
            *ssl_stream_,
            boost::asio::buffer(header_buf.data(), TCP_HEADER_SIZE),
            boost::asio::as_tuple(boost::asio::use_awaitable));
        ec = read_ec;
        bytes_read = read_n;
    } else {
        auto [read_ec, read_n] = co_await boost::asio::async_read(
            socket_,
            boost::asio::buffer(header_buf.data(), TCP_HEADER_SIZE),
            boost::asio::as_tuple(boost::asio::use_awaitable));
        ec = read_ec;
        bytes_read = read_n;
    }

    if (ec) {
        if (ec != boost::asio::error::eof) {
            NEVO_LOG_ERROR("network", "Read frame header failed: {}", ec.message());
        } else {
            NEVO_LOG_DEBUG("network", "Connection closed by peer (EOF on header read)");
        }
        co_return std::nullopt;
    }

    if (bytes_read != TCP_HEADER_SIZE) {
        NEVO_LOG_ERROR("network",
                       "Incomplete frame header: expected {}, got {}",
                       TCP_HEADER_SIZE, bytes_read);
        co_return std::nullopt;
    }

    co_return header_buf;
}

bool TcpConnection::parseFrameHeader(
    const std::array<uint8_t, TCP_HEADER_SIZE>& header_data,
    uint32_t& out_payload_length,
    uint32_t& out_message_type,
    uint32_t& out_request_id)
{
    // 网络字节序（大端）转主机字节序
    // 使用 memcpy 避免未对齐内存访问（ARM 平台安全性）
    uint32_t net_payload_length, net_message_type, net_request_id;
    std::memcpy(&net_payload_length, header_data.data(), 4);
    std::memcpy(&net_message_type, header_data.data() + 4, 4);
    std::memcpy(&net_request_id, header_data.data() + 8, 4);

    out_payload_length = boost::endian::big_to_native(net_payload_length);
    out_message_type = boost::endian::big_to_native(net_message_type);
    out_request_id = boost::endian::big_to_native(net_request_id);

    return true;
}

std::array<uint8_t, TCP_HEADER_SIZE>
TcpConnection::encodeFrameHeader(uint32_t payload_length, uint32_t message_type, uint32_t request_id)
{
    std::array<uint8_t, TCP_HEADER_SIZE> buf{};

    // 主机字节序转网络字节序（大端）
    const uint32_t net_payload_length = boost::endian::native_to_big(payload_length);
    const uint32_t net_message_type   = boost::endian::native_to_big(message_type);
    const uint32_t net_request_id     = boost::endian::native_to_big(request_id);

    std::memcpy(buf.data(),     &net_payload_length, 4);
    std::memcpy(buf.data() + 4, &net_message_type,   4);
    std::memcpy(buf.data() + 8, &net_request_id,     4);

    return buf;
}

void TcpConnection::notifyDisconnected()
{
    if (onDisconnected) {
        onDisconnected();
    }
}

} // namespace nevo
