#pragma once
/**
 * @file Resampler.h
 * @brief 采样率转换器 - 基于 miniaudio 内置重采样器
 *
 * 主要用途：蓝牙耳机采样率转换（如 16kHz -> 48kHz）。
 * 蓝牙 HFP 协议通常使用 8kHz/16kHz，而 Opus 编码器要求 48kHz 输入。
 * 本类封装 miniaudio 的 ma_resampler，提供 RAII 管理和线程安全的重采样接口。
 *
 * 实时安全约束：
 *   - process() 可在实时音频回调中调用，无内存分配、无锁
 *   - initialize() / reset() 不可在实时线程中调用
 *   - ma_resampler 内部使用线性插值，O(n) 时间复杂度
 */

#include <cstdint>
#include <memory>
#include "nevo/core/common/Result.h"

// Include miniaudio for ma_resampler definition (needed by unique_ptr deleter)
#include <miniaudio.h>

namespace nevo {

class Resampler {
public:
    /// 重采样器配置
    struct Config {
        uint32_t input_sample_rate = 16000;     // 输入采样率（如蓝牙 HFP 的 16kHz）
        uint32_t output_sample_rate = 48000;    // 输出采样率（Opus 要求 48kHz）
        uint32_t channels = 1;                  // 声道数（VoIP 为单声道）
    };

        Resampler() : Resampler(Config{}) {}
    explicit Resampler(const Config& config);
    ~Resampler();

    // 禁止拷贝（ma_resampler 含内部状态，不可共享）
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    // 允许移动
    Resampler(Resampler&&) noexcept;
    Resampler& operator=(Resampler&&) noexcept;

    /// 初始化/重新初始化 ma_resampler
    /// 不可在实时线程中调用（内部会分配内存）
    /// @return 成功或错误
    Result<void> initialize();

    /// 处理一帧 PCM 数据，执行采样率转换
    /// 实时安全：无内存分配、无锁，可在音频回调中调用
    ///
    /// @param input         输入 PCM float32 采样缓冲区
    /// @param input_frames  输入帧数（采样数 = input_frames * channels）
    /// @param output        输出 PCM float32 缓冲区（调用者需确保足够大）
    /// @param output_frames 输出缓冲区可容纳的最大帧数
    /// @return 实际输出的帧数
    Result<uint32_t> process(const float* input,
                             uint32_t input_frames,
                             float* output,
                             uint32_t output_frames);

    /// 重置重采样器内部状态
    /// 在音频设备切换（如蓝牙耳机连接/断开）时调用
    /// 不可在实时线程中调用
    void reset();

    /// 获取配置
    const Config& config() const { return config_; }

    /// 更新输入采样率（如检测到新设备的采样率）
    /// 调用后需重新 initialize()
    void setInputSampleRate(uint32_t sample_rate);

    /// 更新输出采样率
    /// 调用后需重新 initialize()
    void setOutputSampleRate(uint32_t sample_rate);

    /// 估算给定输入帧数对应的输出帧数
    /// 用于预分配输出缓冲区
    uint32_t estimateOutputFrames(uint32_t input_frames) const;

    /// 检查重采样器是否已初始化
    bool isInitialized() const { return resampler_ != nullptr; }

private:
    /// 自定义 deleter 用于 unique_ptr 管理 ma_resampler
    struct MaResamplerDeleter {
        void operator()(ma_resampler* resampler) const;
    };

    std::unique_ptr<ma_resampler, MaResamplerDeleter> resampler_;
    Config config_;
    bool initialized_ = false;
};

} // namespace nevo
