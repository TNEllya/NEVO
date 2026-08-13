# NEVO 视频通话模块设计文档

**版本**: v1.0  
**日期**: 2026-06-24  
**作者**: AI Assistant (Claude Code)  
**状态**: 待审阅  

---

## 1. 文档信息

### 1.1 目的

本文档为 NEVO 项目设计并实现一个完整的类微信风格视频通话模块。模块需覆盖 C++ 核心协议、服务端转发、Python 客户端、C++ Qt 服务端管理界面以及 Web 管理界面，确保视觉风格统一、操作体验流畅、传输低延迟、跨平台兼容，并满足数据安全与隐私保护要求。

### 1.2 目标读者

- 后端 C++ 开发工程师
- Python GUI 开发工程师
- Web 前端开发工程师
- 测试与 DevOps 工程师
- 项目经理与后续维护人员

### 1.3 相关文档

- `README.md`
- `CODE_WIKI.md`
- `proto/video.proto`
- `proto/control.proto`
- `src/client/include/nevo/client/ClientCore.h`
- `src/server/include/nevo/server/ServerCore.h`
- `src/network/include/nevo/network/UdpSocket.h`

---

## 2. 背景与目标

### 2.1 项目背景

NEVO 当前已具备：

- TCP/TLS 控制通道
- UDP 语音通道（带 XChaCha20-Poly1305 加密、密钥轮换、NAT 穿透、TCP 隧道回退）
- UDP 视频通道基础设施：`VideoRelay` 已实现并集成于 `ServerCore`，支持按频道转发加密视频包
- 独立的视频 UDP 端口（`video_udp_port_`），与语音 UDP 端口分离
- 频道与用户管理体系
- Python/PyQt5 客户端 GUI
- C++ Qt6 服务端管理界面（`src/server/ui/`）
- Web 管理面板（HTTP + SSE）

视频通话功能需要在上述基础设施之上扩展，复用现有用户关系、加密、网络通道和管理体系，同时新增视频采集、编码、传输、解码、渲染、状态管理等能力。

### 2.2 业务目标

| 目标编号 | 目标描述 | 优先级 |
|----------|----------|--------|
| G1 | 用户可查看联系人/频道成员在线状态 | P0 |
| G2 | 用户可发起、接收、拒绝、挂断一对一视频通话 | P0 |
| G3 | 通话中支持实时双向视频与音频 | P0 |
| G4 | 通话状态机清晰：Idle → Calling → Ringing → Connecting → Connected → Ended | P0 |
| G5 | 支持静音、关闭摄像头、切换摄像头 | P0 |
| G6 | 支持网络状态监测与自适应码率/分辨率调整 | P1 |
| G7 | UI/UX 遵循类微信风格，视觉统一、操作流畅 | P0 |
| G8 | 数据端到端加密，保护通话隐私 | P0 |
| G9 | 提供完整 API 文档、集成步骤、测试用例与性能优化建议 | P0 |

### 2.3 非目标

- 本期不实现多人视频会议（P2，预留扩展）
- 本期不实现云端录制与回放（P2）
- 本期不实现实时美颜、虚拟背景（P3）
- 本期不实现屏幕共享（P1，接口预留）

---

## 3. 术语表

| 术语 | 说明 |
|------|------|
| VideoCallManager | 客户端视频通话管理器，负责通话状态机、媒体协商、本地采集与远端渲染协调 |
| VideoRelay | 服务端视频包转发器，按通话关系转发视频帧 |
| IVideoSource | 视频源抽象接口，负责摄像头采集与编码 |
| IVideoSink | 视频渲染器抽象接口，负责解码与显示 |
| NAL | H.264/H.265 网络抽象层单元 |
| KeyFrame | 关键帧，可独立解码 |
| DeltaFrame | 差分帧，依赖关键帧 |
| RTT | Round-Trip Time，网络往返时延 |
| PLI | Picture Loss Indication，关键帧请求 |
| FIR | Full Intra Request，完整帧内请求 |

---

## 4. 架构总览

### 4.1 总体架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              客户端 (Python/C++)                          │
│  ┌──────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐ │
│  │  UI Layer    │  │ VideoCallManager│  │ IVideoSource / IVideoSink   │ │
│  │ (PyQt5/Qt6)  │◄─┤  (状态机/协商)   │◄─┤ (采集/编码/解码/渲染)        │ │
│  └──────────────┘  └────────┬────────┘  └─────────────────────────────┘ │
│                             │                                           │
│  ┌──────────────────────────┴─────────────────────────────────────────┐│
│  │                      NetworkManager / UdpSocket                      ││
│  │              控制面 TCP  +  媒体面 UDP / TCP Tunnel                 ││
│  └──────────────────────────┬─────────────────────────────────────────┘│
└─────────────────────────────┼───────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              服务端 (C++)                                 │
│  ┌──────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐ │
│  │ ClientSession│◄─┤  ServerCore     │◄─┤   ChannelManager            │ │
│  │ (控制消息)    │  │ (生命周期协调)   │  │   (频道/用户关系)            │ │
│  └──────┬───────┘  └────────┬────────┘  └─────────────────────────────┘ │
│         │                   │                                           │
│         │         ┌─────────▼──────────┐                                │
│         │         │    VideoRelay      │                                │
│         │         │  (视频包转发)       │                                │
│         │         └─────────┬──────────┘                                │
│         │                   │                                           │
│  ┌──────▼───────────────────▼─────────────────────────────────────────┐│
│  │                         ControlServer / Web Proxy                   ││
│  │              JSON-over-TCP IPC  +  Web HTTP/SSE API                 ││
│  └─────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.2 设计原则

1. **模块独立**: 新增 `nevo::video` 命名空间，VideoCallManager 与 VideoRelay 不侵入 ClientCore/ServerCore 核心逻辑。
2. **复用基础设施**: 复用现有 TCP 控制通道、UDP Socket、加密模块、NAT 穿透、TCP 隧道。
3. **协议兼容**: 控制面复用 `control.proto` 并扩展视频通话消息；媒体面复用 `video.proto` 的 `VideoPacketHeader`。
4. **平台抽象**: IVideoSource/IVideoSink 抽象使 Qt/Python/Web 可各自实现具体采集与渲染。
5. **安全第一**: 视频帧复用现有 XChaCha20-Poly1305 会话密钥加密，服务端不解密仅转发。

---

## 5. 详细设计

### 5.1 共享视频核心（src/core/video）

#### 5.1.1 类型定义

```cpp
// src/core/include/nevo/core/video/VideoTypes.h
namespace nevo::video {

enum class VideoCodec : uint32_t { Unknown=0, H264=1, H265=2, VP8=3, VP9=4, AV1=5 };

struct CodecCapability {
    VideoCodec codec;
    uint32_t max_width = 1920;
    uint32_t max_height = 1080;
    uint32_t max_fps = 30;
    bool hardware_accelerated = false;
};

struct VideoProfile {
    VideoCodec codec;
    uint32_t width = 640;
    uint32_t height = 480;
    uint32_t fps = 30;
    uint32_t target_bitrate_kbps = 1000;
};

enum class VideoFrameType : uint32_t { KeyFrame=0, DeltaFrame=1 };

struct VideoFrame {
    VideoFrameType type = VideoFrameType::DeltaFrame;
    uint32_t sequence_number = 0;
    uint64_t timestamp_us = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> payload;
};

enum class VideoCallState {
    Idle, Calling, Ringing, Connecting, Connected, Reconnecting, Ended
};

enum class VideoCallEndReason {
    LocalHangup, RemoteHangup, RemoteRejected, RemoteBusy,
    Timeout, NetworkError, PeerDisconnected, Unknown
};

using VideoCallId = uint64_t;

} // namespace nevo::video
```

#### 5.1.2 视频源/渲染器抽象

```cpp
// src/core/include/nevo/core/video/IVideoSource.h
class IVideoSource {
public:
    virtual ~IVideoSource() = default;
    virtual Result<void> startCapture(const VideoProfile& profile) = 0;
    virtual void stopCapture() = 0;
    virtual std::vector<std::string> enumerateDevices() const = 0;
    virtual Result<void> selectDevice(const std::string& device_id) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
    std::function<void(const VideoFrame& frame)> onEncodedFrame;
};
```

```cpp
// src/core/include/nevo/core/video/IVideoSink.h
class IVideoSink {
public:
    virtual ~IVideoSink() = default;
    virtual void renderFrame(const VideoFrame& frame) = 0;
    virtual void clear() = 0;
    virtual void setRenderSize(uint32_t width, uint32_t height) = 0;
};
```

### 5.2 客户端 VideoCallManager

#### 5.2.1 职责

- 维护一对一视频通话状态机
- 发起/接收/拒绝/挂断通话
- 收集本地编解码能力并与远端协商
- 管理本地视频源（摄像头）与远端/本地预览渲染器
- 处理视频帧的发送与接收
- 实现网络自适应（码率/分辨率调整）
- 向 UI 暴露回调

#### 5.2.2 接口

```cpp
// src/client/include/nevo/client/VideoCallManager.h
class VideoCallManager : public std::enable_shared_from_this<VideoCallManager> {
public:
    explicit VideoCallManager(boost::asio::io_context& io_ctx, NetworkManager& net_mgr);
    ~VideoCallManager();

    void setLocalVideoSource(std::shared_ptr<IVideoSource> source);
    void setRemoteVideoSink(std::shared_ptr<IVideoSink> sink);
    void setLocalPreviewSink(std::shared_ptr<IVideoSink> preview_sink);

    std::vector<CodecCapability> localCapabilities() const;
    void setPreferredProfile(const VideoProfile& profile);

    boost::asio::awaitable<Result<void>> initiateCall(UserId peer_id);
    void acceptCall();
    void rejectCall();
    void hangUp();

    void setLocalAudioMuted(bool muted);
    void setLocalVideoEnabled(bool enabled);
    bool isLocalAudioMuted() const;
    bool isLocalVideoEnabled() const;

    VideoCallState state() const;
    std::optional<UserId> peerId() const;
    VideoCallId currentCallId() const;
    VideoProfile negotiatedProfile() const;

    std::function<void(VideoCallState state)> onStateChanged;
    std::function<void(UserId peer_id)> onIncomingCall;
    std::function<void(const VideoProfile& profile)> onProfileNegotiated;
    std::function<void(int bitrate_kbps, float packet_loss)> onNetworkAdaptation;
    std::function<void(VideoCallEndReason reason, const std::string& message)> onCallEnded;
};
```

#### 5.2.3 状态机

```
Idle
 │
 │ initiateCall(peer_id)
 ▼
Calling ──(超时 30s)──> Ended(Timeout)
 │
 │ 远端 accept
 ▼
Connecting ──(建立媒体 5s 超时)──> Ended(NetworkError)
 │
 │ 首个 KeyFrame 到达/发送成功
 ▼
Connected ──(网络抖动)──> Reconnecting ──(恢复)──> Connected
 │
 │ hangUp / remote hangup / peer disconnected
 ▼
Ended
 │
 │(短暂停留后自动回到 Idle)
 ▼
Idle
```

### 5.3 服务端 VideoRelay

#### 5.3.1 现状

`src/server/VideoRelay.h/.cpp` **已经实现并集成到 `ServerCore`**，当前行为：

- 按 `ChannelId` 维护用户 UDP 端点映射
- 接收视频 UDP 包，使用发送者密钥解密，再使用接收者密钥重新加密后转发
- 与 `AudioRelay` 逻辑类似，服务于**频道级视频**（例如频道内直播/屏幕共享）

#### 5.3.2 为满足一对一视频通话的改造

当前 `VideoRelay` 按频道转发，所有同频道用户都能收到彼此视频。一对一视频通话需要**按 `call_id` 隔离**，改造方案：

1. 在 `VideoClientMapping` 中增加 `VideoCallId active_call_id` 字段。
2. 新增 `registerCallParticipant(VideoCallId, UserId, endpoint, capabilities)` / `unregisterCallParticipant(VideoCallId, UserId)`。
3. `handleVideoPacket` 解析 `VideoPacketHeader.call_id`，只转发给同 `call_id` 且 `user_id != sender_id` 的参与者。
4. 保留现有按频道转发能力，作为向后兼容。

#### 5.3.3 目标接口

```cpp
// src/server/include/nevo/server/VideoRelay.h
class VideoRelay : public std::enable_shared_from_this<VideoRelay> {
public:
    explicit VideoRelay(boost::asio::io_context& io_ctx,
                        std::shared_ptr<UdpSocket> udp_socket,
                        ChannelManager* channel_mgr);
    ~VideoRelay();

    // 原有频道级接口（保留兼容）
    void addClientMapping(UserId user_id,
                          const boost::asio::ip::udp::endpoint& ep,
                          ChannelId channel_id);
    void removeClientMapping(UserId user_id);
    void updateClientChannel(UserId user_id, ChannelId channel_id);

    // 新增一对一通话接口
    void registerCallParticipant(VideoCallId call_id,
                                 UserId user_id,
                                 const boost::asio::ip::udp::endpoint& endpoint,
                                 const std::vector<CodecCapability>& capabilities);
    void unregisterCallParticipant(VideoCallId call_id, UserId user_id);
    void updateCallParticipantEndpoint(VideoCallId call_id, UserId user_id,
                                       const boost::asio::ip::udp::endpoint& endpoint);

    void handleVideoPacket(const uint8_t* data, uint32_t size,
                           const boost::asio::ip::udp::endpoint& sender);
    void handleTcpVideoPacket(UserId sender_id, const uint8_t* data, uint32_t size);

    struct RelayStats { uint64_t packets_relayed=0, packets_dropped=0, bytes_relayed=0; };
    RelayStats stats() const;
};
```

### 5.4 控制面协议扩展

在 `proto/control.proto` 中新增以下消息：

```protobuf
message VideoCallRequest {
    uint64 target_user_id = 1;
    uint64 call_id        = 2;
    repeated nevo.video.CodecCapability capabilities = 3;
}

message VideoCallResponse {
    uint64 call_id = 1;
    bool   accepted = 2;
    string reason = 3;
    nevo.video.VideoProfile profile = 4;
}

message VideoCallHangup {
    uint64 call_id = 1;
    nevo.video.VideoCallEndReason reason = 2;
}

message VideoProfileUpdate {
    uint64 call_id = 1;
    nevo.video.VideoProfile profile = 2;
}

message VideoControlMessage {
    oneof payload {
        VideoCallRequest    video_call_request = 1;
        VideoCallResponse   video_call_response = 2;
        VideoCallHangup     video_call_hangup = 3;
        VideoProfileUpdate  video_profile_update = 4;
    }
}
```

同时扩展 `ControlMessageType` 枚举：

```cpp
enum class ControlMessageType : uint32_t {
    // ... existing ...
    VideoCallRequest  = 50,
    VideoCallResponse = 51,
    VideoCallHangup   = 52,
    VideoProfileUpdate = 53,
};
```

### 5.5 媒体面协议

复用并扩展 `proto/video.proto`：

```protobuf
message VideoPacketHeader {
    uint32 sequence_number = 1;
    uint64 sender_id       = 2;
    uint64 channel_id      = 3;
    uint64 call_id         = 4;  // 新增：通话 ID
    uint32 timestamp       = 5;
    uint32 frame_type      = 6;  // 0=KeyFrame, 1=DeltaFrame
    uint32 fragment_index  = 7;
    uint32 fragment_total  = 8;
    uint32 width           = 9;
    uint32 height          = 10;
    uint32 fps             = 11;
    bool   tcp_tunnel      = 12;
}
```

UDP 视频包格式：

```
[2 字节长度前缀][VideoPacketHeader 序列化][nonce][加密 video payload + tag]
```

- 加密使用现有 `VoiceCrypto`，AAD 为 `VideoPacketHeader` 序列化数据。
- 视频包走与语音包相同的 UDP Socket，通过 `call_id` 区分。
- 大包分片：单个 H.264 NAL 超过 MTU 时拆分为多个 fragment，接收端重组。

TCP 隧道视频包格式：

```
[4 字节总长度（大端）][1 字节类型=0xFE][VideoPacketHeader + payload]
```

### 5.6 UI/UX 设计

#### 5.6.1 类微信风格原则

- **大头像居中**: 通话等待/响铃界面以对方大头像为主视觉。
- **底部操作栏**: 静音、关闭摄像头、切换摄像头、挂断四大按钮固定在底部。
- **状态提示**: 顶部显示 "等待对方接听"、"对方已挂断"、"网络不佳" 等状态。
- **悬浮小窗**: 通话中本地预览以小窗悬浮在右上角，可拖动。
- **暗色遮罩**: 背景使用暗色半透明遮罩，突出视频画面。

#### 5.6.2 关键界面

| 界面 | 元素 |
|------|------|
| 联系人列表 | 在线状态绿点、视频通话按钮 |
| 发起呼叫 | 对方大头像、昵称、"正在呼叫..."、取消按钮 |
| 来电接听 | 对方头像、昵称、接听（绿）/拒绝（红）大按钮 |
| 通话中 | 远端全屏视频、本地小窗预览、底部功能按钮栏、网络状态角标 |
| 通话结束 | 通话时长、挂断原因提示 |

---

## 6. 数据流

### 6.1 发起通话

```
用户 A (Python/Qt/Web)
  │ 点击视频通话
  │
  ▼
VideoCallManager::initiateCall(B)
  │
  ▼
发送 VideoCallRequest → TCP → ServerCore → ClientSession(B)
  │
  ▼
用户 B 收到 onIncomingCall 回调，弹出接听界面
```

### 6.2 接受通话

```
用户 B 点击接听
  │
  ▼
VideoCallManager::acceptCall()
  │
  ▼
发送 VideoCallResponse(accepted=true, profile=协商结果) → TCP → ServerCore → ClientSession(A)
  │
  ▼
双方状态变为 Connecting，启动本地采集与编码
  │
  ▼
VideoCallManager 通过 UdpSocket 发送首个 KeyFrame
  │
  ▼
服务端 VideoRelay 根据 call_id 转发
  │
  ▼
双方收到首个 KeyFrame 后状态变为 Connected
```

### 6.3 视频帧转发

```
用户 A 摄像头 → IVideoSource → 编码器 → VideoFrame
  │
  ▼
VideoCallManager 打包为 UDP 视频包（加密）
  │
  ▼
UdpSocket → 服务端
  │
  ▼
VideoRelay 解析包头，查找同 call_id 的远端 B
  │
  ▼
转发加密视频包 → 用户 B
  │
  ▼
VideoCallManager 解密 → 解码器 → IVideoSink → 显示
```

### 6.4 挂断

```
用户 A 点击挂断
  │
  ▼
VideoCallManager::hangUp()
  │
  ▼
发送 VideoCallHangup → 服务端 → 用户 B
  │
  ▼
双方释放媒体资源，状态变为 Ended，稍后回到 Idle
```

---

## 7. 安全与隐私

### 7.1 加密

- 视频载荷使用与语音相同的 `VoiceCrypto`（XChaCha20-Poly1305）加密。
- 会话密钥通过登录后的密钥交换获得，并支持自动密钥轮换。
- `VideoPacketHeader` 作为 AAD 参与认证，防止头部篡改。
- 服务端为每对通话参与者维护独立的 `VoiceCrypto` 上下文：用发送者密钥解密后，再用接收者密钥重新加密转发。这意味着服务端在转发瞬间可接触明文，**不是严格意义上的端到端加密**，但符合 NEVO 现有语音架构（服务器中继模型）。如需真正 E2EE，应改用客户端之间直接协商密钥或 DTLS/SRTP，本期不做变更。

### 7.2 隐私

- 摄像头未启用时不采集画面，本地预览关闭时不渲染。
- 通话建立前不传输视频流。
- 所有通话元数据（call_id、participant）仅通过已认证的 TCP 控制通道交换。

### 7.3 认证与授权

- 只有已认证用户才能发起视频通话。
- 服务端验证 target_user_id 是否在线，不在线则返回 busy/offline。
- 拒绝未登录用户的 VideoCallRequest。

---

## 8. 性能与自适应

### 8.1 网络监测

VideoCallManager 持续监测：

- RTT（通过现有 UdpPing）
- 丢包率（基于 sequence_number 间隙）
- 抖动（Jitter，基于到达时间差）
- 实际发送码率

### 8.2 自适应策略

| 网络状况 | 动作 |
|----------|------|
| RTT < 100ms, 丢包 < 1% | 提升分辨率/码率 |
| RTT 100-300ms, 丢包 1-3% | 保持当前配置 |
| RTT > 300ms 或 丢包 > 3% | 降低码率 20%，必要时降低分辨率 |
| 丢包 > 10% 持续 3s | 触发 KeyFrame 请求，切换到 TCP 隧道 |
| 网络恢复 | 逐步提升码率/分辨率 |

### 8.3 编码策略

- 默认 H.264，Baseline/Main Profile。
- 帧率根据网络动态调整：15/20/24/30 fps。
- 分辨率阶梯：180p/360p/480p/720p。
- 码率阶梯：300/500/1000/2000 kbps。

### 8.4 关键帧恢复

- 远端丢包导致花屏时，发送 PLI/FIR 控制消息请求关键帧。
- 每次分辨率/码率切换后发送关键帧。

---

## 9. 测试策略

### 9.1 单元测试

| 测试目标 | 内容 |
|----------|------|
| VideoTypes | 序列化/反序列化 |
| VideoFrame 分片 | 大包分片与重组 |
| VideoCallManager 状态机 | 状态转换与超时 |
| VideoRelay 转发 | 单接收者转发、不在线丢弃 |
| CodecCapability 协商 | 交集选择、降级策略 |

### 9.2 集成测试

- 本地双客户端端到端视频通话（loopback）。
- 服务端转发压力测试（模拟 10 对并发通话）。
- 网络劣化模拟（NetEm/Clumsy）：高延迟、丢包、抖动。
- 音频+视频同时传输测试。

### 9.3 UI 测试

- Python 客户端自动化：接听、挂断、静音、关闭摄像头。
- Web 端 Selenium/Playwright 测试。

### 9.4 性能测试

- CPU/GPU 占用：1080p@30fps 本地编码+解码。
- 端到端延迟：< 300ms（同城网络）。
- 并发通话：单服务端支持 ≥50 对并发。

---

## 10. 集成步骤

### 10.1 后端 C++ 集成

1. 在 `proto/control.proto` 中新增视频通话消息。
2. 在 `src/core/video/` 下创建 `VideoTypes.h`、`IVideoSource.h`、`IVideoSink.h`。
3. 在 `src/client/` 下创建 `VideoCallManager.h/.cpp`。
4. 在 `src/server/` 下创建 `VideoRelay.h/.cpp`。
5. 在 `NetworkManager` 中增加视频包发送/接收接口。
6. 在 `ClientSession` 中处理视频通话控制消息。
7. 在 `ServerCore` 中初始化并注册 `VideoRelay`。
8. 更新 `CMakeLists.txt` 添加新源文件与依赖（OpenCV/FFmpeg/libx264 等按需）。

### 10.2 Python 客户端集成

1. 实现 `IVideoSource` 的 Python 版本：基于 OpenCV 或 PyQt5 摄像头采集。
2. 实现 `IVideoSink` 的 Python 版本：基于 PyQt5 QLabel/QPainter 渲染。
3. 在 `nevo_client.py` 中新增视频通话状态机。
4. 在 `main_window.py` 中添加视频通话 UI（发起/接听/通话中）。
5. 扩展 `nevo_wire.py` 处理新的控制消息。

### 10.3 C++ Qt 服务端 GUI 集成

1. 在 `src/server/ui/` 下的服务端管理界面新增 "视频通话统计" 面板。
2. 显示当前进行中的通话数、每对通话的码率、丢包率。

### 10.4 Web 管理界面集成

1. 在 `web/server.py` 中新增 `/api/video_calls` REST API。
2. 在 `web/js/app.js` 中新增视频通话监控仪表盘。

### 10.5 文档与示例

1. 更新 `README.md` 视频通话章节。
2. 编写 `docs/video_call_api.md`。
3. 提供最小集成示例代码。

---

## 11. 依赖项

| 依赖 | 用途 | 是否新增 |
|------|------|----------|
| OpenCV | 摄像头采集、图像格式转换 | 是 |
| libx264 / OpenH264 | H.264 编码 | 是 |
| FFmpeg | 可选：解码与格式工具 | 是（推荐） |
| Qt Multimedia (C++) | Qt 端摄像头与渲染 | 是（可选） |
| PyQt5 / PyQt6 | Python 端 GUI 与摄像头 | 已有 |
| libsodium | 视频载荷加密 | 已有 |
| Boost.Asio | 网络异步 I/O | 已有 |
| Protobuf | 控制/媒体消息序列化 | 已有 |

---

## 12. 里程碑与实施计划

| 阶段 | 时间（预估） | 交付物 |
|------|-------------|--------|
| M1 | 0.5 周 | 共享视频类型、控制面协议扩展、`VideoCallManager` 接口定义 |
| M2 | 1 周 | `VideoRelay` 改造：新增 `call_id` 隔离的一对一通话转发 |
| M3 | 2 周 | C++ 客户端实现：`VideoCallManager`、视频包加密/分片、TCP 隧道回退 |
| M4 | 2 周 | 编解码器集成（H.264 默认）、本地采集与远端渲染（Qt 参考实现） |
| M5 | 1.5 周 | Python 客户端完整 UI（类微信风格）与视频状态机 |
| M6 | 1 周 | Web 管理界面增强、服务端 GUI 统计面板 |
| M7 | 1 周 | 单元/集成/性能测试、文档、Bug 修复 |

**总计约 9 周**（含缓冲）。服务端 `VideoRelay` 基础已存在，可节省约 1 周。

---

## 13. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 跨平台摄像头/编码器差异大 | 高 | 使用 OpenCV + FFmpeg 抽象，提供平台特定实现兜底 |
| 视频编解码 CPU 占用高 | 中 | 优先支持硬件编码（MediaToolbox/VideoToolbox/DXVA），默认 360p/24fps |
| 大包 UDP 分片导致丢包 | 高 | NAL 按 MTU 分片，丢包时快速请求关键帧 |
| NAT/防火墙阻断 UDP 视频 | 中 | 复用 TCP 隧道回退机制 |
| 现有测试构建问题未修复 | 低 | 先修复 CMake GTest 检测，确保新增测试可运行 |
| 文档提到的 `src/ui` 不存在 | 低 | Python 客户端作为唯一 C++ 客户端 GUI 替代方案 |

---

## 14. 附录

### 14.1 API 速查

详见正文章节 5.2 与 5.3。

### 14.2 新增/修改文件清单

```
proto/control.proto                                (扩展)
proto/video.proto                                  (扩展，增加 call_id)
src/core/include/nevo/core/video/VideoTypes.h      (新增)
src/core/include/nevo/core/video/IVideoSource.h    (新增)
src/core/include/nevo/core/video/IVideoSink.h      (新增)
src/client/include/nevo/client/VideoCallManager.h  (新增)
src/client/src/VideoCallManager.cpp                (新增)
src/server/include/nevo/server/VideoRelay.h        (改造，新增 call_id 转发)
src/server/src/VideoRelay.cpp                      (改造)
src/client/gui_python/video_call_dialog.py         (新增)
src/client/gui_python/video_widgets.py             (新增)
web/server.py                                      (扩展)
web/js/app.js                                      (扩展)
CMakeLists.txt / src/*/CMakeLists.txt              (扩展)
docs/video_call_api.md                             (新增)
```

### 14.3 性能优化建议

1. 使用硬件编码器降低 CPU 占用。
2. 视频渲染使用 GPU 纹理（OpenGL/DirectX/Vulkan）避免 CPU 拷贝。
3. 网络层使用 sendmmsg/recvmmsg（Linux）或 GSO（Linux）批量收发。
4. 视频帧复用内存池，减少堆分配。
5. 根据运动场景动态调整 GOP 长度。
6. 启用前向纠错（FEC）或 NACK 重传应对突发丢包。

---

**审阅记录**

- 2026-06-24: 初稿完成，待用户审阅。
