/**
 * @file OpusEncoder.cpp
 * @brief Opus 编码器 RAII 封装实现
 */

#include "nevo/core/audio/OpusEncoder.h"
#include "nevo/core/common/Logger.h"

#ifdef NEVO_HAS_OPUS
#include <opus/opus.h>
#endif

namespace nevo {

#ifdef NEVO_HAS_OPUS

// ============================================================
// OpusEncoderDeleter
// ============================================================
void OpusEncoderWrapper::OpusEncoderDeleter::operator()(OpusEncoder* encoder) const {
    if (encoder) {
        opus_encoder_destroy(encoder);
    }
}

// ============================================================
// 构造 / 析构
// ============================================================
OpusEncoderWrapper::OpusEncoderWrapper(const Config& config)
    : config_(config)
{
    int error = 0;
    auto* raw = opus_encoder_create(
        static_cast<opus_int32>(config.sample_rate),
        static_cast<int>(config.channels),
        OPUS_APPLICATION_VOIP,  // VoIP 应用模式，优化语音质量
        &error
    );

    if (error != OPUS_OK || !raw) {
        NEVO_LOG_ERROR("audio", "Failed to create Opus encoder: {}", opus_strerror(error));
        return;
    }

    encoder_.reset(raw);

    // 配置编码器参数
    opus_encoder_ctl(encoder_.get(), OPUS_SET_BITRATE(config.bitrate));
    opus_encoder_ctl(encoder_.get(), OPUS_SET_COMPLEXITY(config.complexity));
    opus_encoder_ctl(encoder_.get(), OPUS_SET_VBR(1));  // 可变比特率
    opus_encoder_ctl(encoder_.get(), OPUS_SET_VBR_CONSTRAINT(1));  // 受约束 VBR

    if (config.vad_enabled) {
        opus_encoder_ctl(encoder_.get(), OPUS_SET_VBR(1));
    }
    if (config.dtx_enabled) {
        opus_encoder_ctl(encoder_.get(), OPUS_SET_DTX(1));
    }

    // 设置期望丢包率（5%），让编码器增加冗余
    opus_encoder_ctl(encoder_.get(), OPUS_SET_PACKET_LOSS_PERC(5));

    NEVO_LOG_INFO("audio", "Opus encoder created: {}Hz, {}ch, {}bps",
                  config.sample_rate, config.channels, config.bitrate);
}

OpusEncoderWrapper::~OpusEncoderWrapper() = default;

OpusEncoderWrapper::OpusEncoderWrapper(OpusEncoderWrapper&& other) noexcept
    : encoder_(std::move(other.encoder_))
    , config_(other.config_)
    , last_vad_result_(other.last_vad_result_)
{
}

OpusEncoderWrapper& OpusEncoderWrapper::operator=(OpusEncoderWrapper&& other) noexcept {
    if (this != &other) {
        encoder_ = std::move(other.encoder_);
        config_ = other.config_;
        last_vad_result_ = other.last_vad_result_;
    }
    return *this;
}

// ============================================================
// 编码
// ============================================================
Result<uint32_t> OpusEncoderWrapper::encode(const float* pcm_input,
                                             uint8_t* output,
                                             uint32_t max_output_size) {
    if (!encoder_) {
        return Err<uint32_t>(ResultCode::DeviceNotAvailable, "Opus encoder not initialized");
    }

    const auto encoded_bytes = opus_encode_float(
        encoder_.get(),
        pcm_input,
        static_cast<int>(config_.frame_size),
        output,
        static_cast<opus_int32>(max_output_size)
    );

    if (encoded_bytes < 0) {
        NEVO_LOG_ERROR("audio", "Opus encode failed: {}", opus_strerror(encoded_bytes));
        return Err<uint32_t>(ResultCode::Unknown,
                              std::string("Opus encode error: ") + opus_strerror(encoded_bytes));
    }

    // 检查 VAD 结果 (OPUS_GET_VOICE_ACTIVITY not available in all versions)
    opus_int32 vad_flag = 0;
#ifdef OPUS_GET_VOICE_ACTIVITY_REQUEST
    opus_encoder_ctl(encoder_.get(), OPUS_GET_VOICE_ACTIVITY(&vad_flag));
#else
    // Fallback: use DTX state as VAD indicator
    opus_encoder_ctl(encoder_.get(), OPUS_GET_IN_DTX(&vad_flag));
    vad_flag = !vad_flag;  // Invert: in_dtx=1 means silence, VAD=0
#endif
    last_vad_result_ = (vad_flag != 0);

    return Ok(static_cast<uint32_t>(encoded_bytes));
}

void OpusEncoderWrapper::setBitrate(int32_t bitrate) {
    if (encoder_) {
        opus_encoder_ctl(encoder_.get(), OPUS_SET_BITRATE(bitrate));
        config_.bitrate = bitrate;
    }
}

void OpusEncoderWrapper::setFecEnabled(bool enabled) {
    if (encoder_) {
        opus_encoder_ctl(encoder_.get(), OPUS_SET_INBAND_FEC(enabled ? 1 : 0));
        NEVO_LOG_DEBUG("audio", "Opus in-band FEC {}", enabled ? "enabled" : "disabled");
    }
}

void OpusEncoderWrapper::setPacketLossPerc(int32_t percentage) {
    if (encoder_) {
        percentage = std::max(0, std::min(100, percentage));
        opus_encoder_ctl(encoder_.get(), OPUS_SET_PACKET_LOSS_PERC(percentage));
        NEVO_LOG_DEBUG("audio", "Opus packet loss percentage set to {}%", percentage);
    }
}

#else // !NEVO_HAS_OPUS

// ============================================================
// Stub implementation when Opus is not available
// ============================================================

void OpusEncoderWrapper::OpusEncoderDeleter::operator()(OpusEncoder*) const {
    // No-op stub
}

OpusEncoderWrapper::OpusEncoderWrapper(const Config& config)
    : config_(config)
{
    NEVO_LOG_WARN("audio", "Opus encoder created as stub (Opus library not available)");
}

OpusEncoderWrapper::~OpusEncoderWrapper() = default;

OpusEncoderWrapper::OpusEncoderWrapper(OpusEncoderWrapper&& other) noexcept
    : config_(other.config_)
    , last_vad_result_(other.last_vad_result_)
{
}

OpusEncoderWrapper& OpusEncoderWrapper::operator=(OpusEncoderWrapper&& other) noexcept {
    if (this != &other) {
        config_ = other.config_;
        last_vad_result_ = other.last_vad_result_;
    }
    return *this;
}

Result<uint32_t> OpusEncoderWrapper::encode(const float* /*pcm_input*/,
                                             uint8_t* /*output*/,
                                             uint32_t /*max_output_size*/) {
    return Err<uint32_t>(ResultCode::DeviceNotAvailable,
                          "Opus encoder not available (built without Opus support)");
}

void OpusEncoderWrapper::setBitrate(int32_t /*bitrate*/) {
    // No-op stub
}

void OpusEncoderWrapper::setFecEnabled(bool /*enabled*/) {
    // No-op stub
}

void OpusEncoderWrapper::setPacketLossPerc(int32_t /*percentage*/) {
    // No-op stub
}

#endif // NEVO_HAS_OPUS

} // namespace nevo
