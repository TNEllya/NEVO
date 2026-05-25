# NEVO

低延迟加密 VoIP 应用 — C++20 客户端/服务端架构。

[English](README.md) | **中文**

## 功能特性

- **低延迟音频**：miniaudio 采集/播放 + Opus 编解码（支持带内 FEC）
- **加密语音**：XChaCha20-Poly1305 AEAD 加密，自动密钥轮换
- **NAT 穿透**：STUN 绑定 + TCP 回退语音隧道
- **频道系统**：层级式频道树，支持权限管理
- **Qt 6 界面**：可停靠的频道/用户面板、连接栏、音频设置
- **服务端仪表盘**：实时会话监控、用户管理
- **跨平台**：Windows / Linux / macOS
- **自动更新**：基于 GitHub Release 的客户端在线更新，支持断点续传和 SHA256 校验

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++20 |
| 构建 | CMake 3.21+ |
| 界面 | Qt 6 (Widgets) / PyQt5 + qfluentwidgets |
| 异步 I/O | Boost.Asio（协程） |
| 音频 | miniaudio + Opus |
| 加密 | libsodium (XChaCha20-Poly1305) |
| 协议 | Protobuf（手写生成头文件） |
| 存储 | SQLite3 |
| 日志 | spdlog（内嵌 stub，fmt 风格格式化） |

## 目录结构

```
NEVO/
├── cmake/                  # CMake 辅助模块
├── proto/                  # Protobuf 协议定义
│   └── generated/          # 手写 C++ protobuf 替代文件
├── 3rdparty/               # 内嵌依赖（miniaudio, spdlog stub）
├── src/
│   ├── core/               # 共享核心库
│   │   ├── audio/          # AudioEngine, Opus, JitterBuffer, Mixer, VAD
│   │   ├── common/         # Result<T>, Logger, Types
│   │   ├── protocol/       # PacketCodec, 帧编码
│   │   └── permission/     # 权限系统
│   ├── network/            # 网络库
│   │   ├── TcpConnection   # 异步 TCP + 帧协议
│   │   ├── UdpSocket       # 异步 UDP
│   │   ├── VoiceCrypto     # 加密/解密
│   │   ├── NatTraversal    # STUN 客户端
│   │   └── SslWrapper      # TLS 支持
│   ├── client/             # 客户端应用
│   │   ├── ClientCore      # 客户端生命周期与状态机
│   │   ├── NetworkManager  # 客户端网络编排
│   │   ├── AudioInput/Output
│   │   └── gui_python/     # Python PyQt5 客户端 GUI
│   ├── server/             # 服务端应用
│   │   ├── ServerCore      # 服务端生命周期
│   │   ├── ClientSession   # 每连接处理器
│   │   ├── Database        # SQLite 持久化
│   │   ├── gui_python/     # Python 服务端管理器 GUI
│   │   └── ui/             # 服务端仪表盘（Qt）
│   └── ui/                 # 客户端 UI（Qt 6）
│       ├── MainWindow      # 主窗口（可停靠面板）
│       ├── ChannelTreeModel
│       ├── UserListModel
│       └── ConnectionBar
└── tests/                  # 单元测试与集成测试（GTest）
```

## 构建

### 前置依赖

| 依赖 | 是否必需 | 备注 |
|------|----------|------|
| CMake 3.21+ | 是 | |
| C++20 编译器 | 是 | MSVC 2022, GCC 12+, Clang 15+ |
| Qt 6 | 是 | Core, Widgets 模块 |
| Boost 1.80+ | 可选 | system, lockfree, endian, context |
| OpenSSL | 可选 | TLS 支持 |
| Opus | 可选 | 音频编解码（缺失时使用 stub） |
| libsodium | 可选 | 语音加密（缺失时使用 stub） |
| SQLite3 | 可选 | 服务端持久化（缺失时使用 stub） |

### 构建命令

```bash
# 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 构建
cmake --build build --parallel

# 构建并运行测试
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build
```

### 条件编译

所有可选依赖通过编译时标志优雅降级：

| 标志 | 关闭时的效果 |
|------|-------------|
| `NEVO_HAS_BOOST` | 无网络/客户端模块（协程 stub） |
| `NEVO_HAS_OPUS` | Opus 编解码返回静音/stub |
| `NEVO_HAS_SODIUM` | 无语音加密（明文传输） |
| `NEVO_HAS_SQLITE` | 无服务端数据库（仅内存模式） |
| `NEVO_HAS_OPENSSL` | 无 TLS（明文 TCP） |

## 运行

### 服务端

```bash
./nevo_server [--tcp-port 24430] [--udp-port 24431] [--db server.db] \
              [--threads 4] [--log-level info]
```

### 客户端

启动 GUI 应用程序，使用连接对话框输入服务器地址、端口和凭据。

## 配置

### 服务端配置文件（JSON）

创建 `server_config.json`：

```json
{
    "tcp_port": 24430,
    "udp_port": 24431,
    "db_path": "server.db",
    "threads": 4,
    "log_level": "info",
    "server_name": "NEVO Server",
    "max_users": 100,
    "welcome_message": "Welcome to the NEVO server!"
}
```

优先级：命令行参数 > 配置文件 > 默认值。

### 客户端设置

客户端设置通过 QSettings 自动持久化：
- 上次连接的服务器地址/端口
- 用户名
- 音频输入/输出音量
- VAD/PTT 模式选择

## Docker

```bash
# 构建镜像
docker build -t nevo-server .

# 运行
docker-compose up -d
```

## 测试

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

测试覆盖范围包括：Result<T>、Channel、Permission、Opus 编解码、JitterBuffer、AudioMixer、AudioMemoryPool、TcpConnection、VoiceCrypto、NatTraversal、服务端集成测试。

## 许可证

保留所有权利。
