/**
 * @file TcpVoiceTunnel.cpp
 * @brief TCP 语音隧道实现
 *
 * 实现 TCP 帧封装和流重组逻辑。
 *
 * TCP 帧格式详解：
 *   Offset  Size    Field
 *   0       4       total_length (big-endian) —— 整帧长度，含此字段
 *   4       1       type —— 帧类型，语音帧为 0xFF
 *   5       N       payload —— 语音数据（加密后的 VoicePacket）
 *
 * 重组状态机：
 *   ReadingHeader → 解析 5 字节帧头 → ReadingPayload
 *   ReadingPayload → 积累载荷数据 → 触发回调 → ReadingHeader
 *
 * 关键设计考虑：
 *   1. TCP 是字节流协议，一次 recv 可能包含多个帧或帧的一部分
 *   2. 帧头中的 total_length 包含自身（4 字节），所以载荷长度 = total_length - 5
 *   3. total_length 最小值为 5（仅帧头，无载荷），最大值为 5 + 1400 = 1405
 *   4. 帧类型 0xFF 为语音帧保留值，与 TCP 控制消息类型（1-19）不冲突
 */

#include "nevo/network/TcpVoiceTunnel.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <cstring>

#include "nevo/core/common/Logger.h"

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

TcpVoiceTunnel::TcpVoiceTunnel()
    : reassembly_state_(ReassemblyState::ReadingHeader)
    , expected_frame_length_(0)
    , frame_type_(0)
{
    // 预分配重组缓冲区，避免频繁 realloc
    // 初始大小设为单个最大帧的容量
    reassembly_buffer_.reserve(TCP_VOICE_FRAME_MAX_SIZE);

    NEVO_LOG_DEBUG("network", "TcpVoiceTunnel created");
}

TcpVoiceTunnel::~TcpVoiceTunnel() {
    NEVO_LOG_DEBUG("network", "TcpVoiceTunnel destroyed, {} bytes in buffer",
                   reassembly_buffer_.size());
}

// ============================================================
// 发送：封装语音帧
// ============================================================

std::vector<uint8_t> TcpVoiceTunnel::sendVoiceFrame(const uint8_t* data, size_t size) {
    // 验证载荷大小
    if (size > TCP_VOICE_MAX_PAYLOAD_SIZE) {
        NEVO_LOG_WARN("network",
                      "TcpVoiceTunnel::sendVoiceFrame: payload too large ({} > {}), truncating",
                      size, TCP_VOICE_MAX_PAYLOAD_SIZE);
        size = TCP_VOICE_MAX_PAYLOAD_SIZE;
    }

    if (size == 0) {
        NEVO_LOG_WARN("network", "TcpVoiceTunnel::sendVoiceFrame: empty payload, skipping");
        return {};
    }

    // 构造 TCP 帧：[4-byte total_length BE][1-byte type=0xFF][payload]
    uint32_t total_length = static_cast<uint32_t>(TCP_VOICE_FRAME_HEADER_SIZE + size);

    std::vector<uint8_t> frame(total_length, 0);

    // 写入 total_length（big-endian）
    uint32_t length_be = htonl(total_length);
    std::memcpy(frame.data(), &length_be, sizeof(length_be));

    // 写入帧类型
    frame[4] = TCP_VOICE_FRAME_TYPE;

    // 写入载荷
    std::memcpy(frame.data() + TCP_VOICE_FRAME_HEADER_SIZE, data, size);

    NEVO_LOG_TRACE("network",
                    "TcpVoiceTunnel::sendVoiceFrame: total_len={}, type=0x{:02X}, payload={}",
                    total_length, TCP_VOICE_FRAME_TYPE, size);

    return frame;
}

// ============================================================
// 接收：TCP 流重组
// ============================================================

void TcpVoiceTunnel::onTcpDataReceived(const uint8_t* data, size_t size) {
    if (size == 0 || data == nullptr) {
        return;
    }

    NEVO_LOG_TRACE("network",
                    "TcpVoiceTunnel::onTcpDataReceived: {} bytes, buffer was {} bytes",
                    size, reassembly_buffer_.size());

    // 将新数据追加到重组缓冲区
    reassembly_buffer_.insert(reassembly_buffer_.end(), data, data + size);

    // 防御性检查：重组缓冲区不应无限增长
    // 如果缓冲区超过 2 个最大帧的大小仍未解析出任何帧，说明数据可能损坏
    if (reassembly_buffer_.size() > TCP_VOICE_FRAME_MAX_SIZE * 2) {
        NEVO_LOG_WARN("network",
                      "TcpVoiceTunnel::onTcpDataReceived: buffer overflow ({} bytes), resetting",
                      reassembly_buffer_.size());
        reset();
        return;
    }

    // 尝试从缓冲区中提取所有完整帧
    tryParseFrames();
}

// ============================================================
// 状态查询与重置
// ============================================================

size_t TcpVoiceTunnel::pendingBytes() const {
    return reassembly_buffer_.size();
}

void TcpVoiceTunnel::reset() {
    reassembly_buffer_.clear();
    reassembly_state_ = ReassemblyState::ReadingHeader;
    expected_frame_length_ = 0;
    frame_type_ = 0;

    NEVO_LOG_DEBUG("network", "TcpVoiceTunnel::reset: reassembly state cleared");
}

// ============================================================
// 内部方法：帧解析
// ============================================================

void TcpVoiceTunnel::tryParseFrames() {
    // 循环解析，直到缓冲区数据不足以形成下一个帧
    while (true) {
        if (reassembly_state_ == ReassemblyState::ReadingHeader) {
            if (!parseFrameHeader()) {
                // 帧头数据不足，等待更多数据
                break;
            }
        }

        if (reassembly_state_ == ReassemblyState::ReadingPayload) {
            if (!parseFramePayload()) {
                // 载荷数据不足，等待更多数据
                break;
            }
        }
    }
}

bool TcpVoiceTunnel::parseFrameHeader() {
    // 帧头需要 5 字节
    if (reassembly_buffer_.size() < TCP_VOICE_FRAME_HEADER_SIZE) {
        return false;
    }

    // 解析 total_length（big-endian）
    uint32_t length_be;
    std::memcpy(&length_be, reassembly_buffer_.data(), sizeof(length_be));
    expected_frame_length_ = ntohl(length_be);

    // 解析帧类型
    frame_type_ = reassembly_buffer_[4];

    // 验证帧长度
    // 最小有效长度 = 5（帧头 + 空载荷）
    // 最大有效长度 = 5 + TCP_VOICE_MAX_PAYLOAD_SIZE
    if (expected_frame_length_ < TCP_VOICE_FRAME_HEADER_SIZE) {
        NEVO_LOG_ERROR("network",
                       "TcpVoiceTunnel: invalid frame length {} (min={}), discarding header byte",
                       expected_frame_length_, TCP_VOICE_FRAME_HEADER_SIZE);
        // 帧头损坏，丢弃第一个字节尝试重新同步
        reassembly_buffer_.erase(reassembly_buffer_.begin(),
                                  reassembly_buffer_.begin() + 1);
        return false;
    }

    if (expected_frame_length_ > TCP_VOICE_FRAME_MAX_SIZE) {
        NEVO_LOG_ERROR("network",
                       "TcpVoiceTunnel: frame length {} exceeds max {}, discarding header byte",
                       expected_frame_length_, TCP_VOICE_FRAME_MAX_SIZE);
        // 帧头损坏，丢弃第一个字节尝试重新同步
        reassembly_buffer_.erase(reassembly_buffer_.begin(),
                                  reassembly_buffer_.begin() + 1);
        return false;
    }

    // 验证帧类型
    if (frame_type_ != TCP_VOICE_FRAME_TYPE) {
        NEVO_LOG_WARN("network",
                      "TcpVoiceTunnel: unexpected frame type 0x{:02X} (expected 0xFF), "
                      "discarding header byte",
                      frame_type_);
        // 非语音帧类型，丢弃第一个字节尝试重新同步
        reassembly_buffer_.erase(reassembly_buffer_.begin(),
                                  reassembly_buffer_.begin() + 1);
        return false;
    }

    // 帧头有效，切换到读取载荷状态
    reassembly_state_ = ReassemblyState::ReadingPayload;

    NEVO_LOG_TRACE("network",
                    "TcpVoiceTunnel: parsed header, total_len={}, type=0x{:02X}, payload_len={}",
                    expected_frame_length_, frame_type_,
                    expected_frame_length_ - TCP_VOICE_FRAME_HEADER_SIZE);

    return true;
}

bool TcpVoiceTunnel::parseFramePayload() {
    // 检查是否已接收到完整帧
    if (reassembly_buffer_.size() < expected_frame_length_) {
        return false; // 数据不足，等待更多
    }

    // 计算载荷位置和大小
    const uint8_t* payload = reassembly_buffer_.data() + TCP_VOICE_FRAME_HEADER_SIZE;
    size_t payload_size = expected_frame_length_ - TCP_VOICE_FRAME_HEADER_SIZE;

    NEVO_LOG_DEBUG("network", "TcpVoiceTunnel: frame reassembled, payload={} bytes", payload_size);

    // 触发回调
    if (onVoiceFrame && payload_size > 0) {
        NEVO_LOG_TRACE("network",
                        "TcpVoiceTunnel: complete voice frame, payload={} bytes",
                        payload_size);
        onVoiceFrame(payload, payload_size);
    } else if (payload_size == 0) {
        NEVO_LOG_DEBUG("network", "TcpVoiceTunnel: voice frame with empty payload");
    } else {
        NEVO_LOG_WARN("network", "TcpVoiceTunnel: no onVoiceFrame callback set, frame dropped");
    }

    // 从缓冲区中移除已处理的帧
    reassembly_buffer_.erase(
        reassembly_buffer_.begin(),
        reassembly_buffer_.begin() + expected_frame_length_);

    // 切换回读取帧头状态
    reassembly_state_ = ReassemblyState::ReadingHeader;
    expected_frame_length_ = 0;
    frame_type_ = 0;

    return true;
}

} // namespace nevo
