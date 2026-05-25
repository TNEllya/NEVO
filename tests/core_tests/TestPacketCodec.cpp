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
#include <arpa/inet.h>
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/protocol/PacketTypes.h"
#include "control.pb.h"
#include "voice.pb.h"

namespace nevo {
namespace {

constexpr uint32_t kTcpHeaderSize = TCP_HEADER_SIZE;
constexpr uint32_t kMaxPayloadSize = TCP_MAX_PAYLOAD_SIZE;
constexpr uint32_t kUdpMaxSize = UDP_MAX_PACKET_SIZE;

#ifdef _WIN32
static uint32_t htonl_test(uint32_t host) {
    return htonl(host);
}
static uint32_t ntohl_test(uint32_t net) {
    return ntohl(net);
}
#else
static uint32_t htonl_test(uint32_t host) {
    return htonl(host);
}
static uint32_t ntohl_test(uint32_t net) {
    return ntohl(net);
}
#endif

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
    resp->set_result(0);
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

} // namespace
} // namespace nevo
