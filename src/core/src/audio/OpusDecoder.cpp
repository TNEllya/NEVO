/**
 * @file OpusDecoder.cpp
 * @brief Opus 解码器 RAII 封装实现
 */

#include "nevo/core/audio/OpusDecoder.h"
#include "nevo/core/common/Logger.h"

#ifdef NEVO_HAS_OPUS
#include <opus/opus.h>
#endif

namespace nevo {

#ifdef NEVO_HAS_OPUS

void OpusDecoderWrapper::OpusDecoderDeleter::operator()(OpusDecoder* decoder) const {
    if (decoder) {
        opus_decoder_destroy(decoder);
    }
}

OpusDecoderWrapper::OpusDecoderWrapper(const Config& config)
    : config_(config)
{
    int error = 0;
    auto* raw = opus_decoder_create(
        static_cast<opus_int32>(config.sample_rate),
        static_cast<int>(config.channels),
        &error
    );

    if (error != OPUS_OK || !raw) {
        NEVO_LOG_ERROR("audio", "Failed to create Opus decoder: {}", opus_strerror(error));
        return;
    }

    decoder_.reset(raw);
    NEVO_LOG_INFO("audio", "Opus decoder created: {}Hz, {}ch",
                  config.sample_rate, config.channels);
}

OpusDecoderWrapper::~OpusDecoderWrapper() = default;

OpusDecoderWrapper::OpusDecoderWrapper(OpusDecoderWrapper&& other) noexcept
    : decoder_(std::move(other.decoder_))
    , config_(other.config_)
{
}

OpusDecoderWrapper& OpusDecoderWrapper::operator=(OpusDecoderWrapper&& other) noexcept {
    if (this != &other) {
        decoder_ = std::move(other.decoder_);
        config_ = other.config_;
    }
    return *this;
}

Result<uint32_t> OpusDecoderWrapper::decode(const uint8_t* opus_data,
                                             uint32_t data_size,
                                             float* pcm_output) {
    if (!decoder_) {
        return Err<uint32_t>(ResultCode::DeviceNotAvailable, "Opus decoder not initialized");
    }

    const auto samples = opus_decode_float(
        decoder_.get(),
        opus_data,
        static_cast<opus_int32>(data_size),
        pcm_output,
        static_cast<int>(config_.frame_size),
        0  // decode FEC: 0=不使用FEC, 1=使用FEC
    );

    if (samples < 0) {
        NEVO_LOG_ERROR("audio", "Opus decode failed: {}", opus_strerror(samples));
        return Err<uint32_t>(ResultCode::Unknown,
                              std::string("Opus decode error: ") + opus_strerror(samples));
    }

    return Ok(static_cast<uint32_t>(samples));
}

Result<uint32_t> OpusDecoderWrapper::decodePacketLoss(float* pcm_output) {
    if (!decoder_) {
        return Err<uint32_t>(ResultCode::DeviceNotAvailable, "Opus decoder not initialized");
    }

    // 传入 nullptr 作为 Opus 数据，Opus 会自动执行 PLC
    const auto samples = opus_decode_float(
        decoder_.get(),
        nullptr,  // NULL 表示丢包，触发 PLC
        0,
        pcm_output,
        static_cast<int>(config_.frame_size),
        0
    );

    if (samples < 0) {
        NEVO_LOG_WARN("audio", "Opus PLC failed: {}", opus_strerror(samples));
        // PLC 失败时填充静音
        std::memset(pcm_output, 0, config_.frame_size * config_.channels * sizeof(float));
        return Ok(config_.frame_size);
    }

    return Ok(static_cast<uint32_t>(samples));
}

#else // !NEVO_HAS_OPUS

// ============================================================
// Stub implementation when Opus is not available
// ============================================================

void OpusDecoderWrapper::OpusDecoderDeleter::operator()(OpusDecoder*) const {
    // No-op stub
}

OpusDecoderWrapper::OpusDecoderWrapper(const Config& config)
    : config_(config)
{
    NEVO_LOG_WARN("audio", "Opus decoder created as stub (Opus library not available)");
}

OpusDecoderWrapper::~OpusDecoderWrapper() = default;

OpusDecoderWrapper::OpusDecoderWrapper(OpusDecoderWrapper&& other) noexcept
    : config_(other.config_)
{
}

OpusDecoderWrapper& OpusDecoderWrapper::operator=(OpusDecoderWrapper&& other) noexcept {
    if (this != &other) {
        config_ = other.config_;
    }
    return *this;
}

Result<uint32_t> OpusDecoderWrapper::decode(const uint8_t* /*opus_data*/,
                                             uint32_t /*data_size*/,
                                             float* /*pcm_output*/) {
    return Err<uint32_t>(ResultCode::DeviceNotAvailable,
                          "Opus decoder not available (built without Opus support)");
}

Result<uint32_t> OpusDecoderWrapper::decodePacketLoss(float* pcm_output) {
    // Fill with silence as a fallback
    if (pcm_output) {
        std::memset(pcm_output, 0, 960 * sizeof(float));
    }
    return Err<uint32_t>(ResultCode::DeviceNotAvailable,
                          "Opus decoder not available (built without Opus support)");
}

#endif // NEVO_HAS_OPUS

} // namespace nevo
