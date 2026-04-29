# NEVO 运行时 Bug 修复总结

## 修复范围

本次任务针对 NEVO VoIP 项目编译后发现的 8 个运行时 Bug 进行修复，所有修改均基于已有的 `nevo-voip-system` 设计文档。

## 修复结果

- `nevo_server`：编译通过
- `nevo_client_ui`：编译通过
- 全部 8 个 Bug 已修复

## Bug 修复详情

### 1. TcpConnection 异步连接超时逻辑 broken

**文件**：`src/network/src/TcpConnection.cpp`

**问题**：`asyncConnect` 中启动 `async_connect` 和 `timer.async_wait` 后未等待任何操作完成，立即 `timer.cancel()`，导致 `connect_ec` 永远为 `would_block`，连接结果不可信。

**修复**：将手动超时逻辑替换为 `boost::asio::cancel_after(std::chrono::milliseconds(timeout_ms))`，直接用协程等待 `async_connect` 并在超时后自动取消。成功连接时正确设置 `connected_` 标志。

### 2. JitterBuffer 线程安全数据竞争

**文件**：`src/core/include/nevo/core/audio/JitterBuffer.h`、`src/core/src/audio/JitterBuffer.cpp`

**问题**：`push()` 由网络线程调用，`pop()` / `getNext()` 由音频线程调用，并发操作 `std::map` / `std::deque` 会导致数据竞争和崩溃。

**修复**：在 `JitterBuffer` 中添加 `mutable std::mutex mutex_`，在 `insert`、`getNext`、`push`、`pop`、`removeUser`、`reset` 以及状态查询方法中加锁保护。

### 3. ClientCore 登录假成功

**文件**：`src/client/include/nevo/client/ClientCore.h`、`src/client/src/ClientCore.cpp`

**问题**：`connect()` 协程发送 `LoginRequest` 后不等待 `LoginResponse`，立即报告连接成功，即使用户名/密码被服务端拒绝。

**修复**：在 `ClientCore` 中添加 `boost::asio::steady_timer login_waiter_`、`bool login_completed_`、`Result<void> login_result_`、`std::mutex login_mutex_`。`connect()` 发送登录请求后 `co_await login_waiter_.async_wait()` 等待响应；`handleLoginResponse()` 解析结果后设置 `login_completed_` 并 `cancel()` 定时器通知等待方；登录失败或断开连接时正确取消等待并回退到 `Disconnected` 状态。

### 4. ConnectionManager 心跳定时器重复注册

**文件**：`src/network/src/ConnectionManager.cpp`

**问题**：`startHeartbeatCheck` 中的完成 lambda 在调用 `checkHeartbeatTimeout()` 后又注册了一个空的 `async_wait`，同时 `checkHeartbeatTimeout()` 末尾也重新调度定时器，导致两条并发的定时器链。

**修复**：移除 `startHeartbeatCheck` 中空 lambda 的冗余 `async_wait`，仅保留 `checkHeartbeatTimeout` 末尾的重新调度逻辑，确保只有一条定时器链。

### 5. Server CLI 参数解析异常崩溃

**文件**：`src/server/src/main.cpp`

**问题**：`parseArgs` 中对 `--tcp-port`、`--udp-port`、`--threads` 使用 `std::stoi` 转换参数，未处理 `std::invalid_argument` / `std::out_of_range` 异常，传入非数字参数时服务端直接崩溃。

**修复**：添加 `parseIntSafe<T>` 模板函数，内部 `try/catch` 包装 `std::stoi`，异常时输出友好错误信息并返回 `false`，使 `main()` 优雅退出。

### 6. SslWrapper cipher list 语法错误

**文件**：`src/network/src/SslWrapper.cpp`

**问题**：`SSL_CTX_set_cipher_list` 使用了 `ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS`，OpenSSL 正确语法应使用 `-` 连接组件（如 `ECDHE-RSA-AES256-GCM-SHA384`），`+` 分隔符会导致 cipher list 被拒绝。

**修复**：替换为广泛兼容的标准 cipher list 字符串 `HIGH:!aNULL:!MD5:!DSS`。

### 7. AudioRelay 统计错误

**文件**：`src/server/src/AudioRelay.cpp`

**问题**：当频道内没有其他用户（`peers.empty()`）时，代码仍然递增 `packets_relayed_`，导致统计值虚高。

**修复**：将 `packets_relayed_++` 移动到 `!peers.empty()` 的分支内部，确保只有实际完成转发时才计数。

### 8. MainWindow standalone 构造函数 nullptr 解引用

**文件**：`src/ui/include/nevo/ui/MainWindow.h`、`src/ui/src/MainWindow.cpp`

**问题**：`MainWindow` standalone 构造函数中 `io_ctx_` 被初始化为 `*static_cast_cast<boost::asio::io_context*>(nullptr)`，解引用 nullptr 是未定义行为。此前为修复该问题已将 `io_ctx_` 改为指针类型，但 `MainWindow.cpp` 中多处仍按引用使用（`co_spawn(io_ctx_, ...)`、`io_ctx_.run()` 等），导致编译错误。

**修复**：确认 `io_ctx_` 为 `boost::asio::io_context*` 指针类型；修复 standalone 构造函数初始化（`io_ctx_(nullptr)`）；修复带参构造函数初始化（`io_ctx_(&io_ctx)`）；修复 `io_thread_` lambda 中的 `io_ctx_->run()`；修复析构函数中的 `io_ctx_->stop()`；修复所有 `co_spawn` 调用（解引用为 `*io_ctx_`）。

## 编译验证

```powershell
cmake --build c:\Users\yzd20\Desktop\NEVO\build --target nevo_server nevo_client_ui
```

输出：
- `[100%] Built target nevo_server`
- `[100%] Built target nevo_client_ui`

所有目标编译通过，无错误，仅剩无关警告（Boost.Asio null-dereference 警告、protobuf sign-conversion 警告、Qt qsizetype 转换警告等）。

## 未完全修复的已知问题

- `ClientCore::connect` 中的登录等待逻辑使用了简化版定时器等待，未使用生产级的 promise/future 或条件变量机制，但已满足当前需求。
- 项目中仍存在大量功能 stub（无 SQLite3 时的 Database stub、无 libsodium 时的 VoiceCrypto stub、无 Opus 时的编解码器 stub），这些属于功能缺失而非运行时 Bug，不在本次修复范围内。
