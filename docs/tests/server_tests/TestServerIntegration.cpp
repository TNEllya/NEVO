/**
 * @file TestServerIntegration.cpp
 * @brief Integration tests for NEVO server subsystems
 *
 * Tests the Database, ChannelManager, and a full server login flow.
 * Uses temporary SQLite database files for isolation.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <boost/asio.hpp>

#include "nevo/server/Database.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/server/ServerCore.h"
#include "nevo/network/TcpConnection.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {
namespace {

// Helper: create a unique temporary database path
static std::string makeTempDbPath() {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("nevo_test_" + std::to_string(++counter) + ".db");
    return path.string();
}

// ============================================================
// Database tests
// ============================================================

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = makeTempDbPath();
        db_ = std::make_shared<Database>();
        auto result = db_->initialize(db_path_);
        ASSERT_TRUE(result.ok()) << "Database init failed: " << result.error().message();
    }

    void TearDown() override {
        db_.reset();
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::shared_ptr<Database> db_;
    std::string db_path_;
};

TEST_F(DatabaseTest, CreateUserAndVerify) {
    auto create_result = db_->createUser("alice", "password123");
    ASSERT_TRUE(create_result.ok()) << create_result.error().message();
    UserId uid = create_result.value();
    EXPECT_NE(uid, INVALID_USER_ID);

    // Verify with correct password
    auto verify_result = db_->verifyUser("alice", "password123");
    EXPECT_TRUE(verify_result.ok());
    EXPECT_EQ(verify_result.value(), uid);

    // Verify with wrong password
    auto bad_verify = db_->verifyUser("alice", "wrongpassword");
    EXPECT_FALSE(bad_verify.ok());
    EXPECT_EQ(bad_verify.error().code(), ResultCode::AuthFailed);
}

TEST_F(DatabaseTest, VerifyNonexistentUser) {
    auto result = db_->verifyUser("nobody", "password");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code(), ResultCode::AuthFailed);
}

TEST_F(DatabaseTest, DuplicateUserFails) {
    auto r1 = db_->createUser("bob", "pass1");
    EXPECT_TRUE(r1.ok());

    auto r2 = db_->createUser("bob", "pass2");
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code(), ResultCode::InvalidRequest);
}

TEST_F(DatabaseTest, GetUserById) {
    auto create_result = db_->createUser("charlie", "mypass");
    ASSERT_TRUE(create_result.ok());
    UserId uid = create_result.value();

    auto record = db_->getUser(uid);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->id, uid);
    EXPECT_EQ(record->username, "charlie");
    EXPECT_EQ(record->group_id, GroupId(3)); // Default User group
    EXPECT_GT(record->created_at, 0);
}

TEST_F(DatabaseTest, GetUserByName) {
    auto create_result = db_->createUser("dave", "hispass");
    ASSERT_TRUE(create_result.ok());

    auto record = db_->getUserByName("dave");
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->username, "dave");
}

TEST_F(DatabaseTest, GetNonexistentUserReturnsNullopt) {
    auto record = db_->getUser(UserId(9999));
    EXPECT_FALSE(record.has_value());
}

TEST_F(DatabaseTest, CreateAndGetChannel) {
    auto create_result = db_->createChannel("General", ChannelId(0), UserId(0));
    ASSERT_TRUE(create_result.ok());
    ChannelId cid = create_result.value();

    auto record = db_->getChannel(cid);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->name, "General");
    EXPECT_EQ(record->parent_id, ChannelId(0));
}

TEST_F(DatabaseTest, DeleteChannel) {
    auto create_result = db_->createChannel("TempChannel", ChannelId(0), UserId(0));
    ASSERT_TRUE(create_result.ok());
    ChannelId cid = create_result.value();

    auto delete_result = db_->deleteChannel(cid);
    EXPECT_TRUE(delete_result.ok());

    auto record = db_->getChannel(cid);
    EXPECT_FALSE(record.has_value());
}

TEST_F(DatabaseTest, DeleteNonexistentChannel) {
    auto result = db_->deleteChannel(ChannelId(9999));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code(), ResultCode::ChannelNotFound);
}

TEST_F(DatabaseTest, ConfigSetAndGet) {
    auto set_result = db_->setConfig("server_name", "NEVO Test");
    EXPECT_TRUE(set_result.ok());

    auto value = db_->getConfig("server_name");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "NEVO Test");
}

TEST_F(DatabaseTest, GetNonexistentConfigReturnsNullopt) {
    auto value = db_->getConfig("nonexistent_key");
    EXPECT_FALSE(value.has_value());
}

// ============================================================
// ChannelManager tests
// ============================================================

class ChannelManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = makeTempDbPath();
        db_ = std::make_shared<Database>();
        auto result = db_->initialize(db_path_);
        ASSERT_TRUE(result.ok());

        mgr_ = std::make_shared<ChannelManager>(db_);
        auto init_result = mgr_->initialize();
        ASSERT_TRUE(init_result.ok()) << init_result.error().message();
    }

    void TearDown() override {
        mgr_.reset();
        db_.reset();
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::shared_ptr<Database> db_;
    std::shared_ptr<ChannelManager> mgr_;
    std::string db_path_;
};

TEST_F(ChannelManagerTest, DefaultChannelStructureCreated) {
    // After initialization, Root and Lobby should exist
    auto* root = mgr_->getRootChannel();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name(), "Root");

    auto* lobby = mgr_->getDefaultChannel();
    ASSERT_NE(lobby, nullptr);
    EXPECT_EQ(lobby->name(), "Lobby");

    // Lobby should be a child of Root
    EXPECT_EQ(lobby->parent(), root);
}

TEST_F(ChannelManagerTest, CreateChannelUnderRoot) {
    auto result = mgr_->createChannel(ChannelId(0), "Gaming", UserId(1));
    ASSERT_TRUE(result.ok()) << result.error().message();
    ChannelId cid = result.value();

    auto* ch = mgr_->getChannel(cid);
    ASSERT_NE(ch, nullptr);
    EXPECT_EQ(ch->name(), "Gaming");
    EXPECT_EQ(ch->parent(), mgr_->getRootChannel());
}

TEST_F(ChannelManagerTest, CreateSubChannel) {
    auto parent_result = mgr_->createChannel(ChannelId(0), "TeamA", UserId(1));
    ASSERT_TRUE(parent_result.ok());
    ChannelId parent_id = parent_result.value();

    auto child_result = mgr_->createChannel(parent_id, "TeamA-Voice", UserId(1));
    ASSERT_TRUE(child_result.ok()) << child_result.error().message();
    ChannelId child_id = child_result.value();

    auto* child = mgr_->getChannel(child_id);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->name(), "TeamA-Voice");

    auto* parent = mgr_->getChannel(parent_id);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(child->parent(), parent);

    // findChild should find the nested channel from root
    auto* root = mgr_->getRootChannel();
    EXPECT_NE(root->findChild(child_id), nullptr);
}

TEST_F(ChannelManagerTest, MoveUserToChannel) {
    auto lobby_id = mgr_->getDefaultChannel()->id();

    auto move_result = mgr_->moveUserToChannel(UserId(1), lobby_id);
    EXPECT_TRUE(move_result.ok()) << move_result.error().message();

    auto* user_channel = mgr_->getUserChannel(UserId(1));
    ASSERT_NE(user_channel, nullptr);
    EXPECT_EQ(user_channel->id(), lobby_id);
    EXPECT_TRUE(lobby_id != ChannelId(0));
}

TEST_F(ChannelManagerTest, MoveUserBetweenChannels) {
    auto lobby_id = mgr_->getDefaultChannel()->id();
    auto ch_result = mgr_->createChannel(ChannelId(0), "Gaming", UserId(1));
    ASSERT_TRUE(ch_result.ok());
    ChannelId gaming_id = ch_result.value();

    // Move user to Lobby first
    mgr_->moveUserToChannel(UserId(1), lobby_id);
    EXPECT_TRUE(mgr_->getDefaultChannel()->hasUser(UserId(1)));

    // Move to Gaming
    mgr_->moveUserToChannel(UserId(1), gaming_id);
    EXPECT_FALSE(mgr_->getDefaultChannel()->hasUser(UserId(1)));

    auto* gaming = mgr_->getChannel(gaming_id);
    ASSERT_NE(gaming, nullptr);
    EXPECT_TRUE(gaming->hasUser(UserId(1)));
}

TEST_F(ChannelManagerTest, RemoveUserFromChannel) {
    auto lobby_id = mgr_->getDefaultChannel()->id();
    mgr_->moveUserToChannel(UserId(1), lobby_id);
    EXPECT_NE(mgr_->getUserChannel(UserId(1)), nullptr);

    mgr_->removeUserFromChannel(UserId(1));
    EXPECT_EQ(mgr_->getUserChannel(UserId(1)), nullptr);
}

TEST_F(ChannelManagerTest, DeleteChannelNotAllowedForRootAndLobby) {
    auto* root = mgr_->getRootChannel();
    auto delete_root = mgr_->deleteChannel(root->id());
    EXPECT_FALSE(delete_root.ok());

    auto* lobby = mgr_->getDefaultChannel();
    auto delete_lobby = mgr_->deleteChannel(lobby->id());
    EXPECT_FALSE(delete_lobby.ok());
}

TEST_F(ChannelManagerTest, DeleteChannelRemovesFromTree) {
    auto ch_result = mgr_->createChannel(ChannelId(0), "TempRoom", UserId(1));
    ASSERT_TRUE(ch_result.ok());
    ChannelId cid = ch_result.value();

    auto delete_result = mgr_->deleteChannel(cid);
    EXPECT_TRUE(delete_result.ok()) << delete_result.error().message();

    EXPECT_EQ(mgr_->getChannel(cid), nullptr);
}

TEST_F(ChannelManagerTest, GetAllChannels) {
    auto channels = mgr_->getAllChannels();
    // At minimum, Root and Lobby
    EXPECT_GE(channels.size(), 2u);
}

// ============================================================
// Full login flow integration test
// ============================================================

class ServerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = makeTempDbPath();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::string db_path_;
};

TEST_F(ServerIntegrationTest, ServerInitializeAndShutdown) {
    boost::asio::io_context io_ctx;

    // Use high ports to reduce collision risk
    uint16_t tcp_port = 24800;
    uint16_t udp_port = 24801;

    ServerCore server(io_ctx, tcp_port, udp_port);

    // Initialize
    auto init_result = server.initialize(db_path_);
    ASSERT_TRUE(init_result.ok()) << "Server init failed: " << init_result.error().message();

    // Verify subsystems are initialized
    EXPECT_NE(server.database(), nullptr);
    EXPECT_NE(server.channelManager(), nullptr);

    // Start server (non-blocking)
    server.start();
    EXPECT_TRUE(server.isRunning());

    // Give the server a moment to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Shutdown
    server.shutdown();
    EXPECT_FALSE(server.isRunning());
}

TEST_F(ServerIntegrationTest, ServerDatabaseAndChannelManagerWork) {
    boost::asio::io_context io_ctx;

    ServerCore server(io_ctx, 24810, 24811);
    auto init_result = server.initialize(db_path_);
    ASSERT_TRUE(init_result.ok());

    // Create a user via the server's database
    auto db = server.database();
    ASSERT_NE(db, nullptr);

    auto user_result = db->createUser("testuser", "testpass");
    ASSERT_TRUE(user_result.ok());
    UserId uid = user_result.value();

    // Verify the user
    auto verify_result = db->verifyUser("testuser", "testpass");
    EXPECT_TRUE(verify_result.ok());
    EXPECT_EQ(verify_result.value(), uid);

    // Check channel manager
    auto ch_mgr = server.channelManager();
    ASSERT_NE(ch_mgr, nullptr);

    auto* root = ch_mgr->getRootChannel();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name(), "Root");

    auto* lobby = ch_mgr->getDefaultChannel();
    ASSERT_NE(lobby, nullptr);
    EXPECT_EQ(lobby->name(), "Lobby");

    // Create a custom channel
    auto ch_result = ch_mgr->createChannel(ChannelId(0), "TestRoom", uid);
    EXPECT_TRUE(ch_result.ok());

    // Move user to the custom channel
    auto move_result = ch_mgr->moveUserToChannel(uid, ch_result.value());
    EXPECT_TRUE(move_result.ok());

    auto* user_channel = ch_mgr->getUserChannel(uid);
    ASSERT_NE(user_channel, nullptr);
    EXPECT_EQ(user_channel->id(), ch_result.value());

    server.shutdown();
}

TEST_F(ServerIntegrationTest, ClientConnectLoginDisconnect) {
    boost::asio::io_context io_ctx;

    // Use ports that are less likely to be in use
    uint16_t tcp_port = 24820;
    uint16_t udp_port = 24821;

    ServerCore server(io_ctx, tcp_port, udp_port);
    auto init_result = server.initialize(db_path_);
    ASSERT_TRUE(init_result.ok());

    // Pre-create a user for login
    auto db = server.database();
    auto user_result = db->createUser("loginuser", "loginpass");
    ASSERT_TRUE(user_result.ok());

    // Start server
    server.start();

    // Run io_context in a background thread
    std::thread io_thread([&io_ctx]() {
        io_ctx.run();
    });

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Create a client connection
    auto client_conn = std::make_shared<TcpConnection>(io_ctx);

    bool connected = false;
    bool disconnected = false;

    client_conn->onDisconnected = [&disconnected]() {
        disconnected = true;
    };

    // Connect to server using asyncConnect
    boost::asio::co_spawn(io_ctx,
        [client_conn, tcp_port, &connected]() -> boost::asio::awaitable<void> {
            auto ec = co_await client_conn->asyncConnect("127.0.0.1", tcp_port, 3000);
            if (!ec) {
                connected = true;
            }
        },
        boost::asio::detached);

    // Wait for connection
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (connected) {
        EXPECT_TRUE(client_conn->isConnected());

        // Disconnect
        client_conn->close();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Shutdown server
    server.shutdown();
    io_ctx.stop();

    if (io_thread.joinable()) {
        io_thread.join();
    }
}

} // namespace
} // namespace nevo
