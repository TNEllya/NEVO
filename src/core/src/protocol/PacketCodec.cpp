/**
 * @file PacketCodec.cpp
 * @brief 包编解码工具实现
 */

#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/common/Logger.h"

// Protobuf 生成头文件
#include "common.pb.h"
#include "control.pb.h"
#include "voice.pb.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

#include <cstring>
#include <stdexcept>

namespace nevo {

// ============================================================
// TCP 帧编解码
// ============================================================

std::vector<uint8_t> encodeTcpFrame(const control::ControlMessage& msg,
                                     ControlMessageType type,
                                     uint32_t request_id) {
    // 序列化 Protobuf 消息
    const size_t payload_size = msg.ByteSizeLong();
    if (payload_size > TCP_MAX_PAYLOAD_SIZE) {
        NEVO_LOG_ERROR("protocol", "TCP payload too large: {} bytes", payload_size);
        return {};
    }

    // 帧总大小 = 帧头 + 载荷
    std::vector<uint8_t> frame(TCP_HEADER_SIZE + payload_size);

    // 写入帧头（网络字节序 - 大端）
    uint32_t net_payload_len = htonl(static_cast<uint32_t>(payload_size));
    uint32_t net_msg_type = htonl(static_cast<uint32_t>(type));
    uint32_t net_request_id = htonl(request_id);

    std::memcpy(frame.data(), &net_payload_len, 4);
    std::memcpy(frame.data() + 4, &net_msg_type, 4);
    std::memcpy(frame.data() + 8, &net_request_id, 4);

    // 写入载荷
    if (payload_size > 0) {
        if (!msg.SerializeToArray(frame.data() + TCP_HEADER_SIZE, static_cast<int>(payload_size))) {
            NEVO_LOG_ERROR("protocol", "Failed to serialize ControlMessage ({} bytes)", payload_size);
            return {};
        }
    }

    NEVO_LOG_DEBUG("protocol", "Encoded TCP frame: type={}, request_id={}, payload={} bytes",
                   static_cast<uint32_t>(type), request_id, payload_size);
    return frame;
}

std::optional<TcpFrameHeader> decodeTcpFrameHeader(const uint8_t* data, uint32_t size) {
    if (size < TCP_HEADER_SIZE) {
        NEVO_LOG_WARN("protocol", "TCP frame header decode failed: insufficient data ({} < {} bytes)",
                      size, TCP_HEADER_SIZE);
        return std::nullopt;  // 数据不足，需要继续读取
    }

    TcpFrameHeader header;

    // 读取帧头（大端转主机序）
    uint32_t net_payload_len, net_msg_type, net_request_id;
    std::memcpy(&net_payload_len, data, 4);
    std::memcpy(&net_msg_type, data + 4, 4);
    std::memcpy(&net_request_id, data + 8, 4);

    header.payload_length = ntohl(net_payload_len);
    header.message_type = ntohl(net_msg_type);
    header.request_id = ntohl(net_request_id);

    // 安全检查
    if (header.payload_length > TCP_MAX_PAYLOAD_SIZE) {
        NEVO_LOG_ERROR("protocol", "TCP payload too large in header: {} bytes", header.payload_length);
        return std::nullopt;
    }

    NEVO_LOG_DEBUG("protocol", "Decoded TCP frame header: type={}, request_id={}, payload_len={} bytes",
                   header.message_type, header.request_id, header.payload_length);
    return header;
}

std::optional<control::ControlMessage> decodeTcpFramePayload(
    const TcpFrameHeader& header,
    const uint8_t* payload_data) {
    control::ControlMessage msg;
    if (!msg.ParseFromArray(payload_data, static_cast<int>(header.payload_length))) {
        NEVO_LOG_ERROR("protocol", "Failed to parse ControlMessage payload ({} bytes)", header.payload_length);
        return std::nullopt;
    }

    NEVO_LOG_DEBUG("protocol", "Decoded TCP frame payload successfully ({} bytes)", header.payload_length);
    return msg;
}

// ============================================================
// UDP 语音包编解码
// ============================================================

std::vector<uint8_t> encodeVoicePacket(const voice::VoicePacketHeader& header,
                                        const uint8_t* opus_payload,
                                        uint32_t payload_size) {
    // 序列化 Protobuf 头部
    const size_t header_size = header.ByteSizeLong();
    const size_t total_size = header_size + payload_size;

    if (total_size > UDP_MAX_PACKET_SIZE) {
        NEVO_LOG_WARN("protocol", "UDP packet exceeds MTU: {} bytes", total_size);
    }

    std::vector<uint8_t> packet(total_size);

    // 写入 Protobuf 头部（Varint 编码，不含长度前缀）
    header.SerializeToArray(packet.data(), static_cast<int>(header_size));

    // 写入加密 Opus 载荷
    if (payload_size > 0 && opus_payload != nullptr) {
        std::memcpy(packet.data() + header_size, opus_payload, payload_size);
    }

    NEVO_LOG_DEBUG("protocol", "Encoded voice packet: header={} bytes, payload={} bytes, total={} bytes",
                   header_size, payload_size, total_size);
    return packet;
}

std::optional<voice::VoicePacketHeader> decodeVoicePacketHeader(
    const uint8_t* data,
    uint32_t size,
    uint32_t& out_header_size) {
    voice::VoicePacketHeader header;

    // Protobuf 不含长度前缀，需要尝试解析并确定实际消耗的字节数
    // 使用 ParseFromArray 并通过 ByteSizeLong 确定实际大小
    // 更优方案：使用 CodedInputStream 的递归解析
    if (!header.ParseFromArray(data, static_cast<int>(size))) {
        NEVO_LOG_ERROR("protocol", "Failed to parse VoicePacketHeader");
        return std::nullopt;
    }

    out_header_size = static_cast<uint32_t>(header.ByteSizeLong());

    NEVO_LOG_DEBUG("protocol", "Decoded voice packet header: {} bytes", out_header_size);
    return header;
}

std::pair<const uint8_t*, uint32_t> getVoicePayload(
    const uint8_t* data,
    uint32_t header_size,
    uint32_t total_size) {
    if (total_size <= header_size) {
        return {nullptr, 0};
    }
    return {data + header_size, total_size - header_size};
}

// ============================================================
// 工具函数
// ============================================================

ControlMessageType getControlMessageType(const control::ControlMessage& msg) {
    switch (msg.payload_case()) {
        case control::ControlMessage::kLoginRequest:      return ControlMessageType::LoginRequest;
        case control::ControlMessage::kLoginResponse:     return ControlMessageType::LoginResponse;
        case control::ControlMessage::kJoinChannel:       return ControlMessageType::JoinChannel;
        case control::ControlMessage::kLeaveChannel:      return ControlMessageType::LeaveChannel;
        case control::ControlMessage::kCreateChannel:     return ControlMessageType::CreateChannel;
        case control::ControlMessage::kDeleteChannel:     return ControlMessageType::DeleteChannel;
        case control::ControlMessage::kChannelList:       return ControlMessageType::ChannelList;
        case control::ControlMessage::kUserJoined:        return ControlMessageType::UserJoined;
        case control::ControlMessage::kUserLeft:          return ControlMessageType::UserLeft;
        case control::ControlMessage::kUserSpeaking:      return ControlMessageType::UserSpeaking;
        case control::ControlMessage::kPttToggle:         return ControlMessageType::PttToggle;
        case control::ControlMessage::kMuteToggle:        return ControlMessageType::MuteToggle;
        case control::ControlMessage::kServerMessage:     return ControlMessageType::ServerMessage;
        case control::ControlMessage::kStunBindRequest:   return ControlMessageType::StunBindRequest;
        case control::ControlMessage::kStunBindResponse:  return ControlMessageType::StunBindResponse;
        case control::ControlMessage::kUdpPingRequest:    return ControlMessageType::UdpPingRequest;
        case control::ControlMessage::kUdpPingResponse:   return ControlMessageType::UdpPingResponse;
        case control::ControlMessage::kKeyRotationRequest:  return ControlMessageType::KeyRotationRequest;
        case control::ControlMessage::kKeyRotationResponse: return ControlMessageType::KeyRotationResponse;
        default: return ControlMessageType::Unknown;
    }
}

const char* controlMessageTypeToString(ControlMessageType type) {
    switch (type) {
        case ControlMessageType::LoginRequest:      return "LoginRequest";
        case ControlMessageType::LoginResponse:     return "LoginResponse";
        case ControlMessageType::JoinChannel:       return "JoinChannel";
        case ControlMessageType::LeaveChannel:      return "LeaveChannel";
        case ControlMessageType::CreateChannel:     return "CreateChannel";
        case ControlMessageType::DeleteChannel:     return "DeleteChannel";
        case ControlMessageType::ChannelList:       return "ChannelList";
        case ControlMessageType::UserJoined:        return "UserJoined";
        case ControlMessageType::UserLeft:          return "UserLeft";
        case ControlMessageType::UserSpeaking:      return "UserSpeaking";
        case ControlMessageType::PttToggle:         return "PttToggle";
        case ControlMessageType::MuteToggle:        return "MuteToggle";
        case ControlMessageType::ServerMessage:     return "ServerMessage";
        case ControlMessageType::StunBindRequest:   return "StunBindRequest";
        case ControlMessageType::StunBindResponse:  return "StunBindResponse";
        case ControlMessageType::UdpPingRequest:    return "UdpPingRequest";
        case ControlMessageType::UdpPingResponse:   return "UdpPingResponse";
        case ControlMessageType::KeyRotationRequest:  return "KeyRotationRequest";
        case ControlMessageType::KeyRotationResponse: return "KeyRotationResponse";
        default: return "Unknown";
    }
}

} // namespace nevo
