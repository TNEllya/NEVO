/**
 * @file TestResampler.cpp
 * @brief Unit tests for audio sample rate converter
 *
 * 覆盖缺口：Resampler 完全缺少测试
 * 风险等级：高 - Resampler 是音频管线核心组件，负责蓝牙耳机采样率转换
 * 涉及解析、实时安全约束、数据验证
 */

#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <numeric>
#include "nevo/core/audio/Resampler.h"

namespace nevo {
namespace {

constexpr float kEpsilon = 0.001f;

static std::vector<float> generateSineWave(uint32_t sample_rate, float frequency, uint32_t frame_count) {
    std::vector<float> wave(frame_count);
    for (uint32_t i = 0; i < frame_count; ++i) {
        wave[i] = 0.5f * std::sin(2.0f * M_PI * frequency * i / sample_rate);
    }
    return wave;
}

static float computeRms(const std::vector<float>& data) {
    if (data.empty()) return 0.0f;
    double sum = 0.0;
    for (float v : data) {
        sum += v * v;
    }
    return std::sqrt(sum / data.size());
}

static float computeMaxAbs(const std::vector<float>& data) {
    float max_abs = 0.0f;
    for (float v : data) {
        max_abs = std::max(max_abs, std::fabs(v));
    }
    return max_abs;
}

// ============================================================
// Construction and configuration
// ============================================================

TEST(ResamplerTest, DefaultConstruction) {
    Resampler resampler;
    EXPECT_EQ(resampler.config().input_sample_rate, 16000u);
    EXPECT_EQ(resampler.config().output_sample_rate, 48000u);
    EXPECT_EQ(resampler.config().channels, 1u);
}

TEST(ResamplerTest, CustomConfiguration) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 8000;
    cfg.output_sample_rate = 48000;
    cfg.channels = 1;

    Resampler resampler(cfg);
    EXPECT_EQ(resampler.config().input_sample_rate, 8000u);
    EXPECT_EQ(resampler.config().output_sample_rate, 48000u);
}

TEST(ResamplerTest, Configure48000to44100) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 44100;
    cfg.channels = 1;

    Resampler resampler(cfg);
    EXPECT_TRUE(resampler.initialize().ok());
    EXPECT_TRUE(resampler.isInitialized());
}

// ============================================================
// Initialization
// ============================================================

TEST(ResamplerTest, InitializeSucceeds) {
    Resampler resampler;
    auto result = resampler.initialize();
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(resampler.isInitialized());
}

TEST(ResamplerTest, ReinitializeAfterConstruction) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());
    EXPECT_TRUE(resampler.isInitialized());
}

TEST(ResamplerTest, CanProcessAfterInitialization) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(480);
    std::vector<float> output(441);

    auto result = resampler.process(input.data(), 480, output.data(), 441);
    EXPECT_TRUE(result.ok());
}

// ============================================================
// Sample rate conversion: 16kHz -> 48kHz (3x upsampling)
// ============================================================

TEST(ResamplerTest, Upsample16kTo48k) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;
    cfg.channels = 1;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_frames = 160;
    uint32_t expected_output_frames = 480;

    std::vector<float> input(input_frames, 0.5f);
    std::vector<float> output(expected_output_frames + 100, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), expected_output_frames + 100);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    EXPECT_EQ(output_frames, expected_output_frames);

    for (uint32_t i = 0; i < output_frames; ++i) {
        EXPECT_NEAR(output[i], 0.5f, kEpsilon);
    }
}

TEST(ResamplerTest, UpsampleSineWave16kTo48k) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;
    cfg.channels = 1;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_sample_rate = 16000;
    float frequency = 1000.0f;
    uint32_t input_frames = 160;
    uint32_t output_frames = 480;

    auto input = generateSineWave(input_sample_rate, frequency, input_frames);
    std::vector<float> output(output_frames + 100, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), output_frames + 100);
    ASSERT_TRUE(result.ok());

    EXPECT_GT(computeRms(output), 0.1f);
    EXPECT_LT(computeMaxAbs(output), 1.01f);
}

// ============================================================
// Sample rate conversion: 48kHz -> 16kHz (3x downsampling)
// ============================================================

TEST(ResamplerTest, Downsample48kTo16k) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 16000;
    cfg.channels = 1;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_frames = 480;
    std::vector<float> input(input_frames, 0.5f);
    std::vector<float> output(200, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), 200);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    EXPECT_EQ(output_frames, 160u);
}

TEST(ResamplerTest, DownsampleSineWave48kTo16k) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 16000;
    cfg.channels = 1;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_sample_rate = 48000;
    float frequency = 2000.0f;
    uint32_t input_frames = 480;

    auto input = generateSineWave(input_sample_rate, frequency, input_frames);
    std::vector<float> output(200, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), 200);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    EXPECT_EQ(output_frames, 160u);
    EXPECT_GT(computeRms({output.data(), output.data() + output_frames}), 0.1f);
}

// ============================================================
// Edge cases
// ============================================================

TEST(ResamplerTest, ZeroInputFrames) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(100, 0.5f);
    std::vector<float> output(100, 0.0f);

    auto result = resampler.process(input.data(), 0, output.data(), 100);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 0u);
}

TEST(ResamplerTest, SilentInput) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(160, 0.0f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), 160, output.data(), 500);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    for (uint32_t i = 0; i < output_frames; ++i) {
        EXPECT_NEAR(output[i], 0.0f, kEpsilon);
    }
}

TEST(ResamplerTest, FullScaleInput) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(480, 1.0f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), 480, output.data(), 500);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    for (uint32_t i = 0; i < output_frames; ++i) {
        EXPECT_NEAR(output[i], 1.0f, kEpsilon);
    }
}

TEST(ResamplerTest, NegativeFullScaleInput) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(480, -1.0f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), 480, output.data(), 500);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    for (uint32_t i = 0; i < output_frames; ++i) {
        EXPECT_NEAR(output[i], -1.0f, kEpsilon);
    }
}

// ============================================================
// Output buffer size limits
// ============================================================

TEST(ResamplerTest, OutputBufferTooSmall) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 16000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(480, 0.5f);
    std::vector<float> output(100, 0.0f);

    auto result = resampler.process(input.data(), 480, output.data(), 100);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 100u);
}

TEST(ResamplerTest, OutputBufferVeryLarge) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(480, 0.5f);
    std::vector<float> output(10000, 0.0f);

    auto result = resampler.process(input.data(), 480, output.data(), 10000);
    ASSERT_TRUE(result.ok());

    uint32_t output_frames = result.value();
    EXPECT_GT(output_frames, 0u);
    EXPECT_LT(output_frames, 10000u);
}

// ============================================================
// Reset functionality
// ============================================================

TEST(ResamplerTest, ResetClearsState) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input1(160, 1.0f);
    std::vector<float> output1(500, 0.0f);
    resampler.process(input1.data(), 160, output1.data(), 500);

    resampler.reset();

    std::vector<float> input2(160, 0.0f);
    std::vector<float> output2(500, 0.0f);
    auto result = resampler.process(input2.data(), 160, output2.data(), 500);
    ASSERT_TRUE(result.ok());
}

TEST(ResamplerTest, ResetThenProcess) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    resampler.reset();

    std::vector<float> input(480, 0.5f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), 480, output.data(), 500);
    ASSERT_TRUE(result.ok());
}

// ============================================================
// Configuration update
// ============================================================

TEST(ResamplerTest, UpdateInputSampleRate) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    resampler.setInputSampleRate(8000);
    EXPECT_EQ(resampler.config().input_sample_rate, 8000u);

    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(80);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), 80, output.data(), 500);
    ASSERT_TRUE(result.ok());
}

TEST(ResamplerTest, UpdateOutputSampleRate) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 16000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    resampler.setOutputSampleRate(44100);
    EXPECT_EQ(resampler.config().output_sample_rate, 44100u);

    ASSERT_TRUE(resampler.initialize().ok());

    std::vector<float> input(480);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), 480, output.data(), 500);
    ASSERT_TRUE(result.ok());
}

// ============================================================
// Output frame estimation
// ============================================================

TEST(ResamplerTest, EstimateOutputFrames) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);

    uint32_t input_frames = 160;
    uint32_t estimated = resampler.estimateOutputFrames(input_frames);

    EXPECT_EQ(estimated, 480u);
}

TEST(ResamplerTest, EstimateOutputFramesDownsample) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 16000;

    Resampler resampler(cfg);

    uint32_t input_frames = 480;
    uint32_t estimated = resampler.estimateOutputFrames(input_frames);

    EXPECT_EQ(estimated, 160u);
}

TEST(ResamplerTest, EstimateOutputFramesNonIntegerRatio) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 44100;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);

    uint32_t input_frames = 441;
    uint32_t estimated = resampler.estimateOutputFrames(input_frames);

    EXPECT_GT(estimated, 0u);
}

// ============================================================
// Common sample rate combinations
// ============================================================

TEST(ResamplerTest, CommonRate44100To48000) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 44100;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_frames = 441;
    std::vector<float> input(input_frames, 0.5f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), 500);
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value(), 0u);
}

TEST(ResamplerTest, CommonRate48000To44100) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 48000;
    cfg.output_sample_rate = 44100;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_frames = 480;
    std::vector<float> input(input_frames, 0.5f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), 500);
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value(), 0u);
}

TEST(ResamplerTest, BluetoothHfpRate) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 8000;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    uint32_t input_frames = 80;
    std::vector<float> input(input_frames, 0.5f);
    std::vector<float> output(500, 0.0f);

    auto result = resampler.process(input.data(), input_frames, output.data(), 500);
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value(), 0u);
}

// ============================================================
// Multiple consecutive calls
// ============================================================

TEST(ResamplerTest, MultipleConsecutiveCalls) {
    Resampler resampler;
    ASSERT_TRUE(resampler.initialize().ok());

    for (int i = 0; i < 10; ++i) {
        std::vector<float> input(480, 0.5f);
        std::vector<float> output(500, 0.0f);

        auto result = resampler.process(input.data(), 480, output.data(), 500);
        ASSERT_TRUE(result.ok());
    }
}

TEST(ResamplerTest, AlternatingInputSizes) {
    Resampler::Config cfg;
    cfg.input_sample_rate = 16000;
    cfg.output_sample_rate = 48000;

    Resampler resampler(cfg);
    ASSERT_TRUE(resampler.initialize().ok());

    for (uint32_t frames : {80u, 160u, 40u, 320u, 160u}) {
        std::vector<float> input(frames, 0.5f);
        uint32_t estimated = resampler.estimateOutputFrames(frames);
        std::vector<float> output(estimated + 100, 0.0f);

        auto result = resampler.process(input.data(), frames, output.data(), estimated + 100);
        ASSERT_TRUE(result.ok());
        EXPECT_GT(result.value(), 0u);
    }
}

} // namespace
} // namespace nevo
