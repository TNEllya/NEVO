# NEVO 项目已知问题修复任务计划

- [x] Task 1: 添加 .gitignore 文件
    - 1.1: 创建标准 C++ 项目 .gitignore，排除 build/、IDE 文件、OS 临时文件

- [x] Task 2: 修复客户端 UI 频道树数据流断裂
    - 2.1: 在 ClientCore.h 中新增 ChannelListCallback 类型和 onChannelList 回调成员
    - 2.2: 在 ClientCore::handleChannelEvent() 中解析 ChannelList protobuf 消息，构建 vector<ChannelInfo> 并触发 onChannelList 回调
    - 2.3: 在 MainWindow::setupClientCoreCallbacks() 中连接 onChannelList 到 channel_model_->updateFromChannelList()

- [x] Task 3: 修复延迟和 NAT 显示永远为静态
    - 3.1: 在 ClientCore.h 中新增 LatencyUpdateCallback 类型和 onLatencyUpdate 回调成员
    - 3.2: 在 ClientCore 连接成功及状态变更时触发 onLatencyUpdate 回调，传递延迟和 NAT 信息
    - 3.3: 在 MainWindow::setupClientCoreCallbacks() 中连接回调到 connection_bar_->updateLatency() 和 updateNatType()

- [x] Task 4: 修复服务器密钥交换占位符
    - 4.1: 在 ServerCore 中添加 VoiceCrypto 实例和密钥对生成逻辑
    - 4.2: 在 ClientSession 中添加获取服务器公钥的接口
    - 4.3: 替换 ClientSession::handleLogin() 中的占位符为真实 X25519 公钥，libsodium 不可用时回退到随机字节

- [x] Task 5: 添加静音/耳聋/PTT UI 控件
    - 5.1: 在 MainWindow.h 中添加 mute_action_、deafen_action_、ptt_action_ 成员和对应槽声明
    - 5.2: 在 setupMenuBar() 和工具栏中创建 Mute、Deafen、PTT 按钮
    - 5.3: 实现 onMuteToggled()、onDeafenedToggled()、onPttToggled() 槽函数并连接到 ClientCore
    - 5.4: 在 onStateChanged 回调中根据连接状态启用/禁用按钮，同步按钮勾选状态

- [x] Task 6: 连接音频设置信号到后端
    - 6.1: 在 OpusEncoderWrapper 中添加 setFecEnabled() 和 setPacketLossPerc() 方法
    - 6.2: 在 AudioEngine 中添加 setPacketLossPerc() 转发方法
    - 6.3: 扩展 MainWindow::onAudioSettingsAction()，应用 VAD 灵敏度、PTT 按键、噪声抑制设置

- [x] Task 7: 实现右键上下文菜单
    - 7.1: 在 MainWindow.h 中声明 onChannelContextMenu 和 onUserContextMenu 槽
    - 7.2: 在 setupDockWidgets() 中连接 customContextMenuRequested 信号
    - 7.3: 实现频道树右键菜单（Join Channel）和用户列表右键菜单（View Info）

- [x] Task 8: 应用 FEC 冗余度到 Opus 编码器
    - 8.1: 修改 AudioInput::onEncodedAudio()，将 fec_redundancy 通过 engine_->setPacketLossPerc() 应用到编码器
    - 8.2: 启用 Opus in-band FEC（OPUS_SET_INBAND_FEC）

- [x] Task 9: 修复服务器端 speaking/mute 状态硬编码
    - 9.1: 替换 getStatusSnapshot() 中 is_speaking 和 is_muted 硬编码为从 session->user() 读取实际值
    - 9.2: 替换 getActiveSessions() 中同样的硬编码

- [x] Task 10: 修复服务器 UI disconnect_all_action_ 未添加到菜单
    - 10.1: 在 setupMenuBar() 中创建 "Disconnect All" action 并添加到 Server 菜单
    - 10.2: 连接到 onDisconnectAll() 槽，在服务器启动/停止时控制启用状态

- [x] Task 11: 修复服务器 UI refresh_timer_ 成员未赋值
    - 11.1: 将 setupUi() 中的局部变量 QTimer* refresh_timer 改为成员变量 refresh_timer_
