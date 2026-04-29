# NEVO 项目进一步开发任务计划

- [x] Task 1: 修复 Logger 初始化——添加 console/file sink（DEV-1）
    - 1.1: 读取并分析 `Logger.h` 和 `Logger.cpp` 当前实现
    - 1.2: 在 `initialize()` 中添加 console sink（stdout colored）和可选 file sink
    - 1.3: 添加 `setLogFile(path)` 方法
    - 1.4: 设置默认日志格式 `[时间] [级别] [分类] 消息`
    - 1.5: 修复 CMakeLists.txt 中 spdlog 的链接方式，确保使用真实库而非 stub

- [x] Task 2: 创建项目 README.md（DEV-2）
    - 2.1: 编写项目简介、特性列表、技术栈
    - 2.2: 编写构建说明（依赖、CMake 命令、条件编译选项）
    - 2.3: 编写运行说明（客户端/服务器启动参数）
    - 2.4: 编写项目目录结构说明

- [x] Task 3: 关键模块补充日志（DEV-3）
    - 3.1: `PacketCodec.cpp` — 序列化/反序列化错误添加 ERROR 日志
    - 3.2: `TcpVoiceTunnel` — 重组失败、帧解析错误添加 WARN 日志
    - 3.3: `UdpSocket` — 绑定/发送/接收关键路径添加 INFO/DEBUG 日志
    - 3.4: `Resampler` — 初始化、重采样失败添加 INFO/ERROR 日志
    - 3.5: `AudioMemoryPool` — 池耗尽、溢出添加 ERROR 日志
    - 3.6: `AudioMixer` — 硬限幅、输入溢出添加 DEBUG 日志

- [x] Task 4: Result<T> 安全性改进（DEV-4）
    - 4.1: 在 `value()` 中添加 `assert(has_value())` 断言
    - 4.2: 修复 `value_or()` 中不必要的 `std::move(default_val)`
    - 4.3: 添加 `value_or()` 的 const 重载版本

- [x] Task 5: 客户端设置持久化（DEV-5）
    - 5.1: 在 MainWindow 中添加 `QSettings` 成员和 `saveSettings()`/`loadSettings()` 方法
    - 5.2: 保存/恢复：服务器地址、端口、用户名、输入/输出音量
    - 5.3: 保存/恢复：VAD/PTT 模式选择
    - 5.4: 在连接成功和设置变更时触发保存

- [x] Task 6: 服务器配置文件支持（DEV-6）
    - 6.1: 创建 `ServerConfig` 类，支持从 JSON 文件加载配置
    - 6.2: 配置项：tcp_port, udp_port, db_path, threads, log_level, server_name, max_users, welcome_message
    - 6.3: 实现优先级合并：命令行参数 > 配置文件 > 默认值
    - 6.4: 在 `main.cpp` 中集成配置加载逻辑
    - 6.5: 创建 `server_config.example.json` 示例文件

- [x] Task 7: CMake Install 目标与 CPack 打包（DEV-7）
    - 7.1: 为库目标添加 `install(TARGETS ...)` 和 `install(FILES ...)` 头文件安装
    - 7.2: 添加 CPack 配置（版本号、包格式 DEB/RPM/ZIP）
    - 7.3: 添加 CMake 版本号变量到项目配置

- [x] Task 8: Dockerfile 服务器容器化（DEV-8）
    - 8.1: 创建多阶段构建 Dockerfile（build → runtime）
    - 8.2: 创建 `docker-compose.yml` 定义服务器服务
    - 8.3: 暴露 TCP/UDP 端口，挂载配置和数据库目录
