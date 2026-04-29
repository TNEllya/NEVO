/**
 * @file Resampler.cpp
 * @brief 采样率转换器实现 - 基于 miniaudio 内置重采样器
 */

#include "nevo/core/audio/Resampler.h"
#include "nevo/core/common/Logger.h"

// miniaudio 通过 CMake target 链接，无需定义 MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace nevo {

// ============================================================
// MaResamplerDeleter
// ============================================================
void Resampler::MaResamplerDeleter::operator()(ma_resampler* resampler) const {
    if (resampler) {
        ma_resampler_uninit(resampler, nullptr);
        delete resampler;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================
Resampler::Resampler(const Config& config)
    : config_(config)
{
}

Resampler::~Resampler() = default;

Resampler::Resampler(Resampler&& other) noexcept
    : resampler_(std::move(other.resampler_))
    , config_(other.config_)
    , initialized_(other.initialized_)
{
    other.initialized_ = false;
}

Resampler& Resampler::operator=(Resampler&& other) noexcept {
    if (this != &other) {
        resampler_ = std::move(other.resampler_);
        config_ = other.config_;
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

// ============================================================
// 初始化
// ============================================================
Result<void> Resampler::initialize() {
    // 如果已初始化，先清理旧状态
    if (resampler_) {
        resampler_.reset();
        initialized_ = false;
    }

    // 验证采样率参数
    if (config_.input_sample_rate == 0 || config_.output_sample_rate == 0) {
        NEVO_LOG_ERROR("audio", "Resampler: invalid sample rates (in={}, out={})",
                       config_.input_sample_rate, config_.output_sample_rate);
        return Err<void>(ResultCode::InvalidRequest,
                         "Resampler: sample rate cannot be zero");
    }

    if (config_.channels == 0 || config_.channels > 2) {
        NEVO_LOG_ERROR("audio", "Resampler: invalid channel count: {}", config_.channels);
        return Err<void>(ResultCode::InvalidRequest,
                         "Resampler: channels must be 1 or 2");
    }

    // 分配 ma_resampler 结构体内存
    // 注意：ma_resampler 是一个较大的 POD 结构体，在堆上分配避免栈溢出
    auto* raw = new (std::nothrow) ma_resampler{};
    if (!raw) {
        NEVO_LOG_ERROR("audio", "Resampler: failed to allocate ma_resampler");
        return Err<void>(ResultCode::DeviceNotAvailable,
                         "Resampler: memory allocation failed");
    }

    // 配置 ma_resampler
    ma_resampler_config ma_config = ma_resampler_config_init(
        ma_format_f32,                                          // 输入/输出格式：float32
        static_cast<ma_uint32>(config_.channels),               // 声道数
        static_cast<ma_uint32>(config_.input_sample_rate),      // 输入采样率
        static_cast<ma_uint32>(config_.output_sample_rate),     // 输出采样率
        ma_resample_algorithm_linear                            // 线性插值（低延迟，适合实时）
    );

    // 初始化 miniaudio 重采样器
    const ma_result result = ma_resampler_init(&ma_config, nullptr, raw);
    if (result != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Resampler: ma_resampler_init failed with error {}", result);
        delete raw;
        return Err<void>(ResultCode::DeviceNotAvailable,
                         "Resampler: ma_resampler_init failed");
    }

    resampler_.reset(raw);
    initialized_ = true;

    NEVO_LOG_INFO("audio", "Resampler initialized: {}Hz -> {}Hz, {}ch",
                  config_.input_sample_rate, config_.output_sample_rate, config_.channels);
    return Ok();
}

// ============================================================
// 重采样处理
// ============================================================
Result<uint32_t> Resampler::process(const float* input,
                                     uint32_t input_frames,
                                     float* output,
                                     uint32_t output_frames) {
    if (!initialized_ || !resampler_) {
        NEVO_LOG_ERROR("audio", "Resampler::process called before initialization");
        return Err<uint32_t>(ResultCode::DeviceNotAvailable,
                             "Resampler not initialized");
    }

    if (!input || !output || input_frames == 0) {
        return Ok(static_cast<uint32_t>(0));
    }

    // ma_resampler_process_pcm_frames 使用帧数（每声道采样数）
    ma_uint64 frames_in = static_cast<ma_uint64>(input_frames);
    ma_uint64 frames_out = static_cast<ma_uint64>(output_frames);

    // 执行重采样
    // 注意：此函数实时安全，不会分配内存或加锁
    const ma_result result = ma_resampler_process_pcm_frames(
        resampler_.get(),
        input,
        &frames_in,        // [in/out] 输入：期望处理的帧数；输出：实际消耗的帧数
        output,
        &frames_out        // [in/out] 输入：输出缓冲区可容纳的帧数；输出：实际输出的帧数
    );

    if (result != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Resampler::process failed with ma_error {}", result);
        return Err<uint32_t>(ResultCode::Unknown,
                             "Resampler process failed");
    }

    // 检查是否所有输入帧都被消耗
    if (frames_in != input_frames) {
        NEVO_LOG_WARN("audio",
                      "Resampler: not all input consumed (requested={}, consumed={})",
                      input_frames, frames_in);
    }

    return Ok(static_cast<uint32_t>(frames_out));
}

// ============================================================
// 重置
// ============================================================
void Resampler::reset() {
    if (resampler_) {
        ma_resampler_reset(resampler_.get());
        NEVO_LOG_INFO("audio", "Resampler reset: {}Hz -> {}Hz",
                      config_.input_sample_rate, config_.output_sample_rate);
    }
}

// ============================================================
// 配置更新
// ============================================================
void Resampler::setInputSampleRate(uint32_t sample_rate) {
    if (config_.input_sample_rate != sample_rate) {
        config_.input_sample_rate = sample_rate;
        initialized_ = false;  // 需要重新 initialize()
        NEVO_LOG_INFO("audio", "Resampler input sample rate changed to {}Hz", sample_rate);
    }
}

void Resampler::setOutputSampleRate(uint32_t sample_rate) {
    if (config_.output_sample_rate != sample_rate) {
        config_.output_sample_rate = sample_rate;
        initialized_ = false;  // 需要重新 initialize()
        NEVO_LOG_INFO("audio", "Resampler output sample rate changed to {}Hz", sample_rate);
    }
}

// ============================================================
// 输出帧数估算
// ============================================================
uint32_t Resampler::estimateOutputFrames(uint32_t input_frames) const {
    // 简单比例估算，预留 10% 余量以应对重采样器的内部缓冲
    const double ratio = static_cast<double>(config_.output_sample_rate) /
                         static_cast<double>(config_.input_sample_rate);
    return static_cast<uint32_t>(input_frames * ratio * 1.1) + 1;
}

} // namespace nevo
