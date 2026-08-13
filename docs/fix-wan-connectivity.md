# 外网无法加入服务器 — 修复报告

**状态:** ✅ 已修复并验证
**日期:** 2026-08-13
**影响面:** 外网客户端经内网穿透（frp）访问 NEVO 服务器
**交付:** 服务器新镜像已部署；`test/V3` 已替换为修复后新构建

---

## 1. 问题现象

- 局域网（192.168.31.x）客户端连接服务器正常；
- **外网客户端完全连不上服务器**（登录失败/超时），或能登录但语音不通。

## 2. 网络拓扑与链路

```
外网客户端 ──> frps(8.138.90.187:24430/24431/24432) ──> frpc(服务器, host/bridge)
              ──> 服务器容器(172.19.0.2:24430 TCP / 24431-24432 UDP) ──> 中继 ──> 对端
```

服务器在家庭路由器 NAT 后（出口 120.230.113.190），**无公网 IPv4 入站**；
外网访问唯一通道是 **frp 内网穿透**（frps 阿里云 8.138.90.187 ↔ frpc 服务器本机）。

## 3. 根因（三层）

### 3.1 frpc 隧道配置错误（TCP 可达性）
- `frpc.toml` 中 `NEVO_udp` 隧道 localPort/remotePort 误写为 **24430**（语音实际在 24431）；
- **24432（视频 UDP）隧道完全缺失**；
- 修复：改为 24431/24432，新增 `NEVO_video_udp` 隧道。

### 3.2 服务器中继 fail-closed 拒绝外网语音包
- 服务器（8/13 安全加固）中继要求发送端点必须在映射表中（登录时由 `client_udp_port` 注册）；
- frp 转发后 UDP 源端口 ≠ 客户端上报端口 → 全部语音包被 `unknown sender` 拒绝；
- **修复**：中继增加**解密认证自动注册**——未知端点发来的包若能使用包头 `sender_id` 对应
  用户的会话密钥解密成功（= 持有密钥 = 身份可信），自动绑定该端点（`AudioRelay`/`VideoRelay`），
  同时保留频道成员校验与端点占用检查（防注入安全不回退）。

### 3.3 frp UDP 回程缺陷 → 全 TCP 媒体隧道
- 实测（服务器本机模拟外网源、多形态抓包）：**frp 0.69.1 的 UDP 回程不工作**
  （frpc 收到 docker-proxy/服务器回包后不向 frps 转发，frps 方向 0 数据）；
- 语音帧经 UDP 隧道即使到达服务器，中继回包也无法送达外网接收端；
- **修复**：媒体帧改走 **TCP 控制连接**（`TCP_VOICE_FRAME_TYPE=0xFF`）：
  - 客户端：语音帧打包后经登录的 TCP 连接发送（`nevo_client.send_voice_frame_tcp`），
    接收端从 TCP 帧回调解密（`on_tcp_voice_frame`）；
  - 服务器：中继转发时若接收者有活跃 TCP 会话，**优先 TCP 下发**
    （`AudioRelay::setTcpSenderCallback` → `ClientSession::sendVoiceFrameTcp`），UDP 兜底；
  - frp TCP 回程与登录同链路，已验证可靠。

### 3.4 会话清理竞争（断线重连后用户被移出频道）
- 同账号断线重连：旧会话 `disconnect()` 无条件 `removeUserFromChannel`，
  晚于新会话的频道加入 → 新会话用户被移出频道 → 中继频道成员校验拒绝其媒体包；
- **修复**：频道成员清理移至 `ServerCore::onClientDisconnected`，
  仅当该用户**无其他活跃会话**时移除。

## 4. 代码改动

| 文件 | 改动 |
|---|---|
| `src/server/src/AudioRelay.cpp` | 解密认证自动注册；`no peers` 提前返回修复（认证路径）；TCP 下发回调 |
| `src/server/include/nevo/server/AudioRelay.h` | `VoiceTcpSender` 回调类型与成员 |
| `src/server/src/VideoRelay.cpp` | 解密认证自动注册（对称修复） |
| `src/server/src/ClientSession.cpp` | `sendVoiceFrameTcp`；disconnect 不再无条件移除频道成员 |
| `src/server/include/nevo/server/ClientSession.h` | 新方法声明 |
| `src/server/src/ServerCore.cpp` | 注册 TCP 下发回调；频道清理移到断线处理（多会话保护） |
| `src/client/gui_python/nevo_client.py` | `TCP_VOICE_FRAME_TYPE`、`send_voice_frame_tcp`、`on_tcp_voice_frame` 回调 |
| `webclient/gateway.py` | MediaBridge TCP 发送/接收；channel_id 时序兜底 |

## 5. 验证

本地全链路（双端点模拟跨设备）+ 外网 frp 通道（两台"外网"客户端均走 8.138.90.187）：

| 检查项 | 修复前 | 修复后（V3 打包网关） |
|---|---|---|
| 外网登录（frp TCP） | ❌ 配置错误时超时 | ✅ |
| 频道在线互见 | ✅ | ✅ |
| 聊天双向 | ✅ | ✅ |
| **语音双向** | ❌ 0 帧 | ✅ 20+ 帧/方向 |
| 发送者身份 | ❌ | ✅ |
| 连续稳定性 | — | ✅ 连续 4 轮全 PASS |

- 服务器中继日志确认：`UDP endpoint auto-registered via crypto auth`、
  `TCP tunnel mapping auto-created`、`TCP voice send` 均正常；
- 回归：`ctest 459/459` 全绿（含中继安全 fail-closed 系列）；
- `test_wire_format 8/8`、`test_voice_crypto 7/7`。

## 6. 交付物

- **服务器**：新镜像 `nellya/nevo-server:latest`（已 push registry + 服务器 docker load 部署，healthy）；
  - 回滚点：`nevo-server:old-20260813-wanfix`（8/13 早间镜像）。
- **`test/V3`**：已替换为新构建（网关 `nevo_gateway.exe` md5 `a7b8f445…`，
  含 TCP 媒体隧道 + channel 兜底），`.nevo_update` 历史保留。
- **frpc 配置**：`/opt/1panel/apps/frpc/frpc/data/frpc.toml`（备份 `frpc.toml.bak-wanfix`）：
  NEVO 隧道 localIP=172.19.0.2（nevo-server 容器，IP 已在 compose 固定）、
  24431/24432 UDP + 24430 TCP；1Panel compose 已同步（bridge 双网络）。

## 7. 遗留与建议

- **外网视频**：VideoRelay 暂无 TCP 隧道路径，外网视频仍不可用（语音已通）；
  后续为 VideoRelay 增加与 AudioRelay 对称的 TCP 下发即可。
- frp UDP 回程缺陷（frpc 0.69.1 实测不转发回包）为 frp 上游行为，已通过 TCP 隧道绕开；
  若 frp 后续修复，可评估回退 UDP 以降低 TCP 开销。
- `nevo-server` 容器 IP 已固定（172.19.0.2），frpc.toml 的 localIP 与其绑定；
  重建 nevo-server 时勿移除 compose 的 ipv4_address 配置。
- 真机双设备（两台物理机）外网语音建议用新 V3 包做一次人工确认。
