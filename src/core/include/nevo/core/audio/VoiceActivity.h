#pragma once
/**
 * @file VoiceActivity.h
 * @brief 语音活动检测（VAD）和按键说话（PTT）
 *
 * 两种模式：
 *   1. VAD：基于 Opus 内置 VAD + 能量阈值自动检测
 *   2. PTT：用户手动按键控制
 *
 * 当两者同时启用时，PTT 优先级高于 VAD。
 */

#include <cstdint>
#include <atomic>
#include <mutex>

namespace nevo {

class VoiceActivity {
public:
    /// VAD 灵敏度配置
    struct Config {
        bool vad_enabled = true;        // 是否启用 VAD
        bool ptt_enabled = false;       // 是否启用 PTT
        float energy_threshold = 0.01f; // PCM 能量阈值（归一化 0.0~1.0）
        uint32_t hangover_frames = 10;  // 语音结束后保持发送的帧数（~200ms）
    };

        VoiceActivity() : VoiceActivity(Config{}) {}
    explicit VoiceActivity(const Config& config);

    /// 检测当前帧是否应发送
    /// @param pcm_data PCM float32 采样数据
    /// @param frame_size 帧大小（采样数）
    /// @param opus_vad_result Opus 编码器的 VAD 结果
    /// @return true=应发送此帧
    bool shouldTransmit(const float* pcm_data, uint32_t frame_size, bool opus_vad_result);

    /// 手动设置 PTT 按键状态（从 UI 线程调用）
    void setPttActive(bool active);

    /// 查询 PTT 当前状态
    bool isPttActive() const;

    /// 启用/禁用 VAD
    void setVadEnabled(bool enabled);
    bool isVadEnabled() const;

    /// 启用/禁用 PTT
    void setPttEnabled(bool enabled);
    bool isPttEnabled() const;

    /// 设置 VAD 能量阈值
    void setEnergyThreshold(float threshold);
    float energyThreshold() const;

    /// 查询当前是否处于"正在说话"状态
    bool isSpeaking() const;

private:
    /// 计算 PCM 帧的 RMS 能量
    static float computeRmsEnergy(const float* pcm_data, uint32_t frame_size);

    mutable std::mutex config_mutex_;   ///< 保护 config_ 和 hangover_counter_ 的跨线程访问
    Config config_;                     ///< 在 config_mutex_ 保护下访问
    std::atomic<bool> ptt_active_{false};
    std::atomic<bool> speaking_{false};
    uint32_t hangover_counter_ = 0;     ///< 在 config_mutex_ 保护下访问
};

} // namespace nevo
