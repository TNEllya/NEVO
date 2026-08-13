/**
 * @file TestAudioRelaySecurity.cpp
 * @brief AudioRelay 授权模型安全单测
 *
 * 直接验证语音中继的 fail-closed 安全修复（对应修复方案 T-03b/c）：
 *   1. 未认证端点（映射表无记录）发包 → 丢弃
 *   2. 包头 sender_id 与映射身份不一致 → 丢弃
 *   3. 发送者非目标频道成员 → 丢弃（防跨频道语音注入）
 *   4. 发送者无加密上下文 → 丢弃（不再"原样转发"）
 *   5. 合法成员 → 正常转发（逐客户端解密/重加密）
 */

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "nevo/server/AudioRelay.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/server/Database.h"
#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/common/Types.h"

#include "voice.pb.h"

namespace nevo {
namespace {

// ---- 工具 ----

std::string makeTempDbPath() {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("nevo_relay_test_" + std::to_string(++counter) + ".db");
    return path.string();
}

void fillKey(std::array<uint8_t, CRYPTO_KEY_SIZE>& key, int seed) {
    for (size_t i = 0; i < CRYPTO_KEY_SIZE; ++i) {
        key[i] = static_cast<uint8_t>((seed * 7 + static_cast<int>(i) * 13 + 37) & 0xFF);
    }
}

// 组包：2B 小端头长 + protobuf VoicePacketHeader + 加密载荷
// AAD = protobuf 头（不含 2B 长度前缀），与 AudioRelay 解密端一致
std::vector<uint8_t> buildVoicePacket(VoiceCrypto& crypto,
                                      UserId sender_id,
                                      ChannelId channel_id,
                                      const std::vector<uint8_t>& payload) {
    voice::VoicePacketHeader header;
    header.set_sequence_number(1);
    header.set_sender_id(sender_id.value);
    header.set_channel_id(channel_id.value);
    header.set_timestamp(1234);
    header.set_tcp_tunnel(false);

    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    header.SerializeToArray(header_buf.data(), static_cast<int>(header_size));

    std::vector<uint8_t> encrypted = crypto.encrypt(
        payload.data(), static_cast<uint32_t>(payload.size()),
        header_buf.data(), header_buf.size());

    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::vector<uint8_t> full_packet;
    full_packet.reserve(2 + header_buf.size() + encrypted.size());
    full_packet.insert(full_packet.end(),
                       reinterpret_cast<const uint8_t*>(&header_len),
                       reinterpret_cast<const uint8_t*>(&header_len) + 2);
    full_packet.insert(full_packet.end(), header_buf.begin(), header_buf.end());
    full_packet.insert(full_packet.end(), encrypted.begin(), encrypted.end());
    return full_packet;
}

// 解密校验：2B 头长 + protobuf 头 + 加密载荷，AAD = protobuf 头
bool decryptVoicePacketForTest(VoiceCrypto& crypto,
                               const uint8_t* data, uint32_t size,
                               std::vector<uint8_t>& out_plaintext) {
    uint32_t header_size = 0;
    auto header = decodeVoicePacketHeader(data, size, header_size);
    if (!header) return false;

    auto [payload_ptr, payload_size] = getVoicePayload(data, header_size, size);
    if (!payload_ptr || payload_size < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE) {
        return false;
    }

    const uint8_t* nonce = payload_ptr;
    const uint8_t* ciphertext = payload_ptr + XCHACHA_NONCE_SIZE;
    size_t ct_len = payload_size - XCHACHA_NONCE_SIZE;

    auto decrypted = crypto.decrypt(
        ciphertext, ct_len, nonce, XCHACHA_NONCE_SIZE,
        data + 2, header_size - 2);
    if (!decrypted) return false;

    out_plaintext = std::move(*decrypted);
    return true;
}

// ---- Fixture ----

class AudioRelaySecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试用户 ID（不与频道 ID 冲突）——先赋值再建立频道成员关系
        user_a_ = UserId(1001);
        user_b_ = UserId(1002);
        user_c_ = UserId(1003);

        db_path_ = makeTempDbPath();
        db_ = std::make_shared<Database>();
        auto db_result = db_->initialize(db_path_);
        ASSERT_TRUE(db_result.ok()) << db_result.error().message();

        channel_mgr_ = std::make_shared<ChannelManager>(db_);
        auto ch_result = channel_mgr_->initialize();
        ASSERT_TRUE(ch_result.ok()) << ch_result.error().message();

        lobby_id_ = channel_mgr_->getDefaultChannel()->id();
        ASSERT_TRUE(lobby_id_);

        // 第二个频道 RoomA：user_b 所在，user_a 不在
        auto room_result = channel_mgr_->createChannel(
            ChannelId(0), "RoomA", UserId(1));
        ASSERT_TRUE(room_result.ok());
        room_id_ = room_result.value();

        // user_a / user_c 在 Lobby，user_b 在 RoomA
        ASSERT_TRUE(channel_mgr_->moveUserToChannel(user_a_, lobby_id_).ok());
        ASSERT_TRUE(channel_mgr_->moveUserToChannel(user_c_, lobby_id_).ok());
        ASSERT_TRUE(channel_mgr_->moveUserToChannel(user_b_, room_id_).ok());

        relay_ = std::make_shared<AudioRelay>();
        relay_->setChannelManager(channel_mgr_);
        relay_->setSessionKeyQuery([this](UserId uid) -> const uint8_t* {
            auto it = keys_.find(uid);
            return it != keys_.end() ? it->second.data() : nullptr;
        });

        io_ctx_ = std::make_shared<boost::asio::io_context>();
        relay_udp_ = std::make_shared<UdpSocket>(*io_ctx_);
        auto bind_result = relay_udp_->bind(0);
        ASSERT_FALSE(bind_result) << bind_result.message();
        relay_->setUdpSocket(relay_udp_);
        relay_->setIoContext(*io_ctx_);
    }

    void TearDown() override {
        relay_.reset();
        channel_mgr_.reset();
        db_.reset();
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    boost::asio::ip::udp::endpoint makeEndpoint(uint16_t port) {
        return {boost::asio::ip::make_address("127.0.0.1"), port};
    }

    std::string db_path_;
    std::shared_ptr<Database> db_;
    std::shared_ptr<ChannelManager> channel_mgr_;
    std::shared_ptr<AudioRelay> relay_;
    std::shared_ptr<boost::asio::io_context> io_ctx_;
    std::shared_ptr<UdpSocket> relay_udp_;

    std::unordered_map<UserId, std::array<uint8_t, CRYPTO_KEY_SIZE>> keys_;

    ChannelId lobby_id_;
    ChannelId room_id_;
    UserId user_a_;
    UserId user_b_;
    UserId user_c_;
};

// ============================================================
// 安全用例
// ============================================================

TEST_F(AudioRelaySecurityTest, UnknownEndpointRejected) {
    // 无任何映射的端点发包 → 必须丢弃（不自动建映射、不转发）
    VoiceCrypto attacker_crypto;
    std::array<uint8_t, CRYPTO_KEY_SIZE> attacker_key{};
    fillKey(attacker_key, 1);
    attacker_crypto.setSessionKey(attacker_key.data());

    std::vector<uint8_t> payload(64, 0x55);
    auto packet = buildVoicePacket(attacker_crypto, user_a_, lobby_id_, payload);

    relay_->handleVoicePacket(packet.data(), static_cast<uint32_t>(packet.size()),
                              makeEndpoint(40001));

    EXPECT_EQ(relay_->packetsRelayed(), 0u);
    EXPECT_EQ(relay_->packetsDropped(), 1u);
}

TEST_F(AudioRelaySecurityTest, HeaderSenderIdMismatchRejected) {
    // 已认证的 user_a 端点发包，但包头声称 sender_id=user_b → 丢弃
    auto ep_a = makeEndpoint(40002);
    relay_->addClientMapping(user_a_, ep_a);

    VoiceCrypto crypto;
    std::array<uint8_t, CRYPTO_KEY_SIZE> key_a{};
    fillKey(key_a, 2);
    crypto.setSessionKey(key_a.data());
    keys_[user_a_] = key_a;

    std::vector<uint8_t> payload(64, 0x66);
    auto packet = buildVoicePacket(crypto, user_b_, lobby_id_, payload);  // 伪造身份

    relay_->handleVoicePacket(packet.data(), static_cast<uint32_t>(packet.size()), ep_a);

    EXPECT_EQ(relay_->packetsRelayed(), 0u);
    EXPECT_EQ(relay_->packetsDropped(), 1u);
}

TEST_F(AudioRelaySecurityTest, NonMemberChannelRejected) {
    // user_b 是 RoomA 成员，试图向 Lobby 发语音 → 丢弃（防跨频道注入）
    auto ep_b = makeEndpoint(40003);
    relay_->addClientMapping(user_b_, ep_b);

    VoiceCrypto crypto;
    std::array<uint8_t, CRYPTO_KEY_SIZE> key_b{};
    fillKey(key_b, 3);
    crypto.setSessionKey(key_b.data());
    keys_[user_b_] = key_b;

    std::vector<uint8_t> payload(64, 0x77);
    auto packet = buildVoicePacket(crypto, user_b_, lobby_id_, payload);

    relay_->handleVoicePacket(packet.data(), static_cast<uint32_t>(packet.size()), ep_b);

    EXPECT_EQ(relay_->packetsRelayed(), 0u);
    EXPECT_EQ(relay_->packetsDropped(), 1u);
}

TEST_F(AudioRelaySecurityTest, NoCryptoContextFailClosed) {
    // user_a 是 Lobby 成员但服务端没有其会话密钥 → 丢弃（绝不原样转发）
    auto ep_a = makeEndpoint(40004);
    relay_->addClientMapping(user_a_, ep_a);
    // 加入 user_c 映射使 peers 非空，确保走到"无密钥上下文即丢弃"分支
    relay_->addClientMapping(user_c_, makeEndpoint(40009));
    // 注意：keys_ 中不放入 user_a 的密钥

    VoiceCrypto crypto;
    std::array<uint8_t, CRYPTO_KEY_SIZE> key{};
    fillKey(key, 4);
    crypto.setSessionKey(key.data());

    std::vector<uint8_t> payload(64, 0x88);
    auto packet = buildVoicePacket(crypto, user_a_, lobby_id_, payload);

    relay_->handleVoicePacket(packet.data(), static_cast<uint32_t>(packet.size()), ep_a);

    EXPECT_EQ(relay_->packetsRelayed(), 0u);
    EXPECT_EQ(relay_->packetsDropped(), 1u);
}

TEST_F(AudioRelaySecurityTest, LegitimateMemberForwarded) {
    // user_a 与 user_c 同为 Lobby 成员且均有密钥 → 语音正常转发（重加密）
    auto ep_a = makeEndpoint(40005);
    relay_->addClientMapping(user_a_, ep_a);

    auto receiver_udp = std::make_shared<UdpSocket>(*io_ctx_);
    auto receiver_bind = receiver_udp->bind(0);
    ASSERT_FALSE(receiver_bind) << receiver_bind.message();
    // bind(0) 为双栈 socket，local_endpoint() 是 [::]:port（任意地址，不可作目的）。
    // 取其端口构造回环 IPv4 端点作为中继映射地址（双栈 socket 可接收 IPv4 报文）。
    auto receiver_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"),
        receiver_udp->socket().local_endpoint().port());
    relay_->addClientMapping(user_c_, receiver_ep);

    std::array<uint8_t, CRYPTO_KEY_SIZE> key_a{};
    std::array<uint8_t, CRYPTO_KEY_SIZE> key_c{};
    fillKey(key_a, 5);
    fillKey(key_c, 6);
    keys_[user_a_] = key_a;
    keys_[user_c_] = key_c;

    VoiceCrypto sender_crypto;
    sender_crypto.setSessionKey(key_a.data());
    VoiceCrypto receiver_crypto;
    receiver_crypto.setSessionKey(key_c.data());

    const std::vector<uint8_t> payload(64, 0x99);
    auto packet = buildVoicePacket(sender_crypto, user_a_, lobby_id_, payload);

    // 运行 io_context（处理中继的异步发送协程）
    auto work = boost::asio::make_work_guard(*io_ctx_);
    std::thread io_thread([this]() { io_ctx_->run(); });

    relay_->handleVoicePacket(packet.data(), static_cast<uint32_t>(packet.size()), ep_a);

    // 接收端轮询（非阻塞 + 2s 截止）
    receiver_udp->socket().non_blocking(true);
    std::vector<uint8_t> recv_buf(2048);
    boost::asio::ip::udp::endpoint from;
    boost::system::error_code recv_ec;
    size_t recv_n = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        recv_n = receiver_udp->socket().receive_from(
            boost::asio::buffer(recv_buf), from, 0, recv_ec);
        if (!recv_ec && recv_n > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    work.reset();
    io_ctx_->stop();
    io_thread.join();

    ASSERT_FALSE(recv_ec) << recv_ec.message();
    ASSERT_GT(recv_n, 0u) << "Receiver got nothing — relay forwarding broken";

    // 验证收到的是用 user_c 密钥重加密的、内容一致的语音
    std::vector<uint8_t> decrypted;
    ASSERT_TRUE(decryptVoicePacketForTest(receiver_crypto, recv_buf.data(),
                                          static_cast<uint32_t>(recv_n), decrypted));
    EXPECT_EQ(decrypted, payload);

    EXPECT_GE(relay_->packetsRelayed(), 1u);
}

} // namespace
} // namespace nevo
