# NEVO VoIP 系统实现任务计划

- [x] Task 1: 项目脚手架与 CMake 构建体系
    - 1.1: 创建顶层 CMakeLists.txt（C++20, 项目定义, find_package 声明）
    - 1.2: 创建 cmake/ 目录下的辅助模块（CompilerWarnings.cmake, PlatformSetup.cmake, FindOpus.cmake, FindRNNoise.cmake）
    - 1.3: 创建完整目录结构（src/core, src/network, src/server, src/client, src/ui, proto/, tests/, mobile/）
    - 1.4: 创建 src/CMakeLists.txt 子目录入口
    - 1.5: 配置 3rdparty/ 第三方依赖引入方式（FetchContent 或 vcpkg：miniaudio, spdlog, protobuf, libsodium, libargon2, googletest）

- [x] Task 2: 核心基础类型与工具（nevo_core - common 模块）
    - 2.1: 实现 Types.h（UserId, ChannelId, SessionId 等强类型定义，INVALID 常量）
    - 2.2: 实现 Result.h（Result<T> 模板类 + Error 类，支持值或错误存储）
    - 2.3: 实现 Logger.h（基于 spdlog 的日志封装，支持分类标签和异步输出）
    - 2.4: 实现 src/core/CMakeLists.txt（编译 nevo_core 共享库，导出头文件）

- [x] Task 3: 数据模型（nevo_core - model 模块）
    - 3.1: 实现 User.h/cpp（用户模型：id, username, status, muted, deafened, group_id）
    - 3.2: 实现 Channel.h/cpp（树状频道模型：id, name, parent, children, users, 增删操作）
    - 3.3: 实现 Permission.h/cpp（权限位掩码枚举, PermissionGroup 结构体, 权限检查函数）

- [x] Task 4: Protobuf 协议定义与代码生成
    - 4.1: 编写 proto/common.proto（UserId, ChannelId, ResultCode, UserStatus, UserInfo, ChannelInfo）
    - 4.2: 编写 proto/control.proto（TcpPacketHeader 含 request_id, LoginRequest/Response 含密钥交换, 频道操作, NAT 穿透消息, ControlMessage oneof）
    - 4.3: 编写 proto/voice.proto（VoicePacketHeader 含 nonce/auth_tag/tcp_tunnel/fec 字段）
    - 4.4: 配置 CMake protobuf 代码生成规则（protoc 编译 .proto → C++ 头文件和源文件）
    - 4.5: 实现 PacketCodec.h/cpp（基于生成代码的包编解码工具函数）

- [x] Task 5: 音频引擎核心（nevo_core - audio 模块，上）
    - 5.1: 实现 AudioMemoryPool.h/cpp（预分配固定大小内存块池，acquire/release 原子操作，实时安全）
    - 5.2: 实现 OpusEncoder.h/cpp（RAII 封装 Opus 编码器，unique_ptr + 自定义 deleter，PCM float32 → Opus bytes）
    - 5.3: 实现 OpusDecoder.h/cpp（RAII 封装 Opus 解码器，Opus bytes → PCM float32，支持 PLC 丢包补偿）
    - 5.4: 实现 VoiceActivity.h/cpp（VAD 语音激活检测基于 Opus 内置 VAD + 能量阈值，PTT 模式切换）

- [x] Task 6: 音频引擎核心（nevo_core - audio 模块，下）
    - 6.1: 实现 JitterBuffer.h/cpp（按序列号排序环形缓冲区，可配置延迟目标，超时丢弃，FEC 恢复）
    - 6.2: 实现 AudioMixer.h/cpp（多路 PCM 混音，自动限幅防爆音，每用户独立音量控制）
    - 6.3: 实现 Resampler.h/cpp（基于 miniaudio 内置重采样器，支持任意采样率转换到 48kHz，蓝牙 16kHz 适配）
    - 6.4: 实现 AudioEngine.h/cpp（miniaudio 初始化/设备管理, spsc_queue 无锁 FIFO, 输入/输出回调管线, VAD/PTT/音量控制, 设备采样率变化通知）
    - 6.5: 编写 src/core/src/audio/CMakeLists.txt（链接 Opus, miniaudio, boost::lockfree）

- [x] Task 7: 网络库基础（nevo_network - TCP/UDP 连接）
    - 7.1: 实现 TcpConnection.h/cpp（基于 Boost.Asio C++20 协程的 TCP 连接封装，异步读写循环，strand 保护，帧边界协议 [4字节长度][payload]）
    - 7.2: 实现 UdpSocket.h/cpp（基于 Boost.Asio 的 UDP 收发封装，async_receive_from/async_send_to 协程化）
    - 7.3: 实现 SslWrapper.h/cpp（TLS 包装器，基于 OpenSSL/Boost.Beast，用于 TCP 信令加密）
    - 7.4: 实现 ConnectionManager.h/cpp（管理活跃连接集合，心跳超时检测，断连清理）
    - 7.5: 实现 PacketRouter.h/cpp（根据消息类型分发到对应处理器）
    - 7.6: 编写 src/network/CMakeLists.txt（链接 nevo_core, Boost::system, OpenSSL）

- [x] Task 8: 网络库扩展 - NAT 穿透与安全（nevo_network - 高级模块）
    - 8.1: 实现 NatTraversal.h/cpp（STUN Binding Request/Response 编解码, NAT 类型探测逻辑, UDP 打洞流程, TURN 中继申请）
    - 8.2: 实现 VoiceCrypto.h/cpp（AES-128-GCM 加解密封装, nonce 计数器生成, AAD 认证, 密钥轮换逻辑, 旧密钥窗口期保留）
    - 8.3: 实现 TcpVoiceTunnel.h/cpp（TCP 帧协议封装语音数据, 重组缓冲区, 帧边界解析, 发送/接收回调）
    - 8.4: 更新 src/network/CMakeLists.txt（链接 libsodium, libnice）

- [x] Task 9: 服务端核心（nevo_server - 主框架）
    - 9.1: 实现 Database.h/cpp（SQLite3 封装，用户 CRUD，频道 CRUD，配置读写，WAL 模式，Argon2id 密码验证）
    - 9.2: 实现 ChannelManager.h/cpp（树状频道管理，创建/删除/移动频道，用户进出频道，默认频道和 AFK 频道）
    - 9.3: 实现 PermissionManager.h/cpp（基于组的权限检查，位掩码验证，管理员/频道管理员/用户/访客四级组定义）
    - 9.4: 实现 ClientSession.h/cpp（TCP 读取循环, 控制消息处理分发, 登录/登出/加入频道/离开频道处理, UDP 端点绑定, NAT 信息记录）
    - 9.5: 实现 ServerCore.h/cpp（Boost.Asio TCP acceptor + UDP socket 协程循环, 客户端会话生命周期管理, 优雅关闭）
    - 9.6: 实现 AudioRelay.h/cpp（UDP 语音包接收, 按频道转发, VoiceCrypto 解密/重加密, TCP 隧道降级转发, "谁在说话"通知）
    - 9.7: 实现 main.cpp（命令行参数解析, io_context 线程池启动, 信号处理）
    - 9.8: 编写 src/server/CMakeLists.txt（链接 nevo_core, nevo_network, SQLite3, libargon2）

- [x] Task 10: 客户端核心（nevo_client - 主框架）
    - 10.1: 实现 NetworkManager.h/cpp（TCP 连接/断连/重连, UDP 通道建立, NAT 穿透完整流程, 语音包收发, 控制消息收发, 回调注册）
    - 10.2: 实现 AudioInput.h/cpp（从 AudioEngine 获取采集 PCM, Opus 编码, FEC 冗余计算, VoiceCrypto 加密, 通过 NetworkManager 发送）
    - 10.3: 实现 AudioOutput.h/cpp（从 NetworkManager 接收加密语音包, VoiceCrypto 解密, JitterBuffer 排序, Opus 解码, AudioMixer 混音, 送入 AudioEngine 播放）
    - 10.4: 实现 ClientCore.h/cpp（NetworkManager + AudioInput + AudioOutput + AudioEngine 生命周期管理, 连接状态机, 登录/频道操作业务逻辑）
    - 10.5: 编写 src/client/CMakeLists.txt（链接 nevo_core, nevo_network, Opus, miniaudio, libsodium）

- [x] Task 11: Qt 6 客户端界面（nevo_ui）
    - 11.1: 实现 ChannelTreeModel.h/cpp（QAbstractItemModel 封装树状频道数据, 拖拽加入频道, 右键菜单创建/删除频道）
    - 11.2: 实现 UserListModel.h/cpp（QAbstractListModel 封装当前频道用户列表, 说话者高亮动画, 静音/耳机图标）
    - 11.3: 实现 ConnectionBar.h/cpp（连接状态指示, 服务器地址输入, 连接/断开按钮, 延迟/NAT类型显示, 音量滑块）
    - 11.4: 实现 AudioSettingsWidget.h/cpp（输入设备/输出设备选择, VAD 灵敏度调节, PTT 按键设置, 降噪开关, 音量控制）
    - 11.5: 实现 MainWindow.h/cpp（左侧频道树, 右侧用户列表, 底部连接栏, 整体布局, 信号槽连接 ClientCore）
    - 11.6: 实现 main.cpp（QApplication 初始化, ClientCore 实例化, 窗口显示）
    - 11.7: 编写 src/ui/CMakeLists.txt（链接 nevo_client, Qt6::Core, Qt6::Widgets, Qt6::Quick）

- [x] Task 12: 移动端原生适配层
    - 12.1: 实现 Android 原生层（AndroidManifest.xml 权限声明, NevoAudioService.kt 前台服务, 通知渠道）
    - 12.2: 实现 iOS 原生层（Info.plist 后台音频模式, NevoAudioSession.swift AVAudioSession 配置, 蓝牙路由监听）
    - 12.3: 实现移动端 CMake 工具链配置（Android NDK 交叉编译, iOS toolchain 文件）
    - 12.4: 实现平台桥接层（C++ 回调到 Kotlin/Swift 的通知机制, 设备采样率变化通知接口）

- [x] Task 13: 单元测试与集成测试
    - 13.1: 编写 core_tests（Result<T> 测试, Channel 树操作测试, Permission 位掩码测试）
    - 13.2: 编写 audio_tests（OpusEncoder/Decoder 编解码往返测试, JitterBuffer 排序/丢包测试, AudioMixer 限幅测试, AudioMemoryPool 压力测试）
    - 13.3: 编写 network_tests（TcpConnection 回环测试, VoiceCrypto 加解密往返测试, NatTraversal STUN 消息编解码测试）
    - 13.4: 编写 server_tests（模拟客户端登录流程, 频道操作集成测试, AudioRelay 转发测试）
    - 13.5: 编写 tests/CMakeLists.txt（链接 googletest, 注册所有测试）

- [x] Task 14: 端到端集成与验收
    - 14.1: 编写端到端集成脚本（启动服务端 → 多客户端连接 → 登录 → 加入频道 → 语音通话 → 断开）
    - 14.2: 验证 NAT 穿透降级链（本地模拟不同 NAT 类型 → 验证打洞/TURN/TCP 降级）
    - 14.3: 验证音频管线延迟（采集 → 编码 → UDP → 转发 → 解码 → 播放 端到端延迟 < 100ms）
    - 14.4: 验证加密安全性（Wireshark 抓包验证 UDP 载荷不可读, 密钥轮换无中断）
    - 14.5: 验证移动端后台保活（Android 前台服务通知, iOS 后台音频持续播放）
