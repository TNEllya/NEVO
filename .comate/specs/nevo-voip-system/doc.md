# NEVO - 实时 VoIP 系统设计文档

## 1. 项目概述

NEVO 是一个类似 TeamSpeak/Discord 的实时 VoIP 系统，包含服务端、桌面客户端和移动客户端，支持跨平台运行（Windows, Linux, macOS, Android, iOS）。

### 核心目标
- 低延迟（端到端 < 100ms）、高质量的实时语音通信
- 树状频道系统，支持多层级频道与子频道
- 基于组的权限管理
- 跨平台一致性体验

---

## 2. 技术栈

| 领域 | 技术选型 | 说明 |
|------|----------|------|
| 核心语言 | C++20 | 智能指针、RAII、协程、Concepts |
| 信令与控制 | TCP / Boost.Asio | 异步非阻塞，C++20 协程 |
| 音频流传输 | UDP / Boost.Asio | 低延迟，FEC 前向纠错；UDP 不通时 TCP 隧道兜底 |
| NAT 穿透 | STUN/TURN (libnice 或自实现) | NAT 类型检测、打洞；TURN 中继兜底 |
| 音频采集/播放 | miniaudio | 单头文件，跨平台，零依赖 |
| 音频编解码 | Opus Codec | 低延迟（~22.5ms），高质量 |
| 回声消除/降噪 | RNNoise | 轻量级神经网络降噪 |
| 语音加密 | AES-128-GCM (libsodium) | UDP 语音载荷加密，防窃听 |
| 密码哈希 | Argon2id (libargon2) | 抗彩虹表、抗 GPU 暴力破解 |
| 无锁队列 | boost::lockfree::spsc_queue | 音频线程与网络线程间零锁数据传递 |
| UI 框架 | Qt 6 (Widgets + QML) | 桌面+移动一套代码 |
| 数据库 | SQLite3 | 服务端用户/配置存储 |
| 序列化 | Protobuf | 二进制协议，高效紧凑 |
| 构建系统 | CMake 3.21+ | 跨平台构建 |
| 日志 | spdlog | 高性能异步日志 |
| 测试 | Google Test | 单元测试与集成测试 |

### 关键技术决策
- **miniaudio 替代 PortAudio**：miniaudio 是单头文件库，零外部依赖，跨平台支持更优（含 Android/iOS），API 更现代
- **Protobuf 替代 JSON 信令**：二进制序列化效率远高于 JSON，减少带宽占用，强类型安全
- **RNNoise 替代 WebRTC APM**：WebRTC APM 体积庞大且构建复杂，RNNoise 轻量且降噪效果优秀
- **STUN/TURN NAT 穿透**：大多数客户端位于 NAT 之后，必须支持打洞和中继。首选 libnice（ICE/STUN/TURN 一体化）；也可自实现轻量 STUN 协议 + TURN 中继
- **AES-128-GCM 加密语音**：libsodium 的 `crypto_aead_aes256gcm` 提供高性能加密+认证，防止 UDP 明文窃听
- **Argon2id 替代 SHA-256 存储密码**：Argon2id 是密码哈希竞赛胜出算法，抗 GPU/ASIC 暴力破解，内置 salt
- **boost::lockfree::spsc_queue**：专为音频实时线程设计，单生产者单消费者无锁队列，避免音频回调中任何锁竞争
- **TCP 隧道兜底**：当 UDP 完全被阻断（对称型 NAT / 严格防火墙）时，语音数据封装到 TCP 传输，保证可用性

---

## 3. 项目目录结构

```
NEVO/
├── CMakeLists.txt                      # 顶层 CMake：项目定义、依赖查找
├── cmake/
│   ├── CompilerWarnings.cmake          # 编译器警告配置
│   ├── FindOpus.cmake                  # Opus 查找模块
│   ├── FindRNNoise.cmake               # RNNoise 查找模块
│   └── PlatformSetup.cmake             # 平台特定配置
├── 3rdparty/
│   ├── miniaudio/                      # 单头文件音频库
│   ├── spdlog/                         # 日志库
│   ├── protobuf/                       # Protobuf 运行时
│   ├── libsodium/                      # 加密库 (AES-GCM, key exchange)
│   ├── libnice/                        # ICE/STUN/TURN (NAT穿透)
│   ├── argon2/                         # 密码哈希库
│   └── googletest/                     # 测试框架
├── proto/                              # Protobuf 协议定义
│   ├── common.proto                    # 公共消息类型
│   ├── control.proto                   # TCP 信令消息
│   └── voice.proto                     # UDP 语音包头
├── src/
│   ├── CMakeLists.txt
│   ├── core/                           # 共享核心库: nevo_core
│   │   ├── CMakeLists.txt
│   │   ├── include/nevo/core/
│   │   │   ├── common/
│   │   │   │   ├── Types.h             # 基础类型定义（UserId, ChannelId等）
│   │   │   │   ├── Result.h            # Result<T> 错误处理
│   │   │   │   └── Logger.h            # 日志接口
│   │   │   ├── protocol/
│   │   │   │   ├── PacketTypes.h       # 包类型枚举
│   │   │   │   └── PacketCodec.h       # 包编解码
│   │   │   ├── audio/
│   │   │   │   ├── AudioEngine.h       # 音频引擎核心
│   │   │   │   ├── OpusEncoder.h       # Opus 编码器封装
│   │   │   │   ├── OpusDecoder.h       # Opus 解码器封装
│   │   │   │   ├── JitterBuffer.h      # 抖动缓冲区
│   │   │   │   ├── AudioMixer.h        # 混音器
│   │   │   │   ├── VoiceActivity.h     # VAD / PTT
│   │   │   │   ├── AudioMemoryPool.h   # 音频帧内存池（避免实时回调中 malloc）
│   │   │   │   └── Resampler.h         # 重采样器（蓝牙 16k→48k 适配）
│   │   │   └── model/
│   │   │       ├── User.h              # 用户模型
│   │   │       ├── Channel.h           # 频道模型（树状）
│   │   │       └── Permission.h        # 权限模型
│   │   └── src/
│   │       ├── common/
│   │       ├── protocol/
│   │       ├── audio/
│   │       └── model/
│   ├── network/                        # 网络库: nevo_network
│   │   ├── CMakeLists.txt
│   │   ├── include/nevo/network/
│   │   │   ├── TcpConnection.h         # TCP 连接封装
│   │   │   ├── UdpSocket.h             # UDP Socket 封装
│   │   │   ├── NatTraversal.h          # STUN/TURN NAT 穿透
│   │   │   ├── VoiceCrypto.h           # UDP 语音 AES-GCM 加解密
│   │   │   ├── TcpVoiceTunnel.h        # TCP 语音隧道（UDP不通时兜底）
│   │   │   ├── ConnectionManager.h     # 连接管理器
│   │   │   ├── PacketRouter.h          # 包路由
│   │   │   └── SslWrapper.h            # TLS 包装
│   │   └── src/
│   │       ├── TcpConnection.cpp
│   │       ├── UdpSocket.cpp
│   │       ├── NatTraversal.cpp
│   │       ├── VoiceCrypto.cpp
│   │       ├── TcpVoiceTunnel.cpp
│   │       ├── ConnectionManager.cpp
│   │       ├── PacketRouter.cpp
│   │       └── SslWrapper.cpp
│   ├── server/                         # 服务端可执行文件: nevo_server
│   │   ├── CMakeLists.txt
│   │   ├── include/nevo/server/
│   │   │   ├── ServerCore.h            # 服务端核心
│   │   │   ├── ClientSession.h         # 客户端会话
│   │   │   ├── ChannelManager.h        # 频道管理
│   │   │   ├── PermissionManager.h     # 权限管理
│   │   │   ├── AudioRelay.h            # 音频转发
│   │   │   └── Database.h              # SQLite 数据库
│   │   └── src/
│   │       ├── main.cpp
│   │       ├── ServerCore.cpp
│   │       ├── ClientSession.cpp
│   │       ├── ChannelManager.cpp
│   │       ├── PermissionManager.cpp
│   │       ├── AudioRelay.cpp
│   │       └── Database.cpp
│   ├── client/                         # 客户端核心库: nevo_client
│   │   ├── CMakeLists.txt
│   │   ├── include/nevo/client/
│   │   │   ├── ClientCore.h            # 客户端核心
│   │   │   ├── AudioInput.h            # 麦克风采集 + Opus 编码
│   │   │   ├── AudioOutput.h           # Opus 解码 + 扬声器播放
│   │   │   └── NetworkManager.h        # 客户端网络管理
│   │   └── src/
│   │       ├── ClientCore.cpp
│   │       ├── AudioInput.cpp
│   │       ├── AudioOutput.cpp
│   │       └── NetworkManager.cpp
│   └── ui/                             # Qt 6 界面: nevo_ui
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── nevo/ui/
│       │       ├── MainWindow.h
│       │       ├── ChannelTreeModel.h
│       │       ├── UserListModel.h
│       │       ├── AudioSettingsWidget.h
│       │       └── ConnectionBar.h
│       ├── src/
│       │   ├── main.cpp
│       │   ├── MainWindow.cpp
│       │   ├── ChannelTreeModel.cpp
│       │   ├── UserListModel.cpp
│       │   ├── AudioSettingsWidget.cpp
│       │   └── ConnectionBar.cpp
│       └── resources/
│           ├── icons/
│           └── themes/
├── mobile/                             # 移动端原生层
│   ├── android/
│   │   ├── AndroidManifest.xml         # 后台音频服务权限声明
│   │   ├── src/
│   │   │   └── NevoAudioService.kt     # Android 后台音频前台服务
│   │   └── res/
│   └── ios/
│       ├── Info.plist                  # 后台音频模式配置
│       └── NevoAudioSession.swift      # AVAudioSession 配置 + 蓝牙路由管理
├── tests/
│   ├── CMakeLists.txt
│   ├── core_tests/
│   ├── network_tests/
│   ├── audio_tests/
│   └── server_tests/
└── docs/
```

---

## 4. 核心类架构设计

### 4.1 类关系描述

```
┌─────────────────────────────────────────────────────────────────────┐
│                         服务端 (nevo_server)                         │
│                                                                     │
│  ServerCore ──┬── ChannelManager (树状频道管理)                       │
│               ├── PermissionManager (基于组的权限)                     │
│               ├── AudioRelay (UDP音频转发)                            │
│               ├── Database (SQLite持久化)                             │
│               ├── VoiceCrypto (语音包加解密)                           │
│               └── vector<shared_ptr<ClientSession>>                   │
│                        │                                             │
│              ClientSession (每个客户端连接)                             │
│               ├── TcpConnection (信令通道)                             │
│               ├── UdpEndpoint (语音通道)                               │
│               ├── NatInfo (NAT类型、公网端点)                           │
│               ├── User (用户状态)                                      │
│               └── Channel* (当前频道)                                  │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                         客户端 (nevo_client)                         │
│                                                                     │
│  ClientCore ──┬── NetworkManager (TCP/UDP通信)                        │
│               ├── AudioInput (采集+编码)                               │
│               ├── AudioOutput (解码+播放)                              │
│               ├── AudioEngine (音频流水线管理)                          │
│               └── VoiceActivity (VAD/PTT)                             │
│                                                                     │
│  NetworkManager ──┬── TcpConnection (信令)                             │
│                   ├── UdpSocket (语音)                                 │
│                   ├── NatTraversal (STUN打洞/TURN中继)                  │
│                   ├── VoiceCrypto (AES-GCM加解密)                      │
│                   └── TcpVoiceTunnel (UDP不通时TCP兜底)                 │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                       共享核心 (nevo_core)                            │
│                                                                     │
│  AudioEngine ──┬── OpusEncoder (编码)                                 │
│                ├── OpusDecoder (解码)                                  │
│                ├── JitterBuffer (抖动缓冲)                              │
│                ├── AudioMixer (混音)                                   │
│                ├── VoiceActivity (语音活动检测)                         │
│                ├── AudioMemoryPool (帧内存池，避免实时回调中new)           │
│                └── Resampler (重采样，蓝牙16k→48k适配)                   │
│                                                                     │
│  Protocol ────── PacketCodec (包编解码，基于Protobuf)                    │
│  Model ───────── User, Channel, Permission                            │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.2 核心类详细设计

#### ServerCore（服务端核心）

```cpp
class ServerCore {
public:
    ServerCore(boost::asio::io_context& ioc, uint16_t tcp_port, uint16_t udp_port);
    ~ServerCore() = default;

    // 启动服务器（非阻塞）
    void start();
    // 优雅关闭
    void shutdown();

    // 客户端管理
    void onClientConnected(std::shared_ptr<ClientSession> session);
    void onClientDisconnected(std::shared_ptr<ClientSession> session);

    // 频道操作
    Result<ChannelId> createChannel(ChannelId parent, const std::string& name);
    Result<void> removeChannel(ChannelId id);
    Result<void> moveUserToChannel(UserId user, ChannelId channel);

private:
    // 异步接受TCP连接（C++20协程）
    boost::asio::awaitable<void> acceptTcpLoop();
    // 异步接收UDP语音包
    boost::asio::awaitable<void> receiveUdpLoop();

    boost::asio::io_context& io_context_;
    tcp::acceptor tcp_acceptor_;
    udp::socket udp_socket_;

    std::unique_ptr<ChannelManager> channel_mgr_;
    std::unique_ptr<PermissionManager> perm_mgr_;
    std::unique_ptr<AudioRelay> audio_relay_;
    std::unique_ptr<Database> database_;

    // 线程安全：所有对 sessions_ 的操作通过 strand 序列化
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::unordered_map<UserId, std::shared_ptr<ClientSession>> sessions_;
};
```

#### ClientSession（客户端会话，服务端侧）

```cpp
class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    ClientSession(tcp::socket socket, ServerCore& server);
    ~ClientSession();

    void start();  // 开始读取循环
    void disconnect();

    // 发送控制消息
    boost::asio::awaitable<void> sendControl(const ControlMessage& msg);

    // 状态访问
    UserId userId() const;
    ChannelId currentChannel() const;
    bool isAuthenticated() const;

private:
    boost::asio::awaitable<void> readLoop();
    void handleControlMessage(const ControlMessage& msg);

    // 信令处理
    void handleLogin(const LoginRequest& req);
    void handleJoinChannel(const JoinChannelRequest& req);
    void handleLeaveChannel();
    void handleLogout();

    tcp::socket socket_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    ServerCore& server_;

    std::optional<udp::endpoint> udp_endpoint_;  // 客户端UDP端点
    User user_;
    ChannelId current_channel_ = INVALID_CHANNEL_ID;
    bool authenticated_ = false;
};
```

#### AudioEngine（音频引擎，客户端侧）

```cpp
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // 初始化/销毁 miniaudio 设备
    Result<void> initialize(uint32_t sample_rate = 48000, uint32_t frames_per_buffer = 960);
    void shutdown();

    // 音频输入回调（采集 → 编码 → 发送）
    // 注意：回调在 miniaudio 实时线程中执行，不得做任何 malloc/lock
    void setInputCallback(std::function<void(const float* pcm, uint32_t frames)> callback);
    // 音频输出：接收远端编码数据 → 解码 → 混音 → 播放
    void queueAudioData(UserId from, const uint8_t* opus_data, uint32_t size);

    // VAD / PTT 控制
    void setVadEnabled(bool enabled);
    void setPttEnabled(bool enabled);
    void setPttActive(bool active);  // 按键状态

    // 音量控制
    void setInputGain(float gain);    // 0.0 ~ 2.0
    void setOutputVolume(float vol);  // 0.0 ~ 2.0

    // 蓝牙采样率适配（移动端）
    void onDeviceSampleRateChanged(uint32_t new_sample_rate);

private:
    // miniaudio 回调（静态）- 严禁在回调中做任何堆分配或加锁
    static void maInputCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
    static void maOutputCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);

    ma_device input_device_;
    ma_device output_device_;
    ma_context context_;

    std::unique_ptr<OpusEncoder> encoder_;
    std::unordered_map<UserId, std::unique_ptr<OpusDecoder>> decoders_;
    std::unique_ptr<JitterBuffer> jitter_buffer_;
    std::unique_ptr<AudioMixer> mixer_;
    std::unique_ptr<VoiceActivity> vad_;
    std::unique_ptr<AudioMemoryPool> memory_pool_;    // 预分配帧内存池
    std::unique_ptr<Resampler> resampler_;             // 蓝牙采样率转换

    // 无锁 FIFO：音频实时线程 → 网络线程
    // spsc_queue 保证：生产者（miniaudio回调）和消费者（网络线程）零锁
    using AudioFrame = std::array<float, 960>;  // 单帧 20ms @ 48kHz mono
    boost::lockfree::spsc_queue<AudioFrame> input_fifo_{64};   // 采集→编码
    boost::lockfree::spsc_queue<AudioFrame> output_fifo_{64};  // 解码→播放

    uint32_t sample_rate_ = 48000;
    uint32_t frames_per_buffer_ = 960;  // 20ms @ 48kHz
    uint32_t device_sample_rate_ = 48000;  // 实际设备采样率（蓝牙可能为 16000）

    std::function<void(const float* pcm, uint32_t frames)> input_callback_;
};
```

#### NetworkManager（客户端网络管理）

```cpp
class NetworkManager {
public:
    NetworkManager(boost::asio::io_context& ioc);
    ~NetworkManager() = default;

    // TCP 连接管理
    Result<void> connect(const std::string& host, uint16_t tcp_port);
    void disconnect();

    // UDP 绑定 + NAT 穿透（连接TCP后自动建立）
    // 内部流程：绑定本地UDP → STUN探测NAT类型 → 尝试打洞 → 失败则启用TURN中继
    Result<void> establishUdpChannel(uint16_t local_udp_port);

    // 发送控制消息（TCP）
    boost::asio::awaitable<void> sendControl(const ControlMessage& msg);

    // 发送语音数据（优先UDP，UDP不通时走TCP隧道）
    void sendVoicePacket(const uint8_t* data, uint32_t size);

    // 回调注册
    void setOnControlMessage(std::function<void(const ControlMessage&)> callback);
    void setOnVoicePacket(std::function<void(const uint8_t*, uint32_t, UserId)> callback);
    void setOnDisconnected(std::function<void()> callback);

    // NAT 状态查询
    enum class NatType { Open, FullCone, Restricted, PortRestricted, Symmetric, Blocked };
    NatType detectedNatType() const;
    bool isUdpAvailable() const;  // UDP 是否可用（打洞成功或开放NAT）

private:
    boost::asio::awaitable<void> tcpReadLoop();
    boost::asio::awaitable<void> udpReceiveLoop();

    // NAT 穿透流程
    boost::asio::awaitable<void> stunProbe();       // STUN 探测 NAT 类型
    boost::asio::awaitable<void> holePunching();     // 尝试 UDP 打洞
    void fallbackToTurn();                           // TURN 中继兜底
    void fallbackToTcpTunnel();                      // TCP 隧道兜底（最终兜底）

    boost::asio::io_context& io_context_;
    std::unique_ptr<TcpConnection> tcp_conn_;
    std::unique_ptr<UdpSocket> udp_socket_;
    std::unique_ptr<NatTraversal> nat_traversal_;
    std::unique_ptr<VoiceCrypto> voice_crypto_;     // UDP 语音加解密
    std::unique_ptr<TcpVoiceTunnel> tcp_tunnel_;    // TCP 语音隧道

    NatType nat_type_ = NatType::Blocked;
    bool udp_available_ = false;
};
```

---

## 5. 通信协议设计

### 5.1 双通道架构 + NAT 穿透

```
客户端 ←──TCP/TLS──→ 服务端    信令与控制（登录、频道操作、权限、用户列表）
客户端 ←──UDP─────→ 服务端    音频流传输（低延迟，AES-GCM加密，允许丢包）

NAT 穿透流程（自动选择最优路径）：
  1. 客户端通过 STUN 服务器探测自身 NAT 类型
  2. 如果是 开放/锥形 NAT → UDP 直连（最优延迟）
  3. 如果是 对称型 NAT → TURN 中继（增加延迟，但保证连通）
  4. 如果 UDP 完全被阻断 → TCP 隧道兜底（延迟最高，但保证可用性）

语音包加密：
  - UDP 载荷使用 AES-128-GCM 加密 + 认证
  - 密钥通过 TLS TCP 通道在登录时协商（Diffie-Hellman 密钥交换）
  - 每个 UDP 会话使用独立密钥，定期轮换
```

### 5.2 Protobuf 协议定义

#### common.proto
```protobuf
syntax = "proto3";
package nevo.common;

message UserId { uint64 id = 1; }
message ChannelId { uint64 id = 1; }

enum ResultCode {
    OK = 0;
    ERROR_UNKNOWN = 1;
    ERROR_AUTH_FAILED = 2;
    ERROR_PERMISSION_DENIED = 3;
    ERROR_CHANNEL_NOT_FOUND = 4;
    ERROR_ALREADY_IN_CHANNEL = 5;
    ERROR_INVALID_REQUEST = 6;
}

enum UserStatus {
    OFFLINE = 0;
    ONLINE = 1;
    AWAY = 2;
    MUTED = 3;
    DEAFENED = 4;
}

message UserInfo {
    uint64 id = 1;
    string username = 2;
    UserStatus status = 3;
    bool muted = 4;
    bool deafened = 5;
    uint32 group_id = 6;
}

message ChannelInfo {
    uint64 id = 1;
    string name = 2;
    uint64 parent_id = 3;
    repeated ChannelInfo children = 4;
    repeated UserInfo users = 5;
}
```

#### control.proto（TCP 信令）
```protobuf
syntax = "proto3";
package nevo.control;

import "common.proto";

// TCP 包头：4字节长度 + 2字节类型 + 4字节请求ID + payload
message TcpPacketHeader {
    uint32 payload_length = 1;
    uint16 message_type = 2;
    uint32 request_id = 3;       // 请求-响应关联ID（0=单向通知）
                                 // 客户端发起请求时生成唯一ID
                                 // 服务端响应时回填相同ID
                                 // 支持乱序响应匹配
}

// === 客户端 → 服务端 ===

message LoginRequest {
    string username = 1;
    // 密码传输：客户端使用 SRP (Secure Remote Password) 或
    // 简单方案：SHA-256(username + password) 传输，服务端用 Argon2id 存储
    // 注意：此处传输的是认证凭证，非最终存储格式
    bytes auth_credential = 2;
    // 客户端支持的语音加密密钥交换方式
    repeated string key_exchange_methods = 3;  // e.g. "x25519", "ecdh-p256"
}

message JoinChannelRequest {
    uint64 channel_id = 1;
}

message LeaveChannelRequest {}

message CreateChannelRequest {
    uint64 parent_id = 1;
    string name = 2;
}

message DeleteChannelRequest {
    uint64 channel_id = 1;
}

message PttToggle {
    bool active = 1;
}

message UserMuteToggle {
    bool muted = 1;
}

// === 服务端 → 客户端 ===

message LoginResponse {
    nevo.common.ResultCode result = 1;
    nevo.common.UserInfo user_info = 2;
    string session_token = 3;
    // 语音加密密钥交换：服务端公钥，用于后续 UDP 语音加密密钥协商
    bytes server_public_key = 4;
    string key_exchange_method = 5;  // 实际选用的密钥交换方式
}

message ChannelListUpdate {
    repeated nevo.common.ChannelInfo channels = 1;
}

message UserJoinedChannel {
    nevo.common.UserInfo user = 1;
    uint64 channel_id = 2;
}

message UserLeftChannel {
    uint64 user_id = 1;
    uint64 channel_id = 2;
}

message UserSpeaking {
    uint64 user_id = 1;
    bool speaking = 2;
}

message ServerMessage {
    string text = 1;
}

// === NAT 穿透相关 ===

message StunBindRequest {
    uint32 transaction_id = 1;  // STUN 事务ID
}

message StunBindResponse {
    uint32 transaction_id = 1;
    bytes mapped_address = 2;   // 客户端公网地址（IP:Port）
    uint32 nat_type = 3;        // 检测到的 NAT 类型
}

message UdpPingRequest {
    uint32 sequence = 1;        // UDP 连通性探测序列号
    bytes client_udp_key = 2;   // 客户端 UDP 加密初始密钥材料
}

message UdpPingResponse {
    uint32 sequence = 1;
    bool udp_reachable = 2;     // 服务端是否收到客户端的 UDP 包
}

// 联合消息类型
message ControlMessage {
    oneof payload {
        LoginRequest login_request = 1;
        LoginResponse login_response = 2;
        JoinChannelRequest join_channel = 3;
        LeaveChannelRequest leave_channel = 4;
        CreateChannelRequest create_channel = 5;
        DeleteChannelRequest delete_channel = 6;
        ChannelListUpdate channel_list = 7;
        UserJoinedChannel user_joined = 8;
        UserLeftChannel user_left = 9;
        UserSpeaking user_speaking = 10;
        PttToggle ptt_toggle = 11;
        UserMuteToggle mute_toggle = 12;
        ServerMessage server_message = 13;
        StunBindRequest stun_bind_request = 14;
        StunBindResponse stun_bind_response = 15;
        UdpPingRequest udp_ping_request = 16;
        UdpPingResponse udp_ping_response = 17;
    }
}
```

#### voice.proto（UDP 语音包头）
```protobuf
syntax = "proto3";
package nevo.voice;

message VoicePacketHeader {
    uint32 sequence_number = 1;   // 序列号（用于排序和丢包检测）
    uint64 sender_id = 2;         // 发送者 UserId
    uint64 channel_id = 3;        // 目标频道
    uint32 timestamp = 4;         // 采集时间戳 (ms)
    bool last_frame = 5;          // 是否为语音末尾帧
    uint32 fec_payload_size = 6;  // FEC 冗余数据大小（0=无FEC）
    // 加密认证信息
    bytes nonce = 7;              // AES-GCM nonce（12字节），每包唯一
    bytes auth_tag = 8;           // AES-GCM 认证标签（16字节）
    // 传输通道标识
    bool tcp_tunnel = 9;          // true=此包通过TCP隧道传输（用于延迟估算补偿）
}
```

### 5.3 UDP 语音包格式

```
┌──────────────────────────────────┬──────────────────────────────────────┐
│  VoicePacketHeader (变长 Protobuf) │  加密后的 Opus 音频数据                │
│  序列号 | 发送者ID | 频道ID | ...    │  AES-128-GCM(ciphertext + auth_tag)   │
│  nonce | auth_tag | tcp_tunnel     │  (含可选 FEC 冗余数据)                 │
└──────────────────────────────────┴──────────────────────────────────────┘

加密层设计：
  - 明文区域：VoicePacketHeader（含 nonce, auth_tag）——必须明文以便路由
  - 密文区域：Opus 编码音频 + FEC 冗余数据
  - 认证范围：AES-GCM AAD = VoicePacketHeader 明文字段，确保头部不被篡改
  - 密钥生命周期：每次登录通过 DH 协商产生会话密钥，每 10 分钟轮换
```

- 头部使用 Protobuf 编码，紧凑高效
- 载荷为 Opus 编码后的音频帧（AES-GCM 加密）
- FEC：每隔 N 帧携带前 N-1 帧的异或冗余，丢包时可恢复
- 当 UDP 不可用时，整个包（头+加密载荷）封装到 TCP 隧道传输

---

## 6. 核心数据流

### 6.1 语音发送流程（客户端）

```
麦克风采集 → miniaudio回调（实时线程）→ PCM float32
    → 输入增益调节
    → [蓝牙16k设备] Resampler 重采样到 48kHz
    → VAD检测（语音激活检测）
    → [如果VAD通过或PTT激活]
        → PCM帧写入 spsc_queue（无锁FIFO，零分配）
        ────── 实时线程 / 网络线程 分界 ──────
        → 网络线程从 spsc_queue 读取 PCM帧
        → 从 AudioMemoryPool 获取预分配 Opus 输出缓冲区
        → Opus编码 (PCM → Opus bytes)
        → [可选] 计算FEC冗余
        → AES-GCM 加密 Opus 载荷（nonce 从计数器生成）
        → 构造VoicePacketHeader
        → [UDP可用] UDP发送到服务器
        → [UDP不通] 封装到TCP隧道发送
```

### 6.2 语音接收流程（客户端）

```
[UDP接收 或 TCP隧道接收] → AES-GCM 解密 + 认证（失败则丢弃）
    → 解析VoicePacketHeader + Opus数据
    → 按sender_id分发到对应JitterBuffer
    → JitterBuffer排序、去抖动
    → [如果丢包] 使用FEC恢复或PLC补偿
    → 从 AudioMemoryPool 获取预分配解码缓冲区
    → Opus解码 (Opus bytes → PCM float32)
    → 送入AudioMixer（混合多个说话者）
    → 输出音量调节
    → [蓝牙16k设备] Resampler 重采样到设备采样率
    → PCM帧写入 output spsc_queue（无锁FIFO）
    ────── 网络线程 / 实时线程 分界 ──────
    → miniaudio播放回调从 output spsc_queue 读取 → 扬声器播放
```

### 6.3 服务端音频转发流程

```
UDP接收（或TCP隧道接收）→ AES-GCM 解密验证
    → 解析VoicePacketHeader
    → 查找sender_id对应的频道
    → 为每个目标用户重新 AES-GCM 加密（每个用户独立会话密钥）
    → [目标用户UDP可达] 转发加密语音包到UDP端点
    → [目标用户UDP不通] 通过TCP隧道转发
    → 同时通过TCP通知频道内用户"谁在说话"
```

---

## 7. 频道系统设计

### 7.1 树状频道模型

```
Root
├── Lobby（默认频道，用户登录后自动进入）
├── Gaming
│   ├── Valorant
│   ├── CS2
│   └── Minecraft
├── Music
│   ├── Listening Room
│   └── Jam Session
└── AFK（长时间无活动自动移入）
```

### 7.2 Channel 类

```cpp
class Channel {
public:
    Channel(ChannelId id, const std::string& name, Channel* parent = nullptr);

    ChannelId id() const;
    const std::string& name() const;
    Channel* parent() const;
    const std::vector<Channel*>& children() const;
    const std::vector<UserId>& users() const;

    void addChild(Channel* child);
    void removeChild(Channel* child);
    void addUser(UserId uid);
    void removeUser(UserId uid);

private:
    ChannelId id_;
    std::string name_;
    Channel* parent_;
    std::vector<Channel*> children_;
    std::vector<UserId> users_;
};
```

---

## 8. 权限系统设计

### 8.1 权限组

| 组 | 权限 |
|----|------|
| Admin | 所有权限：创建/删除频道、踢人、修改权限、修改服务器配置 |
| Channel Admin | 管理自己创建的频道：子频道创建/删除、踢出频道内用户 |
| User | 加入频道、说话、创建临时子频道 |
| Guest | 加入频道、只听（不能说话） |

### 8.2 权限位掩码

```cpp
enum class Permission : uint64_t {
    JoinChannel      = 1ULL << 0,
    Speak            = 1ULL << 1,
    CreateChannel    = 1ULL << 2,
    DeleteChannel    = 1ULL << 3,
    KickUser         = 1ULL << 4,
    MoveUser         = 1ULL << 5,
    MuteUser         = 1ULL << 6,
    ManagePermission = 1ULL << 7,
    ServerAdmin      = 1ULL << 8,
};

struct PermissionGroup {
    uint32_t group_id;
    std::string name;
    uint64_t permissions;  // 位掩码
};
```

---

## 9. 数据库设计（SQLite）

### 表结构

```sql
CREATE TABLE users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT UNIQUE NOT NULL,
    password_hash   TEXT NOT NULL,          -- Argon2id 哈希（含内置salt和参数）
    argon2_params   TEXT NOT NULL,          -- JSON: {"m":65536,"t":3,"p":4} 内存/迭代/并行
    group_id        INTEGER DEFAULT 3,      -- 默认 User 组
    created_at      TEXT DEFAULT (datetime('now')),
    last_login      TEXT
);

-- 密码存储说明：
-- 使用 Argon2id(password, random_salt, m=64MB, t=3, p=4)
-- Argon2id 内置 salt，抗彩虹表和 GPU 暴力破解
-- argon2_params 字段允许未来调整参数而不破坏已有记录

CREATE TABLE channels (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    parent_id   INTEGER REFERENCES channels(id) ON DELETE SET NULL,
    created_by  INTEGER REFERENCES users(id),
    is_permanent BOOLEAN DEFAULT 1,
    created_at  TEXT DEFAULT (datetime('now'))
);

CREATE TABLE server_config (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL
);

CREATE TABLE bans (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER REFERENCES users(id),
    ip_address  TEXT,
    reason      TEXT,
    expires_at  TEXT
);
```

---

## 10. 错误处理策略

### Result<T> 模式

```cpp
template<typename T>
class Result {
public:
    // 成功构造
    Result(T value) : value_(std::move(value)), error_(std::nullopt) {}
    // 错误构造
    Result(Error error) : value_(std::nullopt), error_(std::move(error)) {}

    bool ok() const { return error_.has_value() == false; }
    explicit operator bool() const { return ok(); }

    const T& value() const& { return value_.value(); }
    T& value() & { return value_.value(); }

    const Error& error() const { return error_.value(); }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

class Error {
public:
    Error(ResultCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ResultCode code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    ResultCode code_;
    std::string message_;
};
```

### 异常安全保证
- 所有网络 I/O 使用 `as_tuple(use_awaitable)` 避免 异常 在正常断连时抛出
- 资源管理严格遵循 RAII，使用 `std::unique_ptr` / `std::shared_ptr`
- Opus 编解码器句柄使用自定义 deleter 封装在 `unique_ptr` 中

---

## 11. 线程模型

### 服务端
```
io_context (单实例)
├── Thread Pool (N = hardware_concurrency)
│   ├── Strand-per-ClientSession (TCP 读写序列化)
│   └── UDP Socket (voice relay，使用strand保护内部状态)
└── 定时器 (心跳检测、AFK检查等)
```

### 客户端
```
io_context (单实例, 独立线程运行)
├── TCP 连接 (strand保护)
├── UDP Socket (strand保护)
├── NatTraversal (STUN/TURN, strand保护)
├── VoiceCrypto (纯计算，线程安全)
├── TcpVoiceTunnel (复用TCP连接，strand保护)
└── 定时器 (重连、心跳、密钥轮换)

UI 线程 (Qt 主线程)
├── 通过信号槽与网络线程通信

音频实时线程 (miniaudio 内部线程)
├── 绝对禁止：malloc/new, mutex, 任何阻塞调用
├── 允许操作：浮点运算、内存池预分配块读写、spsc_queue push/pop
├── 输入回调：采集PCM → 增益 → VAD → push到input_fifo
└── 输出回调：从output_fifo pop → 写入播放缓冲

数据流分界（零锁跨线程）：
  miniaudio实时线程 ──[spsc_queue]──→ 网络线程
  网络线程 ──[spsc_queue]──→ miniaudio实时线程
  网络线程 ──[Qt信号槽]──→ UI线程

AudioMemoryPool 设计：
  - 初始化时预分配 N 个固定大小内存块（如 64 个 4KB Opus缓冲区）
  - acquire(): 从空闲链表取一块（O(1)，无锁原子操作）
  - release(): 归还到空闲链表（O(1)）
  - 实时回调只做 acquire/release，永不触发系统分配器
```

---

## 12. 边界条件与异常处理

| 场景 | 处理策略 |
|------|----------|
| TCP 连接断开 | 客户端自动重连（指数退避），服务端清理会话 |
| UDP 丢包 | FEC 恢复前帧，不可恢复时 Opus PLC 补偿 |
| UDP 乱序 | JitterBuffer 按序列号重排，超时丢弃 |
| 对称型 NAT / UDP 阻断 | 自动切换到 TURN 中继或 TCP 隧道，UI 提示延迟可能增加 |
| STUN 服务器不可达 | 跳过打洞，直接尝试 UDP 发送（乐观策略），失败后走 TCP 隧道 |
| AES-GCM 认证失败 | 丢弃该语音包，记录日志（可能是重放攻击或密钥不同步） |
| 密钥轮换期间丢包 | 保留旧密钥 2 个窗口期，新密钥失败时回退旧密钥尝试 |
| 客户端崩溃 | 服务端心跳超时（10s）后清理会话 |
| 音频设备不可用 | 优雅降级，通知用户，不崩溃 |
| 蓝牙耳机采样率切换 | miniaudio 回调通知 → Resampler 实时切换 → 无爆音过渡 |
| App 切到后台（移动端） | Android: 前台服务保活; iOS: AVAudioSession 配置后台模式 |
| Opus 编解码错误 | 记录日志，使用静音填充，不断开连接 |
| SQLite 数据库锁 | WAL 模式 + 重试机制 |
| 同时大量用户说话 | AudioMixer 自动限幅，防止爆音 |
| 音频内存池耗尽 | 丢弃最旧帧，优先保证实时性而非完整性 |

---

## 13. 预期成果

1. **可编译的跨平台项目框架**：CMake 构建系统，Windows/Linux/macOS 可构建
2. **异步 TCP 服务器**：基于 Boost.Asio C++20 协程，处理客户端登录、频道操作
3. **NAT 穿透系统**：STUN 探测 → UDP 打洞 → TURN 中继 → TCP 隧道，四级降级策略
4. **UDP 音频转发**：服务端接收并转发加密语音包到同频道用户
5. **完整音频管线**：miniaudio 采集/播放 → Opus 编解码 → JitterBuffer → AudioMixer → Resampler
6. **实时音频安全**：AudioMemoryPool 预分配 + spsc_queue 无锁传递，实时回调零分配零锁
7. **语音加密**：AES-128-GCM 端到端加密，DH 密钥协商，定期轮换
8. **Protobuf 信令协议**：强类型、高效，含 request_id 请求-响应关联
9. **Qt 6 客户端界面**：频道树、用户列表、连接状态栏
10. **移动端适配**：Android 前台服务保活、iOS 后台音频模式、蓝牙采样率自适应
11. **SQLite 持久化**：Argon2id 密码哈希、用户账户、频道配置、服务器设置
12. **权限系统**：基于组的权限控制，位掩码实现

---

## 14. 受影响文件总览

### 新建文件

| 类别 | 文件路径 | 说明 |
|------|----------|------|
| 构建 | `CMakeLists.txt` | 顶层 CMake |
| 构建 | `cmake/*.cmake` | 编译配置模块 |
| 协议 | `proto/*.proto` | Protobuf 协议定义 |
| 核心库 | `src/core/include/nevo/core/**` | 核心头文件 |
| 核心库 | `src/core/src/**` | 核心实现 |
| 音频 | `src/core/include/nevo/core/audio/AudioMemoryPool.h` | 音频帧预分配内存池 |
| 音频 | `src/core/include/nevo/core/audio/Resampler.h` | 重采样器（蓝牙适配） |
| 网络 | `src/network/include/nevo/network/**` | 网络头文件 |
| 网络 | `src/network/include/nevo/network/NatTraversal.h` | STUN/TURN NAT 穿透 |
| 网络 | `src/network/include/nevo/network/VoiceCrypto.h` | AES-GCM 语音加解密 |
| 网络 | `src/network/include/nevo/network/TcpVoiceTunnel.h` | TCP 语音隧道 |
| 网络 | `src/network/src/**` | 网络实现 |
| 服务端 | `src/server/include/nevo/server/**` | 服务端头文件 |
| 服务端 | `src/server/src/**` | 服务端实现 |
| 客户端 | `src/client/include/nevo/client/**` | 客户端头文件 |
| 客户端 | `src/client/src/**` | 客户端实现 |
| 界面 | `src/ui/include/nevo/ui/**` | UI 头文件 |
| 界面 | `src/ui/src/**` | UI 实现 |
| 移动端 | `mobile/android/**` | Android 原生层（前台服务、权限） |
| 移动端 | `mobile/ios/**` | iOS 原生层（AVAudioSession、蓝牙路由） |
| 测试 | `tests/**` | 单元测试与集成测试 |

---

## 15. NAT 穿透详细设计

### 15.1 穿透策略降级链

```
客户端连接流程：
  1. TCP/TLS 连接到服务器（认证 + 信令通道建立）
  2. 登录成功后，通过 TCP 通道交换 STUN 服务器地址
  3. 客户端向 STUN 服务器发送 Binding Request
     → 获取公网映射地址 (mapped address)
     → 判定 NAT 类型（Full Cone / Restricted / Port Restricted / Symmetric）
  4. 将 NAT 类型 + 公网地址通过 TCP 通知服务器
  5. 尝试 UDP 直连：
     客户端 → 服务器 UDP 端口发送 UdpPingRequest
     服务器收到后通过 TCP 回复 UdpPingResponse(udp_reachable=true)
  6. 根据结果选择路径：
     ┌─────────────────────────────────────────────────────┐
     │ NAT 类型           │ 选择的路径                       │
     ├─────────────────────────────────────────────────────┤
     │ Open / Full Cone   │ UDP 直连（最优）                  │
     │ Restricted         │ UDP 打洞（服务端辅助）              │
     │ Port Restricted    │ UDP 打洞（需要双方同时发）          │
     │ Symmetric          │ TURN 中继                         │
     │ UDP 被阻断         │ TCP 隧道                          │
     └─────────────────────────────────────────────────────┘
```

### 15.2 NatTraversal 类设计

```cpp
class NatTraversal {
public:
    NatTraversal(boost::asio::io_context& ioc);

    // STUN 探测：向 STUN 服务器发送 Binding Request，获取映射地址和 NAT 类型
    boost::asio::awaitable<NatInfo> probeStun(const std::string& stun_host, uint16_t stun_port);

    // UDP 打洞：向服务端 UDP 端口发送探测包
    boost::asio::awaitable<bool> punchUdp(udp::socket& socket, const udp::endpoint& server_ep);

    // TURN 中继：申请 TURN 服务器分配中继地址
    boost::asio::awaitable<udp::endpoint> allocateTurnRelay(const std::string& turn_host,
                                                             uint16_t turn_port,
                                                             const std::string& credentials);

private:
    boost::asio::io_context& io_context_;
    // STUN 消息编解码（RFC 5389 简化实现）
    std::vector<uint8_t> encodeStunBindingRequest(uint32_t transaction_id);
    NatInfo parseStunBindingResponse(const uint8_t* data, uint32_t size);
};

struct NatInfo {
    NetworkManager::NatType type;
    boost::asio::ip::udp::endpoint mapped_endpoint;  // 公网映射地址
    bool udp_reachable = false;
};
```

### 15.3 TCP 语音隧道

当 UDP 完全不可用时，语音数据通过已建立的 TCP 连接传输：

```cpp
class TcpVoiceTunnel {
public:
    TcpVoiceTunnel(TcpConnection& tcp_conn);

    // 发送语音包（封装为 TCP 帧格式）
    // 帧格式：[4字节总长度][1字节类型=0xFF(voice_tunnel)][VoicePacket+载荷]
    boost::asio::awaitable<void> sendVoiceFrame(const uint8_t* data, uint32_t size);

    // 接收端：从 TCP 流中提取语音帧
    // 注意：TCP 是字节流，需要帧边界协议
    void onTcpDataReceived(const uint8_t* data, uint32_t size);

    void setOnVoiceFrame(std::function<void(const uint8_t*, uint32_t)> callback);

private:
    TcpConnection& tcp_conn_;
    std::vector<uint8_t> reassembly_buffer_;  // TCP 帧重组缓冲区
    std::function<void(const uint8_t*, uint32_t)> on_voice_frame_;
};
```

---

## 16. 安全架构

### 16.1 密码安全

```
注册流程：
  客户端：password → Argon2id(password, random_salt_32bytes, m=65536, t=3, p=4) → 存储 hash + salt + params

登录流程：
  客户端 → 服务端：username + SHA-256(username + password)  （传输凭证）
  服务端：查询用户记录 → Argon2id 验证

注意：传输层安全性由 TLS 保证，因此传输中使用简单哈希即可
      服务端存储使用 Argon2id，即使数据库泄露也无法暴力破解
```

### 16.2 语音加密

```
密钥协商流程（登录时通过 TLS TCP 通道）：
  1. 客户端生成 X25519 临时密钥对 → 发送 client_public_key (LoginRequest)
  2. 服务端生成 X25519 临时密钥对 → 发送 server_public_key (LoginResponse)
  3. 双方计算共享密钥：shared_secret = X25519(own_private, peer_public)
  4. 派生 AES-128-GCM 密钥：voice_key = HKDF(shared_secret, "nevo-voice-key", 16bytes)
  5. 派生每个 UDP 包的 nonce：从单调递增计数器生成（12字节）

密钥轮换（每 10 分钟）：
  - 服务端在 TCP 通道发送 KeyRotationRequest（包含新 public_key）
  - 客户端回复 KeyRotationResponse（包含新 public_key）
  - 双方重新计算 shared_secret → 新 voice_key
  - 旧密钥保留 2 个窗口期用于处理迟到的包
```

### 16.3 VoiceCrypto 类

```cpp
class VoiceCrypto {
public:
    VoiceCrypto();

    // 初始化会话密钥（登录成功后调用）
    void setSessionKey(const uint8_t key[16]);

    // 加密语音载荷
    // plaintext: Opus 编码数据
    // header_aad: VoicePacketHeader 明文字段（用于 AEAD 认证）
    // 返回：nonce(12) + ciphertext + auth_tag(16)
    std::vector<uint8_t> encrypt(const uint8_t* plaintext, uint32_t pt_len,
                                  const uint8_t* header_aad, uint32_t aad_len);

    // 解密语音载荷
    // 返回：解密后的 Opus 数据；认证失败返回 std::nullopt
    std::optional<std::vector<uint8_t>> decrypt(const uint8_t* ciphertext, uint32_t ct_len,
                                                 const uint8_t* nonce, uint32_t nonce_len,
                                                 const uint8_t* header_aad, uint32_t aad_len);

    // 密钥轮换
    void rotateKey(const uint8_t new_key[16]);

private:
    std::array<uint8_t, 16> current_key_;
    std::array<uint8_t, 16> previous_key_;  // 轮换期间保留旧密钥
    std::atomic<uint64_t> nonce_counter_{0};  // 单调递增 nonce 生成器
    bool has_previous_key_ = false;
};
```

---

## 17. 移动端适配

### 17.1 Android 适配

#### 后台音频保活

```kotlin
// NevoAudioService.kt - Android 前台服务
class NevoAudioService : Service() {
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // 创建前台通知渠道
        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)
        return START_STICKY  // 被系统杀死后自动重启
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotification(): Notification {
        val channel = NotificationChannel(
            "nevo_audio", "VoIP Audio", NotificationManager.IMPORTANCE_LOW
        )
        NotificationManagerCompat.getInstance(this).createNotificationChannel(channel)

        return NotificationCompat.Builder(this, "nevo_audio")
            .setContentTitle("NEVO - Voice Chat Active")
            .setSmallIcon(R.drawable.ic_voip)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    companion object {
        private const val NOTIFICATION_ID = 1001
    }
}
```

#### AndroidManifest.xml 关键配置

```xml
<manifest>
    <!-- 音频录制权限 -->
    <uses-permission android:name="android.permission.RECORD_AUDIO" />
    <!-- 前台服务权限 (Android 14+) -->
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK" />
    <!-- 网络权限 -->
    <uses-permission android:name="android.permission.INTERNET" />
    <!-- 保持唤醒（音频处理不休眠） -->
    <uses-permission android:name="android.permission.WAKE_LOCK" />

    <application>
        <service
            android:name=".NevoAudioService"
            android:foregroundServiceType="mediaPlayback"
            android:exported="false" />
    </application>
</manifest>
```

### 17.2 iOS 适配

#### Info.plist 后台音频模式

```xml
<key>UIBackgroundModes</key>
<array>
    <string>audio</string>
</array>
<key>NSMicrophoneUsageDescription</key>
<string>NEVO needs microphone access for voice chat.</string>
```

#### AVAudioSession 配置

```swift
// NevoAudioSession.swift
class NevoAudioSession {
    static func configure() {
        let session = AVAudioSession.sharedInstance()
        do {
            // 设置为 PlayAndRecord 模式，支持同时录音和播放
            try session.setCategory(.playAndRecord,
                                     mode: .voiceChat,       // 自动配置语音优化
                                     options: [.allowBluetooth,  // 允许蓝牙耳机
                                               .defaultToSpeaker])  // 默认扬声器
            try session.setPreferredIOBufferDuration(0.02)  // 20ms 缓冲（低延迟）
            try session.setActive(true)
        } catch {
            NSLog("AVAudioSession configuration failed: \(error)")
        }
    }

    // 监听蓝牙音频路由变化
    static func observeRouteChange() {
        NotificationCenter.default.addObserver(
            forName: AVAudioSession.routeChangeNotification,
            object: nil,
            queue: nil
        ) { notification in
            guard let userInfo = notification.userInfo,
                  let reason = userInfo[AVAudioSessionRouteChangeReasonKey] as? UInt else { return }

            let changeReason = AVAudioSession.RouteChangeReason(rawValue: reason)
            switch changeReason {
            case .newDeviceAvailable:
                // 蓝牙耳机连接 → 采样率可能变为 16kHz
                // 通知 AudioEngine 触发 Resampler 重新配置
                notifyDeviceSampleRateChanged()
            case .oldDeviceUnavailable:
                // 蓝牙耳机断开 → 恢复 48kHz
                notifyDeviceSampleRateChanged()
            default:
                break
            }
        }
    }

    static func notifyDeviceSampleRateChanged() {
        let newRate = AVAudioSession.sharedInstance().sampleRate
        // 通过回调通知 C++ AudioEngine
        AudioEngineBridge.deviceSampleRateChanged(UInt32(newRate))
    }
}
```

### 17.3 蓝牙采样率适配

蓝牙耳机在 HFP 模式下采样率通常为 16kHz，A2DP 模式为 44.1kHz。当检测到设备采样率变化时：

1. miniaudio 回调检测到设备实际采样率变化
2. 通过 `AudioEngine::onDeviceSampleRateChanged()` 通知引擎
3. Resampler 重新配置：`device_sample_rate → 48000Hz`（Opus 固定使用 48kHz）
4. 使用 libsamplerate 或 miniaudio 内置重采样器进行高质量转换
5. 过渡期间使用交叉淡入淡出（crossfade）避免爆音
