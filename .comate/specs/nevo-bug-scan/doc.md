# NEVO 项目 BUG 扫描与修复规格文档

## 概述

对 NEVO VoIP 项目进行全面静态代码分析，识别出 **7 个严重 BUG**、**9 个高危 BUG**、**2 个中危 BUG**。以下为详细分析、修复方案及后续建议。

---

## 严重级别 BUG (Critical)

### BUG-C1: ClientCore.cpp 多余闭合大括号导致编译失败

- **文件**: `src/client/src/ClientCore.cpp:479-480`
- **严重程度**: Critical（构建失败）
- **影响范围**: 整个 client 模块无法编译

**问题描述**:
`setState()` 函数在 line 478 正确闭合后，line 479-480 有两行多余的 `}`，导致后续函数定义脱离 `nevo` 命名空间。

```cpp
// line 478: 正确闭合 setState()
}
// line 479-480: 多余的闭合大括号
    }
}
```

**修复方案**: 删除 line 479-480 的多余大括号。

---

### BUG-C2: TCP 帧双重封装导致协议损坏（发送方向）

- **文件**: `src/client/src/NetworkManager.cpp:466-469`
- **严重程度**: Critical（所有客户端→服务器控制消息损坏）
- **影响范围**: 登录、频道加入、静音切换等所有控制消息

**问题描述**:
`sendControl()` 先调用 `encodeTcpFrame()` 生成完整帧 `[12字节头][protobuf载荷]`，然后传给 `tcp_conn_->asyncSend(frame, type, request_id)`。而 `TcpConnection::asyncSend()` 会再次调用 `encodeFrameHeader()` 添加另一层 12 字节帧头。

```
线路上实际传输: [外层12字节头][内层12字节头][protobuf载荷]
```

服务器 `asyncReadLoop` 读取外层头，将 `内层头+protobuf` 作为"载荷"传给 `onMessage`，导致后续解析失败。

**修复方案**: `sendControl()` 应只序列化 protobuf 载荷，将裸载荷传给 `asyncSend()`，让 `TcpConnection` 负责帧头封装：

```cpp
// 修改前:
std::vector<uint8_t> frame = encodeTcpFrame(message, type, request_id);
auto ec = co_await tcp_conn_->asyncSend(frame, static_cast<uint32_t>(type), request_id);

// 修改后:
std::vector<uint8_t> payload(message.ByteSizeLong());
message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));
auto ec = co_await tcp_conn_->asyncSend(payload, static_cast<uint32_t>(type), request_id);
```

---

### BUG-C3: handleTcpMessage 重新解析已剥离的帧头（接收方向）

- **文件**: `src/client/src/NetworkManager.cpp:662-711`
- **严重程度**: Critical（所有服务器→客户端控制消息被丢弃）
- **影响范围**: 登录响应、频道列表、用户事件等

**问题描述**:
`TcpConnection::asyncReadLoop` 已剥离 12 字节帧头，通过 `onMessage` 回调只传递 protobuf 裸载荷。但 `handleTcpMessage()` 假设数据仍包含帧头，尝试从前 12 字节解析帧头——实际上这 12 字节是 protobuf 序列化数据的开头，解析结果为垃圾值。

**修复方案**: 重构 `handleTcpMessage` 接口，改为同时传递消息类型和 request_id，不再从数据中解析帧头：

```cpp
// 修改回调签名:
tcp_conn_->onMessage = [this](std::vector<uint8_t> data, uint32_t msg_type, uint32_t req_id) {
    handleTcpMessage(std::move(data), static_cast<ControlMessageType>(msg_type), req_id);
};

// TcpConnection::asyncReadLoop 中传递额外参数:
if (onMessage) {
    onMessage(std::move(payload), message_type, request_id);
}

// handleTcpMessage 简化:
void NetworkManager::handleTcpMessage(std::vector<uint8_t> data,
                                       ControlMessageType type,
                                       uint32_t request_id) {
    // 直接反序列化 protobuf，无需再解析帧头
    control::ControlMessage message;
    if (!message.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        NEVO_LOG_WARN("network", "Failed to parse control message");
        return;
    }
    if (onControlMessage) {
        onControlMessage(message, type, request_id);
    }
}
```

---

### BUG-C4: 远端用户解码器从未创建——音频管线断裂

- **文件**: `src/core/src/audio/AudioEngine.cpp:743-761`
- **严重程度**: Critical（所有远端用户音频被静默丢弃）
- **影响范围**: 无法听到任何远端用户语音

**问题描述**:
`queueAudioData()` 将 Opus 数据推入 JitterBuffer 并调用 `processMixCycle()`，但从未为远端用户创建解码器。`processMixCycle()` 遍历 `decoders_` map（为空），跳过所有用户。`getOrCreateDecoder()` 方法存在但从未被调用。

**修复方案**: 在 `queueAudioData()` 中调用 `getOrCreateDecoder()`:

```cpp
Result<void> AudioEngine::queueAudioData(UserId user_id,
                                          const uint8_t* opus_data,
                                          uint32_t data_size) {
    if (!running_.load(std::memory_order_acquire)) {
        return Err<void>(ResultCode::DeviceNotAvailable, "AudioEngine not running");
    }
    if (!opus_data || data_size == 0) {
        return Ok();
    }

    // 确保远端用户有解码器
    getOrCreateDecoder(user_id);

    jitter_buffer_->push(user_id, opus_data, data_size, 0);
    processMixCycle();
    return Ok();
}
```

---

### BUG-C5: PTT 在 VAD 禁用时失效——始终传输

- **文件**: `src/core/src/audio/VoiceActivity.cpp:17-37`
- **严重程度**: Critical（PTT 完全失效，音频始终传输）
- **影响范围**: 隐私泄露，用户以为 PTT 按键说话实际始终在发送

**问题描述**:
当 PTT 启用但未按键且无 hangover 时，控制流落入 `if (!config_.vad_enabled)` 分支返回 `true`，完全绕过 PTT 状态检查。

```cpp
// line 19-30: PTT 按下和 hangover 处理
// line 32-36: BUG — PTT 启用但未按下时也落入此分支
if (!config_.vad_enabled) {
    speaking_.store(true, std::memory_order_relaxed);
    return true;   // 始终传输！
}
```

**修复方案**: 在 PTT hangover 检查后添加 PTT 守卫：

```cpp
// PTT 释放后的挂起期
if (config_.ptt_enabled && hangover_counter_ > 0) {
    --hangover_counter_;
    speaking_.store(hangover_counter_ > 0, std::memory_order_relaxed);
    return hangover_counter_ > 0;
}

// PTT 启用但未按下且无挂起 → 不传输
if (config_.ptt_enabled) {
    speaking_.store(false, std::memory_order_relaxed);
    return false;
}

// VAD 模式
if (!config_.vad_enabled) {
    speaking_.store(true, std::memory_order_relaxed);
    return true;
}
```

---

### BUG-C6: 输出音量双重衰减

- **文件**: `src/core/src/audio/AudioEngine.cpp:489-497` 和 `src/core/src/audio/AudioMixer.cpp:109-113`
- **严重程度**: Critical（音量控制表现异常）
- **影响范围**: 设置 50% 音量实际输出 25%

**问题描述**:
`setOutputVolume()` 同时设置 `output_volume_`（原子变量，在输出回调中应用）和 `mixer_->setVolume(volume)`（在 `AudioMixer::mix()` 中应用）。音量被应用两次，导致有效音量 = 设置值²。

**修复方案**: 只在一处应用音量。移除 `setOutputVolume()` 中对 `mixer_->setVolume()` 的调用，保留输出回调中的音量应用（这样音量在硬限幅之后应用，更合理）：

```cpp
void AudioEngine::setOutputVolume(float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    output_volume_.store(volume, std::memory_order_relaxed);
    // 不再同时设置 mixer volume — 输出回调已应用音量
    NEVO_LOG_DEBUG("audio", "Output volume set to {:.2f}", volume);
}
```

同时在 `initialize()` 中移除 `mixer_->setVolume(config_.output_volume)` 调用。

---

### BUG-C7: AudioMemoryPool::release() 数据竞争

- **文件**: `src/core/src/audio/AudioMemoryPool.cpp:54-77`
- **严重程度**: Critical（内存池损坏，可能导致同一块被分配两次）
- **影响范围**: 音频处理管线稳定性

**问题描述**:
`release()` 中先写入 `free_stack_[current_top] = block`，后执行 CAS。当两个线程同时释放且读到相同 `current_top` 时，先写的值被后写覆盖，导致内存泄漏和双重分配。

```cpp
free_stack_[current_top] = block;   // WRITE before CAS — 危险！
if (stack_top_.compare_exchange_weak(current_top, current_top + 1, ...)) {
    return;
}
// CAS 失败但写入已发生
```

**修复方案**: 使用 CAS 先占位，写入在 CAS 成功后进行：

```cpp
void AudioMemoryPool::release(uint8_t* block) noexcept {
    if (!block) return;
    assert(block >= storage_.data() &&
           block < storage_.data() + storage_.size());

    uint32_t current_top = stack_top_.load(std::memory_order_acquire);
    while (true) {
        if (current_top >= config_.block_count) {
            NEVO_LOG_ERROR("audio", "AudioMemoryPool::release: stack overflow");
            return;
        }
        // CAS 先保留槽位
        if (stack_top_.compare_exchange_weak(current_top, current_top + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
            // CAS 成功后安全写入
            free_stack_[current_top] = block;
            return;
        }
    }
}
```

---

## 高危级别 BUG (High)

### BUG-H1: processMixCycle() 线程不安全

- **文件**: `src/core/src/audio/AudioEngine.cpp:626-728, 759`
- **严重程度**: High（并发音频数据可能导致混音损坏）

**问题描述**: `queueAudioData()` 直接调用 `processMixCycle()`，如果多个网络线程同时调用 `queueAudioData()`，会导致 `mixer_->clear()` 和 `mixer_->mix()` 之间的操作交错，产生重复帧或丢失用户音频。

**修复方案**: 添加混音周期专用互斥锁：

```cpp
// AudioEngine.h 中添加:
std::mutex mix_cycle_mutex_;

// queueAudioData 和 processMixCycle 中:
void AudioEngine::processMixCycle() {
    std::lock_guard<std::mutex> lock(mix_cycle_mutex_);
    // ... 原有逻辑 ...
}
```

---

### BUG-H2: 输入重采样器在设备实际采样率匹配时配置错误

- **文件**: `src/core/src/audio/AudioEngine.cpp:110-121, 237-246`
- **严重程度**: High（音频严重失真）

**问题描述**: 如果 `config_.input_sample_rate ≠ 实际设备采样率`，但实际设备采样率恰好等于 48kHz（kOpusSampleRate），重采样器在 Step 3 按 config 采样率初始化（错误），而 Step 12 因 `in_dev->sampleRate == kOpusSampleRate` 跳过重配置。结果重采样器以错误配置处理 48kHz 数据，产生 3 倍频移失真。

**修复方案**: 当设备实际采样率等于 Opus 采样率时，重置重采样器：

```cpp
if (in_dev->sampleRate != kOpusSampleRate) {
    input_resampler_->setInputSampleRate(in_dev->sampleRate);
    input_resampler_->setOutputSampleRate(kOpusSampleRate);
    auto res = input_resampler_->initialize();
    if (!res) {
        NEVO_LOG_ERROR("audio", "Input Resampler reinit failed: {}", res.error().message());
    }
} else {
    // 设备采样率已匹配 Opus，无需重采样
    input_resampler_.reset();
}

// 同样处理输出重采样器
if (out_dev->sampleRate != kOpusSampleRate) {
    output_resampler_->setInputSampleRate(kOpusSampleRate);
    output_resampler_->setOutputSampleRate(out_dev->sampleRate);
    auto res = output_resampler_->initialize();
    if (!res) {
        NEVO_LOG_ERROR("audio", "Output Resampler reinit failed: {}", res.error().message());
    }
} else {
    output_resampler_.reset();
}
```

---

### BUG-H3: 缺少 has_user_info() 检查导致无效 UserId

- **文件**: `src/client/src/ClientCore.cpp:588`
- **严重程度**: High（登录后可能以 UserId(0) 运行）

**问题描述**: `handleLoginResponse()` 访问 `resp.user_info().id()` 前未检查 `resp.has_user_info()`。若服务器未设置 `user_info`，protobuf 返回默认对象 `id() == 0`，导致 `local_user_id_` 被设为无效值。

**修复方案**: 添加 `has_user_info()` 检查：

```cpp
if (success) {
    if (resp.has_user_info()) {
        local_user_id_ = UserId(resp.user_info().id());
    } else {
        NEVO_LOG_WARN("client", "Login response missing user_info");
    }
    // ... 密钥处理 ...
}
```

---

### BUG-H4: ClientCore::getState() 非原子成员数据竞争

- **文件**: `src/client/src/ClientCore.cpp:385-412`
- **严重程度**: High（未定义行为）

**问题描述**: `session_id_`、`local_user_id_`、`local_username_`、`current_channel_`、`current_channel_name_` 为普通成员，在 io_context 线程写入，在 `getState()`（可从任意线程调用）读取，无任何同步保护。

**修复方案**: 添加互斥锁保护状态成员：

```cpp
// ClientCore.h 中添加:
mutable std::mutex state_mutex_;

// 所有写入点（handleLoginResponse, disconnect 等）和 getState() 中:
ClientStateSnapshot ClientCore::getState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // ... 原有读取逻辑 ...
}
```

---

### BUG-H5: VoiceCrypto 密钥管理数据竞争

- **文件**: `src/network/src/VoiceCrypto.cpp:91-122, 144-242`
- **严重程度**: High（加密/解密可能失败，未定义行为）

**问题描述**: `encrypt()`/`decrypt()` 从音频线程读取 `current_key_`，`setSessionKey()`/`rotateKey()` 从 io_context 线程写入 `current_key_`，无同步保护。`has_old_key_`（普通 bool）也存在同样问题。

**修复方案**: 添加互斥锁保护密钥操作：

```cpp
// VoiceCrypto.h 中添加:
mutable std::mutex key_mutex_;

// setSessionKey/rotateKey 中:
void VoiceCrypto::setSessionKey(const uint8_t key[CRYPTO_KEY_SIZE]) {
    std::lock_guard<std::mutex> lock(key_mutex_);
    // ... 原有逻辑 ...
}

// encrypt/decrypt 中读取密钥时加锁:
std::vector<uint8_t> VoiceCrypto::encrypt(...) {
    std::array<uint8_t, CRYPTO_KEY_SIZE> key_copy;
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        key_copy = current_key_;
    }
    // 使用 key_copy 代替 current_key_.data()
}
```

---

### BUG-H6: TcpConnection::parseFrameHeader 未对齐内存访问

- **文件**: `src/network/src/TcpConnection.cpp:376-383`
- **严重程度**: High（ARM 平台崩溃，x86 上性能问题）

**问题描述**: 使用 `reinterpret_cast<const uint32_t*>` 直接从 `std::array<uint8_t>` 读取，违反对齐要求。编码方向（`encodeFrameHeader`）已正确使用 `std::memcpy`，解码方向应保持一致。

**修复方案**: 使用 `std::memcpy` 替代 `reinterpret_cast`：

```cpp
bool TcpConnection::parseFrameHeader(
    const std::array<uint8_t, TCP_HEADER_SIZE>& header_data,
    uint32_t& out_payload_length,
    uint32_t& out_message_type,
    uint32_t& out_request_id)
{
    uint32_t net_payload_length, net_message_type, net_request_id;
    std::memcpy(&net_payload_length, header_data.data(), 4);
    std::memcpy(&net_message_type, header_data.data() + 4, 4);
    std::memcpy(&net_request_id, header_data.data() + 8, 4);

    out_payload_length = boost::endian::big_to_native(net_payload_length);
    out_message_type = boost::endian::big_to_native(net_message_type);
    out_request_id = boost::endian::big_to_native(net_request_id);

    return true;
}
```

---

### BUG-H7: 协程中捕获 `this` 导致 Use-After-Free

- **文件**: `src/ui/src/MainWindow.cpp:214-231, 350-363`
- **严重程度**: High（窗口关闭时可能崩溃）

**问题描述**: `co_spawn` 启动的协程以 `detached` 方式运行，捕获了原始 `this` 指针。若用户在协程 `co_await` 期间关闭窗口，协程恢复后通过悬空指针访问 `MainWindow` 导致未定义行为。

**修复方案**: 使用 `QPointer` 作为生命周期守卫：

```cpp
boost::asio::co_spawn(*io_ctx_,
    [this, host, port, username, password]() mutable
    -> boost::asio::awaitable<void> {
        auto result = co_await client_core_->connect(
            host.toStdString(), port,
            username.toStdString(), password.toStdString());

        if (!result) {
            QPointer<MainWindow> guard(this);
            QMetaObject::invokeMethod(this, [guard, result]() {
                if (!guard) return;
                QMessageBox::critical(guard, ...);
            }, Qt::QueuedConnection);
        }
    },
    boost::asio::detached);
```

---

### BUG-H8: MainWindow 析构函数未清理回调导致悬挂调用

- **文件**: `src/ui/src/MainWindow.cpp:177-189`
- **严重程度**: High（析构期间回调可能访问已销毁成员）

**问题描述**: 析构函数先调用 `client_core_->disconnect()`（可能触发回调），再停止 io_context。回调中 `postToUiThread` 投递的 lambda 可能在成员变量已开始销毁后执行。

**修复方案**: 析构时先清空回调再断开连接：

```cpp
MainWindow::~MainWindow()
{
#ifdef NEVO_HAS_BOOST
    // 先清空回调，防止 disconnect 触发新的 postToUiThread
    if (client_core_) {
        client_core_->onStateChanged = nullptr;
        client_core_->onUserJoined = nullptr;
        client_core_->onUserLeft = nullptr;
        client_core_->onUserSpeaking = nullptr;
        client_core_->onServerMessage = nullptr;
        client_core_->onChannelList = nullptr;
        client_core_->onLatencyUpdate = nullptr;
        client_core_->onError = nullptr;
        client_core_->disconnect();
    }
    if (io_ctx_) io_ctx_->stop();
    if (io_thread_ && io_thread_->joinable()) {
        io_thread_->join();
    }
#endif
    NEVO_LOG_INFO("ui", "MainWindow destroyed");
}
```

---

### BUG-H9: postToUiThread 回调捕获原始 this 无生命周期保证

- **文件**: `src/ui/src/MainWindow.cpp:633-731`（所有回调）
- **严重程度**: High（窗口关闭时 UI 线程事件队列中的 lambda 可能访问已销毁对象）

**问题描述**: `setupClientCoreCallbacks()` 中每个回调都通过 `postToUiThread([this, ...]())` 捕获原始 `this`。若 `MainWindow` 在 posted lambda 执行前被销毁，将导致 use-after-free。

**修复方案**: 使用 `QPointer<MainWindow>` 守卫所有 `postToUiThread` 回调：

```cpp
client_core_->onUserJoined =
    [this](const User& user) {
        QPointer<MainWindow> guard(this);
        postToUiThread([guard, user]() {
            if (!guard) return;
            // ... 使用 guard 代替 this ...
        });
    };
```

---

## 中危级别 BUG (Medium)

### BUG-M1: VoiceActivity 非原子 config_ 和 hangover_counter_ 数据竞争

- **文件**: `src/core/include/nevo/core/audio/VoiceActivity.h:63,66`
- **严重程度**: Medium

**问题描述**: `config_` 和 `hangover_counter_` 为非原子成员，`setVadEnabled()`/`setPttEnabled()` 从主线程写 `config_`，`shouldTransmit()` 从编码线程读写两者。

**修复方案**: 将 `config_` 修改为原子加载/存储，或使用 mutex：

```cpp
// 方案 A: 使用 std::atomic<Config>（需要 Config 可平凡复制）
// 方案 B: 使用 mutex 保护
mutable std::mutex config_mutex_;
Config config_;  // 在 config_mutex_ 保护下访问
```

---

### BUG-M2: AudioEngine::initialize() 中 mixer 初始化音量设置

- **文件**: `src/core/src/audio/AudioEngine.cpp:164`
- **严重程度**: Medium（与 BUG-C6 相关）

**问题描述**: `initialize()` 中调用 `mixer_->setVolume(config_.output_volume)`，与输出回调中的 `output_volume_` 形成双重音量。应移除此行。

---

## 数据流路径

### 控制消息发送流（修复前）
```
ClientCore → NetworkManager::sendControl()
  → encodeTcpFrame() → [12字节头][protobuf]  ← 第一层封装
  → TcpConnection::asyncSend()
    → encodeFrameHeader() → [12字节头]        ← 第二层封装
  → 线路: [外层头][内层头][protobuf]           ← 双重封装！
```

### 控制消息发送流（修复后）
```
ClientCore → NetworkManager::sendControl()
  → protobuf.SerializeToArray() → [protobuf裸载荷]
  → TcpConnection::asyncSend()
    → encodeFrameHeader() → [12字节头]
  → 线路: [帧头][protobuf]                    ← 单层封装
```

### 控制消息接收流（修复前）
```
TcpConnection::asyncReadLoop()
  → 读帧头 → 读载荷 → onMessage([protobuf裸载荷])
  → NetworkManager::handleTcpMessage([protobuf裸载荷])
    → decodeTcpFrameHeader(前12字节)           ← 错误！解析protobuf数据当作帧头
```

### 控制消息接收流（修复后）
```
TcpConnection::asyncReadLoop()
  → 读帧头(含msg_type, req_id) → 读载荷 → onMessage([payload], msg_type, req_id)
  → NetworkManager::handleTcpMessage([payload], type, request_id)
    → 直接 ParseFromArray(payload)             ← 正确！
```

---

## 修复优先级

| 优先级 | BUG 编号 | 说明 |
|--------|----------|------|
| P0 | C1 | 编译失败，必须首先修复 |
| P0 | C2, C3 | 协议通信完全不可用 |
| P0 | C4 | 远端音频完全无法播放 |
| P0 | C5 | PTT 隐私泄露 |
| P1 | C6, M2 | 音量控制异常 |
| P1 | C7 | 内存池损坏 |
| P1 | H1 | 混音并发问题 |
| P1 | H2 | 重采样器配置错误 |
| P2 | H3-H6 | 数据竞争、未定义行为 |
| P2 | H7-H9 | UI 生命周期问题 |
| P3 | M1 | VoiceActivity 线程安全 |

---

## 后续迭代建议

### 代码质量改进
1. **统一帧编解码**: 将 `PacketCodec` 中的 `encodeTcpFrame`/`decodeTcpFrame` 与 `TcpConnection` 的帧头处理合并，消除双重封装的根源
2. **线程安全审计**: 为所有跨线程访问的成员添加文档注释，标明访问线程和同步策略
3. **RAII 音频设备管理**: `AudioEngine::initialize()` 中 `ma_device` 的错误路径存在泄漏风险，应使用自定义 deleter 的 `unique_ptr`

### 功能扩展
4. **UDP 心跳与延迟统计**: 实现真正的 UDP ping 测量，替换当前 `onLatencyUpdate(-1)` 的占位实现
5. **TURN 中继实现**: 完成当前 stub 的 TURN relay，支持对称 NAT 穿越
6. **配置持久化**: 客户端/服务器设置保存到本地文件（JSON/TOML）

### 技术栈升级
7. **CMake 现代化**: 升级到 CMake presets，添加 Conan/vcpkg 包管理
8. **CI/CD 流水线**: 添加 GitHub Actions 自动构建和测试
9. **测试覆盖**: 为核心模块（VoiceCrypto, JitterBuffer, AudioMixer）添加单元测试
