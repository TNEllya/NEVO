/**
 * @file AudioMemoryPool.cpp
 * @brief 音频帧预分配内存池实现
 */

#include "nevo/core/audio/AudioMemoryPool.h"
#include "nevo/core/common/Logger.h"
#include <cassert>

namespace nevo {

AudioMemoryPool::AudioMemoryPool(const AudioMemoryPoolConfig& config)
    : config_(config)
    , storage_(static_cast<size_t>(config.block_size) * config.block_count)
    , free_stack_(config.block_count)
{
    // 初始化空闲栈：将所有块地址压入栈中
    for (uint32_t i = 0; i < config.block_count; ++i) {
        free_stack_[i] = storage_.data() + static_cast<size_t>(i) * config.block_size;
    }
    stack_top_.store(config.block_count, std::memory_order_relaxed);

    NEVO_LOG_INFO("audio", "AudioMemoryPool initialized: {} blocks x {} bytes = {} bytes",
                  config.block_count, config.block_size, storage_.size());
}

AudioMemoryPool::~AudioMemoryPool() {
    const auto remaining = stack_top_.load(std::memory_order_relaxed);
    if (remaining != config_.block_count) {
        NEVO_LOG_WARN("audio", "AudioMemoryPool destroyed with {} blocks still in use",
                      config_.block_count - remaining);
    }
}

uint8_t* AudioMemoryPool::acquire() noexcept {
    // 原子地递减栈顶并获取块
    // 使用 CAS 循环实现无锁 acquire
    uint32_t current_top = stack_top_.load(std::memory_order_acquire);

    while (current_top > 0) {
        const uint32_t new_top = current_top - 1;
        if (stack_top_.compare_exchange_weak(current_top, new_top,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
            return free_stack_[new_top];
        }
        // CAS 失败，current_top 已被更新，重试
    }

    // 池耗尽
    NEVO_LOG_ERROR("audio", "AudioMemoryPool::acquire: pool exhausted ({} blocks)", config_.block_count);
    return nullptr;
}

void AudioMemoryPool::release(uint8_t* block) noexcept {
    if (!block) return;

    // 验证指针属于此池
    assert(block >= storage_.data() &&
           block < storage_.data() + storage_.size());

    // 原子地将块压回栈中
    // 先通过 CAS 保留槽位，成功后再写入，避免并发写入同一槽位
    uint32_t current_top = stack_top_.load(std::memory_order_acquire);

    while (true) {
        if (current_top >= config_.block_count) {
            // 栈溢出：试图归还不属于池的指针
            NEVO_LOG_ERROR("audio", "AudioMemoryPool::release: stack overflow");
            return;
        }
        if (stack_top_.compare_exchange_weak(current_top, current_top + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
            // CAS 成功，安全写入（此槽位已由此线程独占）
            free_stack_[current_top] = block;
            return;
        }
    }
}

uint32_t AudioMemoryPool::available() const noexcept {
    return stack_top_.load(std::memory_order_relaxed);
}

} // namespace nevo
