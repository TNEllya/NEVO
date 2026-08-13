# Debug Session: client-ab-no-comm

**Status:** [OPEN]
**Started:** 2026-07-04
**Symptom:** 客户端 A 与 B 在 192.168.31.39 服务器上无法进行语音和屏幕共享通讯

## Hypotheses

### H1: 视频 UDP 端口（24432）未在 Docker 中映射 ⭐ 强嫌疑
- **观察点**: docker-compose.yml 只映射 `24431/udp`，但服务端实际在 `udp_port+1=24432` 启动视频中继
- **证据线索**: 服务端日志 `Video UDP relay bound on port 24432`
- **验证方法**: 检查 `docker port`、`docker compose ps` 端口映射；外部访问 24432/udp

### H2: 客户端未正确获取视频 UDP 端口
- **观察点**: 客户端 video_engine 需要从登录响应或其他控制消息获取 `server_video_udp_port`
- **验证方法**: 检查 nevo_client.py 中 `server_video_udp_port` 的赋值逻辑

### H3: Per-client 加密密钥在成功路径未注册到 server 映射
- **观察点**: 当 crypto_box_seal 成功时，`generateSessionKeyForClient` 是否同时将密钥注册到 per-client map
- **验证方法**: 检查 `generateSessionKeyForClient` 实现

### H4: AudioRelay 频道映射时机问题
- **观察点**: 客户端登录时已加入默认频道，但 AudioRelay 的 channel 信息可能未及时同步
- **验证方法**: 检查 `addClientMapping` 与 `updateClientChannel` 的调用顺序

### H5: UDP 端点地址归一化在 NAT 环境下失效
- **观察点**: IPv4-mapped IPv6 地址处理
- **验证方法**: 检查实际客户端 UDP 包到达服务器时的源地址格式

## Investigation Log

### Step 1: 静态代码审查（完成）

**H1 确认** ✅: Docker 端口映射缺失
- `Dockerfile` L137: `EXPOSE 24430/tcp 24431/udp 24433/tcp 8090/tcp` — 缺少 24432/udp
- `docker-compose.yml` L31-35: 只映射 24431/udp — 缺少 24432/udp
- `docker-compose.dev.yml` L17-20: 同样缺失
- 服务端日志确认: `Video UDP relay bound on port 24432`
- 客户端从 login_resp 获取 `server_video_udp_port=24432`，发包到 24432 — 被 Docker 丢弃

**H2 排除** ✅: 客户端正确获取视频端口
- `ClientSession.cpp` L432: `login_resp->set_server_video_udp_port(server_core_->videoUdpPort())`
- `nevo_client.py` L404-405: 从 login_resp 读取并设置
- 客户端端口号正确，问题在服务端 Docker 端口映射

**H3 排除** ✅: 加密密钥注册正常
- `ServerCore.cpp` L705-708: `generateSessionKeyForClient` 成功后注册到 `client_session_keys_`
- Fallback 路径也调用 `setClientSessionKey` 注册

**H4 待验证**: AudioRelay 频道映射 — 需运行时日志
**H5 待验证**: UDP 端点归一化 — 需运行时日志

### Step 2: 修复 Docker 端口映射 + 添加语音链路插桩日志
- 修复 Dockerfile/docker-compose.yml/docker-compose.dev.yml 暴露 24432/udp
- 在 AudioRelay 添加 INFO 日志：peers 为空、转发成功/失败
- 设置 NEVO_LOG_LEVEL=debug 以便收集详细日志

### Step 3: 全面诊断结果（3 个并行调查）

#### 问题 A: 视频/屏幕共享 — Docker 端口未映射 ✅ 已修复
- 服务端在 24432 绑定视频 UDP，但 Docker 未映射此端口
- 已修复 Dockerfile + docker-compose.yml

#### 问题 B: 视频通话信令 — 服务端缺少自定义线格式解码器 ❌ 未修复
- `PacketCodec.cpp` 的 `CASE_DECODERS` 表缺少 case 80/81/82/83 (VideoCall*)
- 服务端收到这些消息时报 "Unknown custom wire case_value" 并丢弃
- `CASE_ENCODERS` 表也缺少 81/82/83 的编码器
- 此外 ScreenShareStart(60)/Stop(61) 也缺失

#### 问题 C: 文件传输 — 功能未实现 ❌ 无法快速修复
- `FileUploadRequest` 只有元数据 (filename/size)，无文件数据字段
- 服务端创建 DB 记录但从不读取文件字节
- 客户端假装成功，插入 [IMG:id] 文字占位符
- 其他客户端只收到占位符文字，看不到文件
- FileDownload 完全未实现

#### 问题 D: 语音通讯 — 需运行时验证
- UDP 端口 24431 已正确映射
- 中继逻辑正确：解密→重新加密→转发
- 潜在问题：加密上下文延迟创建、DTX 空帧被丢弃、密钥轮换失败
- 已添加插桩日志，需用户测试后收集

#### 问题 E: 过期的 Protobuf 文件
- `src/client/gui_python/proto/control_pb2.py` 是旧版本
- 但 Python 客户端 TCP 控制使用自定义线格式，不使用 protobuf
- 因此不是活跃 bug，但是潜在隐患

### Step 4: 语音根因确认 ✅ 找到！

**根本原因: `opuslib` 未安装在 .venv 中，Opus 编码器无法创建**

- `voice_engine.py` 的 `_init_opus()` 调用 `import opuslib`
- `opuslib` 包未安装在 `.venv` 中 → `ImportError` → `self._opus_encoder = None`
- `send_voice_data()` 检查 `if not self._opus_encoder: return` → **从不发送任何语音包**
- 加密包 `nacl` (PyNaCl) 已正确安装
- `opus.dll` 存在于 `build\bin\Release\` 但未加入 PATH

**修复**:
1. 安装 `opuslib` 包到 .venv ✅
2. 修改 `voice_engine.py` 自动将 opus.dll 目录加入 PATH ✅
   - 开发模式: `build\bin\Release\`
   - PyInstaller 模式: `sys._MEIPASS`
3. 重新构建客户端（包含 opus.dll 和 opuslib）🔄 进行中

**影响范围**:
- 语音通讯: Opus 编码器无法创建 → 无语音包发送 ✅ 已修复
- 屏幕共享: 视频引擎可能使用不同的编解码器，需验证
- 视频通话: 同上
- 文件传输: 功能未实现（独立问题）
