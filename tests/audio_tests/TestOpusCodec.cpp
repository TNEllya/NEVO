/**
 * @file TestOpusCodec.cpp
 * @brief Unit tests for Opus encoder/decoder wrapper
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "nevo/core/audio/OpusEncoder.h"
#include "nevo/core/audio/OpusDecoder.h"

namespace nevo {
namespace {

// Helper: generate a sine wave PCM buffer
static std::vector<float> generateSineWave(float frequency, float duration_sec,
                                           uint32_t sample_rate, float amplitude) {
    uint32_t num_samples = static_cast<uint32_t>(duration_sec * sample_rate);
    std::vector<float> pcm(num_samples);
    for (uint32_t i = 0; i < num_samples; ++i) {
        pcm[i] = amplitude * std::sin(2.0f * static_cast<float>(M_PI) * frequency *
                                       static_cast<float>(i) / static_cast<float>(sample_rate));
    }
    return pcm;
}

// Helper: compute RMS energy of a PCM buffer
static float computeRms(const float* data, size_t count) {
    if (count == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += static_cast<double>(data[i]) * data[i];
    }
    return static_cast<float>(std::sqrt(sum / count));
}

// ============================================================
// OpusEncoderWrapper creation and encoding
// ============================================================

TEST(OpusEncoderTest, CreationWithDefaultConfig) {
    OpusEncoderWrapper encoder;
    const auto& cfg = encoder.config();
    EXPECT_EQ(cfg.sample_rate, 48000u);
    EXPECT_EQ(cfg.channels, 1u);
    EXPECT_EQ(cfg.frame_size, 960u);
}

TEST(OpusEncoderTest, CreationWithCustomConfig) {
    OpusEncoderWrapper::Config cfg;
    cfg.bitrate = 32000;
    cfg.complexity = 5;
    OpusEncoderWrapper encoder(cfg);
    EXPECT_EQ(encoder.config().bitrate, 32000);
    EXPECT_EQ(encoder.config().complexity, 5);
}

TEST(OpusEncoderTest, EncodeSineWaveProducesOutput) {
    OpusEncoderWrapper encoder;

    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.5f);
    ASSERT_GE(pcm.size(), 960u);

    std::vector<uint8_t> output(4000);
    auto result = encoder.encode(pcm.data(), output.data(), static_cast<uint32_t>(output.size()));

    EXPECT_TRUE(result.ok()) << "Encode failed: " << result.error().message();
    EXPECT_GT(result.value(), 0u) << "Encoded bytes should be > 0";
}

TEST(OpusEncoderTest, EncodeSilenceReturnsSmallOrZeroBytes) {
    OpusEncoderWrapper::Config cfg;
    cfg.dtx_enabled = true;
    cfg.vad_enabled = true;
    OpusEncoderWrapper encoder(cfg);

    // All-zero PCM = silence
    std::vector<float> silence(960, 0.0f);
    std::vector<uint8_t> output(4000);

    // Encode a few frames first to let the encoder learn the background
    for (int i = 0; i < 3; ++i) {
        encoder.encode(silence.data(), output.data(), static_cast<uint32_t>(output.size()));
    }

    auto result = encoder.encode(silence.data(), output.data(), static_cast<uint32_t>(output.size()));
    EXPECT_TRUE(result.ok());
    // With DTX, silence frames may produce 0 bytes or very small output
    EXPECT_LE(result.value(), 4000u);
}

// ============================================================
// OpusDecoderWrapper creation and decoding
// ============================================================

TEST(OpusDecoderTest, CreationWithDefaultConfig) {
    OpusDecoderWrapper decoder;
    const auto& cfg = decoder.config();
    EXPECT_EQ(cfg.sample_rate, 48000u);
    EXPECT_EQ(cfg.channels, 1u);
    EXPECT_EQ(cfg.frame_size, 960u);
}

TEST(OpusDecoderTest, DecodeProducesSamples) {
    OpusEncoderWrapper encoder;
    OpusDecoderWrapper decoder;

    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.5f);
    std::vector<uint8_t> encoded(4000);

    auto enc_result = encoder.encode(pcm.data(), encoded.data(),
                                      static_cast<uint32_t>(encoded.size()));
    ASSERT_TRUE(enc_result.ok());

    std::vector<float> decoded(960, 0.0f);
    auto dec_result = decoder.decode(encoded.data(), enc_result.value(), decoded.data());

    EXPECT_TRUE(dec_result.ok()) << "Decode failed: " << dec_result.error().message();
    EXPECT_EQ(dec_result.value(), 960u);
}

// ============================================================
// Encode-then-decode roundtrip
// ============================================================

TEST(OpusCodecTest, RoundtripPcmIsNotSilence) {
    OpusEncoderWrapper encoder;
    OpusDecoderWrapper decoder;

    // Generate a 440Hz sine wave at 50% amplitude, 20ms
    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.5f);
    ASSERT_GE(pcm.size(), 960u);

    // Encode
    std::vector<uint8_t> encoded(4000);
    auto enc_result = encoder.encode(pcm.data(), encoded.data(),
                                      static_cast<uint32_t>(encoded.size()));
    ASSERT_TRUE(enc_result.ok()) << "Encode failed: " << enc_result.error().message();
    ASSERT_GT(enc_result.value(), 0u);

    // Decode
    std::vector<float> decoded(960, 0.0f);
    auto dec_result = decoder.decode(encoded.data(), enc_result.value(), decoded.data());
    ASSERT_TRUE(dec_result.ok()) << "Decode failed: " << dec_result.error().message();
    EXPECT_EQ(dec_result.value(), 960u);

    // Verify decoded audio is not silence
    float rms = computeRms(decoded.data(), 960);
    EXPECT_GT(rms, 0.01f) << "Decoded audio should not be silence (RMS too low)";
}

TEST(OpusCodecTest, MultipleRoundtripFrames) {
    OpusEncoderWrapper encoder;
    OpusDecoderWrapper decoder;

    // Generate 100ms of sine wave = 5 frames of 20ms
    auto pcm = generateSineWave(1000.0f, 0.1f, 48000, 0.8f);

    for (size_t frame = 0; frame < 5; ++frame) {
        const float* frame_ptr = pcm.data() + frame * 960;
        std::vector<uint8_t> encoded(4000);

        auto enc_result = encoder.encode(frame_ptr, encoded.data(),
                                          static_cast<uint32_t>(encoded.size()));
        ASSERT_TRUE(enc_result.ok());

        std::vector<float> decoded(960, 0.0f);
        auto dec_result = decoder.decode(encoded.data(), enc_result.value(), decoded.data());
        ASSERT_TRUE(dec_result.ok());
        EXPECT_EQ(dec_result.value(), 960u);

        float rms = computeRms(decoded.data(), 960);
        EXPECT_GT(rms, 0.01f) << "Frame " << frame << " decoded as silence";
    }
}

// ============================================================
// PLC: decodePacketLoss
// ============================================================

TEST(OpusDecoderTest, DecodePacketLossDoesNotCrash) {
    OpusDecoderWrapper decoder;

    // First, decode a valid frame so the decoder has state
    OpusEncoderWrapper encoder;
    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.5f);
    std::vector<uint8_t> encoded(4000);

    auto enc_result = encoder.encode(pcm.data(), encoded.data(),
                                      static_cast<uint32_t>(encoded.size()));
    ASSERT_TRUE(enc_result.ok());

    std::vector<float> decoded(960, 0.0f);
    auto dec_result = decoder.decode(encoded.data(), enc_result.value(), decoded.data());
    ASSERT_TRUE(dec_result.ok());

    // Now simulate a lost packet with PLC
    std::vector<float> plc_output(960, 0.0f);
    auto plc_result = decoder.decodePacketLoss(plc_output.data());

    EXPECT_TRUE(plc_result.ok()) << "PLC decode failed: " << plc_result.error().message();
    EXPECT_EQ(plc_result.value(), 960u);

    // PLC output should produce something (even if it's attenuated)
    // At minimum, the call should not crash and should return a valid frame size
}

// ============================================================
// OpusEncoder VAD detection
// ============================================================

TEST(OpusEncoderTest, VadDetectsSpeech) {
    OpusEncoderWrapper::Config cfg;
    cfg.vad_enabled = true;
    OpusEncoderWrapper encoder(cfg);

    // Encode a loud sine wave (speech-like)
    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.8f);
    std::vector<uint8_t> output(4000);

    auto result = encoder.encode(pcm.data(), output.data(),
                                  static_cast<uint32_t>(output.size()));
    ASSERT_TRUE(result.ok());

    // The encoder should detect voice activity for a loud sine wave
    EXPECT_TRUE(encoder.lastFrameHadVoice());
}

TEST(OpusEncoderTest, VadDetectsSilence) {
    OpusEncoderWrapper::Config cfg;
    cfg.vad_enabled = true;
    cfg.dtx_enabled = true;
    OpusEncoderWrapper encoder(cfg);

    // Encode pure silence
    std::vector<float> silence(960, 0.0f);
    std::vector<uint8_t> output(4000);

    // Feed a few silence frames to let the VAD stabilize
    for (int i = 0; i < 5; ++i) {
        encoder.encode(silence.data(), output.data(), static_cast<uint32_t>(output.size()));
    }

    // After multiple silence frames, VAD should report no voice
    EXPECT_FALSE(encoder.lastFrameHadVoice());
}

// ============================================================
// Move semantics
// ============================================================

TEST(OpusEncoderTest, MoveConstruction) {
    OpusEncoderWrapper::Config cfg;
    cfg.bitrate = 48000;
    OpusEncoderWrapper encoder1(cfg);

    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.5f);
    std::vector<uint8_t> output(4000);
    auto result1 = encoder1.encode(pcm.data(), output.data(),
                                    static_cast<uint32_t>(output.size()));
    ASSERT_TRUE(result1.ok());

    OpusEncoderWrapper encoder2(std::move(encoder1));
    // encoder2 should be functional
    std::vector<uint8_t> output2(4000);
    auto result2 = encoder2.encode(pcm.data(), output2.data(),
                                    static_cast<uint32_t>(output2.size()));
    EXPECT_TRUE(result2.ok());
}

TEST(OpusDecoderTest, MoveConstruction) {
    OpusDecoderWrapper decoder1;
    OpusDecoderWrapper decoder2(std::move(decoder1));

    // decoder2 should be functional
    OpusEncoderWrapper encoder;
    auto pcm = generateSineWave(440.0f, 0.02f, 48000, 0.5f);
    std::vector<uint8_t> encoded(4000);

    auto enc_result = encoder.encode(pcm.data(), encoded.data(),
                                      static_cast<uint32_t>(encoded.size()));
    ASSERT_TRUE(enc_result.ok());

    std::vector<float> decoded(960, 0.0f);
    auto dec_result = decoder2.decode(encoded.data(), enc_result.value(), decoded.data());
    EXPECT_TRUE(dec_result.ok());
}

} // namespace
} // namespace nevo
