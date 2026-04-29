#pragma once
/**
 * @file OpusDecoder.h
 * @brief Opus 解码器 RAII 封装
 *
 * 将 Opus 压缩数据解码为 PCM float32。
 * 支持 PLC（Packet Loss Concealment）丢包补偿。
 */

#include <cstdint>
#include <memory>
#include <vector>
#include "nevo/core/common/Result.h"

// Opus 前向声明
struct OpusDecoder;

namespace nevo {

class OpusDecoderWrapper {
public:
    /// 解码器配置
    struct Config {
        uint32_t sample_rate = 48000;
        uint32_t channels = 1;
        uint32_t frame_size = 960;
    };

    OpusDecoderWrapper() : OpusDecoderWrapper(Config{}) {}
    explicit OpusDecoderWrapper(const Config& config);
    ~OpusDecoderWrapper();

    // 禁止拷贝
    OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
    OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

    // 允许移动
    OpusDecoderWrapper(OpusDecoderWrapper&&) noexcept;
    OpusDecoderWrapper& operator=(OpusDecoderWrapper&&) noexcept;

    /// 解码一帧 Opus 数据
    /// @param opus_data Opus 编码数据指针
    /// @param data_size 数据大小（字节）
    /// @param pcm_output PCM float32 输出缓冲区（frame_size * channels 个采样）
    /// @return 解码的采样数，错误返回 Result
    Result<uint32_t> decode(const uint8_t* opus_data,
                             uint32_t data_size,
                             float* pcm_output);

    /// PLC 丢包补偿：当丢包时调用，生成补偿帧
    /// @param pcm_output PCM float32 输出缓冲区
    /// @return 生成的采样数
    Result<uint32_t> decodePacketLoss(float* pcm_output);

    /// 获取当前配置
    const Config& config() const { return config_; }

private:
    struct OpusDecoderDeleter {
        void operator()(OpusDecoder* decoder) const;
    };

    std::unique_ptr<OpusDecoder, OpusDecoderDeleter> decoder_;
    Config config_;
};

} // namespace nevo
