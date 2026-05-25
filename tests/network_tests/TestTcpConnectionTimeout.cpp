/**
 * @file TestTcpConnectionTimeout.cpp
 * @brief Timeout and error handling tests for TcpConnection
 *
 * 覆盖缺口：TcpConnection asyncConnect 超时逻辑修复后的测试
 * 风险等级：高 - 连接超时是网络模块核心功能
 *
 * 背景：根据 fix-nevo-runtime-bugs 修复，Bug #1 TcpConnection 异步连接超时逻辑 broken：
 *   - 原代码启动 async_connect 和 timer.async_wait 后未等待任何操作完成，
 *     立即 timer.cancel()，导致 connect_ec 永远为 would_block
 *   - 修复方案：使用 boost::asio::cancel_after 实现带超时的连接
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

#include <boost/asio.hpp>

#include "nevo/network/TcpConnection.h"

namespace nevo {
namespace {

class TcpConnectionTimeoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_ctx_ = std::make_unique<boost::asio::io_context>();
    }

    void TearDown() override {
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

    void runIoContext(std::chrono::milliseconds duration = std::chrono::seconds(10)) {
        io_thread_ = std::thread([this, duration]() {
            auto deadline = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < deadline && io_ctx_->run_one()) {}
            io_ctx_->stop();
        });
    }

    std::unique_ptr<boost::asio::io_context> io_ctx_;
    std::thread io_thread_;
};

TEST_F(TcpConnectionTimeoutTest, ConnectTimeoutNonExistentHost) {
    auto conn = std::make_shared<TcpConnection>(*io_ctx_);

    boost::system::error_code connect_ec;
    std::atomic<bool> connect_done{false};

    boost::asio::co_spawn(*io_ctx_,
        [conn, &connect_ec, &connect_done]() -> boost::asio::awaitable<void> {
            connect_ec = co_await conn->asyncConnect("192.0.2.1", 12345, 1000);
            connect_done.store(true);
        },
        boost::asio::detached);

    runIoContext(std::chrono::seconds(5));

    EXPECT_TRUE(connect_done.load());
    EXPECT_TRUE(connect_ec);
    EXPECT_FALSE(conn->isConnected());
}

TEST_F(TcpConnectionTimeoutTest, ConnectTimeoutRefusedPort) {
    auto conn = std::make_shared<TcpConnection>(*io_ctx_);

    boost::system::error_code connect_ec;
    std::atomic<bool> connect_done{false};

    boost::asio::co_spawn(*io_ctx_,
        [conn, &connect_ec, &connect_done]() -> boost::asio::awaitable<void> {
            connect_ec = co_await conn->asyncConnect("127.0.0.1", 65432, 1000);
            connect_done.store(true);
        },
        boost::asio::detached);

    runIoContext(std::chrono::seconds(5));

    EXPECT_TRUE(connect_done.load());
    EXPECT_TRUE(connect_ec);
    EXPECT_FALSE(conn->isConnected());
}

TEST_F(TcpConnectionTimeoutTest, ConnectSuccess) {
    boost::asio::ip::tcp::acceptor acceptor(*io_ctx_,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    uint16_t port = acceptor.local_endpoint().port();
    acceptor.close();

    auto conn = std::make_shared<TcpConnection>(*io_ctx_);

    boost::system::error_code connect_ec;
    std::atomic<bool> connect_done{false};

    boost::asio::co_spawn(*io_ctx_,
        [conn, port, &connect_ec, &connect_done]() -> boost::asio::awaitable<void> {
            connect_ec = co_await conn->asyncConnect("127.0.0.1", port, 3000);
            connect_done.store(true);
        },
        boost::asio::detached);

    runIoContext(std::chrono::seconds(5));

    EXPECT_TRUE(connect_done.load());
    EXPECT_FALSE(connect_ec) << "Connect error: " << connect_ec.message();
    EXPECT_TRUE(conn->isConnected());
}

TEST_F(TcpConnectionTimeoutTest, MultipleRapidConnections) {
    boost::asio::ip::tcp::acceptor acceptor(*io_ctx_,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    uint16_t port = acceptor.local_endpoint().port();
    acceptor.close();

    constexpr int NUM_CONNECTIONS = 5;
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::vector<std::shared_ptr<TcpConnection>> connections;

    for (int i = 0; i < NUM_CONNECTIONS; ++i) {
        auto conn = std::make_shared<TcpConnection>(*io_ctx_);
        connections.push_back(conn);

        boost::asio::co_spawn(*io_ctx_,
            [conn, port, &success_count, &fail_count]() -> boost::asio::awaitable<void> {
                auto ec = co_await conn->asyncConnect("127.0.0.1", port, 3000);
                if (!ec) {
                    success_count.fetch_add(1);
                } else {
                    fail_count.fetch_add(1);
                }
            },
            boost::asio::detached);
    }

    runIoContext(std::chrono::seconds(10));

    EXPECT_EQ(fail_count.load(), 0);
}

TEST_F(TcpConnectionTimeoutTest, ConnectToInvalidAddress) {
    auto conn = std::make_shared<TcpConnection>(*io_ctx_);

    boost::system::error_code connect_ec;

    boost::asio::co_spawn(*io_ctx_,
        [conn, &connect_ec]() -> boost::asio::awaitable<void> {
            connect_ec = co_await conn->asyncConnect("invalid hostname", 12345, 1000);
        },
        boost::asio::detached);

    runIoContext(std::chrono::seconds(5));

    EXPECT_TRUE(connect_ec);
}

} // namespace
} // namespace nevo
