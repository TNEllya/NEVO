# 修复编码管线：为 AudioEngine 添加编码线程 - 总结

## 问题根因

`AudioEngine::processEncodeCycle()` 已完整实现（input_fifo → Opus 编码 → encoded_callback → 网络发送），但**从未被任何代码调用**。整个编码管线是死代码，导致：
- `input_fifo_` 持续接收麦克风数据但无人消费
- 约 640ms 后 FIFO 满溢，所有后续帧静默丢弃
- Opus 编码器从未处理任何数据，远端用户永远听不到本地麦克风声音

## 修复内容

### 1. AudioEngine.h (`src/core/include/nevo/core/audio/AudioEngine.h`)
- 添加 `#include <thread>` 和 `#include <stop_token>` 头文件
- 添加私有方法声明 `void encodeThreadFunc(std::stop_token stop_token)`
- 添加成员变量 `std::jthread encode_thread_`

### 2. AudioEngine.cpp (`src/core/src/audio/AudioEngine.cpp`)
- **新增 `encodeThreadFunc()`**：编码线程主循环，持续调用 `processEncodeCycle()`；当 `input_fifo_` 为空时 `sleep_for(1ms)` 避免忙等；退出前处理残留帧
- **修改 `initialize()`**：在 `running_.store(true)` 之后启动 `std::jthread`，使用 lambda 包装调用以解决 `jthread` 自动插入 `stop_token` 到参数列表最前面与成员函数指针 `this` 位置的冲突；启动失败时回滚（停止设备、重置 running 标志）
- **修改 `shutdown()`**：在 `running_.store(false)` 之后、停止 miniaudio 设备之前调用 `request_stop()` + `join()` 确保编码线程安全退出

### 设计决策
- **不用条件变量**：实时回调中不能调用 `notify_one()`（系统调用违反实时安全约束）
- **1ms sleep_for**：FIFO 无数据时降低 CPU 占用；有数据时 `processEncodeCycle()` 立即处理所有可用帧，延迟 < 1ms
- **`std::jthread`**：C++20 原生支持 cooperative cancellation，析构自动 join
- **编码线程在设备之前停止**：避免回调访问已释放资源

## 构建结果

完整构建成功，exit code 0，无编译错误。
