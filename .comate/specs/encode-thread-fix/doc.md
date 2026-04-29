# 修复编码管线：为 AudioEngine 添加编码线程

## 问题描述

用户选择输入设备后麦克风没有声音。经过深入分析，**真正的根本原因不是设备切换逻辑，而是整个编码管线是死代码**。

`AudioEngine::processEncodeCycle()` 已完整实现（从 `input_fifo_` 读取帧 → Opus 编码 → 调用 `encoded_callback_`），但**从未被任何代码调用**。结果：
- `input_fifo_` 持续接收麦克风数据但无人消费
- 约 640ms 后 FIFO 满溢，所有后续帧静默丢弃
- Opus 编码器从未处理任何数据
- `encoded_callback_` 从未触发 → `AudioInput::onEncodedAudio()` 从未执行 → 网络从未发送语音数据

## 当前架构

```
[麦克风] → maInputCallback → input_fifo_ → ??? (无消费者) → 编码 → 网络
[网络]   → queueAudioData → JitterBuffer → processMixCycle → output_fifo_ → maOutputCallback → [扬声器]
```

`processMixCycle()` 被事件驱动调用（每当 `queueAudioData()` 收到远端包），但 `processEncodeCycle()` 没有对应的驱动机制。

## 修复方案

在 `AudioEngine::initialize()` 中创建专用编码线程，持续调用 `processEncodeCycle()`。

### 编码线程设计

```cpp
// 编码线程主循环
void encodeThreadFunc() {
    while (encode_thread_running_.load(std::memory_order_acquire)) {
        processEncodeCycle();
        // 如果 input_fifo_ 中没有数据，短暂休眠避免忙等
        // 使用 sleep_for(1ms) 而非条件变量，因为：
        //   1. 条件变量需要 input callback 中 notify，违反实时安全约束
        //   2. 1ms 延迟对 VoIP 20ms 帧完全可接受
        //   3. 简单可靠，无竞态风险
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

### 关键设计决策

1. **不用条件变量**：`maInputCallback` 运行在实时线程，不能调用 `notify_one()`（系统调用）
2. **1ms sleep_for**：在 input_fifo_ 无数据时降低 CPU 占用；有数据时 `processEncodeCycle()` 立即处理所有可用帧，延迟 < 1ms
3. **`std::jthread`**：C++20 原子支持请求停止，析构自动 join，比 `std::thread` 更安全
4. **先停止线程再停止设备**：`shutdown()` 中确保编码线程退出后再停 miniaudio 设备，避免回调访问已释放资源

## 受影响文件

### 1. `src/core/include/nevo/core/audio/AudioEngine.h`
- **类型**：添加成员变量和方法
- **修改**：
  - 添加 `#include <thread>` 和 `#include <stop_token>`
  - 添加私有方法 `void encodeThreadFunc()`
  - 添加成员变量 `std::jthread encode_thread_`

### 2. `src/core/src/audio/AudioEngine.cpp`
- **类型**：修改 `initialize()` 和 `shutdown()` 方法，添加 `encodeThreadFunc()` 实现
- **修改**：
  - `initialize()`：在 `running_.store(true)` 之前启动编码线程
  - `shutdown()`：在 `running_.store(false)` 之后请求停止并 join 编码线程
  - 新增 `encodeThreadFunc()` 实现

## 实现细节

### AudioEngine.h 新增内容

```cpp
// 在 #include 区域添加
#include <thread>
#include <stop_token>

// 在 private 方法区域添加
void encodeThreadFunc(std::stop_token stop_token);

// 在数据成员区域（running_ 附近）添加
std::jthread encode_thread_;
```

### AudioEngine.cpp - initialize() 修改

在 `running_.store(true, std::memory_order_release);` 之前插入：

```cpp
// 14. 启动编码线程
encode_thread_ = std::jthread(&AudioEngine::encodeThreadFunc, this);
if (!encode_thread_.joinable()) {
    NEVO_LOG_ERROR("audio", "Failed to start encode thread");
    ma_device_stop(output_device_.get());
    ma_device_stop(input_device_.get());
    return Err<void>(ResultCode::InternalError, "Failed to start encode thread");
}
NEVO_LOG_INFO("audio", "Encode thread started");
```

### AudioEngine.cpp - shutdown() 修改

在 `running_.store(false, std::memory_order_release);` 之后插入：

```cpp
// 请求编码线程停止并等待退出
if (encode_thread_.joinable()) {
    encode_thread_.request_stop();
    encode_thread_.join();
}
```

### AudioEngine.cpp - encodeThreadFunc() 实现

```cpp
void AudioEngine::encodeThreadFunc(std::stop_token stop_token) {
    NEVO_LOG_INFO("audio", "Encode thread running");

    while (!stop_token.stop_requested()) {
        processEncodeCycle();

        // 如果 FIFO 中没有数据，短暂休眠避免忙等
        // 使用 sleep_for(1ms) 而非条件变量，因为实时回调中不能调用 notify
#ifdef NEVO_HAS_BOOST
        if (input_fifo_.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#else
        {
            std::lock_guard<std::mutex> lock(input_fifo_mutex_);
            if (input_fifo_.empty()) {
                input_fifo_mutex_.unlock();  // 释放锁再休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;  // 重新获取锁检查
            }
        }
#endif
    }

    NEVO_LOG_INFO("audio", "Encode thread exiting");
}
```

**注意**：Boost SPSC 路径中 `input_fifo_.empty()` 是无锁操作，可在编码线程安全调用。非 Boost 路径需要简化处理以避免在 mutex 持有时 sleep。

简化版（非 Boost 路径）：

```cpp
void AudioEngine::encodeThreadFunc(std::stop_token stop_token) {
    NEVO_LOG_INFO("audio", "Encode thread running");

    while (!stop_token.stop_requested()) {
        bool had_data = false;
#ifdef NEVO_HAS_BOOST
        // processEncodeCycle 内部处理所有 pop 逻辑
        // 通过检查处理前后的 FIFO 状态判断是否有数据
#endif
        processEncodeCycle();

#ifdef NEVO_HAS_BOOST
        if (input_fifo_.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#else
        // 非 Boost 路径：直接短暂休眠，processEncodeCycle 内部会获取锁
        {
            std::lock_guard<std::mutex> lock(input_fifo_mutex_);
            had_data = !input_fifo_.empty();
        }
        if (!had_data) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
    }

    NEVO_LOG_INFO("audio", "Encode thread exiting");
}
```

## 边界条件和异常处理

1. **编码线程启动失败**：`initialize()` 回滚（停止已启动的设备），返回错误
2. **编码线程中 Opus 编码失败**：由 `processEncodeCycle()` 内部处理（已有日志和 continue 逻辑）
3. **编码线程中内存池耗尽**：由 `processEncodeCycle()` 内部处理（已有日志和 continue 逻辑）
4. **shutdown 期间编码线程阻塞**：`request_stop()` 设置停止标志，循环在 < 1ms 内退出
5. **设备切换期间编码线程继续运行**：安全——`processEncodeCycle()` 只访问 `input_fifo_`，设备切换不影响 FIFO

## 数据流路径（修复后）

```
[麦克风] → maInputCallback → input_fifo_ → 编码线程(processEncodeCycle) → Opus编码 → encoded_callback_ → AudioInput::onEncodedAudio → NetworkManager::sendVoicePacket → [网络]
[网络]   → NetworkManager → queueAudioData → JitterBuffer → processMixCycle → output_fifo_ → maOutputCallback → [扬声器]
```

## 预期结果

1. 麦克风数据被正确编码并通过网络发送
2. 远端用户能听到本地用户的声音
3. 设备切换后编码线程继续正常工作
4. `input_fifo_` 不会持续满溢
5. CPU 占用合理（无数据时 ~0.1%，有数据时随编码负载变化）
