#pragma once
/**
 * @file AudioInput.h
 * @brief 音频输入模块 - 麦克风采集 → Opus 编码 → 网络发送
 *
 * AudioInput 是 NEVO VoIP 客户端的音频输入管线桥接器，连接 AudioEngine
 * 的编码输出和 NetworkManager 的语音发送接口。
 *
 * 工作流程：
 *
 *   [麦克风] → AudioEngine(miniaudio) → Opus 编码 → EncodedAudioCallback
 *                                                             |
 *   AudioInput 注册此回调 ─────────────────────────────────────┘
 *       |
 *       ├── 计算 FEC 冗余度
 *       ├── 通过 NetworkManager.sendVoicePacket() 发送
 *       └── (如果用户 Mute 则丢弃)
 *
 * FEC 冗余策略：
 *   根据 VAD 检测结果动态调整冗余度：
 *   - 语音帧：较高冗余度（确保语音质量）
 *   - 静音帧：低冗余度或无冗余（节省带宽）
 *
 * 线程安全：
 *   - start()/stop() 应在主线程调用
 *   - 编码回调在 AudioEngine 的编码线程中触发
 */

#include <atomic>
#include <cstdint>
#include <memory>

#include "nevo/core/common/Result.h"

namespace nevo {

class AudioEngine;
class NetworkManager;

// ============================================================
// FEC 冗余配置
// ============================================================

/// FEC 冗余策略配置
struct FecConfig {
    /// 语音帧 FEC 冗余百分比（0~100）
    /// Opus 编码器会在主帧中嵌入前向纠错信息
    /// 推荐值：15~50，值越高抗丢包能力越强但带宽越大
    uint32_t voice_fec_percentage = 25;

    /// 静音帧 FEC 冗余百分比
    /// 静音帧丢失影响较小，可以使用较低的冗余度
    uint32_t silence_fec_percentage = 5;

    /// 是否在静音帧时完全跳过发送（节省带宽）
    /// 注意：完全跳过可能导致对端 VAD 误判
    bool skip_silence_frames = false;
};

// ============================================================
// AudioInput 类
// ============================================================

/**
 * @class AudioInput
 * @brief 音频输入管线桥接器
 *
 * 将 AudioEngine 的 Opus 编码输出桥接到 NetworkManager 的语音发送接口。
 * 自动处理 FEC 冗余计算和静音帧过滤。
 *
 * 典型用法：
 * @code
 *   AudioInput input;
 *   input.start(audio_engine, network_manager);
 *   // AudioEngine 编码后的数据会自动通过 NetworkManager 发送
 *   input.stop();
 * @endcode
 */
class AudioInput {
public:
    /// 构造函数
    AudioInput();

    /// 析构函数：确保停止并注销回调
    ~AudioInput();

    // 禁止拷贝和移动
    AudioInput(const AudioInput&) = delete;
    AudioInput& operator=(const AudioInput&) = delete;
    AudioInput(AudioInput&&) = delete;
    AudioInput& operator=(AudioInput&&) = delete;

    // ============================================================
    // 生命周期管理
    // ============================================================

    /**
     * @brief 启动音频输入
     *
     * 向 AudioEngine 注册编码回调，当有 Opus 编码数据时
     * 自动通过 NetworkManager 发送。
     *
     * @param engine  音频引擎引用
     * @param network 网络管理器引用
     * @return Result<void> 启动结果
     */
    Result<void> start(AudioEngine& engine, NetworkManager& network);

    /**
     * @brief 停止音频输入
     *
     * 注销 AudioEngine 的编码回调，停止发送语音数据。
     */
    void stop();

    /// 查询是否正在运行
    /// @return true 表示音频输入已启动
    bool isRunning() const;

    // ============================================================
    // 配置
    // ============================================================

    /// 设置 FEC 冗余配置
    /// @param config FEC 配置
    void setFecConfig(const FecConfig& config);

    /// 获取当前 FEC 配置
    /// @return FEC 配置的常引用
    const FecConfig& fecConfig() const;

    /// 设置静音状态（由 ClientCore 控制）
    /// 当用户主动静音时，停止发送语音数据
    /// @param muted true 表示静音
    void setMuted(bool muted);

    /// 查询当前静音状态
    /// @return true 表示已静音
    bool isMuted() const;

    // ============================================================
    // 统计信息
    // ============================================================

    /// 音频输入统计
    struct Stats {
        uint64_t frames_sent = 0;       ///< 已发送帧数
        uint64_t frames_silenced = 0;   ///< 因静音丢弃的帧数
        uint64_t frames_skipped = 0;    ///< 因 skip_silence 跳过的帧数
        uint64_t bytes_sent = 0;        ///< 已发送字节数
    };

    /// 获取统计信息
    /// @return 统计快照
    Stats stats() const;

    /// 重置统计信息
    void resetStats();

private:
    // ============================================================
    // 内部处理
    // ============================================================

    /**
     * @brief 编码回调处理函数
     *
     * 当 AudioEngine 编码完成一帧 Opus 数据时调用。
     * 处理流程：
     *   1. 检查是否静音 → 静音则丢弃
     *   2. 检查是否为静音帧 + skip_silence → 跳过
     *   3. 根据 VAD 结果计算 FEC 冗余度
     *   4. 通过 NetworkManager.sendVoicePacket() 发送
     *
     * @param opus_data  Opus 编码数据指针
     * @param data_size  编码数据字节数
     * @param vad_result VAD 检测结果（true=语音帧）
     */
    void onEncodedAudio(const uint8_t* opus_data,
                        uint32_t data_size,
                        bool vad_result);

    /**
     * @brief 计算 FEC 冗余度
     *
     * 根据 VAD 结果和 FEC 配置计算当前帧的冗余度。
     * 语音帧使用 voice_fec_percentage，静音帧使用 silence_fec_percentage。
     *
     * @param is_voice 是否为语音帧（VAD 检测结果）
     * @return FEC 冗余百分比
     */
    uint32_t calculateFecRedundancy(bool is_voice) const;

    // ============================================================
    // 数据成员
    // ============================================================

    /// 音频引擎指针（不拥有所有权）
    AudioEngine* engine_ = nullptr;

    /// 网络管理器指针（不拥有所有权）
    NetworkManager* network_ = nullptr;

    /// 是否正在运行
    std::atomic<bool> running_{false};

    /// 是否静音
    std::atomic<bool> muted_{false};

    /// FEC 配置
    FecConfig fec_config_;

    /// 统计信息
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

} // namespace nevo
