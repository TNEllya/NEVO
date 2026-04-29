/**
 * @file JitterBuffer.cpp
 * @brief UDP 语音包抖动缓冲区实现
 */

#include "nevo/core/audio/JitterBuffer.h"
#include "nevo/core/common/Logger.h"

#include <algorithm>
#include <cstring>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

JitterBuffer::JitterBuffer(const Config& config)
    : config_(config)
{
    NEVO_LOG_INFO("audio",
                  "JitterBuffer created: max_delay={}, max_buffer_size={}",
                  config_.max_delay_frames, config_.max_buffer_size);
}

// ============================================================
// insert - 插入一帧解码后的 PCM 数据
// ============================================================
void JitterBuffer::insert(uint32_t seq, const float* pcm_data, uint32_t frame_count)
{
    if (!pcm_data) {
        NEVO_LOG_WARN("audio", "JitterBuffer::insert: null pcm_data for seq={}", seq);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // ---- 第一步：初始化 ----
    // 收到第一个包时，设定播放起始序列号。
    // 起始位置设为当前序列号，而不是更早，因为我们无法回溯已丢失的包。
    if (!initialized_) {
        next_play_seq_ = seq;
        initialized_ = true;
        frame_size_ = frame_count;
        NEVO_LOG_INFO("audio",
                      "JitterBuffer initialized: start_seq={}, frame_size={}",
                      seq, frame_count);
    }

    // ---- 第二步：丢弃过旧的帧 ----
    // 序列号低于当前播放位置的帧已经没有播放价值，直接丢弃。
    // 这防止了因网络重传或乱序导致的延迟累积。
    if (seq < next_play_seq_) {
        NEVO_LOG_DEBUG("audio",
                       "JitterBuffer: discarding old frame seq={} (play_seq={})",
                       seq, next_play_seq_);
        return;
    }

    // ---- 第三步：丢弃重复帧 ----
    // 同一序列号的帧可能因网络重传而重复到达，仅保留第一个。
    if (buffer_.contains(seq)) {
        NEVO_LOG_DEBUG("audio", "JitterBuffer: duplicate frame seq={}, ignoring", seq);
        return;
    }

    // ---- 第四步：存入缓冲区 ----
    // 复制 PCM 数据到缓冲区。std::map 自动按序列号排序。
    buffer_.emplace(seq, std::vector<float>(pcm_data, pcm_data + frame_count));

    NEVO_LOG_TRACE("audio",
                   "JitterBuffer: inserted seq={}, buffer_depth={}",
                   seq, buffer_.size());

    // ---- 第五步：检查初始缓冲是否完成 ----
    // 当缓冲区积累足够帧后，开始播放。
    // 这为乱序包预留了到达时间窗口。
    if (!playing_ && buffer_.size() >= config_.max_delay_frames) {
        playing_ = true;
        NEVO_LOG_INFO("audio",
                      "JitterBuffer: playout started, buffer_depth={}",
                      buffer_.size());
    }

    // ---- 第六步：缓冲区溢出保护 ----
    // 当缓冲区超过最大容量时，丢弃最旧的帧。
    // 这是极端网络情况的保护措施，防止内存无限增长。
    while (buffer_.size() > config_.max_buffer_size) {
        auto oldest = buffer_.begin();
        NEVO_LOG_WARN("audio",
                      "JitterBuffer: buffer overflow, dropping oldest seq={}",
                      oldest->first);
        buffer_.erase(oldest);
    }

    // ---- 第七步：清理过旧帧 ----
    // 移除所有序列号远低于播放位置的帧，防止 map 无限增长。
    cleanupOldFrames();
}

// ============================================================
// getNext - 获取下一帧用于播放
// ============================================================
std::optional<JitterBuffer::Frame> JitterBuffer::getNext()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // ---- 未初始化或未开始播放：返回空 ----
    // 还没收到任何包，或者还在初始缓冲阶段。
    if (!initialized_ || !playing_) {
        return std::nullopt;
    }

    // ---- 缓冲区为空：欠载 ----
    // 所有帧都已播放完毕，当前没有可用数据。
    if (buffer_.empty()) {
        // 注意：不在这里生成 PLC，因为可能只是短暂的间隙
        // （说话者暂停），等下一帧数据到达即可恢复。
        return std::nullopt;
    }

    // ---- 查找期望序列号的帧 ----
    auto it = buffer_.find(next_play_seq_);

    if (it != buffer_.end()) {
        // ---- 正常情况：找到了期望的帧 ----
        // 从缓冲区取出 PCM 数据，构造返回帧。
        Frame frame;
        frame.pcm_data = std::move(it->second);
        frame.lost = false;
        buffer_.erase(it);
        ++next_play_seq_;

        return frame;
    }

    // ---- 异常情况：期望的帧缺失（丢包或乱序） ----
    // 检查缓冲区中是否有更新的帧。如果有，说明缺失帧已经等不到了，
    // 需要生成 PLC 补偿帧。如果没有更新的帧，可能帧还在路上，
    // 但因为我们已经过了初始缓冲阶段，不应再无限等待。

    const uint32_t min_seq_in_buffer = buffer_.begin()->first;

    if (min_seq_in_buffer > next_play_seq_) {
        // 缓冲区中有更新的帧 -> 确认丢包
        // 生成一个 PLC 补偿帧（静音填充），并标记 lost=true
        NEVO_LOG_DEBUG("audio",
                       "JitterBuffer: gap detected at seq={}, next_available={}",
                       next_play_seq_, min_seq_in_buffer);

        Frame plc_frame = generatePlcFrame(frame_size_);
        ++next_play_seq_;

        return plc_frame;
    }

    // 理论上不应到达此处（min_seq <= next_play_seq 且 find 没找到
    // 意味着 min_seq < next_play_seq，但 cleanupOldFrames 应已处理）
    // 作为安全措施，清理并返回空
    cleanupOldFrames();
    return std::nullopt;
}

// ============================================================
// push - 存储远端用户的 Opus 编码数据
// ============================================================
void JitterBuffer::push(UserId user_id, const uint8_t* data, uint32_t data_size, uint32_t timestamp)
{
    if (!data || data_size == 0) {
        NEVO_LOG_WARN("audio", "JitterBuffer::push: null/empty data for user={}",
                      user_id.value);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& queue = user_packets_[user_id];
    queue.push_back(RawPacket{
        std::vector<uint8_t>(data, data + data_size),
        timestamp
    });

    NEVO_LOG_TRACE("audio",
                   "JitterBuffer: pushed {} bytes for user={}, queue_size={}",
                   data_size, user_id.value, queue.size());
}

// ============================================================
// pop - 取出远端用户的 Opus 编码数据
// ============================================================
bool JitterBuffer::pop(UserId user_id, uint8_t*& data, uint32_t& data_size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_packets_.find(user_id);
    if (it == user_packets_.end() || it->second.empty()) {
        data = nullptr;
        data_size = 0;
        return false;
    }

    // 将队列前端的数据包移到 current_packets_ 中持有，
    // 以保证返回的指针在下次 pop() 或 removeUser() 前持续有效。
    auto& current = current_packets_[user_id];
    current = std::move(it->second.front());
    it->second.pop_front();

    // 若该用户队列已空，移除空队列条目以节省内存
    if (it->second.empty()) {
        user_packets_.erase(it);
    }

    data = current.data.data();
    data_size = static_cast<uint32_t>(current.data.size());

    NEVO_LOG_TRACE("audio",
                   "JitterBuffer: popped {} bytes for user={}",
                   data_size, user_id.value);

    return true;
}

// ============================================================
// removeUser - 移除一个用户的所有数据
// ============================================================
void JitterBuffer::removeUser(UserId user_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    user_packets_.erase(user_id);
    current_packets_.erase(user_id);

    NEVO_LOG_DEBUG("audio", "JitterBuffer: removed user={}", user_id.value);
}

// ============================================================
// reset - 重置缓冲区状态
// ============================================================
void JitterBuffer::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    next_play_seq_ = 0;
    initialized_ = false;
    playing_ = false;
    frame_size_ = 0;

    // 同时清空多用户原始数据包
    user_packets_.clear();
    current_packets_.clear();

    NEVO_LOG_DEBUG("audio", "JitterBuffer: reset");
}

// ============================================================
// 状态查询
// ============================================================

uint32_t JitterBuffer::bufferDepth() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(buffer_.size());
}

bool JitterBuffer::isPlaying() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return playing_;
}

uint32_t JitterBuffer::nextPlaySequence() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return next_play_seq_;
}

// ============================================================
// generatePlcFrame - 生成 PLC 补偿帧
// ============================================================
JitterBuffer::Frame JitterBuffer::generatePlcFrame(uint32_t frame_count)
{
    Frame frame;
    frame.pcm_data.resize(frame_count, 0.0f);  // 全零静音填充
    frame.lost = true;
    return frame;
}

// ============================================================
// cleanupOldFrames - 清理过旧帧
// ============================================================
void JitterBuffer::cleanupOldFrames()
{
    if (!initialized_ || buffer_.empty()) {
        return;
    }

    // 移除所有序列号低于当前播放位置的帧
    // 这些帧已经没有播放价值（可能因网络延迟导致晚到）
    auto it = buffer_.begin();
    while (it != buffer_.end() && it->first < next_play_seq_) {
        it = buffer_.erase(it);
    }
}

} // namespace nevo
