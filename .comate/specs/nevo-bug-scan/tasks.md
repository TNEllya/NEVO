# NEVO 项目 BUG 修复任务计划

- [x] Task 1: 修复 ClientCore.cpp 多余闭合大括号（BUG-C1）
    - 1.1: 删除 `src/client/src/ClientCore.cpp` 第 479-480 行的多余 `}` 

- [x] Task 2: 修复 TCP 帧双重封装——发送方向（BUG-C2）
    - 2.1: 修改 `NetworkManager::sendControl()` 中的帧编码逻辑，只序列化 protobuf 裸载荷传给 `asyncSend()`

- [x] Task 3: 修复 handleTcpMessage 重新解析已剥离帧头——接收方向（BUG-C3）
    - 3.1: 修改 `TcpConnection::onMessage` 回调签名，增加 `msg_type` 和 `request_id` 参数
    - 3.2: 修改 `TcpConnection::asyncReadLoop` 在回调时传递解析出的 `message_type` 和 `request_id`
    - 3.3: 重构 `NetworkManager::handleTcpMessage` 接口，接收类型和 request_id 参数，直接反序列化 protobuf
    - 3.4: 更新 `NetworkManager` 中 `onMessage` 回调绑定

- [x] Task 4: 修复远端用户解码器从未创建（BUG-C4）
    - 4.1: 在 `AudioEngine::queueAudioData()` 中调用 `getOrCreateDecoder(user_id)`

- [x] Task 5: 修复 PTT 在 VAD 禁用时失效（BUG-C5）
    - 5.1: 在 `VoiceActivity::shouldTransmit()` 的 PTT hangover 检查后添加 PTT 守卫分支

- [x] Task 6: 修复输出音量双重衰减（BUG-C6 + BUG-M2）
    - 6.1: 移除 `AudioEngine::setOutputVolume()` 中对 `mixer_->setVolume()` 的调用
    - 6.2: 移除 `AudioEngine::initialize()` 中对 `mixer_->setVolume(config_.output_volume)` 的调用

- [x] Task 7: 修复 AudioMemoryPool::release() 数据竞争（BUG-C7）
    - 7.1: 将 `free_stack_[current_top] = block` 移到 CAS 成功之后执行

- [x] Task 8: 修复 processMixCycle() 线程不安全（BUG-H1）
    - 8.1: 在 `AudioEngine.h` 中添加 `std::mutex mix_cycle_mutex_` 成员
    - 8.2: 在 `processMixCycle()` 入口添加 `std::lock_guard<std::mutex> lock(mix_cycle_mutex_)`

- [x] Task 9: 修复输入/输出重采样器配置错误（BUG-H2）
    - 9.1: 当设备实际采样率等于 Opus 采样率时，`reset()` 重采样器而非跳过
    - 9.2: 同样处理输出重采样器

- [x] Task 10: 修复缺少 has_user_info() 检查（BUG-H3）
    - 10.1: 在 `ClientCore::handleLoginResponse()` 中添加 `resp.has_user_info()` 检查

- [x] Task 11: 修复 ClientCore::getState() 数据竞争（BUG-H4）
    - 11.1: 在 `ClientCore.h` 中添加 `mutable std::mutex state_mutex_` 成员
    - 11.2: 在 `getState()` 和所有写入点（handleLoginResponse, disconnect, handleChannelEvent 等）添加锁保护

- [x] Task 12: 修复 VoiceCrypto 密钥管理数据竞争（BUG-H5）
    - 12.1: 在 `VoiceCrypto.h` 中添加 `mutable std::mutex key_mutex_` 成员
    - 12.2: 在 `setSessionKey()` 和 `rotateKey()` 中加锁
    - 12.3: 在 `encrypt()` 和 `decrypt()` 中加锁读取密钥副本

- [x] Task 13: 修复 parseFrameHeader 未对齐内存访问（BUG-H6）
    - 13.1: 使用 `std::memcpy` 替代 `reinterpret_cast` 进行字节序转换

- [x] Task 14: 修复协程和回调中捕获 this 的 Use-After-Free（BUG-H7 + H9）
    - 14.1: 在 `onConnectAction()` 和 `onJoinChannelRequested()` 的协程中使用 `QPointer` 守卫 `invokeMethod` 回调
    - 14.2: 在 `setupClientCoreCallbacks()` 所有 `postToUiThread` 回调中添加 `QPointer` 守卫

- [x] Task 15: 修复 MainWindow 析构函数未清理回调（BUG-H8）
    - 15.1: 在析构函数中 `disconnect()` 前先清空所有 ClientCore 回调

- [x] Task 16: 修复 VoiceActivity 非原子成员数据竞争（BUG-M1）
    - 16.1: 在 `VoiceActivity.h` 中添加 `mutable std::mutex config_mutex_`
    - 16.2: 保护 `config_` 的读写和 `hangover_counter_` 的跨线程访问
