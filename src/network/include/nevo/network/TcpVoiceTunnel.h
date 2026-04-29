#pragma once
/**
 * @file TcpVoiceTunnel.h
 * @brief TCP 语音隧道——当 UDP 被阻断时，通过 TCP 传输语音数据
 *
 * 在 NAT 类型为 Blocked 或 Symmetric（且 TURN 不可用）的场景下，
 * UDP 语音数据无法直接传输。TcpVoiceTunnel 将语音帧封装在 TCP 流中传输，
 * 确保语音通信的可用性。
 *
 * TCP 帧格式：
 *   [4-byte total_length (big-endian)][1-byte type=0xFF][payload...]
 *
 *   - total_length: 整帧长度（含 4 字节长度字段 + 1 字节类型 + 载荷）
 *   - type: 帧类型标识，语音帧固定为 0xFF
 *   - payload: VoicePacket 数据（含加密后的语音载荷）
 *
 * TCP 流重组：
 *   由于 TCP 是字节流协议，需要在接收端重组帧边界。
 *   onTcpDataReceived() 方法处理部分帧的缓存和拼接，
 *   完整帧组装后通过 onVoiceFrame 回调输出。
 *
 * 典型用法：
 * @code
 *   TcpVoiceTunnel tunnel;
 *   tunnel.onVoiceFrame = [](const uint8_t* data, size_t size) {
 *       // 处理接收到的语音帧
 *   };
 *
 *   // 发送端：封装语音数据
 *   tunnel.sendVoiceFrame(voice_data, voice_size);
 *
 *   // 接收端：喂入 TCP 数据流
 *   tunnel.onTcpDataReceived(tcp_buffer, bytes_received);
 * @endcode
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {

// ============================================================
// TCP 语音隧道常量
// ============================================================

/// TCP 语音帧类型标识
inline constexpr uint8_t TCP_VOICE_FRAME_TYPE = 0xFF;

/// TCP 帧头大小：4 字节长度 + 1 字节类型 = 5 字节
inline constexpr size_t TCP_VOICE_FRAME_HEADER_SIZE = 5;

/// TCP 帧最大载荷大小（与 UDP 包对齐，1400 字节）
inline constexpr size_t TCP_VOICE_MAX_PAYLOAD_SIZE = UDP_MAX_PACKET_SIZE;

/// TCP 帧最大总大小（头 + 载荷）
inline constexpr size_t TCP_VOICE_FRAME_MAX_SIZE =
    TCP_VOICE_FRAME_HEADER_SIZE + TCP_VOICE_MAX_PAYLOAD_SIZE;

// ============================================================
// TcpVoiceTunnel 类
// ============================================================

/**
 * @class TcpVoiceTunnel
 * @brief TCP 语音隧道封装
 *
 * 将离散的语音帧封装到 TCP 字节流中传输，
 * 并在接收端从 TCP 流中重组出完整的语音帧。
 *
 * 线程安全：
 *   - sendVoiceFrame() 不是线程安全的，需在单线程中调用
 *   - onTcpDataReceived() 不是线程安全的，需在单线程中调用
 *   - 如果发送和接收在不同线程，需外部同步
 */
class TcpVoiceTunnel {
public:
    /// 构造函数
    TcpVoiceTunnel();

    /// 析构函数
    ~TcpVoiceTunnel();

    // ----- 禁止拷贝，允许移动 -----
    TcpVoiceTunnel(const TcpVoiceTunnel&) = delete;
    TcpVoiceTunnel& operator=(const TcpVoiceTunnel&) = delete;
    TcpVoiceTunnel(TcpVoiceTunnel&&) noexcept = default;
    TcpVoiceTunnel& operator=(TcpVoiceTunnel&&) noexcept = default;

    // ============================================================
    // 回调
    // ============================================================

    /**
     * @brief 语音帧接收回调
     *
     * 当从 TCP 流中重组出一个完整的语音帧时触发。
     * data 指向帧的 payload 部分（不含 TCP 帧头）。
     *
     * @param data  帧载荷数据指针
     * @param size  载荷数据大小
     */
    std::function<void(const uint8_t* data, size_t size)> onVoiceFrame;

    // ============================================================
    // 发送
    // ============================================================

    /**
     * @brief 封装语音数据为 TCP 帧并发送
     *
     * 帧格式：[4-byte total_length BE][1-byte type=0xFF][payload]
     *
     * total_length = 4 (length field) + 1 (type) + size (payload)
     *              = 5 + size
     *
     * @param data 语音数据指针（加密后的语音载荷）
     * @param size 数据大小
     * @return std::vector<uint8_t> 封装后的 TCP 帧数据（可直接写入 TCP socket）
     */
    std::vector<uint8_t> sendVoiceFrame(const uint8_t* data, size_t size);

    // ============================================================
    // 接收
    // ============================================================

    /**
     * @brief 处理接收到的 TCP 数据
     *
     * 将 TCP 字节流数据喂入重组缓冲区，自动拼接并提取完整帧。
     * 每提取到一个完整的语音帧，触发 onVoiceFrame 回调。
     *
     * 支持以下场景：
     *   - 一次 TCP 接收包含多个完整帧
     *   - 一次 TCP 接收只包含一个帧的部分数据
     *   - 帧边界与 TCP 接收边界不对齐
     *
     * @param data TCP 接收到的数据
     * @param size 数据大小
     */
    void onTcpDataReceived(const uint8_t* data, size_t size);

    // ============================================================
    // 状态查询与重置
    // ============================================================

    /**
     * @brief 获取重组缓冲区中待处理的数据大小
     * @return 缓冲区中的字节数
     */
    size_t pendingBytes() const;

    /**
     * @brief 重置重组状态
     *
     * 清空重组缓冲区，丢弃所有未完成的帧数据。
     * 通常在 TCP 连接断开或重连时调用。
     */
    void reset();

private:
    // ============================================================
    // 重组状态
    // ============================================================

    /// 重组阶段枚举
    enum class ReassemblyState {
        ReadingHeader,   ///< 正在读取帧头（5 字节：length + type）
        ReadingPayload,  ///< 正在读取帧载荷
    };

    /// 当前重组状态
    ReassemblyState reassembly_state_ = ReassemblyState::ReadingHeader;

    /// 重组缓冲区——缓存跨 TCP 接收边界的帧数据
    std::vector<uint8_t> reassembly_buffer_;

    /// 当前帧的预期总长度（从帧头解析）
    uint32_t expected_frame_length_ = 0;

    /// 当前帧的类型（从帧头解析）
    uint8_t frame_type_ = 0;

    // ============================================================
    // 内部方法
    // ============================================================

    /**
     * @brief 尝试从重组缓冲区中提取完整帧
     *
     * 反复尝试解析帧头和载荷，直到缓冲区数据不足以形成完整帧。
     * 每提取到一个完整的语音帧，触发 onVoiceFrame 回调。
     */
    void tryParseFrames();

    /**
     * @brief 解析帧头（5 字节）
     *
     * 从缓冲区头部读取 4 字节长度和 1 字节类型。
     * 验证长度范围和帧类型合法性。
     *
     * @return true 成功解析帧头并进入 ReadingPayload 状态
     * @return false 缓冲区数据不足或帧头无效
     */
    bool parseFrameHeader();

    /**
     * @brief 解析帧载荷
     *
     * 当缓冲区中积累了足够的载荷数据时，提取完整载荷，
     * 触发 onVoiceFrame 回调，然后回到 ReadingHeader 状态。
     *
     * @return true 成功提取一个完整帧
     * @return false 载荷数据不足，等待更多数据
     */
    bool parseFramePayload();
};

} // namespace nevo
