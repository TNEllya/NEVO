# NEVO 项目已知问题修复与完善

## 需求概述

根据项目完成度分析报告，修复所有已知 Bug、安全占位符、UI 断裂问题及服务器端逻辑缺失，使项目达到可用的 MVP 状态。

---

## 问题清单与修复方案

### 问题 1：客户端 UI 频道树数据流断裂

**现象**：`ChannelTreeModel::updateFromChannelList()` 已实现，但 `ClientCore` 处理 `ChannelList` 消息时未解析数据、未通知 UI（`ClientCore.cpp:580-583` 仅打印日志后 break），`MainWindow::setupClientCoreCallbacks()` 中无频道列表回调。频道树永远为空。

**修复**：
1. 在 `ClientCore.h` 中新增 `ChannelListCallback` 类型和 `onChannelList` 回调成员
2. 在 `ClientCore::handleChannelEvent()` 的 `ChannelList` 分支中解析 protobuf `channel_list` 消息，构建 `std::vector<ChannelInfo>` 并触发 `onChannelList` 回调
3. 在 `MainWindow::setupClientCoreCallbacks()` 中连接 `onChannelList` 回调，调用 `channel_model_->updateFromChannelList()`

**涉及文件**：
- `src/client/include/nevo/client/ClientCore.h` — 新增回调类型和成员
- `src/client/src/ClientCore.cpp` — 解析 ChannelList 消息并触发回调
- `src/ui/src/MainWindow.cpp` — 连接 onChannelList 到 ChannelTreeModel

### 问题 2：延迟和 NAT 显示永远为静态

**现象**：`ConnectionBar::updateLatency()` 和 `updateNatType()` 已实现但从未被调用。`MainWindow::setupClientCoreCallbacks()` 无延迟/NAT 回调。

**修复**：
1. 在 `ClientCore.h` 中新增 `LatencyUpdateCallback` 类型，添加 `onLatencyUpdate` 回调
2. 在 `ClientCore` 中利用已有的 `NetworkManager` 延迟信息，在连接成功后和状态变更时触发延迟回调
3. 在 `MainWindow::setupClientCoreCallbacks()` 中连接回调，调用 `connection_bar_->updateLatency()` 和 `connection_bar_->updateNatType()`
4. 利用 `ClientStateSnapshot` 中已有的 `nat_type` 字段在状态变更时更新 NAT 显示

**涉及文件**：
- `src/client/include/nevo/client/ClientCore.h` — 新增回调
- `src/client/src/ClientCore.cpp` — 触发延迟/NAT 回调
- `src/ui/src/MainWindow.cpp` — 连接回调到 ConnectionBar

### 问题 3：服务器密钥交换占位符（安全问题）

**现象**：`ClientSession.cpp:349` 中 `login_resp->set_server_public_key("placeholder_server_public_key")`，发送硬编码 ASCII 字符串而非真实的 X25519 公钥，导致端到端加密不可用。

**修复**：
1. 在 `ServerCore` 中集成 `VoiceCrypto` 生成会话密钥对
2. 在 `ClientSession::handleLogin()` 中使用 `VoiceCrypto` 生成的真实公钥替换占位符
3. 当 libsodium 不可用时（`!NEVO_HAS_SODIUM`），回退到生成随机字节作为占位，但添加日志警告

**涉及文件**：
- `src/server/include/nevo/server/ServerCore.h` — 添加 VoiceCrypto 成员
- `src/server/src/ServerCore.cpp` — 初始化密钥对
- `src/server/src/ClientSession.cpp` — 替换占位符为真实公钥
- `src/server/include/nevo/server/ClientSession.h` — 添加密钥访问方法

### 问题 4：客户端缺少静音/耳聋/PTT UI 控件

**现象**：`ClientCore` 支持 `setMuted()/setDeafened()/setPttActive()`，但工具栏仅有 Connect/Disconnect 和 Audio Settings 动作，无对应 UI 按钮。

**修复**：
1. 在 `MainWindow.h` 中添加 `mute_action_`、`deafen_action_`、`ptt_action_` 成员
2. 在 `setupMenuBar()` 和工具栏中添加 Mute、Deafen、PTT 按钮（带图标切换）
3. 实现 `onMuteToggled()`、`onDeafenToggled()`、`onPttToggled()` 槽函数
4. 连接到 `ClientCore` 的对应方法
5. 在 `onStateChanged` 回调中根据连接状态启用/禁用这些按钮

**涉及文件**：
- `src/ui/include/nevo/ui/MainWindow.h` — 新增动作成员和槽声明
- `src/ui/src/MainWindow.cpp` — 实现按钮、槽函数和信号连接

### 问题 5：音频设置信号未连接到后端

**现象**：`AudioSettingsWidget` 发射 `inputDeviceChanged`、`outputDeviceChanged`、`vadSensitivityChanged`、`pttKeyChanged`、`noiseSuppressionToggled` 信号，但 `onAudioSettingsAction()` 仅应用 inputVolume 和 outputVolume。

**修复**：
1. 在 `onAudioSettingsAction()` 中，当对话框被接受时，同时应用 VAD 灵敏度（通过 `AudioEngine::setVadEnabled` 配置）、PTT 按键（通过 `ClientCore::setPttActive` 机制）、噪声抑制设置
2. 连接设备切换信号到 AudioEngine（通过 `onDeviceSampleRateChanged` 或日志记录）
3. 添加 `AudioEngine` 的 FEC/丢包率设置接口：新增 `setPacketLossPerc()` 方法

**涉及文件**：
- `src/ui/src/MainWindow.cpp` — 扩展 onAudioSettingsAction
- `src/core/include/nevo/core/audio/AudioEngine.h` — 新增 setPacketLossPerc
- `src/core/src/audio/AudioEngine.cpp` — 实现 setPacketLossPerc
- `src/core/include/nevo/core/audio/OpusEncoder.h` — 新增 setPacketLossPerc
- `src/core/src/audio/OpusEncoder.cpp` — 实现 setPacketLossPerc

### 问题 6：右键上下文菜单未连接

**现象**：`channel_tree_` 和 `user_list_` 设置了 `CustomContextMenu` 策略，但未连接 `customContextMenuRequested` 信号。

**修复**：
1. 在 `MainWindow.h` 中声明 `onChannelContextMenu()` 和 `onUserContextMenu()` 槽
2. 在 `setupDockWidgets()` 中连接 `customContextMenuRequested` 信号
3. 实现上下文菜单：频道树右键显示"Join Channel"，用户列表右键显示"View Info"
4. 频道树菜单项连接到 `onJoinChannelRequested()`，用户列表菜单项显示用户信息

**涉及文件**：
- `src/ui/include/nevo/ui/MainWindow.h` — 新增槽声明
- `src/ui/src/MainWindow.cpp` — 实现上下文菜单

### 问题 7：FEC 冗余度未应用到 Opus 编码器

**现象**：`AudioInput.cpp:169` 中 FEC 冗余度已计算但从未应用，`(void)fec_redundancy;` 被显式忽略。

**修复**：
1. 在 `OpusEncoderWrapper` 中添加 `setFecEnabled()` 和 `setPacketLossPerc()` 方法，使用 `OPUS_SET_INBAND_FEC` 和 `OPUS_SET_PACKET_LOSS_PERC`
2. 在 `AudioEngine` 中暴露 `setPacketLossPerc()` 方法转发到编码器
3. 在 `AudioInput::onEncodedAudio()` 中调用 `engine_->setPacketLossPerc(fec_redundancy)` 替代 `(void)fec_redundancy`
4. 同时启用 `OPUS_SET_INBAND_FEC(1)` 以激活 in-band FEC

**涉及文件**：
- `src/core/include/nevo/core/audio/OpusEncoder.h` — 新增方法声明
- `src/core/src/audio/OpusEncoder.cpp` — 实现 FEC 控制
- `src/core/include/nevo/core/audio/AudioEngine.h` — 新增转发方法
- `src/core/src/audio/AudioEngine.cpp` — 实现转发
- `src/client/src/AudioInput.cpp` — 应用 FEC 冗余度

### 问题 8：服务器端 speaking/mute 状态硬编码

**现象**：`ServerCore.cpp:192-193` 中 `is_speaking = false; // TODO: track speaking state` 和 `is_muted = false; // TODO: track mute state`。`ClientSession` 已通过 `handlePttToggle()` 和 `handleMuteToggle()` 更新了 User 模型的状态，但 `ServerCore::getStatusSnapshot()` 和 `getActiveSessions()` 未从 session 中读取。

**修复**：
1. 在 `ServerCore::getStatusSnapshot()` 中将 `ss.is_speaking = session->user().isSpeaking()` 替代硬编码 `false`
2. 在 `ServerCore::getStatusSnapshot()` 中将 `ss.is_muted = session->user().isMuted()` 替代硬编码 `false`
3. 同样修复 `getActiveSessions()` 中的两处硬编码

**涉及文件**：
- `src/server/src/ServerCore.cpp` — 替换硬编码为实际状态读取

### 问题 9：服务器 UI `disconnect_all_action_` 未添加到菜单

**现象**：`ServerMainWindow.h:136` 声明了 `disconnect_all_action_` 成员，`onDisconnectAll()` 槽已实现，但 `setupMenuBar()` 中从未创建该 action，导致功能不可达。

**修复**：
1. 在 `setupMenuBar()` 中创建 "Disconnect All" action 并添加到 Server 菜单
2. 连接到 `onDisconnectAll()` 槽
3. 在服务器启动/停止时启用/禁用该 action

**涉及文件**：
- `src/server/ui/src/ServerMainWindow.cpp` — 添加 action 创建和连接

### 问题 10：服务器 UI `refresh_timer_` 成员未赋值

**现象**：`ServerMainWindow.h:110` 声明了 `QTimer* refresh_timer_` 成员，但 `setupUi()` 中创建了局部变量 `QTimer* refresh_timer` 而非赋值给成员，导致成员指针悬空。

**修复**：
1. 将 `setupUi()` 中的 `QTimer* refresh_timer = new QTimer(this);` 改为 `refresh_timer_ = new QTimer(this);`
2. 更新后续引用

**涉及文件**：
- `src/server/ui/src/ServerMainWindow.cpp` — 修复变量名

### 问题 11：缺少 .gitignore 文件

**现象**：项目无 `.gitignore`，`build/` 目录等构建产物会被提交。

**修复**：
创建标准 C++ 项目 `.gitignore`，排除构建目录、IDE 文件、OS 临时文件等。

**涉及文件**：
- 新建 `.gitignore`

---

## 边界条件与异常处理

- 所有 UI 回调通过 `postToUiThread()` 安全转发到主线程
- FEC 设置仅在 Opus 可用时生效（`#ifdef NEVO_HAS_OPUS`）
- 密钥交换在 libsodium 不可用时回退到随机字节 + 日志警告
- 上下文菜单仅在有效索引时显示
- 静音/耳聋按钮仅在已连接状态下启用

## 数据流路径

```
Server ChannelList → NetworkManager → ClientCore::handleChannelEvent()
  → onChannelList callback → MainWindow → ChannelTreeModel::updateFromChannelList()

ClientCore::onStateChanged → MainWindow → ConnectionBar::updateLatency/NatType

AudioInput FEC → AudioEngine::setPacketLossPerc → OpusEncoderWrapper::setPacketLossPerc
  → OPUS_SET_PACKET_LOSS_PERC + OPUS_SET_INBAND_FEC

ClientSession PTT/Mute → User::setSpeaking/setMuted → ServerCore::getStatusSnapshot
  → SessionSnapshot.is_speaking/is_muted (正确值)
```

## 预期结果

- 频道树能正确显示服务器频道列表
- 连接状态栏能实时显示延迟和 NAT 类型
- 服务器使用真实密钥进行密钥交换
- 用户可通过工具栏按钮控制静音/耳聋/PTT
- 音频设置中所有控件均能生效
- 右键菜单提供频道/用户操作
- FEC 冗余度正确应用到 Opus 编码器
- 服务器管理界面正确显示用户说话/静音状态
- 服务器 UI 所有声明的功能均可访问
- 项目根目录有完整的 .gitignore
