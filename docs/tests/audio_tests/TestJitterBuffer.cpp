/**
 * @file TestJitterBuffer.cpp
 * @brief Unit tests for UDP voice jitter buffer
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "nevo/core/audio/JitterBuffer.h"

namespace nevo {
namespace {

// Helper: generate a PCM frame with a recognizable pattern
static std::vector<float> makePcmFrame(uint32_t frame_size, float value) {
    return std::vector<float>(frame_size, value);
}

// Helper: generate a PCM frame with a linear ramp pattern
static std::vector<float> makeRampFrame(uint32_t frame_size, float start, float step) {
    std::vector<float> pcm(frame_size);
    for (uint32_t i = 0; i < frame_size; ++i) {
        pcm[i] = start + step * static_cast<float>(i);
    }
    return pcm;
}

// ============================================================
// In-order insertion and retrieval
// ============================================================

TEST(JitterBufferTest, InOrderInsertionAndRetrieval) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 3;
    cfg.max_buffer_size = 32;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert 5 frames in order (seq 0-4)
    for (uint32_t seq = 0; seq < 5; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }

    // After initial buffering (3 frames), playback should start
    EXPECT_TRUE(jb.isPlaying());

    // Retrieve frames in order
    for (uint32_t seq = 0; seq < 5; ++seq) {
        auto frame = jb.getNext();
        ASSERT_TRUE(frame.has_value());
        EXPECT_FALSE(frame->lost);
        // Frame 0 is consumed during initial buffering, the data should match
        EXPECT_EQ(frame->pcm_data.size(), frame_size);
    }

    // Buffer should now be empty
    EXPECT_EQ(jb.bufferDepth(), 0u);
}

TEST(JitterBufferTest, NotPlayingBeforeInitialBuffering) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 5;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert fewer frames than max_delay_frames
    for (uint32_t seq = 0; seq < 3; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }

    EXPECT_FALSE(jb.isPlaying());

    // getNext should return nullopt since not playing yet
    auto frame = jb.getNext();
    EXPECT_FALSE(frame.has_value());
}

TEST(JitterBufferTest, NextPlaySequence) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert frame at seq=10
    auto pcm = makePcmFrame(frame_size, 1.0f);
    jb.insert(10, pcm.data(), frame_size);

    EXPECT_EQ(jb.nextPlaySequence(), 10u);
}

// ============================================================
// Out-of-order insertion (should be reordered)
// ============================================================

TEST(JitterBufferTest, OutOfOrderInsertionIsReordered) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 4;
    cfg.max_buffer_size = 32;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert frames out of order: seq 0, 3, 1, 2
    auto pcm0 = makePcmFrame(frame_size, 0.0f);
    auto pcm3 = makePcmFrame(frame_size, 3.0f);
    auto pcm1 = makePcmFrame(frame_size, 1.0f);
    auto pcm2 = makePcmFrame(frame_size, 2.0f);

    jb.insert(0, pcm0.data(), frame_size);
    jb.insert(3, pcm3.data(), frame_size);
    jb.insert(1, pcm1.data(), frame_size);
    jb.insert(2, pcm2.data(), frame_size);

    // Should have 4 frames buffered
    EXPECT_EQ(jb.bufferDepth(), 4u);
    EXPECT_TRUE(jb.isPlaying());

    // Retrieve should give frames in order
    for (uint32_t seq = 0; seq < 4; ++seq) {
        auto frame = jb.getNext();
        ASSERT_TRUE(frame.has_value()) << "Expected frame at seq " << seq;
        EXPECT_FALSE(frame->lost);
    }
}

TEST(JitterBufferTest, DuplicateFramesAreDiscarded) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    auto pcm = makePcmFrame(frame_size, 1.0f);

    jb.insert(0, pcm.data(), frame_size);
    jb.insert(0, pcm.data(), frame_size); // duplicate

    EXPECT_EQ(jb.bufferDepth(), 1u);
}

// ============================================================
// Gap detection with PLC compensation
// ============================================================

TEST(JitterBufferTest, GapDetectionGeneratesPlcFrame) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 3;
    cfg.max_buffer_size = 32;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert seq 0, 1, then skip 2, insert 3
    auto pcm0 = makePcmFrame(frame_size, 0.0f);
    auto pcm1 = makePcmFrame(frame_size, 1.0f);
    auto pcm3 = makePcmFrame(frame_size, 3.0f);

    jb.insert(0, pcm0.data(), frame_size);
    jb.insert(1, pcm1.data(), frame_size);
    jb.insert(3, pcm3.data(), frame_size);

    EXPECT_TRUE(jb.isPlaying());

    // Frame 0
    auto f0 = jb.getNext();
    ASSERT_TRUE(f0.has_value());
    EXPECT_FALSE(f0->lost);

    // Frame 1
    auto f1 = jb.getNext();
    ASSERT_TRUE(f1.has_value());
    EXPECT_FALSE(f1->lost);

    // Frame 2 is missing, but frame 3 is in buffer
    // Should generate a PLC frame for seq 2
    auto f2 = jb.getNext();
    ASSERT_TRUE(f2.has_value());
    EXPECT_TRUE(f2->lost) << "Expected PLC frame for missing seq 2";
    EXPECT_EQ(f2->pcm_data.size(), frame_size);
    // PLC frames are zero-filled
    EXPECT_FLOAT_EQ(f2->pcm_data[0], 0.0f);

    // Frame 3 should be available
    auto f3 = jb.getNext();
    ASSERT_TRUE(f3.has_value());
    EXPECT_FALSE(f3->lost);
}

// ============================================================
// Buffer overflow behavior
// ============================================================

TEST(JitterBufferTest, BufferOverflowDropsOldest) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    cfg.max_buffer_size = 5;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert 7 frames (exceeds max_buffer_size of 5)
    for (uint32_t seq = 0; seq < 7; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }

    // Buffer should not exceed max_buffer_size
    EXPECT_LE(jb.bufferDepth(), cfg.max_buffer_size);
}

// ============================================================
// Reset functionality
// ============================================================

TEST(JitterBufferTest, ResetClearsAllState) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert and buffer
    for (uint32_t seq = 0; seq < 4; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }

    EXPECT_TRUE(jb.isPlaying());
    EXPECT_GT(jb.bufferDepth(), 0u);

    // Reset
    jb.reset();

    EXPECT_FALSE(jb.isPlaying());
    EXPECT_EQ(jb.bufferDepth(), 0u);
    EXPECT_EQ(jb.nextPlaySequence(), 0u);
}

TEST(JitterBufferTest, InsertAfterResetStartsFresh) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // First round
    for (uint32_t seq = 0; seq < 3; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }
    EXPECT_TRUE(jb.isPlaying());

    // Reset and start fresh with different sequence numbers
    jb.reset();

    auto pcm = makePcmFrame(frame_size, 1.0f);
    jb.insert(100, pcm.data(), frame_size); // New starting sequence

    EXPECT_EQ(jb.nextPlaySequence(), 100u);
    EXPECT_FALSE(jb.isPlaying()); // Not enough frames yet for initial buffering

    // Add more to exceed max_delay_frames
    for (uint32_t i = 1; i < cfg.max_delay_frames; ++i) {
        auto pcm2 = makePcmFrame(frame_size, static_cast<float>(i));
        jb.insert(100 + i, pcm2.data(), frame_size);
    }

    EXPECT_TRUE(jb.isPlaying());
}

TEST(JitterBufferTest, OldFramesAreDiscarded) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    cfg.max_buffer_size = 32;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert and consume seq 0-3
    for (uint32_t seq = 0; seq < 4; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }

    // Consume all
    for (uint32_t i = 0; i < 4; ++i) {
        jb.getNext();
    }

    // Try to insert a frame with an old seq (less than current play position)
    auto old_pcm = makePcmFrame(frame_size, 0.0f);
    jb.insert(1, old_pcm.data(), frame_size); // seq 1 is already past

    // The old frame should be discarded
    EXPECT_EQ(jb.bufferDepth(), 0u);
}

TEST(JitterBufferTest, EmptyBufferReturnsNullopt) {
    JitterBuffer::Config cfg;
    cfg.max_delay_frames = 2;
    JitterBuffer jb(cfg);

    const uint32_t frame_size = 960;

    // Insert enough to start playing, then drain
    for (uint32_t seq = 0; seq < 4; ++seq) {
        auto pcm = makePcmFrame(frame_size, static_cast<float>(seq));
        jb.insert(seq, pcm.data(), frame_size);
    }

    // Drain all frames
    while (jb.getNext().has_value()) {}

    // getNext on empty playing buffer returns nullopt (underrun)
    auto frame = jb.getNext();
    EXPECT_FALSE(frame.has_value());
}

} // namespace
} // namespace nevo
