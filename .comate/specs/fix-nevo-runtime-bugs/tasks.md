# NEVO 运行时 Bug 修复任务计划

- [x] Task 1: 修复 TcpConnection 异步连接超时逻辑
    - 1.1: 分析现有 broken 实现：确认 `asyncConnect` 中启动 `async_connect` 和 `timer.async_wait` 后未等待、立即 `timer.cancel()` 的问题
    - 1.2: 重写 `asyncConnect`：改用 `boost::asio::cancel_after` 模式同时等待连接完成和超时
    - 1.3: 验证连接成功路径和超时路径均正确设置 `connected_` 和返回错误码

- [x] Task 2: 修复 JitterBuffer 线程安全数据竞争
    - 2.1: 在 `JitterBuffer.h` 中为 `user_packets_` 和 `current_packets_` 添加 `std::mutex`
    - 2.2: 在 `push()` 中对共享数据结构加锁
    - 2.3: 在 `pop()` 中对共享数据结构加锁
    - 2.4: 在 `removeUser()` 和 `reset()` 中加锁保护

- [x] Task 3: 修复 ClientCore 登录假成功问题
    - 3.1: 在 `ClientCore.h` 中添加 `std::promise<bool>` / `std::future<bool>` 或条件变量机制用于等待 `LoginResponse`
    - 3.2: 修改 `connect()` 协程：发送 `LoginRequest` 后等待响应，超时则断开并返回错误
    - 3.3: 修改 `handleLoginResponse()`：解析响应结果后设置 promise 值并通知等待方
    - 3.4: 处理登录失败场景：服务端拒绝时正确切换回 `Disconnected` 状态

- [x] Task 4: 修复 ConnectionManager 心跳定时器重复注册
    - 4.1: 分析 `startHeartbeatCheck()` 和 `checkHeartbeatTimeout()` 的定时器链交互
    - 4.2: 移除 `startHeartbeatCheck()` 中空 lambda 的冗余 `async_wait` 注册
    - 4.3: 确保只有 `checkHeartbeatTimeout()` 负责重新调度下一次心跳检查
    - 4.4: 验证断连后定时器正确取消，无残留回调

- [x] Task 5: 修复 Server CLI 参数解析异常崩溃
    - 5.1: 在 `src/server/src/main.cpp` 的 `parseArgs()` 中为所有 `std::stoi` 调用添加 `try/catch (std::invalid_argument/std::out_of_range)`
    - 5.2: 异常时输出友好错误信息并返回 `false`，使 `main()` 优雅退出

- [x] Task 6: 修复 SslWrapper cipher list 语法错误
    - 6.1: 将 `src/network/src/SslWrapper.cpp` 中的 `ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS` 修正为 OpenSSL 标准语法（使用 `-` 连接密钥交换、认证、加密、MAC 组件）
    - 6.2: 使用广泛兼容的标准 cipher list 字符串，如 `HIGH:!aNULL:!MD5:!DSS`

- [x] Task 7: 修复 AudioRelay 统计错误
    - 7.1: 将 `packets_relayed_++` 移动到 `!peers.empty()` 的分支内部，确保空列表时不计数

- [x] Task 8: 修复 MainWindow standalone 构造函数 nullptr 解引用
    - 8.1: 修改 `MainWindow` standalone 构造函数中对 `io_ctx_` 的初始化方式，避免解引用 `nullptr`
    - 8.2: 将 `io_ctx_` 从引用改为指针，standalone 路径初始化为 `nullptr`

