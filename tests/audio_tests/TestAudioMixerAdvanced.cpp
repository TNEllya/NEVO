/**
 * @file TestAudioMixerAdvanced.cpp
 * @brief AudioMixer 主音量控制和高级功能测试
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "nevo/core/audio/AudioMixer.h"

namespace nevo {

class AudioMixerVolumeTest : public ::testing::Test {
protected:
    void SetUp() override {
        mixer = std::make_unique<AudioMixer>();
        frame_size = 480;
    }

    std::unique_ptr<AudioMixer> mixer;
    uint32_t frame_size;
};

static std::vector<float> makePcm(uint32_t size, float value) {
    return std::vector<float>(size, value);
}

static float peakAbs(const float* data, uint32_t count) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

TEST_F(AudioMixerVolumeTest, SetVolumeAffectsAllStreams) {
    auto pcm = makePcm(frame_size, 0.5f);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);
    mixer->addStream(UserId(2), pcm.data(), frame_size);

    mixer->setVolume(0.5f);

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    float peak = peakAbs(output.data(), frame_size);
    EXPECT_LE(peak, 1.0f);
    EXPECT_GT(peak, 0.0f);
}

TEST_F(AudioMixerVolumeTest, ZeroVolumeMutesAllOutput) {
    auto pcm = makePcm(frame_size, 0.5f);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);
    mixer->setVolume(0.0f);

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_FLOAT_EQ(output[i], 0.0f);
    }
}

TEST_F(AudioMixerVolumeTest, MaxVolumeAllowsBoost) {
    auto pcm = makePcm(frame_size, 0.3f);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);
    mixer->setVolume(2.0f);

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    float peak = peakAbs(output.data(), frame_size);
    EXPECT_LE(peak, 1.0f);
    EXPECT_GT(peak, 0.5f);
}

TEST_F(AudioMixerVolumeTest, VolumeClampedToValidRange) {
    mixer->setVolume(-1.0f);
    mixer->setVolume(3.0f);
    mixer->setVolume(1.5f);
    EXPECT_FLOAT_EQ(mixer->getUserVolume(UserId(1)), 1.0f);
}

TEST_F(AudioMixerVolumeTest, VolumePersistsAcrossReset) {
    mixer->setVolume(0.5f);

    auto pcm = makePcm(frame_size, 0.5f);
    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);

    std::vector<float> output1(frame_size, 0.0f);
    mixer->mix(output1.data(), frame_size);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);

    std::vector<float> output2(frame_size, 0.0f);
    mixer->mix(output2.data(), frame_size);

    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_FLOAT_EQ(output1[i], output2[i]);
    }
}

TEST_F(AudioMixerVolumeTest, ClearRemovesAllStreams) {
    auto pcm = makePcm(frame_size, 0.5f);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);
    EXPECT_EQ(mixer->activeStreamCount(), 1u);

    mixer->clear();
    EXPECT_EQ(mixer->activeStreamCount(), 0u);
}

TEST_F(AudioMixerVolumeTest, ClearThenMixProducesSilence) {
    auto pcm = makePcm(frame_size, 0.5f);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);
    mixer->clear();

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_FLOAT_EQ(output[i], 0.0f);
    }
}

TEST_F(AudioMixerVolumeTest, AddInputEquivalentToAddStream) {
    auto pcm = makePcm(frame_size, 0.3f);

    AudioMixer mixer1, mixer2;

    mixer1.reset();
    mixer1.addInput(UserId(1), pcm.data(), frame_size);
    std::vector<float> output1(frame_size, 0.0f);
    mixer1.mix(output1.data(), frame_size);

    mixer2.reset();
    mixer2.addStream(UserId(1), pcm.data(), frame_size);
    std::vector<float> output2(frame_size, 0.0f);
    mixer2.mix(output2.data(), frame_size);

    for (uint32_t i = 0; i < frame_size; ++i) {
        EXPECT_FLOAT_EQ(output1[i], output2[i]);
    }
}

TEST_F(AudioMixerVolumeTest, MasterVolumeWithUserVolume) {
    auto pcm = makePcm(frame_size, 0.5f);

    mixer->setUserVolume(UserId(1), 0.5f);
    mixer->setVolume(0.5f);

    mixer->reset();
    mixer->addStream(UserId(1), pcm.data(), frame_size);

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    float peak = peakAbs(output.data(), frame_size);
    EXPECT_LE(peak, 1.0f);
    EXPECT_GT(peak, 0.0f);
}

TEST_F(AudioMixerVolumeTest, TwentyUsersConcurrentMixing) {
    const uint32_t num_users = 20;
    auto pcm = makePcm(frame_size, 0.05f);

    mixer->reset();
    for (uint32_t i = 0; i < num_users; ++i) {
        mixer->addStream(UserId(i), pcm.data(), frame_size);
    }

    EXPECT_EQ(mixer->activeStreamCount(), num_users);

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    float peak = peakAbs(output.data(), frame_size);
    EXPECT_LE(peak, 1.0f);
}

TEST_F(AudioMixerVolumeTest, EmptyMixDoesNotCrash) {
    mixer->reset();

    std::vector<float> output(frame_size, 0.0f);
    mixer->mix(output.data(), frame_size);

    float peak = peakAbs(output.data(), frame_size);
    EXPECT_FLOAT_EQ(peak, 0.0f);
}

TEST_F(AudioMixerVolumeTest, ConfiguredMaxSpeakersLimitsStreams) {
    AudioMixer::Config cfg;
    cfg.max_speakers = 5;
    cfg.frame_size = frame_size;
    AudioMixer limited_mixer(cfg);

    auto pcm = makePcm(frame_size, 0.2f);

    limited_mixer.reset();
    for (uint32_t i = 0; i < 10; ++i) {
        limited_mixer.addStream(UserId(i), pcm.data(), frame_size);
    }

    EXPECT_EQ(limited_mixer.activeStreamCount(), 5u);
}

} // namespace nevo
