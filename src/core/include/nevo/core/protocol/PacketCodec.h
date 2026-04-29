#pragma once
/**
 * @file PacketCodec.h
 * @brief 包编解码工具
 *
 * 提供 TCP 帧和 UDP 语音包的序列化/反序列化功能。
 * TCP 帧格式：[4字节总长度][4字节消息类型][4字节request_id][Protobuf载荷]
 * UDP 帧格式：[变长Protobuf头部][加密Opus载荷]
 */

#include "nevo/core/protocol/PacketTypes.h"
#include <cstdint>
#include <vector>
#include <optional>
#include <string>
#include <utility>

// Protobuf 生成代码的前向声明
namespace nevo::control { class ControlMessage; }
namespace nevo::voice { class VoicePacketHeader; }

namespace nevo {

// ============================================================
// TCP 帧编解码
// ============================================================

/// TCP 帧头
struct TcpFrameHeader {
    uint32_t payload_length;   // 载荷字节长度
    uint32_t message_type;     // ControlMessageType 枚举值
    uint32_t request_id;       // 请求-响应关联ID
};

/// 编码 TCP 帧：头部 + Protobuf 序列化后的 ControlMessage
/// @return 完整的 TCP 帧字节流
std::vector<uint8_t> encodeTcpFrame(const control::ControlMessage& msg,
                                     ControlMessageType type,
                                     uint32_t request_id = 0);

/// 从字节流中解码 TCP 帧头
/// @param data 数据指针
/// @param size 可用数据大小
/// @return 解码成功返回帧头，数据不足返回 std::nullopt
std::optional<TcpFrameHeader> decodeTcpFrameHeader(const uint8_t* data, uint32_t size);

/// 从字节流中解码 TCP 帧载荷（ControlMessage）
/// @param header 已解码的帧头
/// @param payload_data 载荷数据指针（紧跟帧头之后）
/// @return 解码成功返回 ControlMessage，失败返回 std::nullopt
std::optional<control::ControlMessage> decodeTcpFramePayload(
    const TcpFrameHeader& header,
    const uint8_t* payload_data);

// ============================================================
// UDP 语音包编解码
// ============================================================

/// 编码 UDP 语音包：Protobuf 头部 + 加密 Opus 载荷
/// @return 完整的 UDP 包字节流
std::vector<uint8_t> encodeVoicePacket(const voice::VoicePacketHeader& header,
                                        const uint8_t* opus_payload,
                                        uint32_t payload_size);

/// 解码 UDP 语音包头
/// @param data 数据指针
/// @param size 可用数据大小
/// @param out_header_size [out] 头部占用的字节数
/// @return 解码成功返回 VoicePacketHeader，失败返回 std::nullopt
std::optional<voice::VoicePacketHeader> decodeVoicePacketHeader(
    const uint8_t* data,
    uint32_t size,
    uint32_t& out_header_size);

/// 获取 UDP 语音包的加密载荷部分
/// @param data 完整 UDP 包数据
/// @param header_size 头部字节数（由 decodeVoicePacketHeader 输出）
/// @param total_size 完整包总大小
/// @return 载荷数据指针和大小
std::pair<const uint8_t*, uint32_t> getVoicePayload(
    const uint8_t* data,
    uint32_t header_size,
    uint32_t total_size);

// ============================================================
// 工具函数
// ============================================================

/// 获取 ControlMessage 的消息类型
ControlMessageType getControlMessageType(const control::ControlMessage& msg);

/// ControlMessageType 转字符串（用于日志）
const char* controlMessageTypeToString(ControlMessageType type);

} // namespace nevo
