#pragma once
/**
 * @file AudioMixer.h
 * @brief 多用户 PCM 音频混音器
 *
 * 将多个用户的 PCM 音频流混合为单路输出，用于 VoIP 群组通话。
 * 支持每用户独立音量控制，以及自动硬限幅（hard limiting）防止
 * 多人同时说话时的削波失真。
 *
 * 核心机制：
 *   1. 每帧周期开始时调用 reset() 清空累积缓冲区。
 *   2. 对每个活跃用户调用 addStream() 添加其 PCM 帧。
 *   3. 调用 mix() 生成最终混音输出。
 *
 * 硬限幅策略：
 *   - 先将所有用户的 PCM 帧按各自音量加权求和。
 *   - 检测混合后帧的峰值幅度。
 *   - 若峰值超过 1.0（满幅），对整帧进行等比衰减，使峰值刚好等于 1.0。
 *   - 最后对每个采样做 [-1.0, 1.0] 安全裁剪，确保输出不溢出。
 *   这种"brick-wall limiter"方式在保留各用户相对音量关系的同时，
 *   不会产生硬裁剪带来的严重失真。
 *
 * 线程安全：不需要。与 JitterBuffer 一样，由音频输出线程独占调用。
 *
 * 典型使用流程（每 20ms 周期）：
 *   mixer.reset();
 *   for (auto& [user_id, jitter_buffer] : user_streams) {
 *       auto frame = jitter_buffer.getNext();
 *       if (frame && !frame->lost) {
 *           mixer.addStream(user_id, frame->pcm_data.data(), frame->pcm_data.size());
 *       }
 *   }
 *   mixer.mix(output_buffer, frame_count);
 *   // 将 output_buffer 送入音频设备播放
 */

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "nevo/core/common/Types.h"

namespace nevo {

// ============================================================
// 音频混音器
// ============================================================
class AudioMixer {
public:
    /// 混音器配置
    struct Config {
        /// 最大同时说话人数。
        /// 超过此数量的用户帧将被忽略。
        /// 16 人同时说话在 VoIP 场景中已经非常罕见。
        uint32_t max_speakers = 16;

        /// 每帧采样数（用于内部缓冲区预分配）。
        /// 默认 960 = 20ms @ 48kHz mono。
        uint32_t frame_size = 960;
    };

        AudioMixer() : AudioMixer(Config{}) {}
    explicit AudioMixer(const Config& config);
    ~AudioMixer() = default;

    // 禁止拷贝
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // 允许移动
    AudioMixer(AudioMixer&&) noexcept = default;
    AudioMixer& operator=(AudioMixer&&) noexcept = default;

    // ============================================================
    // 核心接口
    // ============================================================

    /// 添加一个用户的 PCM 帧到当前混音周期。
    ///
    /// 在 mix() 之前调用，将指定用户的一帧 PCM 数据添加到混音累积缓冲区。
    /// 该用户的音量由 setUserVolume() 控制（默认 1.0）。
    /// 若当前活跃用户数已达 max_speakers，此调用将被忽略。
    ///
    /// @param user_id     用户唯一标识
    /// @param pcm_data    PCM float32 采样数据指针
    /// @param frame_count 该帧的采样数（应与 Config::frame_size 一致）
    void addStream(UserId user_id, const float* pcm_data, uint32_t frame_count);

    /// 生成最终混音输出帧。
    ///
    /// 将所有已添加的用户流按各自音量加权求和，然后执行硬限幅处理。
    /// 输出写入调用方提供的缓冲区。
    ///
    /// 硬限幅算法：
    ///   1. 加权求和所有活跃流
    ///   2. 找到混合帧的峰值幅度 peak
    ///   3. 若 peak > 1.0，对整帧乘以 (1.0 / peak) 进行等比衰减
    ///   4. 安全裁剪到 [-1.0, 1.0]
    ///
    /// @param output      输出 PCM float32 缓冲区（由调用方分配，至少 frame_count 个采样）
    /// @param frame_count 期望输出的采样数
    void mix(float* output, uint32_t frame_count);

    /// 设置指定用户的音量。
    ///
    /// @param user_id 用户唯一标识
    /// @param volume  音量系数，范围 [0.0, 2.0]
    ///                0.0 = 静音，1.0 = 原始音量，2.0 = 放大一倍
    ///                超出范围的值将被裁剪到 [0.0, 2.0]
    void setUserVolume(UserId user_id, float volume);

    /// 移除一个用户。
    /// 清除该用户的音量设置和缓冲区数据。
    /// @param user_id 用户唯一标识
    void removeUser(UserId user_id);

    /// 设置主音量（应用于混音输出的总音量）。
    ///
    /// 主音量在所有用户流混合并硬限幅后应用，
    /// 用于全局输出音量控制。
    ///
    /// @param volume 音量系数，范围 [0.0, 2.0]
    ///               0.0 = 静音，1.0 = 原始音量，2.0 = 放大一倍
    ///               超出范围的值将被裁剪到 [0.0, 2.0]
    void setVolume(float volume);

    /// 清空所有用户缓冲区（等同于 reset()）。
    /// 每个混音周期开始时调用（在 addInput() 之前）。
    void clear();

    /// 添加一个用户的 PCM 帧到当前混音周期（等同于 addStream()）。
    ///
    /// @param user_id     用户唯一标识
    /// @param pcm_data    PCM float32 采样数据指针
    /// @param frame_count 该帧的采样数
    void addInput(UserId user_id, const float* pcm_data, uint32_t frame_count);

    /// 重置混音器状态，清空所有用户缓冲区。
    /// 每个混音周期开始时调用（在 addStream() 之前）。
    void reset();

    // ============================================================
    // 状态查询
    // ============================================================

    /// 获取当前活跃用户数（当前周期已调用 addStream 的用户数）
    uint32_t activeStreamCount() const;

    /// 获取已注册音量设置的用户数
    uint32_t registeredUserCount() const;

    /// 获取指定用户的音量设置。若用户未注册，返回 1.0（默认音量）
    float getUserVolume(UserId user_id) const;

    /// 获取当前配置
    const Config& config() const { return config_; }

private:
    // ============================================================
    // 内部类型
    // ============================================================

    /// 每个用户的流状态
    struct StreamEntry {
        /// PCM 采样数据（每帧周期开始时由 addStream 填充）
        std::vector<float> pcm_data;

        /// 用户音量系数 [0.0, 2.0]
        float volume = 1.0f;

        /// 当前帧周期是否有数据（addStream 已调用）
        bool active = false;
    };

    // ============================================================
    // 内部方法
    // ============================================================

    /// 裁剪音量值到合法范围 [0.0, 2.0]
    static float clampVolume(float volume);

    /// 硬限幅：对混合后的帧进行峰值限制和裁剪
    /// @param mixed_buffer 混合后的采样缓冲区
    /// @param frame_count  采样数
    static void hardLimit(float* mixed_buffer, uint32_t frame_count);

    // ============================================================
    // 成员变量
    // ============================================================

    Config config_;

    /// 所有用户的流状态（包括音量设置和当前帧数据）
    /// 使用 UserId 作为 key，UserId 已有 std::hash 特化
    std::unordered_map<UserId, StreamEntry> streams_;

    /// 当前周期活跃用户数缓存（避免每次调用 activeStreamCount 遍历）
    uint32_t active_count_ = 0;

    /// 混音累积缓冲区（每帧复用，避免频繁分配）
    std::vector<float> mix_buffer_;

    /// 主音量系数 [0.0, 2.0]（应用于最终混音输出）
    float master_volume_ = 1.0f;
};

} // namespace nevo
