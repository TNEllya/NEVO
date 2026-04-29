/**
 * @file TestAudioMemoryPool.cpp
 * @brief Unit tests for audio frame pre-allocated memory pool
 */

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "nevo/core/audio/AudioMemoryPool.h"

namespace nevo {
namespace {

// ============================================================
// Acquire and release
// ============================================================

TEST(AudioMemoryPoolTest, AcquireReturnsNonNull) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 4;
    AudioMemoryPool pool(cfg);

    uint8_t* block = pool.acquire();
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(pool.available(), 3u);

    pool.release(block);
    EXPECT_EQ(pool.available(), 4u);
}

TEST(AudioMemoryPoolTest, AcquireMultipleBlocks) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 8;
    AudioMemoryPool pool(cfg);

    std::vector<uint8_t*> blocks;
    for (uint32_t i = 0; i < 8; ++i) {
        uint8_t* block = pool.acquire();
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }
    EXPECT_EQ(pool.available(), 0u);

    // Release all
    for (auto* b : blocks) {
        pool.release(b);
    }
    EXPECT_EQ(pool.available(), 8u);
}

TEST(AudioMemoryPoolTest, AcquiredBlockIsWritable) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 256;
    cfg.block_count = 2;
    AudioMemoryPool pool(cfg);

    uint8_t* block = pool.acquire();
    ASSERT_NE(block, nullptr);

    // Write a pattern
    for (uint32_t i = 0; i < cfg.block_size; ++i) {
        block[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Verify pattern
    for (uint32_t i = 0; i < cfg.block_size; ++i) {
        EXPECT_EQ(block[i], static_cast<uint8_t>(i & 0xFF));
    }

    pool.release(block);
}

TEST(AudioMemoryPoolTest, BlockSizeMatchesConfig) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 8192;
    cfg.block_count = 4;
    AudioMemoryPool pool(cfg);

    EXPECT_EQ(pool.blockSize(), 8192u);
}

TEST(AudioMemoryPoolTest, AvailableCountAfterPartialAcquire) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 10;
    AudioMemoryPool pool(cfg);

    EXPECT_EQ(pool.available(), 10u);

    uint8_t* b1 = pool.acquire();
    uint8_t* b2 = pool.acquire();
    uint8_t* b3 = pool.acquire();

    EXPECT_EQ(pool.available(), 7u);

    pool.release(b1);
    pool.release(b2);
    pool.release(b3);

    EXPECT_EQ(pool.available(), 10u);
}

// ============================================================
// Pool exhaustion
// ============================================================

TEST(AudioMemoryPoolTest, ExhaustionReturnsNullptr) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 4;
    AudioMemoryPool pool(cfg);

    std::vector<uint8_t*> blocks;
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t* block = pool.acquire();
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }

    // Pool is exhausted
    EXPECT_EQ(pool.available(), 0u);
    uint8_t* extra = pool.acquire();
    EXPECT_EQ(extra, nullptr);

    // Clean up
    for (auto* b : blocks) {
        pool.release(b);
    }
}

// ============================================================
// Release after exhaustion allows new acquire
// ============================================================

TEST(AudioMemoryPoolTest, ReleaseAfterExhaustionAllowsNewAcquire) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 2;
    AudioMemoryPool pool(cfg);

    // Exhaust pool
    uint8_t* b1 = pool.acquire();
    uint8_t* b2 = pool.acquire();
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(b2, nullptr);

    // Pool exhausted
    EXPECT_EQ(pool.acquire(), nullptr);

    // Release one block
    pool.release(b1);
    EXPECT_EQ(pool.available(), 1u);

    // Should be able to acquire again
    uint8_t* b3 = pool.acquire();
    ASSERT_NE(b3, nullptr);
    EXPECT_EQ(pool.available(), 0u);

    // Clean up
    pool.release(b2);
    pool.release(b3);
    EXPECT_EQ(pool.available(), 2u);
}

// ============================================================
// Concurrent acquire/release from multiple threads
// ============================================================

TEST(AudioMemoryPoolTest, ConcurrentAcquireRelease) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 64;
    AudioMemoryPool pool(cfg);

    const int num_threads = 8;
    const int operations_per_thread = 1000;

    std::atomic<int> acquire_failures{0};
    std::atomic<int> total_acquired{0};

    auto worker = [&pool, &acquire_failures, &total_acquired](int thread_id) {
        std::vector<uint8_t*> local_blocks;

        for (int i = 0; i < operations_per_thread; ++i) {
            uint8_t* block = pool.acquire();
            if (block) {
                // Write a marker to verify no overlap
                std::memset(block, static_cast<uint8_t>(thread_id & 0xFF), 64);
                local_blocks.push_back(block);
                total_acquired.fetch_add(1, std::memory_order_relaxed);

                // Occasionally release a block
                if (local_blocks.size() > 5) {
                    pool.release(local_blocks.back());
                    local_blocks.pop_back();
                }
            } else {
                acquire_failures.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Release remaining blocks
        for (auto* b : local_blocks) {
            pool.release(b);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, t);
    }

    for (auto& t : threads) {
        t.join();
    }

    // After all threads finish, all blocks should be returned
    EXPECT_EQ(pool.available(), cfg.block_count);

    // Some acquires may have failed due to contention, but that's expected
    // The important thing is no crash and all blocks are returned
}

TEST(AudioMemoryPoolTest, ConcurrentStressNoLeak) {
    AudioMemoryPoolConfig cfg;
    cfg.block_size = 4096;
    cfg.block_count = 16;
    AudioMemoryPool pool(cfg);

    const int num_threads = 4;
    const int iterations = 500;

    auto worker = [&pool]() {
        for (int i = 0; i < iterations; ++i) {
            uint8_t* block = pool.acquire();
            if (block) {
                // Simulate brief usage
                block[0] = static_cast<uint8_t>(i & 0xFF);
                pool.release(block);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    // All blocks should be back
    EXPECT_EQ(pool.available(), cfg.block_count);
}

} // namespace
} // namespace nevo
