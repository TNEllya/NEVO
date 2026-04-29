# 修复编码管线：为 AudioEngine 添加编码线程

- [x] Task 1: 在 AudioEngine.h 中添加编码线程相关的成员和方法声明
    - 1.1: 添加 `#include <thread>` 和 `#include <stop_token>` 头文件
    - 1.2: 添加私有方法声明 `void encodeThreadFunc(std::stop_token stop_token)`
    - 1.3: 添加成员变量 `std::jthread encode_thread_`

- [x] Task 2: 在 AudioEngine.cpp 中实现 encodeThreadFunc()
    - 2.1: 实现编码线程主循环：循环调用 `processEncodeCycle()`
    - 2.2: Boost 路径：`input_fifo_.empty()` 时 `sleep_for(1ms)` 避免忙等
    - 2.3: 非 Boost 路径：检查 mutex 保护的 FIFO 是否为空，空时 `sleep_for(1ms)`

- [x] Task 3: 在 initialize() 中启动编码线程
    - 3.1: 在 `running_.store(true)` 之前创建并启动 `std::jthread`
    - 3.2: 检查线程是否可 join，失败时回滚（停止已启动的设备）并返回错误

- [x] Task 4: 在 shutdown() 中停止编码线程
    - 4.1: 在 `running_.store(false)` 之后调用 `request_stop()` 和 `join()`
    - 4.2: 确保编码线程在 miniaudio 设备停止之前退出

- [x] Task 5: 构建验证
    - 5.1: 执行完整构建，确保无编译错误和链接错误
