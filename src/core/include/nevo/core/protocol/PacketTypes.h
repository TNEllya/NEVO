#pragma once
/**
 * @file PacketTypes.h
 * @brief 包类型枚举定义
 */

#include <cstdint>

namespace nevo {

// ============================================================
// TCP 控制消息类型（与 proto/control.proto 中的 MessageType 对应）
// ============================================================
enum class ControlMessageType : uint32_t {
    Unknown = 0,
    LoginRequest = 1,
    LoginResponse = 2,
    JoinChannel = 3,
    LeaveChannel = 4,
    CreateChannel = 5,
    DeleteChannel = 6,
    ChannelList = 7,
    UserJoined = 8,
    UserLeft = 9,
    UserSpeaking = 10,
    PttToggle = 11,
    MuteToggle = 12,
    ServerMessage = 13,
    StunBindRequest = 14,
    StunBindResponse = 15,
    UdpPingRequest = 16,
    UdpPingResponse = 17,
    KeyRotationRequest = 18,
    KeyRotationResponse = 19,
    AdminAuthRequest = 20,
    AdminAuthResponse = 21,
    SetAdminRequest = 22,
    SetAdminResponse = 23,
    KickUserRequest = 24,
    KickUserResponse = 25,
    BanUserRequest = 26,
    BanUserResponse = 27,
    MoveUserRequest = 28,
    MoveUserResponse = 29,
    ChatSend = 30,
    ChatBroadcast = 31,
    SetServerNameRequest = 32,
    SetServerNameResponse = 33,
    RenameChannel = 34,
    RenameChannelResponse = 35,
    FileListRequest = 40,
    FileListResponse = 41,
    FileUploadRequest = 42,
    FileUploadResponse = 43,
    FileUploadChunkRequest = 44,
    FileUploadChunkAck = 45,
    FileDownloadRequest = 46,
    FileDownloadResponse = 47,
    FileDeleteRequest = 49,
    FileDeleteResponse = 50,
    ScreenShareStart = 60,
    ScreenShareStop = 61,
    ScreenShareState = 62,
    BindOwnerRequest = 70,
    BindOwnerResponse = 71,
    VideoCallRequest = 80,
    VideoCallResponse = 81,
    VideoCallHangup = 82,
    VideoCallProfileUpdate = 83,
};

// ============================================================
// TCP 帧格式常量
// ============================================================
/// TCP 帧头固定大小：4字节长度 + 4字节类型 + 4字节 request_id = 12字节
inline constexpr uint32_t TCP_HEADER_SIZE = 12;

/// TCP 帧最大载荷大小（1MB，防止恶意大包）
inline constexpr uint32_t TCP_MAX_PAYLOAD_SIZE = 1024 * 1024;

// ============================================================
// UDP 语音包常量
// ============================================================
/// UDP 包最大大小（MTU 安全值）
inline constexpr uint32_t UDP_MAX_PACKET_SIZE = 1400;

/// Opus 编码最大帧大小
inline constexpr uint32_t OPUS_MAX_FRAME_SIZE = 4000;

/// @deprecated AES-GCM Nonce 长度（12字节）——历史遗留常量。
/// 语音加密实际使用 XChaCha20-Poly1305（24 字节 nonce，见 network/VoiceCrypto.h 的
/// XCHACHA_NONCE_SIZE）。本常量仅为早期协议设计保留，新代码不得使用。
inline constexpr uint32_t AES_GCM_NONCE_SIZE = 12;

/// @deprecated AES-GCM 认证标签长度（16字节）——历史遗留常量。
/// 实际使用的 Poly1305 认证标签同为 16 字节（POLY1305_TAG_SIZE），数值巧合一致，
/// 但算法不同。新代码应使用 VoiceCrypto.h 中的 POLY1305_TAG_SIZE。
inline constexpr uint32_t AES_GCM_TAG_SIZE = 16;

/// 密钥轮换间隔（秒）
inline constexpr uint32_t KEY_ROTATION_INTERVAL_SEC = 600;  // 10 分钟

/// 旧密钥保留窗口期（秒）
inline constexpr uint32_t KEY_OVERLAP_WINDOW_SEC = 20;

} // namespace nevo
