/**
 * @file TestVoiceActivity.cpp
 * @brief Unit tests for voice activity detection and PTT
 *
 * 覆盖缺口：VoiceActivity 完全缺少测试
 * 风险等级：高 - VAD/PTT 是语音通话核心功能，影响用户体验和带宽使用
 * 涉及数据验证和并发控制
 */

#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include "nevo/core/audio/VoiceActivity.h"

namespace nevo {
namespace {

constexpr uint32_t kFrameSize = 960;

static std::vector<float> generateSilence(uint32_t frame_count) {
    return std::vector<float>(frame_count, 0.0f);
}

static std::vector<float> generateTone(uint32_t frame_count, float amplitude) {
    std::vector<float> tone(frame_count);
    for (uint32_t i = 0; i < frame_count; ++i) {
        tone[i] = amplitude * std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    }
    return tone;
}

static std::vector<float> generateNoise(float amplitude, uint32_t frame_count) {
    std::vector<float> noise(frame_count);
    for (uint32_t i = 0; i < frame_count; ++i) {
        noise[i] = amplitude * (2.0f * (static_cast<float>(rand()) / RAND_MAX) - 1.0f);
    }
    return noise;
}

static std::vector<float> generateVoiceLike(uint32_t frame_count, float amplitude) {
    std::vector<float> voice(frame_count);
    for (uint32_t i = 0; i < frame_count; ++i) {
        float envelope = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / frame_count));
        voice[i] = amplitude * envelope * std::sin(2.0f * M_PI * 300.0f * i / 48000.0f);
    }
    return voice;
}

// ============================================================
// Default configuration
// ============================================================

TEST(VoiceActivityTest, DefaultConfiguration) {
    VoiceActivity vad;
    EXPECT_TRUE(vad.isVadEnabled());
    EXPECT_FALSE(vad.isPttEnabled());
    EXPECT_FALSE(vad.isPttActive());
    EXPECT_FALSE(vad.isSpeaking());
}

TEST(VoiceActivityTest, DefaultEnergyThreshold) {
    VoiceActivity vad;
    EXPECT_FLOAT_EQ(vad.energyThreshold(), 0.01f);
}

// ============================================================
// PTT mode
// ============================================================

TEST(VoiceActivityTest, PttActivation) {
    VoiceActivity vad;
    ASSERT_FALSE(vad.isPttActive());

    vad.setPttActive(true);
    EXPECT_TRUE(vad.isPttActive());
    EXPECT_TRUE(vad.shouldTransmit(generateSilence(kFrameSize).data(), kFrameSize, false));
}

TEST(VoiceActivityTest, PttDeactivation) {
    VoiceActivity vad;
    vad.setPttActive(true);
    ASSERT_TRUE(vad.isPttActive());

    vad.setPttActive(false);
    EXPECT_FALSE(vad.isPttActive());
}

TEST(VoiceActivityTest, PttOverridesVad) {
    VoiceActivity vad;

    vad.setPttActive(true);

    auto silence = generateSilence(kFrameSize);
    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, false));
}

TEST(VoiceActivityTest, PttToggleMultipleTimes) {
    VoiceActivity vad;

    for (int i = 0; i < 10; ++i) {
        vad.setPttActive(true);
        EXPECT_TRUE(vad.isPttActive());

        vad.setPttActive(false);
        EXPECT_FALSE(vad.isPttActive());
    }
}

// ============================================================
// VAD mode - voice detection
// ============================================================

TEST(VoiceActivityTest, VadEnabledByDefault) {
    VoiceActivity vad;
    EXPECT_TRUE(vad.isVadEnabled());
}

TEST(VoiceActivityTest, VadDisable) {
    VoiceActivity vad;
    ASSERT_TRUE(vad.isVadEnabled());

    vad.setVadEnabled(false);
    EXPECT_FALSE(vad.isVadEnabled());
}

TEST(VoiceActivityTest, VadReenable) {
    VoiceActivity vad;
    vad.setVadEnabled(false);
    ASSERT_FALSE(vad.isVadEnabled());

    vad.setVadEnabled(true);
    EXPECT_TRUE(vad.isVadEnabled());
}

TEST(VoiceActivityTest, VadDetectsVoice) {
    VoiceActivity vad;

    auto voice = generateVoiceLike(kFrameSize, 0.5f);
    bool should_transmit = vad.shouldTransmit(voice.data(), kFrameSize, false);

    EXPECT_TRUE(should_transmit);
}

TEST(VoiceActivityTest, VadIgnoresSilence) {
    VoiceActivity vad;

    auto silence = generateSilence(kFrameSize);
    bool should_transmit = vad.shouldTransmit(silence.data(), kFrameSize, false);

    EXPECT_FALSE(should_transmit);
}

TEST(VoiceActivityTest, VadConsidersOpusResult) {
    VoiceActivity vad;

    auto silence = generateSilence(kFrameSize);

    EXPECT_FALSE(vad.shouldTransmit(silence.data(), kFrameSize, false));
    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, true));
}

// ============================================================
// Energy threshold
// ============================================================

TEST(VoiceActivityTest, EnergyThresholdDefault) {
    VoiceActivity vad;
    EXPECT_FLOAT_EQ(vad.energyThreshold(), 0.01f);
}

TEST(VoiceActivityTest, SetEnergyThreshold) {
    VoiceActivity vad;

    vad.setEnergyThreshold(0.05f);
    EXPECT_FLOAT_EQ(vad.energyThreshold(), 0.05f);
}

TEST(VoiceActivityTest, HighThresholdBlocksQuietSounds) {
    VoiceActivity::Config cfg;
    cfg.energy_threshold = 0.5f;
    cfg.vad_enabled = true;

    VoiceActivity vad(cfg);

    auto quiet_voice = generateVoiceLike(kFrameSize, 0.1f);
    bool should_transmit = vad.shouldTransmit(quiet_voice.data(), kFrameSize, false);

    EXPECT_FALSE(should_transmit);
}

TEST(VoiceActivityTest, LowThresholdAcceptsQuietSounds) {
    VoiceActivity::Config cfg;
    cfg.energy_threshold = 0.001f;
    cfg.vad_enabled = true;

    VoiceActivity vad(cfg);

    auto quiet_voice = generateVoiceLike(kFrameSize, 0.1f);
    bool should_transmit = vad.shouldTransmit(quiet_voice.data(), kFrameSize, false);

    EXPECT_TRUE(should_transmit);
}

// ============================================================
// Hangover frames
// ============================================================

TEST(VoiceActivityTest, HangoverKeepsSpeakingAfterVoice) {
    VoiceActivity::Config cfg;
    cfg.vad_enabled = true;
    cfg.hangover_frames = 5;

    VoiceActivity vad(cfg);

    auto voice = generateVoiceLike(kFrameSize, 0.5f);
    vad.shouldTransmit(voice.data(), kFrameSize, true);

    auto silence = generateSilence(kFrameSize);
    for (int i = 0; i < 4; ++i) {
        bool should_transmit = vad.shouldTransmit(silence.data(), kFrameSize, false);
        EXPECT_TRUE(should_transmit);
    }
}

TEST(VoiceActivityTest, HangoverExpires) {
    VoiceActivity::Config cfg;
    cfg.vad_enabled = true;
    cfg.hangover_frames = 3;

    VoiceActivity vad(cfg);

    auto voice = generateVoiceLike(kFrameSize, 0.5f);
    vad.shouldTransmit(voice.data(), kFrameSize, true);

    auto silence = generateSilence(kFrameSize);

    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, false));
    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, false));
    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, false));
    EXPECT_FALSE(vad.shouldTransmit(silence.data(), kFrameSize, false));
}

TEST(VoiceActivityTest, ZeroHangover) {
    VoiceActivity::Config cfg;
    cfg.vad_enabled = true;
    cfg.hangover_frames = 0;

    VoiceActivity vad(cfg);

    auto voice = generateVoiceLike(kFrameSize, 0.5f);
    vad.shouldTransmit(voice.data(), kFrameSize, true);

    auto silence = generateSilence(kFrameSize);
    EXPECT_FALSE(vad.shouldTransmit(silence.data(), kFrameSize, false));
}

// ============================================================
// Speaking state
// ============================================================

TEST(VoiceActivityTest, SpeakingStateAfterVoice) {
    VoiceActivity vad;

    auto voice = generateVoiceLike(kFrameSize, 0.5f);
    vad.shouldTransmit(voice.data(), kFrameSize, true);

    EXPECT_TRUE(vad.isSpeaking());
}

TEST(VoiceActivityTest, SpeakingStateAfterSilence) {
    VoiceActivity::Config cfg;
    cfg.hangover_frames = 0;

    VoiceActivity vad(cfg);

    auto silence = generateSilence(kFrameSize);
    EXPECT_FALSE(vad.shouldTransmit(silence.data(), kFrameSize, false));

    EXPECT_FALSE(vad.isSpeaking());
}

TEST(VoiceActivityTest, PttAffectsSpeakingState) {
    VoiceActivity vad;

    EXPECT_FALSE(vad.isSpeaking());

    vad.setPttActive(true);
    EXPECT_TRUE(vad.isSpeaking());

    vad.setPttActive(false);
    EXPECT_FALSE(vad.isSpeaking());
}

// ============================================================
// Edge cases
// ============================================================

TEST(VoiceActivityTest, EmptyFrame) {
    VoiceActivity vad;

    float single_sample = 0.5f;
    bool should_transmit = vad.shouldTransmit(&single_sample, 1, false);

    EXPECT_TRUE(should_transmit);
}

TEST(VoiceActivityTest, VeryLargeFrame) {
    VoiceActivity vad;

    auto voice = generateVoiceLike(9600, 0.5f);
    bool should_transmit = vad.shouldTransmit(voice.data(), 9600, false);

    EXPECT_TRUE(should_transmit);
}

TEST(VoiceActivityTest, MaxVolumeVoice) {
    VoiceActivity vad;

    auto voice = generateVoiceLike(kFrameSize, 1.0f);
    bool should_transmit = vad.shouldTransmit(voice.data(), kFrameSize, false);

    EXPECT_TRUE(should_transmit);
}

TEST(VoiceActivityTest, VeryQuietVoice) {
    VoiceActivity::Config cfg;
    cfg.energy_threshold = 0.0001f;

    VoiceActivity vad(cfg);

    auto quiet = generateVoiceLike(kFrameSize, 0.001f);
    bool should_transmit = vad.shouldTransmit(quiet.data(), kFrameSize, false);

    EXPECT_TRUE(should_transmit);
}

// ============================================================
// Complex scenarios
// ============================================================

TEST(VoiceActivityTest, AlternatingVoiceAndSilence) {
    VoiceActivity::Config cfg;
    cfg.hangover_frames = 1;

    VoiceActivity vad(cfg);

    for (int i = 0; i < 10; ++i) {
        if (i % 2 == 0) {
            auto voice = generateVoiceLike(kFrameSize, 0.5f);
            EXPECT_TRUE(vad.shouldTransmit(voice.data(), kFrameSize, true));
        } else {
            auto silence = generateSilence(kFrameSize);
            bool should_transmit = vad.shouldTransmit(silence.data(), kFrameSize, false);
            if (vad.hangover_counter_ > 0) {
                EXPECT_TRUE(should_transmit);
            }
        }
    }
}

TEST(VoiceActivityTest, MultipleUsersScenario) {
    VoiceActivity vad;

    for (int burst = 0; burst < 5; ++burst) {
        auto voice = generateVoiceLike(kFrameSize, 0.5f);
        bool speaking_before = vad.isSpeaking();

        for (int frame = 0; frame < 10; ++frame) {
            vad.shouldTransmit(voice.data(), kFrameSize, true);
        }

        EXPECT_TRUE(vad.isSpeaking());
    }
}

TEST(VoiceActivityTest, RapidPttToggle) {
    VoiceActivity vad;

    for (int i = 0; i < 100; ++i) {
        vad.setPttActive(i % 2 == 0);
        EXPECT_EQ(vad.isPttActive(), i % 2 == 0);
    }
}

TEST(VoiceActivityTest, MixedVadAndPtt) {
    VoiceActivity vad;

    vad.setPttEnabled(true);

    auto silence = generateSilence(kFrameSize);
    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, false));

    vad.setPttActive(false);
    EXPECT_FALSE(vad.shouldTransmit(silence.data(), kFrameSize, false));

    vad.setPttActive(true);
    EXPECT_TRUE(vad.shouldTransmit(silence.data(), kFrameSize, false));
}

// ============================================================
// Configuration through constructor
// ============================================================

TEST(VoiceActivityTest, ConstructorWithConfig) {
    VoiceActivity::Config cfg;
    cfg.vad_enabled = false;
    cfg.ptt_enabled = true;
    cfg.energy_threshold = 0.02f;
    cfg.hangover_frames = 15;

    VoiceActivity vad(cfg);

    EXPECT_FALSE(vad.isVadEnabled());
    EXPECT_TRUE(vad.isPttEnabled());
    EXPECT_FLOAT_EQ(vad.energyThreshold(), 0.02f);
}

TEST(VoiceActivityTest, PttEnabledControl) {
    VoiceActivity vad;

    EXPECT_FALSE(vad.isPttEnabled());

    vad.setPttEnabled(true);
    EXPECT_TRUE(vad.isPttEnabled());

    vad.setPttEnabled(false);
    EXPECT_FALSE(vad.isPttEnabled());
}

// ============================================================
// Noise robustness
// ============================================================

TEST(VoiceActivityTest, WhiteNoiseBelowThreshold) {
    VoiceActivity::Config cfg;
    cfg.energy_threshold = 0.1f;

    VoiceActivity vad(cfg);

    auto noise = generateNoise(0.05f, kFrameSize);
    bool should_transmit = vad.shouldTransmit(noise.data(), kFrameSize, false);

    EXPECT_FALSE(should_transmit);
}

TEST(VoiceActivityTest, WhiteNoiseAboveThreshold) {
    VoiceActivity::Config cfg;
    cfg.energy_threshold = 0.01f;

    VoiceActivity vad(cfg);

    auto noise = generateNoise(0.1f, kFrameSize);
    bool should_transmit = vad.shouldTransmit(noise.data(), kFrameSize, false);

    EXPECT_TRUE(should_transmit);
}

// ============================================================
// Reset behavior
// ============================================================

TEST(VoiceActivityTest, SpeakingStateResets) {
    VoiceActivity vad;

    auto voice = generateVoiceLike(kFrameSize, 0.5f);
    vad.shouldTransmit(voice.data(), kFrameSize, true);
    EXPECT_TRUE(vad.isSpeaking());

    auto silence = generateSilence(kFrameSize);
    for (int i = 0; i < 100; ++i) {
        vad.shouldTransmit(silence.data(), kFrameSize, false);
    }

    EXPECT_FALSE(vad.isSpeaking());
}

} // namespace
} // namespace nevo
