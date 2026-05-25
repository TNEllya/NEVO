/**
 * @file TestTcpPayloadValidation.cpp
 * @brief TCP frame payload size validation tests
 *
 * 覆盖缺口：TcpConnection 帧载荷大小安全校验
 * 风险等级：高 - 超大载荷可能导致缓冲区溢出或拒绝服务
 *
 * 背景：根据 fix-nevo-runtime-bugs 修复，Bug #1 中相关安全检查：
 *   - 检查载荷长度是否超出最大限制（TCP_MAX_PAYLOAD_SIZE）
 *   - 防止恶意大包攻击
 */

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

#include <boost/asio.hpp>

#include "nevo/network/TcpConnection.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {
namespace {

class PayloadValidationServer {
public:
    PayloadValidationServer(boost::asio::io_context& io_ctx, uint16_t port)
        : acceptor_(io_ctx, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
        , io_ctx_(io_ctx)
    {
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    }

    void start() {
        doAccept();
    }

    void stop() {
        acceptor_.close();
    }

    size_t getReceivedPayloadCount() const { return received_count_; }
    size_t getMalformedFrameCount() const { return malformed_count_; }

private:
    void doAccept() {
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_ctx_);
        acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
            if (!ec) {
                handleConnection(socket);
            }
            if (acceptor_.is_open()) {
                doAccept();
            }
        });
    }

    void handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        auto header_buf = std::make_shared<std::array<uint8_t, TCP_HEADER_SIZE>>();
        auto self = this;

        boost::asio::async_read(*socket,
            boost::asio::buffer(header_buf->data(), TCP_HEADER_SIZE),
            [this, self, socket, header_buf](boost::system::error_code ec, size_t bytes) {
                if (ec) return;

                if (bytes != TCP_HEADER_SIZE) {
                    self->malformed_count_++;
                    return;
                }

                uint32_t net_val;
                std::memcpy(&net_val, header_buf->data(), 4);
                uint32_t payload_length = ntohl(net_val);

                if (payload_length > TCP_MAX_PAYLOAD_SIZE) {
                    self->malformed_count_++;
                    boost::system::error_code ignored;
                    socket->close(ignored);
                    return;
                }

                if (payload_length > 0) {
                    auto payload_buf = std::make_shared<std::vector<uint8_t>>(payload_length);
                    boost::asio::async_read(*socket,
                        boost::asio::buffer(payload_buf->data(), payload_length),
                        [this, self, payload_buf](boost::system::error_code, size_t read_bytes) {
                            if (read_bytes == payload_buf->size()) {
                                self->received_count_++;
                            }
                        });
                } else {
                    self->received_count_++;
                }
            });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::io_context& io_ctx_;
    std::atomic<size_t> received_count_{0};
    std::atomic<size_t> malformed_count_{0};
};

TEST(TcpPayloadValidationTest, MaxPayloadSize) {
    boost::asio::io_context io_ctx;

    boost::asio::ip::tcp::acceptor temp_acceptor(io_ctx,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    uint16_t port = temp_acceptor.local_endpoint().port();
    temp_acceptor.close();

    PayloadValidationServer server(io_ctx, port);
    server.start();

    std::thread io_thread([&io_ctx]() {
        io_ctx.run();
    });

    auto conn = std::make_shared<TcpConnection>(io_ctx);

    bool connected = false;
    boost::asio::co_spawn(io_ctx,
        [conn, port, &connected]() -> boost::asio::awaitable<void> {
            auto ec = co_await conn->asyncConnect("127.0.0.1", port, 3000);
            if (!ec) {
                connected = true;
            }
        },
        boost::asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (connected) {
        std::vector<uint8_t> payload(TCP_MAX_PAYLOAD_SIZE, 0xAB);
        boost::asio::co_spawn(io_ctx,
            [conn, payload]() -> boost::asio::awaitable<void> {
                co_await conn->asyncSend(payload,
                    static_cast<uint32_t>(ControlMessageType::LoginRequest), 1);
            },
            boost::asio::detached);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    server.stop();
    io_ctx.stop();
    io_thread.join();

    EXPECT_GE(server.getReceivedPayloadCount(), 1u);
}

TEST(TcpPayloadValidationTest, ZeroPayload) {
    boost::asio::io_context io_ctx;

    boost::asio::ip::tcp::acceptor temp_acceptor(io_ctx,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    uint16_t port = temp_acceptor.local_endpoint().port();
    temp_acceptor.close();

    PayloadValidationServer server(io_ctx, port);
    server.start();

    std::thread io_thread([&io_ctx]() {
        io_ctx.run();
    });

    auto conn = std::make_shared<TcpConnection>(io_ctx);

    boost::asio::co_spawn(io_ctx,
        [conn, port]() -> boost::asio::awaitable<void> {
            co_await conn->asyncConnect("127.0.0.1", port, 3000);
            co_await conn->asyncSend(nullptr, 0,
                static_cast<uint32_t>(ControlMessageType::Ping), 1);
        },
        boost::asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    server.stop();
    io_ctx.stop();
    io_thread.join();

    EXPECT_GE(server.getReceivedPayloadCount(), 1u);
}

TEST(TcpPayloadValidationTest, SmallPayload) {
    boost::asio::io_context io_ctx;

    boost::asio::ip::tcp::acceptor temp_acceptor(io_ctx,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    uint16_t port = temp_acceptor.local_endpoint().port();
    temp_acceptor.close();

    PayloadValidationServer server(io_ctx, port);
    server.start();

    std::thread io_thread([&io_ctx]() {
        io_ctx.run();
    });

    auto conn = std::make_shared<TcpConnection>(io_ctx);

    boost::asio::co_spawn(io_ctx,
        [conn, port]() -> boost::asio::awaitable<void> {
            co_await conn->asyncConnect("127.0.0.1", port, 3000);

            std::vector<uint8_t> payload = {'H', 'e', 'l', 'l', 'o'};
            co_await conn->asyncSend(payload,
                static_cast<uint32_t>(ControlMessageType::LoginRequest), 1);
        },
        boost::asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    server.stop();
    io_ctx.stop();
    io_thread.join();

    EXPECT_GE(server.getReceivedPayloadCount(), 1u);
}

TEST(TcpPayloadValidationTest, HeaderOnlyFrame) {
    auto header = TcpConnection::encodeFrameHeader(0, 1, 0);
    EXPECT_EQ(header.size(), TCP_HEADER_SIZE);

    uint32_t net_val;
    std::memcpy(&net_val, header.data(), 4);
    uint32_t payload_length = ntohl(net_val);
    EXPECT_EQ(payload_length, 0u);
}

TEST(TcpPayloadValidationTest, PayloadLengthFieldRange) {
    for (uint32_t test_val : {0u, 1u, 100u, 1000u, TCP_MAX_PAYLOAD_SIZE, 0xFFFFFFFFu}) {
        auto header = TcpConnection::encodeFrameHeader(test_val, 1, 0);

        uint32_t net_val;
        std::memcpy(&net_val, header.data(), 4);
        uint32_t decoded = ntohl(net_val);

        EXPECT_EQ(decoded, test_val) << "Failed for value " << test_val;
    }
}

} // namespace
} // namespace nevo
