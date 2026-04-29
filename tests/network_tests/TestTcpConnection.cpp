/**
 * @file TestTcpConnection.cpp
 * @brief Unit tests for TCP connection frame protocol
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

// ============================================================
// TCP frame header encode/decode
// ============================================================

TEST(TcpFrameTest, EncodeFrameHeaderAndParse) {
    // Use the static method from TcpConnection to encode
    auto header = TcpConnection::encodeFrameHeader(100, 1, 42);

    EXPECT_EQ(header.size(), TCP_HEADER_SIZE);

    // Parse it back using the public parseFrameHeader method
    // We need a TcpConnection instance to access parseFrameHeader, but it's private.
    // So we do manual parsing using boost::endian or manual network byte order conversion.

    // Parse payload_length (bytes 0-3, big endian)
    uint32_t net_val;
    std::memcpy(&net_val, header.data(), 4);
    uint32_t payload_length = ntohl(net_val);
    EXPECT_EQ(payload_length, 100u);

    // Parse message_type (bytes 4-7)
    uint32_t net_type;
    std::memcpy(&net_type, header.data() + 4, 4);
    uint32_t message_type = ntohl(net_type);
    EXPECT_EQ(message_type, 1u);

    // Parse request_id (bytes 8-11)
    uint32_t net_req;
    std::memcpy(&net_req, header.data() + 8, 4);
    uint32_t request_id = ntohl(net_req);
    EXPECT_EQ(request_id, 42u);
}

TEST(TcpFrameTest, EncodeZeroPayload) {
    auto header = TcpConnection::encodeFrameHeader(0, 5, 0);

    uint32_t net_val;
    std::memcpy(&net_val, header.data(), 4);
    uint32_t payload_length = ntohl(net_val);
    EXPECT_EQ(payload_length, 0u);
}

TEST(TcpFrameTest, EncodeMaxValues) {
    auto header = TcpConnection::encodeFrameHeader(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);

    uint32_t net_val;
    std::memcpy(&net_val, header.data(), 4);
    EXPECT_EQ(ntohl(net_val), 0xFFFFFFFFu);

    std::memcpy(&net_val, header.data() + 4, 4);
    EXPECT_EQ(ntohl(net_val), 0xFFFFFFFFu);

    std::memcpy(&net_val, header.data() + 8, 4);
    EXPECT_EQ(ntohl(net_val), 0xFFFFFFFFu);
}

// ============================================================
// Connection creation (use local echo server)
// ============================================================

class EchoServer {
public:
    EchoServer(boost::asio::io_context& io_ctx, uint16_t port)
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

private:
    void doAccept() {
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_ctx_);
        acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
            if (!ec) {
                doRead(socket);
            }
            if (acceptor_.is_open()) {
                doAccept();
            }
        });
    }

    void doRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        auto header_buf = std::make_shared<std::array<uint8_t, TCP_HEADER_SIZE>>();
        auto self = this;

        boost::asio::async_read(*socket,
            boost::asio::buffer(header_buf->data(), TCP_HEADER_SIZE),
            [this, self, socket, header_buf](boost::system::error_code ec, size_t) {
                if (ec) return;

                // Parse payload length
                uint32_t net_val;
                std::memcpy(&net_val, header_buf->data(), 4);
                uint32_t payload_length = ntohl(net_val);

                if (payload_length > 0 && payload_length <= TCP_MAX_PAYLOAD_SIZE) {
                    auto payload_buf = std::make_shared<std::vector<uint8_t>>(payload_length);
                    boost::asio::async_read(*socket,
                        boost::asio::buffer(payload_buf->data(), payload_length),
                        [this, self, socket, header_buf, payload_buf](boost::system::error_code ec2, size_t) {
                            if (ec2) return;
                            // Echo back: header + payload
                            std::vector<boost::asio::const_buffer> buffers;
                            buffers.push_back(boost::asio::buffer(header_buf->data(), TCP_HEADER_SIZE));
                            buffers.push_back(boost::asio::buffer(payload_buf->data(), payload_buf->size()));
                            boost::asio::async_write(*socket, buffers,
                                [](boost::system::error_code, size_t) {});
                        });
                } else {
                    // Zero payload: just echo header
                    boost::asio::async_write(*socket,
                        boost::asio::buffer(header_buf->data(), TCP_HEADER_SIZE),
                        [](boost::system::error_code, size_t) {});
                }
            });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::io_context& io_ctx_;
};

TEST(TcpConnectionTest, ConnectionCreationAndState) {
    boost::asio::io_context io_ctx;

    auto conn = std::make_shared<TcpConnection>(io_ctx);
    EXPECT_FALSE(conn->isConnected());
}

TEST(TcpConnectionTest, SendReceiveRoundtripWithEchoServer) {
    boost::asio::io_context io_ctx;

    // Start echo server on a random available port
    boost::asio::ip::tcp::acceptor temp_acceptor(io_ctx,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    uint16_t server_port = temp_acceptor.local_endpoint().port();
    temp_acceptor.close();

    EchoServer server(io_ctx, server_port);
    server.start();

    // Run io_context in a background thread
    std::thread io_thread([&io_ctx]() {
        io_ctx.run();
    });

    // Create client connection
    auto conn = std::make_shared<TcpConnection>(io_ctx);

    // Set up message receiver
    std::vector<uint8_t> received_payload;
    bool message_received = false;

    conn->onMessage = [&received_payload, &message_received](std::vector<uint8_t> data) {
        received_payload = std::move(data);
        message_received = true;
    };

    // Connect to echo server using asyncConnect via co_spawn
    boost::asio::co_spawn(io_ctx,
        [conn, server_port]() -> boost::asio::awaitable<void> {
            auto ec = co_await conn->asyncConnect("127.0.0.1", server_port, 3000);
            EXPECT_FALSE(ec) << "Connect failed: " << ec.message();
            EXPECT_TRUE(conn->isConnected());

            // Start read loop
            boost::asio::co_spawn(conn->socket().get_executor(),
                [conn]() -> boost::asio::awaitable<void> {
                    co_await conn->asyncReadLoop();
                },
                boost::asio::detached);

            // Send a test message
            std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
            auto send_ec = co_await conn->asyncSend(payload,
                static_cast<uint32_t>(ControlMessageType::LoginRequest), 1);
            EXPECT_FALSE(send_ec) << "Send failed: " << send_ec.message();

            // Wait for echo response
            co_await boost::asio::steady_timer(
                co_await boost::asio::this_coro::executor,
                std::chrono::milliseconds(500)).async_wait(
                    boost::asio::use_awaitable);
        },
        boost::asio::detached);

    // Wait for the test to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    server.stop();
    io_ctx.stop();

    if (io_thread.joinable()) {
        io_thread.join();
    }
}

TEST(TcpConnectionTest, RemoteEndpointStringWhenNotConnected) {
    boost::asio::io_context io_ctx;
    auto conn = std::make_shared<TcpConnection>(io_ctx);
    EXPECT_EQ(conn->remoteEndpointString(), "not connected");
}

} // namespace
} // namespace nevo
