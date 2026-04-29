# NEVO 项目进一步开发建议与实施规格

## 概述

基于全面代码扫描，识别出 **8 个高优先级开发项**，分为基础设施、功能完善、工程化三个层次。以下按优先级排列，所有建议均附带具体实现方案。

---

## P0 — 基础设施（阻塞性问题）

### DEV-1: Logger 初始化——日志系统完全无效

- **问题**: `LoggerManager::initialize()` 创建 logger 对象但 **从未添加任何 sink**（无 console sink、无 file sink），所有日志输出为空操作。整个项目的 `NEVO_LOG_*` 调用全部静默。
- **严重程度**: Critical（调试和运行时诊断完全不可用）
- **文件**: `src/core/include/nevo/core/common/Logger.h`, `src/core/src/common/Logger.cpp`

**修复方案**:
1. 在 `initialize()` 中添加 console sink（stdout colored）和可选 file sink
2. 添加 `setLogFile(path)` 方法支持日志文件输出
3. 设置默认日志格式：`[YYYY-MM-DD HH:MM:SS.mmm] [level] [category] message`
4. 确保 spdlog 真实库被链接（当前 CMakeLists.txt 创建的是 stub）

---

### DEV-2: 项目 README.md 缺失

- **问题**: 项目无 README、无 LICENSE、无贡献指南，对开发者极不友好
- **文件**: 项目根目录 `README.md`

**实现方案**:
创建完整的 README.md 包含：
- 项目简介与特性列表
- 技术栈与架构概览
- 构建说明（依赖、CMake 命令）
- 运行说明（客户端/服务器启动参数）
- 项目目录结构
- 配置说明

---

## P1 — 功能完善（核心体验）

### DEV-3: 关键模块补充日志

- **问题**: 多个模块完全无日志输出，故障时无法诊断：
  - `PacketCodec.cpp` — 序列化错误静默
  - `TcpVoiceTunnel` — 重组失败静默
  - `UdpSocket` — 无日志
  - `VoiceActivity` — 无日志
  - `Resampler` — 无日志
  - `AudioMemoryPool` — 池耗尽静默
  - `AudioMixer` — 无日志

**实现方案**: 为每个模块在关键路径添加 NEVO_LOG 调用：
- 错误路径：ERROR 级别
- 状态变更：INFO 级别
- 性能敏感路径：TRACE/DEBUG 级别

---

### DEV-4: Result<T> 安全性——value() 未检查调用为 UB

- **问题**: `Result<T>::value()` 在 error 状态下调用为未定义行为（直接 `std::get<0>` 无任何检查），比 `std::optional::value()` 更危险（后者至少会 throw）
- **文件**: `src/core/include/nevo/core/common/Result.h:106`

**修复方案**:
1. 在 `value()` 中添加 `assert(has_value())` 断言（debug 模式下捕获错误）
2. 添加 `value_or()` 的 `const` 重载版本
3. 修复 `value_or()` 的 `std::move(default_val)` 问题（移除不必要的 move）

---

### DEV-5: 客户端设置持久化

- **问题**: 客户端不保存任何用户设置（上次连接的服务器、音频偏好、VAD/PTT 选择等），每次启动都要重新配置
- **文件**: `src/ui/src/MainWindow.cpp`, `src/ui/include/nevo/ui/MainWindow.h`

**实现方案**:
1. 在 MainWindow 中使用 `QSettings` 保存/恢复设置
2. 保存项：上次服务器地址/端口、用户名、输入/输出音量、VAD/PTT 模式
3. 启动时恢复设置到 UI 控件
4. 连接成功时保存服务器信息

---

## P2 — 工程化（部署与分发）

### DEV-6: 服务器配置文件支持

- **问题**: 服务器仅支持命令行参数，无法从配置文件加载参数。Database 中有 `server_config` 表但未在启动时使用
- **文件**: `src/server/src/main.cpp`, 新增 `src/server/include/nevo/server/ServerConfig.h`

**实现方案**:
1. 创建 `ServerConfig` 类，支持从 JSON 文件加载配置
2. 配置项：tcp_port, udp_port, db_path, threads, log_level, server_name, max_users, welcome_message
3. 优先级：命令行参数 > 配置文件 > 默认值
4. 提供 `server_config.example.json` 示例文件

---

### DEV-7: CMake Install 目标与 CPack 打包

- **问题**: 项目无 install target、无 CPack 配置，无法执行 `cmake --install` 或生成安装包
- **文件**: `CMakeLists.txt`（根目录及子目录）

**实现方案**:
1. 为所有库目标添加 `install(TARGETS ...)` 命令
2. 为公共头文件添加 `install(FILES ...)`
3. 添加 CPack 配置（DEB/RPM/ZIP 格式）
4. 添加 CMake 版本号变量

---

### DEV-8: Dockerfile 服务器容器化

- **问题**: 无 Docker 支持，无法容器化部署服务器
- **文件**: 新增 `Dockerfile`, `docker-compose.yml`

**实现方案**:
1. 多阶段构建 Dockerfile（build → runtime）
2. docker-compose.yml 定义服务器服务
3. 暴露 TCP/UDP 端口
4. 挂载配置文件和数据库目录

---

## 实施优先级

| 优先级 | 编号 | 说明 | 影响范围 |
|--------|------|------|----------|
| P0 | DEV-1 | Logger 初始化 | 全项目调试能力 |
| P0 | DEV-2 | README.md | 项目可用性 |
| P1 | DEV-3 | 补充日志 | 运维诊断 |
| P1 | DEV-4 | Result<T> 安全 | 代码健壮性 |
| P1 | DEV-5 | 设置持久化 | 用户体验 |
| P2 | DEV-6 | 服务器配置 | 部署便捷性 |
| P2 | DEV-7 | Install/CPack | 分发能力 |
| P2 | DEV-8 | Dockerfile | 容器化部署 |
