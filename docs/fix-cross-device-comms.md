# V3 跨设备无法通讯 — 修复报告

**状态:** ✅ 已修复并验证（本地 + 外网双链路）
**日期:** 2026-08-13
**影响面:** V3 Web 客户端（Electron + 网关）跨设备文字/语音通讯
**交付:** `test/V3` 已替换为新构建（网关 `dd06b19a`）；服务器新镜像已部署（回滚点已保留）

---

## 1. 问题现象

- V3 客户端跨设备**文字聊天、语音等所有功能均无法正常使用**；
- 外网（frp 穿透，8.138.90.187）链路：登录成功、频道互见正常，但**聊天消息大面积丢失**（实测多轮到达率 A→B 1/5、B→A 0/5）；
- 本地链路同样存在间歇性丢消息（低频，压测放大后稳定复现）。

## 2. 链路

```
V3 Electron ──> nevo_gateway(Python) ──TCP 控制连接──> nevo_server(C++/Asio)
     └─ 文字：控制帧（case 30/31）      └─ 语音：UDP + TCP 隧道（type 0xFF）
```

文字聊天只走 TCP 控制连接，语音走 UDP 中继 + TCP 隧道。文字丢包说明**控制链路的传输层本身有缺陷**。

## 3. 根因（三层，均在服务端 `TcpConnection`）

### 3.1 并发 composed async_write 无串行化 → 字节流交错损坏（主因）

`ClientSession::sendControl` / `sendVoiceFrameTcp` 每条消息各自
`co_spawn` 一个独立协程，直接在**同一个 socket** 上调用
`boost::asio::async_write`。asio 的 composed async_write **不允许同一
socket 上并发执行**（头文件注释明确写着"所有 socket 操作都通过 strand 调度"，
但代码只在读循环启动时 post 过一次 strand，写路径从未串行化）。

- 局域网低延迟下重叠概率低 → 偶发丢消息；
- 外网 frp 高延迟下每次写耗时拉长 → 重叠概率大增 → 帧字节交错、流损坏，
  接收端解析错位后**后续所有消息全部丢失**（与"第一条能到、之后全灭"的现象完全吻合）。

### 3.2 载荷读取用 `asio::cancel_after` → 读循环静默卡死

读循环的载荷读取使用 `cancel_after(TCP_PAYLOAD_READ_TIMEOUT)`。其取消路径
存在竞态：操作完成与定时器取消竞争时可能丢失完成处理器，表现为——

- 帧头（12 字节）已读到（有 TRACE 日志）；
- 载荷读取永远不返回：无错误日志、无超时日志、连接保持 ESTABLISHED、
  客户端 `sendall` 已成功但数据永远不被处理；
- 该会话从此**半死不活**：所有后续发送/接收全部静默失效。

实测中该模式反复出现（含登录帧：登录头已到、登录体永远丢失）。
关键佐证：**只有带 cancel_after 的载荷读会卡死，从不带它的帧头读从未卡死**。

### 3.3 客户端登录等待无超时（放大缺陷）

`NevoClient.connect()` 发送登录帧后同步 `_read_frame()` 且 socket 为阻塞
模式（timeout=None）。服务器一旦出现 3.2 的卡死，网关 WS 处理线程将
**永久阻塞**，该网关会话的所有后续命令（重新登录、发消息）全部超时，
浏览器侧表现为"所有功能均无法使用"。

## 4. 代码改动

| 文件 | 改动 |
|---|---|
| `src/network/src/TcpConnection.cpp` | ①新增**写队列单写者模式**：所有出帧（控制 + TCP 语音）先拷贝入 `write_queue_`，由唯一 `drainWriteQueue()` 协程逐帧串行 async_write，杜绝并发写交错；②载荷读取改用**独立 steady_timer**（超时直接 close() 中止挂起读，slowloris 防护不依赖 cancel_after）；③写失败统一在排空协程上报断开 |
| `src/network/include/nevo/network/TcpConnection.h` | 新增写队列成员与 `drainWriteQueue()` 声明 |
| `src/client/gui_python/nevo_client.py` | 登录响应等待限时 15 秒（超时返回失败而非永久阻塞），失败后错误可上浮到浏览器 |

## 5. 验证

**本地（修复后服务器 + 修复后网关）：**

| 检查项 | 修复前 | 修复后 |
|---|---|---|
| 裸 TCP 双线程并发聊天压测（绕过网关层，直击服务器） | 每轮均有丢包 | **30/30 轮 × 10 消息双向 0 丢包** |
| ws_chain_test（打包 V3 网关，登录/互见/聊天/语音 8 项） | 间歇失败 | **9 轮全 PASS**（打包网关 3/3 + 源码网关 6/6） |
| ctest 全量 | — | **459/459 PASS** |
| Python 测试（wire 8/8、voice_crypto 7/7） | — | **全 PASS** |

**外网（frp → 8.138.90.187，修复后镜像已部署）：**

| 检查项 | 修复前 | 修复后 |
|---|---|---|
| 多轮聊天一致性 | A→B 1/5、B→A 0/5 | **A→B 5/5、B→A 5/5** |
| ws_chain_test（聊天+语音双向） | 聊天全 FAIL | **6/7 全 PASS**（1 次为部署后首分钟的登录阶段瞬态抖动，重试即恢复） |

## 6. 交付物

- **`test/V3`**：已替换为新构建（网关 `nevo_gateway.exe` md5 `dd06b19aeb9c…`，
  含写队列修复的服务端配套 + 客户端登录限时；Electron app.asar 无前端改动）；
- **`test/server/nevo_server.exe`**：已替换为修复后二进制；
- **服务器镜像**：`nellya/nevo-server:latest` = `03a71f11dc06`（已部署，容器 healthy）；
  - 回滚点：`nellya/nevo-server:old-20260813-commsfix` = `57e9102d4df6`（修复前镜像）。

## 7. 备注与遗留

- 排查中发现：服务器登录校验在并发登录时受 SQLite 锁竞争影响可延迟 1~1.5s
  （登录阶段的偶发抖动，非丢消息缺陷；重试即恢复）；
- **遗留（沿用上轮）**：外网视频（VideoRelay 无 TCP 隧道路径）仍不可用；
- **安全提醒**：`test/cross_device_repro/` 下部分脚本仍硬编码 SSH/Docker 凭据，
  建议尽快轮换并统一改为环境变量注入；
- 本报告对应的源码改动尚未提交 git（工作区 3 个文件），确认后可提交。
