#pragma once
/**
 * @file SslWrapper.h
 * @brief TLS/SSL 包装器
 *
 * 基于 OpenSSL 和 Boost.Asio 的 SSL/TLS 封装。
 * 为 TcpConnection 提供 TLS 握手功能，确保 TCP 控制通道的
 * 通信安全。
 *
 * 使用方式：
 *   SslWrapper ssl(ctx);
 *   auto ec = co_await ssl.wrapSocket(tcp_socket);
 *   // 握手成功后，tcp_socket 已升级为 SSL 连接
 *
 * 注意：wrapSocket 会替换底层 stream，调用方需使用
 *       ssl_stream 进行后续的读写操作。
 */

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <string>
#include <functional>
#include <memory>
#include <optional>

#include "nevo/core/common/Logger.h"

namespace nevo {

// ============================================================
// SSL 包装器类
// ============================================================

class SslWrapper {
public:
    /// SSL 验证模式
    enum class VerifyMode {
        /// 完整证书验证（生产环境）
        FullVerify,
        /// 跳过证书验证（开发/测试环境，不安全）
        SkipVerify,
    };

    /// SSL 配置选项
    struct Options {
        /// 证书验证模式
        VerifyMode verify_mode = VerifyMode::FullVerify;

        /// CA 证书文件路径（用于验证服务端证书）
        /// 为空则使用系统默认 CA 证书
        std::string ca_cert_file;

        /// 客户端证书文件路径（用于双向 TLS 认证）
        std::string client_cert_file;

        /// 客户端私钥文件路径
        std::string client_key_file;

        /// 目标主机名（用于 SNI 和证书主机名验证）
        std::string hostname;
    };

    /// 构造函数（使用默认 SSL 配置）
    /// @param io_ctx Boost.Asio I/O 上下文引用
    explicit SslWrapper(boost::asio::io_context& io_ctx);

    /// 构造函数
    /// @param io_ctx Boost.Asio I/O 上下文引用
    /// @param options SSL 配置选项
    explicit SslWrapper(boost::asio::io_context& io_ctx,
                        Options options);

    /// 析构函数
    ~SslWrapper();

    // 禁止拷贝
    SslWrapper(const SslWrapper&) = delete;
    SslWrapper& operator=(const SslWrapper&) = delete;

    // ============================================================
    // TLS 握手
    // ============================================================

    /// 对已有的 TCP socket 执行 TLS 握手
    /// 成功后返回 ssl_stream 的共享指针，后续应使用此 stream 通信。
    ///
    /// @param tcp_socket 已连接的 TCP socket（所有权转移）
    /// @return 成功返回 ssl_stream 共享指针，失败返回 std::nullopt
    boost::asio::awaitable<
        std::optional<std::shared_ptr<boost::asio::ssl::stream<
            boost::asio::ip::tcp::socket>>>>
    wrapSocket(boost::asio::ip::tcp::socket tcp_socket);

    // ============================================================
    // 配置
    // ============================================================

    /// 获取 SSL 上下文引用（用于高级自定义）
    /// @return SSL 上下文引用
    boost::asio::ssl::context& sslContext();

    /// 获取当前配置选项
    /// @return 配置选项的常引用
    const Options& options() const;

    /// 是否跳过证书验证
    /// @return true 表示跳过验证（不安全，仅用于开发）
    bool skipVerify() const;

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /// 初始化 SSL 上下文配置
    void initSslContext();

    /// 证书验证回调
    /// @param preverified 预验证结果
    /// @param ctx 证书上下文
    /// @return true 接受证书
    bool verifyCertificate(bool preverified,
                           boost::asio::ssl::verify_context& ctx);

    // ============================================================
    // 成员变量
    // ============================================================

    /// SSL 上下文
    boost::asio::ssl::context ssl_ctx_;

    /// 配置选项
    Options options_;
};

} // namespace nevo
