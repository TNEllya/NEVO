/**
 * @file TestAudioMixer.cpp
 * @brief Unit tests for multi-user PCM audio mixer
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "nevo/core/audio/AudioMixer.h"

namespace nevo {
namespace {

// Helper: generate a PCM buffer with a constant value
static std::vector<float> makeConstantPcm(uint32_t frame_size, float value) {
    return std::vector<float>(frame_size, value);
}

// Helper: compute peak absolute value in a buffer
static float peakAbs(const float* data, uint32_t count) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        float a = std::fabs(data[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

// Helper: compute RMS energy
static float computeRms(const float* data, uint32_t count) {
    if (count == 0) return 0.0f;
    double sum = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        sum += static_cast<double>(data[i]) * data[i];
    }
    return static_cast<float>(std::sqrt(sum / count));
}

// ============================================================
// Single stream mixing
// ============================================================

TEST(AudioMixerTest, SingleStreamPassesThrough) {
    AudioMixer mixer;
    const uint32_t frame_size = 960;

    mixer.reset();

    auto pcm = makeConstantPcm(frame_size, 0.5f);
    mixer.addStream(UserId(1), pcm.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // Single stream at volume 1.0 should pass through (within hard limit)
    float rms = computeRms(output.data(), frame_size);
    EXPECT_GT(rms, 0.1f);
    EXPECT_LT(peakAbs(output.data(), frame_size), 1.01f);
}

TEST(AudioMixerTest, SingleStreamPreservesShape) {
    AudioMixer mixer;
    const uint32_t frame_size = 960;

    // Generate a sine wave
    std::vector<float> sine(frame_size);
    for (uint32_t i = 0; i < frame_size; ++i) {
        sine[i] = 0.3f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f *
                                    static_cast<float>(i) / 48000.0f);
    }

    mixer.reset();
    mixer.addStream(UserId(1), sine.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // Output should closely match input (single stream, no limiting needed)
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], sine[i], 0.001f);
    }
}

// ============================================================
// Multiple stream mixing
// ============================================================

TEST(AudioMixerTest, MultipleStreamsAreSummed) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    auto pcm1 = makeConstantPcm(frame_size, 0.2f);
    auto pcm2 = makeConstantPcm(frame_size, 0.3f);

    mixer.reset();
    mixer.addStream(UserId(1), pcm1.data(), frame_size);
    mixer.addStream(UserId(2), pcm2.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // With hard limiting, 0.2 + 0.3 = 0.5, which is under 1.0
    // So no limiting should occur
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], 0.5f, 0.001f);
    }
}

TEST(AudioMixerTest, ActiveStreamCount) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    auto pcm = makeConstantPcm(frame_size, 0.1f);

    mixer.reset();
    EXPECT_EQ(mixer.activeStreamCount(), 0u);

    mixer.addStream(UserId(1), pcm.data(), frame_size);
    EXPECT_EQ(mixer.activeStreamCount(), 1u);

    mixer.addStream(UserId(2), pcm.data(), frame_size);
    EXPECT_EQ(mixer.activeStreamCount(), 2u);

    mixer.reset();
    EXPECT_EQ(mixer.activeStreamCount(), 0u);
}

// ============================================================
// Hard limiting
// ============================================================

TEST(AudioMixerTest, HardLimitingClampsToUnity) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    // Add 4 streams at 0.5 each => sum = 2.0 => should be limited to 1.0
    auto pcm = makeConstantPcm(frame_size, 0.5f);

    mixer.reset();
    mixer.addStream(UserId(1), pcm.data(), frame_size);
    mixer.addStream(UserId(2), pcm.data(), frame_size);
    mixer.addStream(UserId(3), pcm.data(), frame_size);
    mixer.addStream(UserId(4), pcm.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // All samples should be within [-1.0, 1.0]
    float peak = peakAbs(output.data(), frame_size);
    EXPECT_LE(peak, 1.0f);

    // The output should be near 1.0 (limited from 2.0 by factor 0.5)
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], 1.0f, 0.01f);
    }
}

TEST(AudioMixerTest, HardLimitingNegativeValues) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    // Add streams at -0.5 each => sum = -2.0 => should be limited to -1.0
    auto pcm = makeConstantPcm(frame_size, -0.5f);

    mixer.reset();
    mixer.addStream(UserId(1), pcm.data(), frame_size);
    mixer.addStream(UserId(2), pcm.data(), frame_size);
    mixer.addStream(UserId(3), pcm.data(), frame_size);
    mixer.addStream(UserId(4), pcm.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    float peak = peakAbs(output.data(), frame_size);
    EXPECT_LE(peak, 1.0f);

    // All samples should be near -1.0
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], -1.0f, 0.01f);
    }
}

// ============================================================
// Per-user volume control
// ============================================================

TEST(AudioMixerTest, SetUserVolumeAffectsMix) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    auto pcm1 = makeConstantPcm(frame_size, 0.5f);
    auto pcm2 = makeConstantPcm(frame_size, 0.5f);

    mixer.setUserVolume(UserId(1), 0.5f); // Half volume for user 1
    mixer.setUserVolume(UserId(2), 1.0f);

    mixer.reset();
    mixer.addStream(UserId(1), pcm1.data(), frame_size);
    mixer.addStream(UserId(2), pcm2.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // 0.5 * 0.5 + 0.5 * 1.0 = 0.75
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], 0.75f, 0.001f);
    }
}

TEST(AudioMixerTest, VolumeIsClamped) {
    AudioMixer mixer;

    // Volume should be clamped to [0.0, 2.0]
    mixer.setUserVolume(UserId(1), -1.0f);
    EXPECT_FLOAT_EQ(mixer.getUserVolume(UserId(1)), 0.0f);

    mixer.setUserVolume(UserId(2), 5.0f);
    EXPECT_FLOAT_EQ(mixer.getUserVolume(UserId(2)), 2.0f);

    mixer.setUserVolume(UserId(3), 1.0f);
    EXPECT_FLOAT_EQ(mixer.getUserVolume(UserId(3)), 1.0f);
}

TEST(AudioMixerTest, UnregisteredUserVolumeIsOne) {
    AudioMixer mixer;
    EXPECT_FLOAT_EQ(mixer.getUserVolume(UserId(999)), 1.0f);
}

TEST(AudioMixerTest, MutedUserProducesZeroContribution) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    auto pcm = makeConstantPcm(frame_size, 0.5f);

    mixer.setUserVolume(UserId(1), 0.0f); // Mute user 1

    mixer.reset();
    mixer.addStream(UserId(1), pcm.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // Muted user should produce silence
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], 0.0f, 0.001f);
    }
}

// ============================================================
// removeUser
// ============================================================

TEST(AudioMixerTest, RemoveUserClearsState) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;

    auto pcm = makeConstantPcm(frame_size, 0.5f);

    mixer.setUserVolume(UserId(1), 0.8f);
    mixer.reset();
    mixer.addStream(UserId(1), pcm.data(), frame_size);

    EXPECT_EQ(mixer.activeStreamCount(), 1u);
    EXPECT_EQ(mixer.registeredUserCount(), 1u);

    mixer.removeUser(UserId(1));

    EXPECT_EQ(mixer.activeStreamCount(), 0u);
    EXPECT_EQ(mixer.registeredUserCount(), 0u);

    // After removing, the user's volume is also gone
    EXPECT_FLOAT_EQ(mixer.getUserVolume(UserId(1)), 1.0f); // Default for unregistered
}

TEST(AudioMixerTest, RemoveNonexistentUserIsNoop) {
    AudioMixer mixer;
    mixer.removeUser(UserId(999)); // Should not crash
}

// ============================================================
// max_speakers limit
// ============================================================

TEST(AudioMixerTest, MaxSpeakersLimitsActiveStreams) {
    AudioMixer::Config cfg;
    cfg.max_speakers = 2;
    cfg.frame_size = 480;
    AudioMixer mixer(cfg);

    const uint32_t frame_size = 480;
    auto pcm = makeConstantPcm(frame_size, 0.3f);

    mixer.reset();

    mixer.addStream(UserId(1), pcm.data(), frame_size);
    mixer.addStream(UserId(2), pcm.data(), frame_size);
    EXPECT_EQ(mixer.activeStreamCount(), 2u);

    // Third user should be ignored
    mixer.addStream(UserId(3), pcm.data(), frame_size);
    EXPECT_EQ(mixer.activeStreamCount(), 2u);

    // Mix should only contain 2 streams
    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // 0.3 + 0.3 = 0.6 (no limiting needed)
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], 0.6f, 0.001f);
    }
}

TEST(AudioMixerTest, MaxSpeakersAllowsReAddAfterReset) {
    AudioMixer::Config cfg;
    cfg.max_speakers = 2;
    cfg.frame_size = 480;
    AudioMixer mixer(cfg);

    const uint32_t frame_size = 480;
    auto pcm = makeConstantPcm(frame_size, 0.3f);

    // First cycle: fill max_speakers
    mixer.reset();
    mixer.addStream(UserId(1), pcm.data(), frame_size);
    mixer.addStream(UserId(2), pcm.data(), frame_size);
    mixer.addStream(UserId(3), pcm.data(), frame_size); // Ignored
    EXPECT_EQ(mixer.activeStreamCount(), 2u);

    // Second cycle: reset clears active count
    mixer.reset();
    mixer.addStream(UserId(1), pcm.data(), frame_size);
    mixer.addStream(UserId(2), pcm.data(), frame_size);
    mixer.addStream(UserId(3), pcm.data(), frame_size); // Still ignored
    EXPECT_EQ(mixer.activeStreamCount(), 2u);
}

TEST(AudioMixerTest, SameUserTwiceInOneCycleDoesNotDoubleCount) {
    AudioMixer mixer;
    const uint32_t frame_size = 480;
    auto pcm1 = makeConstantPcm(frame_size, 0.3f);
    auto pcm2 = makeConstantPcm(frame_size, 0.5f);

    mixer.reset();
    mixer.addStream(UserId(1), pcm1.data(), frame_size);
    mixer.addStream(UserId(1), pcm2.data(), frame_size); // Same user, overwrites

    EXPECT_EQ(mixer.activeStreamCount(), 1u);

    std::vector<float> output(frame_size, 0.0f);
    mixer.mix(output.data(), frame_size);

    // Should use the latest PCM data (0.5f) at default volume
    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_NEAR(output[i], 0.5f, 0.001f);
    }
}

} // namespace
} // namespace nevo
