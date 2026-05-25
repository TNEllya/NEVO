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
#include <unordered_map>

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
    const size_t header_size = header.ByteSizeLong();
    const size_t total_size = 2 + header_size + payload_size;

    if (total_size > UDP_MAX_PACKET_SIZE) {
        NEVO_LOG_WARN("protocol", "UDP packet exceeds MTU: {} bytes", total_size);
    }

    std::vector<uint8_t> packet(total_size);

    uint16_t header_len = static_cast<uint16_t>(header_size);
    std::memcpy(packet.data(), &header_len, 2);

    header.SerializeToArray(packet.data() + 2, static_cast<int>(header_size));

    if (payload_size > 0 && opus_payload != nullptr) {
        std::memcpy(packet.data() + 2 + header_size, opus_payload, payload_size);
    }

    NEVO_LOG_DEBUG("protocol", "Encoded voice packet: prefix=2, header={} bytes, payload={} bytes, total={} bytes",
                   header_size, payload_size, total_size);
    return packet;
}

std::optional<voice::VoicePacketHeader> decodeVoicePacketHeader(
    const uint8_t* data,
    uint32_t size,
    uint32_t& out_header_size) {
    voice::VoicePacketHeader header;

    if (size < 2) {
        NEVO_LOG_ERROR("protocol", "Voice packet too small: {} bytes", size);
        return std::nullopt;
    }

    uint16_t prefix_header_len = 0;
    std::memcpy(&prefix_header_len, data, 2);

    if (prefix_header_len > 0 && prefix_header_len <= size - 2) {
        if (!header.ParseFromArray(data + 2, static_cast<int>(prefix_header_len))) {
            NEVO_LOG_ERROR("protocol", "Failed to parse VoicePacketHeader with prefix (len={})", prefix_header_len);
            return std::nullopt;
        }
        out_header_size = 2 + prefix_header_len;
    } else {
        if (!header.ParseFromArray(data, static_cast<int>(size))) {
            NEVO_LOG_ERROR("protocol", "Failed to parse VoicePacketHeader (legacy)");
            return std::nullopt;
        }
        out_header_size = static_cast<uint32_t>(header.ByteSizeLong());
    }

    NEVO_LOG_DEBUG("protocol", "Decoded voice packet header: total consumed={} bytes", out_header_size);
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
        case control::ControlMessage::kAdminAuthRequest:    return ControlMessageType::AdminAuthRequest;
        case control::ControlMessage::kAdminAuthResponse:   return ControlMessageType::AdminAuthResponse;
        case control::ControlMessage::kSetServerNameRequest: return ControlMessageType::SetServerNameRequest;
        case control::ControlMessage::kSetServerNameResponse: return ControlMessageType::SetServerNameResponse;
        case control::ControlMessage::kSetAdminRequest:    return ControlMessageType::SetAdminRequest;
        case control::ControlMessage::kSetAdminResponse:   return ControlMessageType::SetAdminResponse;
        case control::ControlMessage::kKickUserRequest:    return ControlMessageType::KickUserRequest;
        case control::ControlMessage::kKickUserResponse:   return ControlMessageType::KickUserResponse;
        case control::ControlMessage::kBanUserRequest:     return ControlMessageType::BanUserRequest;
        case control::ControlMessage::kBanUserResponse:    return ControlMessageType::BanUserResponse;
        case control::ControlMessage::kMoveUserRequest:    return ControlMessageType::MoveUserRequest;
        case control::ControlMessage::kMoveUserResponse:   return ControlMessageType::MoveUserResponse;
        case control::ControlMessage::kChatSend:           return ControlMessageType::ChatSend;
        case control::ControlMessage::kChatBroadcast:      return ControlMessageType::ChatBroadcast;
        case control::ControlMessage::kRenameChannel:      return ControlMessageType::RenameChannel;
        case control::ControlMessage::kRenameChannelResponse: return ControlMessageType::RenameChannelResponse;
        case control::ControlMessage::kFileListRequest:     return ControlMessageType::FileListRequest;
        case control::ControlMessage::kFileListResponse:    return ControlMessageType::FileListResponse;
        case control::ControlMessage::kFileUploadRequest:   return ControlMessageType::FileUploadRequest;
        case control::ControlMessage::kFileUploadResponse:  return ControlMessageType::FileUploadResponse;
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
        case ControlMessageType::AdminAuthRequest:    return "AdminAuthRequest";
        case ControlMessageType::AdminAuthResponse:   return "AdminAuthResponse";
        case ControlMessageType::SetServerNameRequest: return "SetServerNameRequest";
        case ControlMessageType::SetServerNameResponse: return "SetServerNameResponse";
        case ControlMessageType::SetAdminRequest:    return "SetAdminRequest";
        case ControlMessageType::SetAdminResponse:   return "SetAdminResponse";
        case ControlMessageType::KickUserRequest:    return "KickUserRequest";
        case ControlMessageType::KickUserResponse:   return "KickUserResponse";
        case ControlMessageType::BanUserRequest:     return "BanUserRequest";
        case ControlMessageType::BanUserResponse:    return "BanUserResponse";
        case ControlMessageType::MoveUserRequest:    return "MoveUserRequest";
        case ControlMessageType::MoveUserResponse:   return "MoveUserResponse";
        case ControlMessageType::ChatSend:           return "ChatSend";
        case ControlMessageType::ChatBroadcast:      return "ChatBroadcast";
        case ControlMessageType::RenameChannel:      return "RenameChannel";
        case ControlMessageType::RenameChannelResponse: return "RenameChannelResponse";
        case ControlMessageType::FileListRequest:     return "FileListRequest";
        case ControlMessageType::FileListResponse:    return "FileListResponse";
        case ControlMessageType::FileUploadRequest:   return "FileUploadRequest";
        case ControlMessageType::FileUploadResponse:  return "FileUploadResponse";
        case ControlMessageType::FileDownloadRequest: return "FileDownloadRequest";
        case ControlMessageType::FileDownloadResponse: return "FileDownloadResponse";
        case ControlMessageType::FileDeleteRequest:   return "FileDeleteRequest";
        case ControlMessageType::FileDeleteResponse:  return "FileDeleteResponse";
        default: return "Unknown";
    }
}

// ============================================================
// 自定义线格式解码（兼容 Python 客户端）
// ============================================================

namespace {

class CustomWireReader {
public:
    CustomWireReader(const uint8_t* data, uint32_t size)
        : data_(data), size_(size), offset_(0) {}

    bool eof() const { return offset_ >= size_; }
    uint32_t remaining() const { return size_ - offset_; }

    bool readU32(uint32_t& out) {
        if (offset_ + 4 > size_) return false;
        std::memcpy(&out, data_ + offset_, 4);
        offset_ += 4;
        return true;
    }

    bool readU16(uint16_t& out) {
        if (offset_ + 2 > size_) return false;
        std::memcpy(&out, data_ + offset_, 2);
        offset_ += 2;
        return true;
    }

    bool readU64(uint64_t& out) {
        if (offset_ + 8 > size_) return false;
        std::memcpy(&out, data_ + offset_, 8);
        offset_ += 8;
        return true;
    }

    bool readBool(bool& out) {
        if (offset_ + 1 > size_) return false;
        out = (data_[offset_] != 0);
        offset_ += 1;
        return true;
    }

    bool readString(std::string& out) {
        uint32_t len = 0;
        if (!readU32(len)) return false;
        if (offset_ + len > size_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return true;
    }

    bool readBytes(std::string& out) {
        uint32_t len = 0;
        if (!readU32(len)) return false;
        if (offset_ + len > size_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return true;
    }

    bool readRaw(uint32_t len, const uint8_t*& out) {
        if (offset_ + len > size_) return false;
        out = data_ + offset_;
        offset_ += len;
        return true;
    }

    uint32_t offset() const { return offset_; }

private:
    const uint8_t* data_;
    uint32_t size_;
    uint32_t offset_;
};

bool decodeLoginRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_login_request();
    std::string s;
    if (!r.readString(s)) return false;
    req->set_username(s);
    if (!r.readBytes(s)) return false;
    req->set_auth_credential(s);
    uint32_t count = 0;
    if (!r.readU32(count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!r.readString(s)) return false;
        req->add_key_exchange_methods(s);
    }
    if (!r.readBytes(s)) return false;
    req->set_client_public_key(s);
    uint16_t udp_port = 0;
    if (r.readU16(udp_port)) {
        req->set_client_udp_port(static_cast<uint32_t>(udp_port));
    }
    uint16_t video_udp_port = 0;
    if (r.readU16(video_udp_port)) {
        req->set_client_video_udp_port(static_cast<uint32_t>(video_udp_port));
    }
    return true;
}

bool decodeJoinChannelRequest(CustomWireReader& r, control::ControlMessage& msg) {
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    msg.mutable_join_channel()->set_channel_id(id);
    return true;
}

bool decodeLeaveChannelRequest(CustomWireReader&, control::ControlMessage& msg) {
    msg.mutable_leave_channel();
    return true;
}

bool decodeCreateChannelRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_create_channel();
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    req->set_parent_id(id);
    std::string s;
    if (!r.readString(s)) return false;
    req->set_name(s);
    return true;
}

bool decodeDeleteChannelRequest(CustomWireReader& r, control::ControlMessage& msg) {
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    msg.mutable_delete_channel()->set_channel_id(id);
    return true;
}

bool decodePttToggle(CustomWireReader& r, control::ControlMessage& msg) {
    bool v = false;
    if (!r.readBool(v)) return false;
    msg.mutable_ptt_toggle()->set_active(v);
    return true;
}

bool decodeMuteToggle(CustomWireReader& r, control::ControlMessage& msg) {
    bool v = false;
    if (!r.readBool(v)) return false;
    msg.mutable_mute_toggle()->set_muted(v);
    return true;
}

bool decodeUdpPingRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_udp_ping_request();
    uint32_t seq = 0;
    if (!r.readU32(seq)) return false;
    req->set_sequence(seq);
    std::string s;
    if (!r.readBytes(s)) return false;
    req->set_client_udp_key(s);
    return true;
}

bool decodeAdminAuthRequest(CustomWireReader& r, control::ControlMessage& msg) {
    std::string s;
    if (!r.readString(s)) return false;
    msg.mutable_admin_auth_request()->set_password(s);
    return true;
}

bool decodeSetServerNameRequest(CustomWireReader& r, control::ControlMessage& msg) {
    std::string s;
    if (!r.readString(s)) return false;
    msg.mutable_set_server_name_request()->set_server_name(s);
    return true;
}

bool decodeChatSendRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_chat_send();
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    req->set_channel_id(id);
    std::string s;
    if (!r.readString(s)) return false;
    req->set_text(s);
    return true;
}

bool decodeKickUserRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_kick_user_request();
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    req->set_user_id(id);
    std::string s;
    if (!r.readString(s)) return false;
    req->set_reason(s);
    return true;
}

bool decodeBanUserRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_ban_user_request();
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    req->set_user_id(id);
    std::string s;
    if (!r.readString(s)) return false;
    req->set_reason(s);
    uint64_t exp = 0;
    if (!r.readU64(exp)) return false;
    req->set_expires_at(exp);
    return true;
}

bool decodeMoveUserRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_move_user_request();
    uint64_t uid = 0, cid = 0;
    if (!r.readU64(uid)) return false;
    if (!r.readU64(cid)) return false;
    req->set_user_id(uid);
    req->set_channel_id(cid);
    return true;
}

bool decodeRenameChannelRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_rename_channel();
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    req->set_channel_id(id);
    std::string s;
    if (!r.readString(s)) return false;
    req->set_new_name(s);
    return true;
}

bool decodeFileListRequest(CustomWireReader& r, control::ControlMessage& msg) {
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    msg.mutable_file_list_request()->set_channel_id(id);
    return true;
}

bool decodeFileUploadRequest(CustomWireReader& r, control::ControlMessage& msg) {
    auto* req = msg.mutable_file_upload_request();
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    req->set_channel_id(id);
    std::string s;
    if (!r.readString(s)) return false;
    req->set_filename(s);
    if (!r.readU64(id)) return false;
    req->set_file_size(id);
    return true;
}

bool decodeFileDeleteRequest(CustomWireReader& r, control::ControlMessage& msg) {
    uint64_t id = 0;
    if (!r.readU64(id)) return false;
    msg.mutable_file_delete_request()->set_file_id(id);
    return true;
}

using DecodeFunc = bool(*)(CustomWireReader&, control::ControlMessage&);

static const std::unordered_map<uint32_t, DecodeFunc> CASE_DECODERS = {
    {1,  decodeLoginRequest},
    {3,  decodeJoinChannelRequest},
    {4,  decodeLeaveChannelRequest},
    {5,  decodeCreateChannelRequest},
    {6,  decodeDeleteChannelRequest},
    {11, decodePttToggle},
    {12, decodeMuteToggle},
    {16, decodeUdpPingRequest},
    {20, decodeAdminAuthRequest},
    {32, decodeSetServerNameRequest},
    {30, decodeChatSendRequest},
    {24, decodeKickUserRequest},
    {26, decodeBanUserRequest},
    {28, decodeMoveUserRequest},
    {34, decodeRenameChannelRequest},
    {40, decodeFileListRequest},
    {42, decodeFileUploadRequest},
    {49, decodeFileDeleteRequest},
};

} // anonymous namespace

std::optional<control::ControlMessage> decodeCustomWirePayload(
    const uint8_t* data, uint32_t size) {
    if (size < 8) {
        NEVO_LOG_WARN("protocol", "Custom wire payload too small: {} bytes", size);
        return std::nullopt;
    }

    // Hex dump first 64 bytes of input
    {
        std::string hex;
        const char* digits = "0123456789abcdef";
        for (uint32_t i = 0; i < size && i < 64; ++i) {
            hex.push_back(digits[(data[i] >> 4) & 0xF]);
            hex.push_back(digits[data[i] & 0xF]);
            hex.push_back(' ');
        }
        if (size > 64) hex += "...";
        NEVO_LOG_INFO("protocol", "Decoding custom wire: {} bytes | hex={}", size, hex);
    }

    CustomWireReader r(data, size);

    uint32_t case_value = 0;
    uint32_t inner_len = 0;
    if (!r.readU32(case_value) || !r.readU32(inner_len)) {
        NEVO_LOG_WARN("protocol", "Failed to read custom wire header");
        return std::nullopt;
    }

    if (inner_len != r.remaining()) {
        NEVO_LOG_DEBUG("protocol", "Custom wire inner_len={} but remaining={}", inner_len, r.remaining());
    }

    auto it = CASE_DECODERS.find(case_value);
    if (it == CASE_DECODERS.end()) {
        NEVO_LOG_WARN("protocol", "Unknown custom wire case_value: {}", case_value);
        return std::nullopt;
    }

    control::ControlMessage msg;
    if (!it->second(r, msg)) {
        NEVO_LOG_WARN("protocol", "Failed to decode custom wire case={}", case_value);
        return std::nullopt;
    }

    NEVO_LOG_DEBUG("protocol", "Decoded custom wire format: case={}", case_value);
    return msg;
}

// ============================================================
// 自定义线格式编码（服务端 → Python 客户端）
// ============================================================

namespace {

class CustomWireWriter {
public:
    void writeU16(uint16_t value) {
        const auto p = static_cast<uint32_t>(buf_.size());
        buf_.resize(p + 2);
        std::memcpy(buf_.data() + p, &value, 2);
    }

    void writeU32(uint32_t value) {
        const auto p = static_cast<uint32_t>(buf_.size());
        buf_.resize(p + 4);
        std::memcpy(buf_.data() + p, &value, 4);
    }

    void writeU64(uint64_t value) {
        const auto p = static_cast<uint32_t>(buf_.size());
        buf_.resize(p + 8);
        std::memcpy(buf_.data() + p, &value, 8);
    }

    void writeBool(bool value) {
        buf_.push_back(value ? 1 : 0);
    }

    void writeString(const std::string& s) {
        writeU32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void writeBytes(const std::string& s) {
        writeU32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void writeRaw(const uint8_t* data, uint32_t size) {
        buf_.insert(buf_.end(), data, data + size);
    }

    void writeRawVec(const std::vector<uint8_t>& v) {
        buf_.insert(buf_.end(), v.begin(), v.end());
    }

    std::vector<uint8_t> take() { return std::move(buf_); }
    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

using EncodeFunc = std::vector<uint8_t>(*)(const control::ControlMessage&);

// ---- 响应类型编码器 ----

std::vector<uint8_t> encodeLoginResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.login_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    // UserInfo: [4B len][fields...] or [4B 0] for null
    if (r.has_user_info()) {
        const auto& u = r.user_info();
        // 先计算 user_data 大小: id(8) + username(4+len) + status(4) + muted(1) + deafened(1) + group_id(4)
        std::vector<uint8_t> user_data;
        {
            CustomWireWriter uw;
            uw.writeU64(u.id());
            uw.writeString(u.username());
            uw.writeU32(static_cast<uint32_t>(u.status()));
            uw.writeBool(u.muted());
            uw.writeBool(u.deafened());
            uw.writeU32(static_cast<uint32_t>(u.group_id()));
            user_data = uw.take();
        }
        w.writeU32(static_cast<uint32_t>(user_data.size()));
        w.writeRawVec(user_data);
    } else {
        w.writeU32(0); // user_data length = 0
    }
    w.writeString(r.session_token());
    w.writeBytes(r.server_public_key());
    w.writeString(r.key_exchange_method());
    w.writeBytes(r.encrypted_session_key());
    w.writeU32(r.owner_exists() ? 1 : 0);
    w.writeU16(static_cast<uint16_t>(r.server_udp_port()));
    w.writeU16(static_cast<uint16_t>(r.server_video_udp_port()));
    return w.take();
}

std::vector<uint8_t> encodeServerMessage(const control::ControlMessage& msg) {
    CustomWireWriter w;
    w.writeString(msg.server_message().text());
    return w.take();
}

std::vector<uint8_t> encodeUdpPingResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.udp_ping_response();
    w.writeU32(r.sequence());
    w.writeBool(r.udp_reachable());
    return w.take();
}

std::vector<uint8_t> encodeStunBindResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.stun_bind_response();
    w.writeU32(r.transaction_id());
    w.writeBytes(r.mapped_address());
    w.writeU32(r.nat_type());
    return w.take();
}

std::vector<uint8_t> encodeAdminAuthResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.admin_auth_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    w.writeString(r.message());
    return w.take();
}

std::vector<uint8_t> encodeSetAdminResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.set_admin_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    w.writeString(r.message());
    return w.take();
}

std::vector<uint8_t> encodeKickUserResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.kick_user_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    w.writeString(r.message());
    return w.take();
}

std::vector<uint8_t> encodeBanUserResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.ban_user_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    w.writeString(r.message());
    return w.take();
}

std::vector<uint8_t> encodeMoveUserResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.move_user_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    w.writeString(r.message());
    return w.take();
}

std::vector<uint8_t> encodeSetServerNameResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& r = msg.set_server_name_response();
    w.writeU32(static_cast<uint32_t>(r.result()));
    w.writeString(r.message());
    return w.take();
}

// ---- 通知/广播类型编码器（之前缺失）----

// 辅助：序列化 UserInfo 为 [id(u64)][username(str)][status(u32)][muted(bool)][deafened(bool)][group_id(u32)]
static std::vector<uint8_t> encodeUserInfo(const common::UserInfo& u) {
    CustomWireWriter w;
    w.writeU64(u.id());
    w.writeString(u.username());
    w.writeU32(static_cast<uint32_t>(u.status()));
    w.writeBool(u.muted());
    w.writeBool(u.deafened());
    w.writeU32(static_cast<uint32_t>(u.group_id()));
    return w.take();
}

// 辅助：序列化 ChannelInfo（匹配 Python serialize_channel_info）
// 格式: [id(u64)][name(str)][parent_id(u64)][children_count(u32)][[4B len][child]...][users_count(u32)][[4B len][user]...]
static std::vector<uint8_t> encodeChannelInfo(const common::ChannelInfo& ch) {
    // 先序列化 children
    std::vector<uint8_t> children_buf;
    {
        CustomWireWriter cw;
        cw.writeU32(static_cast<uint32_t>(ch.children_size()));
        for (const auto& child : ch.children()) {
            auto cd = encodeChannelInfo(child);  // 递归
            cw.writeU32(static_cast<uint32_t>(cd.size()));
            cw.writeRawVec(cd);
        }
        children_buf = cw.take();
    }
    // 序列化 users
    std::vector<uint8_t> users_buf;
    {
        CustomWireWriter uw;
        uw.writeU32(static_cast<uint32_t>(ch.users_size()));
        for (const auto& user : ch.users()) {
            auto ud = encodeUserInfo(user);
            uw.writeU32(static_cast<uint32_t>(ud.size()));
            uw.writeRawVec(ud);
        }
        users_buf = uw.take();
    }

    CustomWireWriter w;
    w.writeU64(ch.id());
    w.writeString(ch.name());
    w.writeU64(ch.parent_id());
    w.writeRawVec(children_buf);
    w.writeRawVec(users_buf);
    return w.take();
}

std::vector<uint8_t> encodeChannelListUpdate(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& update = msg.channel_list();
    w.writeU32(static_cast<uint32_t>(update.channels_size()));
    for (const auto& ch : update.channels()) {
        auto cd = encodeChannelInfo(ch);
        w.writeU32(static_cast<uint32_t>(cd.size()));
        w.writeRawVec(cd);
    }
    return w.take();
}

std::vector<uint8_t> encodeUserJoinedChannel(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& n = msg.user_joined();
    // UserInfo: [4B len][fields...] or [4B 0] for null
    if (n.has_user()) {
        const auto& u = n.user();
        std::vector<uint8_t> user_data;
        {
            CustomWireWriter uw;
            uw.writeU64(u.id());
            uw.writeString(u.username());
            uw.writeU32(static_cast<uint32_t>(u.status()));
            uw.writeBool(u.muted());
            uw.writeBool(u.deafened());
            uw.writeU32(static_cast<uint32_t>(u.group_id()));
            user_data = uw.take();
        }
        w.writeU32(static_cast<uint32_t>(user_data.size()));
        w.writeRawVec(user_data);
    } else {
        w.writeU32(0); // user data length = 0
    }
    w.writeU64(n.channel_id());
    return w.take();
}

std::vector<uint8_t> encodeUserLeftChannel(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& n = msg.user_left();
    w.writeU64(n.user_id());
    w.writeU64(n.channel_id());
    return w.take();
}

std::vector<uint8_t> encodeUserSpeaking(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& n = msg.user_speaking();
    w.writeU64(n.user_id());
    w.writeBool(n.speaking());
    return w.take();
}

std::vector<uint8_t> encodeMuteToggle(const control::ControlMessage& msg) {
    CustomWireWriter w;
    w.writeBool(msg.mute_toggle().muted());
    return w.take();
}

std::vector<uint8_t> encodeStunBindRequest(const control::ControlMessage& msg) {
    CustomWireWriter w;
    w.writeU32(msg.stun_bind_request().transaction_id());
    return w.take();
}

std::vector<uint8_t> encodeKeyRotationRequest(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& req = msg.key_rotation_request();
    w.writeBytes(req.new_server_public_key());
    w.writeU64(req.key_epoch());
    w.writeBytes(req.encrypted_session_key());
    return w.take();
}

std::vector<uint8_t> encodeKeyRotationResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& resp = msg.key_rotation_response();
    w.writeBytes(resp.new_client_public_key());
    w.writeU64(resp.key_epoch());
    return w.take();
}

std::vector<uint8_t> encodeChatBroadcast(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& cb = msg.chat_broadcast();
    w.writeU64(cb.sender_id());
    w.writeString(cb.sender_name());
    w.writeU64(cb.channel_id());
    w.writeString(cb.text());
    w.writeU64(cb.timestamp());
    return w.take();
}

std::vector<uint8_t> encodeFileListResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& resp = msg.file_list_response();
    w.writeU32(static_cast<uint32_t>(resp.entries_size()));
    for (const auto& entry : resp.entries()) {
        w.writeU64(entry.id());
        w.writeU64(entry.channel_id());
        w.writeU64(entry.uploader_id());
        w.writeString(entry.filename());
        w.writeU64(entry.file_size());
        w.writeU64(entry.upload_time());
    }
    return w.take();
}

std::vector<uint8_t> encodeFileUploadResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& resp = msg.file_upload_response();
    w.writeU32(static_cast<uint32_t>(resp.result()));
    w.writeString(resp.message());
    w.writeU64(resp.file_id());
    return w.take();
}

std::vector<uint8_t> encodeFileDeleteResponse(const control::ControlMessage& msg) {
    CustomWireWriter w;
    const auto& resp = msg.file_delete_response();
    w.writeU32(static_cast<uint32_t>(resp.result()));
    w.writeString(resp.message());
    return w.take();
}

// ============================================================
// C++ Protobuf case → Python wire format case
//
// Protobuf oneof 字段号与 Python MessageType 的 case_value 完全一致：
//   field 1=login_request  → wire 1,  field 7=channel_list → wire 7, etc.
// 无需任何映射转换，直接使用 payload_case() 值即可。
// ============================================================

} // anonymous namespace

// 编码器映射表：key = C++ Protobuf case (payload_case value)
static const std::unordered_map<int, EncodeFunc> CASE_ENCODERS = {
    {control::ControlMessage::kLoginResponse,          encodeLoginResponse},
    {control::ControlMessage::kServerMessage,          encodeServerMessage},
    {control::ControlMessage::kUdpPingResponse,        encodeUdpPingResponse},
    {control::ControlMessage::kStunBindResponse,       encodeStunBindResponse},
    {control::ControlMessage::kAdminAuthResponse,      encodeAdminAuthResponse},
    {control::ControlMessage::kSetAdminResponse,       encodeSetAdminResponse},
    {control::ControlMessage::kKickUserResponse,       encodeKickUserResponse},
    {control::ControlMessage::kBanUserResponse,        encodeBanUserResponse},
    {control::ControlMessage::kMoveUserResponse,       encodeMoveUserResponse},
    {control::ControlMessage::kSetServerNameResponse,  encodeSetServerNameResponse},
    // 通知/广播类型（之前缺失）
    {control::ControlMessage::kChannelList,      encodeChannelListUpdate},
    {control::ControlMessage::kUserJoined,       encodeUserJoinedChannel},
    {control::ControlMessage::kUserLeft,         encodeUserLeftChannel},
    {control::ControlMessage::kUserSpeaking,           encodeUserSpeaking},
    {control::ControlMessage::kMuteToggle,             encodeMuteToggle},
    {control::ControlMessage::kStunBindRequest,        encodeStunBindRequest},
    {control::ControlMessage::kKeyRotationRequest,     encodeKeyRotationRequest},
    {control::ControlMessage::kKeyRotationResponse,    encodeKeyRotationResponse},
    {control::ControlMessage::kChatBroadcast,          encodeChatBroadcast},
    {control::ControlMessage::kFileListResponse,        encodeFileListResponse},
    {control::ControlMessage::kFileUploadResponse,      encodeFileUploadResponse},
    {control::ControlMessage::kFileDeleteResponse,      encodeFileDeleteResponse},
};

std::vector<uint8_t> encodeCustomWirePayload(const control::ControlMessage& msg) {
    int proto_case = static_cast<int>(msg.payload_case());

    if (proto_case == control::ControlMessage::PAYLOAD_NOT_SET) {
        NEVO_LOG_WARN("protocol", "encodeCustomWirePayload: message has no payload");
        return {};
    }

    auto it = CASE_ENCODERS.find(proto_case);
    if (it == CASE_ENCODERS.end() || it->second == nullptr) {
        NEVO_LOG_WARN("protocol", "encodeCustomWirePayload: unsupported proto_case={}", proto_case);
        return {};
    }

    auto inner = it->second(msg);

    // Protobuf oneof 字段号与 Python wire case 完全一致，无需映射
    int wire_case = proto_case;

    CustomWireWriter w;
    w.writeU32(static_cast<uint32_t>(wire_case));
    w.writeU32(static_cast<uint32_t>(inner.size()));
    auto result = w.take();
    result.insert(result.end(), inner.begin(), inner.end());

    NEVO_LOG_INFO("protocol", "Encoded custom wire: case={}, inner={}, total={} bytes",
                  wire_case, inner.size(), result.size());
    return result;
}

} // namespace nevo
