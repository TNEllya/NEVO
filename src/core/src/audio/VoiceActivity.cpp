/**
 * @file VoiceActivity.cpp
 * @brief 语音活动检测实现
 */

#include "nevo/core/audio/VoiceActivity.h"
#include <cmath>
#include <algorithm>

namespace nevo {

VoiceActivity::VoiceActivity(const Config& config)
    : config_(config)
{
}

bool VoiceActivity::shouldTransmit(const float* pcm_data, uint32_t frame_size, bool opus_vad_result) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    // PTT 模式：按键即说话，不检测 VAD
    if (config_.ptt_enabled && ptt_active_.load(std::memory_order_relaxed)) {
        hangover_counter_ = config_.hangover_frames;
        speaking_.store(true, std::memory_order_relaxed);
        return true;
    }

    // PTT 释放后的挂起期
    if (config_.ptt_enabled && hangover_counter_ > 0) {
        --hangover_counter_;
        speaking_.store(hangover_counter_ > 0, std::memory_order_relaxed);
        return hangover_counter_ > 0;
    }

    // PTT 启用但未按下且无挂起 → 不传输
    if (config_.ptt_enabled) {
        speaking_.store(false, std::memory_order_relaxed);
        return false;
    }

    // VAD 模式
    if (!config_.vad_enabled) {
        // VAD 和 PTT 都未启用：始终发送
        speaking_.store(true, std::memory_order_relaxed);
        return true;
    }

    // 综合判断：Opus VAD + 能量阈值
    const float energy = computeRmsEnergy(pcm_data, frame_size);
    const bool energy_active = (energy >= config_.energy_threshold);
    const bool is_active = opus_vad_result && energy_active;

    if (is_active) {
        // 检测到语音
        hangover_counter_ = config_.hangover_frames;
        speaking_.store(true, std::memory_order_relaxed);
        return true;
    }

    // 挂起期：语音结束后保持发送几帧，避免尾音被切断
    if (hangover_counter_ > 0) {
        --hangover_counter_;
        speaking_.store(true, std::memory_order_relaxed);
        return true;
    }

    speaking_.store(false, std::memory_order_relaxed);
    return false;
}

void VoiceActivity::setPttActive(bool active) {
    ptt_active_.store(active, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (!active && hangover_counter_ == 0) {
            speaking_.store(false, std::memory_order_relaxed);
        }
    }
}

bool VoiceActivity::isPttActive() const {
    return ptt_active_.load(std::memory_order_relaxed);
}

void VoiceActivity::setVadEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.vad_enabled = enabled;
}

bool VoiceActivity::isVadEnabled() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.vad_enabled;
}

void VoiceActivity::setPttEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.ptt_enabled = enabled;
}

bool VoiceActivity::isPttEnabled() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.ptt_enabled;
}

void VoiceActivity::setEnergyThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.energy_threshold = std::clamp(threshold, 0.0f, 1.0f);
}

float VoiceActivity::energyThreshold() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.energy_threshold;
}

bool VoiceActivity::isSpeaking() const {
    return speaking_.load(std::memory_order_relaxed);
}

float VoiceActivity::computeRmsEnergy(const float* pcm_data, uint32_t frame_size) {
    if (!pcm_data || frame_size == 0) return 0.0f;

    double sum = 0.0;
    for (uint32_t i = 0; i < frame_size; ++i) {
        const double sample = static_cast<double>(pcm_data[i]);
        sum += sample * sample;
    }
    return static_cast<float>(std::sqrt(sum / frame_size));
}

} // namespace nevo
