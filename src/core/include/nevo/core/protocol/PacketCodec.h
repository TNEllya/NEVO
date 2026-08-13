#pragma once
/**
 * @file PacketCodec.h
 * @brief 包编解码工具
 *
 * ============================================================
 * 线格式（唯一运行时真源，全平台统一）
 * ============================================================
 * TCP 控制通道（C++ 服务端、C++ 客户端、Python 客户端、Android 客户端、
 * Web 网关共用）运行时统一使用【自定义小端 TLV 格式】，权威定义见
 * docs/protocol-wire-format.md：
 *   帧头：[4B 大端总长度][4B 大端消息类型][4B 大端 request_id]
 *   载荷：[4B 小端 case_value][4B 小端 inner_len][inner TLV 载荷]
 *         inner: string/bytes = [4B 小端 len][data]，uint32/64 小端，bool 1B
 * 跨语言一致性由金样互操作测试锁定：
 *   - tests/core_tests/TestPacketCodec.cpp（PacketCodecInteropTest）
 *   - src/client/gui_python/tests/test_wire_format.py
 * 任何格式变更必须先更新上述两处金样并同步三端实现，否则不允许提交。
 *
 * UDP 语音/视频通道：[变长 Protobuf 头部（VoicePacketHeader/VideoPacketHeader）]
 * + [加密载荷]，头部序列化为 protobuf（四端一致，无重复实现）。
 *
 * 下方 encodeTcpFrame/decodeTcpFramePayload 为【legacy Protobuf TCP 帧】
 * 编码路径：当前生产运行时未使用（仅测试覆盖），保留用于协议迁移备选
 * 与单元测试。新增代码一律使用 encodeCustomWirePayload/decodeCustomWirePayload。
 * ============================================================
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

// ============================================================
// 自定义线格式解码（兼容 Python 客户端）
// ============================================================
//
// Python 客户端使用自定义二进制格式（非 Protobuf）序列化消息：
//   载荷 = [4B LE: case_value] [4B LE: inner_payload_len] [inner_payload]
//   其中 inner_payload 使用小端序的 TLV 编码：
//     string  -> [4B LE len][UTF-8 data]
//     bytes   -> [4B LE len][raw data]
//     uint32  -> [4B LE value]
//     uint64  -> [8B LE value]
//     bool    -> [1B value]

/// 从自定义线格式载荷中解析出 ControlMessage（兼容 Python 客户端）
/// @param data 原始载荷数据（TcpConnection 读取的 payload）
/// @param size 数据大小
/// @return 解码成功返回 ControlMessage，失败返回 std::nullopt
std::optional<control::ControlMessage> decodeCustomWirePayload(
    const uint8_t* data, uint32_t size);

/// 将 ControlMessage 编码为自定义线格式载荷（兼容 Python 客户端）
/// @param msg 要编码的 ControlMessage
/// @return 编码后的字节流（空 vector 表示失败）
std::vector<uint8_t> encodeCustomWirePayload(const control::ControlMessage& msg);

} // namespace nevo
