# NEVO 视频通话模块实现计划

**版本**: v1.0  
**日期**: 2026-06-24  
**依据设计文档**: `docs/superpowers/specs/2026-06-24-wechat-style-video-call-design.md`  
**状态**: 待执行  

---

## 1. 计划概览

本计划将视频通话模块拆分为 7 个阶段、共 32 个可执行任务。每个任务包含：目标、修改文件、关键接口/行为、验收标准、依赖关系。任务按依赖顺序排列，建议尽量按阶段推进，但 M1/M2 可部分并行。

---

## 2. 任务总览

| 阶段 | 主题 | 任务数 | 预估工时 |
|------|------|--------|----------|
| M1 | 共享类型与协议扩展 | 5 | 0.5 周 |
| M2 | 服务端 VideoRelay 改造 | 5 | 1 周 |
| M3 | 客户端 VideoCallManager 核心 | 6 | 2 周 |
| M4 | 编解码器与渲染 | 5 | 2 周 |
| M5 | Python 客户端 UI | 4 | 1.5 周 |
| M6 | Web/服务端 GUI 增强 | 4 | 1 周 |
| M7 | 测试、文档、收尾 | 3 | 1 周 |
| **合计** | | **32** | **9 周** |

---

## 3. 详细任务拆解

### M1: 共享类型与协议扩展（0.5 周）

#### M1-T1: 扩展 `proto/video.proto` 增加 `call_id`

- **目标**: 让视频包头支持一对一通话隔离字段。
- **修改文件**: `proto/video.proto`
- **具体行为**:
  - 在 `VideoPacketHeader` 中新增 `uint64 call_id = 4`（注意调整后续字段编号）。
  - 重新生成或同步手写 `video.pb.h/.cc`（当前项目使用手写生成代码，位于 `proto/generated/`）。
- **验收标准**:
  - `video.pb.h` 中存在 `call_id()` 和 `set_call_id()`。
  - 现有频道级视频转发仍然编译通过。
- **依赖**: 无
- **负责人**: 后端 C++

#### M1-T2: 扩展 `proto/control.proto` 增加视频通话控制消息

- **目标**: 定义发起、接受、拒绝、挂断、媒体协商控制消息。
- **修改文件**: `proto/control.proto`
- **具体行为**:
  - 新增 `VideoCallRequest`、`VideoCallResponse`、`VideoCallHangup`、`VideoProfileUpdate` 消息。
  - 在顶层 `ControlMessage` oneof 中新增 `video_control` 字段。
- **验收标准**:
  - 控制消息可序列化/反序列化。
  - Python 客户端能解析新消息。
- **依赖**: M1-T1
- **负责人**: 后端 C++

#### M1-T3: 扩展 `ControlMessageType` 枚举

- **目标**: 为控制面消息分配类型 ID。
- **修改文件**: `src/core/include/nevo/core/protocol/PacketTypes.h`
- **具体行为**:
  - 新增：
    - `VideoCallRequest = 50`
    - `VideoCallResponse = 51`
    - `VideoCallHangup = 52`
    - `VideoProfileUpdate = 53`
- **验收标准**:
  - 枚举值不冲突，编译通过。
- **依赖**: 无
- **负责人**: 后端 C++

#### M1-T4: 创建 `src/core/video/VideoTypes.h`

- **目标**: 定义共享视频类型、状态枚举、编解码器能力。
- **修改文件**:
  - 新增 `src/core/include/nevo/core/video/VideoTypes.h`
  - 扩展 `src/core/CMakeLists.txt` 安装头文件
- **具体行为**:
  - 定义 `VideoCodec`、`CodecCapability`、`VideoProfile`、`VideoFrame`、`VideoFrameType`。
  - 定义 `VideoCallState`、`VideoCallEndReason`、`VideoCallId`。
  - 提供 `videoCodecToString`、`videoCallStateToString` 辅助函数。
- **验收标准**:
  - 可被 `nevo_client`、`nevo_server`、`voice_relay_sim` 引用并编译通过。
- **依赖**: 无
- **负责人**: 后端 C++

#### M1-T5: 创建 `IVideoSource` 与 `IVideoSink` 抽象接口

- **目标**: 抽象视频采集与渲染，支持不同平台实现。
- **修改文件**:
  - 新增 `src/core/include/nevo/core/video/IVideoSource.h`
  - 新增 `src/core/include/nevo/core/video/IVideoSink.h`
- **具体行为**:
  - `IVideoSource`: `startCapture`、`stopCapture`、`enumerateDevices`、`selectDevice`、`setEnabled`、`onEncodedFrame`。
  - `IVideoSink`: `renderFrame`、`clear`、`setRenderSize`。
- **验收标准**:
  - 接口编译通过，可被客户端和测试程序实现。
- **依赖**: M1-T4
- **负责人**: 后端 C++

---

### M2: 服务端 VideoRelay 改造（1 周）

#### M2-T1: 在 `VideoClientMapping` 中增加 `call_id`

- **目标**: 让参与者同时属于频道和通话。
- **修改文件**: `src/server/include/nevo/server/VideoRelay.h`
- **具体行为**:
  - 在 `VideoClientMapping` 中新增 `std::optional<VideoCallId> active_call_id;`。
- **验收标准**:
  - 结构体大小合理，不影响现有按频道转发。
- **依赖**: M1-T4
- **负责人**: 后端 C++

#### M2-T2: 新增通话级参与者注册/注销接口

- **目标**: 支持按 `call_id` 管理参与者。
- **修改文件**:
  - `src/server/include/nevo/server/VideoRelay.h`
  - `src/server/src/VideoRelay.cpp`
- **具体行为**:
  - 新增：
    - `registerCallParticipant(VideoCallId, UserId, endpoint, capabilities)`
    - `unregisterCallParticipant(VideoCallId, UserId)`
    - `updateCallParticipantEndpoint(VideoCallId, UserId, endpoint)`
  - 维护 `call_id → set<UserId>` 索引。
- **验收标准**:
  - 注册/注销后 `findUserByEndpoint` 和通话 peer 查找正确。
- **依赖**: M2-T1
- **负责人**: 后端 C++

#### M2-T3: 改造 `handleVideoPacket` 支持 `call_id` 隔离

- **目标**: 一对一视频包只转发给同通话对端。
- **修改文件**: `src/server/src/VideoRelay.cpp`
- **具体行为**:
  - 解析 `VideoPacketHeader.call_id`。
  - 若 `call_id != 0`，只转发给同 `call_id` 且 `user_id != sender_id` 的参与者。
  - 若 `call_id == 0`，保持原有按频道转发逻辑。
- **验收标准**:
  - 单元测试：同 call_id 参与者收到，其他 call_id 参与者收不到。
  - 单元测试：call_id=0 仍按频道转发。
- **依赖**: M2-T2
- **负责人**: 后端 C++

#### M2-T4: 在 `ClientSession` 中处理视频通话控制消息

- **目标**: 服务端控制面接收并转发视频通话信令。
- **修改文件**:
  - `src/server/include/nevo/server/ClientSession.h`
  - `src/server/src/ClientSession.cpp`
- **具体行为**:
  - 在 `handleControlMessage` 中新增分支处理：
    - `VideoCallRequest`：转发给目标用户
    - `VideoCallResponse`：转发给发起者
    - `VideoCallHangup`：广播给通话双方
    - `VideoProfileUpdate`：转发给通话对端
  - 调用 `ServerCore` 接口注册/注销 `VideoRelay` 通话参与者。
- **验收标准**:
  - 信令能正确路由到目标会话。
- **依赖**: M2-T3, M1-T2
- **负责人**: 后端 C++

#### M2-T5: 在 `ServerCore` 中新增 `VideoRelay` 通话参与者管理

- **目标**: 连接 `ClientSession` 与 `VideoRelay`。
- **修改文件**:
  - `src/server/include/nevo/server/ServerCore.h`
  - `src/server/src/ServerCore.cpp`
- **具体行为**:
  - 新增 `registerVideoCallParticipant(call_id, user_id, endpoint, capabilities)`。
  - 新增 `unregisterVideoCallParticipant(call_id, user_id)`。
  - 在客户端断开时自动清理其参与的通话映射。
- **验收标准**:
  - 客户端断开后，相关 call_id 的转发映射被清理。
- **依赖**: M2-T4
- **负责人**: 后端 C++

---

### M3: 客户端 VideoCallManager 核心（2 周）

#### M3-T1: 创建 `VideoCallManager.h/.cpp` 骨架

- **目标**: 建立客户端视频通话管理器类。
- **修改文件**:
  - 新增 `src/client/include/nevo/client/VideoCallManager.h`
  - 新增 `src/client/src/VideoCallManager.cpp`
  - 扩展 `src/client/CMakeLists.txt`
- **具体行为**:
  - 类持有 `NetworkManager&`、`IVideoSource`、`IVideoSink`、状态机。
  - 实现构造函数、析构函数、基本查询方法。
- **验收标准**:
  - `nevo_client` 库编译通过。
- **依赖**: M1-T5
- **负责人**: 后端 C++

#### M3-T2: 实现通话状态机

- **目标**: 管理 Idle → Calling → Ringing → Connecting → Connected → Ended 转换。
- **修改文件**: `src/client/src/VideoCallManager.cpp`
- **具体行为**:
  - `initiateCall` 切换到 Calling。
  - 收到 `VideoCallResponse` 后根据 accepted 进入 Connecting 或 Ended。
  - 收到 `VideoCallRequest` 切换到 Ringing。
  - 首个 KeyFrame 交互成功后进入 Connected。
  - `hangUp` / `rejectCall` / 对端挂断 / 超时进入 Ended。
  - 超时定时器：Calling 30s、Connecting 5s。
- **验收标准**:
  - 单元测试覆盖所有状态转换与超时。
- **依赖**: M3-T1
- **负责人**: 后端 C++

#### M3-T3: 实现视频包发送路径

- **目标**: 将编码后的视频帧发送到服务端。
- **修改文件**:
  - `src/client/src/VideoCallManager.cpp`
  - `src/client/include/nevo/client/NetworkManager.h`（扩展）
  - `src/client/src/NetworkManager.cpp`（扩展）
- **具体行为**:
  - `IVideoSource::onEncodedFrame` 回调触发打包。
  - 构建 `VideoPacketHeader`（含 `call_id`、sequence_number、timestamp、frame_type）。
  - 使用 `VoiceCrypto` 加密 payload，AAD 为 header。
  - 通过 `NetworkManager::sendVideoPacket` 走 UDP（或 TCP 隧道）。
  - 实现 NAL 分片：payload 超过 1200 字节时拆分 fragment。
- **验收标准**:
  - 单元测试：分片与重组正确。
  - 端到端测试：视频包能从客户端到达服务端 VideoRelay。
- **依赖**: M3-T2
- **负责人**: 后端 C++

#### M3-T4: 实现视频包接收路径

- **目标**: 接收远端视频包并交给渲染器。
- **修改文件**:
  - `src/client/src/VideoCallManager.cpp`
  - `src/client/src/NetworkManager.cpp`
- **具体行为**:
  - `NetworkManager` 收到视频 UDP 包回调给 `VideoCallManager::onVideoPacket`。
  - 解密 payload。
  - 按 `call_id` 过滤非当前通话的包。
  - 重组分片，恢复完整 NAL。
  - 调用 `IVideoSink::renderFrame`。
- **验收标准**:
  - 单元测试：解密、重组、过滤正确。
- **依赖**: M3-T3
- **负责人**: 后端 C++

#### M3-T5: 实现编解码器能力协商

- **目标**: 通话建立时协商双方支持的编解码器、分辨率、帧率。
- **修改文件**: `src/client/src/VideoCallManager.cpp`
- **具体行为**:
  - `initiateCall` 携带本地 `CodecCapability` 列表。
  - 收到 `VideoCallRequest` 后选择交集，在 `acceptCall` 的 `VideoCallResponse` 中返回 `VideoProfile`。
  - 协商策略：优先 H.264，其次降级到双方支持的最高配置。
- **验收标准**:
  - 单元测试：交集选择、降级策略正确。
- **依赖**: M3-T2
- **负责人**: 后端 C++

#### M3-T6: 集成静音、关闭摄像头、切换摄像头

- **目标**: 通话中媒体控制。
- **修改文件**: `src/client/src/VideoCallManager.cpp`
- **具体行为**:
  - `setLocalAudioMuted`：通知 `AudioEngine` 静音并发送控制消息更新对端状态。
  - `setLocalVideoEnabled`：停止/恢复 `IVideoSource` 采集。
  - 支持 `IVideoSource::selectDevice` 切换前后摄像头。
- **验收标准**:
  - UI 操作后本地状态与对端状态同步。
- **依赖**: M3-T4
- **负责人**: 后端 C++

---

### M4: 编解码器与渲染（2 周）

#### M4-T1: 引入 H.264 编码依赖

- **目标**: 让项目可链接 H.264 编码器。
- **修改文件**:
  - `CMakeLists.txt`
  - `vcpkg.json`（如使用）
- **具体行为**:
  - 添加 `find_package(OpenH264)` 或 `find_package(x264)`。
  - 找不到时降级为 stub/明文测试帧。
  - 定义 `NEVO_HAS_H264_ENCODER` 宏。
- **验收标准**:
  - 配置阶段能检测到编码库，无库时不阻断其他功能。
- **依赖**: 无
- **负责人**: 后端 C++/DevOps

#### M4-T2: 实现 `OpenH264VideoSource`

- **目标**: C++ 默认视频采集+编码源。
- **修改文件**:
  - 新增 `src/client/src/video/OpenH264VideoSource.cpp`
  - 新增 `src/client/include/nevo/client/video/OpenH264VideoSource.h`
- **具体行为**:
  - 使用 OpenCV 采集摄像头帧。
  - 使用 OpenH264 编码为 H.264 NAL。
  - 按协商的 `VideoProfile` 设置分辨率/帧率/码率。
  - 定时触发 `onEncodedFrame`。
- **验收标准**:
  - 能输出连续 H.264 NAL，IDR 帧可识别。
- **依赖**: M4-T1, M1-T5
- **负责人**: 后端 C++

#### M4-T3: 实现 `OpenH264VideoSink`

- **目标**: C++ 默认 H.264 解码+渲染器。
- **修改文件**:
  - 新增 `src/client/src/video/OpenH264VideoSink.cpp`
  - 新增 `src/client/include/nevo/client/video/OpenH264VideoSink.h`
- **具体行为**:
  - 使用 OpenH264 解码 H.264 NAL。
  - 将 YUV 转为 RGB（可用 OpenCV）。
  - 通过 Qt/QPainter 或 SDL/Raw 渲染。
- **验收标准**:
  - 能解码并显示本地测试流。
- **依赖**: M4-T2
- **负责人**: 后端 C++

#### M4-T4: 实现网络自适应

- **目标**: 根据网络状况调整码率/分辨率/帧率。
- **修改文件**: `src/client/src/VideoCallManager.cpp`
- **具体行为**:
  - 监测 RTT、丢包率、抖动。
  - 实现升降级策略（详见设计文档 8.2）。
  - 通过 `VideoProfileUpdate` 通知对端。
- **验收标准**:
  - 模拟高丢包时码率/分辨率下降；恢复后回升。
- **依赖**: M3-T6
- **负责人**: 后端 C++

#### M4-T5: 实现关键帧请求（PLI/FIR）

- **目标**: 丢包花屏时快速恢复。
- **修改文件**:
  - `proto/control.proto`（新增 PLI/FIR 控制消息）
  - `src/client/src/VideoCallManager.cpp`
  - `src/server/src/ClientSession.cpp`
- **具体行为**:
  - 检测到连续丢包或解码失败时发送 PLI。
  - 收到 PLI 后立即编码发送 KeyFrame。
- **验收标准**:
  - 丢包后能在 1 个 GOP 内恢复清晰画面。
- **依赖**: M4-T3
- **负责人**: 后端 C++

---

### M5: Python 客户端 UI（1.5 周）

#### M5-T1: 扩展 `nevo_wire.py` 支持视频通话控制消息

- **目标**: Python 客户端能收发视频通话信令。
- **修改文件**: `src/client/gui_python/nevo_wire.py`
- **具体行为**:
  - 新增 `encode_video_call_request`、`decode_video_call_response` 等函数。
  - 与 C++ 服务端控制消息二进制格式兼容。
- **验收标准**:
  - Python 与 C++ 控制消息可互解析。
- **依赖**: M1-T2
- **负责人**: Python GUI

#### M5-T2: 在 `nevo_client.py` 中新增视频通话状态机

- **目标**: Python 客户端核心具备视频通话能力。
- **修改文件**: `src/client/gui_python/nevo_client.py`
- **具体行为**:
  - 新增 `NevoClient::start_video_call(peer_id)`、`accept_video_call()`、`reject_video_call()`、`hangup_video_call()`。
  - 处理 incoming call 回调。
  - 调用 `VideoCallManager`（C++ 绑定）或纯 Python 视频引擎。
- **验收标准**:
  - 状态机与 C++ VideoCallManager 一致。
- **依赖**: M5-T1, M3-T2
- **负责人**: Python GUI

#### M5-T3: 实现 Python 视频采集与渲染控件

- **目标**: Python 端摄像头采集和画面显示。
- **修改文件**:
  - 新增 `src/client/gui_python/video_widgets.py`
- **具体行为**:
  - 使用 OpenCV 或 PyQt5 QCamera 采集。
  - 使用 QLabel + QPixmap 渲染远端/本地画面。
  - 实现本地预览小窗（可拖动）。
- **验收标准**:
  - 摄像头画面能实时显示在 QLabel 中。
- **依赖**: M5-T2
- **负责人**: Python GUI

#### M5-T4: 实现类微信风格视频通话对话框

- **目标**: 发起、接听、通话中 UI。
- **修改文件**:
  - 新增 `src/client/gui_python/video_call_dialog.py`
  - 扩展 `src/client/gui_python/main_window.py`
- **具体行为**:
  - 联系人列表增加视频通话按钮和在线状态。
  - 发起呼叫界面：大头像、昵称、状态文字、取消按钮。
  - 来电接听界面：接听/拒绝大按钮。
  - 通话中界面：远端全屏、本地小窗、底部操作栏（静音、关摄像头、切换摄像头、挂断）。
- **验收标准**:
  - 界面风格与微信视频通话一致，操作流畅。
- **依赖**: M5-T3
- **负责人**: Python GUI

---

### M6: Web/服务端 GUI 增强（1 周）

#### M6-T1: 在 `ServerCore` 中暴露视频通话统计

- **目标**: 让管理界面能获取通话数据。
- **修改文件**:
  - `src/server/include/nevo/server/ServerCore.h`
  - `src/server/src/ServerCore.cpp`
- **具体行为**:
  - 新增 `ServerStatusSnapshot` 字段：当前进行中的视频通话数、每对通话的码率/丢包率。
  - `VideoRelay` 提供通话级统计接口。
- **验收标准**:
  - `ControlServer` 能读取视频通话统计。
- **依赖**: M2-T5
- **负责人**: 后端 C++

#### M6-T2: 在 C++ 服务端 GUI 新增视频通话监控面板

- **目标**: 服务端管理界面显示视频通话状态。
- **修改文件**:
  - 新增或扩展 `src/server/ui/src/VideoCallMonitor.cpp`
  - 扩展 `src/server/ui/src/ServerMainWindow.cpp`
- **具体行为**:
  - 新增面板显示：通话 ID、双方用户、持续时间、码率、丢包率。
  - 支持强制结束某路通话。
- **验收标准**:
  - 面板能实时刷新。
- **依赖**: M6-T1
- **负责人**: C++ Qt

#### M6-T3: 扩展 Web 代理支持视频通话 REST API

- **目标**: Web 端能读取视频通话统计。
- **修改文件**: `web/server.py`
- **具体行为**:
  - 新增 `/api/video_calls` GET/DELETE。
  - 新增 `/api/video_calls/stream` SSE 实时推送。
- **验收标准**:
  - Web 端能正确显示/操作视频通话。
- **依赖**: M6-T1
- **负责人**: Web

#### M6-T4: 在 Web 前端新增视频通话监控仪表盘

- **目标**: Web 管理界面展示视频通话。
- **修改文件**:
  - `web/index.html`
  - `web/js/app.js`
  - `web/css/style.css`
- **具体行为**:
  - 新增 "视频通话" 标签页。
  - 显示当前通话列表、统计图表、强制挂断按钮。
- **验收标准**:
  - UI 正常展示，操作反馈正确。
- **依赖**: M6-T3
- **负责人**: Web

---

### M7: 测试、文档、收尾（1 周）

#### M7-T1: 修复现有测试构建问题并补充视频单元测试

- **目标**: 让新增测试能运行。
- **修改文件**:
  - `CMakeLists.txt`（修复 `GTest::gtest_main` 检测）
  - 新增 `tests/video/VideoTypesTest.cpp`
  - 新增 `tests/video/VideoRelayTest.cpp`
  - 新增 `tests/video/VideoCallManagerTest.cpp`
- **具体行为**:
  - 将 `TARGET GTest::gtest_main` 改为同时检查 `gtest_main`。
  - 编写 VideoTypes 序列化、VideoFrame 分片、状态机、VideoRelay call_id 隔离测试。
- **验收标准**:
  - `ctest` 能发现测试并全部通过。
- **依赖**: M3, M4
- **负责人**: 测试/后端 C++

#### M7-T2: 编写视频通话 API 文档与集成示例

- **目标**: 提供完整开发者文档。
- **修改文件**:
  - 新增 `docs/video_call_api.md`
  - 更新 `README.md`
- **具体行为**:
  - API 接口说明。
  - Python/C++ 集成示例。
  - 测试用例说明。
  - 性能优化建议。
- **验收标准**:
  - 文档完整、示例可运行。
- **依赖**: M5, M6
- **负责人**: 文档

#### M7-T3: 端到端验证与性能调优

- **目标**: 确保模块可工作并达到性能指标。
- **修改文件**: 调优相关
- **具体行为**:
  - 本地双客户端视频通话验证。
  - 网络劣化测试（Clumsy/NetEm）。
  - CPU/内存占用分析。
  - 并发压力测试。
- **验收标准**:
  - 端到端延迟 < 300ms（同城）。
  - CPU 占用可接受（默认 360p@24fps）。
  - 服务端支持 ≥50 对并发。
- **依赖**: M7-T1
- **负责人**: 测试/后端 C++

---

## 4. 依赖关系图

```
M1-T1 ──┬──► M2-T1 ──► M2-T2 ──► M2-T3 ──► M2-T4 ──► M2-T5
M1-T2 ──┤                                    │
M1-T3 ──┤                                    ▼
M1-T4 ──┼──► M1-T5 ──► M3-T1 ──► M3-T2 ──► M3-T3 ──► M3-T4 ──► M3-T5 ──► M3-T6
        │                                              │
        │                                              ▼
        │                                           M4-T1 ──► M4-T2 ──► M4-T3 ──► M4-T4 ──► M4-T5
        │                                              │
        │                                              ▼
        │                                           M5-T1 ──► M5-T2 ──► M5-T3 ──► M5-T4
        │                                              │
        │                                              ▼
        │                                           M6-T1 ──► M6-T2
        │                                              │
        │                                              ▼
        │                                           M6-T3 ──► M6-T4
        │                                              │
        └──────────────────────────────────────────────┴──► M7-T1 ──► M7-T2 ──► M7-T3
```

---

## 5. 关键决策点

| 决策 | 当前选择 | 备注 |
|------|----------|------|
| 传输协议 | 自研 UDP + TCP 隧道回退 | 复用现有 NEVO 基础设施 |
| 默认编解码器 | H.264（OpenH264） | 兼容性最佳 |
| 通话隔离 | 按 `call_id` | 在现有 `VideoRelay` 上扩展 |
| 加密模型 | 服务器解密再加密（per-client） | 与语音一致，非严格 E2EE |
| 客户端实现 | Python PyQt5 为主 | 当前唯一用户 GUI |
| 多人视频 | 本期不做 | 架构预留 |

---

## 6. 风险与应急预案

| 风险 | 阶段 | 应急方案 |
|------|------|----------|
| OpenH264 在 Windows 编译困难 | M4 | 改用 x264 或 FFmpeg libx264；若均失败，先用纯软件 stub 保证链路 |
| Python 视频渲染性能不足 | M5 | 改用 PyQt6 或 QML，或回退到 C++ 插件 |
| UDP 视频大包丢包严重 | M4 | 强化分片 + NACK/PLI + TCP 隧道回退 |
| 现有 `VoiceCrypto` 处理视频高吞吐成瓶颈 | M3/M4 | 评估是否需要独立视频密钥上下文或批量处理 |
| 测试构建 CMake 问题未修复 | M7 | 作为首个阻塞任务，优先修复 |

---

## 7. 建议执行顺序

### 第一周：M1 + M2

- 优先完成协议扩展与 `VideoRelay` 改造，这是后续所有工作的基础。
- 并行进行 Python 控制消息扩展（M5-T1）。

### 第二～三周：M3

- 重点实现 `VideoCallManager` 状态机、收发路径、协商。
- 同步开始 C++ 视频采集/渲染（M4-T1/T2/T3）。

### 第四～五周：M4 + M5

- 完成编解码器集成与 Python UI。
- 此时应能进行本地端到端视频通话。

### 第六周：M6

- Web 监控、服务端 GUI 统计面板。

### 第七周：M7

- 测试修复、文档、性能调优、收尾。

---

## 8. 验收总标准

- [ ] 两名用户可在 Python 客户端之间发起、接听、挂断一对一视频通话。
- [ ] 通话中支持静音、关闭摄像头、切换摄像头。
- [ ] 视频画面延迟 < 300ms（同城网络）。
- [ ] 服务端 `VideoRelay` 按 `call_id` 隔离，不会把 A 的视频转发给 B 的错误对象。
- [ ] Web 管理界面可查看当前视频通话并强制结束。
- [ ] 所有新增单元测试通过，`ctest` 能正常发现测试。
- [ ] 文档 `docs/video_call_api.md` 完整，示例可运行。

---

## 9. 附录：推荐首个可执行任务

若希望立即开始编码，建议按以下顺序执行前三个任务：

1. **M7-T1（前置修复）**: 修复 `CMakeLists.txt` 中 `GTest::gtest_main` 检测，确保测试子目录能加入。
2. **M1-T4 + M1-T5**: 创建 `VideoTypes.h`、`IVideoSource.h`、`IVideoSink.h`。
3. **M1-T1 + M1-T2 + M1-T3**: 扩展 `proto/video.proto`、`proto/control.proto`、`PacketTypes.h`。

完成以上任务后，即可进入 M2 服务端改造。
