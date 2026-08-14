# NEVO

> 低延迟端到端加密 VoIP 通讯系统 —— 自托管的 TeamSpeak / Mumble / Discord 语音替代方案。

[📜 更新日志](CHANGELOG.md)

NEVO 是一款跨平台实时语音/视频通信系统：C++20 服务端承载所有在线用户的实时通信，Windows 端提供 Electron Web 客户端（V3），Android 端提供 Kotlin 原生客户端。全部语音/视频流量均经过端到端加密。

## ✨ 功能特性

### 语音与视频
- **实时语音**：Opus 编解码（48 kHz），低延迟帧传输
- **视频通话与视频大厅**：一对一视频 + 多人视频大厅
- **屏幕共享**：H.264 独立视频通道，支持系统/应用音频
- **频道文字聊天**：Emoji、图片与文件传输

### 安全
- **端到端加密**：XChaCha20-Poly1305 AEAD 加密全部语音/视频/控制流量
- **会话密钥自动轮换**（每 10 分钟，20 秒重叠期），密钥轮换全链路传播到媒体层
- TLS 1.2+ 控制通道（OpenSSL，可选）
- 中继数据解密认证，防止伪造与注入

### 网络
- **NAT 穿透四级级联回退**：STUN 探测 → UDP 打洞 → TURN 中继 → 全 TCP 媒体隧道
- 会话断线自动清理、对端重启去重

### 管理与协作
- 层级频道树（创建/删除/重命名/移动）
- 位掩码权限系统（Admin / ChannelAdmin / User / Guest）
- 踢出/封禁/移动用户、管理员认证、服主绑定
- Web 管理面板（JSON-over-TCP 管理通道 + 8090 Web UI）

### 客户端体验
- **自动更新**：基于 GitHub Release，签名清单校验、多源路由探测与断点续传、增量（delta）更新与回滚
- 主题切换：浅色 / 深色 / 跟随系统
- 跨平台：Windows（Electron Web 客户端 V3）、Android（Kotlin + Jetpack Compose）

## 🧱 技术栈

| 组件 | 技术 |
|------|------|
| 核心引擎 / 服务端 | C++20 |
| 构建系统 | CMake 3.21+ + vcpkg |
| 异步 I/O | Boost.Asio |
| 音频 | miniaudio + Opus |
| 加密 | libsodium（XChaCha20-Poly1305） |
| 协议 | Protobuf（`proto/`）+ 自定义小端 TLV 线格式 |
| 存储 | SQLite3 |
| 日志 | spdlog |
| Web 客户端 | Electron + Python 网关（PyInstaller 打包） |
| Android 客户端 | Kotlin + Jetpack Compose + JNI/C++ |
| 部署 | Docker / docker-compose，CI 自动发布镜像 |

## 📁 仓库结构

```text
NEVO/
├── src/
│   ├── core/       # 核心引擎：音频编解码、协议编解码、数据模型、权限、VAD（无外部依赖）
│   ├── network/    # 网络传输层：TCP/UDP、NAT 穿透、加密传输、TLS
│   ├── server/     # 服务端：会话管理、音频/视频中继、SQLite 持久化、Web 管理代理
│   └── client/     # C++ 客户端核心 + Electron 网关 Python 协议库（gui_python/）
├── webclient/      # Electron Web 客户端（V3）：electron/ + js/ + gateway.py
├── mobile/android/ # Kotlin Android 客户端（独立 Gradle 构建）
├── proto/          # Protobuf 协议定义（common / control / voice / video）
├── web/            # Web 管理面板（HTML/JS + Python 代理）
├── website/        # 项目官网静态页
├── tests/          # GTest 单元测试
├── test/           # 集成测试与跨设备复现脚本
├── docs/           # 设计文档与部署脚本
├── scripts/        # 辅助脚本（协议枚举同步等）
├── 3rdparty/       # 第三方库（miniaudio、spdlog）
└── installer/      # NSIS Windows 安装包脚本
```

## 🚀 快速开始

### 服务端：Docker 部署（推荐）

```bash
cp .env.example .env   # 按需编辑端口/数据目录等配置
docker compose up -d --build
docker compose ps
docker compose logs -f
```

更新镜像：`docker compose pull && docker compose up -d`

### 服务端：本地构建（需要 vcpkg 工具链）

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Web 客户端（V3）

源码位于 `webclient/`，使用 Electron + electron-builder 打包：

```bash
cd webclient
npm install
npm run build   # 产出 NSIS 安装包
```

### Android 客户端

```bash
cd mobile/android
./gradlew assembleRelease
```

## ⚙️ 配置

服务端默认端口：

| 端口 | 协议 | 用途 |
|------|------|------|
| 24430 | TCP | 客户端控制 / 信令 |
| 24431 | UDP | 语音媒体（Opus） |
| 24432 | UDP | 视频 / 屏幕共享媒体 |
| 24433 | TCP | 管理通道（JSON-over-TCP） |
| 8090 | TCP | Web 管理面板 |

服务端配置文件见 `server_config.example.json`（TCP/UDP 端口、线程数、日志级别、文件传输限速/并发/大小上限等）。
配置优先级：命令行参数 > 配置文件 > 默认值。

## 🧪 测试

```bash
cd build && ctest --output-on-failure -C Release
```

覆盖范围：核心库（Result\<T\>、Channel、Permission、Opus、JitterBuffer、Mixer、VAD）、网络层（TcpConnection、VoiceCrypto、NatTraversal）、服务端集成测试；另有 `test/` 下的跨设备复现脚本与 Web 客户端测试。

## 📚 文档

- [更新日志](CHANGELOG.md)
- [AGENTS.md](AGENTS.md) — 开发约束与核心代码边界
- [NEVO 项目全面分析报告](NEVO项目分析报告.md)
- `docs/` — 设计文档（协议线格式、跨设备通信修复、屏幕共享设计、自动更新方案等）

## 📄 许可证

保留所有权利（All Rights Reserved）。
