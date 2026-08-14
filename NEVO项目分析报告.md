# NEVO 项目全面分析报告

> 分析日期：2026-08-01 ｜ 分析范围：全仓库（C++ 核心、Android 客户端、Web 客户端、构建/CI/部署、协议、测试）

---

## 一、项目概览

**NEVO 是一款低延迟端到端加密 VoIP 通讯系统**，定位类似于 TeamSpeak / Mumble / Discord 语音频道的自托管替代方案。

- **技术栈**：C++20 服务端/核心库 + Kotlin Android 客户端 + Electron Web 客户端（V3）+ Web 管理面板
- **核心特性**：
  1. 实时语音：Opus 编解码，48kHz 采样，桌面端 20ms 帧
  2. 端到端加密：XChaCha20-Poly1305 AEAD 加密全部语音/视频数据
  3. NAT 穿透：STUN 探测 → UDP 打洞 → TURN 中继 → TCP 隧道，四级级联回退
  4. 层级频道树管理（创建/删除/重命名/移动）
  5. 频道文字聊天（Emoji、图片、文件上传）
  6. 屏幕共享（H.264，独立 UDP 视频通道，支持系统/应用音频）
  7. 一对一视频通话（编解码协商 + 状态机管理）
  8. 基于组位掩码的权限系统（Admin / ChannelAdmin / User / Guest）
  9. 管理功能：踢出/封禁/移动用户、管理员认证、服主绑定
  10. 会话密钥自动轮换（每 10 分钟，20 秒重叠期）
  11. TLS 1.2+ 控制通道（OpenSSL，可选）

### 版本与规模

| 指标 | 数值 |
|------|------|
| CMake 项目版本 | 0.1.0 |
| C++ 核心源文件 | ~22 个 .cpp（core 14 + network 8）+ server/client |
| Android Kotlin 文件 | 71 个（纯 Kotlin，无 Java） |
| C++ 单元测试 | 27 个测试文件（GTest） |
| Protobuf 消息类型 | 87 种（`MessageType` 枚举，`proto/control.proto:18-87`） |

---

## 二、仓库结构

```
NEVO/
├── src/                  # C++ 源码（core / network / server / client）
├── proto/                # Protobuf 协议定义（common/control/voice/video）
├── mobile/android/       # Android 客户端（Kotlin + JNI/C++）
├── tests/                # GTest 单元测试（27 个文件）
├── test/                 # 集成测试与测试客户端
├── web/                  # Web 管理面板（HTML/JS + Python 代理）
├── webclient/            # Web 客户端（Electron 网关）
├── website/              # 项目官网静态页
├── nevo-client/          # React+TS+Vite 客户端（实验性）
├── design/、nevo-gui-design/、nevo-ui-design/  # UI 设计稿/设计系统
├── installer/            # NSIS Windows 安装包脚本
├── cmake/                # CMake 辅助模块
├── 3rdparty/             # miniaudio、spdlog（头文件内嵌）
├── vcpkg/                # vcpkg 包管理器
├── docs/                 # 文档 + ⚠️ 含整个项目的嵌套副本
└── build*/、dist*/       # 多个构建/分发输出目录
```

---

## 三、服务端架构分析（C++）

### 3.1 模块划分

| 模块 | 目标 | 职责 |
|------|------|------|
| `src/core/` | `nevo_core`（库） | 音频引擎、协议编解码、数据模型、日志 |
| `src/network/` | `nevo_network`（库） | TCP/UDP 通信、NAT 穿透、语音加密、TLS |
| `src/server/` | `nevo_server` + 可选 `nevo_server_gui`（Qt6） | 服务端主程序 + 管理 GUI |
| `src/client/` | `nevo_client`（库）+ `nevo_console_client` | 客户端核心 + 控制台客户端 |
| `src/voice_relay_sim.cpp` | 独立可执行 | 语音中继模拟测试 |

### 3.2 nevo_core —— 核心库

**音频管线**（`src/audio/`）：

| 文件 | 职责 |
|------|------|
| `AudioEngine.cpp` (60 KB) | 中央管线：采集 → 编码 → 网络 → 解码 → 混音 → 播放；miniaudio 采集/播放，Boost lockfree SPSC 无锁队列连接实时线程与网络线程 |
| `OpusEncoder/Decoder.cpp` | Opus 编解码封装 |
| `JitterBuffer.cpp` | 抖动缓冲，处理 UDP 丢包/乱序 |
| `AudioMixer.cpp` | 多用户混音 |
| `Resampler.cpp` | 采样率转换（蓝牙耳机适配） |
| `VoiceActivity.cpp` | VAD 语音活动检测 |
| `AudioMemoryPool.cpp` | 实时安全内存池 |

**协议**（`src/protocol/PacketCodec.cpp`，40 KB）：TCP 帧（12 字节头 + Protobuf 载荷）与 UDP 语音包编解码，并兼容 Python 自定义线格式。

**模型**（`src/model/`）：`User`、`Channel`、`Permission`（位掩码权限，`PermissionManager`）。

### 3.3 nevo_network —— 网络库

8 个 .cpp：`TcpConnection`（C++20 协程）、`UdpSocket`、`SslWrapper`（OpenSSL TLS）、`ConnectionManager`、`PacketRouter`、`NatTraversal`（STUN/打洞/中继）、`VoiceCrypto`（XChaCha20-Poly1305）、`TcpVoiceTunnel`（UDP 不可达时的 TCP 回退隧道）。

### 3.4 端口规划（`.env.example`）

| 端口 | 用途 |
|------|------|
| TCP 24430 | 客户端控制/信令 |
| UDP 24431 | 语音媒体 |
| UDP 24432 | 视频/屏幕共享媒体（= UDP+1） |
| TCP 24433 | 管理接口（JSON-over-TCP ControlServer） |
| TCP 8090 | Web 管理 UI |

默认容量：`NEVO_MAX_USERS=100`、`NEVO_THREADS=4`。

---

## 四、协议设计（Protobuf）

### 4.1 帧格式

- **TCP 控制帧**：`TcpPacketHeader`（`proto/control.proto:9-13`）—— `payload_length`(4B) + `message_type`(4B) + `request_id`(4B，0=单向通知)，共 12 字节头 + Protobuf 载荷。
- **UDP 语音帧**：`VoicePacketHeader`（`proto/voice.proto:7-17`）—— 序列号、sender_id、channel_id、采集时间戳、末帧标记、FEC 冗余大小、nonce、auth_tag、TCP 隧道标记。

### 4.2 消息族

`ControlMessage` oneof 联合（`proto/control.proto:422-475`），覆盖 7 大类：

1. **会话**：登录/登出（含 X25519 公钥与 UDP 端口协商）
2. **频道**：加入/离开/创建/删除/重命名/列表/用户进出广播
3. **语音状态**：PTT、静音、说话状态
4. **NAT/密钥**：STUN 绑定、UDP Ping、密钥轮换（key_epoch 纪元号）
5. **管理**：管理员认证、设管理、踢人、封禁（支持过期时间）、移动用户、改服务器名、服主绑定（bind_key）
6. **聊天/文件**：聊天广播、文件列表/上传/下载/删除
7. **视频**：屏幕共享（源类型/分辨率/音频源）、一对一视频通话（能力协商 `VideoCodecCapability`、profile 更新、挂断原因）

协议设计规范良好：请求/响应均带 `ResultCode`，request_id 支持请求-响应关联，枚举编号与 oneof 字段编号一一对应。

---

## 五、加密方案

| 层面 | 方案 |
|------|------|
| 密钥交换 | X25519 + `crypto_box_seal`（libsodium 密封盒，登录时下发加密会话密钥） |
| 语音/视频加密 | XChaCha20-Poly1305 AEAD（24 字节 nonce，单调递增计数器生成） |
| 密钥轮换 | 每 10 分钟轮换，旧密钥保留 20 秒重叠期用于解密在途包 |
| 控制通道 | OpenSSL TLS 1.2+（可选） |
| 服务端密钥 | X25519 密钥对 |

**Android 端**采用双层策略：JNI/libsodium 优先，失败回退 `javax.crypto`（AES/GCM/NoPadding，12 字节 IV，128 位 tag）。

---

## 六、客户端分析

### 6.1 Android 客户端（主力移动端）

- **架构**：`core`（基础设施）+ `feature`（channel/chat/connection/screen_share/settings/voice，各含 data/ui/domain）+ `ui`（主题/导航）；**Hilt** 依赖注入；Room 数据库 + DataStore 偏好；含 zh-rCN/zh-rTW 国际化。
- **音频**（`NativeAudioEngine.kt` + `native/jni_audio.cpp`）：采集/播放用 Android `AudioRecord`/`AudioTrack`（Java 层），JNI 层仅做 Opus 编解码（32kbps、复杂度 5、带内 FEC、预期丢包 10%）、浮点加权混音、JitterBuffer（容量 64 包）。48kHz 单声道，每帧 1920 采样（40ms）。
- **加密**：见第五节。`VoiceCryptoState` 管理密钥纪元与重叠期。
- **前台服务**保活通话。

### 6.2 其他客户端

| 客户端 | 技术 | 状态 |
|--------|------|------|
| Electron Web 客户端 | JS + Electron 包装 + Python 网关（`webclient/`，V3） | 活跃（唯一 GUI 客户端） |
| 服务端管理 GUI | Qt6 Widgets，含 en/zh_CN/zh_TW 翻译 | 可选构建 |
| Web 管理面板 | HTML/JS + Python 代理（`web/`） | 活跃 |
| React 客户端 | React+TS+Vite（`nevo-client/`） | 早期/实验性 |
| iOS | — | **未发现** |

---

## 七、构建、CI 与测试

### 7.1 构建系统

- CMake ≥ 3.21，C++20 强制；统一输出到 `bin/`、`lib/`。
- 依赖全部经 **vcpkg** `find_package`：Boost(system/lockfree)、SQLite3、libsodium、Opus、Protobuf、OpenSSL；可选 Qt6(Widgets)、RNNoise（降噪）。
- 内嵌第三方：miniaudio（构建期生成 impl 编译为静态库）、spdlog（头文件内嵌于 `src/core/include/spdlog/`）。
- 平台：Windows(MSVC 主目标)、Linux、macOS(CoreAudio)、Android。
- argon2 经 PkgConfig 可选探测（密码哈希）。

### 7.2 CI（`.github/workflows/build.yml`）

4 个独立 job（非矩阵），触发于 main/master/develop 分支：
- `build-linux`（ubuntu-22.04）：apt 装依赖 → Release 构建 → 跑测试
- 其余 job 覆盖 Windows 打包、Docker 镜像推送（GHCR）等（docker 相关文件近期从根目录删除，CI 可能未同步清理）

### 7.3 测试

- `tests/` 27 个 GTest 文件：`audio_tests/`(9)、`network_tests/`(7，含 NAT 穿透、TCP 连接/超时/载荷校验、VoiceCrypto)、`core_tests/`、`server_tests/`。
- ⚠️ `BUILD_TESTING` 默认 **OFF**，本地构建默认不编译测试。
- 另有 `test/` 目录存放集成测试与测试客户端、`src/voice_relay_sim.cpp` 中继模拟器。

---

## 八、发现的问题与风险（按严重程度排序）

### 🔴 高

1. **Android 加密回退实现不完整**：`CryptoManager.kt` 的 `fallbackGenerateKeyPair()`（约第 141-148 行）仅生成随机字节，**并非真正的 X25519**，仅"兼容"用途。若 JNI 库加载失败，密钥交换安全性降级。
2. **TLS 控制通道为可选**：若部署未开启 TLS，明文 TCP 控制信令（含管理员密码 `AdminAuthRequest`、服主 bind_key）存在嗅探风险。建议默认强制 TLS 或改用 Noise/封口传输。
3. **协议文档不一致**：`proto/voice.proto:14-15` 注释仍写 "AES-GCM nonce（12字节）"，实际服务端已实现 XChaCha20-Poly1305（24 字节 nonce），易误导新客户端实现。

### 🟡 中

4. **`docs/` 目录内含整个项目的嵌套副本**（`docs/src/`、`docs/tests/`、`docs/3rdparty/`、`docs/docker-compose.yml` 等），严重污染仓库、增大体积，且可能造成搜索/构建混淆，应立即清理。
5. **构建产物入库**：根目录存在 `build/`、`build_client/`、`build_server_manager/`、`dist*/`、`nevo_server.db`（SQLite 数据库）等，应全部加入 `.gitignore` 并从历史清除。
6. **客户端实现过度分散**：`web/`、`webclient/`、`nevo-client/`（React）三套并行，维护成本高、协议演进时易不同步，建议收敛（PyQt5 桌面客户端已下线，仅保留 Electron V3）。
7. **CMake 重复定义**：顶层 `CMakeLists.txt` 内部自定义了与 `cmake/CompilerWarnings.cmake`、`cmake/PlatformSetup.cmake` 同名但简化的宏，两份实现并存易漂移，应统一 `include()` 模块。

### 🟢 低 / 卫生

8. **根目录文档被清空**：`README.md`、`README_zh.md`、`LICENSE`、`SECURITY_AUDIT_REPORT.md` 等均在 git 中删除未提交——项目当前**没有入口文档与许可证**，对外协作/开源发布受阻。
9. Docker 部署文件（`Dockerfile`、`docker-compose*.yml`、`deploy.sh`）已从根目录删除，但 `.env.example` 仍以 `docker compose up -d` 为使用说明，文档与现状脱节；CI 中 docker job 亦需核对。
10. Android 端 40ms 帧与桌面端 20ms 帧不一致（功能上兼容，但跨端延迟特性不同，建议文档注明）。

---

## 九、总结

NEVO 是一个**架构完整度较高**的自托管加密语音系统：

- ✅ **亮点**：音频管线设计专业（无锁队列、内存池、JitterBuffer、VAD、混音）；NAT 穿透四级级联完善；Protobuf 协议规范（87 种消息、请求关联 ID）；密钥轮换机制（纪元号 + 重叠期）在同类开源项目中少见；Android 端采用 Hilt/Room/JNI 的现代架构。
- ⚠️ **短板**：加密回退路径不完整、TLS 非强制；仓库卫生差（嵌套副本、构建产物入库）；客户端实现分散；测试默认不构建；文档缺失（README/LICENSE 已删）。

**优先建议**：
1. 清理 `docs/` 嵌套副本与构建产物，恢复 README 与 LICENSE；
2. 修复 Android 加密回退（或直接移除回退、强制 libsodium）；
3. 默认启用 TLS 控制通道，修正 `voice.proto` 注释；
4. 收敛 Web 客户端实现，统一 CMake 警告/平台模块；
5. CI 默认开启 `BUILD_TESTING` 并上传覆盖率。
