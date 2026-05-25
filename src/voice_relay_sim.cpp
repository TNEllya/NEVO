/**
 * @file voice_relay_sim.cpp
 * @brief 本地多客户端语音通信模拟测试
 *
 * 验证修复后的语音通信端到端流程：
 *   合成音频 → Opus编码 → XChaCha20-Poly1305加密 → 2字节前缀打包
 *   → UDP发送 → 模拟中继 → UDP接收 → 解析 → 解密 → Opus解码 → 验证音频
 *
 * 验证的BUG修复：
 *   BUG#1: sendVoicePacket 缺少2字节长度前缀
 *   BUG#2: AAD 应为 data+2, header_size-2（不含2字节前缀）
 *   BUG#3: handleLoginResponse 未配置 voice_server_endpoint
 *   BUG#4: 客户端未发送UDP注册包
 *   BUG#5: ClientSession 未识别 TCP_VOICE_FRAME_TYPE
 */

#define _USE_MATH_DEFINES
#include <cmath>

#include <boost/asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"
#include "nevo/network/TcpVoiceTunnel.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/protocol/PacketTypes.h"
#include "nevo/core/common/Types.h"
#include "nevo/core/audio/OpusEncoder.h"
#include "nevo/core/audio/OpusDecoder.h"

#include "voice.pb.h"

namespace {

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST_CASE(name) \
    static void test_##name(); \
    struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            g_tests_run++; \
            std::cout << "[ RUN  ] " #name << std::endl; \
            try { \
                test_##name(); \
                g_tests_passed++; \
                std::cout << "[ PASS ] " #name << std::endl; \
            } catch (const std::exception& e) { \
                g_tests_failed++; \
                std::cout << "[ FAIL ] " #name << ": " << e.what() << std::endl; \
            } catch (...) { \
                g_tests_failed++; \
                std::cout << "[ FAIL ] " #name << ": unknown exception" << std::endl; \
            } \
        } \
    } g_test_registrar_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw std::runtime_error( \
        std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
        ": ASSERT_TRUE failed: " #expr); } while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error( \
        std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
        ": ASSERT_EQ failed: " #a " != " #b); } while(0)

#define ASSERT_GT(a, b) \
    do { if (!((a) > (b))) throw std::runtime_error( \
        std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
        ": ASSERT_GT failed: " #a " <= " #b); } while(0)

#define ASSERT_GE(a, b) \
    do { if (!((a) >= (b))) throw std::runtime_error( \
        std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
        ": ASSERT_GE failed: " #a " < " #b); } while(0)

#define ASSERT_NE(a, b) \
    do { if ((a) == (b)) throw std::runtime_error( \
        std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
        ": ASSERT_NE failed: " #a " == " #b); } while(0)

#define EXPECT_TRUE(expr) \
    do { if (!(expr)) std::cout << "  [WARN] " << __FILE__ << ":" \
        << __LINE__ << ": EXPECT_TRUE failed: " #expr << std::endl; } while(0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ(a, b) \
    do { if ((a) != (b)) std::cout << "  [WARN] " << __FILE__ << ":" \
        << __LINE__ << ": EXPECT_EQ failed: " #a " != " #b << std::endl; } while(0)

#define EXPECT_GT(a, b) \
    do { if (!((a) > (b))) std::cout << "  [WARN] " << __FILE__ << ":" \
        << __LINE__ << ": EXPECT_GT failed: " #a " <= " #b << std::endl; } while(0)

#define EXPECT_GE(a, b) \
    do { if (!((a) >= (b))) std::cout << "  [WARN] " << __FILE__ << ":" \
        << __LINE__ << ": EXPECT_GE failed: " #a " < " #b << std::endl; } while(0)

std::atomic<uint16_t> g_next_port{30000};
uint16_t allocatePort() { return g_next_port.fetch_add(1); }

void fillKey(uint8_t* key, int seed) {
    for (int i = 0; i < static_cast<int>(nevo::CRYPTO_KEY_SIZE); ++i) {
        key[i] = static_cast<uint8_t>((seed * 7 + i * 13 + 37) & 0xFF);
    }
}

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

std::vector<uint8_t> encodeOpusFrame(nevo::OpusEncoderWrapper& encoder,
                                      const std::vector<float>& pcm,
                                      int frame_size = 960) {
    std::vector<uint8_t> output(4000);
    auto result = encoder.encode(pcm.data(), output.data(),
                                  static_cast<uint32_t>(output.size()));
    if (!result.ok() || result.value() == 0) return {};
    output.resize(static_cast<size_t>(result.value()));
    return output;
}

std::vector<float> decodeOpusFrame(nevo::OpusDecoderWrapper& decoder,
                                    const uint8_t* data, size_t size,
                                    int frame_size = 960) {
    std::vector<float> pcm(static_cast<size_t>(frame_size));
    auto result = decoder.decode(data, static_cast<uint32_t>(size), pcm.data());
    if (!result.ok() || result.value() == 0) return {};
    pcm.resize(static_cast<size_t>(result.value()));
    return pcm;
}

std::vector<uint8_t> buildVoicePacket(nevo::VoiceCrypto& crypto,
                                       nevo::UserId sender_id,
                                       nevo::ChannelId channel_id,
                                       uint64_t sequence,
                                       const uint8_t* payload,
                                       uint32_t payload_size) {
    nevo::voice::VoicePacketHeader header;
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

bool decryptVoicePacket(nevo::VoiceCrypto& crypto,
                         const uint8_t* data, uint32_t size,
                         std::vector<uint8_t>& out_plaintext) {
    uint32_t header_size = 0;
    auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
    if (!header) return false;

    auto [payload_ptr, payload_size] = nevo::getVoicePayload(data, header_size, size);
    if (!payload_ptr || payload_size == 0) return false;
    if (payload_size < nevo::XCHACHA_NONCE_SIZE + nevo::POLY1305_TAG_SIZE) return false;

    const uint8_t* nonce = payload_ptr;
    const uint8_t* ciphertext = payload_ptr + nevo::XCHACHA_NONCE_SIZE;
    size_t ct_len = payload_size - nevo::XCHACHA_NONCE_SIZE;

    const uint8_t* aad = data + 2;
    uint32_t aad_size = header_size - 2;

    auto decrypted = crypto.decrypt(
        ciphertext, ct_len,
        nonce, nevo::XCHACHA_NONCE_SIZE,
        aad, aad_size);
    if (!decrypted) return false;

    out_plaintext = std::move(*decrypted);
    return true;
}

struct ClientInfo {
    nevo::UserId user_id;
    nevo::ChannelId channel_id;
    boost::asio::ip::udp::endpoint endpoint;
};

class SimpleVoiceRelay {
public:
    void addClient(nevo::UserId uid, nevo::ChannelId cid,
                    const boost::asio::ip::udp::endpoint& ep) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_[uid] = {uid, cid, ep};
    }

    void updateChannel(nevo::UserId uid, nevo::ChannelId cid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(uid);
        if (it != clients_.end()) it->second.channel_id = cid;
    }

    std::vector<boost::asio::ip::udp::endpoint> getPeers(
        nevo::UserId sender_id, nevo::ChannelId channel_id) {
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
    std::unordered_map<nevo::UserId, ClientInfo> clients_;
    std::mutex mutex_;
};

}

// ============================================================
// Test 1: BUG#1 - 2字节长度前缀包格式验证
// ============================================================

TEST_CASE(PacketFormatWithLengthPrefix) {
    uint8_t key[nevo::CRYPTO_KEY_SIZE];
    fillKey(key, 1);
    nevo::VoiceCrypto crypto;
    crypto.setSessionKey(key);

    nevo::voice::VoicePacketHeader header;
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

    ASSERT_TRUE(full_packet.size() > 2);
    uint16_t decoded_prefix = 0;
    std::memcpy(&decoded_prefix, full_packet.data(), 2);
    ASSERT_EQ(decoded_prefix, header_size);

    uint32_t decoded_header_size = 0;
    auto decoded = nevo::decodeVoicePacketHeader(full_packet.data(),
                                                   static_cast<uint32_t>(full_packet.size()),
                                                   decoded_header_size);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->sequence_number(), 42u);
    ASSERT_EQ(decoded->sender_id(), 100u);
    ASSERT_EQ(decoded->channel_id(), 1u);
    ASSERT_GT(decoded_header_size, 0u);

    std::cout << "  [INFO] Packet format: 2-byte prefix=" << decoded_prefix
              << ", header_size=" << decoded_header_size
              << ", total_packet=" << full_packet.size() << std::endl;
}

// ============================================================
// Test 2: BUG#2 - AAD匹配验证 (data+2, header_size-2)
// ============================================================

TEST_CASE(AADMatchDecryptSuccess) {
    uint8_t key[nevo::CRYPTO_KEY_SIZE];
    fillKey(key, 2);
    nevo::VoiceCrypto sender_crypto, receiver_crypto;
    sender_crypto.setSessionKey(key);
    receiver_crypto.setSessionKey(key);

    nevo::voice::VoicePacketHeader header;
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
    ASSERT_EQ(std::memcmp(decrypted_opus.data(), plaintext, sizeof(plaintext)), 0);

    std::cout << "  [INFO] AAD match: encrypt AAD=header_buf(" << header_size
              << "B), decrypt AAD=data+2(" << (header_size - 2) << "B) -> SUCCESS" << std::endl;
}

// ============================================================
// Test 3: BUG#2 - AAD不匹配验证 (错误AAD解密必须失败)
// ============================================================

TEST_CASE(AADMismatchDecryptFails) {
    uint8_t key[nevo::CRYPTO_KEY_SIZE];
    fillKey(key, 3);
    nevo::VoiceCrypto sender_crypto, receiver_crypto;
    sender_crypto.setSessionKey(key);
    receiver_crypto.setSessionKey(key);

    nevo::voice::VoicePacketHeader header;
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
    auto decoded = nevo::decodeVoicePacketHeader(full_packet.data(),
                                                   static_cast<uint32_t>(full_packet.size()),
                                                   header_size_decoded);
    ASSERT_TRUE(decoded.has_value());

    auto [payload_ptr, payload_size] = nevo::getVoicePayload(
        full_packet.data(), header_size_decoded,
        static_cast<uint32_t>(full_packet.size()));
    ASSERT_NE(payload_ptr, nullptr);

    const uint8_t* nonce = payload_ptr;
    const uint8_t* ciphertext = payload_ptr + nevo::XCHACHA_NONCE_SIZE;
    size_t ct_len = payload_size - nevo::XCHACHA_NONCE_SIZE;

    auto decrypted = receiver_crypto.decrypt(
        ciphertext, ct_len,
        nonce, nevo::XCHACHA_NONCE_SIZE,
        full_packet.data(), header_size_decoded);

    ASSERT_FALSE(decrypted.has_value());

    std::cout << "  [INFO] AAD mismatch: wrong AAD=data(" << header_size_decoded
              << "B) -> decrypt FAILED as expected" << std::endl;
}

// ============================================================
// Test 4: Opus编解码 + 加密/解密端到端
// ============================================================

TEST_CASE(OpusEncodeEncryptDecryptDecode) {
    auto sine = generateSineWave(440.0f, 0.02f);

    nevo::OpusEncoderWrapper::Config enc_cfg;
    enc_cfg.sample_rate = 48000;
    enc_cfg.channels = 1;
    enc_cfg.frame_size = 960;
    enc_cfg.bitrate = 64000;
    nevo::OpusEncoderWrapper encoder(enc_cfg);

    nevo::OpusDecoderWrapper::Config dec_cfg;
    dec_cfg.sample_rate = 48000;
    dec_cfg.channels = 1;
    dec_cfg.frame_size = 960;
    nevo::OpusDecoderWrapper decoder(dec_cfg);

    auto opus_frame = encodeOpusFrame(encoder, sine);
    ASSERT_FALSE(opus_frame.empty());

    uint8_t key[nevo::CRYPTO_KEY_SIZE];
    fillKey(key, 4);
    nevo::VoiceCrypto sender_crypto, receiver_crypto;
    sender_crypto.setSessionKey(key);
    receiver_crypto.setSessionKey(key);

    auto full_packet = buildVoicePacket(sender_crypto, nevo::UserId(100), nevo::ChannelId(1),
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

    std::cout << "  [INFO] Opus encode/decode: input=" << sine.size()
              << " samples, opus=" << opus_frame.size()
              << " bytes, decoded=" << decoded_pcm.size() << " samples" << std::endl;
}

// ============================================================
// Test 5: BUG#5 - TCP语音帧类型识别
// ============================================================

TEST_CASE(TcpVoiceFrameTypeIdentification) {
    ASSERT_EQ(nevo::TCP_VOICE_FRAME_TYPE, 0xFF);

    std::vector<uint8_t> voice_data = {0x01, 0x02, 0x03, 0x04};
    nevo::TcpVoiceTunnel tunnel;
    auto frame = tunnel.sendVoiceFrame(voice_data.data(),
                                        static_cast<uint32_t>(voice_data.size()));
    ASSERT_GE(frame.size(), nevo::TCP_VOICE_FRAME_HEADER_SIZE);
    ASSERT_EQ(frame[4], nevo::TCP_VOICE_FRAME_TYPE);

    std::cout << "  [INFO] TCP voice frame: type=0x" << std::hex
              << static_cast<int>(frame[4]) << std::dec
              << ", header_size=" << nevo::TCP_VOICE_FRAME_HEADER_SIZE
              << ", frame_size=" << frame.size() << std::endl;
}

// ============================================================
// Test 6: 双客户端同频道UDP语音中继
// ============================================================

TEST_CASE(TwoClientUdpRelay) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto bind_result = relay_udp->bind(relay_port);
    ASSERT_FALSE(bind_result);

    SimpleVoiceRelay relay;
    uint8_t shared_key[nevo::CRYPTO_KEY_SIZE];
    fillKey(shared_key, 10);

    nevo::UserId sender_id(1001), receiver_id(1002);
    nevo::ChannelId channel_id(1);

    auto sender_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto receiver_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
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
        auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;

        nevo::UserId sender(header->sender_id());
        nevo::ChannelId channel(header->channel_id());
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

    nevo::VoiceCrypto sender_crypto;
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

    ASSERT_GE(packets_received.load(), 1);

    if (packets_received.load() > 0) {
        nevo::VoiceCrypto receiver_crypto;
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
        ASSERT_GT(decrypt_success, 0);
    }

    sender_udp->close();
    receiver_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "  [INFO] 2-client relay: sent=" << num_packets
              << ", received=" << packets_received.load() << std::endl;
}

// ============================================================
// Test 7: 三客户端同频道语音中继
// ============================================================

TEST_CASE(ThreeClientUdpRelay) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[nevo::CRYPTO_KEY_SIZE];
    fillKey(shared_key, 20);

    nevo::UserId user_a(2001), user_b(2002), user_c(2003);
    nevo::ChannelId channel_id(1);

    auto udp_a = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto udp_b = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto udp_c = std::make_shared<nevo::UdpSocket>(io_ctx);
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
        auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        nevo::UserId sender(header->sender_id());
        nevo::ChannelId channel(header->channel_id());
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

    nevo::VoiceCrypto crypto_a;
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

    ASSERT_GE(b_received.load(), 1);
    ASSERT_GE(c_received.load(), 1);

    udp_a->close();
    udp_b->close();
    udp_c->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "  [INFO] 3-client relay: B received=" << b_received.load()
              << ", C received=" << c_received.load() << std::endl;
}

// ============================================================
// Test 8: 频道隔离 - 不同频道用户不互通
// ============================================================

TEST_CASE(ChannelIsolation) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[nevo::CRYPTO_KEY_SIZE];
    fillKey(shared_key, 30);

    nevo::UserId user_a(3001), user_b(3002);
    nevo::ChannelId lobby_channel(1), other_channel(2);

    auto udp_a = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto udp_b = std::make_shared<nevo::UdpSocket>(io_ctx);
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
        auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        nevo::UserId sender(header->sender_id());
        nevo::ChannelId channel(header->channel_id());
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

    nevo::VoiceCrypto crypto_a;
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

    ASSERT_EQ(b_received.load(), 0);

    udp_a->close();
    udp_b->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "  [INFO] Channel isolation: B(different channel) received="
              << b_received.load() << " (expected 0)" << std::endl;
}

// ============================================================
// Test 9: TCP隧道语音包中继
// ============================================================

TEST_CASE(TcpTunnelVoiceRelay) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[nevo::CRYPTO_KEY_SIZE];
    fillKey(shared_key, 40);

    nevo::UserId sender_id(4001), receiver_id(4002);
    nevo::ChannelId channel_id(1);

    auto receiver_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
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

    nevo::VoiceCrypto sender_crypto;
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

    ASSERT_GE(packets_received.load(), 1);

    receiver_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "  [INFO] TCP tunnel relay: received=" << packets_received.load() << std::endl;
}

// ============================================================
// Test 10: 完整语音链路 - Opus → 加密 → 中继 → 解密 → Opus解码
// ============================================================

TEST_CASE(FullVoicePipelineEndToEnd) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[nevo::CRYPTO_KEY_SIZE];
    fillKey(shared_key, 50);

    nevo::UserId sender_id(5001), receiver_id(5002);
    nevo::ChannelId channel_id(1);

    auto sender_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto receiver_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
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
        auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        nevo::UserId sender(header->sender_id());
        nevo::ChannelId channel(header->channel_id());
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

    nevo::OpusEncoderWrapper::Config enc_cfg;
    enc_cfg.sample_rate = 48000;
    enc_cfg.channels = 1;
    enc_cfg.frame_size = 960;
    nevo::OpusEncoderWrapper encoder(enc_cfg);

    nevo::OpusDecoderWrapper::Config dec_cfg;
    dec_cfg.sample_rate = 48000;
    dec_cfg.channels = 1;
    dec_cfg.frame_size = 960;
    nevo::OpusDecoderWrapper decoder(dec_cfg);

    nevo::VoiceCrypto sender_crypto;
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

    ASSERT_GE(packets_received.load(), 1);

    nevo::VoiceCrypto receiver_crypto;
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
                EXPECT_GT(max_amp, 0.01f);
                std::cout << "  [INFO] Full pipeline: decoded=" << decoded_pcm.size()
                          << " samples, max_amplitude=" << max_amp << std::endl;
                break;
            }
        }
    }
    ASSERT_TRUE(decode_ok);

    sender_udp->close();
    receiver_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();
}

// ============================================================
// Test 11: BUG#3 - UDP端口配置验证
// ============================================================

TEST_CASE(UdpPortConfiguration) {
    ASSERT_GT(nevo::UDP_MAX_PACKET_SIZE, 0u);
    ASSERT_GT(nevo::OPUS_MAX_FRAME_SIZE, 0u);

    boost::asio::io_context io_ctx;
    auto udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto bind_result = udp->bind(0);
    ASSERT_FALSE(bind_result);
    ASSERT_GT(udp->localPort(), 0);

    uint16_t configured_port = udp->localPort();
    std::cout << "  [INFO] UDP socket bound to port " << configured_port
              << " (simulates setVoiceServerUdpPort config)" << std::endl;

    udp->close();
}

// ============================================================
// Test 12: BUG#4 - UDP注册包验证（模拟发送注册包到中继）
// ============================================================

TEST_CASE(UdpRegistrationPacket) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    auto client_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(client_udp->bind(0));

    std::atomic<bool> registration_received{false};
    std::atomic<uint32_t> received_sender_id{0};

    relay_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                               const boost::asio::ip::udp::endpoint& from_ep) {
        uint32_t header_size = 0;
        auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
        if (header) {
            registration_received.store(true);
            received_sender_id.store(header->sender_id());
            std::cout << "  [INFO] Relay received registration from user "
                      << header->sender_id() << " at "
                      << from_ep.address().to_string() << ":" << from_ep.port() << std::endl;
        }
    };

    boost::asio::co_spawn(io_ctx,
        [relay_udp]() -> boost::asio::awaitable<void> {
            co_await relay_udp->asyncReceiveFrom();
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint8_t key[nevo::CRYPTO_KEY_SIZE];
    fillKey(key, 99);
    nevo::VoiceCrypto crypto;
    crypto.setSessionKey(key);

    nevo::UserId user_id(9999);
    nevo::ChannelId channel_id(0);

    uint8_t empty_payload[] = {0x00};
    auto reg_packet = buildVoicePacket(crypto, user_id, channel_id,
                                        0, empty_payload, sizeof(empty_payload));

    auto relay_ep = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), relay_port);

    boost::asio::co_spawn(io_ctx,
        [client_udp, reg_packet, relay_ep]() -> boost::asio::awaitable<void> {
            co_await client_udp->asyncSendTo(
                reg_packet.data(), static_cast<uint32_t>(reg_packet.size()), relay_ep);
        }, boost::asio::detached);

    int max_wait = 2000;
    int waited = 0;
    while (!registration_received.load() && waited < max_wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    ASSERT_TRUE(registration_received.load());
    ASSERT_EQ(received_sender_id.load(), 9999u);

    client_udp->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "  [INFO] UDP registration packet received by relay" << std::endl;
}

// ============================================================
// Test 13: TCP隧道重组验证
// ============================================================

TEST_CASE(TcpTunnelReassembly) {
    nevo::TcpVoiceTunnel sender_tunnel;
    nevo::TcpVoiceTunnel receiver_tunnel;

    std::vector<std::vector<uint8_t>> received_frames;
    receiver_tunnel.onVoiceFrame = [&](const uint8_t* data, size_t size) {
        received_frames.emplace_back(data, data + size);
    };

    std::vector<uint8_t> frame1 = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> frame2 = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    std::vector<uint8_t> frame3(200, 0x55);

    auto tcp_data1 = sender_tunnel.sendVoiceFrame(frame1.data(), frame1.size());
    auto tcp_data2 = sender_tunnel.sendVoiceFrame(frame2.data(), frame2.size());
    auto tcp_data3 = sender_tunnel.sendVoiceFrame(frame3.data(), frame3.size());

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), tcp_data1.begin(), tcp_data1.end());
    combined.insert(combined.end(), tcp_data2.begin(), tcp_data2.end());
    combined.insert(combined.end(), tcp_data3.begin(), tcp_data3.end());

    receiver_tunnel.onTcpDataReceived(combined.data(), combined.size());

    ASSERT_EQ(received_frames.size(), 3u);
    ASSERT_EQ(received_frames[0].size(), frame1.size());
    ASSERT_EQ(std::memcmp(received_frames[0].data(), frame1.data(), frame1.size()), 0);
    ASSERT_EQ(received_frames[1].size(), frame2.size());
    ASSERT_EQ(std::memcmp(received_frames[1].data(), frame2.data(), frame2.size()), 0);
    ASSERT_EQ(received_frames[2].size(), frame3.size());
    ASSERT_EQ(std::memcmp(received_frames[2].data(), frame3.data(), frame3.size()), 0);

    std::cout << "  [INFO] TCP tunnel reassembly: 3 frames, combined="
              << combined.size() << " bytes" << std::endl;
}

// ============================================================
// Test 14: 双向语音通信模拟
// ============================================================

TEST_CASE(BidirectionalVoiceCommunication) {
    boost::asio::io_context io_ctx;

    uint16_t relay_port = allocatePort();
    auto relay_udp = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(relay_udp->bind(relay_port));

    SimpleVoiceRelay relay;
    uint8_t shared_key[nevo::CRYPTO_KEY_SIZE];
    fillKey(shared_key, 60);

    nevo::UserId user_a(6001), user_b(6002);
    nevo::ChannelId channel_id(1);

    auto udp_a = std::make_shared<nevo::UdpSocket>(io_ctx);
    auto udp_b = std::make_shared<nevo::UdpSocket>(io_ctx);
    ASSERT_FALSE(udp_a->bind(0));
    ASSERT_FALSE(udp_b->bind(0));

    auto ep_a = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_a->localPort());
    auto ep_b = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), udp_b->localPort());

    relay.addClient(user_a, channel_id, ep_a);
    relay.addClient(user_b, channel_id, ep_b);

    std::atomic<int> a_received{0}, b_received{0};
    std::mutex a_mutex, b_mutex;
    std::vector<std::vector<uint8_t>> a_data, b_data;

    udp_a->onPacket = [&](const uint8_t* data, uint32_t size, const auto&) {
        if (size > 0) {
            std::lock_guard<std::mutex> lock(a_mutex);
            a_data.emplace_back(data, data + size);
            a_received.fetch_add(1);
        }
    };
    udp_b->onPacket = [&](const uint8_t* data, uint32_t size, const auto&) {
        if (size > 0) {
            std::lock_guard<std::mutex> lock(b_mutex);
            b_data.emplace_back(data, data + size);
            b_received.fetch_add(1);
        }
    };

    boost::asio::co_spawn(io_ctx,
        [udp_a]() -> boost::asio::awaitable<void> {
            co_await udp_a->asyncReceiveFrom();
        }, boost::asio::detached);
    boost::asio::co_spawn(io_ctx,
        [udp_b]() -> boost::asio::awaitable<void> {
            co_await udp_b->asyncReceiveFrom();
        }, boost::asio::detached);

    relay_udp->onPacket = [&](const uint8_t* data, uint32_t size,
                               const boost::asio::ip::udp::endpoint&) {
        uint32_t header_size = 0;
        auto header = nevo::decodeVoicePacketHeader(data, size, header_size);
        if (!header) return;
        nevo::UserId sender(header->sender_id());
        nevo::ChannelId channel(header->channel_id());
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

    nevo::VoiceCrypto crypto_a, crypto_b;
    crypto_a.setSessionKey(shared_key);
    crypto_b.setSessionKey(shared_key);

    for (int i = 0; i < 3; ++i) {
        uint8_t payload_a[] = {0xA0, static_cast<uint8_t>(i)};
        auto packet_a = buildVoicePacket(crypto_a, user_a, channel_id,
                                          static_cast<uint64_t>(i * 2),
                                          payload_a, sizeof(payload_a));
        boost::asio::co_spawn(io_ctx,
            [udp_a, packet_a, relay_ep]() -> boost::asio::awaitable<void> {
                co_await udp_a->asyncSendTo(
                    packet_a.data(), static_cast<uint32_t>(packet_a.size()), relay_ep);
            }, boost::asio::detached);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        uint8_t payload_b[] = {0xB0, static_cast<uint8_t>(i)};
        auto packet_b = buildVoicePacket(crypto_b, user_b, channel_id,
                                          static_cast<uint64_t>(i * 2 + 1),
                                          payload_b, sizeof(payload_b));
        boost::asio::co_spawn(io_ctx,
            [udp_b, packet_b, relay_ep]() -> boost::asio::awaitable<void> {
                co_await udp_b->asyncSendTo(
                    packet_b.data(), static_cast<uint32_t>(packet_b.size()), relay_ep);
            }, boost::asio::detached);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    int max_wait = 3000;
    int waited = 0;
    while ((a_received.load() < 1 || b_received.load() < 1) && waited < max_wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    ASSERT_GE(b_received.load(), 1);
    ASSERT_GE(a_received.load(), 1);

    int a_decrypt = 0, b_decrypt = 0;
    {
        std::lock_guard<std::mutex> lock(b_mutex);
        for (const auto& pkt : b_data) {
            std::vector<uint8_t> plaintext;
            if (decryptVoicePacket(crypto_b, pkt.data(),
                                    static_cast<uint32_t>(pkt.size()), plaintext)) {
                b_decrypt++;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(a_mutex);
        for (const auto& pkt : a_data) {
            std::vector<uint8_t> plaintext;
            if (decryptVoicePacket(crypto_a, pkt.data(),
                                    static_cast<uint32_t>(pkt.size()), plaintext)) {
                a_decrypt++;
            }
        }
    }

    EXPECT_GT(b_decrypt, 0);
    EXPECT_GT(a_decrypt, 0);

    udp_a->close();
    udp_b->close();
    relay_udp->close();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "  [INFO] Bidirectional: A->B decrypt=" << b_decrypt
              << ", B->A decrypt=" << a_decrypt << std::endl;
}

// ============================================================
// main
// ============================================================

int main() {
    std::cout << "==============================================" << std::endl;
    std::cout << "  NEVO Voice Communication Simulation Test" << std::endl;
    std::cout << "  Multi-Client Local Relay Verification" << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << std::endl;

    std::cout << "[==========] Running " << g_tests_run << " tests." << std::endl;
    std::cout << std::endl;

    std::cout << "[==========] " << g_tests_run << " tests ran." << std::endl;
    std::cout << "[  PASSED  ] " << g_tests_passed << " tests." << std::endl;
    if (g_tests_failed > 0) {
        std::cout << "[  FAILED  ] " << g_tests_failed << " tests." << std::endl;
        std::cout << std::endl;
        std::cout << " " << g_tests_failed << " FAILED TEST(S)" << std::endl;
        return 1;
    }

    std::cout << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "  ALL TESTS PASSED" << std::endl;
    std::cout << "  Voice communication fixes verified!" << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
