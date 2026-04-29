/**
 * @file AudioMixer.cpp
 * @brief 多用户 PCM 音频混音器实现
 */

#include "nevo/core/audio/AudioMixer.h"
#include "nevo/core/common/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nevo {

// ============================================================
// 构造
// ============================================================

AudioMixer::AudioMixer(const Config& config)
    : config_(config)
    , mix_buffer_(config.frame_size, 0.0f)
{
    NEVO_LOG_INFO("audio",
                  "AudioMixer created: max_speakers={}, frame_size={}",
                  config_.max_speakers, config_.frame_size);
}

// ============================================================
// addStream - 添加一个用户的 PCM 帧到当前混音周期
// ============================================================
void AudioMixer::addStream(UserId user_id, const float* pcm_data, uint32_t frame_count)
{
    if (!pcm_data) {
        NEVO_LOG_WARN("audio", "AudioMixer::addStream: null pcm_data for user={}",
                      user_id.value);
        return;
    }

    // ---- 检查活跃流数量上限 ----
    // 先确认该用户是否已活跃（已调用过 addStream），若已活跃则不计入新增
    auto it = streams_.find(user_id);
    const bool already_active = (it != streams_.end() && it->second.active);

    if (!already_active && active_count_ >= config_.max_speakers) {
        // 已达最大同时说话人数，忽略此流
        NEVO_LOG_WARN("audio",
                      "AudioMixer: max_speakers ({}) reached, ignoring user={}",
                      config_.max_speakers, user_id.value);
        return;
    }

    // ---- 获取或创建用户流条目 ----
    // 若用户首次出现，会自动创建 StreamEntry（音量默认 1.0）
    auto& entry = streams_[user_id];

    // ---- 存储 PCM 数据 ----
    // 复制 PCM 数据到用户条目中。每帧周期只会保留最新的一帧。
    entry.pcm_data.assign(pcm_data, pcm_data + frame_count);

    // ---- 标记为活跃 ----
    if (!entry.active) {
        entry.active = true;
        ++active_count_;
        NEVO_LOG_INFO("audio", "AudioMixer: addInput user={}, active_count={}",
                      user_id.value, active_count_);
    }
}

// ============================================================
// mix - 生成最终混音输出帧
// ============================================================
void AudioMixer::mix(float* output, uint32_t frame_count)
{
    if (!output) {
        NEVO_LOG_ERROR("audio", "AudioMixer::mix: null output buffer");
        return;
    }

    // ---- 第一步：清零混音累积缓冲区 ----
    // 确保缓冲区大小足够，并填零
    if (mix_buffer_.size() < frame_count) {
        mix_buffer_.resize(frame_count, 0.0f);
    }
    std::memset(mix_buffer_.data(), 0, frame_count * sizeof(float));

    // ---- 第二步：加权求和所有活跃流 ----
    // 对每个活跃用户的 PCM 帧乘以其音量系数后累加到混合缓冲区。
    // 这是混音的核心：多路信号在时域上的线性叠加。
    for (auto& [user_id, entry] : streams_) {
        if (!entry.active || entry.pcm_data.empty()) {
            continue;
        }

        // 确保帧长度匹配（防御性编程）
        const uint32_t samples_to_mix = std::min(
            frame_count,
            static_cast<uint32_t>(entry.pcm_data.size())
        );

        const float volume = entry.volume;

        // 按音量加权累加
        for (uint32_t i = 0; i < samples_to_mix; ++i) {
            mix_buffer_[i] += entry.pcm_data[i] * volume;
        }
    }

    // ---- 第三步：应用主音量 ----
    // 主音量在所有用户流加权求和后、硬限幅前应用。
    // 这样硬限幅可以捕获主音量放大导致的削波。
    if (master_volume_ != 1.0f) {
        for (uint32_t i = 0; i < frame_count; ++i) {
            mix_buffer_[i] *= master_volume_;
        }
    }

    // ---- 第四步：硬限幅处理 ----
    // 当多人同时说话时，叠加后的幅度可能远超 [-1.0, 1.0]。
    // 硬限幅确保输出不会溢出，同时尽量保留各用户的相对音量关系。
    hardLimit(mix_buffer_.data(), frame_count);

    // ---- 第五步：将结果复制到输出缓冲区 ----
    std::memcpy(output, mix_buffer_.data(), frame_count * sizeof(float));
}

// ============================================================
// setUserVolume - 设置指定用户的音量
// ============================================================
void AudioMixer::setUserVolume(UserId user_id, float volume)
{
    auto& entry = streams_[user_id];
    entry.volume = clampVolume(volume);

    NEVO_LOG_DEBUG("audio", "AudioMixer: set volume for user={} to {:.2f}",
                   user_id.value, entry.volume);
}

// ============================================================
// removeUser - 移除一个用户
// ============================================================
void AudioMixer::removeUser(UserId user_id)
{
    auto it = streams_.find(user_id);
    if (it != streams_.end()) {
        // 如果该用户当前活跃，减少活跃计数
        if (it->second.active) {
            --active_count_;
        }
        streams_.erase(it);
        NEVO_LOG_INFO("audio", "AudioMixer: removeInput user={}, active_count={}",
                      user_id.value, active_count_);
    }
}

// ============================================================
// setVolume - 设置主音量
// ============================================================
void AudioMixer::setVolume(float volume)
{
    master_volume_ = clampVolume(volume);

    NEVO_LOG_DEBUG("audio", "AudioMixer: master volume set to {:.2f}",
                   master_volume_);
}

// ============================================================
// clear - 清空所有用户缓冲区（等同于 reset）
// ============================================================
void AudioMixer::clear()
{
    reset();
}

// ============================================================
// addInput - 添加一个用户的 PCM 帧（等同于 addStream）
// ============================================================
void AudioMixer::addInput(UserId user_id, const float* pcm_data, uint32_t frame_count)
{
    addStream(user_id, pcm_data, frame_count);
}

// ============================================================
// reset - 重置混音器状态
// ============================================================
void AudioMixer::reset()
{
    // 清空所有用户的活跃状态和 PCM 数据，但保留音量设置。
    // 这样下一个混音周期不需要重新设置音量。
    for (auto& [user_id, entry] : streams_) {
        entry.active = false;
        entry.pcm_data.clear();
    }
    active_count_ = 0;

    // 清零混合缓冲区
    std::fill(mix_buffer_.begin(), mix_buffer_.end(), 0.0f);
}

// ============================================================
// 状态查询
// ============================================================

uint32_t AudioMixer::activeStreamCount() const
{
    return active_count_;
}

uint32_t AudioMixer::registeredUserCount() const
{
    return static_cast<uint32_t>(streams_.size());
}

float AudioMixer::getUserVolume(UserId user_id) const
{
    auto it = streams_.find(user_id);
    if (it != streams_.end()) {
        return it->second.volume;
    }
    // 未注册的用户返回默认音量
    return 1.0f;
}

// ============================================================
// clampVolume - 裁剪音量到合法范围
// ============================================================
float AudioMixer::clampVolume(float volume)
{
    // 音量范围 [0.0, 2.0]：
    //   0.0 = 完全静音
    //   1.0 = 原始音量（默认）
    //   2.0 = 最大放大（4x 功率，配合限幅器仍可安全输出）
    if (volume < 0.0f) {
        return 0.0f;
    }
    if (volume > 2.0f) {
        return 2.0f;
    }
    return volume;
}

// ============================================================
// hardLimit - 硬限幅算法
// ============================================================
void AudioMixer::hardLimit(float* mixed_buffer, uint32_t frame_count)
{
    // ---- 阶段一：峰值检测 ----
    // 找到混合帧中绝对值最大的采样点。
    // 使用绝对值是因为音频信号有正有负，限幅应对称处理。
    float peak = 0.0f;
    for (uint32_t i = 0; i < frame_count; ++i) {
        const float abs_val = std::fabs(mixed_buffer[i]);
        if (abs_val > peak) {
            peak = abs_val;
        }
    }

    // ---- 阶段二：等比衰减（brick-wall limiting） ----
    // 如果峰值超过 1.0，对整帧进行等比衰减。
    // 等比衰减的优点：
    //   - 保留各采样之间的相对关系，不产生谐波失真
    //   - 所有采样均匀衰减，听感自然
    //   - 与简单硬裁剪相比，避免了削波带来的刺耳噪声
    //
    // 缺点：
    //   - 瞬态响应：突然的峰值会导致整帧音量骤降
    //   - 但在 VoIP 场景中，帧长仅 20ms，这种瞬态几乎不可感知
    if (peak > 1.0f) {
        NEVO_LOG_DEBUG("audio", "AudioMixer: hard limiting activated, peak={:.2f}", peak);
        const float gain = 1.0f / peak;
        for (uint32_t i = 0; i < frame_count; ++i) {
            mixed_buffer[i] *= gain;
        }
    }

    // ---- 阶段三：安全裁剪 ----
    // 最终安全网：确保没有任何采样超出 [-1.0, 1.0] 范围。
    // 在正常情况下（等比衰减已将峰值限制在 1.0），此步骤不会改变数据。
    // 但在极端情况下（如浮点精度问题），此步骤防止 DAC 输出溢出。
    for (uint32_t i = 0; i < frame_count; ++i) {
        if (mixed_buffer[i] > 1.0f) {
            mixed_buffer[i] = 1.0f;
        } else if (mixed_buffer[i] < -1.0f) {
            mixed_buffer[i] = -1.0f;
        }
    }
}

} // namespace nevo
