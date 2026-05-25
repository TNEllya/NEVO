#pragma once
/**
 * @file AudioOutput.h
 * @brief 音频输出模块 - 接收 → 解密 → 解码 → 混音 → 播放
 *
 * AudioOutput 是 NEVO VoIP 客户端的音频输出管线桥接器，连接
 * NetworkManager 的语音包接收和 AudioEngine 的音频播放接口。
 *
 * 工作流程：
 *
 *   [NetworkManager.onVoicePacket] → VoiceCrypto.decrypt → AudioEngine.queueAudioData
 *                                                                     |
 *   AudioOutput 注册回调 ────────────────────────────────────────────┘
 *       |
 *       ├── 解密语音包（XChaCha20-Poly1305 AEAD）
 *       ├── 按用户 ID 分发到对应的 Opus 解码器
 *       └── 通过 AudioEngine.queueAudioData() 送入解码/混音/播放管线
 *
 * 音频输出管线（AudioEngine 内部）：
 *
 *   queueAudioData() → Opus 解码 → JitterBuffer → AudioMixer → output_fifo → miniaudio
 *
 * 线程安全：
 *   - start()/stop() 应在主线程调用
 *   - 语音包回调在 NetworkManager 的 io_context 线程中触发
 */

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio/ip/udp.hpp>

#include "nevo/core/common/Result.h"
#include "nevo/core/common/Types.h"

namespace nevo {

class AudioEngine;
class NetworkManager;
class VoiceCrypto;

// ============================================================
// AudioOutput 类
// ============================================================

/**
 * @class AudioOutput
 * @brief 音频输出管线桥接器
 *
 * 将 NetworkManager 接收到的加密语音包桥接到 AudioEngine 的解码播放管线。
 * 自动处理语音包解密、按用户分发和解码器管理。
 *
 * 典型用法：
 * @code
 *   AudioOutput output;
 *   output.start(audio_engine, network_manager);
 *   // 收到的语音包会自动解密并送入 AudioEngine 播放
 *   output.stop();
 * @endcode
 */
class AudioOutput {
public:
    /// 构造函数
    AudioOutput();

    /// 析构函数：确保停止并注销回调
    ~AudioOutput();

    // 禁止拷贝和移动
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&) = delete;
    AudioOutput& operator=(AudioOutput&&) = delete;

    // ============================================================
    // 生命周期管理
    // ============================================================

    /**
     * @brief 启动音频输出
     *
     * 向 NetworkManager 注册语音包回调，当收到加密语音数据时
     * 自动解密并送入 AudioEngine 播放。
     *
     * @param engine  音频引擎引用
     * @param network 网络管理器引用
     * @return Result<void> 启动结果
     */
    Result<void> start(AudioEngine& engine, NetworkManager& network);

    /**
     * @brief 停止音频输出
     *
     * 注销 NetworkManager 的语音包回调，停止接收和解码语音数据。
     * 同时清理所有远端用户的解码器。
     */
    void stop();

    /// 查询是否正在运行
    /// @return true 表示音频输出已启动
    bool isRunning() const;

    // ============================================================
    // 用户管理
    // ============================================================

    /**
     * @brief 添加远端用户
     *
     * 当新用户加入频道时调用，为其准备解码通道。
     * AudioEngine 内部会创建对应的 Opus 解码器。
     *
     * @param user_id 远端用户 ID
     */
    void addRemoteUser(UserId user_id);

    /**
     * @brief 移除远端用户
     *
     * 当用户离开频道时调用，释放其解码器资源。
     *
     * @param user_id 远端用户 ID
     */
    void removeRemoteUser(UserId user_id);

    // ============================================================
    // 配置
    // ============================================================

    /// 设置耳聋状态（由 ClientCore 控制）
    /// 当用户主动设置耳聋时，丢弃所有收到的语音数据
    /// @param deafened true 表示耳聋
    void setDeafened(bool deafened);

    /// 查询当前耳聋状态
    /// @return true 表示已耳聋
    bool isDeafened() const;

    // ============================================================
    // 统计信息
    // ============================================================

    /// 音频输出统计
    struct Stats {
        uint64_t packets_received = 0;   ///< 收到的语音包总数
        uint64_t packets_decrypted = 0;  ///< 成功解密的包数
        uint64_t packets_decoded = 0;    ///< 成功解码的包数
        uint64_t decrypt_failures = 0;   ///< 解密失败的包数
        uint64_t bytes_received = 0;     ///< 收到的字节数
        uint64_t active_users = 0;       ///< 当前活跃远端用户数
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
     * @brief 语音包接收处理函数
     *
     * 当 NetworkManager 收到并解密语音包后调用。
     * 处理流程：
     *   1. 检查是否耳聋 → 耳聋则丢弃
     *   2. 解密语音包（VoiceCrypto.decrypt）
     *   3. 解析语音包头（VoicePacketHeader），提取 user_id
     *   4. 通过 AudioEngine.queueAudioData() 送入解码管线
     *
     * @param data     解密后的语音数据指针
     * @param size     数据大小
     * @param sender   发送方端点
     */
    void onVoicePacketReceived(const uint8_t* data,
                               uint32_t size,
                               UserId sender_id);

    // ============================================================
    // 数据成员
    // ============================================================

    /// 音频引擎指针（不拥有所有权）
    AudioEngine* engine_ = nullptr;

    /// 网络管理器指针（不拥有所有权）
    NetworkManager* network_ = nullptr;

    /// 是否正在运行
    std::atomic<bool> running_{false};

    /// 是否耳聋（丢弃所有收到的语音）
    std::atomic<bool> deafened_{false};

    /// 当前频道中的远端用户集合
    std::unordered_set<UserId> remote_users_;
    mutable std::mutex users_mutex_;    ///< 保护 remote_users_

    /// 统计信息
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

} // namespace nevo
