/**
 * @file TestVoiceRelayIntegration.cpp
 * @brief 多客户端语音中继集成测试
 *
 * 验证修复后的语音通信端到端流程：
 *   合成音频 → Opus编码 → 加密 → UDP发送 → 中继
 *   → UDP接收 → 解密 → Opus解码 → 验证音频内容
 *
 * 测试场景：
 *   1. 包格式一致性验证（2字节长度前缀）
 *   2. AAD匹配验证（XChaCha20-Poly1305解密成功）
 *   3. AAD不匹配验证（错误AAD解密必须失败）
 *   4. Opus编解码 + 加密/解密端到端
 *   5. TCP语音帧类型识别
 *   6. 双客户端同频道语音中继（UDP路径）
 *   7. 三客户端同频道语音中继
 *   8. 频道隔离验证（不同频道用户不互通）
 *   9. TCP隧道语音包中继
 *   10. 完整语音链路 - Opus → 加密 → 中继 → 解密 → Opus 解码
 */

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <unordered_map>

#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"
#include "nevo/network/TcpVoiceTunnel.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/common/Types.h"
#include "nevo/core/audio/OpusEncoder.h"
#include "nevo/core/audio/OpusDecoder.h"

#include "voice.pb.h"

namespace nevo {

namespace {

std::atomic<uint16_t> g_next_port{27100};
uint16_t allocatePort() { return g_next_port.fetch_add(1); }

std::vector<float> generateSineWave(float frequency, float duration_sec,
                                     float sample_rate = 48000.0f) {
    size_t num_samples = static_cast<size_t>(duration_sec * sample_rate);
    std::vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        samples[i] = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * frequency *
                                       static_cast<float>(i) / sample_rate);
    }
    return samples;
}

std::vector<uint8_t> encodeOpusFrame(OpusEncoder& encoder,
                                      const std::vector<float>& pcm,
                                      int frame_size = 960) {
    std::vector<uint8_t> output(4000);
    int len = encoder.encode(pcm.data(), frame_size, output.data(),
                             static_cast<opus_int32>(output.size()));
    if (len <= 0) return {};
    output.resize(static_cast<size_t>(len));
    return output;
}

std::vector<float> decodeOpusFrame(OpusDecoder& decoder,
                                    const uint8_t* data, size_t size,
                                    int frame_size = 960) {
    std::vector<float> pcm(static_cast<size_t>(frame_size));
    int len = decoder.decode(data, static_cast<int>(size), pcm.data(),
                              frame_size);
    if (len <= 0) return {};
    pcm.resize(static_cast<size_t>(len));
    return pcm;
}

std::vector<uint8_t> buildVoicePacket(VoiceCrypto& crypto,
                                       UserId sender_id,
                                       ChannelId channel_id,
                                       uint64_t sequence,
                                       const uint8_t* payload,
                                       uint32_t payload_size) {
    voice::VoicePacketHeader header;
    header.set_sequence_number(sequence);
    header.set_sender_id(sender_id.value);
    header.set_channel_id(channel_id.value);
    header.set_timestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    header.set_tcp_tunnel(false);

    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    header.SerializeToArray(header_buf.data(), static_cast<int>(header_size));

    std::vector<uint8_t> encrypted = crypto.encrypt(
        payload, payload_size,
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

bool decryptVoicePacket(VoiceCrypto& crypto,
                         const uint8_t* data, uint32_t size,
                         std::vector<uint8_t>& out_plaintext) {
    uint32_t header_size = 0;
    auto header = decodeVoicePacketHeader(data, size, header_size);
    if (!header) return false;

    auto [payload_ptr, payload_size] = getVoicePayload(data, header_size, size);
    if (!payload_ptr || payload_size == 0) return false;
    if (payload_size < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE) return false;

    const uint8_t* nonce = payload_ptr;
    const uint8_t* ciphertext = payload_ptr + XCHACHA_NONCE_SIZE;
    size_t ct_len = payload_size - XCHACHA_NONCE_SIZE;

    const uint8_t* aad = data + 2;
    uint32_t aad_size = header_size - 2;

    auto decrypted = crypto.decrypt(
        ciphertext, ct_len,
        nonce, XCHACHA_NONCE_SIZE,
        aad, aad_size);
    if (!decrypted) return false;

    out_plaintext = std::move(*decrypted);
    return true;
}

void fillKey(uint8_t* key, int seed) {
    for (int i = 0; i < static_cast<int>(CRYPTO_KEY_SIZE); ++i) {
        key[i] = static_cast<uint8_t>((seed * 7 + i * 13 + 37) & 0xFF);
    }
}

struct ClientInfo {
    UserId user_id;
    ChannelId channel_id;
    boost::asio::ip::udp::endpoint endpoint;
};

class SimpleVoiceRelay {
public:
    void addClient(UserId uid, ChannelId cid,
                    const boost::asio::ip::udp::endpoint& ep) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_[uid] = {uid, cid, ep};
    }

    void updateChannel(UserId uid, ChannelId cid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(uid);
        if (it != clients_.end()) it->second.channel_id = cid;
    }

    std::vector<boost::asio::ip::udp::endpoint> getPeers(
        UserId sender_id, ChannelId channel_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<boost::asio::ip::udp::endpoint> peers;
        for (const auto& [uid, info] : clients_) {
            if (uid != sender_id && info.channel_id == channel_id) {
                peers.push_back(info.endpoint);
            }
        }
        return peers;
    }

private:
    std::unordered_map<UserId, ClientInfo> clients_;
    std::mutex mutex_;
};

}

// ============================================================

class VoiceRelayIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================
// Test 1: 包格式解析 - 2字节长度前缀
// ============================================================

TEST_F(VoiceRelayIntegrationTest, PacketFormatWithLengthPrefix) {
    uint8_t key[CRYPTO_KEY_SIZE];
    fillKey(key, 1);
    VoiceCrypto crypto;
    crypto.setSessionKey(key);

    voice::VoicePacketHeader header;
    header.set_sequence_number(42);
    header.set_sender_id(100);
    header.set_channel_id(1);
    header.set_timestamp(12345);
    header.set_tcp_tunnel(false);

    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    header.SerializeToArray(header_buf.data(), static_cast<int>(header_size));

    uint8_t opus_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto encrypted = crypto.encrypt(opus_data, sizeof(opus_data),
                                     header_buf.data(), header_buf.size());

    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::vector<uint8_t> full_packet;
    full_packet.reserve(2 + header_buf.size() + encrypted.size());
    full_packet.insert(full_packet.end(),
                       reinterpret_cast<const uint8_t*>(&header_len),
                       reinterpret_cast<const uint8_t*>(&header_len) + 2);
    full_packet.insert(full_packet.end(), header_buf.begin(), header_buf.end());
    full_packet.insert(full_packet.end(), encrypted.begin(), encrypted.end());

    uint32_t decoded_header_size = 0;
    auto decoded = decodeVoicePacketHeader(full_packet.data(),
                                            static_cast<uint32_t>(full_packet.size()),
                                            decoded_header_size);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sequence_number(), 42u);
    EXPECT_EQ(decoded->sender_id(), 100u);
    EXPECT_EQ(decoded->channel_id(), 1u);
    EXPECT_GT(decoded_header_size, 0u);
}

// ============================================================
// Test 2: AAD 匹配 - 加密/解密使用相同 AAD (data+2, header_size-2)
// ============================================================

TEST_F(VoiceRelayIntegrationTest, AADMatchDecryptSuccess) {
    uint8_t key[CRYPTO_KEY_SIZE];
    fillKey(key, 2);
    VoiceCrypto sender_crypto, receiver_crypto;
    sender_crypto.setSessionKey(key);
    receiver_crypto.setSessionKey(key);

    voice::VoicePacketHeader header;
    header.set_sequence_number(1);
    header.set_sender_id(100);
    header.set_channel_id(1);
    header.set_timestamp(12345);
    header.set_tcp_tunnel(false);

    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    header.SerializeToArray(header_buf.data(), static_cast<int>(header_size));

    uint8_t plaintext[] = {0xAA, 0xBB, 0xCC, 0xDD};
    auto encrypted = sender_crypto.encrypt(plaintext, sizeof(plaintext),
                                            header_buf.data(), header_buf.size());

    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::vector<uint8_t> full_packet;
    full_packet.reserve(2 + header_buf.size() + encrypted.size());
    full_packet.insert(full_packet.end(),
                       reinterpret_cast<const uint8_t*>(&header_len),
                       reinterpret_cast<const uint8_t*>(&header_len) + 2);
    full_packet.insert(full_packet.end(), header_buf.begin(), header_buf.end());
    full_packet.insert(full_packet.end(), encrypted.begin(), encrypted.end());

    std::vector<uint8_t> decrypted_opus;
    ASSERT_TRUE(decryptVoicePacket(receiver_crypto,
                                    full_packet.data(),
                                    static_cast<uint32_t>(full_packet.size()),
                                    decrypted_opus));
    ASSERT_EQ(decrypted_opus.size(), sizeof(plaintext));
    EXPECT_EQ(std::memcmp(decrypted_opus.data(), plaintext, sizeof(plaintext)), 0);
}

// ============================================================
// Test 3: AAD 不匹配 - 使用错误 AAD 解密必须失败
// ============================================================

TEST_F(VoiceRelayIntegrationTest, AADMismatchDecryptFails) {
    uint8_t key[CRYPTO_KEY_SIZE];
    fillKey(key, 3);
    VoiceCrypto sender_crypto, receiver_crypto;
    sender_crypto.setSessionKey(key);
    receiver_crypto.setSessionKey(key);

    voice::VoicePacketHeader header;
    header.set_sequence_number(1);
    header.set_sender_id(100);
    header.set_channel_id(1);
    header.set_timestamp(12345);

    const size_t header_size = header.ByteSizeLong();
    std::vector<uint8_t> header_buf(header_size);
    header.SerializeToArray(header_buf.data(), static_cast<int>(header_size));

    uint8_t plaintext[] = {0xAA, 0xBB};
    auto encrypted = sender_crypto.encrypt(plaintext, sizeof(plaintext),
                                            header_buf.data(), header_buf.size());

    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::vector<uint8_t> full_packet;
    full_packet.reserve(2 + header_buf.size() + encrypted.size());
    full_packet.insert(full_packet.end(),
                       reinterpret_cast<const uint8_t*>(&header_len),
                       reinterpret_cast<const uint8_t*>(&header_len) + 2);
    full_packet.insert(full_packet.end(), header_buf.begin(), header_buf.end());
    full_packet.insert(full_packet.end(), encrypted.begin(), encrypted.end());

    uint32_t header_size_decoded = 0;
    auto decoded = decodeVoicePacketHeader(full_packet.data(),
                                            static_cast<uint32_t>(full_packet.size()),
                                            header_size_decoded);
    ASSERT_TRUE(decoded.has_value());

    auto [payload_ptr, payload_size] = getVoicePayload(
        full_packet.data(), header_size_decoded,
        static_cast<uint32_t>(full_packet.size()));
    ASSERT_NE(payload_ptr, nullptr);

    const uint8_t* nonce = payload_ptr;
    const uint8_t* ciphertext = payload_ptr + XCHACHA_NONCE_SIZE;
    size_t ct_len = payload_size - XCHACHA_NONCE_SIZE;

    auto decrypted = receiver_crypto.decrypt(
        ciphertext, ct_len,
        nonce, XCHACHA_NONCE_SIZE,
        full_packet.data(), header_size_decoded);

    EXPECT_FALSE(decrypted.has_value())
        << "Decrypt with wrong AAD (includes 2-byte prefix) must fail";
}

// ============================================================
// Test 4: Opus 编解码 + 加密/解密端到端
// ============================================================

TEST_F(VoiceRelayIntegrationTest, OpusEncodeEncryptDecryptDecode) {
    auto sine = generateSineWave(440.0f, 0.02f);

    OpusEncoder encoder(48000, 1, OPUS_APPLICATION_VOIP);
    OpusDecoder decoder(48000, 1);

    auto opus_frame = encodeOpusFrame(encoder, sine);
    ASSERT_FALSE(opus_frame.empty());

    uint8_t key[CRYPTO_KEY_SIZE];
    fillKey(key, 4);
    VoiceCrypto sender_crypto, receiver_crypto;
    sender_crypto.setSessionKey(key);
    receiver_crypto.setSessionKey(key);

    auto full_packet = buildVoicePacket(sender_crypto, UserId(100), ChannelId(1),
                                         1, opus_frame.data(),
                                         static_cast<uint32_t>(opus_frame.size()));
    ASSERT_FALSE(full_packet.empty());

    std::vector<uint8_t> decrypted_opus;
    ASSERT_TRUE(decryptVoicePacket(receiver_crypto,
                                    full_packet.data(),
                                    static_cast<uint32_t>(full_packet.size()),
                                    decrypted_opus));
    EXPECT_EQ(decrypted_opus.size(), opus_frame.size());
    EXPECT_EQ(std::memcmp(decrypted_opus.data(), opus_frame.data(),
                           opus_frame.size()), 0);

    auto decoded_pcm = decodeOpusFrame(decoder, decrypted_opus.data(),
                                        decrypted_opus.size());
    ASSERT_FALSE(decoded_pcm.empty());
    EXPECT_GE(decoded_pcm.size(), 400u);
}

// ============================================================
// Test 5: TCP 语音帧类型识别
// ============================================================

TEST_F(VoiceRelayIntegrationTest, TcpVoiceFrameTypeIdentification) {
    EXPECT_EQ(TCP_VOICE_FRAME_TYPE, 0xFF);

    std::vector<uint8_t> voice_data = {0x01, 0x02, 0x03, 0x04};
    TcpVoiceTunnel tunnel;
    auto frame = tunnel.sendVoiceFrame(voice_data.data(),
                                        static_cast<uint32_t>(voice_data.size()));
    ASSERT_GE(frame.size(), TCP_VOICE_FRAME_HEADER_SIZE);
    EXPECT_EQ(frame[4], TCP_VOICE_FRAME_TYPE);
}

// ============================================================
// Test 6: 双客户端同频道语音中继
// ============================================================

TEST_F(VoiceRelayIntegrationTest, TwoClientUdpRelay) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<UdpSocket>(io_ctx);
    auto bind_result = relay_udp->bind(relay_port);
    ASSERT_FALSE(bind_result) << "Relay UDP bind failed: " << bind_result.message();

    SimpleVoiceRelay relay;
    uint8_t shared_key[CRYPTO_KEY_SIZE];
    fillKey(shared_key, 10);

    UserId sender_id(1001), receiver_id(1002);
    ChannelId channel_id(1);

    auto sender_udp = std::make_shared<UdpSocket>(io_ctx);
    auto receiver_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(sender_udp->bind(0));
    ASSERT_FALSE(receiver_udp->bind(0));

    auto sender_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), sender_udp->localPort());
    auto receiver_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), receiver_udp->localPort());

    relay.addClient(sender_id, channel_id, sender_ep);
    relay.addClient(receiver_id, channel_id, receiver_ep);

    std::atomic<int> packets_received{0};
    std::vector<std::vector<uint8_t>> received_data;
    std::mutex recv_mutex;

    receiver_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                                 const boost::asio::ip::udp::endpoint&) {
        if (size > 0) {
            std::lock_guard<std::mutex> lock(recv_mutex);
            received_data.emplace_back(data, data + size);
            packets_received.fetch_add(1);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [receiver_udp]() -> boost::asio::awaitable<void> {
            co_await receiver_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    relay_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                               const boost::asio::ip::udp::endpoint& from) {
        uint32_t header_size = 0;
        auto header = decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;

        UserId sender(header->sender_id());
        ChannelId channel(header->channel_id());
        auto peers = relay.getPeers(sender, channel);

        for (const auto& peer_ep : peers) {
            auto pkt = std::make_shared<std::vector<uint8_t>>(data, data + size);
            boost::asio::co_spawn(io_ctx,
                [relay_udp, pkt, peer_ep]() -> boost::asio::awaitable<void> {
                    co_await relay_udp->asyncSendTo(
                        pkt->data(), static_cast<uint32_t>(pkt->size()), peer_ep);
                }, boost::asio::detached);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [relay_udp]() -> boost::asio::awaitable<void> {
            co_await relay_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto relay_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), relay_port);

    VoiceCrypto sender_crypto;
    sender_crypto.setSessionKey(shared_key);

    const int num_packets = 5;
    for (int i = 0; i < num_packets; ++i) {
        uint8_t opus_payload[] = {0x10, 0x20, 0x30, 0x40,
                                  static_cast<uint8_t>(i & 0xFF)};
        auto packet = buildVoicePacket(sender_crypto, sender_id, channel_id,
                                        static_cast<uint64_t>(i),
                                        opus_payload, sizeof(opus_payload));

        boost::asio::co_spawn(io_ctx,
            [sender_udp, packet, relay_ep]() -> boost::asio::awaitable<void> {
                co_await sender_udp->asyncSendTo(
                    packet.data(), static_cast<uint32_t>(packet.size()), relay_ep);
            }, boost::asio::detached);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    int max_wait_ms = 3000;
    int waited = 0;
    while (packets_received.load() < 1 && waited < max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    EXPECT_GE(packets_received.load(), 1)
        << "Receiver should get at least 1 relayed voice packet";

    if (packets_received.load() > 0) {
        VoiceCrypto receiver_crypto;
        receiver_crypto.setSessionKey(shared_key);

        std::lock_guard<std::mutex> lock(recv_mutex);
        int decrypt_success = 0;
        for (const auto& pkt : received_data) {
            std::vector<uint8_t> plaintext;
            if (decryptVoicePacket(receiver_crypto,
                                    pkt.data(),
                                    static_cast<uint32_t>(pkt.size()),
                                    plaintext)) {
                decrypt_success++;
                EXPECT_GE(plaintext.size(), 4u);
            }
        }
        EXPECT_GT(decrypt_success, 0)
            << "At least one relayed packet should decrypt successfully";
    }

    sender_udp->close();
    receiver_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();
}

// ============================================================
// Test 7: 三客户端同频道语音中继
// ============================================================

TEST_F(VoiceRelayIntegrationTest, ThreeClientUdpRelay) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[CRYPTO_KEY_SIZE];
    fillKey(shared_key, 20);

    UserId user_a(2001), user_b(2002), user_c(2003);
    ChannelId channel_id(1);

    auto udp_a = std::make_shared<UdpSocket>(io_ctx);
    auto udp_b = std::make_shared<UdpSocket>(io_ctx);
    auto udp_c = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(udp_a->bind(0));
    ASSERT_FALSE(udp_b->bind(0));
    ASSERT_FALSE(udp_c->bind(0));

    auto ep_a = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_a->localPort());
    auto ep_b = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_b->localPort());
    auto ep_c = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_c->localPort());

    relay.addClient(user_a, channel_id, ep_a);
    relay.addClient(user_b, channel_id, ep_b);
    relay.addClient(user_c, channel_id, ep_c);

    std::atomic<int> b_received{0}, c_received{0};
    udp_b->onPacket = [&](const uint8_t*, uint32_t, const auto&) {
        b_received.fetch_add(1);
    };
    udp_c->onPacket = [&](const uint8_t*, uint32_t, const auto&) {
        c_received.fetch_add(1);
    };

    boost::asio::co_spawn(io_ctx,
        [udp_b]() -> boost::asio::awaitable<void> {
            co_await udp_b->asyncReceiveFrom();
        }, boost::asio::detached);
    boost::asio::co_spawn(io_ctx,
        [udp_c]() -> boost::asio::awaitable<void> {
            co_await udp_c->asyncReceiveFrom();
        }, boost::asio::detached);

    relay_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                               const boost::asio::ip::udp::endpoint&) {
        uint32_t header_size = 0;
        auto header = decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        UserId sender(header->sender_id());
        ChannelId channel(header->channel_id());
        auto peers = relay.getPeers(sender, channel);
        for (const auto& peer_ep : peers) {
            auto pkt = std::make_shared<std::vector<uint8_t>>(data, data + size);
            boost::asio::co_spawn(io_ctx,
                [relay_udp, pkt, peer_ep]() -> boost::asio::awaitable<void> {
                    co_await relay_udp->asyncSendTo(
                        pkt->data(), static_cast<uint32_t>(pkt->size()), peer_ep);
                }, boost::asio::detached);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [relay_udp]() -> boost::asio::awaitable<void> {
            co_await relay_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto relay_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), relay_port);

    VoiceCrypto crypto_a;
    crypto_a.setSessionKey(shared_key);

    for (int i = 0; i < 3; ++i) {
        uint8_t payload[] = {0xAA, static_cast<uint8_t>(i)};
        auto packet = buildVoicePacket(crypto_a, user_a, channel_id,
                                        static_cast<uint64_t>(i),
                                        payload, sizeof(payload));

        boost::asio::co_spawn(io_ctx,
            [udp_a, packet, relay_ep]() -> boost::asio::awaitable<void> {
                co_await udp_a->asyncSendTo(
                    packet.data(), static_cast<uint32_t>(packet.size()), relay_ep);
            }, boost::asio::detached);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    int max_wait = 3000;
    int waited = 0;
    while ((b_received.load() < 1 || c_received.load() < 1) && waited < max_wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    EXPECT_GE(b_received.load(), 1) << "Client B should receive relayed packets from A";
    EXPECT_GE(c_received.load(), 1) << "Client C should receive relayed packets from A";

    udp_a->close();
    udp_b->close();
    udp_c->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();
}

// ============================================================
// Test 8: 频道隔离 - 不同频道用户不互通
// ============================================================

TEST_F(VoiceRelayIntegrationTest, ChannelIsolation) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[CRYPTO_KEY_SIZE];
    fillKey(shared_key, 30);

    UserId user_a(3001), user_b(3002);
    ChannelId lobby_channel(1), other_channel(2);

    auto udp_a = std::make_shared<UdpSocket>(io_ctx);
    auto udp_b = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(udp_a->bind(0));
    ASSERT_FALSE(udp_b->bind(0));

    auto ep_a = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_a->localPort());
    auto ep_b = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_b->localPort());

    relay.addClient(user_a, lobby_channel, ep_a);
    relay.addClient(user_b, other_channel, ep_b);

    std::atomic<int> b_received{0};
    udp_b->onPacket = [&](const uint8_t*, uint32_t, const auto&) {
        b_received.fetch_add(1);
    };

    boost::asio::co_spawn(io_ctx,
        [udp_b]() -> boost::asio::awaitable<void> {
            co_await udp_b->asyncReceiveFrom();
        }, boost::asio::detached);

    relay_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                               const boost::asio::ip::udp::endpoint&) {
        uint32_t header_size = 0;
        auto header = decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        UserId sender(header->sender_id());
        ChannelId channel(header->channel_id());
        auto peers = relay.getPeers(sender, channel);
        for (const auto& peer_ep : peers) {
            auto pkt = std::make_shared<std::vector<uint8_t>>(data, data + size);
            boost::asio::co_spawn(io_ctx,
                [relay_udp, pkt, peer_ep]() -> boost::asio::awaitable<void> {
                    co_await relay_udp->asyncSendTo(
                        pkt->data(), static_cast<uint32_t>(pkt->size()), peer_ep);
                }, boost::asio::detached);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [relay_udp]() -> boost::asio::awaitable<void> {
            co_await relay_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto relay_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), relay_port);

    VoiceCrypto crypto_a;
    crypto_a.setSessionKey(shared_key);

    for (int i = 0; i < 3; ++i) {
        uint8_t payload[] = {0xFF};
        auto packet = buildVoicePacket(crypto_a, user_a, lobby_channel,
                                        static_cast<uint64_t>(i),
                                        payload, sizeof(payload));

        boost::asio::co_spawn(io_ctx,
            [udp_a, packet, relay_ep]() -> boost::asio::awaitable<void> {
                co_await udp_a->asyncSendTo(
                    packet.data(), static_cast<uint32_t>(packet.size()), relay_ep);
            }, boost::asio::detached);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(b_received.load(), 0)
        << "User B in different channel should NOT receive A's voice packets";

    udp_a->close();
    udp_b->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();
}

// ============================================================
// Test 9: TCP 隧道语音包中继（模拟 TCP → UDP 转发）
// ============================================================

TEST_F(VoiceRelayIntegrationTest, TcpTunnelVoiceRelay) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[CRYPTO_KEY_SIZE];
    fillKey(shared_key, 40);

    UserId sender_id(4001), receiver_id(4002);
    ChannelId channel_id(1);

    auto receiver_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(receiver_udp->bind(0));

    auto receiver_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), receiver_udp->localPort());

    relay.addClient(sender_id, channel_id, boost::asio::ip::udp::endpoint());
    relay.addClient(receiver_id, channel_id, receiver_ep);

    std::atomic<int> packets_received{0};
    receiver_udp->onPacket = [&](const uint8_t*, uint32_t, const auto&) {
        packets_received.fetch_add(1);
    };

    boost::asio::co_spawn(io_ctx,
        [receiver_udp]() -> boost::asio::awaitable<void> {
            co_await receiver_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    VoiceCrypto sender_crypto;
    sender_crypto.setSessionKey(shared_key);

    uint8_t opus_data[] = {0x50, 0x60, 0x70, 0x80};
    auto packet = buildVoicePacket(sender_crypto, sender_id, channel_id,
                                    1, opus_data, sizeof(opus_data));

    auto peers = relay.getPeers(sender_id, channel_id);
    for (const auto& peer_ep : peers) {
        auto pkt = std::make_shared<std::vector<uint8_t>>(packet);
        boost::asio::co_spawn(io_ctx,
            [relay_udp, pkt, peer_ep]() -> boost::asio::awaitable<void> {
                co_await relay_udp->asyncSendTo(
                    pkt->data(), static_cast<uint32_t>(pkt->size()), peer_ep);
            }, boost::asio::detached);
    }

    int max_wait = 2000;
    int waited = 0;
    while (packets_received.load() < 1 && waited < max_wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    EXPECT_GE(packets_received.load(), 1)
        << "Receiver should get voice packet relayed via TCP tunnel path";

    receiver_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();
}

// ============================================================
// Test 10: 完整语音链路 - Opus → 加密 → 中继 → 解密 → Opus 解码
// ============================================================

TEST_F(VoiceRelayIntegrationTest, FullVoicePipelineEndToEnd) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[CRYPTO_KEY_SIZE];
    fillKey(shared_key, 50);

    UserId sender_id(5001), receiver_id(5002);
    ChannelId channel_id(1);

    auto sender_udp = std::make_shared<UdpSocket>(io_ctx);
    auto receiver_udp = std::make_shared<UdpSocket>(io_ctx);
    ASSERT_FALSE(sender_udp->bind(0));
    ASSERT_FALSE(receiver_udp->bind(0));

    auto sender_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), sender_udp->localPort());
    auto receiver_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), receiver_udp->localPort());

    relay.addClient(sender_id, channel_id, sender_ep);
    relay.addClient(receiver_id, channel_id, receiver_ep);

    std::atomic<int> packets_received{0};
    std::vector<std::vector<uint8_t>> received_data;
    std::mutex recv_mutex;

    receiver_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                                 const boost::asio::ip::udp::endpoint&) {
        if (size > 0) {
            std::lock_guard<std::mutex> lock(recv_mutex);
            received_data.emplace_back(data, data + size);
            packets_received.fetch_add(1);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [receiver_udp]() -> boost::asio::awaitable<void> {
            co_await receiver_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    relay_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                               const boost::asio::ip::udp::endpoint&) {
        uint32_t header_size = 0;
        auto header = decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        UserId sender(header->sender_id());
        ChannelId channel(header->channel_id());
        auto peers = relay.getPeers(sender, channel);
        for (const auto& peer_ep : peers) {
            auto pkt = std::make_shared<std::vector<uint8_t>>(data, data + size);
            boost::asio::co_spawn(io_ctx,
                [relay_udp, pkt, peer_ep]() -> boost::asio::awaitable<void> {
                    co_await relay_udp->asyncSendTo(
                        pkt->data(), static_cast<uint32_t>(pkt->size()), peer_ep);
                }, boost::asio::detached);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [relay_udp]() -> boost::asio::awaitable<void> {
            co_await relay_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto relay_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), relay_port);

    OpusEncoder encoder(48000, 1, OPUS_APPLICATION_VOIP);
    OpusDecoder decoder(48000, 1);
    VoiceCrypto sender_crypto;
    sender_crypto.setSessionKey(shared_key);

    auto sine = generateSineWave(440.0f, 0.02f);
    auto opus_frame = encodeOpusFrame(encoder, sine);
    ASSERT_FALSE(opus_frame.empty());

    auto packet = buildVoicePacket(sender_crypto, sender_id, channel_id,
                                    1, opus_frame.data(),
                                    static_cast<uint32_t>(opus_frame.size()));

    boost::asio::co_spawn(io_ctx,
        [sender_udp, packet, relay_ep]() -> boost::asio::awaitable<void> {
            co_await sender_udp->asyncSendTo(
                packet.data(), static_cast<uint32_t>(packet.size()), relay_ep);
        }, boost::asio::detached);

    int max_wait = 3000;
    int waited = 0;
    while (packets_received.load() < 1 && waited < max_wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    ASSERT_GE(packets_received.load(), 1) << "Should receive relayed voice packet";

    VoiceCrypto receiver_crypto;
    receiver_crypto.setSessionKey(shared_key);

    std::lock_guard<std::mutex> lock(recv_mutex);
    bool decode_ok = false;
    for (const auto& pkt : received_data) {
        std::vector<uint8_t> decrypted;
        if (decryptVoicePacket(receiver_crypto,
                                pkt.data(),
                                static_cast<uint32_t>(pkt.size()),
                                decrypted)) {
            auto decoded_pcm = decodeOpusFrame(decoder,
                                                decrypted.data(),
                                                decrypted.size());
            if (!decoded_pcm.empty()) {
                decode_ok = true;
                EXPECT_GE(decoded_pcm.size(), 400u);
                float max_amp = 0.0f;
                for (float s : decoded_pcm) {
                    max_amp = std::max(max_amp, std::abs(s));
                }
                EXPECT_GT(max_amp, 0.01f)
                    << "Decoded audio should have non-trivial amplitude";
                break;
            }
        }
    }
    EXPECT_TRUE(decode_ok)
        << "Full pipeline: encode -> encrypt -> relay -> decrypt -> decode should succeed";

    sender_udp->close();
    receiver_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();
}

} // namespace nevo
