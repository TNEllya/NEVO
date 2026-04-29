# NEVO 项目 BUG 扫描与修复 — 完成总结

## 执行概况

共识别 **18 个 BUG**（7 Critical + 9 High + 2 Medium），规划为 **16 个修复任务**，全部完成。

## 修复清单

### Critical 级别（7 项）

| BUG | 问题 | 修复文件 | 修复内容 |
|-----|------|----------|----------|
| C1 | ClientCore.cpp 多余闭合大括号导致编译失败 | `src/client/src/ClientCore.cpp` | 删除 line 479-480 的多余 `}` |
| C2 | TCP 帧双重封装（发送方向） | `src/client/src/NetworkManager.cpp` | `sendControl()` 只序列化裸 Protobuf 载荷，由 `TcpConnection::asyncSend()` 负责帧头 |
| C3 | handleTcpMessage 重新解析已剥离帧头（接收方向） | `TcpConnection.h/cpp`, `NetworkManager.h/cpp`, `ClientSession.h/cpp` | `onMessage` 回调增加 `msg_type`/`request_id` 参数；`handleTcpMessage` 直接反序列化 |
| C4 | 远端用户解码器从未创建 | `src/core/src/audio/AudioEngine.cpp` | `queueAudioData()` 中调用 `getOrCreateDecoder(user_id)` |
| C5 | PTT 在 VAD 禁用时失效 | `src/core/src/audio/VoiceActivity.cpp` | PTT hangover 检查后添加 PTT 守卫分支 |
| C6 | 输出音量双重衰减 | `src/core/src/audio/AudioEngine.cpp` | 移除 `setOutputVolume()` 中 `mixer_->setVolume()` 调用和 `initialize()` 中的初始设置 |
| C7 | AudioMemoryPool::release() 数据竞争 | `src/core/src/audio/AudioMemoryPool.cpp` | CAS 先保留槽位，成功后再写入 `free_stack_` |

### High 级别（9 项）

| BUG | 问题 | 修复文件 | 修复内容 |
|-----|------|----------|----------|
| H1 | processMixCycle() 线程不安全 | `AudioEngine.h/cpp` | 添加 `mix_cycle_mutex_`，`processMixCycle()` 入口加锁 |
| H2 | 输入/输出重采样器配置错误 | `src/core/src/audio/AudioEngine.cpp` | 设备实际采样率匹配 Opus 时 `reset()` 重采样器 |
| H3 | 缺少 has_user_info() 检查 | `src/client/src/ClientCore.cpp` | 添加 `resp.has_user_info()` 检查 |
| H4 | ClientCore::getState() 数据竞争 | `src/client/src/ClientCore.cpp` | `getState()` 和所有状态写入点添加 `state_mutex_` 锁保护 |
| H5 | VoiceCrypto 密钥管理数据竞争 | `VoiceCrypto.h/cpp` | 添加 `key_mutex_`，`encrypt/decrypt` 中拷贝密钥副本，`setSessionKey/rotateKey` 加锁 |
| H6 | parseFrameHeader 未对齐内存访问 | `src/network/src/TcpConnection.cpp` | 使用 `std::memcpy` 替代 `reinterpret_cast` |
| H7 | 协程中捕获 this 导致 UAF | `src/ui/src/MainWindow.cpp` | 协程 `invokeMethod` 回调使用 `QPointer` 守卫 |
| H8 | MainWindow 析构函数未清理回调 | `src/ui/src/MainWindow.cpp` | 析构时先清空所有 ClientCore 回调再 disconnect |
| H9 | postToUiThread 回调捕获 this 无生命周期保证 | `src/ui/src/MainWindow.cpp` | 所有回调改用 `QPointer<MainWindow>` 守卫 |

### Medium 级别（2 项，合并到 Task 6 和 Task 16）

| BUG | 问题 | 修复文件 | 修复内容 |
|-----|------|----------|----------|
| M1 | VoiceActivity 非原子成员数据竞争 | `VoiceActivity.h/cpp` | 添加 `config_mutex_`，保护 `config_` 和 `hangover_counter_` |
| M2 | initialize() 中 mixer 初始音量设置 | `AudioEngine.cpp` | 与 C6 一并修复，移除 `mixer_->setVolume()` |

## 修改文件汇总

| 文件路径 | 修改类型 |
|----------|----------|
| `src/client/src/ClientCore.cpp` | 删除多余大括号、添加 state_mutex_ 锁、添加 has_user_info 检查 |
| `src/client/include/nevo/client/NetworkManager.h` | handleTcpMessage 签名变更 |
| `src/client/src/NetworkManager.cpp` | 修复双重封装、重构 handleTcpMessage、更新 onMessage 回调 |
| `src/server/include/nevo/server/ClientSession.h` | handleControlMessage 签名变更 |
| `src/server/src/ClientSession.cpp` | 更新 onMessage 回调和 handleControlMessage |
| `src/network/include/nevo/network/TcpConnection.h` | onMessage 回调签名变更（增加 msg_type, request_id） |
| `src/network/src/TcpConnection.cpp` | asyncReadLoop 传递 msg_type/request_id、修复未对齐访问 |
| `src/network/include/nevo/network/VoiceCrypto.h` | 添加 key_mutex_ |
| `src/network/src/VoiceCrypto.cpp` | encrypt/decrypt 中拷贝密钥副本、setSessionKey/rotateKey 加锁 |
| `src/core/src/audio/AudioEngine.cpp` | 添加 getOrCreateDecoder 调用、移除双重音量、重采样器 reset、mix_cycle_mutex_ |
| `src/core/include/nevo/core/audio/AudioEngine.h` | 添加 mix_cycle_mutex_ |
| `src/core/src/audio/AudioMemoryPool.cpp` | 修复 release() CAS 前写入问题 |
| `src/core/src/audio/VoiceActivity.cpp` | 添加 PTT 守卫、config_mutex_ 保护所有读写 |
| `src/core/include/nevo/core/audio/VoiceActivity.h` | 添加 config_mutex_、#include mutex |

## 后续迭代建议

### P0 — 核心功能完善
1. **统一帧编解码层**：将 `PacketCodec` 的 `encodeTcpFrame`/`decodeTcpFrame` 与 `TcpConnection` 的帧头处理合并为单一入口，从架构层面消除双重封装的可能
2. **UDP 心跳与真实延迟测量**：实现 UDP ping 替换当前 `onLatencyUpdate(-1)` 占位

### P1 — 线程安全与稳定性
3. **线程安全文档化**：为所有跨线程访问的类添加 Thread Safety 注释（如 `@threadsafe` 标签），标明各成员的访问线程和同步策略
4. **RAII 设备管理**：`AudioEngine::initialize()` 中 `ma_device` 的错误路径存在泄漏风险，应使用自定义 deleter 的 `unique_ptr`
5. **单元测试**：为 VoiceCrypto、JitterBuffer、AudioMixer 等核心模块添加单元测试

### P2 — 功能扩展
6. **TURN 中继实现**：完成当前 stub 的 TURN relay，支持对称 NAT 穿越
7. **配置持久化**：客户端/服务器设置保存到本地文件（JSON/TOML）
8. **服务器配置文件**：支持从配置文件加载服务器参数（端口、密钥、频道等）

### P3 — 工程化
9. **CI/CD 流水线**：添加 GitHub Actions 自动构建和测试
10. **CMake 现代化**：升级到 CMake presets，添加 Conan/vcpkg 包管理
11. **Docker 支持**：提供服务器端 Dockerfile 和 docker-compose.yml
