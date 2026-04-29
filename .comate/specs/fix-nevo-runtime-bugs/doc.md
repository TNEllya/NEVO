# NEVO 运行时 Bug 修复与功能完善文档

## 1. 需求场景

NEVO 项目编译成功（`nevo_server.exe` 和 `nevo_client_ui.exe` 均已生成），但代码审查和运行时分析发现多处严重/高危 bug。这些 bug 会导致连接超时失效、数据竞争崩溃、假登录成功、服务端 CLI 崩溃、TLS 握手失败等问题。本需求基于现有代码分析和项目设计文档，修复这些运行时 bug，确保核心功能的正确性和稳定性。

## 2. 项目架构概述

NEVO 是 C++20 跨平台 VoIP 系统，模块依赖关系：
```
nevo_client_ui → nevo_client → nevo_network → nevo_core
nevo_server   ──────────────→ nevo_network → nevo_core
```

## 3. Bug 清单与修复方案

### Bug A: TcpConnection::asyncConnect 超时逻辑 Broken [Critical]

**位置**: `src/network/src/TcpConnection.cpp:46-121`

**问题描述**:
当前实现启动 `async_connect` 和 `timer.async_wait` 后，**立即**调用 `timer.cancel()`（第101行），未通过协程 `co_await` 等待任一操作完成。`connect_ec` 和 `timer_expired` 在 lambda 回调中修改，但主代码在回调执行前就读取了它们的值。结果：
- `timer_expired` 永远为 `false`
- `connect_ec` 永远是 `boost::asio::error::would_block`
- 函数永远报告"成功"，即使连接从未完成

**修复方案**:
重写 `asyncConnect`，使用 `boost::asio::experimental::make_parallel_group` 或 `select` 模式，让协程真正等待 `async_connect` 和 `timer.async_wait` 两者之一先完成。当定时器先完成时返回 `timed_out`；当连接先完成时取消定时器并返回连接结果。

**代码变更**:
```cpp
// 方案：使用 as_tuple + 协程 select
auto [connect_ec, timer_ec] = co_await /* 并行等待两者 */;
```

**边界条件**:
- DNS 解析失败：保持现有处理（已正确）
- 超时值为 0：应直接返回 `timed_out`
- 连接完成时定时器已过期：需要检查 `connect_ec` 是否为 `operation_aborted`（被定时器关闭 socket 导致）

---

### Bug B: JitterBuffer 数据竞争 [Critical]

**位置**: `src/core/src/audio/JitterBuffer.cpp:163-213 (push/pop)`

**问题描述**:
`JitterBuffer::push()` 由网络线程调用（收到语音包时），`JitterBuffer::pop()` 由音频线程调用（`AudioEngine::processMixCycle()`）。两者同时修改 `user_packets_`（`std::unordered_map<UserId, std::dequedeque<RawPacket>>`）和 `current_packets_`，没有任何同步机制。这会导致：
- `std::unordered_map` 并发插入/查找 → 崩溃或内存损坏
- `std::deque` 并发 push_back/pop_front → 未定义行为

**修复方案**:
在 `JitterBuffer` 中添加 `std::mutex mutex_`，在 `push()`、`pop()`、`removeUser()`、`reset()` 中对 `user_packets_` 和 `current_packets_` 的操作加锁。`insert()` 和 `getNext()` 操作的是独立的 `buffer_`（PCM 解码后数据），根据调用上下文判断是否也需要保护。

**边界条件**:
- 高频音频线程不应被网络线程阻塞太久：锁粒度要小，只保护数据结构操作
- `removeUser()` 与 `pop()` 并发：需要同一把锁保护

---

### Bug C: ClientCore::connect 假登录成功 [High]

**位置**: `src/client/src/ClientCore.cpp:126-136`

**问题描述**:
代码注释明确说明："简化方案：登录请求发送成功即视为登录成功"。发送 `LoginRequest` 后不等 `LoginResponse`，直接将状态设为 `Connected`。这导致：
- 服务端拒绝凭证时，客户端仍显示"已连接"
- UDP 通道在认证前就被建立，浪费资源
- `local_username_` 在认证成功前就被设置

**修复方案**:
使用 `std::promise<Result<void>>` / `std::future` 或 `boost::asio::steady_timer` + `std::atomic<bool>` 机制，在 `connect()` 协程中等待 `handleLoginResponse()` 完成。如果在超时时间内收到 `LoginResponse` 且 `success=true`，则继续后续步骤（建立 UDP、初始化音频）；否则返回认证失败错误。

**代码变更**:
```cpp
// 在 ClientCore 中添加等待机制
std::promise<Result<void>> login_promise_;
std::mutex login_mutex_;
std::optional<std::promise<Result<void>>> pending_login_;
```

**边界条件**:
- 服务端永远不响应：需要设置超时（如 10 秒）
- 连接断开发生在等待期间：`handleDisconnected()` 需要唤醒等待中的协程
- 重复登录请求：清理上一次的 promise

---

### Bug D: ConnectionManager 心跳定时器重复注册 [High]

**位置**: `src/network/src/ConnectionManager.cpp:140-181` 和 `237-298`

**问题描述**:
`startHeartbeatCheck` 启动一个定时器，其完成 lambda 中：
1. 调用 `checkHeartbeatTimeout()`
2. 然后注册一个**空**的 `async_wait` lambda（不做任何事）

同时 `checkHeartbeatTimeout()` 内部又会重新调度自己。这导致两条并发的定时器链：
- 空 lambda 链（浪费 CPU）
- `checkHeartbeatTimeout` 链（实际工作）

**修复方案**:
移除 `startHeartbeatCheck` 中空 lambda 的二次注册。让 `checkHeartbeatTimeout()` 成为唯一的定时器重调度点。简化 `startHeartbeatCheck` 为只调用一次 `checkHeartbeatTimeout()`。

---

### Bug E: Server CLI std::stoi 无异常处理 [Medium]

**位置**: `src/server/src/main.cpp:88-112`

**问题描述**:
`parseArgs` 中使用 `std::stoi(argv[++i])` 解析 `--tcp-port`、`--udp-port`、`--threads`，没有任何 `try/catch`。传入非数字参数时抛出 `std::invalid_argument`，导致整个服务端进程崩溃。

**修复方案**:
将 `std::stoi` 调用包裹在 `try/catch` 中，捕获 `std::invalid_argument` 和 `std::out_of_range`，输出友好的错误信息后返回错误码。

---

### Bug F: SSL Cipher List 语法错误 [Medium]

**位置**: `src/network/src/SslWrapper.cpp:187-190`

**问题描述**:
`SSL_CTX_set_cipher_list` 调用字符串为 `"ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS"`。OpenSSL cipher list 使用 `-` 作为算法与模式的分隔符（如 `ECDHE-RSA-AES256-GCM-SHA384`），`+` 不是合法语法。这会导致 OpenSSL 拒绝整个 cipher list，TLS 握手可能失败。

**修复方案**:
将 cipher list 字符串修正为 OpenSSL 标准格式。使用现代、广泛支持的 cipher 组合，例如：
```
"ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-AES256-GCM-SHA384:!aNULL:!MD5:!DSS"
```

---

### Bug G: AudioRelay 统计错误 [Low]

**位置**: `src/server/src/AudioRelay.cpp:94-98`

**问题描述**:
当 `peers.empty()`（频道中无其他用户需要转发）时，代码仍然递增 `packets_relayed_`，导致统计数据虚高。

**修复方案**:
将 `++packets_relayed_` 移到 `peers.empty()` 检查之后，或改为 `if (!peers.empty()) { ... ++packets_relayed_; }`。

---

### Bug H: MainWindow Standalone 构造函数 Nullptr 解引用 [Low]

**位置**: `src/ui/src/MainWindow.cpp:120`

**问题描述**:
在 `NEVO_HAS_BOOST` 路径下，standalone 构造函数使用 `*static_cast_cast<boost::asio::io_context*>(nullptr)` 初始化 `io_ctx_` 引用成员。解引用 `nullptr` 是未定义行为。

**修复方案**:
将 `io_ctx_` 从引用改为指针（`boost::asio::io_context* io_ctx_`），或在 standalone 模式下使用 `std::optional<std::reference_wrapper_wrapper<boost::asio::io_context>>`。更简单的方案是延迟初始化，只在 `external_io` 不为 nullptr 时才初始化 `io_ctx_`，否则创建一个内部 `io_context` 实例。

## 4. 受影响文件

| 文件 | 修改类型 |
|------|----------|
| `src/network/src/TcpConnection.cpp` | 重写 asyncConnect 协程超时逻辑 |
| `src/core/include/nevo/core/audio/JitterBuffer.h` | 添加 mutex 成员 |
| `src/core/src/audio/JitterBuffer.cpp` | push/pop/removeUser/reset 加锁 |
| `src/client/include/nevo/client/ClientCore.h` | 添加登录等待机制相关成员 |
| `src/client/src/ClientCore.cpp` | 修复假登录成功，添加异步等待 |
| `src/network/src/ConnectionManager.cpp` | 清理重复定时器注册 |
| `src/server/src/main.cpp` | std::stoi 异常处理 |
| `src/network/src/SslWrapper.cpp` | 修正 cipher list 语法 |
| `src/server/src/AudioRelay.cpp` | 修正统计计数位置 |
| `src/ui/src/MainWindow.cpp` | 修正 standalone 构造函数 |

## 5. 边界条件与异常处理

- **并发安全**: JitterBuffer 的锁只在必要时持有，避免音频线程卡顿
- **协程取消**: TcpConnection 超时重写需正确处理协程取消和 socket 关闭的竞态
- **向后兼容**: 所有修复保持原有接口不变，只修改内部实现
- **错误码传播**: ClientCore 登录等待超时需返回明确的 `ResultCode::Timeout`

## 6. 预期结果

- `TcpConnection::asyncConnect` 正确实现超时，超时后返回 `timed_out`
- `JitterBuffer` 在网络线程和音频线程并发访问下不再数据竞争
- `ClientCore::connect` 只有在收到服务端 `LoginResponse` 成功后才返回 Connected
- `ConnectionManager` 心跳检测只有一条定时器链
- 服务端 CLI 传入非法数字参数时优雅退出并打印帮助信息
- TLS 握手使用正确的 cipher list
- 语音转发统计只在实际转发时递增
- `MainWindow` standalone 模式不再解引用 nullptr
