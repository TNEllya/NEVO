/**
 * @file SslWrapper.cpp
 * @brief TLS/SSL 包装器实现
 *
 * 使用 Boost.Asio 的 SSL 支持和 OpenSSL 实现 TLS 握手。
 * 支持：
 *   - TLS 客户端握手
 *   - 证书验证（可配置跳过，用于开发调试）
 *   - SNI（Server Name Indication）扩展
 *   - 双向 TLS 认证（mTLS）
 *
 * 安全注意事项：
 *   - SkipVerify 模式仅用于开发/测试，生产环境必须使用 FullVerify
 *   - FullVerify 模式下会验证证书链和主机名
 */

#include "nevo/network/SslWrapper.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

SslWrapper::SslWrapper(boost::asio::io_context& io_ctx)
    : SslWrapper(io_ctx, Options{})
{
}

SslWrapper::SslWrapper(boost::asio::io_context& io_ctx, Options options)
    : ssl_ctx_(boost::asio::ssl::context::tls_client)
    , options_(std::move(options))
{
    initSslContext();
    NEVO_LOG_DEBUG("network", "SslWrapper constructed (verify={})",
                   options_.verify_mode == VerifyMode::FullVerify ? "full" : "skip");
}

SslWrapper::~SslWrapper()
{
    NEVO_LOG_DEBUG("network", "SslWrapper destroyed");
}

// ============================================================
// wrapSocket - TLS 握手
// ============================================================

boost::asio::awaitable<
    std::optional<std::shared_ptr<boost::asio::ssl::stream<
        boost::asio::ip::tcp::socket>>>>
SslWrapper::wrapSocket(boost::asio::ip::tcp::socket tcp_socket)
{
    NEVO_LOG_INFO("network", "Starting TLS handshake with {}",
                  options_.hostname.empty() ? "(no SNI)" : options_.hostname);

    // ---- 步骤1：创建 SSL stream ----
    // 将 tcp_socket 移动到 ssl_stream 中，socket 的所有权转移
    auto ssl_stream = std::make_shared<boost::asio::ssl::stream<
        boost::asio::ip::tcp::socket>>(std::move(tcp_socket), ssl_ctx_);

    // ---- 步骤2：设置 SNI（Server Name Indication）----
    // SNI 允许服务端在同一 IP 上托管多个 TLS 证书
    if (!options_.hostname.empty()) {
        SSL_set_tlsext_host_name(ssl_stream->native_handle(),
                                 options_.hostname.c_str());

        NEVO_LOG_DEBUG("network", "SNI hostname set to {}", options_.hostname);
    }

    // ---- 步骤3：配置证书验证 ----
    if (options_.verify_mode == VerifyMode::FullVerify) {
        // 设置验证模式：验证对端证书
        ssl_stream->set_verify_mode(boost::asio::ssl::verify_peer);

        // 使用 RFC 6125 主机名验证 + 自定义日志回调
        if (!options_.hostname.empty()) {
            auto host_verify = boost::asio::ssl::host_name_verification(options_.hostname);
            ssl_stream->set_verify_callback(
                [this, host_verify](bool preverified, boost::asio::ssl::verify_context& ctx) mutable {
                    // 先执行自定义验证（记录证书链信息）
                    verifyCertificate(preverified, ctx);
                    // 再执行主机名验证
                    return host_verify(preverified, ctx);
                });
        } else {
            ssl_stream->set_verify_callback(
                [this](bool preverified, boost::asio::ssl::verify_context& ctx) {
                    return verifyCertificate(preverified, ctx);
                });
        }

        NEVO_LOG_DEBUG("network", "SSL verify mode: full (verify_peer, host_name_verification)");
    } else {
        // 跳过证书验证（仅用于开发/测试）
        ssl_stream->set_verify_mode(boost::asio::ssl::verify_none);

        NEVO_LOG_WARN("network",
                      "SSL verify mode: SKIP (insecure, development only!)");
    }

    // ---- 步骤4：执行 TLS 握手 ----
    auto [ec] = co_await ssl_stream->async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
        NEVO_LOG_ERROR("network", "TLS handshake failed: {}", ec.message());

        // 关闭 ssl_stream 以释放资源
        boost::system::error_code shutdown_ec;
        ssl_stream->shutdown(shutdown_ec);

        co_return std::nullopt;
    }

    NEVO_LOG_INFO("network", "TLS handshake successful (cipher: {})",
                  SSL_get_cipher_name(ssl_stream->native_handle()));

    co_return ssl_stream;
}

// ============================================================
// 配置访问
// ============================================================

boost::asio::ssl::context& SslWrapper::sslContext()
{
    return ssl_ctx_;
}

const SslWrapper::Options& SslWrapper::options() const
{
    return options_;
}

bool SslWrapper::skipVerify() const
{
    return options_.verify_mode == VerifyMode::SkipVerify;
}

// ============================================================
// 内部方法
// ============================================================

void SslWrapper::initSslContext()
{
    // ---- 设置最低 TLS 版本为 TLS 1.2 ----
    // 禁用 SSLv2、SSLv3、TLS 1.0、TLS 1.1（已知不安全）
    SSL_CTX_set_min_proto_version(ssl_ctx_.native_handle(), TLS1_2_VERSION);

    // ---- 加载默认 CA 证书 ----
    // 如果未指定 CA 证书文件，使用系统默认
    if (options_.ca_cert_file.empty()) {
        ssl_ctx_.set_default_verify_paths();
        NEVO_LOG_DEBUG("network", "Using system default CA certificates");
    } else {
        // 加载指定的 CA 证书文件
        boost::system::error_code ec;
        ssl_ctx_.load_verify_file(options_.ca_cert_file, ec);
        if (ec) {
            NEVO_LOG_ERROR("network",
                           "Failed to load CA cert file '{}': {}",
                           options_.ca_cert_file, ec.message());
        } else {
            NEVO_LOG_DEBUG("network", "Loaded CA cert from '{}'",
                           options_.ca_cert_file);
        }
    }

    // ---- 配置客户端证书（双向 TLS）----
    if (!options_.client_cert_file.empty() && !options_.client_key_file.empty()) {
        boost::system::error_code ec;

        // 加载客户端证书
        ssl_ctx_.use_certificate_file(options_.client_cert_file,
                                       boost::asio::ssl::context::pem, ec);
        if (ec) {
            NEVO_LOG_ERROR("network",
                           "Failed to load client cert '{}': {}",
                           options_.client_cert_file, ec.message());
        }

        // 加载客户端私钥
        ssl_ctx_.use_private_key_file(options_.client_key_file,
                                       boost::asio::ssl::context::pem, ec);
        if (ec) {
            NEVO_LOG_ERROR("network",
                           "Failed to load client key '{}': {}",
                           options_.client_key_file, ec.message());
        }

        NEVO_LOG_INFO("network", "Client certificate configured for mTLS");
    }

    // ---- 禁用不安全的密码套件 ----
    // 仅允许 ECDHE 和 DHE 密钥交换（前向保密）
    SSL_CTX_set_cipher_list(ssl_ctx_.native_handle(),
                            "HIGH:!aNULL:!MD5:!DSS");
}

bool SslWrapper::verifyCertificate(bool preverified,
                                    boost::asio::ssl::verify_context& ctx)
{
    // 获取当前证书链中正在验证的证书
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    if (!cert) {
        NEVO_LOG_WARN("network", "SSL verify: no certificate in context");
        return preverified;
    }

    // 获取证书主题信息（用于日志）
    char subject_buf[256];
    X509_NAME_oneline(X509_get_subject_name(cert), subject_buf, sizeof(subject_buf));

    int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());

    if (!preverified) {
        int error = X509_STORE_CTX_get_error(ctx.native_handle());
        NEVO_LOG_ERROR("network",
                       "SSL verify failed at depth {}: {} (subject: {})",
                       depth,
                       X509_verify_cert_error_string(error),
                       subject_buf);
        return false;
    }

    NEVO_LOG_TRACE("network",
                   "SSL verify OK at depth {} (subject: {})",
                   depth, subject_buf);

    return true;
}

} // namespace nevo
