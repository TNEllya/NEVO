# 语音"10 秒后无声音" — 密钥轮换传播缺失修复报告

**状态:** ✅ 已定位根因并修复（确定性单测通过；跨越轮换边界的 13 分钟 E2E 浸泡见下）
**日期:** 2026-08-14
**影响面:** V3 Web 客户端（网关 MediaBridge）跨设备语音/视频；**所有已连接 >10 分钟的 Web 客户端在服务端密钥轮换后语音永久失效**

---

## 1. 问题现象（用户报告）

- 点击右上角"断开语音"后**才能**听到对方语音；
- 且仅能通讯约 10 秒，之后无声音。

## 2. 排查过程（三层复现，逐步收敛）

| 层 | 测试 | 结果 |
|---|---|---|
| 传输层（网关→服务器→网关，绕过浏览器） | 本地 35s 持续语音浸泡（50 帧/s） | 全程 3464 帧无中断 |
| 传输层 | 外网 frp → 8.138.90.187（部署服务器）35s 浸泡 | 全程 3464 帧无中断 |
| 浏览器层 | IAB 真浏览器 + 假 WS 对端：本地 40s、外网 40s 双向 | 解码/播放/发送全程健康 |
| **服务器日志** | 长时间运行后 | **发现海量 `Failed to decrypt voice packet from user_id=5`（15265 条）** |

关键证据：本地服务器日志中，`user=5` 的中继成功日志在某一时刻**戛然而止**，
随后同一发送端口的每一帧都变成 `VoiceCrypto::decrypt: authentication failed (both keys)`。
定位到该时刻前后：

```
37859: VoiceCrypto: key rotated, old key expires in 20s      ← 服务端密钥轮换
37861: Key rotation complete, notified 1 / 1 clients
37864: Key rotation response from user 5: epoch=2            ← 客户端已应答！
38866: Voice relayed: user=5 -> 1 peers   (最后一次成功中继)
38891: Failed to decrypt voice packet from user_id=5  (此后 15265 连败，直至今日)
```

## 3. 根因

**服务端每 600 秒轮换一次每客户端语音会话密钥**（`KEY_ROTATION_INTERVAL_SEC`），
轮换后旧密钥保留 20 秒宽限期（`KEY_OVERLAP_WINDOW_SEC`）。

客户端密钥轮换处理链：

1. 服务端 `ServerCore::rotateSessionKey()` → 生成新密钥 → `audio_relay_->rotateClientKey()`
   （服务端中继加密层换新钥、旧钥留 20s）→ 向每个会话发 `KeyRotationRequest`；
2. `NevoClient._handle_key_rotation_request()` 收到后更新 `self._session_key`，
   并调用 `_rotate_session_key_in_media()` 把新密钥传给**注册过的媒体引擎**
   （`self._voice_engine` / `self._video_engine`）；
3. PyQt 桌面端在 `connect(voice_engine=..., video_engine=...)` 时注册了引擎 → 正常；
4. **Web 网关的 `MediaBridge` 在登录后才创建，从未注册为媒体引擎** →
   轮换到达时 `_rotate_session_key_in_media()` 遍历的引擎为 `None`，新密钥**从未传播**
   到 MediaBridge 自己的 `VoiceCrypto`。

后果（与现象完全吻合）：

- 轮换后 0~20s：发送方向靠服务端旧密钥宽限期勉强可用；
- 轮换后 20s：网关仍用旧密钥加密 → 服务端 `decrypt` 双钥全败 → **发送被全部丢弃**；
- 接收方向：服务端用接收者的**新**密钥重加密 → 网关只有旧钥 → 解密失败 →
  **接收从轮换时刻起即静默**。
- 服务端虽收到客户端 `KeyRotationResponse`（应答只说明"客户端 TCP 层收到"，
  不含媒体加密层是否已切换），无从发现媒体层未同步。
- 直到重新登录（`generateSessionKeyForClient(reuse_existing=true)` 会下发当前有效
  密钥并新建 MediaBridge）语音才恢复——这解释了"点击（断开语音→重连/重登）后
  又能听到"，以及"一段时间后再次无声音"（下一次轮换）。

## 4. 修复

| 文件 | 改动 |
|---|---|
| `src/client/gui_python/nevo_client.py` | 新增 `register_media_engines(voice_engine, video_engine)`：登录后注册媒体引擎（PyQt 走 connect() 传参，Web 网关走本方法），使密钥轮换可传播到媒体加密层 |
| `webclient/gateway.py` | ① `MediaBridge.set_session_key()` / `rotate_session_key()`（后者旧钥保留 20s 宽限，与服务端语义一致）；② 登录成功后 `c.register_media_engines(self._media_bridge)` |

## 5. 验证

- **确定性单测**（`test/cross_device_repro/test_media_rotation_propagation.py`）：
  MediaBridge 密钥切换 + 旧钥宽限 + NevoClient 轮换传播 3 项全 PASS；
- **回归**：`tests.test_wire_format/test_voice_crypto/test_file_transfer` 30/30；
- **E2E 浸泡**（`test/cross_device_repro/rotation_soak_test.py`）：重启服务器（轮换
  定时器从 0 起），双网关双向持续语音 ~13 分钟，跨越 600s 轮换点。结果：

  | 指标 | 修复前 | 修复后 |
  |---|---|---|
  | 服务端解密失败（全程） | 15265 条（轮换后 20s 起持续） | **0 条** |
  | 每 30s 接收帧数（双向，含 570-660s 轮换点） | 轮换后归零 | **全程 ~2884/~5774 稳定，无波动** |
  | 客户端轮换应答 | 有应答但媒体密钥未同步 | **新密钥传播到 MediaBridge，媒体连续** |

## 6. 遗留发现（与本修复无关，建议下轮处理）

1. **桌面端（PyQt）收不到中继语音**：服务端中继自"全 TCP 媒体隧道"轮次起
   对**任何有活跃 TCP 会话的接收者**优先走 TCP 下发（UDP 仅作回调返回 false
   时的兜底，实际永不触发）。但 PyQt 客户端从未接线 `on_tcp_voice_frame`
   （`src/client/gui_python` 全目录仅 `nevo_client.py` 定义、`gateway.py` 接线），
   桌面端收到 0xFF 语音帧后直接丢弃 → **web→桌面、桌面→桌面语音当前不可用**。
   可选修复：a) 桌面端接线 TCP 语音帧喂给 VoiceEngine（与 UDP 解析同格式）；
   b) 服务端对每接收者同时 TCP+UDP 双发（web 端已有去重容忍）。建议下轮评估。
2. 轮换时序与"断开语音"按钮的交互（leave→自动重连默认频道重启语音引擎）
   属 UI 行为，不影响本修复结论。

---

# 补充：真正的"10 秒后无声音"根因（2026-08-14 第二轮）

**状态:** ✅ 已修复并验证（真实 Electron 客户端 75s 回归 PASS）

## 现象复盘

密钥轮换修复部署后，用户反馈客户端"10 秒后无声音"**仍然出现**。
用真实 Electron 客户端（CDP 驱动双实例）复现，发现：

- 对端 B 的 `audioDecoder` 在登录后数秒内变为 `closed`，控制台：
  `[MEDIA] AudioDecoder error: EncodingError: Null or empty decoder buffer.`
- 向频道注入一个空载荷语音帧 → 本端解码器立即 `configured → closed`，100% 复现。

## 根因

**网关 MediaBridge 的 UDP 注册/保活包（载荷为空）被服务器当语音中继给同频道用户**：

1. 网关登录/加频道时 `set_channel()` 发注册包、之后每 15s 发保活包（`_send_registration_packet`，
   加密空明文，`last_frame=True`）——用途仅是 NAT 端点注册；
2. 服务器 `AudioRelay::relayVoicePacket` 解密后**不区分载荷是否为空**，一律重加密中继给同频道 peers；
3. 接收端浏览器 `handleVoiceFrame` 把空 base64 解成空 ArrayBuffer → `EncodedAudioChunk(空)` →
   Chrome/Electron 的 `AudioDecoder.decode()` 抛 `EncodingError: Null or empty decoder buffer`，
   **解码器进入 closed 终态，此后所有语音帧被静默丢弃**；
4. 与"点击断开语音后能听到、约 10 秒后无声音"完全吻合：点击 → 语音引擎重启（新解码器）→
   下一个保活包（15s 周期，落到 ~10 秒量级）再次杀死解码器。之前 IAB 浏览器未复现是因为
   其 Chromium 版本容忍空 chunk（静默忽略），Electron/Chrome 严格抛错。

## 修复（三层防御，全部落地）

| 层 | 文件 | 改动 |
|---|---|---|
| 浏览器 | `webclient/js/media.js` | `handleVoiceFrame`/`handleVideoFrame` 空载荷直接 return，不喂解码器 |
| 网关 | `webclient/gateway.py` | `_on_tcp_voice_frame`/`_voice_recv_loop`/`_video_recv_loop` 解密后空明文不转发给浏览器 |
| 服务器 | `src/server/src/AudioRelay.cpp` | 解密后（含 UDP 端点注册路径）空载荷不再中继给 peers（`relayVoicePacket` 步骤 5.2） |

## 验证

- ctest 459/459 PASS（含 AudioRelay 改动）；
- 传输层：注入 5 空帧 + 10 真实帧，对端只收到 20 帧（10×2 双路），空帧 0 到达；
- **真实客户端回归**：Electron 双实例（打包网关）+ 干扰源每 2s 空帧注入 75s，
  A/B 解码器全程 `configured`、队列持续、零控制台错误；同期跨越服务端密钥轮换亦无影响。

---

# 补充：语音质量优化（2026-08-14 第三轮）

**状态:** ✅ 已完成并验证（真实 Electron 客户端 40s 零中断；打包网关 1:1 帧交付）

## 质量问题的三个来源（实测定位）

| # | 来源 | 实测证据 |
|---|---|---|
| 1 | **TCP/UDP 双路重复送达**：网关对同一帧同时走 TCP 隧道与 UDP，服务端两条路径都中继 | 修复前浏览器收到 100 帧/s（发送 50/s），抖动缓冲被灌满只能丢帧 |
| 2 | **主线程编码抖动**：AudioWorklet 每 2.67ms 投递一次（375 次/s），主线程编码+base64+WS 发送跟不上则累积延迟 | worklet 消息频率实测 375/s |
| 3 | **播放排程 setTimeout 漂移**：固定 20ms setTimeout 与编码时钟（AudioContext 时钟）不同步，漂移累积导致周期性断音 | 修复前队列稳定顶满 12 帧（240ms）且持续丢旧帧 |

另外：部分 Chromium 版本按输入块长度输出非 20ms Opus 帧（IAB 实测 150~160 帧/s），接收端固定 20ms 排程播放即断档——需在发送端强制 20ms 帧。

## 修复（webclient 两个文件，服务器无改动）

| 文件 | 改动 |
|---|---|
| `webclient/js/media.js` | ① AudioWorklet 内累积到 960 样本（20ms）再投递——保证所有 Chromium 输出标准 20ms 帧、主线程消息降到 50 次/s；② 播放改为 AudioContext 时钟对齐排程（`src.start(when)` + 提前 5ms 唤醒 + 3ms 钳制），彻底消除定时器漂移；③ 预填 2 帧再开播，吸收起始抖动；④ Opus 码率 32k→48kbps |
| `webclient/gateway.py` | TCP/UDP 双路去重：按 (sender_id, sequence_number) 只转发一份（`_is_duplicate`，两接收路径共享状态） |

## 验证（真实客户端）

- 修复前：100 帧/s 入站、队列顶满 12 丢帧、播放断续；
- 修复后：**50 帧/s 入站（1:1）**，队列稳定 A 1-2 帧 / B 5-6 帧（20~120ms 延迟），**40 秒监测 0 次播放中断**；
- 打包网关 1000 帧发送 → 1000 帧到达（去重生效），稳态 48.6/s；
- 新网关包 md5 `ea90036d6beb…` 已替换 test/V3/resources/nevo_gateway（旧包 .bak-20260813）。
