/**
 * @file AudioEngine.cpp
 * @brief 音频引擎实现 - 中央音频管线管理器
 *
 * 本文件实现了 NEVO VoIP 系统的核心音频管线。特别注意：
 *
 * 实时安全约束（CRITICAL）：
 *   miniaudio 的输入/输出回调运行在实时音频线程上，这些回调必须遵守
 *   严格的实时约束，否则会导致音频卡顿、爆音甚至系统崩溃。
 *
 *   回调中禁止的操作：
 *     - 任何形式的堆内存分配（malloc/new/std::vector::push_back 等）
 *     - 任何互斥锁操作（std::mutex、std::recursive_mutex 等）
 *     - 任何系统调用（文件 I/O、网络、sleep 等）
 *     - 任何可能抛出异常的操作
 *     - 任何非 O(1) 复杂度的操作
 *
 *   回调中允许的操作：
 *     - 原子操作（std::atomic）
 *     - SPSC 队列的 push/pop（无锁）
 *     - 内存拷贝（memcpy、std::copy）
 *     - 算术运算（浮点乘法、加法等）
 *     - 纯函数调用（无副作用函数）
 */

#include "nevo/core/audio/AudioEngine.h"
#include "nevo/core/audio/JitterBuffer.h"
#include "nevo/core/audio/AudioMixer.h"
#include "nevo/core/common/Logger.h"

#include <miniaudio.h>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <cmath>

namespace nevo {

// ============================================================
// miniaudio 资源 Deleter
// ============================================================

void AudioEngine::MaDeviceDeleter::operator()(ma_device* device) const {
    if (device) {
        ma_device_uninit(device);
        delete device;
    }
}

void AudioEngine::MaContextDeleter::operator()(ma_context* context) const {
    if (context) {
        ma_context_uninit(context);
        delete context;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

AudioEngine::AudioEngine()
    : input_fifo_()
    , output_fifo_()
{
}

AudioEngine::~AudioEngine() {
    if (running_.load(std::memory_order_acquire)) {
        shutdown();
    }
}

// ============================================================
// 初始化
// ============================================================

Result<void> AudioEngine::initContext() {
    if (ma_context_) {
        // Already initialized
        return Ok();
    }

    auto* ctx = new (std::nothrow) ma_context{};
    if (!ctx) {
        NEVO_LOG_ERROR("audio", "Failed to allocate ma_context");
        return Err<void>(ResultCode::DeviceNotAvailable, "ma_context allocation failed");
    }

    ma_result ma_ret = ma_context_init(nullptr, 0, nullptr, ctx);
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "ma_context_init failed: {}", ma_ret);
        delete ctx;
        return Err<void>(ResultCode::DeviceNotAvailable, "ma_context_init failed");
    }
    ma_context_.reset(ctx);

    NEVO_LOG_INFO("audio", "ma_context initialized (early, for device enumeration)");
    return Ok();
}

Result<void> AudioEngine::initialize(const Config& config) {
    // 防止重复初始化
    if (running_.load(std::memory_order_acquire)) {
        NEVO_LOG_WARN("audio", "AudioEngine already running, shutting down first");
        shutdown();
    }

    config_ = config;

    // ----------------------------------------------------------
    // 1. 初始化 AudioMemoryPool（必须在最前面，其他组件可能使用）
    // ----------------------------------------------------------
    memory_pool_ = std::make_unique<AudioMemoryPool>(AudioMemoryPoolConfig{});

    // ----------------------------------------------------------
    // 2. 初始化 ma_context（用于设备枚举和管理）
    //    若已通过 initContext() 提前初始化，则复用
    // ----------------------------------------------------------
    if (!ma_context_) {
        auto ctx_result = initContext();
        if (!ctx_result) {
            return ctx_result;
        }
    }

    // ----------------------------------------------------------
    // 3. 初始化输入 Resampler（如设备采样率非 48kHz）
    // ----------------------------------------------------------
    input_resampler_ = std::make_unique<Resampler>(Resampler::Config{
        config_.input_sample_rate,
        kOpusSampleRate,
        config_.channels
    });
    if (config_.input_sample_rate != kOpusSampleRate) {
        auto res = input_resampler_->initialize();
        if (!res) {
            NEVO_LOG_ERROR("audio", "Input Resampler init failed: {}", res.error().message());
            return Err<void>(res.error().code(), "Input Resampler init failed: " + res.error().message());
        }
    }

    // ----------------------------------------------------------
    // 4. 初始化输出 Resampler（如设备采样率非 48kHz）
    // ----------------------------------------------------------
    output_resampler_ = std::make_unique<Resampler>(Resampler::Config{
        kOpusSampleRate,
        config_.output_sample_rate,
        config_.channels
    });
    if (config_.output_sample_rate != kOpusSampleRate) {
        auto res = output_resampler_->initialize();
        if (!res) {
            NEVO_LOG_ERROR("audio", "Output Resampler init failed: {}", res.error().message());
            return Err<void>(res.error().code(), "Output Resampler init failed: " + res.error().message());
        }
    }

    // ----------------------------------------------------------
    // 5. 初始化 Opus 编码器
    // ----------------------------------------------------------
    encoder_ = std::make_unique<OpusEncoderWrapper>(config_.encoder_config);
#ifdef NEVO_HAS_OPUS
    NEVO_LOG_INFO("audio", "Opus encoder initialized");
#else
    NEVO_LOG_WARN("audio", "Opus encoder initialized as stub (Opus not available)");
#endif

    // ----------------------------------------------------------
    // 6. 初始化 RNNoise 降噪状态
    // ----------------------------------------------------------
#ifdef NEVO_HAS_RNNOISE
    rnnoise_state_ = rnnoise_create();
    if (rnnoise_state_) {
        NEVO_LOG_INFO("audio", "RNNoise denoiser initialized");
    } else {
        NEVO_LOG_WARN("audio", "Failed to create RNNoise denoiser state");
    }
#else
    NEVO_LOG_INFO("audio", "RNNoise not available, noise suppression disabled");
#endif

    // ----------------------------------------------------------
    // 7. 初始化 VAD
    // ----------------------------------------------------------
    voice_activity_ = std::make_unique<VoiceActivity>(config_.vad_config);

    // ----------------------------------------------------------
    // 7. 初始化 JitterBuffer
    // ----------------------------------------------------------
    jitter_buffer_ = std::make_unique<JitterBuffer>();
    NEVO_LOG_INFO("audio", "JitterBuffer initialized");

    // ----------------------------------------------------------
    // 8. 初始化 AudioMixer
    // ----------------------------------------------------------
    mixer_ = std::make_unique<AudioMixer>();
    // 注意：不在此处设置 mixer volume，音量通过 output_volume_ 在输出回调中应用
    NEVO_LOG_INFO("audio", "AudioMixer initialized");

    // ----------------------------------------------------------
    // 9. 设置音量控制的原子变量
    // ----------------------------------------------------------
    input_gain_.store(config_.input_gain, std::memory_order_relaxed);
    output_volume_.store(config_.output_volume, std::memory_order_relaxed);
    actual_input_sample_rate_.store(config_.input_sample_rate, std::memory_order_relaxed);
    actual_output_sample_rate_.store(config_.output_sample_rate, std::memory_order_relaxed);

    // ----------------------------------------------------------
    // 10. 初始化 miniaudio 输入设备（麦克风）
    // ----------------------------------------------------------
    ma_result ma_ret = MA_SUCCESS;

    auto* in_dev = new (std::nothrow) ma_device{};
    if (!in_dev) {
        NEVO_LOG_ERROR("audio", "Failed to allocate input ma_device");
        return Err<void>(ResultCode::DeviceNotAvailable, "Input device allocation failed");
    }

    ma_device_config in_config = ma_device_config_init(ma_device_type_capture);
    in_config.capture.format   = ma_format_f32;                          // float32 PCM
    in_config.capture.channels = static_cast<ma_uint32>(config_.channels);
    in_config.sampleRate       = static_cast<ma_uint32>(config_.input_sample_rate);
    in_config.periodSizeInFrames = static_cast<ma_uint32>(config_.frame_size);  // 请求 20ms 周期
    in_config.dataCallback     = &AudioEngine::maInputCallback;         // 静态回调
    in_config.pUserData        = this;                                   // 传入 this 指针
    in_config.capture.shareMode = ma_share_mode_shared;                // 显式共享模式，允许多应用同时使用
    in_config.wasapi.usage      = ma_wasapi_usage_pro_audio;           // VoIP 优先级，提高设备访问优先级
    // Use user's previously selected device if available
    if (has_selected_input_id_) {
        in_config.capture.pDeviceID = &selected_input_id_;
    }

    ma_ret = ma_device_init(ma_context_.get(), &in_config, in_dev);
    if (ma_ret != MA_SUCCESS && has_selected_input_id_) {
        // Saved device unavailable, fall back to default
        if (ma_ret == MA_BUSY) {
            NEVO_LOG_WARN("audio", "Saved input device is in use by another application, falling back to default");
        } else {
            NEVO_LOG_WARN("audio", "Saved input device unavailable, falling back to default: {}", ma_ret);
        }
        has_selected_input_id_ = false;
        in_config.capture.pDeviceID = nullptr;
        ma_ret = ma_device_init(ma_context_.get(), &in_config, in_dev);
    }
    if (ma_ret != MA_SUCCESS) {
        delete in_dev;
        if (ma_ret == MA_BUSY) {
            NEVO_LOG_ERROR("audio", "Input device is in use by another application (exclusive mode): {}", ma_ret);
            return Err<void>(ResultCode::DeviceInUse,
                "Input device is in use by another application. "
                "Please close other applications using the microphone or disable exclusive mode.");
        }
        NEVO_LOG_ERROR("audio", "Input device init failed: {}", ma_ret);
        return Err<void>(ResultCode::DeviceNotAvailable, "Input device init failed");
    }
    input_device_.reset(in_dev);

    // 记录实际设备采样率（可能与请求值不同）
    actual_input_sample_rate_.store(in_dev->sampleRate, std::memory_order_relaxed);
    // 记录当前输入设备名称（供 UI 查询，避免设置对话框触发意外设备切换）
    current_input_device_name_ = in_dev->capture.name;
    NEVO_LOG_INFO("audio", "Input device: {}Hz, {}ch, name='{}'",
                  in_dev->sampleRate, in_dev->capture.channels, in_dev->capture.name);

    // ----------------------------------------------------------
    // 11. 初始化 miniaudio 输出设备（扬声器）
    // ----------------------------------------------------------
    auto* out_dev = new (std::nothrow) ma_device{};
    if (!out_dev) {
        NEVO_LOG_ERROR("audio", "Failed to allocate output ma_device");
        return Err<void>(ResultCode::DeviceNotAvailable, "Output device allocation failed");
    }

    ma_device_config out_config = ma_device_config_init(ma_device_type_playback);
    out_config.playback.format   = ma_format_f32;
    out_config.playback.channels = static_cast<ma_uint32>(config_.channels);
    out_config.sampleRate        = static_cast<ma_uint32>(config_.output_sample_rate);
    out_config.periodSizeInFrames = static_cast<ma_uint32>(config_.frame_size);
    out_config.dataCallback      = &AudioEngine::maOutputCallback;
    out_config.pUserData         = this;
    out_config.playback.shareMode = ma_share_mode_shared;              // 显式共享模式
    out_config.wasapi.usage       = ma_wasapi_usage_pro_audio;         // VoIP 优先级
    // Use user's previously selected device if available
    if (has_selected_output_id_) {
        out_config.playback.pDeviceID = &selected_output_id_;
    }

    ma_ret = ma_device_init(ma_context_.get(), &out_config, out_dev);
    if (ma_ret != MA_SUCCESS && has_selected_output_id_) {
        // Saved device unavailable, fall back to default
        if (ma_ret == MA_BUSY) {
            NEVO_LOG_WARN("audio", "Saved output device is in use by another application, falling back to default");
        } else {
            NEVO_LOG_WARN("audio", "Saved output device unavailable, falling back to default: {}", ma_ret);
        }
        has_selected_output_id_ = false;
        out_config.playback.pDeviceID = nullptr;
        ma_ret = ma_device_init(ma_context_.get(), &out_config, out_dev);
    }
    if (ma_ret != MA_SUCCESS) {
        delete out_dev;
        if (ma_ret == MA_BUSY) {
            NEVO_LOG_ERROR("audio", "Output device is in use by another application (exclusive mode): {}", ma_ret);
            return Err<void>(ResultCode::DeviceInUse,
                "Output device is in use by another application. "
                "Please close other applications using the speaker or disable exclusive mode.");
        }
        NEVO_LOG_ERROR("audio", "Output device init failed: {}", ma_ret);
        return Err<void>(ResultCode::DeviceNotAvailable, "Output device init failed");
    }
    output_device_.reset(out_dev);

    actual_output_sample_rate_.store(out_dev->sampleRate, std::memory_order_relaxed);
    // 记录当前输出设备名称（供 UI 查询）
    current_output_device_name_ = out_dev->playback.name;
    NEVO_LOG_INFO("audio", "Output device: {}Hz, {}ch, name='{}'",
                  out_dev->sampleRate, out_dev->playback.channels, out_dev->playback.name);

    // ----------------------------------------------------------
    // 12. 检查实际采样率是否需要重采样
    // ----------------------------------------------------------
    if (in_dev->sampleRate != kOpusSampleRate) {
        NEVO_LOG_INFO("audio", "Input device sample rate {}Hz differs from Opus {}Hz, "
                      "configuring input resampler", in_dev->sampleRate, kOpusSampleRate);
        input_resampler_->setInputSampleRate(in_dev->sampleRate);
        input_resampler_->setOutputSampleRate(kOpusSampleRate);
        auto res = input_resampler_->initialize();
        if (!res) {
            NEVO_LOG_ERROR("audio", "Input Resampler init failed: {}", res.error().message());
            return Err<void>(res.error().code(),
                "Input resampler initialization failed: " + res.error().message());
        }
    } else {
        // 设备实际采样率已匹配 Opus，无需重采样
        input_resampler_.reset();
    }

    if (out_dev->sampleRate != kOpusSampleRate) {
        NEVO_LOG_INFO("audio", "Output device sample rate {}Hz differs from Opus {}Hz, "
                      "configuring output resampler", out_dev->sampleRate, kOpusSampleRate);
        output_resampler_->setInputSampleRate(kOpusSampleRate);
        output_resampler_->setOutputSampleRate(out_dev->sampleRate);
        auto res = output_resampler_->initialize();
        if (!res) {
            NEVO_LOG_ERROR("audio", "Output Resampler init failed: {}", res.error().message());
            return Err<void>(res.error().code(),
                "Output resampler initialization failed: " + res.error().message());
        }
    } else {
        // 设备实际采样率已匹配 Opus，无需重采样
        output_resampler_.reset();
    }

    // ----------------------------------------------------------
    // 13. 启动设备
    // ----------------------------------------------------------
    ma_ret = ma_device_start(input_device_.get());
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Failed to start input device: {}", ma_ret);
        return Err<void>(ResultCode::DeviceNotAvailable, "Failed to start input device");
    }

    ma_ret = ma_device_start(output_device_.get());
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Failed to start output device: {}", ma_ret);
        ma_device_stop(input_device_.get());
        return Err<void>(ResultCode::DeviceNotAvailable, "Failed to start output device");
    }

    running_.store(true, std::memory_order_release);

    // ----------------------------------------------------------
    // 14. 启动编码线程
    // ----------------------------------------------------------
    encode_thread_ = std::jthread([this](std::stop_token st) {
        encodeThreadFunc(std::move(st));
    });
    if (!encode_thread_.joinable()) {
        NEVO_LOG_ERROR("audio", "Failed to start encode thread");
        ma_device_stop(output_device_.get());
        ma_device_stop(input_device_.get());
        running_.store(false, std::memory_order_release);
        return Err<void>(ResultCode::Unknown, "Failed to start encode thread");
    }
    NEVO_LOG_INFO("audio", "Encode thread started");

    NEVO_LOG_INFO("audio", "AudioEngine initialized and running "
                  "(input={}Hz, output={}Hz, frame={} samples)",
                  actual_input_sample_rate_.load(std::memory_order_relaxed),
                  actual_output_sample_rate_.load(std::memory_order_relaxed),
                  config_.frame_size);

    return Ok();
}

// ============================================================
// 关闭
// ============================================================

void AudioEngine::shutdown() {
    // 原子性地将 running_ 从 true 切换为 false，
    // 防止两个线程同时进入关闭逻辑导致 double-join
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false,
            std::memory_order_acq_rel)) {
        return;  // 另一个线程已发起关闭
    }

    NEVO_LOG_INFO("audio", "AudioEngine shutting down...");

    // 2. 请求编码线程停止并等待退出
    //    必须在停止 miniaudio 设备之前，避免回调访问已释放资源
    if (encode_thread_.joinable()) {
        encode_thread_.request_stop();
        encode_thread_.join();
    }

    // 3. 停止 miniaudio 设备（会等待当前回调完成）
    if (input_device_) {
        ma_device_stop(input_device_.get());
    }
    if (output_device_) {
        ma_device_stop(output_device_.get());
    }

    // 4. 清空队列
#ifdef NEVO_HAS_BOOST
    AudioFrame dummy;
    while (input_fifo_.pop(dummy)) {}
    while (output_fifo_.pop(dummy)) {}
    while (monitor_fifo_.pop(dummy)) {}
#else
    {
        std::lock_guard<std::mutex> lock(input_fifo_mutex_);
        input_fifo_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(output_fifo_mutex_);
        output_fifo_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(monitor_fifo_mutex_);
        monitor_fifo_.clear();
    }
#endif

    // 重置输出回调帧累积状态
    output_frame_valid_ = false;
    output_frame_offset_ = 0;
    monitor_frame_valid_ = false;
    monitor_frame_offset_ = 0;

    // 5. 释放资源（逆序释放）
    output_device_.reset();
    input_device_.reset();
    ma_context_.reset();

    mixer_.reset();
    jitter_buffer_.reset();
    voice_activity_.reset();

    // 释放 RNNoise 状态
#ifdef NEVO_HAS_RNNOISE
    if (rnnoise_state_) {
        rnnoise_destroy(rnnoise_state_);
        rnnoise_state_ = nullptr;
    }
#endif

    encoder_.reset();

    // 清除所有远端解码器
    {
        std::lock_guard<std::mutex> lock(decoders_mutex_);
        decoders_.clear();
    }

    input_resampler_.reset();
    output_resampler_.reset();
    memory_pool_.reset();

    NEVO_LOG_INFO("audio", "AudioEngine shutdown complete");
}

// ============================================================
// 运行状态查询
// ============================================================

bool AudioEngine::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

// ============================================================
// miniaudio 输入回调（实时音频线程）
// ============================================================
//
// !!! 实时安全约束 !!!
// 此回调在 miniaudio 的实时音频线程上执行。
// 绝对禁止：malloc/new、mutex lock、系统调用、日志输出、异常抛出。
// 仅允许：原子操作、SPSC push、memcpy、算术运算。
//
void AudioEngine::maInputCallback(ma_device* p_device,
                                   void* p_output,
                                   const void* p_input,
                                   ma_uint32 frame_count) {
    // 通过 pUserData 取回 AudioEngine 指针
    auto* self = static_cast<AudioEngine*>(p_device->pUserData);
    (void)p_output;  // miniaudio 回调签名要求，输入回调不使用输出缓冲区

    // 安全检查：确保引擎仍在运行
    if (!self || !self->running_.load(std::memory_order_acquire)) {
        return;
    }

    const auto* pcm_input = static_cast<const float*>(p_input);

    // ----------------------------------------------------------
    // Step 1: 读取输入 PCM 数据并应用增益
    // ----------------------------------------------------------
    // 增益值通过原子变量读取，实时安全
    const float gain = self->input_gain_.load(std::memory_order_relaxed);

    // 临时帧缓冲区（栈上分配，实时安全）
    AudioFrame frame{};
    const uint32_t samples_to_process = std::min(
        static_cast<uint32_t>(frame_count),
        static_cast<uint32_t>(kFrameSize)
    );

    // Compute peak level for input meter (real-time safe: arithmetic only)
    float peak = 0.0f;
    for (uint32_t i = 0; i < samples_to_process; ++i) {
        const float abs_sample = std::abs(pcm_input[i] * gain);
        if (abs_sample > peak) peak = abs_sample;
    }
    self->input_peak_level_.store(peak, std::memory_order_relaxed);
    // NOTE: input_level_callback_ has a narrow data race window with the setter.
    // The valid_ flag reduces but does not fully eliminate it. For UI level meters,
    // prefer polling getCurrentInputLevel() from a QTimer instead of using this callback.
    if (self->input_level_callback_valid_.load(std::memory_order_acquire)) {
        self->input_level_callback_(peak);
    }

    // 应用增益并复制到帧缓冲区
    for (uint32_t i = 0; i < samples_to_process; ++i) {
        // 硬限幅防止溢出（clip to [-1.0, 1.0]）
        const float sample = pcm_input[i] * gain;
        frame[i] = std::clamp(sample, -1.0f, 1.0f);
    }
    // 若设备帧数 < kFrameSize，剩余部分为静音（已由 std::array 零初始化）

    // ----------------------------------------------------------
    // Step 2: RNNoise 降噪处理（仅当启用且采样率为 48kHz 时）
    // ----------------------------------------------------------
    if (self->noise_suppression_enabled_.load(std::memory_order_relaxed)) {
#ifdef NEVO_HAS_RNNOISE
        if (self->rnnoise_state_ && samples_to_process == kFrameSize) {
            // RNNoise 每次处理 480 样本（10ms @ 48kHz）
            // 我们的帧是 960 样本（20ms），需要调用两次
            rnnoise_process_frame(self->rnnoise_state_, frame.data(), frame.data());
            rnnoise_process_frame(self->rnnoise_state_, frame.data() + 480, frame.data() + 480);
        }
#endif
    }

    // ----------------------------------------------------------
    // Step 3: 如果需要重采样，执行输入端重采样
    // ----------------------------------------------------------
    // 注意：如果设备采样率 != 48kHz，需要将设备采样率的 PCM 转换为 48kHz
    // 但重采样可能改变帧大小，这里简化处理：
    //   - 如果设备帧数 = kFrameSize（48kHz/20ms），直接入队
    //   - 如果需要重采样，在编码线程中处理
    //
    // 当前实现：直接将设备原始帧推入 FIFO，重采样在编码线程完成。
    // 这是更安全的方案：避免在回调中调用 ma_resampler_process_pcm_frames
    // 可能产生的内部状态问题。
    //
    // 对于 48kHz 设备，frame_count == kFrameSize，直接入队即可。
    // 对于非 48kHz 设备，帧大小不同，使用独立的帧格式处理。
    // ----------------------------------------------------------

    // ----------------------------------------------------------
    // Step 4: 推入 input_fifo
    // ----------------------------------------------------------
#ifdef NEVO_HAS_BOOST
    // spsc_queue::push 是无锁操作，实时安全
    // 如果 FIFO 已满（消费者太慢），丢弃此帧（静音丢失好于延迟累积）
    if (!self->input_fifo_.push(frame)) {
        // FIFO 满了，丢弃帧。这是正确行为：
        // 宁可丢帧也不让延迟无限增长。
        // 注意：此处不能打日志（实时线程禁止 I/O）
    }
#else
    // Fallback: mutex-protected push (NOT real-time safe)
    {
        std::lock_guard<std::mutex> lock(self->input_fifo_mutex_);
        if (self->input_fifo_.size() < kFifoCapacity) {
            self->input_fifo_.push_back(frame);
        }
    }
#endif

    // ----------------------------------------------------------
    // Step 5: 推入 monitor_fifo（当监听启用时）
    // ----------------------------------------------------------
    if (self->monitor_enabled_.load(std::memory_order_relaxed)) {
#ifdef NEVO_HAS_BOOST
        self->monitor_fifo_.push(frame);  // 满则丢弃，可接受
#else
        {
            std::lock_guard<std::mutex> lock(self->monitor_fifo_mutex_);
            if (self->monitor_fifo_.size() < kFifoCapacity) {
                self->monitor_fifo_.push_back(frame);
            }
        }
#endif
    }
}

// ============================================================
// miniaudio 输出回调（实时音频线程）
// ============================================================
//
// !!! 实时安全约束 !!!
// 同 maInputCallback 的所有约束。
//
void AudioEngine::maOutputCallback(ma_device* p_device,
                                    void* p_output,
                                    const void* p_input,
                                    ma_uint32 frame_count) {
    auto* self = static_cast<AudioEngine*>(p_device->pUserData);
    (void)p_input;  // miniaudio 回调签名要求，输出回调不使用输入缓冲区

    if (!self || !self->running_.load(std::memory_order_acquire)) {
        // 引擎未运行，输出静音
        std::memset(p_output, 0, frame_count * sizeof(float));
        return;
    }

    auto* pcm_output = static_cast<float*>(p_output);
    const float volume = self->output_volume_.load(std::memory_order_relaxed);
    const float mon_vol = self->monitor_volume_.load(std::memory_order_relaxed);
    const bool monitor_on = self->monitor_enabled_.load(std::memory_order_relaxed);

    // ----------------------------------------------------------
    // 累积式帧消费：逐采样填充输出缓冲区
    // 从当前帧的 offset 位置读取，帧耗尽时从 FIFO 弹出新帧
    // 解决 frame_count != kFrameSize 时的帧浪费和断续问题
    // ----------------------------------------------------------
    uint32_t written = 0;
    while (written < frame_count) {
        // 确保 output 帧有效
        if (!self->output_frame_valid_) {
#ifdef NEVO_HAS_BOOST
            self->output_frame_valid_ = self->output_fifo_.pop(self->output_current_frame_);
#else
            {
                std::lock_guard<std::mutex> lock(self->output_fifo_mutex_);
                if (!self->output_fifo_.empty()) {
                    self->output_current_frame_ = self->output_fifo_.front();
                    self->output_fifo_.pop_front();
                    self->output_frame_valid_ = true;
                }
            }
#endif
            self->output_frame_offset_ = 0;
        }

        // 确保 monitor 帧有效
        if (monitor_on && !self->monitor_frame_valid_) {
#ifdef NEVO_HAS_BOOST
            self->monitor_frame_valid_ = self->monitor_fifo_.pop(self->monitor_current_frame_);
#else
            {
                std::lock_guard<std::mutex> lock(self->monitor_fifo_mutex_);
                if (!self->monitor_fifo_.empty()) {
                    self->monitor_current_frame_ = self->monitor_fifo_.front();
                    self->monitor_fifo_.pop_front();
                    self->monitor_frame_valid_ = true;
                }
            }
#endif
            self->monitor_frame_offset_ = 0;
        }

        // 如果两个 FIFO 都没有更多数据，退出循环
        if (!self->output_frame_valid_ && !self->monitor_frame_valid_) {
            break;
        }

        // 从当前偏移位置混合一个采样
        float sample = 0.0f;
        if (self->output_frame_valid_) {
            sample += self->output_current_frame_[self->output_frame_offset_] * volume;
        }
        if (self->monitor_frame_valid_) {
            sample += self->monitor_current_frame_[self->monitor_frame_offset_] * mon_vol;
        }
        pcm_output[written] = std::clamp(sample, -1.0f, 1.0f);

        // 推进偏移
        if (self->output_frame_valid_) {
            self->output_frame_offset_++;
            if (self->output_frame_offset_ >= kFrameSize) {
                self->output_frame_valid_ = false;
            }
        }
        if (self->monitor_frame_valid_) {
            self->monitor_frame_offset_++;
            if (self->monitor_frame_offset_ >= kFrameSize) {
                self->monitor_frame_valid_ = false;
            }
        }

        written++;
    }

    // 剩余填充静音
    if (written < frame_count) {
        std::memset(pcm_output + written, 0, (frame_count - written) * sizeof(float));
    }
}

// ============================================================
// 网络线程侧：编码周期
// ============================================================

void AudioEngine::processEncodeCycle() {
    if (!running_.load(std::memory_order_acquire) || !encoder_) {
        return;
    }

    // 从 input_fifo 中取出帧（可能有多帧待处理）
    AudioFrame frame;
    while (true) {
#ifdef NEVO_HAS_BOOST
        if (!input_fifo_.pop(frame)) break;
#else
        {
            std::lock_guard<std::mutex> lock(input_fifo_mutex_);
            if (input_fifo_.empty()) break;
            frame = input_fifo_.front();
            input_fifo_.pop_front();
        }
#endif
        // ----------------------------------------------------------
        // Step 1: 输入端重采样（如需要）
        // ----------------------------------------------------------
        float* pcm_to_encode = frame.data();
        uint32_t frames_to_encode = kFrameSize;

        if (input_resampler_ && input_resampler_->isInitialized()) {
            // 设备采样率 != 48kHz，需要重采样
            const uint32_t input_rate = actual_input_sample_rate_.load(std::memory_order_relaxed);

            // 计算设备采样率下对应的帧大小
            // 例如：16kHz 设备，20ms 帧 = 320 采样
            const uint32_t device_frame_size = input_rate * kFrameSize / kOpusSampleRate;

            // 确保重采样输出缓冲区足够大
            const uint32_t max_output_frames = input_resampler_->estimateOutputFrames(device_frame_size);
            if (resample_output_buffer_.size() < max_output_frames) {
                resample_output_buffer_.resize(max_output_frames);
            }

            auto res = input_resampler_->process(
                frame.data(),
                device_frame_size,
                resample_output_buffer_.data(),
                max_output_frames
            );

            if (res) {
                pcm_to_encode = resample_output_buffer_.data();
                frames_to_encode = res.value();
            } else {
                NEVO_LOG_WARN("audio", "Input resample failed, using original data");
                // 降级：使用原始数据（可能采样率不匹配，但比静音好）
            }
        }

        // ----------------------------------------------------------
        // Step 2: VAD 检测
        // ----------------------------------------------------------
        const bool opus_vad = encoder_->lastFrameHadVoice();
        const bool should_send = voice_activity_->shouldTransmit(
            pcm_to_encode, frames_to_encode, opus_vad
        );

        if (!should_send) {
            // VAD 判定为静音，跳过编码（如果 Opus DTX 启用，
            // 编码器会自动产生极小带宽的舒适噪声帧）
            // 但仍需编码以维持 DTX 状态
        }

        // ----------------------------------------------------------
        // Step 3: Opus 编码
        // ----------------------------------------------------------
        // 从内存池获取编码输出缓冲区
        uint8_t* encode_buffer = memory_pool_->acquire();
        if (!encode_buffer) {
            NEVO_LOG_WARN("audio", "Memory pool exhausted, dropping frame");
            continue;
        }

        auto encode_result = encoder_->encode(
            pcm_to_encode,
            encode_buffer,
            memory_pool_->blockSize()
        );

        if (encode_result) {
            const uint32_t encoded_size = encode_result.value();

            if (encoded_size > 0) {
                // --------------------------------------------------
                // Step 4: 通过回调发送编码数据到网络层
                // --------------------------------------------------
                EncodedAudioCallback callback;
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    callback = encoded_callback_;
                }

                if (callback) {
                    const bool vad_result = should_send || encoder_->lastFrameHadVoice();
                    callback(encode_buffer, encoded_size, vad_result);
                }
            }
        } else {
            NEVO_LOG_ERROR("audio", "Opus encode error: {}", encode_result.error().message());
        }

        // 归还内存块
        memory_pool_->release(encode_buffer);
    }
}

// ============================================================
// 编码线程
// ============================================================

void AudioEngine::encodeThreadFunc(std::stop_token stop_token) {
    NEVO_LOG_INFO("audio", "Encode thread running");

    while (!stop_token.stop_requested()) {
        processEncodeCycle();

#ifdef NEVO_HAS_BOOST
        if (input_fifo_.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#else
        {
            bool has_data = false;
            {
                std::lock_guard<std::mutex> lock(input_fifo_mutex_);
                has_data = !input_fifo_.empty();
            }
            if (!has_data) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
#endif
    }

    // 处理线程退出前 FIFO 中可能残留的帧
    processEncodeCycle();

    NEVO_LOG_INFO("audio", "Encode thread exiting");
}

// ============================================================
// 网络线程侧：混音周期
// ============================================================

void AudioEngine::processMixCycle() {
    if (!running_.load(std::memory_order_acquire) || !mixer_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mix_cycle_mutex_);

    // 从 JitterBuffer 中读取各用户的解码数据
    // Mixer 将多路音频混合为单路输出
    AudioFrame mixed_frame{};
    mixer_->clear();

    // 遍历所有远端用户，从 JitterBuffer 提取数据
    {
        std::lock_guard<std::mutex> lock(decoders_mutex_);
        for (auto& [user_id, decoder] : decoders_) {
            if (!decoder) continue;

            // 从 JitterBuffer 中取出该用户的 PCM 数据
            uint8_t* opus_data = nullptr;
            uint32_t opus_size = 0;

            if (jitter_buffer_->pop(user_id, opus_data, opus_size)) {
                // 解码 Opus 数据
                float pcm_buffer[kFrameSize];
                auto decode_result = decoder->decode(opus_data, opus_size, pcm_buffer);

                if (decode_result && decode_result.value() > 0) {
                    // 将解码后的 PCM 添加到 Mixer
                    mixer_->addInput(user_id, pcm_buffer, decode_result.value());
                }
            } else {
                // JitterBuffer 无数据（丢包或延迟），执行 PLC
                float plc_buffer[kFrameSize];
                auto plc_result = decoder->decodePacketLoss(plc_buffer);

                if (plc_result && plc_result.value() > 0) {
                    mixer_->addInput(user_id, plc_buffer, plc_result.value());
                }
            }
        }
    }

    // 执行混音
    mixer_->mix(mixed_frame.data(), kFrameSize);

    // 输出端重采样（如需要）
    float* pcm_to_output = mixed_frame.data();
    uint32_t frames_to_output = kFrameSize;

    if (output_resampler_ && output_resampler_->isInitialized()) {
        const uint32_t max_output_frames = output_resampler_->estimateOutputFrames(kFrameSize);
        if (resample_output_buffer_.size() < max_output_frames) {
            resample_output_buffer_.resize(max_output_frames);
        }

        auto res = output_resampler_->process(
            mixed_frame.data(),
            kFrameSize,
            resample_output_buffer_.data(),
            max_output_frames
        );

        if (res) {
            pcm_to_output = resample_output_buffer_.data();
            frames_to_output = res.value();
        }
    }

    // 将混音后的帧推入 output_fifo
    // 注意：如果输出端重采样了，帧大小可能不是 kFrameSize
    // 这里简化处理：只推入 kFrameSize 大小的帧（48kHz 标准）
    // 重采样后的帧通过独立路径处理
    if (frames_to_output == kFrameSize) {
        AudioFrame out_frame{};
        std::memcpy(out_frame.data(), pcm_to_output, kFrameSize * sizeof(float));
#ifdef NEVO_HAS_BOOST
        output_fifo_.push(out_frame);
#else
        {
            std::lock_guard<std::mutex> lock(output_fifo_mutex_);
            if (output_fifo_.size() < kFifoCapacity) {
                output_fifo_.push_back(out_frame);
            }
        }
#endif
    } else {
        // 非标准帧大小：累积后推入
        // 实际场景中，如果输出设备需要重采样，
        // 应在输出回调中直接执行重采样以避免缓冲区不匹配
        AudioFrame out_frame{};
        const uint32_t copy_count = std::min(frames_to_output, static_cast<uint32_t>(kFrameSize));
        std::memcpy(out_frame.data(), pcm_to_output, copy_count * sizeof(float));
#ifdef NEVO_HAS_BOOST
        output_fifo_.push(out_frame);
#else
        {
            std::lock_guard<std::mutex> lock(output_fifo_mutex_);
            if (output_fifo_.size() < kFifoCapacity) {
                output_fifo_.push_back(out_frame);
            }
        }
#endif
    }
}

// ============================================================
// 注册编码回调
// ============================================================

void AudioEngine::setInputCallback(EncodedAudioCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    encoded_callback_ = std::move(callback);
}

// ============================================================
// 接收远端音频数据
// ============================================================

Result<void> AudioEngine::queueAudioData(UserId user_id,
                                          const uint8_t* opus_data,
                                          uint32_t data_size) {
    if (!running_.load(std::memory_order_acquire)) {
        return Err<void>(ResultCode::DeviceNotAvailable, "AudioEngine not running");
    }

    if (!opus_data || data_size == 0) {
        return Ok();  // 空数据，忽略
    }

    // 确保远端用户有解码器
    getOrCreateDecoder(user_id);

    // 将 Opus 数据推入 JitterBuffer
    // JitterBuffer 负责排序、去重、缓冲
    jitter_buffer_->push(user_id, opus_data, data_size, 0 /* timestamp 由 JitterBuffer 内部管理 */);

    // 触发混音周期
    processMixCycle();

    return Ok();
}

// ============================================================
// 获取或创建远端解码器
// ============================================================

OpusDecoderWrapper* AudioEngine::getOrCreateDecoder(UserId user_id) {
    std::lock_guard<std::mutex> lock(decoders_mutex_);

    auto it = decoders_.find(user_id);
    if (it != decoders_.end() && it->second) {
        return it->second.get();
    }

    // 创建新的解码器
    auto decoder = std::make_unique<OpusDecoderWrapper>(config_.decoder_config);
    auto* ptr = decoder.get();
    decoders_[user_id] = std::move(decoder);

    NEVO_LOG_INFO("audio", "Created Opus decoder for user {}", user_id.value);
    return ptr;
}

// ============================================================
// 移除远端用户
// ============================================================

void AudioEngine::removeRemoteUser(UserId user_id) {
    std::lock_guard<std::mutex> lock(decoders_mutex_);
    decoders_.erase(user_id);

    if (jitter_buffer_) {
        jitter_buffer_->removeUser(user_id);
    }

    NEVO_LOG_INFO("audio", "Removed remote user {}", user_id.value);
}

// ============================================================
// VAD / PTT 控制
// ============================================================

void AudioEngine::setPttActive(bool active) {
    if (voice_activity_) {
        voice_activity_->setPttActive(active);
    }
}

void AudioEngine::setVadEnabled(bool enabled) {
    if (voice_activity_) {
        voice_activity_->setVadEnabled(enabled);
    }
}

void AudioEngine::setPttEnabled(bool enabled) {
    if (voice_activity_) {
        voice_activity_->setPttEnabled(enabled);
    }
}

bool AudioEngine::isSpeaking() const {
    return voice_activity_ ? voice_activity_->isSpeaking() : false;
}

// ============================================================
// 音量控制
// ============================================================

void AudioEngine::setInputGain(float gain) {
    // 钳位到合理范围
    gain = std::clamp(gain, 0.0f, 2.0f);
    input_gain_.store(gain, std::memory_order_relaxed);
    NEVO_LOG_DEBUG("audio", "Input gain set to {:.2f}", gain);
}

void AudioEngine::setOutputVolume(float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    output_volume_.store(volume, std::memory_order_relaxed);
    // 注意：音量已在输出回调中通过 output_volume_ 应用，
    // 不再同时设置 mixer volume，避免双重衰减

    NEVO_LOG_DEBUG("audio", "Output volume set to {:.2f}", volume);
}

float AudioEngine::inputGain() const {
    return input_gain_.load(std::memory_order_relaxed);
}

float AudioEngine::outputVolume() const {
    return output_volume_.load(std::memory_order_relaxed);
}

void AudioEngine::setFecEnabled(bool enabled) {
    if (encoder_) {
        encoder_->setFecEnabled(enabled);
    }
}

void AudioEngine::setPacketLossPerc(int32_t percentage) {
    if (encoder_) {
        encoder_->setPacketLossPerc(percentage);
    }
}

void AudioEngine::setNoiseSuppressionEnabled(bool enabled) {
    noise_suppression_enabled_.store(enabled, std::memory_order_relaxed);
    NEVO_LOG_DEBUG("audio", "Noise suppression {}", enabled ? "enabled" : "disabled");
}

bool AudioEngine::isNoiseSuppressionAvailable() const {
#ifdef NEVO_HAS_RNNOISE
    return true;
#else
    return false;
#endif
}

void AudioEngine::setVadSensitivity(int sensitivity) {
    if (voice_activity_) {
        // Sensitivity 0-100: higher sensitivity = lower threshold
        float threshold = 1.0f - (static_cast<float>(sensitivity) / 100.0f);
        threshold = std::clamp(threshold, 0.001f, 0.5f);
        voice_activity_->setEnergyThreshold(threshold);
        NEVO_LOG_DEBUG("audio", "VAD sensitivity set to {}% (threshold={:.3f})", sensitivity, threshold);
    }
}

// ============================================================
// 设备管理
// ============================================================

Result<void> AudioEngine::onDeviceSampleRateChanged(uint32_t new_sample_rate) {
    if (!running_.load(std::memory_order_acquire)) {
        NEVO_LOG_WARN("audio", "Device sample rate change ignored: engine not running");
        return Err<void>(ResultCode::InvalidRequest, "Engine not running");
    }

    NEVO_LOG_INFO("audio", "Device sample rate changed to {}Hz, reconfiguring...", new_sample_rate);

    // ----------------------------------------------------------
    // 重配置输入 Resampler
    // ----------------------------------------------------------
    if (new_sample_rate != kOpusSampleRate) {
        auto res = reconfigureInputResampler(new_sample_rate);
        if (!res) {
            NEVO_LOG_ERROR("audio", "Failed to reconfigure input resampler: {}", res.error().message());
            return res;
        }
    } else {
        // 采样率与 Opus 一致，不需要重采样
        if (input_resampler_) {
            input_resampler_->reset();
        }
    }

    // 更新实际采样率
    actual_input_sample_rate_.store(new_sample_rate, std::memory_order_relaxed);

    NEVO_LOG_INFO("audio", "Device sample rate change handled successfully");
    return Ok();
}

Result<void> AudioEngine::reconfigureInputResampler(uint32_t new_sample_rate) {
    if (!input_resampler_) {
        input_resampler_ = std::make_unique<Resampler>();
    }

    input_resampler_->setInputSampleRate(new_sample_rate);
    input_resampler_->setOutputSampleRate(kOpusSampleRate);

    auto res = input_resampler_->initialize();
    if (!res) {
        NEVO_LOG_ERROR("audio", "Input resampler reinit failed: {}Hz -> {}Hz: {}",
                       new_sample_rate, kOpusSampleRate, res.error().message());
        return Err<void>(res.error().code(), "Input resampler reinit failed");
    }

    NEVO_LOG_INFO("audio", "Input resampler reconfigured: {}Hz -> {}Hz",
                  new_sample_rate, kOpusSampleRate);
    return Ok();
}

Result<void> AudioEngine::reconfigureOutputResampler(uint32_t new_sample_rate) {
    if (!output_resampler_) {
        output_resampler_ = std::make_unique<Resampler>();
    }

    output_resampler_->setInputSampleRate(kOpusSampleRate);
    output_resampler_->setOutputSampleRate(new_sample_rate);

    auto res = output_resampler_->initialize();
    if (!res) {
        NEVO_LOG_ERROR("audio", "Output resampler reinit failed: {}Hz -> {}Hz: {}",
                       kOpusSampleRate, new_sample_rate, res.error().message());
        return Err<void>(res.error().code(), "Output resampler reinit failed");
    }

    NEVO_LOG_INFO("audio", "Output resampler reconfigured: {}Hz -> {}Hz",
                  kOpusSampleRate, new_sample_rate);
    return Ok();
}

uint32_t AudioEngine::inputSampleRate() const {
    return actual_input_sample_rate_.load(std::memory_order_relaxed);
}

uint32_t AudioEngine::outputSampleRate() const {
    return actual_output_sample_rate_.load(std::memory_order_relaxed);
}

// ============================================================
// 设备枚举
// ============================================================

std::vector<AudioEngine::DeviceInfo> AudioEngine::enumerateInputDevices() {
    std::vector<DeviceInfo> result;

    if (!ma_context_) {
        // shutdown() 会销毁 ma_context_，但设备枚举仍需工作
        // 惰性重建上下文，确保 UI 始终能列出设备
        auto ctx_result = initContext();
        if (!ctx_result) {
            NEVO_LOG_WARN("audio", "Cannot enumerate devices: failed to re-init ma_context");
            return result;
        }
    }

    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_devices = nullptr;
    ma_uint32 capture_count = 0;

    ma_result ma_ret = ma_context_get_devices(ma_context_.get(),
                                               &playback_devices, &playback_count,
                                               &capture_devices, &capture_count);
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "ma_context_get_devices failed: {}", ma_ret);
        return result;
    }

    for (ma_uint32 i = 0; i < capture_count; ++i) {
        DeviceInfo info;
        info.name = capture_devices[i].name;
        info.id = capture_devices[i].id;
        info.is_default = capture_devices[i].isDefault;
        result.push_back(std::move(info));
    }

    NEVO_LOG_INFO("audio", "Enumerated {} input devices", result.size());
    return result;
}

std::vector<AudioEngine::DeviceInfo> AudioEngine::enumerateOutputDevices() {
    std::vector<DeviceInfo> result;

    if (!ma_context_) {
        auto ctx_result = initContext();
        if (!ctx_result) {
            NEVO_LOG_WARN("audio", "Cannot enumerate devices: failed to re-init ma_context");
            return result;
        }
    }

    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_devices = nullptr;
    ma_uint32 capture_count = 0;

    ma_result ma_ret = ma_context_get_devices(ma_context_.get(),
                                               &playback_devices, &playback_count,
                                               &capture_devices, &capture_count);
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "ma_context_get_devices failed: {}", ma_ret);
        return result;
    }

    for (ma_uint32 i = 0; i < playback_count; ++i) {
        DeviceInfo info;
        info.name = playback_devices[i].name;
        info.id = playback_devices[i].id;
        info.is_default = playback_devices[i].isDefault;
        result.push_back(std::move(info));
    }

    NEVO_LOG_INFO("audio", "Enumerated {} output devices", result.size());
    return result;
}

// ============================================================
// 设备选择
// ============================================================

Result<void> AudioEngine::selectInputDevice(const ma_device_id& id) {
    // Always save preference (will be applied on next initialize() if engine not running)
    selected_input_id_ = id;
    has_selected_input_id_ = true;

    if (!running_.load(std::memory_order_acquire)) {
        NEVO_LOG_INFO("audio", "Input device preference saved (engine not running, will apply on next start)");
        return Ok();
    }

    // Stop current input device
    if (input_device_) {
        ma_device_stop(input_device_.get());
    }

    // Reinitialize with selected device
    auto* new_dev = new (std::nothrow) ma_device{};
    if (!new_dev) {
        // 分配失败，重启旧设备
        if (input_device_) {
            ma_device_start(input_device_.get());
        }
        return Err<void>(ResultCode::DeviceNotAvailable, "Device allocation failed");
    }

    ma_device_config dev_config = ma_device_config_init(ma_device_type_capture);
    dev_config.capture.format   = ma_format_f32;
    dev_config.capture.channels = static_cast<ma_uint32>(config_.channels);
    dev_config.sampleRate       = static_cast<ma_uint32>(config_.input_sample_rate);
    dev_config.periodSizeInFrames = static_cast<ma_uint32>(config_.frame_size);
    dev_config.dataCallback     = &AudioEngine::maInputCallback;
    dev_config.pUserData        = this;
    dev_config.capture.pDeviceID = &id;
    dev_config.capture.shareMode = ma_share_mode_shared;                // 显式共享模式
    dev_config.wasapi.usage      = ma_wasapi_usage_pro_audio;          // VoIP 优先级

    ma_result ma_ret = ma_device_init(ma_context_.get(), &dev_config, new_dev);
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Failed to init selected input device: {}", ma_ret);
        ma_device_uninit(new_dev);
        delete new_dev;
        // Restart the old device
        if (input_device_) {
            ma_device_start(input_device_.get());
        }
        if (ma_ret == MA_BUSY) {
            return Err<void>(ResultCode::DeviceInUse,
                "Input device is in use by another application. "
                "Please close other applications using the microphone or disable exclusive mode.");
        }
        return Err<void>(ResultCode::DeviceNotAvailable, "Selected input device init failed");
    }

    // Store new device info before replacing (needed for recovery)
    const uint32_t new_sample_rate = new_dev->sampleRate;
    const std::string new_name = new_dev->capture.name;

    // Success: replace old device (MaDeviceDeleter handles ma_device_uninit)
    input_device_.reset(new_dev);
    current_input_device_name_ = new_name;

    actual_input_sample_rate_.store(new_sample_rate, std::memory_order_relaxed);

    // Handle sample rate change
    if (new_sample_rate != kOpusSampleRate) {
        reconfigureInputResampler(new_sample_rate);
    } else if (input_resampler_) {
        input_resampler_->reset();
    }

    ma_ret = ma_device_start(input_device_.get());
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Failed to start selected input device: {}", ma_ret);
        // 新设备初始化成功但启动失败 — 保留新设备（已初始化），
        // 用户可尝试再次切换设备。旧设备已销毁，无法恢复。
        return Err<void>(ResultCode::DeviceNotAvailable, "Selected input device start failed");
    }

    NEVO_LOG_INFO("audio", "Switched input device to: {} ({}Hz)",
                  new_name, new_sample_rate);
    return Ok();
}

Result<void> AudioEngine::selectOutputDevice(const ma_device_id& id) {
    // Always save preference (will be applied on next initialize() if engine not running)
    selected_output_id_ = id;
    has_selected_output_id_ = true;

    if (!running_.load(std::memory_order_acquire)) {
        NEVO_LOG_INFO("audio", "Output device preference saved (engine not running, will apply on next start)");
        return Ok();
    }

    // Stop current output device
    if (output_device_) {
        ma_device_stop(output_device_.get());
    }

    auto* new_dev = new (std::nothrow) ma_device{};
    if (!new_dev) {
        // 分配失败，重启旧设备
        if (output_device_) {
            ma_device_start(output_device_.get());
        }
        return Err<void>(ResultCode::DeviceNotAvailable, "Device allocation failed");
    }

    ma_device_config dev_config = ma_device_config_init(ma_device_type_playback);
    dev_config.playback.format   = ma_format_f32;
    dev_config.playback.channels = static_cast<ma_uint32>(config_.channels);
    dev_config.sampleRate        = static_cast<ma_uint32>(config_.output_sample_rate);
    dev_config.periodSizeInFrames = static_cast<ma_uint32>(config_.frame_size);
    dev_config.dataCallback      = &AudioEngine::maOutputCallback;
    dev_config.pUserData         = this;
    dev_config.playback.pDeviceID = &id;
    dev_config.playback.shareMode = ma_share_mode_shared;              // 显式共享模式
    dev_config.wasapi.usage       = ma_wasapi_usage_pro_audio;         // VoIP 优先级

    ma_result ma_ret = ma_device_init(ma_context_.get(), &dev_config, new_dev);
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Failed to init selected output device: {}", ma_ret);
        ma_device_uninit(new_dev);
        delete new_dev;
        if (output_device_) {
            ma_device_start(output_device_.get());
        }
        if (ma_ret == MA_BUSY) {
            return Err<void>(ResultCode::DeviceInUse,
                "Output device is in use by another application. "
                "Please close other applications using the speaker or disable exclusive mode.");
        }
        return Err<void>(ResultCode::DeviceNotAvailable, "Selected output device init failed");
    }

    const uint32_t new_sample_rate = new_dev->sampleRate;
    const std::string new_name = new_dev->playback.name;

    output_device_.reset(new_dev);
    current_output_device_name_ = new_name;

    actual_output_sample_rate_.store(new_sample_rate, std::memory_order_relaxed);

    if (new_sample_rate != kOpusSampleRate) {
        reconfigureOutputResampler(new_sample_rate);
    } else if (output_resampler_) {
        output_resampler_->reset();
    }

    ma_ret = ma_device_start(output_device_.get());
    if (ma_ret != MA_SUCCESS) {
        NEVO_LOG_ERROR("audio", "Failed to start selected output device: {}", ma_ret);
        return Err<void>(ResultCode::DeviceNotAvailable, "Selected output device start failed");
    }

    NEVO_LOG_INFO("audio", "Switched output device to: {} ({}Hz)",
                  new_name, new_sample_rate);
    return Ok();
}

Result<void> AudioEngine::selectInputDeviceByName(const std::string& name) {
    auto devices = enumerateInputDevices();
    for (const auto& dev : devices) {
        if (dev.name == name) {
            std::string old_name = current_input_device_name_;
            current_input_device_name_ = name;
            auto result = selectInputDevice(dev.id);
            if (!result) {
                current_input_device_name_ = std::move(old_name);
            }
            return result;
        }
    }
    return Err<void>(ResultCode::DeviceNotAvailable,
                     "Input device not found: " + name);
}

Result<void> AudioEngine::selectOutputDeviceByName(const std::string& name) {
    auto devices = enumerateOutputDevices();
    for (const auto& dev : devices) {
        if (dev.name == name) {
            std::string old_name = current_output_device_name_;
            current_output_device_name_ = name;
            auto result = selectOutputDevice(dev.id);
            if (!result) {
                current_output_device_name_ = std::move(old_name);
            }
            return result;
        }
    }
    return Err<void>(ResultCode::DeviceNotAvailable,
                     "Output device not found: " + name);
}

// ============================================================
// 测试音 & 输入电平
// ============================================================

Result<void> AudioEngine::playTestTone(float frequency, float duration_sec) {
    if (!output_device_ || !running_.load(std::memory_order_acquire)) {
        return Err<void>(ResultCode::InvalidRequest, "AudioEngine not running");
    }

    if (test_tone_playing_.exchange(true)) {
        return Ok();  // Already playing, ignore
    }

    // Generate sine wave into output FIFO
    const uint32_t total_samples = static_cast<uint32_t>(
        kOpusSampleRate * duration_sec);
    const uint32_t total_frames = (total_samples + kFrameSize - 1) / kFrameSize;
    const float omega = 2.0f * 3.14159265358979f * frequency / static_cast<float>(kOpusSampleRate);

    uint32_t samples_generated = 0;
    for (uint32_t f = 0; f < total_frames; ++f) {
        AudioFrame frame{};
        const uint32_t samples_this_frame = std::min(
            static_cast<uint32_t>(kFrameSize), total_samples - samples_generated);

        for (uint32_t i = 0; i < samples_this_frame; ++i) {
            frame[i] = 0.3f * std::sin(omega * static_cast<float>(samples_generated + i));
        }

#ifdef NEVO_HAS_BOOST
        output_fifo_.push(frame);
#else
        {
            std::lock_guard<std::mutex> lock(output_fifo_mutex_);
            if (output_fifo_.size() < kFifoCapacity) {
                output_fifo_.push_back(frame);
            }
        }
#endif
        samples_generated += samples_this_frame;
    }

    test_tone_playing_.store(false, std::memory_order_relaxed);
    NEVO_LOG_INFO("audio", "Test tone: {:.0f}Hz for {:.1f}s", frequency, duration_sec);
    return Ok();
}

float AudioEngine::getCurrentInputLevel() const {
    float level = input_peak_level_.load(std::memory_order_relaxed);
    // Clamp and normalize
    if (level != level) return 0.0f;  // NaN check
    return std::clamp(level, 0.0f, 1.0f);
}

void AudioEngine::setInputLevelCallback(InputLevelCallback cb) {
    // First invalidate so audio thread stops using old callback
    input_level_callback_valid_.store(false, std::memory_order_release);
    // Update the callback
    input_level_callback_ = std::move(cb);
    // Validate if the new callback is non-empty
    if (input_level_callback_) {
        input_level_callback_valid_.store(true, std::memory_order_release);
    }
}

void AudioEngine::setMonitorEnabled(bool enabled) {
    monitor_enabled_.store(enabled, std::memory_order_release);
    // 不在主线程排空 monitor_fifo_！
    // 输出回调因 monitor_enabled_=false 而停止消费，FIFO 中残留帧在下次启用时
    // 由输出回调自然消费（偏移量会被重置）。shutdown() 中会在回调停止后排空。
}

void AudioEngine::setMonitorVolume(float volume) {
    monitor_volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

std::string AudioEngine::currentInputDeviceName() const {
    return current_input_device_name_;
}

std::string AudioEngine::currentOutputDeviceName() const {
    return current_output_device_name_;
}

} // namespace nevo
