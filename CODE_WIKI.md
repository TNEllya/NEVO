# NEVO Code Wiki

> 本文档基于当前仓库源码、构建脚本、配置文件与测试目录整理，面向希望快速理解、运行、维护和扩展 NEVO 项目的开发者。

## 目录

- [1. 项目概览](#1-项目概览)
- [2. 仓库结构](#2-仓库结构)
- [3. 整体架构](#3-整体架构)
- [4. 构建目标与依赖关系](#4-构建目标与依赖关系)
- [5. 主要模块职责](#5-主要模块职责)
- [6. 关键类与函数说明](#6-关键类与函数说明)
- [7. 核心运行链路](#7-核心运行链路)
- [8. 协议与数据模型](#8-协议与数据模型)
- [9. 配置、部署与运行方式](#9-配置部署与运行方式)
- [10. 测试与 CI](#10-测试与-ci)
- [11. 扩展开发指南](#11-扩展开发指南)

## 1. 项目概览

### 1.1 项目定位

NEVO 是一个低延迟加密 VoIP 语音通信系统，主实现采用 C++20 + CMake，目标是提供跨平台的客户端、服务端和桌面管理能力。项目同时包含 C++/Qt 原生 GUI 与 Python/PyQt GUI 路线。

项目核心能力包括：

| 能力 | 说明 |
| --- | --- |
| 低延迟音频 | 使用 miniaudio 做采集/播放，Opus 做语音编解码 |
| 控制与语音双通道 | 控制消息走 TCP，实时语音优先走 UDP |
| 加密语音 | 使用 libsodium 上的 XChaCha20-Poly1305 AEAD 方案 |
| NAT 穿透 | STUN 探测、UDP 打洞、TURN/TCP 降级思路 |
| 频道系统 | 树状频道、用户流转、频道内语音转发 |
| 权限系统 | 基于权限位与权限组的用户/频道操作控制 |
| 服务端持久化 | 使用 SQLite3 保存用户、频道、配置、封禁等数据 |
| 桌面界面 | C++ Qt 6 客户端界面与服务端 GUI，另有 Python/PyQt5 GUI |
| 容器部署 | 提供 Dockerfile 与 docker-compose.yml |

### 1.2 技术栈

| 类别 | 技术 |
| --- | --- |
| 主语言 | C++20 |
| 辅助语言 | Python、Kotlin、Swift |
| 构建系统 | CMake 3.21+ |
| GUI | Qt 6 Widgets、PyQt5、qfluentwidgets |
| 异步网络 | Boost.Asio、C++20 协程 |
| 音频 | miniaudio、Opus |
| 加密 | libsodium、OpenSSL |
| 协议 | Protocol Buffers schema + 项目内生成/适配代码 |
| 存储 | SQLite3 |
| 日志 | spdlog 接口兼容封装/本地 stub |
| 测试 | Google Test、CTest |
| 部署 | Docker、docker-compose、GitHub Actions |

## 2. 仓库结构

```text
NEVO/
├── 3rdparty/                    # 嵌入式第三方依赖，如 miniaudio、spdlog stub
├── cmake/                       # CMake 辅助模块：编译器警告、平台设置
├── mobile/                      # Android / iOS 音频集成骨架
├── proto/                       # Protobuf 协议定义
├── scripts/                     # 辅助脚本，如协议枚举同步
├── src/
│   ├── core/                    # 共享核心库：音频、模型、协议、日志、Result
│   ├── network/                 # 网络库：TCP/UDP/TLS/NAT/语音加密
│   ├── client/                  # C++ 客户端核心库 + Python 客户端 GUI
│   ├── server/                  # C++ 服务端 + C++/Python 服务端管理界面
│   └── ui/                      # C++ Qt 6 客户端 GUI
├── tests/                       # GTest 单元测试和集成测试
├── .github/workflows/build.yml  # CI 构建与测试流水线
├── CMakeLists.txt               # 顶层 CMake 入口
├── Dockerfile                   # 服务端容器镜像构建
├── docker-compose.yml           # 服务端容器编排
├── README.md                    # 项目说明与基础运行方式
└── server_config.example.json   # 服务端配置样例
```

## 3. 整体架构

### 3.1 分层架构

```text
+----------------------------------------------------------------------------+
|                             Application Layer                              |
|----------------------------------------------------------------------------|
| nevo_server | nevo_client_ui | nevo_server_gui | Python Client/Server GUIs |
+--------------------------------------+-------------------------------------+
                                       |
+--------------------------------------v-------------------------------------+
|                                Client Layer                                |
|----------------------------------------------------------------------------|
| nevo_client: ClientCore, NetworkManager, AudioInput, AudioOutput           |
+--------------------------------------+-------------------------------------+
                                       |
+--------------------------------------v-------------------------------------+
|                               Network Layer                                |
|----------------------------------------------------------------------------|
| nevo_network: TcpConnection, UdpSocket, SslWrapper, NatTraversal,          |
|               VoiceCrypto, TcpVoiceTunnel, ConnectionManager, PacketRouter |
+--------------------------------------+-------------------------------------+
                                       |
+--------------------------------------v-------------------------------------+
|                                 Core Layer                                 |
|----------------------------------------------------------------------------|
| nevo_core: AudioEngine, AudioMixer, JitterBuffer, Opus wrappers,           |
|            Result, Logger, Types, User, Channel, Permission, PacketCodec   |
+----------------------------------------------------------------------------+
```

### 3.2 架构特点

1. `core` 是最低层共享库，不直接依赖客户端或服务端业务。
2. `network` 构建在 `core` 之上，封装传输、加密与 NAT 相关能力。
3. `client` 依赖 `core` 和 `network`，把音频引擎与网络连接组装成客户端业务层。
4. `server` 依赖 `core` 和 `network`，负责会话、频道、语音中继、数据库和控制接口。
5. `ui` 作为 C++ Qt 客户端界面，在 Boost 可用时链接 `nevo_client`，否则仍可构建为有限功能界面。
6. Python GUI 位于 `src/client/gui_python` 和 `src/server/gui_python`，与 C++ Qt GUI 并存，偏向快速管理和打包分发。

## 4. 构建目标与依赖关系

### 4.1 CMake 构建目标

| 目标 | 类型 | 来源 | 说明 |
| --- | --- | --- | --- |
| `miniaudio` | 静态/对象库 | `3rdparty/miniaudio.c` | 本地音频库封装 |
| `spdlog_stub` | INTERFACE 库 | 顶层 `CMakeLists.txt` | spdlog 兼容接口 |
| `nevo_config` | INTERFACE 库 | 顶层 `CMakeLists.txt` | 注入依赖可用性宏 |
| `nevo_core` | 库 | `src/core/CMakeLists.txt` | 共享核心能力 |
| `nevo_network` | 库 | `src/network/CMakeLists.txt` | TCP/UDP/TLS/NAT/加密 |
| `nevo_client` | 库 | `src/client/CMakeLists.txt` | 客户端核心业务层 |
| `nevo_server` | 可执行文件 | `src/server/CMakeLists.txt` | 服务端进程 |
| `nevo_client_ui` | 可执行文件 | `src/ui/CMakeLists.txt` | C++ Qt 客户端 |
| `nevo_server_gui` | 可执行文件 | `src/server/CMakeLists.txt` | C++ Qt 服务端管理界面 |

### 4.2 条件依赖与编译宏

顶层构建脚本会检测可选依赖，并通过 `nevo_config` 注入编译宏。

| 依赖 | 宏 | 不可用时的行为 |
| --- | --- | --- |
| Boost | `NEVO_HAS_BOOST` | 跳过 `network` 和 `client`，服务端也无法构建 |
| Boost lockfree | `NEVO_HAS_BOOST_LOCKFREE` | 使用非 lockfree 降级实现 |
| Opus | `NEVO_HAS_OPUS` | Opus 编解码走 stub/静音降级 |
| libsodium | `NEVO_HAS_SODIUM` | 语音加密走明文或 stub 降级 |
| SQLite3 | `NEVO_HAS_SQLITE` | 服务端模块跳过或数据库能力降级 |
| OpenSSL | `NEVO_HAS_OPENSSL` | TLS 能力不可用，TCP 明文 |
| Qt6 | `NEVO_HAS_QT` | 跳过 Qt GUI 模块 |

### 4.3 模块依赖图

```text
nevo_client_ui
   ├── Qt6::Core / Qt6::Widgets
   ├── nevo_core
   └── nevo_client       # 当 Boost 可用时
          ├── nevo_core
          └── nevo_network
                 └── nevo_core

nevo_server
   ├── nevo_core
   ├── nevo_network
   └── SQLite3::SQLite3 # 当 SQLite3 可用时

nevo_server_gui
   ├── Qt6::Core / Qt6::Widgets
   ├── nevo_core
   ├── nevo_network
   └── 服务端源码对象
```

## 5. 主要模块职责

### 5.1 `src/core`：共享核心库

`src/core` 是客户端和服务端共同依赖的基础层，主要包含音频处理、基础类型、错误处理、数据模型与协议编解码。

| 子目录 | 职责 |
| --- | --- |
| `audio/` | 音频采集播放协调、编解码、混音、抖动缓冲、VAD、重采样、内存池 |
| `common/` | `Result<T>`、日志封装、强类型 ID、常量类型 |
| `model/` | 用户、频道、权限模型 |
| `protocol/` | 控制帧与语音包编解码、消息类型定义 |

关键设计点：实时音频链路中的内存分配、缓冲和编解码被集中在 `core/audio`，便于客户端复用；服务端则主要复用模型和协议能力。

### 5.2 `src/network`：网络与安全传输库

`src/network` 负责网络通信基础设施，不直接承载登录、频道管理等业务语义。

| 文件/类 | 职责 |
| --- | --- |
| `TcpConnection` | Boost.Asio TCP 连接封装，处理帧读取、发送队列、超时与 TLS 升级 |
| `UdpSocket` | UDP socket 封装，负责语音包发送与接收循环 |
| `SslWrapper` | OpenSSL/TLS 上下文与证书验证封装 |
| `NatTraversal` | STUN/TURN/NAT 类型探测与穿透辅助 |
| `VoiceCrypto` | UDP 语音载荷加密/解密、nonce、密钥轮换 |
| `TcpVoiceTunnel` | UDP 不可用时的 TCP 语音隧道封装 |
| `ConnectionManager` | 连接生命周期、心跳、超时清理 |
| `PacketRouter` | 控制消息类型到处理函数的分发 |

### 5.3 `src/client`：客户端核心库

`src/client` 的 C++ 部分构建为 `nevo_client` 库，不是单独可执行文件。它负责把音频、网络、协议组织成 GUI 可调用的客户端业务接口。

| 文件/类 | 职责 |
| --- | --- |
| `ClientCore` | 客户端生命周期、连接状态机、业务事件回调 |
| `NetworkManager` | 控制连接、语音通道、NAT 模式、密钥与消息收发 |
| `AudioInput` | 将 `AudioEngine` 输出的编码语音帧送入网络层 |
| `AudioOutput` | 将网络收到的远端语音送回音频引擎播放 |
| `gui_python/` | Python/PyQt5 客户端 GUI、音频管理、协议适配 |

### 5.4 `src/server`：服务端与管理界面

`src/server` 同时包含服务端可执行文件、C++ Qt 服务端管理界面和 Python 服务端管理器。

| 文件/类 | 职责 |
| --- | --- |
| `ServerCore` | 服务端中枢，管理 TCP 接入、UDP 接收、会话、数据库、状态快照 |
| `ClientSession` | 单客户端控制会话，处理登录、频道、聊天、管理命令等控制消息 |
| `ChannelManager` | 树状频道结构管理，与数据库同步 |
| `AudioRelay` | 根据用户所在频道转发 UDP 语音包 |
| `Database` | SQLite3 持久化封装 |
| `ControlServer` | 面向管理 GUI 的 JSON-over-TCP IPC 控制服务 |
| `ServerConfig` | 服务端配置结构、默认值、JSON/命令行参数读取 |
| `ui/` | C++ Qt 服务端管理界面 |
| `gui_python/` | Python/PyQt5 服务端管理器 |

### 5.5 `src/ui`：C++ Qt 客户端 GUI

`src/ui` 构建 `nevo_client_ui`，是 C++ 桌面客户端入口。

| 文件/类 | 职责 |
| --- | --- |
| `MainWindow` | 主窗口，组合频道树、用户列表、聊天、连接栏、音频设置 |
| `LoginDialog` | 登录/连接信息输入 |
| `ChannelTreeModel` | Qt tree model，用于频道树展示 |
| `ChannelItemDelegate` | 频道树绘制代理 |
| `UserListModel` | 当前频道用户列表 model |
| `AudioSettingsWidget` | 音频设备、VAD/PTT、输入输出音量、测试等设置 |
| `ConnectionBar` | 连接状态、延迟、NAT/传输模式等底部状态展示 |
| `ChatWidget` | 频道文字聊天面板 |
| `ThemeManager` | 深色主题应用与样式管理 |
| `IconProvider` | UI 图标提供器 |

### 5.6 `proto` 与协议脚本

`proto` 目录保存协议 schema：

| 文件 | 说明 |
| --- | --- |
| `common.proto` | 通用消息、ID、状态等定义 |
| `control.proto` | 登录、频道、聊天、管理等控制面消息 |
| `voice.proto` | 语音包头、语音传输相关消息 |

`scripts/sync_proto_enums.py` 用于同步协议枚举，降低 C++/Python/Proto 定义漂移的风险。

### 5.7 `mobile`：移动端音频集成骨架

| 路径 | 说明 |
| --- | --- |
| `mobile/android` | Android Manifest、CMakeLists、`NevoAudioService.kt` |
| `mobile/ios` | iOS Info.plist、`NevoAudioSession.swift`、README |

移动端目录目前更像平台音频会话/服务的集成骨架，不是完整移动客户端。

## 6. 关键类与函数说明

### 6.1 `AudioEngine`

位置：`src/core/include/nevo/core/audio/AudioEngine.h`、`src/core/src/audio/AudioEngine.cpp`

职责：协调完整音频流水线，包括设备上下文、采集、播放、编码、解码、混音与输入回调。

常见关键接口：

| 接口 | 说明 |
| --- | --- |
| `initContext()` | 仅初始化音频上下文，便于连接前枚举设备 |
| `initialize()` / `initialize(const Config&)` | 初始化并启动音频引擎 |
| `shutdown()` | 停止设备并释放资源 |
| `setInputCallback()` | 注册编码后语音数据回调，供客户端网络层发送 |
| `queueAudioData()` | 将远端语音数据送入解码/混音/播放管线 |
| `setMuted()` / `setInputVolume()` / `setOutputVolume()` | 音频控制接口 |

### 6.2 `JitterBuffer`

位置：`src/core/include/nevo/core/audio/JitterBuffer.h`、`src/core/src/audio/JitterBuffer.cpp`

职责：缓冲网络侧乱序或抖动到达的语音帧，按序输出给解码器，并辅助处理延迟、丢包和多用户语音流稳定性。

### 6.3 `AudioMixer`

位置：`src/core/include/nevo/core/audio/AudioMixer.h`、`src/core/src/audio/AudioMixer.cpp`

职责：对多个用户的 PCM 音频做混音，支持用户音量控制、限幅和输出帧生成。它是多人频道语音播放体验的关键组件。

### 6.4 `OpusEncoderWrapper` / `OpusDecoderWrapper`

位置：`src/core/include/nevo/core/audio/OpusEncoder.h`、`src/core/include/nevo/core/audio/OpusDecoder.h`

职责：封装 Opus 编码器与解码器。在 `NEVO_HAS_OPUS` 不可用时，项目会走 stub 降级逻辑，使核心模块仍可编译。

### 6.5 `VoiceActivity`

位置：`src/core/include/nevo/core/audio/VoiceActivity.h`

职责：实现语音活动检测与 PTT 输入控制相关逻辑，决定何时发送麦克风音频。

### 6.6 `Result<T>` 与 `Error`

位置：`src/core/include/nevo/core/common/Result.h`

职责：统一函数错误返回模型，减少异常在业务层和实时链路中的传播。`Result<void>` 是无返回值操作的特化版本。

典型语义：

- 成功：返回包含值或空成功状态的 `Result`
- 失败：返回 `Error`，其中包含 `ResultCode` 与错误消息

### 6.7 `User` / `Channel` / `PermissionManager`

位置：`src/core/include/nevo/core/model/`

职责：

| 类 | 说明 |
| --- | --- |
| `User` | 用户 ID、名称、状态、频道归属等共享模型 |
| `Channel` | 树状频道节点，维护父子关系和频道属性 |
| `PermissionManager` | 权限位、权限组、权限检查逻辑 |

### 6.8 `PacketCodec` 与 `PacketTypes`

位置：`src/core/include/nevo/core/protocol/`

职责：

| 类/文件 | 说明 |
| --- | --- |
| `PacketCodec` | TCP 控制帧与 UDP 语音包的编码/解码 |
| `PacketTypes` | 控制消息类型、帧常量、大小限制等协议基础定义 |

### 6.9 `TcpConnection`

位置：`src/network/include/nevo/network/TcpConnection.h`、`src/network/src/TcpConnection.cpp`

职责：封装异步 TCP 连接，处理帧协议、读写循环、发送队列、连接关闭、超时和可选 TLS 交互。服务端的 `ClientSession` 与客户端的 `NetworkManager` 都会围绕它构建控制通道。

### 6.10 `UdpSocket`

位置：`src/network/include/nevo/network/UdpSocket.h`、`src/network/src/UdpSocket.cpp`

职责：封装 UDP 收发，是实时语音传输的主要通道。服务端通过它接收语音包并交给 `AudioRelay`；客户端通过它发送和接收语音帧。

### 6.11 `VoiceCrypto`

位置：`src/network/include/nevo/network/VoiceCrypto.h`、`src/network/src/VoiceCrypto.cpp`

职责：提供 UDP 语音载荷加密/解密能力。启用 libsodium 时使用 AEAD 加密，未启用时走降级路径。

### 6.12 `NatTraversal`

位置：`src/network/include/nevo/network/NatTraversal.h`、`src/network/src/NatTraversal.cpp`

职责：实现 NAT 类型探测、STUN 绑定请求、候选地址发现等穿透辅助逻辑。客户端 `NetworkManager` 会据此选择 UDP 模式或 TCP 隧道模式。

### 6.13 `ClientCore`

位置：`src/client/include/nevo/client/ClientCore.h`、`src/client/src/ClientCore.cpp`

职责：客户端顶层业务协调器，维护客户端连接状态机，聚合网络管理器、音频输入输出桥接器，并向 GUI 暴露连接、断开、频道、用户、音频等操作。

核心状态包括：

| 状态 | 说明 |
| --- | --- |
| `Disconnected` | 未连接或已断开 |
| `Connecting` | 正在建立控制连接 |
| `Authenticating` | 正在认证/登录 |
| `Connected` | 已连接并可正常收发 |
| `Reconnecting` | 断线重连中 |

### 6.14 `NetworkManager`

位置：`src/client/include/nevo/client/NetworkManager.h`、`src/client/src/NetworkManager.cpp`

职责：客户端网络编排中枢，负责：

- 建立 TCP 控制连接
- 登录、控制消息收发
- UDP 语音通道建立
- NAT 穿透结果处理
- TCP 语音隧道降级
- 语音密钥与加密对象管理
- 将网络事件回调给 `ClientCore`

### 6.15 `AudioInput` / `AudioOutput`

位置：`src/client/include/nevo/client/AudioInput.h`、`src/client/include/nevo/client/AudioOutput.h`

职责：在音频引擎与网络层之间做桥接。

| 类 | 数据方向 |
| --- | --- |
| `AudioInput` | 麦克风/编码器 → 网络发送 |
| `AudioOutput` | 网络接收 → 解码/混音/播放 |

### 6.16 `ServerCore`

位置：`src/server/include/nevo/server/ServerCore.h`、`src/server/src/ServerCore.cpp`

职责：服务端核心入口类，协调所有服务端子系统。

关键接口：

| 接口 | 说明 |
| --- | --- |
| `initialize(const std::string& db_path)` | 初始化数据库、频道管理、音频中继等子系统 |
| `start()` | 启动 TCP accept loop 和 UDP receive loop |
| `shutdown()` | 优雅关闭服务端，断开客户端并关闭 socket |
| `onClientConnected()` | 客户端登录成功后的会话注册 |
| `onClientDisconnected()` | 客户端断开后的状态清理 |
| `getStatusSnapshot()` | 为 GUI/控制接口提供线程安全状态快照 |
| `broadcastToChannel()` | 向频道内用户广播控制消息 |

### 6.17 `ClientSession`

位置：`src/server/include/nevo/server/ClientSession.h`、`src/server/src/ClientSession.cpp`

职责：服务端单客户端会话对象，绑定一个 TCP 控制连接，处理登录、频道加入/离开、聊天、管理命令、用户状态变化等控制消息。

### 6.18 `ChannelManager`

位置：`src/server/include/nevo/server/ChannelManager.h`、`src/server/src/ChannelManager.cpp`

职责：维护服务端频道树，包括频道创建、删除、移动、用户进入/离开、频道快照生成，并与数据库持久化保持同步。

### 6.19 `AudioRelay`

位置：`src/server/include/nevo/server/AudioRelay.h`、`src/server/src/AudioRelay.cpp`

职责：服务端语音转发器。它根据发送者当前频道查找目标用户，将 UDP 语音包转发给同频道内其他客户端，同时维护用户到 UDP endpoint 的映射。

### 6.20 `Database`

位置：`src/server/include/nevo/server/Database.h`、`src/server/src/Database.cpp`

职责：SQLite3 数据库封装，提供用户、频道、权限、配置、封禁等服务端持久化接口。

### 6.21 `ControlServer`

位置：`src/server/include/nevo/server/ControlServer.h`、`src/server/src/ControlServer.cpp`

职责：面向 GUI 管理器的 JSON-over-TCP 控制服务，允许外部管理界面查询状态、执行管理操作、读取日志或修改服务器设置。

### 6.22 `ServerConfig`

位置：`src/server/include/nevo/server/ServerConfig.h`、`src/server/src/ServerConfig.cpp`

职责：集中管理服务端配置，包括默认配置、配置文件读取、命令行参数覆盖和 JSON 解析。

## 7. 核心运行链路

### 7.1 服务端启动链路

入口：`src/server/src/main.cpp`

```text
main()
 ├── ServerConfig::fromArgs(argc, argv)
 ├── LoggerManager::initialize("nevo_server.log", level)
 ├── 创建 boost::asio::io_context
 ├── 创建 ServerCore(io_ctx, tcp_port, udp_port)
 ├── ServerCore::initialize(db_path)
 │    ├── 初始化 Database
 │    ├── 初始化 ChannelManager
 │    ├── 初始化 AudioRelay
 │    └── 准备 TCP/UDP/ControlServer 等子系统
 ├── 注册 SIGINT/SIGTERM 或 Windows 控制台关闭处理器
 ├── ServerCore::start()
 │    ├── 启动 TCP accept loop
 │    └── 启动 UDP receive loop
 └── 运行 io_context 线程池
```

服务端命令行参数优先级为：命令行参数 > 配置文件 > 默认值。

### 7.2 C++ 客户端启动链路

入口：`src/ui/src/main.cpp`

```text
main()
 ├── 创建 QApplication
 ├── 设置应用名、版本、组织名
 ├── 读取 QSettings 中保存的语言偏好
 ├── 加载 Qt 翻译资源
 ├── ThemeManager::applyDarkTheme()
 ├── 当 NEVO_HAS_BOOST 可用：创建 boost::asio::io_context
 ├── 创建 MainWindow
 └── app.exec()
```

当 Boost 可用时，`MainWindow` 接收 `io_context`，可集成 `ClientCore`；当 Boost 不可用时，GUI 仍可作为独立界面启动，但网络客户端能力受限。

### 7.3 客户端连接与语音链路

```text
用户点击连接
 └── MainWindow / LoginDialog 收集连接信息
     └── ClientCore 发起连接
         └── NetworkManager 建立 TCP 控制连接
             ├── 发送登录/认证控制消息
             ├── 建立或协商 UDP 语音通道
             ├── 初始化 VoiceCrypto 密钥
             └── 将连接状态回调给 GUI

麦克风语音发送
 └── AudioEngine 采集 PCM
     └── VoiceActivity 判断是否发送
         └── OpusEncoderWrapper 编码
             └── AudioInput 回调 NetworkManager
                 └── VoiceCrypto 加密
                     └── UdpSocket 或 TcpVoiceTunnel 发送

远端语音播放
 └── UdpSocket 或 TcpVoiceTunnel 接收语音包
     └── VoiceCrypto 解密
         └── AudioOutput 送入 AudioEngine
             └── JitterBuffer 缓冲重排
                 └── OpusDecoderWrapper 解码
                     └── AudioMixer 混音
                         └── miniaudio 播放
```

### 7.4 服务端语音转发链路

```text
UdpSocket 收到客户端语音包
 └── ServerCore 交给 AudioRelay
     ├── 根据 sender user/session 查找当前频道
     ├── 查询频道内其他在线用户
     ├── 根据用户 endpoint 映射确定目标地址
     └── 通过 UdpSocket 转发给同频道其他客户端
```

### 7.5 管理 GUI 链路

C++ 服务端 GUI 直接链接服务端源码和 Qt 组件。Python 服务端管理器则更偏向外部控制：

```text
Python Server Manager
 ├── server_process.py 启动 nevo_server.exe
 ├── 连接 ControlServer 控制端口
 ├── 发送 JSON 管理命令
 └── main_window.py / views 展示仪表盘、会话、频道、日志、配置
```

## 8. 协议与数据模型

### 8.1 控制面与语音面

NEVO 的通信可分为两类：

| 平面 | 传输 | 内容 |
| --- | --- | --- |
| 控制面 | TCP，可选 TLS | 登录、频道、聊天、权限、管理、心跳、密钥协商 |
| 语音面 | UDP 优先，TCP 隧道降级 | 实时语音帧、语音包头、序列号、时间戳、加密载荷 |

这种拆分降低了控制消息可靠性和语音实时性之间的耦合。TCP 保证控制消息可靠到达，UDP 让语音链路尽可能低延迟。

### 8.2 协议文件职责

| 文件 | 说明 |
| --- | --- |
| `proto/common.proto` | 通用枚举、状态、用户/频道基础结构 |
| `proto/control.proto` | 控制消息结构，如登录、频道操作、聊天和管理命令 |
| `proto/voice.proto` | 语音包头和语音传输元信息 |

### 8.3 强类型 ID

`src/core/include/nevo/core/common/Types.h` 定义了用户、频道、会话、权限组等 ID 类型。这样做可以减少把不同 ID 混用的风险，并提高函数签名的可读性。

### 8.4 权限模型

权限定义位于 `Permission.h`，核心思想是使用 bitmask 表达具体权限，再通过权限组和检查函数组合出用户在某个频道上的实际能力。常见权限包括加入频道、发言、管理频道、踢人、封禁等。

## 9. 配置、部署与运行方式

### 9.1 本地构建

基础构建：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

启用测试：

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows 下可执行文件通常位于：

```text
build/bin/
```

### 9.2 服务端运行

使用配置文件：

```bash
./nevo_server --config server_config.json
```

使用命令行覆盖配置：

```bash
./nevo_server --tcp-port 24430 --udp-port 24431 --db nevo_server.db --threads 4 --log-level info
```

服务端支持的主要参数：

| 参数 | 说明 |
| --- | --- |
| `--config <path>` | 从 JSON 文件读取配置 |
| `--tcp-port <port>` | TCP 控制通道监听端口 |
| `--udp-port <port>` | UDP 语音通道监听端口 |
| `--db <path>` | SQLite 数据库文件路径 |
| `--threads <count>` | io_context 线程池大小 |
| `--log-level <level>` | 日志级别：trace/debug/info/warn/error |
| `--help` | 显示帮助 |

### 9.3 服务端配置文件

样例文件：`server_config.example.json`

```json
{
    "tcp_port": 24430,
    "udp_port": 24431,
    "db_path": "nevo_server.db",
    "threads": 4,
    "log_level": "info",
    "server_name": "NEVO Server",
    "max_users": 100,
    "welcome_message": "Welcome to the NEVO server!"
}
```

配置优先级：命令行参数 > 配置文件 > 默认值。

### 9.4 C++ 客户端运行

构建完成后启动 `nevo_client_ui`，通过登录/连接界面输入服务器地址、端口和用户信息。客户端设置通过 Qt `QSettings` 保存，例如语言、最近连接信息和音频设置。

### 9.5 Python GUI 运行

Python GUI 位于：

| GUI | 入口 |
| --- | --- |
| Python 客户端 | `src/client/gui_python/main.py` |
| Python 服务端管理器 | `src/server/gui_python/main.py` |

这部分依赖 PyQt5、qfluentwidgets、sounddevice、numpy/scipy 等 Python 生态库。仓库中还包含已打包产物目录，例如 `py-gui/` 和 `dist/`，它们不应被当作主要源码入口。

### 9.6 Docker 部署

构建镜像：

```bash
docker build -t nevo-server .
```

使用 compose 启动：

```bash
docker-compose up -d
```

容器暴露端口：

| 端口 | 协议 | 说明 |
| --- | --- | --- |
| `24430` | TCP | 控制通道 |
| `24431` | UDP | 语音通道 |

compose 默认挂载：

| 挂载 | 说明 |
| --- | --- |
| `nevo-data:/var/lib/nevo` | 数据目录 |
| `./server_config.json:/etc/nevo/server_config.json:ro` | 只读配置文件 |

## 10. 测试与 CI

### 10.1 测试目录

`tests/` 包含核心、音频、网络、服务端和 UI 相关测试。

| 目录 | 覆盖内容 |
| --- | --- |
| `tests/core_tests` | `Result`、`Channel`、`Permission`、`User` 等基础模型 |
| `tests/audio_tests` | Opus、JitterBuffer、AudioMixer、AudioMemoryPool 等 |
| `tests/network_tests` | TcpConnection、VoiceCrypto、NatTraversal 等 |
| `tests/server_tests` | 服务端参数解析、集成测试 |
| `tests/ui_tests` | Qt model 等 UI 逻辑测试 |
| `tests/src/...` | 自动生成或镜像路径风格的补充测试 |

### 10.2 本地测试命令

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### 10.3 CI 流水线

`.github/workflows/build.yml` 定义了：

| Job | 平台 | 说明 |
| --- | --- | --- |
| `build-linux` | Ubuntu 22.04 | 安装依赖、配置、构建、运行 CTest |
| `build-windows` | Windows latest | 配置并构建，启用测试构建 |
| `lint` | Ubuntu 22.04 | 使用 clang-format 做格式检查，但当前命令带 `|| true`，不会阻塞 CI |

## 11. 扩展开发指南

### 11.1 新增控制消息

建议步骤：

1. 在 `proto/control.proto` 中添加消息定义。
2. 同步 C++/Python 侧消息枚举或生成物。
3. 在 `PacketTypes` 中确认消息类型映射。
4. 在客户端 `NetworkManager` 添加发送/接收处理。
5. 在服务端 `ClientSession` 或 `PacketRouter` 添加对应 handler。
6. 为消息编解码和业务处理添加测试。

### 11.2 新增频道或权限能力

建议修改位置：

| 需求 | 修改位置 |
| --- | --- |
| 新权限位 | `src/core/include/nevo/core/model/Permission.h` |
| 权限检查 | `PermissionManager` |
| 频道结构变化 | `Channel`、`ChannelManager`、`Database` |
| UI 展示 | `ChannelTreeModel`、`MainWindow`、相关 views |
| 服务端控制命令 | `ClientSession`、`ControlServer` |

### 11.3 新增音频处理能力

优先在 `src/core/audio` 内实现底层能力，再通过 `AudioEngine` 暴露配置或开关。客户端 GUI 的设置项应通过 `AudioSettingsWidget` 或 Python settings 页面接入。

### 11.4 新增服务端管理功能

如果是 C++ GUI：

1. 在 `ServerCore` 或相关服务端类暴露线程安全查询/操作接口。
2. 在 `src/server/ui` 添加 model、panel 或按钮逻辑。
3. 更新 `nevo_server_gui` 的 CMake 源文件列表。

如果是 Python GUI：

1. 在 `ControlServer` 添加 JSON 命令。
2. 在 `src/server/gui_python` 的控制客户端中添加调用。
3. 在对应 view 中展示结果。

### 11.5 常见注意事项

- 实时音频路径上尽量避免频繁动态分配和阻塞调用。
- 网络回调中操作共享状态时要遵守现有 mutex/strand 模型。
- 依赖可用性由 CMake 宏控制，新增代码要考虑依赖缺失时的降级路径。
- GUI 层不要直接绕过 `ClientCore`/`ServerCore` 操作底层网络对象。
- 服务端配置变更要同时考虑命令行、JSON 配置、GUI 管理入口和 Docker 默认配置。
- 不要把 `py-gui/`、`dist/`、`_internal/` 这类打包产物当作主要源码修改入口。

## 12. 快速入口索引

| 想了解 | 推荐入口 |
| --- | --- |
| 项目介绍与构建 | `README.md`、`CMakeLists.txt` |
| 构建模块装配 | `src/CMakeLists.txt` |
| 服务端启动 | `src/server/src/main.cpp` |
| 服务端核心 | `src/server/include/nevo/server/ServerCore.h` |
| 客户端 GUI 启动 | `src/ui/src/main.cpp` |
| 客户端核心 | `src/client/include/nevo/client/ClientCore.h` |
| 网络管理 | `src/client/include/nevo/client/NetworkManager.h` |
| TCP/UDP 封装 | `src/network/include/nevo/network/` |
| 音频流水线 | `src/core/include/nevo/core/audio/` |
| 数据模型 | `src/core/include/nevo/core/model/` |
| 协议定义 | `proto/` |
| 服务端配置 | `server_config.example.json` |
| Docker 部署 | `Dockerfile`、`docker-compose.yml` |
| 自动化测试 | `tests/` |
