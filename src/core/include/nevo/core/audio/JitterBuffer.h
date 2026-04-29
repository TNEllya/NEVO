#pragma once
/**
 * @file JitterBuffer.h
 * @brief UDP 语音包抖动缓冲区
 *
 * 对接收到的 UDP 语音包进行重排序和缓冲，消除网络抖动对音频播放的影响。
 * 使用序列号对乱序到达的帧进行排序，保证音频输出线程按序读取。
 *
 * 核心机制：
 *   1. 初始缓冲：收到第一个包后，等待 max_delay_frames 帧的缓冲量再开始播放，
 *      为后续乱序包预留到达时间。
 *   2. 乱序重排：通过序列号将乱序帧插入正确位置。
 *   3. 丢包补偿（PLC）：当检测到序列号缺口且等待超时时，生成 PLC 帧填充，
 *      并标记为 lost，上层可据此调整解码器状态。
 *   4. 过期丢弃：序列号过旧的帧直接丢弃，避免播放延迟不断累积。
 *
 * 线程安全：不需要。单消费者模式，由音频输出线程独占调用。
 *
 * 典型使用流程：
 *   - 网络接收线程：insert(seq, pcm, frame_count)
 *   - 音频输出线程：getNext() -> 播放
 */

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "nevo/core/common/Types.h"

namespace nevo {

// ============================================================
// 抖动缓冲区
// ============================================================
class JitterBuffer {
public:
    /// 缓冲区配置
    struct Config {
        /// 目标播放延迟（帧数）。
        /// 收到第一个包后，等待缓冲区积累这么多帧才开始播放。
        /// 值越大抗抖动能力越强，但延迟越高。
        /// 5 帧 x 20ms = 100ms，是 VoIP 的合理默认值。
        uint32_t max_delay_frames = 5;

        /// 缓冲区最大容量（帧数）。
        /// 当缓冲区中的帧数超过此限制时，丢弃最旧的帧。
        /// 32 帧 x 20ms = 640ms，足以应对极端网络抖动。
        uint32_t max_buffer_size = 32;
    };

    /// 缓冲帧数据。包含 PCM 采样数据和丢包标记。
    struct Frame {
        /// PCM float32 采样数据
        std::vector<float> pcm_data;

        /// 是否为 PLC 补偿帧（丢包填充）
        /// true 表示此帧由丢包补偿生成，非实际接收数据
        /// 上层可据此决定是否更新解码器状态
        bool lost = false;
    };

        JitterBuffer() : JitterBuffer(Config{}) {}
    explicit JitterBuffer(const Config& config);
    ~JitterBuffer() = default;

    // 禁止拷贝（内部含 std::map，拷贝开销大且无必要）
    JitterBuffer(const JitterBuffer&) = delete;
    JitterBuffer& operator=(const JitterBuffer&) = delete;

    // 允许移动
    JitterBuffer(JitterBuffer&&) noexcept = default;
    JitterBuffer& operator=(JitterBuffer&&) noexcept = default;

    // ============================================================
    // 核心接口
    // ============================================================

    /// 插入一帧解码后的 PCM 数据。
    ///
    /// 将帧按序列号存入缓冲区。如果序列号已经过旧（低于当前播放位置），
    /// 则直接丢弃。如果序列号已存在，也丢弃（重复包）。
    ///
    /// @param seq        序列号（由调用方从 RTP 头中提取，
    ///                   调用方需处理 16 位回绕，展开为单调递增的 uint32_t）
    /// @param pcm_data   PCM float32 采样数据指针
    /// @param frame_count 该帧的采样数（通常为 960，即 20ms@48kHz）
    void insert(uint32_t seq, const float* pcm_data, uint32_t frame_count);

    /// 存储远端用户的 Opus 编码数据。
    ///
    /// 网络层收到远端音频包后调用，将原始 Opus 数据按用户缓存。
    /// 数据在后续 pop() 调用时按 FIFO 顺序取出。
    ///
    /// @param user_id   远端用户 ID
    /// @param data      Opus 编码数据指针
    /// @param data_size 数据大小（字节）
    /// @param timestamp 时间戳（RTP 时间戳，用于排序，当前实现中暂未使用）
    void push(UserId user_id, const uint8_t* data, uint32_t data_size, uint32_t timestamp);

    /// 取出远端用户的 Opus 编码数据。
    ///
    /// 从该用户的缓冲队列前端取出一个 Opus 数据包。
    /// 返回的指针指向内部存储，在下次 pop() 或 removeUser() 前有效。
    ///
    /// @param user_id   远端用户 ID
    /// @param data      [out] 输出数据指针（指向内部存储）
    /// @param data_size [out] 输出数据大小（字节）
    /// @return true 表示成功取出数据，false 表示该用户无数据
    bool pop(UserId user_id, uint8_t*& data, uint32_t& data_size);

    /// 移除一个用户的所有数据。
    /// 清除该用户的全部缓冲数据和当前持有数据。
    /// @param user_id 用户唯一标识
    void removeUser(UserId user_id);

    /// 获取下一帧用于播放。
    ///
    /// 按序列号顺序返回帧。如果期望的序列号缺失：
    ///   - 若缓冲区中已有更新的帧（说明缺失帧已无法等到），生成 PLC 补偿帧。
    ///   - 若缓冲区为空，返回 nullopt（欠载，播放静音）。
    ///   - 若缓冲区未完成初始缓冲，返回 nullopt。
    ///
    /// @return 可选的 Frame。nullopt 表示当前无可播放帧；
    ///         Frame.lost=true 表示该帧为丢包补偿帧。
    std::optional<Frame> getNext();

    /// 重置缓冲区状态。
    /// 清空所有缓冲帧，重置播放位置。用于用户停止说话或频道切换时。
    void reset();

    // ============================================================
    // 状态查询
    // ============================================================

    /// 当前缓冲区中的帧数
    uint32_t bufferDepth() const;

    /// 是否已完成初始缓冲，开始正常播放
    bool isPlaying() const;

    /// 获取当前期望播放的序列号
    uint32_t nextPlaySequence() const;

    /// 获取当前配置
    const Config& config() const { return config_; }

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /// 生成 PLC 补偿帧（静音填充）。
    /// 在检测到丢包时调用，生成一个全零的 PCM 帧。
    /// @param frame_count 帧的采样数
    /// @return PLC 补偿帧
    static Frame generatePlcFrame(uint32_t frame_count);

    /// 清理缓冲区中过旧的帧（序列号远低于播放位置的帧）
    void cleanupOldFrames();

    // ============================================================
    // 多用户原始数据包内部类型
    // ============================================================

    /// 原始 Opus 数据包（未解码）
    struct RawPacket {
        std::vector<uint8_t> data;  ///< Opus 编码数据
        uint32_t timestamp = 0;     ///< RTP 时间戳
    };

    // ============================================================
    // 成员变量
    // ============================================================

    Config config_;

    /// 有序帧缓冲区：序列号 -> PCM 数据
    /// 使用 std::map 保证按序列号排序，便于快速查找和顺序遍历
    std::map<uint32_t, std::vector<float>> buffer_;

    /// 下一个期望播放的序列号
    /// 仅在 initialized_ 为 true 后有效
    uint32_t next_play_seq_ = 0;

    /// 是否已收到第一个包（初始化播放序列号）
    bool initialized_ = false;

    /// 是否已开始播放（初始缓冲完成）
    /// 当缓冲区深度达到 max_delay_frames 时设为 true
    bool playing_ = false;

    /// 每帧采样数（从第一次 insert 中学习）
    uint32_t frame_size_ = 0;

    // --- 多用户原始数据包存储 ---

    /// 每用户的 Opus 数据包队列（FIFO）
    std::unordered_map<UserId, std::deque<RawPacket>> user_packets_;

    /// 每用户最近一次 pop 取出的数据包（保持数据指针有效）
    std::unordered_map<UserId, RawPacket> current_packets_;

    /// 互斥锁，保护所有共享数据结构
    mutable std::mutex mutex_;
};

} // namespace nevo
