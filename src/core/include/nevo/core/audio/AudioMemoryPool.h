#pragma once
/**
 * @file AudioMemoryPool.h
 * @brief 音频帧预分配内存池
 *
 * 避免在实时音频回调中触发 malloc/new。
 * 所有内存块在初始化时预分配，运行时 acquire/release 为 O(1) 原子操作。
 *
 * 使用场景：
 *   - Opus 编码器输出缓冲区
 *   - PCM 解码输出缓冲区
 *   - UDP 包组装缓冲区
 *
 * 线程安全：acquire 和 release 使用原子操作，实时线程安全。
 */

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nevo {

/// 内存池配置
struct AudioMemoryPoolConfig {
    uint32_t block_size = 4096;  // 每块大小（字节），足够容纳最大 Opus 帧
    uint32_t block_count = 64;   // 总块数
};

class AudioMemoryPool {
public:
        AudioMemoryPool() : AudioMemoryPool(AudioMemoryPoolConfig{}) {}
    explicit AudioMemoryPool(const AudioMemoryPoolConfig& config);
    ~AudioMemoryPool();

    // 禁止拷贝和移动
    AudioMemoryPool(const AudioMemoryPool&) = delete;
    AudioMemoryPool& operator=(const AudioMemoryPool&) = delete;

    /// 从池中获取一个内存块
    /// @return 块指针，池耗尽时返回 nullptr
    uint8_t* acquire() noexcept;

    /// 归还内存块到池中
    /// @param block 之前通过 acquire() 获取的指针
    void release(uint8_t* block) noexcept;

    /// 查询当前可用块数
    uint32_t available() const noexcept;

    /// 查询块大小
    uint32_t blockSize() const noexcept { return config_.block_size; }

private:
    AudioMemoryPoolConfig config_;

    /// 连续内存区域（所有块）
    std::vector<uint8_t> storage_;

    /// 空闲块索引栈（无锁实现）
    std::vector<uint8_t*> free_stack_;

    /// 栈顶指针（原子变量，实现无锁 acquire/release）
    std::atomic<uint32_t> stack_top_{0};
};

} // namespace nevo
