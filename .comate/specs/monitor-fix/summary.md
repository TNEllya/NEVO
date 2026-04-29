# 修复麦克风监听功能无声问题 - 总结

## 修复的三个根因

### 根因 #1：AudioEngine 未运行时无法监听
AudioEngine::initialize() 仅在用户连接服务器时调用。未连接状态下点击"Test Input"，回调不触发，monitor_fifo_ 无数据流，自然无声。

**修复**：ClientCore::setMonitorEnabled(true) 中检查引擎是否运行，未运行则初始化；setMonitorEnabled(false) 中若引擎仅因监听启动且用户未连接则关闭引擎。添加 `audio_engine_started_for_monitor_` 标志追踪引擎启动原因。initAudioSubsystem() 中重置此标志。

### 根因 #2：SPSC 队列约束违规
setMonitorEnabled(false) 从主线程排空 monitor_fifo_，而 maOutputCallback 从实时线程弹出。两者同时消费违反 SPSC 约束，导致未定义行为。

**修复**：移除 setMonitorEnabled 中的 FIFO 排空逻辑，改为依赖输出回调自然停止消费。shutdown() 中在回调停止后排空（已有此逻辑，安全）。

### 根因 #3：输出回调帧消费不匹配
原逻辑每次回调只弹出一帧（960采样），但设备请求的 frame_count 可能不同。frame_count < 960 时帧后半部分浪费导致 FIFO 消耗过快，frame_count > 960 时输出不完整。

**修复**：重写 maOutputCallback 为累积式帧消费，维护 output_current_frame_/monitor_current_frame_ 及偏移量，跨回调调用保持帧状态，逐采样填充输出缓冲区。

## 修改文件

| 文件 | 修改 |
|------|------|
| `AudioEngine.h` | 添加 6 个帧累积状态成员变量（output/monitor 的 current_frame、offset、valid） |
| `AudioEngine.cpp` | setMonitorEnabled() 移除主线程 FIFO 排空；maOutputCallback() 重写为累积式消费；shutdown() 重置累积状态 |
| `ClientCore.h` | 添加 `audio_engine_started_for_monitor_` 成员 |
| `ClientCore.cpp` | setMonitorEnabled() 添加引擎生命周期管理；initAudioSubsystem() 重置标志 |

## 构建结果

完整构建成功，无编译错误。
