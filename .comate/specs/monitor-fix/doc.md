# 修复麦克风监听功能无声问题

## 问题描述

用户点击"Test Input"按钮后无法听到自己的麦克风声音。

## 根因分析

经过端到端代码追踪，发现以下问题：

### 根因 #1（关键）：AudioEngine 在测试输入时可能未运行

`AudioEngine::initialize()`（启动设备、开始回调）仅在用户连接服务器时通过 `initAudioSubsystem()` 调用。如果用户在**未连接**状态下打开音频设置并点击"Test Input"，`AudioEngine` 不在运行状态，回调不会触发，`monitor_fifo_` 不会有数据流入/流出，自然没有声音。

即使连接状态下，`ClientCore::setMonitorEnabled()` 也没有检查引擎是否运行。

### 根因 #2：SPSC 队列约束违规

`boost::lockfree::spsc_queue` 要求严格单生产者-单消费者。但 `setMonitorEnabled(false)` 从**主线程**排空 `monitor_fifo_`，而 `maOutputCallback` 从**输出实时线程**弹出。两者同时消费违反 SPSC 约束，导致未定义行为。

```cpp
void AudioEngine::setMonitorEnabled(bool enabled) {
    monitor_enabled_.store(enabled, ...);
    if (!enabled) {
        while (monitor_fifo_.pop(dummy)) {}  // 主线程消费！与输出回调冲突
    }
}
```

### 根因 #3：输出回调的帧消费不匹配

`maOutputCallback` 每次只弹出**一帧**（960 采样），但设备可能请求不同数量的采样。如果 `frame_count != 960`：
- `frame_count < 960`：弹出整帧但只用前半部分，后半部分浪费 → FIFO 消耗过快 → 频繁空 FIFO → 断续声音
- `frame_count > 960`：一帧不够填充输出缓冲区 → 剩余部分填静音

## 修复方案

### 修复 #1：确保 AudioEngine 在监控时运行

在 `ClientCore::setMonitorEnabled(true)` 中，如果 AudioEngine 未运行，先初始化它。在 `setMonitorEnabled(false)` 中，如果用户未连接，关闭 AudioEngine。

**ClientCore.h** — 添加成员：
```cpp
bool audio_engine_started_for_monitor_{false};  // 标记引擎是否仅因监控而启动
```

**ClientCore.cpp** — 修改：
```cpp
void ClientCore::setMonitorEnabled(bool enabled) {
    if (!audio_engine_) return;
    
    if (enabled) {
        if (!audio_engine_->isRunning()) {
            AudioEngine::Config config;
            auto result = audio_engine_->initialize(config);
            if (!result) {
                NEVO_LOG_ERROR("client", "Failed to init AudioEngine for monitor: {}",
                              result.error().message());
                return;
            }
            audio_engine_started_for_monitor_ = true;
        }
        audio_engine_->setMonitorEnabled(true);
    } else {
        audio_engine_->setMonitorEnabled(false);
        if (audio_engine_started_for_monitor_ && state_ != ClientState::Connected) {
            audio_engine_->shutdown();
            audio_engine_started_for_monitor_ = false;
        }
    }
}
```

### 修复 #2：消除 SPSC 违规

不在主线程排空 `monitor_fifo_`。改为：设置 `monitor_enabled_ = false` 后，让输出回调自然停止消费，FIFO 中的残留帧会在下次启用时被新数据覆盖或被自然消费。在 `shutdown()` 中排空（此时回调已停止，无冲突）。

**AudioEngine.cpp** — 修改：
```cpp
void AudioEngine::setMonitorEnabled(bool enabled) {
    monitor_enabled_.store(enabled, std::memory_order_release);
    // 不在此处排空 monitor_fifo_！
    // 输出回调会因 monitor_enabled_=false 而停止消费
    // FIFO 中的残留帧在下次启用时由输出回调自然消费
    // shutdown() 中会在回调停止后排空
}
```

### 修复 #3：输出回调累积式帧消费

在输出回调中维护一个"当前帧"缓冲区和偏移量，跨回调调用累积消费。当偏移量耗尽当前帧时，才弹出新帧。这样无论 `frame_count` 是多少，都能正确消费帧数据。

**AudioEngine.h** — 添加成员：
```cpp
// 输出回调帧累积状态（仅在输出回调线程中使用）
AudioFrame output_current_frame_{};
uint32_t output_frame_offset_ = 0;
bool output_frame_valid_ = false;

AudioFrame monitor_current_frame_{};
uint32_t monitor_frame_offset_ = 0;
bool monitor_frame_valid_ = false;
```

**AudioEngine.cpp** — 重写输出回调的帧消费逻辑：
```cpp
// 替代原来的单帧弹出逻辑
// 从当前帧的 offset 位置开始，填充 frame_count 个采样
// 如果当前帧不够，弹出新帧继续填充

uint32_t written = 0;
while (written < frame_count) {
    // 确保 output 帧有效
    if (!output_frame_valid_) {
        output_frame_valid_ = output_fifo_.pop(output_current_frame_);
        output_frame_offset_ = 0;
    }
    // 确保 monitor 帧有效
    if (!monitor_frame_valid_ && monitor_enabled_.load(std::memory_order_relaxed)) {
        monitor_frame_valid_ = monitor_fifo_.pop(monitor_current_frame_);
        monitor_frame_offset_ = 0;
    }
    
    // 如果两个 FIFO 都没有更多数据，退出
    if (!output_frame_valid_ && !monitor_frame_valid_) break;
    
    // 从当前偏移位置混合一个采样
    const uint32_t out_idx = output_frame_offset_;
    const uint32_t mon_idx = monitor_frame_offset_;
    
    float sample = 0.0f;
    if (output_frame_valid_) sample += output_current_frame_[out_idx] * volume;
    if (monitor_frame_valid_) sample += monitor_current_frame_[mon_idx] * mon_vol;
    pcm_output[written] = std::clamp(sample, -1.0f, 1.0f);
    
    // 推进偏移
    output_frame_offset_++;
    monitor_frame_offset_++;
    
    // 如果某个帧已耗尽，标记为无效
    if (output_frame_offset_ >= kFrameSize) output_frame_valid_ = false;
    if (monitor_frame_offset_ >= kFrameSize) monitor_frame_valid_ = false;
    
    written++;
}

// 剩余填充静音
if (written < frame_count) {
    std::memset(pcm_output + written, 0, (frame_count - written) * sizeof(float));
}
```

## 受影响文件

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `src/client/include/nevo/client/ClientCore.h` | 添加成员 | `audio_engine_started_for_monitor_` |
| `src/client/src/ClientCore.cpp` | 修改方法 | `setMonitorEnabled()` 添加引擎生命周期管理 |
| `src/core/include/nevo/core/audio/AudioEngine.h` | 添加成员 | 输出回调帧累积状态变量 |
| `src/core/src/audio/AudioEngine.cpp` | 修改方法 | `setMonitorEnabled()` 移除主线程 FIFO 排空；`maOutputCallback()` 重写帧消费逻辑；`shutdown()` 排空 monitor_fifo_ |

## 数据流（修复后）

```
用户点击 Test Input → ClientCore::setMonitorEnabled(true)
  → AudioEngine 未运行？→ initialize() → 启动设备+编码线程
  → monitor_enabled_ = true
  → maInputCallback 检查 monitor_enabled_ → push to monitor_fifo_
  → maOutputCallback 检查 monitor_enabled_ → pop from monitor_fifo_（累积式消费）
  → 混音并写入输出缓冲区 → 用户听到自己的声音
```
