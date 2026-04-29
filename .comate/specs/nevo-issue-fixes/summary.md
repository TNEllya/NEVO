# NEVO 项目已知问题修复 — 完成总结

## 概述

根据项目完成度分析报告，修复了 11 项已知问题和功能缺失，涉及 13 个文件的修改和 1 个新文件的创建。

## 修改文件清单

| 文件 | 修改类型 | 说明 |
|------|---------|------|
| `.gitignore` | 新建 | 标准 C++ 项目 gitignore |
| `src/client/include/nevo/client/ClientCore.h` | 修改 | 新增 ChannelListCallback、LatencyUpdateCallback 类型和 onChannelList、onLatencyUpdate 回调成员；添加 ChannelTreeModel.h 引用 |
| `src/client/src/ClientCore.cpp` | 修改 | 解析 ChannelList protobuf 消息并触发 onChannelList 回调；在 setState() 中触发 onLatencyUpdate 回调 |
| `src/ui/include/nevo/ui/MainWindow.h` | 修改 | 新增 mute_action_、deafen_action_、ptt_action_ 成员；新增 onMuteToggled/onDeafenedToggled/onPttToggled/onChannelContextMenu/onUserContextMenu 槽声明 |
| `src/ui/src/MainWindow.cpp` | 修改 | 连接 onChannelList 到 ChannelTreeModel；连接 onLatencyUpdate 到 ConnectionBar；添加 Mute/Deafen/PTT 按钮到菜单和工具栏；实现三个音频控制槽函数；实现频道树和用户列表右键菜单；扩展 onAudioSettingsAction 应用全部设置 |
| `src/server/include/nevo/server/ServerCore.h` | 修改 | 添加 VoiceCrypto.h 引用、server_session_key_ 成员和 serverSessionKey() 方法 |
| `src/server/src/ServerCore.cpp` | 修改 | 在 initialize() 中生成服务器会话密钥（libsodium 优先，std::random_device 回退）；替换 speaking/mute 硬编码为从 User 模型读取实际值 |
| `src/server/src/ClientSession.cpp` | 修改 | 替换 "placeholder_server_public_key" 为真实服务器会话密钥 |
| `src/server/ui/src/ServerMainWindow.cpp` | 修改 | 添加 Disconnect All action 到菜单并连接槽函数；修复 refresh_timer_ 局部变量改为成员变量 |
| `src/core/include/nevo/core/audio/OpusEncoder.h` | 修改 | 新增 setFecEnabled() 和 setPacketLossPerc() 方法声明 |
| `src/core/src/audio/OpusEncoder.cpp` | 修改 | 实现 setFecEnabled() 和 setPacketLossPerc()（含 stub 版本） |
| `src/core/include/nevo/core/audio/AudioEngine.h` | 修改 | 新增 setFecEnabled()、setPacketLossPerc()、setVadSensitivity() 方法声明 |
| `src/core/src/audio/AudioEngine.cpp` | 修改 | 实现 setFecEnabled()、setPacketLossPerc()、setVadSensitivity() 转发方法 |
| `src/client/src/AudioInput.cpp` | 修改 | 将 FEC 冗余度通过 engine_->setFecEnabled/setPacketLossPerc 应用到编码器，移除 TODO 和 (void)fec_redundancy |

## 各任务完成情况

1. **添加 .gitignore** — 创建了完整的 C++ 项目 gitignore，排除 build/、IDE 文件、OS 临时文件等
2. **修复频道树数据流断裂** — ClientCore 解析 ChannelList protobuf → 触发 onChannelList 回调 → MainWindow 更新 ChannelTreeModel
3. **修复延迟/NAT 显示静态** — 新增 LatencyUpdateCallback，连接成功时触发，MainWindow 转发到 ConnectionBar
4. **修复密钥交换占位符** — ServerCore 生成真实会话密钥（libsodium 或 std::random_device），ClientSession 发送真实密钥
5. **添加静音/耳聋/PTT 控件** — MainWindow 工具栏和菜单添加三个可勾选 action，连接到 ClientCore 对应方法
6. **连接音频设置到后端** — OpusEncoder 新增 FEC 控制，AudioEngine 转发 FEC/VAD 设置，MainWindow 应用全部音频配置
7. **实现右键上下文菜单** — 频道树右键"Join Channel"，用户列表右键"View Info"显示用户详情
8. **应用 FEC 到编码器** — AudioInput::onEncodedAudio() 调用 engine_->setFecEnabled/setPacketLossPerc
9. **修复 speaking/mute 硬编码** — ServerCore::getStatusSnapshot() 和 getActiveSessions() 从 session->user() 读取 isSpeaking/isMuted
10. **修复 disconnect_all_action_** — 在 setupMenuBar() 中创建并添加到 Server 菜单，服务器运行时启用
11. **修复 refresh_timer_** — 局部变量改为成员变量赋值

## 无破坏性变更

所有修改均在现有代码结构内进行：
- 新增方法均有 stub 回退（如 OpusEncoder 无 Opus 时为 no-op）
- UI 变更在 #ifdef NEVO_HAS_BOOST 保护下
- 密钥生成有 libsodium → std::random_device 的条件编译回退
- 未删除任何现有公共 API
