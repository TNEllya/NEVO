#pragma once
/**
 * @file AudioEngine.h
 * @brief 音频引擎 - 中央音频管线管理器
 *
 * AudioEngine 是 NEVO VoIP 系统的核心音频协调器，管理从麦克风采集到扬声器输出的
 * 完整音频管线。设计原则：
 *
 *   1. 实时安全：miniaudio 回调中绝不允许内存分配、互斥锁、系统调用
 *   2. 零拷贝传输：使用 lock-free SPSC 队列连接实时线程和工作线程
 *   3. RAII 管理：所有 C 资源（miniaudio、Opus）通过 unique_ptr+deleter 管理
 *
 * 音频管线架构：
 *
 *   [麦克风] ──miniaudio input callback──> input_fifo ──网络线程──> Opus编码 ──> 网络
 *                                                                      ^
 *   [扬声器] <──miniaudio output callback── output_fifo <──网络线程── Opus解码 <── 网络
 *
 * 线程模型：
 *   - 实时音频线程：miniaudio 回调（input_callback / output_callback）
 *   - 网络线程：Opus 编解码、JitterBuffer、Mixer
 *   - 主线程：UI 交互、设备管理、VAD/PTT 控制
 *
 * 蓝牙耳机支持：
 *   当蓝牙设备采样率不同于 48kHz 时，Resampler 自动进行采样率转换。
 *   onDeviceSampleRateChanged() 在设备切换时重新配置 Resampler。
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>
#include <deque>

#ifdef NEVO_HAS_BOOST
#include <boost/lockfree/spsc_queue.hpp>
#endif

#include "nevo/core/common/Result.h"
#include "nevo/core/common/Types.h"
#include "nevo/core/audio/OpusEncoder.h"
#include "nevo/core/audio/OpusDecoder.h"
#include "nevo/core/audio/VoiceActivity.h"
#include "nevo/core/audio/AudioMemoryPool.h"
#include "nevo/core/audio/Resampler.h"

#ifdef NEVO_HAS_RNNOISE
#include <rnnoise.h>
#endif

// miniaudio 类型已通过 Resampler.h -> miniaudio.h 引入，无需前向声明

namespace nevo {

// 前向声明：AudioMixer/JitterBuffer 使用 unique_ptr，仅需前向声明
// 完整头文件在 AudioEngine.cpp 中 include
class JitterBuffer;
class AudioMixer;

// ============================================================
// 常量定义
// ============================================================

/// Opus 固定工作采样率
constexpr uint32_t kOpusSampleRate = 48000;

/// 每帧采样数（20ms @ 48kHz mono）
constexpr uint32_t kFrameSize = 960;

/// 声道数（VoIP 单声道）
constexpr uint32_t kChannels = 1;

/// SPSC 队列容量（帧数），约 640ms 缓冲 @ 20ms/帧
constexpr uint32_t kFifoCapacity = 32;

// ============================================================
// AudioFrame - 单帧音频数据
// ============================================================

/// 一帧 PCM float32 音频数据：20ms @ 48kHz mono = 960 采样
using AudioFrame = std::array<float, kFrameSize>;

// ============================================================
// 回调类型定义
// ============================================================

/// 编码后音频数据回调（网络线程调用）
/// @param opus_data Opus 编码数据指针
/// @param data_size 编码数据字节数
/// @param vad_result VAD 检测结果（true=语音帧）
using EncodedAudioCallback = std::function<void(const uint8_t* opus_data,
                                                 uint32_t data_size,
                                                 bool vad_result)>;

// ============================================================
// AudioEngine - 中央音频管线管理器
// ============================================================

class AudioEngine {
public:
    /// 引擎配置
    struct Config {
        uint32_t input_sample_rate = kOpusSampleRate;   // 输入设备采样率
        uint32_t output_sample_rate = kOpusSampleRate;  // 输出设备采样率
        uint32_t frame_size = kFrameSize;               // 每帧采样数
        uint32_t channels = kChannels;                  // 声道数

        // Opus 编码器配置
        OpusEncoderWrapper::Config encoder_config;

        // Opus 解码器配置
        OpusDecoderWrapper::Config decoder_config;

        // VAD/PTT 配置
        VoiceActivity::Config vad_config;

        // 音量
        float input_gain = 1.0f;        // 输入增益（0.0~2.0）
        float output_volume = 1.0f;     // 输出音量（0.0~1.0）
    };

    AudioEngine();
    ~AudioEngine();

    // 禁止拷贝和移动（含 ma_device 等不可移动资源）
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    // ============================================================
    // 生命周期管理
    // ============================================================

    /// 初始化音频上下文（仅创建 ma_context_，不启动设备）
    /// 允许在连接前枚举音频设备。若 ma_context_ 已存在则直接返回成功。
    Result<void> initContext();

    /// 初始化音频引擎
    /// - 创建 ma_context
    /// - 初始化输入/输出 ma_device
    /// - 初始化 Opus 编码器/解码器
    /// - 初始化 JitterBuffer、Mixer、VAD、内存池、Resampler
    /// - 启动 miniaudio 设备
    ///
    /// 不可在实时线程中调用。初始化后音频回调即开始运行。
        Result<void> initialize() { return initialize(Config{}); }
    Result<void> initialize(const Config& config);

    /// 关闭音频引擎
    /// - 停止 miniaudio 设备
    /// - 等待回调退出
    /// - 释放所有资源
    ///
    /// 不可在实时线程中调用。
    void shutdown();

    /// 查询引擎是否已初始化并运行
    bool isRunning() const;

    // ============================================================
    // 音频数据流 - 网络线程侧
    // ============================================================

    /// 注册编码音频回调
    /// 当本地麦克风数据经 Opus 编码后，通过此回调发送到网络层
    /// @param callback 编码数据回调函数
    void setInputCallback(EncodedAudioCallback callback);

    /// 接收远端用户的 Opus 编码数据
    /// 网络层收到远端音频包后调用此方法
    /// 处理流程：Opus 解码 → JitterBuffer → Mixer → push output_fifo → 播放
    ///
    /// @param user_id  远端用户 ID
    /// @param opus_data Opus 编码数据
    /// @param data_size 数据大小（字节）
    Result<void> queueAudioData(UserId user_id,
                                const uint8_t* opus_data,
                                uint32_t data_size);

    /// 移除远端用户的解码器（用户离开频道时调用）
    void removeRemoteUser(UserId user_id);

    // ============================================================
    // VAD / PTT 控制（主线程调用）
    // ============================================================

    /// 设置 PTT 按键状态
    void setPttActive(bool active);

    /// 启用/禁用 VAD
    void setVadEnabled(bool enabled);

    /// 启用/禁用 PTT
    void setPttEnabled(bool enabled);

    /// 查询本地用户是否正在说话
    bool isSpeaking() const;

    // ============================================================
    // 音量控制（主线程调用）
    // ============================================================

    /// 设置输入增益（0.0~2.0，1.0=原始音量）
    void setInputGain(float gain);

    /// 设置输出音量（0.0~1.0）
    void setOutputVolume(float volume);

    /// 设置 FEC 启用状态（转发到 OpusEncoder）
    void setFecEnabled(bool enabled);

    /// 设置期望丢包率百分比（0~100，转发到 OpusEncoder）
    void setPacketLossPerc(int32_t percentage);

    /// 启用/禁用 RNNoise 降噪
    void setNoiseSuppressionEnabled(bool enabled);

    /// 查询 RNNoise 降噪是否可用（编译时决定）
    bool isNoiseSuppressionAvailable() const;

    /// 设置 VAD 灵敏度（0~100）
    void setVadSensitivity(int sensitivity);

    /// 获取当前输入增益
    float inputGain() const;

    /// 获取当前输出音量
    float outputVolume() const;

    // ============================================================
    // 设备管理（主线程调用）
    // ============================================================

    /// 音频设备信息
    struct DeviceInfo {
        std::string name;           ///< 设备名称
        ma_device_id id;            ///< miniaudio 设备 ID
        bool is_default = false;    ///< 是否为系统默认设备
    };

    /// 枚举所有可用的输入音频设备
    std::vector<DeviceInfo> enumerateInputDevices();

    /// 枚举所有可用的输出音频设备
    std::vector<DeviceInfo> enumerateOutputDevices();

    /// 选择输入设备（按 ma_device_id）
    /// 切换设备时会短暂中断音频采集
    Result<void> selectInputDevice(const ma_device_id& id);

    /// 选择输出设备（按 ma_device_id）
    /// 切换设备时会短暂中断音频播放
    Result<void> selectOutputDevice(const ma_device_id& id);

    /// 按设备名称选择输入设备
    Result<void> selectInputDeviceByName(const std::string& name);

    /// 按设备名称选择输出设备
    Result<void> selectOutputDeviceByName(const std::string& name);

    /// 获取当前输入设备名称（空字符串表示使用默认设备）
    std::string currentInputDeviceName() const;

    /// 获取当前输出设备名称（空字符串表示使用默认设备）
    std::string currentOutputDeviceName() const;

    /// 播放测试音（正弦波）
    /// @param frequency  频率（Hz），默认 440Hz（A4 音高）
    /// @param duration_sec  持续时间（秒），默认 1.0 秒
    Result<void> playTestTone(float frequency = 440.0f, float duration_sec = 1.0f);

    /// 获取当前输入电平（0.0~1.0 峰值）
    /// 从实时回调中获取最新峰值，线程安全
    float getCurrentInputLevel() const;

    /// 输入电平回调类型
    using InputLevelCallback = std::function<void(float level)>;

    /// 设置输入电平回调（从音频线程调用，消费者必须分发到 UI 线程）
    void setInputLevelCallback(InputLevelCallback cb);

    // ============================================================
    // 麦克风监听（主线程调用）
    // ============================================================

    /// 启用/禁用麦克风本地监听（将采集的音频路由到扬声器）
    /// 用于"测试输入"功能，使用户能听到自己的麦克风声音
    void setMonitorEnabled(bool enabled);

    /// 设置监听音量（0.0~1.0，默认 0.8）
    void setMonitorVolume(float volume);

    /// 处理音频设备采样率变化
    /// 当蓝牙耳机连接/断开导致设备采样率改变时调用
    /// 内部会重新配置 Resampler 并可能重启设备
    ///
    /// @param new_sample_rate 新的设备采样率
    Result<void> onDeviceSampleRateChanged(uint32_t new_sample_rate);

    /// 获取当前输入设备采样率
    uint32_t inputSampleRate() const;

    /// 获取当前输出设备采样率
    uint32_t outputSampleRate() const;

private:
    // ============================================================
    // miniaudio 回调（实时线程，严格实时安全约束）
    // ============================================================

    /// miniaudio 输入回调（麦克风数据就绪时调用）
    /// 执行流程：读取 PCM → 应用增益 → VAD 检测 → push 到 input_fifo
    ///
    /// !!! 实时安全约束 !!!
    /// - 绝不允许 malloc/new/free/delete
    /// - 绝不允许 mutex lock（包括 std::map、std::string 操作）
    /// - 绝不允许系统调用（文件 I/O、网络、日志）
    /// - 绝不允许异常抛出
    /// - 仅允许：原子操作、SPSC 队列 push/pop、memcpy、算术运算
    static void maInputCallback(ma_device* p_device,
                                void* p_output,
                                const void* p_input,
                                ma_uint32 frame_count);

    /// miniaudio 输出回调（扬声器需要数据时调用）
    /// 执行流程：从 output_fifo pop 帧 → 写入输出缓冲区
    ///
    /// !!! 实时安全约束 !!!
    /// - 同 maInputCallback 的所有约束
    static void maOutputCallback(ma_device* p_device,
                                 void* p_output,
                                 const void* p_input,
                                 ma_uint32 frame_count);

    // ============================================================
    // 网络线程处理
    // ============================================================

    /// 从 input_fifo 读取数据、Opus 编码、调用回调
    /// 由专用编码线程调用
    void processEncodeCycle();

    /// 编码线程入口函数
    void encodeThreadFunc(std::stop_token stop_token);

    /// 处理混音周期：从 JitterBuffer 读取各用户数据 → Mixer → push output_fifo
    void processMixCycle();

    // ============================================================
    // 内部辅助
    // ============================================================

    /// 获取或创建远端用户的 Opus 解码器
    /// @return 解码器指针，失败返回 nullptr
    OpusDecoderWrapper* getOrCreateDecoder(UserId user_id);

    /// 重新配置输入 Resampler
    Result<void> reconfigureInputResampler(uint32_t new_sample_rate);

    /// 重新配置输出 Resampler
    Result<void> reconfigureOutputResampler(uint32_t new_sample_rate);

    // ============================================================
    // 数据成员
    // ============================================================

    // --- 引擎状态 ---
    std::atomic<bool> running_{false};
    Config config_;

    // --- 编码线程 ---
    std::jthread encode_thread_;

    // --- miniaudio 设备管理 ---
    // 使用 unique_ptr 延迟构造，避免默认构造 ma_device（POD 需要显式初始化）
    struct MaDeviceDeleter {
        void operator()(ma_device* device) const;
    };
    struct MaContextDeleter {
        void operator()(ma_context* context) const;
    };
    std::unique_ptr<ma_device, MaDeviceDeleter> input_device_;
    std::unique_ptr<ma_device, MaDeviceDeleter> output_device_;
    std::unique_ptr<ma_context, MaContextDeleter> ma_context_;

    // --- Opus 编解码 ---
    std::unique_ptr<OpusEncoderWrapper> encoder_;
    std::map<UserId, std::unique_ptr<OpusDecoderWrapper>> decoders_;
    std::mutex decoders_mutex_;    // 保护 decoders_ map（仅网络线程和 removeRemoteUser 使用）

    // --- JitterBuffer & Mixer ---
    std::unique_ptr<JitterBuffer> jitter_buffer_;
    std::unique_ptr<AudioMixer> mixer_;

    // --- VAD ---
    std::unique_ptr<VoiceActivity> voice_activity_;

    // --- 内存池 ---
    std::unique_ptr<AudioMemoryPool> memory_pool_;

    // --- Resampler ---
    // 输入端重采样器：设备采样率 → 48kHz（如蓝牙 16kHz → 48kHz）
    std::unique_ptr<Resampler> input_resampler_;
    // 输出端重采样器：48kHz → 设备采样率（如 48kHz → 蓝牙 16kHz）
    std::unique_ptr<Resampler> output_resampler_;

    // --- Lock-free 队列 ---
#ifdef NEVO_HAS_BOOST
    // input_fifo: 实时输入回调 → 编码线程
    //   回调侧 push，编码侧 pop
    boost::lockfree::spsc_queue<AudioFrame, boost::lockfree::capacity<kFifoCapacity>> input_fifo_;

    // output_fifo: 混音线程 → 实时输出回调
    //   混音侧 push，回调侧 pop
    boost::lockfree::spsc_queue<AudioFrame, boost::lockfree::capacity<kFifoCapacity>> output_fifo_;

    // monitor_fifo: 实时输入回调 → 实时输出回调
    //   输入回调 push（当 monitor_enabled_），输出回调 pop 并混入扬声器输出
    boost::lockfree::spsc_queue<AudioFrame, boost::lockfree::capacity<kFifoCapacity>> monitor_fifo_;
#else
    // Fallback: simple mutex-protected queue when Boost is not available
    // Note: This is NOT real-time safe and should only be used for building without Boost
    std::mutex input_fifo_mutex_;
    std::deque<AudioFrame> input_fifo_;
    std::mutex output_fifo_mutex_;
    std::deque<AudioFrame> output_fifo_;
    std::mutex monitor_fifo_mutex_;
    std::deque<AudioFrame> monitor_fifo_;
#endif

    // --- 音量控制（原子变量，实时回调安全读取） ---
    std::atomic<float> input_gain_{1.0f};
    std::atomic<float> output_volume_{1.0f};

    // --- 降噪控制（原子变量，实时回调安全读取） ---
    std::atomic<bool> noise_suppression_enabled_{false};
#ifdef NEVO_HAS_RNNOISE
    DenoiseState* rnnoise_state_ = nullptr;
#endif

    // --- 麦克风监听控制（原子变量，实时回调安全读取） ---
    std::atomic<bool> monitor_enabled_{false};
    std::atomic<float> monitor_volume_{0.8f};

    // --- 编码回调 ---
    EncodedAudioCallback encoded_callback_;
    std::mutex callback_mutex_;    // 保护 encoded_callback_ 的设置

    // --- 混音周期互斥锁 ---
    std::mutex mix_cycle_mutex_;   // 保护 processMixCycle 的并发调用

    // --- 设备采样率（原子变量，供回调读取） ---
    std::atomic<uint32_t> actual_input_sample_rate_{kOpusSampleRate};
    std::atomic<uint32_t> actual_output_sample_rate_{kOpusSampleRate};

    // --- 选择的设备 ID（主线程管理） ---
    ma_device_id selected_input_id_{};
    ma_device_id selected_output_id_{};
    bool has_selected_input_id_ = false;
    bool has_selected_output_id_ = false;
    std::string current_input_device_name_;
    std::string current_output_device_name_;

    // --- 输入电平（实时回调写入，主线程读取） ---
    std::atomic<float> input_peak_level_{0.0f};
    std::atomic<bool> input_level_callback_valid_{false};
    InputLevelCallback input_level_callback_;

    // --- 测试音状态 ---
    std::atomic<bool> test_tone_playing_{false};

    // --- 输出回调帧累积状态（仅在输出回调线程中使用） ---
    // 解决 frame_count != kFrameSize 时的帧浪费问题
    AudioFrame output_current_frame_{};
    uint32_t output_frame_offset_ = 0;
    bool output_frame_valid_ = false;

    AudioFrame monitor_current_frame_{};
    uint32_t monitor_frame_offset_ = 0;
    bool monitor_frame_valid_ = false;

    // --- 临时重采样缓冲区（编码线程使用，非实时） ---
    std::vector<float> resample_input_buffer_;
    std::vector<float> resample_output_buffer_;
};

} // namespace nevo
