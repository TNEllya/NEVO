#pragma once
/**
 * @file TcpConnection.h
 * @brief TCP 连接封装
 *
 * 基于 Boost.Asio 和 C++20 协程的 TCP 连接类。
 * 使用 strand 保证线程安全，采用帧协议进行消息边界划分。
 *
 * 帧格式：[4字节载荷长度][4字节消息类型][4字节request_id][payload]
 * - 载荷长度：payload 的字节数（不包含帧头）
 * - 消息类型：ControlMessageType 枚举值
 * - request_id：请求-响应关联 ID
 *
 * 使用方式：
 *   auto conn = std::make_shared<TcpConnection>(io_ctx);
 *   conn->onMessage = [](std::vector<uint8_t> data, uint32_t msg_type, uint32_t request_id) { ... };
 *   conn->onDisconnected = []() { ... };
 *   co_await conn->asyncConnect("127.0.0.1", 8080);
 *   co_await conn->asyncReadLoop();
 */

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <functional>
#include <vector>
#include <optional>
#include <memory>
#include <atomic>
#include <string>

#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {

// ============================================================
// TCP 连接类
// ============================================================

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    /// 构造函数
    /// @param io_ctx Boost.Asio I/O 上下文引用
    explicit TcpConnection(boost::asio::io_context& io_ctx);

    /// 析构函数：确保连接关闭
    ~TcpConnection();

    // 禁止拷贝
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // ============================================================
    // 协程式异步操作
    // ============================================================

    /// 异步连接到远端主机（协程）
    /// @param host 远端主机地址（IP 或域名）
    /// @param port 远端端口号
    /// @param timeout_ms 连接超时时间（毫秒），默认 5000ms
    /// @return boost::system::error_code，成功返回 0
    boost::asio::awaitable<boost::system::error_code>
    asyncConnect(const std::string& host, uint16_t port, uint32_t timeout_ms = 5000);

    /// 启动异步读取循环（协程）
    /// 该协程会持续读取帧消息，直到连接关闭或出错。
    /// 每读取到一个完整帧，调用 onMessage 回调。
    /// @return boost::system::error_code，连接结束时的错误码
    boost::asio::awaitable<void> asyncReadLoop();

    /// 异步发送数据（协程）
    /// @param data 待发送的载荷数据指针
    /// @param size 载荷字节数
    /// @param type 消息类型（ControlMessageType 枚举值）
    /// @param request_id 请求关联 ID，默认 0
    /// @return boost::system::error_code，成功返回 0
    boost::asio::awaitable<boost::system::error_code>
    asyncSend(const uint8_t* data, uint32_t size,
              uint32_t type, uint32_t request_id = 0);

    /// 异步发送数据（vector 便捷重载）
    /// @param payload 待发送的载荷数据
    /// @param type 消息类型
    /// @param request_id 请求关联 ID
    /// @return boost::system::error_code
    boost::asio::awaitable<boost::system::error_code>
    asyncSend(const std::vector<uint8_t>& payload,
              uint32_t type, uint32_t request_id = 0);

    // ============================================================
    // TLS/SSL 支持
    // ============================================================

    /**
     * @brief 将已连接的 TCP socket 升级为 TLS 连接
     *
     * 在 asyncConnect 成功后调用。将底层 TCP socket 移入 SSL stream，
     * 执行 TLS 握手。成功后所有读写操作自动通过 SSL stream 进行。
     *
     * @param ssl_ctx SSL 上下文（含 CA 证书、客户端证书等配置）
     * @param hostname 用于 SNI 和证书主机名验证的目标主机名
     * @param skip_verify 是否跳过证书验证（仅开发环境）
     * @return error_code，成功返回 0
     */
    boost::asio::awaitable<boost::system::error_code>
    asyncSslHandshake(boost::asio::ssl::context& ssl_ctx,
                      const std::string& hostname,
                      bool skip_verify = false);

    /// 查询是否已启用 TLS
    bool isSslEnabled() const { return use_ssl_; }

    // ============================================================
    // 连接管理
    // ============================================================

    /// 优雅关闭连接
    /// 关闭发送端，等待对端关闭，最终关闭接收端。
    void close();

    /// 检查连接是否已建立
    /// @return true 表示 socket 处于打开状态
    bool isConnected() const;

    /// 获取远端端点信息字符串（用于日志）
    /// @return "host:port" 格式的字符串，未连接时返回 "not connected"
    std::string remoteEndpointString() const;

    // ============================================================
    // 回调设置
    // ============================================================

    /// 消息到达回调
    /// 参数为载荷数据（不含帧头）、消息类型和请求ID
    std::function<void(std::vector<uint8_t>, uint32_t, uint32_t)> onMessage;

    /// 连接断开回调
    std::function<void()> onDisconnected;

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /// 读取帧头（12字节）
    /// @return 帧头数据，失败返回 nullopt
    boost::asio::awaitable<std::optional<std::array<uint8_t, TCP_HEADER_SIZE>>>
    readFrameHeader();

    /// 解析帧头字节流
    bool parseFrameHeader(const std::array<uint8_t, TCP_HEADER_SIZE>& header_data,
                          uint32_t& out_payload_length,
                          uint32_t& out_message_type,
                          uint32_t& out_request_id);

    /// 编码帧头到缓冲区
    static std::array<uint8_t, TCP_HEADER_SIZE>
    encodeFrameHeader(uint32_t payload_length, uint32_t message_type, uint32_t request_id);

    /// 触发断开连接回调
    void notifyDisconnected();

    // ============================================================
    // 服务端支持
    // ============================================================

public:

    /**
     * @brief 获取底层 socket 引用（用于服务端 accept）
     *
     * 允许 ServerCore 直接在 TcpConnection 的 socket 上执行 async_accept。
     * 当 TLS 已启用时，返回 SSL stream 内部的底层 socket。
     *
     * @return TCP socket 引用
     */
    boost::asio::ip::tcp::socket& socket();

    /**
     * @brief 设置连接状态（用于服务端 accept 成功后）
     * @param connected 是否已连接
     */
    void setConnected(bool connected) { connected_ = connected; }

    // ============================================================
    // 成员变量
    // ============================================================

    /// TCP socket（SSL 未启用时使用；SSL 启用后所有权转移至 ssl_stream_）
    boost::asio::ip::tcp::socket socket_;

    /// SSL stream（SSL 启用后拥有 socket 所有权）
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ssl_stream_;

    /// 是否使用 SSL
    bool use_ssl_ = false;

    /// Strand，保证所有异步操作串行执行
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    /// 连接状态标记
    std::atomic<bool> connected_{false};

    /// 是否正在关闭
    std::atomic<bool> closing_{false};

    /// 读缓冲区（帧头 + 载荷复用）
    std::vector<uint8_t> read_buffer_;
};

} // namespace nevo
