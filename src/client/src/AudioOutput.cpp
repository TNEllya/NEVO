/**
 * @file AudioOutput.cpp
 * @brief AudioOutput 实现 - 接收 → 解密 → 解码 → 混音 → 播放
 *
 * 本文件实现了 AudioOutput 的所有核心功能：
 *
 *   1. 向 NetworkManager 注册语音包回调，接收已解密的语音数据
 *   2. 解析语音包头，提取发送者 user_id
 *   3. 按 user_id 分发到 AudioEngine 的对应解码通道
 *   4. 远端用户的解码器管理（添加/移除）
 *   5. 耳聋模式下丢弃所有收到的语音包
 */

#include "nevo/client/AudioOutput.h"
#include "nevo/client/NetworkManager.h"

#include "nevo/core/audio/AudioEngine.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"

// Protobuf 头文件
#include "voice.pb.h"

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

AudioOutput::AudioOutput()
{
    NEVO_LOG_INFO("audio", "AudioOutput created");
}

AudioOutput::~AudioOutput()
{
    stop();
    NEVO_LOG_INFO("audio", "AudioOutput destroyed");
}

// ============================================================
// 生命周期管理
// ============================================================

Result<void> AudioOutput::start(AudioEngine& engine, NetworkManager& network)
{
    if (running_.load(std::memory_order_acquire)) {
        NEVO_LOG_WARN("audio", "AudioOutput already running");
        return Ok();
    }

    engine_ = &engine;
    network_ = &network;

    // 向 NetworkManager 注册语音包回调
    // 当收到并解密语音包后，NetworkManager 会调用此回调
    network_->onVoicePacket = [this](const uint8_t* data, uint32_t size,
                                      const boost::asio::ip::udp::endpoint& sender) {
        onVoicePacketReceived(data, size, sender);
    };

    running_.store(true, std::memory_order_release);

    NEVO_LOG_INFO("audio", "AudioOutput started");
    return Ok();
}

void AudioOutput::stop()
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);

    // 注销 NetworkManager 的语音包回调
    if (network_ != nullptr) {
        network_->onVoicePacket = nullptr;
    }

    // 清理所有远端用户的解码器
    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        for (const auto& user_id : remote_users_) {
            if (engine_ != nullptr) {
                engine_->removeRemoteUser(user_id);
            }
        }
        remote_users_.clear();
    }

    engine_ = nullptr;
    network_ = nullptr;

    NEVO_LOG_INFO("audio", "AudioOutput stopped");
}

bool AudioOutput::isRunning() const
{
    return running_.load(std::memory_order_acquire);
}

// ============================================================
// 用户管理
// ============================================================

void AudioOutput::addRemoteUser(UserId user_id)
{
    std::lock_guard<std::mutex> lock(users_mutex_);

    if (remote_users_.count(user_id) > 0) {
        NEVO_LOG_DEBUG("audio", "Remote user {} already tracked", user_id.value);
        return;
    }

    remote_users_.insert(user_id);

    // AudioEngine 会在 queueAudioData() 时自动创建解码器，
    // 但我们可以预先创建以确保就绪
    // 注意：AudioEngine.getOrCreateDecoder() 是私有方法，
    // 解码器会在第一次 queueAudioData() 调用时自动创建

    NEVO_LOG_INFO("audio", "Added remote user {} (total: {})",
                 user_id.value, remote_users_.size());
}

void AudioOutput::removeRemoteUser(UserId user_id)
{
    std::lock_guard<std::mutex> lock(users_mutex_);

    if (remote_users_.erase(user_id) == 0) {
        NEVO_LOG_DEBUG("audio", "Remote user {} not found for removal", user_id.value);
        return;
    }

    // 通知 AudioEngine 移除该用户的解码器
    if (engine_ != nullptr) {
        engine_->removeRemoteUser(user_id);
    }

    NEVO_LOG_INFO("audio", "Removed remote user {} (total: {})",
                 user_id.value, remote_users_.size());
}

// ============================================================
// 配置
// ============================================================

void AudioOutput::setDeafened(bool deafened)
{
    deafened_.store(deafened, std::memory_order_release);
    NEVO_LOG_INFO("audio", "AudioOutput deafened={}", deafened);
}

bool AudioOutput::isDeafened() const
{
    return deafened_.load(std::memory_order_acquire);
}

// ============================================================
// 统计信息
// ============================================================

AudioOutput::Stats AudioOutput::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats s = stats_;
    {
        std::lock_guard<std::mutex> users_lock(users_mutex_);
        s.active_users = remote_users_.size();
    }
    return s;
}

void AudioOutput::resetStats()
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Stats{};
}

// ============================================================
// 内部处理
// ============================================================

void AudioOutput::onVoicePacketReceived(
    const uint8_t* data,
    uint32_t size,
    const boost::asio::ip::udp::endpoint& sender)
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // ------------------------------------------------------------------
    // 1. 更新接收统计
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.packets_received++;
        stats_.bytes_received += size;
    }

    // ------------------------------------------------------------------
    // 2. 检查耳聋状态
    // ------------------------------------------------------------------
    if (deafened_.load(std::memory_order_acquire)) {
        // 用户已耳聋，丢弃语音包
        return;
    }

    // ------------------------------------------------------------------
    // 3. 解析语音包头，提取发送者 user_id
    // ------------------------------------------------------------------
    // 语音包格式（已解密后）：
    //   [VoicePacketHeader (Protobuf)][Opus 编码载荷]
    //
    // VoicePacketHeader 包含：
    //   - sender_id: 发送者的 UserId
    //   - sequence:  序列号
    //   - timestamp: 时间戳

    UserId sender_id;
    uint32_t header_size = 0;
    const uint8_t* opus_payload = data;
    uint32_t opus_payload_size = size;

    // 尝试解析 Protobuf 语音包头
    auto voice_header = decodeVoicePacketHeader(data, size, header_size);
    if (voice_header.has_value()) {
        // 从 Protobuf 头中提取 sender_id
        sender_id = UserId(voice_header->sender_id());

        // 获取 Opus 载荷
        auto [payload_ptr, payload_sz] = getVoicePayload(data, header_size, size);
        if (payload_ptr && payload_sz > 0) {
            opus_payload = payload_ptr;
            opus_payload_size = payload_sz;
        }
    } else {
        // 无法解析语音包头，使用默认 sender_id
        // 这种情况可能出现在未使用 Protobuf 头的简化协议中
        NEVO_LOG_TRACE("audio", "Voice packet without header, using default sender_id");
        sender_id = UserId(0);  // unknown sender
    }

    // ------------------------------------------------------------------
    // 4. 检查发送者是否为已知远端用户
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        if (sender_id && remote_users_.count(sender_id) == 0) {
            // 未知用户的语音包，可能是尚未收到 UserJoined 通知
            // 为确保音频播放，自动添加该用户
            NEVO_LOG_DEBUG("audio", "Auto-adding unknown remote user {}", sender_id.value);
            remote_users_.insert(sender_id);
        }
    }

    // ------------------------------------------------------------------
    // 5. 通过 AudioEngine 送入解码管线
    // ------------------------------------------------------------------
    if (engine_ != nullptr && sender_id && opus_payload_size > 0) {
        auto result = engine_->queueAudioData(
            sender_id, opus_payload, opus_payload_size);

        if (result) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.packets_decoded++;
        } else {
            NEVO_LOG_WARN("audio", "Failed to queue audio data for user {}: {}",
                         sender_id.value, result.error().message());
        }
    }
}

} // namespace nevo
