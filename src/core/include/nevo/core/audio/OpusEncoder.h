#pragma once
/**
 * @file OpusEncoder.h
 * @brief Opus 编码器 RAII 封装
 *
 * 将 PCM float32 数据编码为 Opus 压缩格式。
 * 使用 unique_ptr + 自定义 deleter 管理 Opus 编码器句柄。
 */

#include <cstdint>
#include <memory>
#include <vector>
#include "nevo/core/common/Result.h"

// Opus 前向声明
struct OpusEncoder;

namespace nevo {

class AudioMemoryPool;

class OpusEncoderWrapper {
public:
    /// 编码器配置
    struct Config {
        uint32_t sample_rate = 48000;
        uint32_t channels = 1;
        uint32_t frame_size = 960;
        int32_t bitrate = 64000;
        int32_t complexity = 10;
        bool vad_enabled = true;
        bool dtx_enabled = true;
    };

    OpusEncoderWrapper() : OpusEncoderWrapper(Config{}) {}
    explicit OpusEncoderWrapper(const Config& config);
    ~OpusEncoderWrapper();

    // 禁止拷贝
    OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
    OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

    // 允许移动
    OpusEncoderWrapper(OpusEncoderWrapper&&) noexcept;
    OpusEncoderWrapper& operator=(OpusEncoderWrapper&&) noexcept;

    /// 编码一帧 PCM 数据
    /// @param pcm_input  PCM float32 输入缓冲区（frame_size * channels 个采样）
    /// @param output     输出缓冲区（由调用者提供）
    /// @param max_output_size 输出缓冲区最大大小
    /// @return 编码后的字节数，0 表示 DTX（静音帧），错误返回 Result
    Result<uint32_t> encode(const float* pcm_input,
                             uint8_t* output,
                             uint32_t max_output_size);

    /// 检查上一帧是否包含语音（Opus 内置 VAD）
    bool lastFrameHadVoice() const { return last_vad_result_; }

    /// 动态调整比特率
    void setBitrate(int32_t bitrate);

    /// 设置 in-band FEC 启用状态
    void setFecEnabled(bool enabled);

    /// 设置期望丢包率百分比（0~100），影响 FEC 冗余度
    void setPacketLossPerc(int32_t percentage);

    /// 获取当前配置
    const Config& config() const { return config_; }

private:
    /// 自定义 deleter 用于 unique_ptr
    struct OpusEncoderDeleter {
        void operator()(OpusEncoder* encoder) const;
    };

    std::unique_ptr<OpusEncoder, OpusEncoderDeleter> encoder_;
    Config config_;
    bool last_vad_result_ = false;
};

} // namespace nevo
