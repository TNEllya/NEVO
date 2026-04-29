/**
 * @file AudioInput.cpp
 * @brief AudioInput 实现 - 麦克风采集 → Opus 编码 → 网络发送
 *
 * 本文件实现了 AudioInput 的所有核心功能：
 *
 *   1. 向 AudioEngine 注册编码回调，接收 Opus 编码后的语音数据
 *   2. FEC 冗余度计算（根据 VAD 结果动态调整）
 *   3. 静音帧过滤
 *   4. 通过 NetworkManager.sendVoicePacket() 发送语音数据
 */

#include "nevo/client/AudioInput.h"
#include "nevo/client/NetworkManager.h"

#include "nevo/core/audio/AudioEngine.h"
#include "nevo/core/common/Logger.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

AudioInput::AudioInput()
{
    NEVO_LOG_INFO("audio", "AudioInput created");
}

AudioInput::~AudioInput()
{
    stop();
    NEVO_LOG_INFO("audio", "AudioInput destroyed");
}

// ============================================================
// 生命周期管理
// ============================================================

Result<void> AudioInput::start(AudioEngine& engine, NetworkManager& network)
{
    if (running_.load(std::memory_order_acquire)) {
        NEVO_LOG_WARN("audio", "AudioInput already running");
        return Ok();
    }

    engine_ = &engine;
    network_ = &network;

    // 向 AudioEngine 注册编码回调
    // 当 Opus 编码完成一帧数据时，AudioEngine 会调用此回调
    engine_->setInputCallback(
        [this](const uint8_t* opus_data, uint32_t data_size, bool vad_result) {
            onEncodedAudio(opus_data, data_size, vad_result);
        });

    running_.store(true, std::memory_order_release);

    NEVO_LOG_INFO("audio", "AudioInput started");
    return Ok();
}

void AudioInput::stop()
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);

    // 注销 AudioEngine 的编码回调
    if (engine_ != nullptr) {
        engine_->setInputCallback(nullptr);
    }

    engine_ = nullptr;
    network_ = nullptr;

    NEVO_LOG_INFO("audio", "AudioInput stopped");
}

bool AudioInput::isRunning() const
{
    return running_.load(std::memory_order_acquire);
}

// ============================================================
// 配置
// ============================================================

void AudioInput::setFecConfig(const FecConfig& config)
{
    fec_config_ = config;
}

const FecConfig& AudioInput::fecConfig() const
{
    return fec_config_;
}

void AudioInput::setMuted(bool muted)
{
    muted_.store(muted, std::memory_order_release);
    NEVO_LOG_INFO("audio", "AudioInput muted={}", muted);
}

bool AudioInput::isMuted() const
{
    return muted_.load(std::memory_order_acquire);
}

// ============================================================
// 统计信息
// ============================================================

AudioInput::Stats AudioInput::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void AudioInput::resetStats()
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Stats{};
}

// ============================================================
// 内部处理
// ============================================================

void AudioInput::onEncodedAudio(const uint8_t* opus_data,
                                uint32_t data_size,
                                bool vad_result)
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // ------------------------------------------------------------------
    // 1. 检查静音状态
    // ------------------------------------------------------------------
    if (muted_.load(std::memory_order_acquire)) {
        // 用户已静音，丢弃编码帧
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.frames_silenced++;
        return;
    }

    // ------------------------------------------------------------------
    // 2. 静音帧过滤（节省带宽）
    // ------------------------------------------------------------------
    if (!vad_result && fec_config_.skip_silence_frames) {
        // 非语音帧且配置了跳过静音帧
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.frames_skipped++;
        return;
    }

    // ------------------------------------------------------------------
    // 3. 计算 FEC 冗余度并应用到 Opus 编码器
    // ------------------------------------------------------------------
    uint32_t fec_redundancy = calculateFecRedundancy(vad_result);

    // 将 FEC 冗余度应用到 Opus 编码器
    // 设置 in-band FEC 和期望丢包率百分比
    if (engine_ != nullptr) {
        engine_->setFecEnabled(fec_redundancy > 0);
        engine_->setPacketLossPerc(static_cast<int32_t>(fec_redundancy));
    }

    // ------------------------------------------------------------------
    // 4. 通过 NetworkManager 发送语音包
    // ------------------------------------------------------------------
    // 注意：sendVoicePacket 是协程函数，不能在回调中直接 co_await。
    // 我们使用 boost::asio::co_spawn 在 io_context 上启动协程。
    //
    // 需要复制数据，因为回调结束后 opus_data 指针可能失效。
    if (network_ != nullptr) {
        // 复制语音数据到堆上，保证协程执行期间数据有效
        auto data_copy = std::make_shared<std::vector<uint8_t>>(
            opus_data, opus_data + data_size);

        // 在 NetworkManager 的 io_context 上派发异步发送任务
        NetworkManager* net_ptr = network_;
        boost::asio::co_spawn(
            net_ptr->ioContext(),
            [net_ptr, data_copy]() -> boost::asio::awaitable<void> {
                auto result = co_await net_ptr->sendVoicePacket(
                    data_copy->data(),
                    static_cast<uint32_t>(data_copy->size()));

                if (!result) {
                    NEVO_LOG_WARN("audio", "Failed to send voice packet: {}",
                                 result.error().message());
                }
            },
            boost::asio::detached);

        // 更新统计
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_sent++;
            stats_.bytes_sent += data_size;
        }
    }
}

uint32_t AudioInput::calculateFecRedundancy(bool is_voice) const
{
    if (is_voice) {
        // 语音帧：使用较高的 FEC 冗余度以确保语音质量
        return fec_config_.voice_fec_percentage;
    } else {
        // 静音帧：使用较低的 FEC 冗余度节省带宽
        return fec_config_.silence_fec_percentage;
    }
}

} // namespace nevo
