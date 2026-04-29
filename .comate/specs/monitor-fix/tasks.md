# 修复麦克风监听功能无声问题

- [x] Task 1: 修复 AudioEngine SPSC 违规 — 移除 setMonitorEnabled 中的主线程 FIFO 排空
    - 1.1: 修改 `AudioEngine::setMonitorEnabled(false)` 不再从主线程排空 `monitor_fifo_`，改为依赖输出回调自然停止消费
    - 1.2: 确保 `shutdown()` 中在回调停止后排空 `monitor_fifo_`（已有此逻辑，确认覆盖 monitor_fifo_）

- [x] Task 2: 添加输出回调帧累积消费 — 在 AudioEngine.h 中添加累积状态成员变量
    - 2.1: 添加 `output_current_frame_`、`output_frame_offset_`、`output_frame_valid_` 成员
    - 2.2: 添加 `monitor_current_frame_`、`monitor_frame_offset_`、`monitor_frame_valid_` 成员

- [x] Task 3: 重写 maOutputCallback 帧消费逻辑 — 使用累积式消费替代单帧弹出
    - 3.1: 用 while 循环逐采样填充输出缓冲区，从当前帧偏移位置读取数据
    - 3.2: 帧耗尽时从 FIFO 弹出新帧，FIFO 空则退出循环，剩余填充静音
    - 3.3: 同时处理 output_fifo_ 和 monitor_fifo_ 的累积消费

- [x] Task 4: ClientCore 添加引擎生命周期管理 — 确保监控时 AudioEngine 运行
    - 4.1: 在 `ClientCore.h` 中添加 `audio_engine_started_for_monitor_` 成员
    - 4.2: 修改 `ClientCore::setMonitorEnabled(true)` — 若引擎未运行则初始化
    - 4.3: 修改 `ClientCore::setMonitorEnabled(false)` — 若引擎仅因监控而启动且用户未连接则关闭
    - 4.4: 修改 `ClientCore::initAudioSubsystem()` — 重置 `audio_engine_started_for_monitor_` 标志

- [x] Task 5: 构建验证
    - 5.1: 执行完整构建，确保无编译错误和链接错误
