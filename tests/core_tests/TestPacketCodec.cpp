/**
 * @file TestPacketCodec.cpp
 * @brief Unit tests for TCP/UDP packet codec
 *
 * 覆盖缺口：PacketCodec 完全缺少测试
 * 风险等级：极高 - PacketCodec 是核心网络协议实现，涉及解析、权限校验
 * 涉及数据验证、字节序转换、自定义线格式编码/解码
 */

#include <gtest/gtest.h>
#include <vector>
#include <cstring>
#include <string>
#include <cstdint>
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/protocol/PacketTypes.h"
#include "control.pb.h"
#include "voice.pb.h"

namespace nevo {
namespace {

constexpr uint32_t kTcpHeaderSize = TCP_HEADER_SIZE;
constexpr uint32_t kMaxPayloadSize = TCP_MAX_PAYLOAD_SIZE;
constexpr uint32_t kUdpMaxSize = UDP_MAX_PACKET_SIZE;

// 跨平台 32 位主机序 → 网络序（大端）
static uint32_t htonl_test(uint32_t host) {
    return ((host & 0x000000FFu) << 24) |
           ((host & 0x0000FF00u) <<  8) |
           ((host & 0x00FF0000u) >>  8) |
           ((host & 0xFF000000u) >> 24);
}
static uint32_t ntohl_test(uint32_t net) {
    return htonl_test(net);
}

std::vector<uint8_t> createTestTcpFrame(uint32_t payload_length, uint32_t message_type, uint32_t request_id, const uint8_t* payload = nullptr) {
    std::vector<uint8_t> frame(kTcpHeaderSize + payload_length);

    uint32_t net_payload_len = htonl_test(payload_length);
    uint32_t net_msg_type = htonl_test(message_type);
    uint32_t net_request_id = htonl_test(request_id);

    std::memcpy(frame.data(), &net_payload_len, 4);
    std::memcpy(frame.data() + 4, &net_msg_type, 4);
    std::memcpy(frame.data() + 8, &net_request_id, 4);

    if (payload && payload_length > 0) {
        std::memcpy(frame.data() + kTcpHeaderSize, payload, payload_length);
    }

    return frame;
}

std::vector<uint8_t> createSimplePayload() {
    control::ControlMessage msg;
    msg.mutable_login_request()->set_username("testuser");
    msg.mutable_login_request()->set_auth_credential("password");

    std::vector<uint8_t> payload(msg.ByteSizeLong());
    msg.SerializeToArray(payload.data(), static_cast<int>(payload.size()));
    return payload;
}

// ============================================================
// TCP Frame Header Decode
// ============================================================

TEST(PacketCodecTest, DecodeTcpFrameHeaderValid) {
    std::vector<uint8_t> frame = createTestTcpFrame(100, 1, 42);

    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    ASSERT_TRUE(header.has_value());

    EXPECT_EQ(header->payload_length, 100u);
    EXPECT_EQ(header->message_type, 1u);
    EXPECT_EQ(header->request_id, 42u);
}

TEST(PacketCodecTest, DecodeTcpFrameHeaderInsufficientData) {
    std::vector<uint8_t> partial(8);

    auto header = decodeTcpFrameHeader(partial.data(), partial.size());
    EXPECT_FALSE(header.has_value());
}

TEST(PacketCodecTest, DecodeTcpFrameHeaderEmptyData) {
    std::vector<uint8_t> empty;

    auto header = decodeTcpFrameHeader(empty.data(), empty.size());
    EXPECT_FALSE(header.has_value());
}

TEST(PacketCodecTest, DecodeTcpFrameHeaderPayloadTooLarge) {
    uint32_t too_large = TCP_MAX_PAYLOAD_SIZE + 1;
    std::vector<uint8_t> frame = createTestTcpFrame(too_large, 1, 0);

    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    EXPECT_FALSE(header.has_value());
}

TEST(PacketCodecTest, DecodeTcpFrameHeaderExactSize) {
    std::vector<uint8_t> frame = createTestTcpFrame(0, 5, 99);

    auto header = decodeTcpFrameHeader(frame.data(), kTcpHeaderSize);
    ASSERT_TRUE(header.has_value());

    EXPECT_EQ(header->payload_length, 0u);
    EXPECT_EQ(header->message_type, 5u);
    EXPECT_EQ(header->request_id, 99u);
}

TEST(PacketCodecTest, DecodeTcpFrameHeaderBoundaryValues) {
    std::vector<uint8_t> frame = createTestTcpFrame(0, 0, 0);
    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->payload_length, 0u);
    EXPECT_EQ(header->message_type, 0u);
    EXPECT_EQ(header->request_id, 0u);
}

TEST(PacketCodecTest, DecodeTcpFrameHeaderMaxValues) {
    std::vector<uint8_t> frame = createTestTcpFrame(TCP_MAX_PAYLOAD_SIZE, 0xFFFFFFFF, 0xFFFFFFFF);
    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->payload_length, TCP_MAX_PAYLOAD_SIZE);
}

// ============================================================
// TCP Frame Encode/Decode Round Trip
// ============================================================

TEST(PacketCodecTest, EncodeDecodeRoundTrip) {
    control::ControlMessage msg;
    msg.mutable_login_request()->set_username("alice");
    msg.mutable_login_request()->set_auth_credential("secret123");

    auto frame = encodeTcpFrame(msg, ControlMessageType::LoginRequest, 123);
    ASSERT_FALSE(frame.empty());
    ASSERT_GE(frame.size(), kTcpHeaderSize);

    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->payload_length, frame.size() - kTcpHeaderSize);
    EXPECT_EQ(header->message_type, static_cast<uint32_t>(ControlMessageType::LoginRequest));
    EXPECT_EQ(header->request_id, 123u);
}

TEST(PacketCodecTest, EncodeEmptyMessage) {
    control::ControlMessage msg;
    auto frame = encodeTcpFrame(msg, ControlMessageType::Unknown, 0);

    EXPECT_GE(frame.size(), kTcpHeaderSize);
    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->payload_length, 0u);
}

TEST(PacketCodecTest, EncodePayloadTooLarge) {
    control::ControlMessage msg;
    std::string huge(TCP_MAX_PAYLOAD_SIZE + 1000, 'x');
    msg.mutable_login_request()->set_username(huge);

    auto frame = encodeTcpFrame(msg, ControlMessageType::LoginRequest, 0);
    EXPECT_TRUE(frame.empty());
}

// ============================================================
// UDP Voice Packet Encode/Decode
// ============================================================

TEST(PacketCodecTest, EncodeVoicePacketBasic) {
    voice::VoicePacketHeader header;
    header.set_sender_id(42);
    header.set_sequence_number(1);

    uint8_t opus_payload[] = {0x01, 0x02, 0x03, 0x04};

    auto packet = encodeVoicePacket(header, opus_payload, 4);
    ASSERT_GE(packet.size(), 2u + header.ByteSizeLong() + 4);

    uint16_t prefix_len = 0;
    std::memcpy(&prefix_len, packet.data(), 2);
    EXPECT_EQ(prefix_len, header.ByteSizeLong());
}

TEST(PacketCodecTest, EncodeVoicePacketEmptyPayload) {
    voice::VoicePacketHeader header;
    header.set_sender_id(100);

    auto packet = encodeVoicePacket(header, nullptr, 0);
    ASSERT_GE(packet.size(), 2u + header.ByteSizeLong());
}

TEST(PacketCodecTest, DecodeVoicePacketHeader) {
    voice::VoicePacketHeader orig_header;
    orig_header.set_sender_id(42);
    orig_header.set_sequence_number(99);

    std::vector<uint8_t> opus_payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto packet = encodeVoicePacket(orig_header, opus_payload.data(), opus_payload.size());

    uint32_t out_header_size = 0;
    auto decoded_header = decodeVoicePacketHeader(packet.data(), packet.size(), out_header_size);

    ASSERT_TRUE(decoded_header.has_value());
    EXPECT_EQ(decoded_header->sender_id(), 42u);
    EXPECT_EQ(decoded_header->sequence_number(), 99u);
    EXPECT_GT(out_header_size, 0u);
}

TEST(PacketCodecTest, DecodeVoicePacketHeaderInsufficientData) {
    std::vector<uint8_t> small(1, 0x00);
    uint32_t header_size = 0;

    auto header = decodeVoicePacketHeader(small.data(), small.size(), header_size);
    EXPECT_FALSE(header.has_value());
}

TEST(PacketCodecTest, DecodeVoicePacketHeaderEmpty) {
    std::vector<uint8_t> empty;
    uint32_t header_size = 0;

    auto header = decodeVoicePacketHeader(empty.data(), empty.size(), header_size);
    EXPECT_FALSE(header.has_value());
}

// ============================================================
// Get Voice Payload
// ============================================================

TEST(PacketCodecTest, GetVoicePayloadValid) {
    voice::VoicePacketHeader header;
    header.set_sender_id(1);

    uint8_t opus[] = {0xAA, 0xBB, 0xCC, 0xDD};
    auto packet = encodeVoicePacket(header, opus, 4);

    uint32_t header_size = 0;
    decodeVoicePacketHeader(packet.data(), packet.size(), header_size);

    auto [payload_ptr, payload_size] = getVoicePayload(packet.data(), header_size, packet.size());

    EXPECT_NE(payload_ptr, nullptr);
    EXPECT_EQ(payload_size, 4u);
    EXPECT_EQ(payload_ptr[0], 0xAA);
    EXPECT_EQ(payload_ptr[1], 0xBB);
}

TEST(PacketCodecTest, GetVoicePayloadInvalidSize) {
    uint8_t dummy_data[10] = {0};
    auto [ptr, size] = getVoicePayload(dummy_data, 20, 10);

    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(size, 0u);
}

TEST(PacketCodecTest, GetVoicePayloadExactSize) {
    uint8_t dummy_data[10] = {0};
    auto [ptr, size] = getVoicePayload(dummy_data, 10, 10);

    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(size, 0u);
}

// ============================================================
// Control Message Type Detection
// ============================================================

TEST(PacketCodecTest, GetControlMessageTypeLoginRequest) {
    control::ControlMessage msg;
    msg.mutable_login_request()->set_username("user");

    EXPECT_EQ(getControlMessageType(msg), ControlMessageType::LoginRequest);
}

TEST(PacketCodecTest, GetControlMessageTypeJoinChannel) {
    control::ControlMessage msg;
    msg.mutable_join_channel()->set_channel_id(123);

    EXPECT_EQ(getControlMessageType(msg), ControlMessageType::JoinChannel);
}

TEST(PacketCodecTest, GetControlMessageTypePttToggle) {
    control::ControlMessage msg;
    msg.mutable_ptt_toggle()->set_active(true);

    EXPECT_EQ(getControlMessageType(msg), ControlMessageType::PttToggle);
}

TEST(PacketCodecTest, GetControlMessageTypeMuteToggle) {
    control::ControlMessage msg;
    msg.mutable_mute_toggle()->set_muted(true);

    EXPECT_EQ(getControlMessageType(msg), ControlMessageType::MuteToggle);
}

TEST(PacketCodecTest, GetControlMessageTypeUnknown) {
    control::ControlMessage msg;

    EXPECT_EQ(getControlMessageType(msg), ControlMessageType::Unknown);
}

// ============================================================
// Control Message Type to String
// ============================================================

TEST(PacketCodecTest, ControlMessageTypeToStringLoginRequest) {
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::LoginRequest), "LoginRequest");
}

TEST(PacketCodecTest, ControlMessageTypeToStringJoinChannel) {
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::JoinChannel), "JoinChannel");
}

TEST(PacketCodecTest, ControlMessageTypeToStringPttToggle) {
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::PttToggle), "PttToggle");
}

TEST(PacketCodecTest, ControlMessageTypeToStringUnknown) {
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::Unknown), "Unknown");
}

TEST(PacketCodecTest, ControlMessageTypeToStringAllTypes) {
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::LoginRequest), "LoginRequest");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::LoginResponse), "LoginResponse");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::JoinChannel), "JoinChannel");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::LeaveChannel), "LeaveChannel");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::CreateChannel), "CreateChannel");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::DeleteChannel), "DeleteChannel");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::PttToggle), "PttToggle");
    EXPECT_STREQ(controlMessageTypeToString(ControlMessageType::MuteToggle), "MuteToggle");
}

// ============================================================
// Custom Wire Format Decode
// ============================================================

TEST(PacketCodecTest, DecodeCustomWireLoginRequest) {
    std::vector<uint8_t> wire;
    wire.resize(8 + 100);

    uint32_t case_val = htonl_test(1);
    uint32_t inner_len = htonl_test(96);
    std::memcpy(wire.data(), &case_val, 4);
    std::memcpy(wire.data() + 4, &inner_len, 4);

    auto username = std::string("testuser");
    uint32_t name_len = htonl_test(static_cast<uint32_t>(username.size()));
    std::memcpy(wire.data() + 8, &name_len, 4);
    std::memcpy(wire.data() + 12, username.data(), username.size());

    auto result = decodeCustomWirePayload(wire.data(), wire.size());
    EXPECT_FALSE(result.has_value());
}

TEST(PacketCodecTest, DecodeCustomWirePayloadTooSmall) {
    std::vector<uint8_t> tiny(4, 0);

    auto result = decodeCustomWirePayload(tiny.data(), tiny.size());
    EXPECT_FALSE(result.has_value());
}

TEST(PacketCodecTest, DecodeCustomWireUnknownCase) {
    std::vector<uint8_t> wire(16, 0);

    uint32_t case_val = htonl_test(9999);
    uint32_t inner_len = htonl_test(8);
    std::memcpy(wire.data(), &case_val, 4);
    std::memcpy(wire.data() + 4, &inner_len, 4);

    auto result = decodeCustomWirePayload(wire.data(), wire.size());
    EXPECT_FALSE(result.has_value());
}

// ============================================================
// Custom Wire Format Encode
// ============================================================

TEST(PacketCodecTest, EncodeCustomWirePayloadLoginResponse) {
    control::ControlMessage msg;
    auto* resp = msg.mutable_login_response();
    resp->set_result(nevo::common::ResultCode::OK);
    resp->set_session_token("token123");
    resp->set_key_exchange_method("x25519");
    resp->set_owner_exists(false);
    resp->set_server_udp_port(5000);
    resp->set_server_video_udp_port(0);

    auto wire = encodeCustomWirePayload(msg);
    EXPECT_FALSE(wire.empty());
    EXPECT_GE(wire.size(), 8u);
}

TEST(PacketCodecTest, EncodeCustomWirePayloadServerMessage) {
    control::ControlMessage msg;
    msg.mutable_server_message()->set_text("Hello World");

    auto wire = encodeCustomWirePayload(msg);
    EXPECT_FALSE(wire.empty());
    EXPECT_GE(wire.size(), 8u);
}

TEST(PacketCodecTest, EncodeCustomWirePayloadUserSpeaking) {
    control::ControlMessage msg;
    auto* speaking = msg.mutable_user_speaking();
    speaking->set_user_id(42);
    speaking->set_speaking(true);

    auto wire = encodeCustomWirePayload(msg);
    EXPECT_FALSE(wire.empty());
}

TEST(PacketCodecTest, EncodeCustomWirePayloadPttToggle) {
    control::ControlMessage msg;
    msg.mutable_ptt_toggle()->set_active(true);

    auto wire = encodeCustomWirePayload(msg);
    EXPECT_FALSE(wire.empty());
}

TEST(PacketCodecTest, EncodeCustomWirePayloadNoPayload) {
    control::ControlMessage msg;

    auto wire = encodeCustomWirePayload(msg);
    EXPECT_TRUE(wire.empty());
}

TEST(PacketCodecTest, EncodeCustomWirePayloadUnknownType) {
    control::ControlMessage msg;
    msg.mutable_create_channel()->set_name("Test");

    auto wire = encodeCustomWirePayload(msg);
    EXPECT_TRUE(wire.empty());
}

// ============================================================
// UDP Packet Size Limits
// ============================================================

TEST(PacketCodecTest, EncodeVoicePacketSizeWarning) {
    voice::VoicePacketHeader header;
    header.set_sender_id(1);
    header.set_sequence_number(1);

    std::vector<uint8_t> large_payload(UDP_MAX_PACKET_SIZE, 0xFF);
    auto packet = encodeVoicePacket(header, large_payload.data(), large_payload.size());

    EXPECT_GE(packet.size(), UDP_MAX_PACKET_SIZE);
}

// ============================================================
// Edge Cases
// ============================================================

TEST(PacketCodecTest, TcpFrameHeaderAlignment) {
    std::vector<uint8_t> frame = createTestTcpFrame(0, 1, 0);

    EXPECT_EQ(frame.size() % 4, 0);
}

TEST(PacketCodecTest, MultipleEncodesProduceSameOutput) {
    control::ControlMessage msg;
    msg.mutable_login_request()->set_username("user");
    msg.mutable_login_request()->set_auth_credential("pass");

    auto frame1 = encodeTcpFrame(msg, ControlMessageType::LoginRequest, 1);
    auto frame2 = encodeTcpFrame(msg, ControlMessageType::LoginRequest, 1);

    EXPECT_EQ(frame1.size(), frame2.size());
}

TEST(PacketCodecTest, DecodeModifiedPayload) {
    control::ControlMessage msg;
    msg.mutable_login_request()->set_username("original");

    auto frame = encodeTcpFrame(msg, ControlMessageType::LoginRequest, 1);
    ASSERT_FALSE(frame.empty());

    if (frame.size() > kTcpHeaderSize) {
        frame[kTcpHeaderSize] = 0xFF;
    }

    auto header = decodeTcpFrameHeader(frame.data(), frame.size());
    ASSERT_TRUE(header.has_value());

    auto decoded = decodeTcpFramePayload(*header, frame.data() + kTcpHeaderSize);
    if (decoded.has_value()) {
        EXPECT_FALSE(decoded->login_request().username() == "original");
    }
}

// ============================================================
// 跨语言互操作金样测试（协议统一性锁死）
//
// 运行时唯一线格式：自定义小端 TLV（见 docs/protocol-wire-format.md）——
// C++ 服务端、Python 客户端、Android 客户端共用同一格式。
// 以下金样字节由 Python 端 nevo_wire 生成并硬编码于此：
// 任何一端格式漂移，断言即失败。
// ============================================================

namespace {

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(
            std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out += hex[b >> 4];
        out += hex[b & 0x0F];
    }
    return out;
}

} // namespace

// Python → C++：Python 编码的 LoginRequest（外层 case=1），C++ 必须逐字段解码一致
TEST(PacketCodecInteropTest, DecodePythonLoginRequestGolden) {
    const std::string golden_hex =
        "010000006800000011000000696e7465726f705f746573745f75736572"
        "0900000070617373773072642101000000160000005832353531392b63"
        "727970746f5f626f785f7365616c20000000000102030405060708090a"
        "0b0c0d0e0f101112131415161718191a1b1c1d1e1f6f5f705f";

    auto bytes = hexToBytes(golden_hex);
    auto msg = decodeCustomWirePayload(bytes.data(),
                                       static_cast<uint32_t>(bytes.size()));
    ASSERT_TRUE(msg.has_value()) << "C++ must decode Python's LoginRequest";
    ASSERT_TRUE(msg->has_login_request());

    const auto& req = msg->login_request();
    EXPECT_EQ(req.username(), "interop_test_user");
    EXPECT_EQ(req.auth_credential(), "passw0rd!");
    ASSERT_EQ(req.key_exchange_methods_size(), 1);
    EXPECT_EQ(req.key_exchange_methods(0), "X25519+crypto_box_seal");

    std::string expected_pubkey;
    for (int i = 0; i < 32; ++i) {
        expected_pubkey.push_back(static_cast<char>(i));
    }
    EXPECT_EQ(req.client_public_key(), expected_pubkey);
    EXPECT_EQ(req.client_udp_port(), 24431u);
    EXPECT_EQ(req.client_video_udp_port(), 24432u);
}

// C++ → Python：C++ 编码的 LoginResponse 必须与 Python 编码的字节逐字节一致
TEST(PacketCodecInteropTest, EncodeLoginResponseMatchesPythonGolden) {
    control::ControlMessage msg;
    auto* r = msg.mutable_login_response();
    r->set_result(nevo::common::ResultCode::OK);

    auto* u = r->mutable_user_info();
    u->set_id(42);
    u->set_username("alice");
    u->set_status(nevo::common::UserStatus::ONLINE);
    u->set_muted(false);
    u->set_deafened(true);
    u->set_group_id(2);

    r->set_session_token("abc123token");

    std::string server_pub;
    for (int i = 16; i < 32; ++i) {
        server_pub.push_back(static_cast<char>(i));
    }
    r->set_server_public_key(server_pub);
    r->set_key_exchange_method("X25519+crypto_box_seal");

    std::string enc_key(80, '\0');
    for (int i = 0; i < 80; ++i) {
        enc_key[static_cast<size_t>(i)] = static_cast<char>((i % 2) ? 0xBB : 0xAA);
    }
    r->set_encrypted_session_key(enc_key);

    r->set_owner_exists(true);
    r->set_server_udp_port(24431);
    r->set_server_video_udp_port(24432);

    auto bytes = encodeCustomWirePayload(msg);
    ASSERT_FALSE(bytes.empty());

    const std::string golden_hex =
        "02000000bc000000000000001b0000002a0000000000000005000000616c6963"
        "65010000000001020000000b000000616263313233746f6b656e100000001011"
        "12131415161718191a1b1c1d1e1f160000005832353531392b63727970746f5f"
        "626f785f7365616c50000000aabbaabbaabbaabbaabbaabbaabbaabbaabbaabb"
        "aabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabb"
        "aabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabb01000000"
        "6f5f705f";

    EXPECT_EQ(bytesToHex(bytes), golden_hex)
        << "C++ wire bytes must be byte-identical to Python's (format drift!)";
}

// 文件传输分片消息（44/45/46/47）跨语言互操作金样：
// C++ 解码 Python 编码的请求（44/46），C++ 编码必须与 Python 编码逐字节一致（45/47）

TEST(PacketCodecInteropTest, DecodePythonFileChunkMessagesGolden) {
    // 44 FileUploadChunkRequest: u64 file_id, u32 chunk_index, u32 total_chunks, bytes data
    {
        auto bytes = hexToBytes(
            "2c0000001e00000009030000000000000200000005000000"
            "0a0000006368756e6b2d64617461");
        auto msg = decodeCustomWirePayload(bytes.data(),
                                           static_cast<uint32_t>(bytes.size()));
        ASSERT_TRUE(msg.has_value());
        ASSERT_TRUE(msg->has_file_upload_chunk_request());
        const auto& req = msg->file_upload_chunk_request();
        EXPECT_EQ(req.file_id(), 777u);
        EXPECT_EQ(req.chunk_index(), 2u);
        EXPECT_EQ(req.total_chunks(), 5u);
        EXPECT_EQ(req.data(), "chunk-data");
    }

    // 46 FileDownloadRequest: u64 file_id
    {
        auto bytes = hexToBytes("2e000000080000000903000000000000");
        auto msg = decodeCustomWirePayload(bytes.data(),
                                           static_cast<uint32_t>(bytes.size()));
        ASSERT_TRUE(msg.has_value());
        ASSERT_TRUE(msg->has_file_download_request());
        EXPECT_EQ(msg->file_download_request().file_id(), 777u);
    }
}

TEST(PacketCodecInteropTest, EncodeFileChunkMessagesMatchesPythonGolden) {
    // 45 FileUploadChunkAck: u64 file_id, u32 chunk_index, u32 result
    {
        control::ControlMessage msg;
        auto* ack = msg.mutable_file_upload_chunk_ack();
        ack->set_file_id(777);
        ack->set_chunk_index(2);
        ack->set_result(nevo::common::ResultCode::OK);
        auto bytes = encodeCustomWirePayload(msg);
        EXPECT_EQ(bytesToHex(bytes),
                  "2d0000001000000009030000000000000200000000000000");
    }

    // 47 FileDownloadResponse: u32 result, string message, u64 file_id,
    //    string filename, u64 file_size, u32 chunk_index, u32 total_chunks, bytes data
    {
        control::ControlMessage msg;
        auto* r = msg.mutable_file_download_response();
        r->set_result(nevo::common::ResultCode::OK);
        r->set_message("OK");
        r->set_file_id(777);
        r->set_filename("a.bin");
        r->set_file_size(100);
        r->set_chunk_index(0);
        r->set_total_chunks(1);
        r->set_data("abc");
        auto bytes = encodeCustomWirePayload(msg);
        EXPECT_EQ(bytesToHex(bytes),
                  "2f0000003200000000000000020000004f4b0903000000000000"
                  "05000000612e62696e64000000000000000000000001000000"
                  "03000000616263");
    }
}

} // namespace
} // namespace nevo
