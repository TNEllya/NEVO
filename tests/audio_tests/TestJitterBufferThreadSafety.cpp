/**
 * @file TestJitterBufferThreadSafety.cpp
 * @brief Thread safety tests for JitterBuffer
 *
 * 覆盖缺口：JitterBuffer 刚添加线程安全保护（mutex），现有测试均为单线程
 * 风险等级：高 - 多线程音频处理场景下可能出现数据竞争
 *
 * 背景：根据 fix-nevo-runtime-bugs 修复，Bug #2 JitterBuffer 存在线程安全数据竞争：
 *   - push() 由网络线程调用
 *   - pop() / getNext() 由音频线程调用
 *   - 并发操作 std::map / std::deque 会导致数据竞争和崩溃
 * 修复方案：添加 mutable std::mutex mutex_ 保护所有共享数据结构
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <chrono>

#include "nevo/core/audio/JitterBuffer.h"

namespace nevo {
namespace {

constexpr uint32_t FRAME_SIZE = 960;
constexpr int NUM_PRODUCER_THREADS = 4;
constexpr int NUM_CONSUMER_THREADS = 2;
constexpr int PACKETS_PER_THREAD = 100;

static std::vector<float> makePcmFrame(float value) {
    return std::vector<float>(FRAME_SIZE, value);
}

class JitterBufferThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.max_delay_frames = 10;
        config_.max_buffer_size = 256;
    }

    JitterBuffer::Config config_;
};

TEST_F(JitterBufferThreadSafetyTest, ConcurrentPushPopNoCrash) {
    JitterBuffer jb(config_);

    std::atomic<bool> stop{false};
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};

    std::vector<std::thread> producers;
    for (int t = 0; t < NUM_PRODUCER_THREADS; ++t) {
        producers.emplace_back([&jb, &stop, &push_count, t]() {
            UserId user(t + 1);
            for (int i = 0; i < PACKETS_PER_THREAD && !stop.load(); ++i) {
                auto packet = std::vector<uint8_t>(50);
                packet[0] = static_cast<uint8_t>(t);
                packet[1] = static_cast<uint8_t>(i);
                jb.push(user, packet.data(), packet.size(), i * 1000);
                push_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int t = 0; t < NUM_CONSUMER_THREADS; ++t) {
        consumers.emplace_back([&jb, &stop, &pop_count, t]() {
            UserId user(t + 1);
            for (int i = 0; i < PACKETS_PER_THREAD && !stop.load(); ++i) {
                uint8_t* data = nullptr;
                uint32_t size = 0;
                if (jb.pop(user, data, size)) {
                    pop_count.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }
    stop.store(true);
    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_GT(push_count.load(), 0);
}

TEST_F(JitterBufferThreadSafetyTest, ConcurrentInsertGetNextNoCrash) {
    JitterBuffer jb(config_);

    std::atomic<bool> running{true};
    std::atomic<uint32_t> insert_count{0};
    std::atomic<uint32_t> getnext_count{0};

    std::thread inserter([&jb, &running, &insert_count]() {
        uint32_t seq = 0;
        while (running.load()) {
            auto pcm = makePcmFrame(static_cast<float>(seq));
            jb.insert(seq, pcm.data(), FRAME_SIZE);
            insert_count.fetch_add(1, std::memory_order_relaxed);
            ++seq;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::thread retriever([&jb, &running, &getnext_count]() {
        while (running.load()) {
            auto frame = jb.getNext();
            if (frame.has_value()) {
                getnext_count.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running.store(false);
    inserter.join();
    retriever.join();

    EXPECT_GE(insert_count.load(), 0u);
    EXPECT_GE(getnext_count.load(), 0u);
}

TEST_F(JitterBufferThreadSafetyTest, ConcurrentRemoveUserNoCrash) {
    JitterBuffer jb(config_);

    std::atomic<bool> running{true};

    std::thread remover([&jb, &running]() {
        UserId user1(1);
        UserId user2(2);

        std::vector<uint8_t> packet(50);
        jb.push(user1, packet.data(), packet.size(), 1000);
        jb.push(user2, packet.data(), packet.size(), 1000);

        while (running.load()) {
            jb.removeUser(user1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            jb.push(user1, packet.data(), packet.size(), 1000);
            jb.removeUser(user2);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            jb.push(user2, packet.data(), packet.size(), 1000);
        }
    });

    std::thread popper([&jb, &running]() {
        UserId user1(1);
        UserId user2(2);

        while (running.load()) {
            uint8_t* data = nullptr;
            uint32_t size = 0;
            jb.pop(user1, data, size);
            jb.pop(user2, data, size);
            std::this_thread::yield();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    running.store(false);
    remover.join();
    popper.join();
}

TEST_F(JitterBufferThreadSafetyTest, ConcurrentResetNoCrash) {
    JitterBuffer jb(config_);

    std::atomic<bool> running{true};
    std::atomic<int> reset_count{0};

    std::thread reseter([&jb, &running, &reset_count]() {
        while (running.load()) {
            jb.reset();
            reset_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    std::thread pusher([&jb, &running]() {
        UserId user(1);
        std::vector<uint8_t> packet(50);

        while (running.load()) {
            for (int i = 0; i < 5; ++i) {
                jb.push(user, packet.data(), packet.size(), i * 1000);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    });

    std::thread getter([&jb, &running]() {
        while (running.load()) {
            auto frame = jb.getNext();
            (void)frame;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    running.store(false);
    reseter.join();
    pusher.join();
    getter.join();

    EXPECT_GE(reset_count.load(), 1);
}

TEST_F(JitterBufferThreadSafetyTest, ConcurrentBufferDepthRead) {
    JitterBuffer jb(config_);

    std::atomic<bool> running{true};
    std::vector<uint32_t> depth_reads;

    std::thread inserter([&jb, &running]() {
        uint32_t seq = 0;
        while (running.load()) {
            auto pcm = makePcmFrame(static_cast<float>(seq));
            jb.insert(seq, pcm.data(), FRAME_SIZE);
            ++seq;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    std::thread reader([&jb, &running, &depth_reads]() {
        while (running.load()) {
            uint32_t depth = jb.bufferDepth();
            depth_reads.push_back(depth);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running.store(false);
    inserter.join();
    reader.join();

    EXPECT_GT(depth_reads.size(), 0u);
    for (uint32_t d : depth_reads) {
        EXPECT_LE(d, config_.max_buffer_size);
    }
}

TEST_F(JitterBufferThreadSafetyTest, ManyThreadsManyUsers) {
    JitterBuffer jb(config_);

    constexpr int NUM_USERS = 10;
    constexpr int OPS_PER_USER = 50;
    std::atomic<bool> running{true};
    std::vector<std::thread> threads;

    for (int u = 0; u < NUM_USERS; ++u) {
        threads.emplace_back([&jb, &running, u]() {
            UserId user(u);
            std::vector<uint8_t> packet(50);

            for (int i = 0; i < OPS_PER_USER; ++i) {
                if (running.load()) {
                    jb.push(user, packet.data(), packet.size(), i * 1000);
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int u = 0; u < NUM_USERS; ++u) {
        threads.emplace_back([&jb, &running, u]() {
            UserId user(u);

            for (int i = 0; i < OPS_PER_USER && running.load(); ++i) {
                uint8_t* data = nullptr;
                uint32_t size = 0;
                jb.pop(user, data, size);
                std::this_thread::yield();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST_F(JitterBufferThreadSafetyTest, StressTestHighContention) {
    JitterBuffer jb(config_);

    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 200;
    std::atomic<bool> running{true};
    std::atomic<int> total_ops{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&jb, &running, &total_ops, t]() {
            UserId user(t % 4 + 1);

            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                if (!running.load()) break;

                int op = i % 4;
                switch (op) {
                    case 0: {
                        auto pcm = makePcmFrame(static_cast<float>(i));
                        jb.insert(i * NUM_THREADS + t, pcm.data(), FRAME_SIZE);
                        break;
                    }
                    case 1: {
                        auto frame = jb.getNext();
                        break;
                    }
                    case 2: {
                        std::vector<uint8_t> packet(50);
                        jb.push(user, packet.data(), packet.size(), i * 1000);
                        break;
                    }
                    case 3: {
                        uint8_t* data = nullptr;
                        uint32_t size = 0;
                        jb.pop(user, data, size);
                        break;
                    }
                }
                total_ops.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_GE(total_ops.load(), NUM_THREADS * OPS_PER_THREAD / 2);
}

} // namespace
} // namespace nevo
