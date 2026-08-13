# V3 客户端跨设备无法通讯 — 修复报告

**状态:** ✅ 已修复并验证
**日期:** 2026-08-13
**影响面:** `test/V3`（NEVO Web Client 打包版）语音/视频链路全断；文字聊天与在线状态不受影响
**交付:** `test/V3` 已替换为修复后新构建（网关 `nevo_gateway.exe` SHA256 摘要见文末）

---

## 1. 问题现象

- V3 客户端（Electron + 本地 Python 网关）能登录服务器、能看到其他设备用户在线、文字聊天可收发；
- 但**语音、视频双向完全无声/无画面**——跨设备无法正常通讯；
- 8月11日同链路测试（`test/cross_device_repro/gw8089.log`）正常，8月13日起失效。

## 2. 链路结构

```
设备 A: 浏览器/Electron ──WS──> nevo_gateway.exe(本地 8088) ──TCP 24430──┐
                                 │  └─UDP 注册/语音/视频包               │
                                 └────UDP 24431/24432──────────────────>│
设备 B: 同上（另一台机器/独立端点） ─────────────────────────────────────>├─ 服务器(192.168.31.39)
                                                             中继：解密→校验→重新加密→转发
```

## 3. 根因（两层叠加）

### 3.1 直接原因：网关登录时未上报 UDP 媒体端口

服务端 8月13日 `acf9d5d` 安全加固后，语音/视频中继（`AudioRelay`/`VideoRelay`）改为 **fail-closed**：

- 中继收到 UDP 包时，发送者端点**必须已存在于映射表**，否则直接丢弃
  （`Voice packet rejected: unknown sender …`，注释原文：*不自动为"自称任意 user_id"的未知端点创建映射——这是语音注入漏洞的根源*）；
- 端点映射**唯一建立途径**是登录请求中的 `client_udp_port` / `client_video_udp_port`
  （`ClientSession.cpp`：`if (client_udp_port > 0)` → `setUdpEndpoint` → `addClientMapping`），
  同时校验包头 `sender_id` 与映射身份一致、频道成员校验（防跨频道注入）。

Web 网关（`webclient/gateway.py`）调用 `c.connect(host, port, username, password)` 时**从不传媒体端口**（`client_udp_port` 恒为 0，旧逻辑只从 `voice_engine` 读取，而网关没有 voice_engine），
且媒体套接字是在登录**之后**才由 `MediaBridge.start()` 创建。→ 端点从未注册 → 所有语音/视频包被 fail-closed 丢弃。

### 3.2 触发原因：V3 包内网关是 8月10日旧构建

- `test/V3/resources/nevo_gateway/nevo_gateway.exe` 与 `webclient/dist/nevo_gateway/nevo_gateway.exe`
  哈希完全一致（`5b2a3f98…`，8月10日 16:26 构建），仅被拷贝进 8月13日的打包；
- 8月13日源码（`acf9d5d` 协议统一 + 语音安全修复、`8ae6225`、`eee104c`）中的
  `nevo_client/nevo_wire/voice_crypto/proto.*` 模块**全部未进入包内**（PYZ 逐模块 marshal 对比确认）；
- 8月11日能通是因为旧服务端在收到首个 UDP 包时**自动建立端点映射**；8月13日 fail-closed 后此路被堵死，
  旧网关（不含 `client_udp_port` 上报逻辑，连字段都不写入）彻底失效。

> 即：即使重新打包也会失败——**当前源码的网关同样存在不报端口的问题**，必须同时修代码。

## 4. 修复内容

### 4.1 `webclient/gateway.py`

1. `MediaBridge` 支持登录前预创建套接字复用：
   - 新增 `MediaBridge.pre_create_sockets()`（IPv6 双栈 UDP，返回 voice/video 套接字）；
   - 构造函数接受 `voice_sock`/`video_sock`；`start()` 优先复用预建套接字，未预建时按旧路径自建；
   - 服务器未提供视频端口时关闭预建视频套接字。
2. `login` 命令处理：
   - 登录**前**预创建媒体套接字，取本地端口随 `connect(..., client_udp_port=…, client_video_udp_port=…)` 上报；
   - 登录成功 → `MediaBridge` 复用同一套接字启动（保证上报端口与实际收包端口一致）；
   - 登录失败/桥接启动失败 → 关闭预建套接字，避免句柄泄漏。

### 4.2 `src/client/gui_python/nevo_client.py`

- `connect()` 新增可选参数 `client_udp_port: int = 0`、`client_video_udp_port: int = 0`
  （默认 0 = 原行为，PyQt 客户端调用完全不受影响）；
- 显式端口优先，缺省时才回退到 `voice_engine/video_engine` 预建端口；
- 修正 LoginRequest 构造字段名（`client_video_udp_port=…`）。

## 5. 验证（本地全链路，双端点模拟跨设备）

本地服务器 = `test/server/nevo_server.exe`（8月13日构建，与线上同源码逻辑）；账号 `tester_a`/`tester_b`；
驱动脚本 `test/cross_device_repro/ws_chain_test.py`（双 WebSocket 客户端直连两个网关）。

| 检查项 | 修复前（当前源码网关） | 修复后（源码网关） | 修复后（打包网关） | 修复后（test/V3 本体） |
|---|---|---|---|---|
| 登录 | ✅ | ✅ | ✅ | ✅ |
| 频道内互相可见 | ✅ | ✅ | ✅ | ✅ |
| 聊天 A→B / B→A | ✅ / ⚠️回声误报 | ✅ / ✅ | ✅ / ✅ | ✅ / ✅ |
| 语音 A→B / B→A | ❌ 0 帧 | ✅ 6/6 帧 | ✅ 6/6 帧 | ✅ 6/6 帧 |
| 视频 A→B / B→A | ❌ | ✅ 2/2 帧 | ✅ 2/2 帧 | —（同链路） |
| 发送者身份校验 | ❌ | ✅ | ✅ | ✅ |

服务器日志佐证：
- 修复前：`Voice packet rejected: unknown sender ::ffff:127.0.0.1:52xxx`（全部语音包被丢弃）；
- 修复后：`Set UDP endpoint for user 6 -> ::ffff:127.0.0.1:51782`、
  `Set Video UDP endpoint for user 6 -> ::ffff:127.0.0.1:51783`、`UDP mapping added: user=5/6`，**0 条 rejected**。

回归单测：`test_wire_format.py` 8/8 ✅、`test_voice_crypto.py` 7/7 ✅（协议/加密金样未破坏）。

## 6. 交付物

- `test/V3/` 已整体替换为新构建（Electron win-unpacked + 新网关）：
  - `NEVO Web Client.exe`（Electron 33.4.11 打包，更新器/清单逻辑同源）；
  - `resources/nevo_gateway/nevo_gateway.exe` 新构建；
  - 保留 `.nevo_update/` 更新历史。
- 新网关构建产物：`webclient/dist/nevo_gateway/`（PyInstaller，来自修复后源码）。

```
旧包网关 SHA256 目录: 5b2a3f98b86e304d255e71108aa4bde1 (md5)
新包网关:            a13ea475144b65af662242460aee436c (md5)  ← 已确认进入 test/V3
```

## 7. 遗留与建议

- 真机双设备实联（两台 PC 各跑一个 V3）建议用新包做一次人工语音/视频确认；本测试为同机双端点，
  服务端视角与跨设备一致（不同 UDP 端点、独立 TCP 会话）。
- 发布流程建议：`webclient/dist/nevo_gateway` 与 `webclient/electron` 每次改动后**重新构建**，
  不要拷贝旧构建产物（本次故障即由旧构建直接拷贝触发）。
- 服务器侧 fail-closed 是正确安全行为（堵住语音注入/身份伪造），不应回退。
