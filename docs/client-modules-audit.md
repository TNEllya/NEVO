# NEVO 客户端模块完成度与问题审计报告

- 审计范围：所有用户端实现（C++ 客户端核心 + Qt UI、Python PyQt5 GUI、Web 网关/客户端、React 客户端、Android 客户端、设计稿目录）
- 审计类型：只读代码审计（Investigation）
- 审计日期：2026-08-02
- 审计人：Ryan（前端开发）
- 关联任务：task_57scphr2ot / RoleRun rr_m7dl57olbb

---

## 0. 执行摘要（TL;DR）

NEVO 的"客户端"实际上由 **6 套并行实现** 构成，彼此协议对齐方式不统一，存在明显的重复建设与一致性风险：

| # | 模块 | 路径 | 性质 | 功能完成度估算 | 可投产度 |
|---|------|------|------|----------------|----------|
| 1 | C++ 客户端核心库 | `src/client/` | 真实引擎（无 UI） | ~80% | 中（需 UI 承载） |
| 2 | Qt UI 组件 | `src/ui/` | **空目录（缺失）** | **0%** | 不可用 |
| 3 | Python PyQt5 GUI | `src/client/gui_python/` | 主力桌面客户端 | v1 ~90% / v2 ~65% | 中高 |
| 4 | Web 网关 + JS 客户端 | `webclient/` | 真实浏览器客户端 | ~75% | 中（安全阻塞） |
| 5 | React 客户端 | `nevo-client/` | **纯 UI 原型（无后端）** | 作为客户端 ~30% | 不可用 |
| 6 | Android 客户端 | `mobile/android/` | 结构完整但语音失效 | ~55% | 低（Critical 阻塞） |
| — | 设计稿 | `nevo-ui-design/`、`nevo-gui-design/` | 静态 HTML 设计交付物 | 非客户端 | — |
| — | 服务端管理面板 | `web/` | 独立运维面板（非用户端） | — | — |

**最严重的三个跨模块问题：**
1. **协议三轨制**：C++/媒体层用 protobuf，但 Python 控制通道（`nevo_wire.py`）、Android（`NevoBuffer.kt`）各自手写二进制编解码，与 `proto/*.proto` 无代码生成保证，极易静默漂移。
2. **Android 语音/加密实质失效**：JNI `.so` 未接入 Gradle 构建 → Opus 编解码 no-op、加密退化为不安全 fallback，且 **零测试**。
3. **Web 客户端明文传输**：`ws://` + 密码明文 JSON，无 TLS/鉴权。

---

## 1. C++ 客户端核心（`src/client/`）+ Qt UI（`src/ui/`）

### 1.1 现状
- `src/client/` 是真实的客户端引擎库 `nevo_client`，~6289 LOC：
  - `ClientCore.cpp` 1664 行（生命周期/状态机协调器，文档完善）
  - `NetworkManager.cpp` 1119 行、`VideoCallManager.cpp` 1046 行、`AudioInput/Output.cpp` 各 ~230 行
  - 头文件文档详尽（状态机转换图、线程安全约定），**无 TODO/FIXME/stub 标记**。
- 仅提供一个 166 行的 `console_main.cpp` 控制台客户端（依赖 boost/sodium/opus 才启用）。
- **`src/ui/` 为空目录**，但 `AGENTS.md` 明确声明"src/ui/ → Qt UI 组件（依赖 core）"。

### 1.2 问题
| 等级 | 问题 | 证据 |
|------|------|------|
| **High** | AGENTS.md 声明的 Qt UI 层完全缺失，`src/ui/` 空目录；C++ 引擎无图形承载，只能靠控制台或 Python GUI | `ls src/ui` 为空；`AGENTS.md` 模块结构表 |
| Medium | C++ 引擎与 Python GUI 并存，桌面端实际由 Python 承载，C++ 引擎是否被 Python 调用存疑（Python 走自己的 `nevo_client.py`） | `src/client/gui_python/nevo_client.py` 1213 行独立实现 |
| Low | 控制台客户端仅在依赖齐全时编译，CI 覆盖路径不确定 | `src/client/CMakeLists.txt` 条件编译 |

### 1.3 完成度
- 核心库逻辑 ~80%（连接/频道/音频/视频/状态机齐全，缺集成验证证据）；**Qt UI 0%**。

---

## 2. Python PyQt5 GUI（`src/client/gui_python/`）— 主力桌面客户端

### 2.1 现状
- ~19.4k LOC，51 个 git 跟踪文件。双入口：
  - `main.py`（主）→ `main_window.py`（1608 行，FluentWindow 风格，**功能完整**）
  - `main_v2.py` → `v2/main_window.py`（1148 行，Discord 风格重写，**部分完成**）
- 功能（v1）：登录/连接、频道、语音、文字聊天、视频通话、屏幕共享、权限/管理、设置、主题（`theme_manager.py`）、i18n（3 语言 × 233 key）、自动更新（`updater.py` 866 行）全部具备。

### 2.2 问题
| 等级 | 问题 | 证据 |
|------|------|------|
| **High** | 控制通道使用手写二进制协议 `nevo_wire.py`（1615 行，`struct.pack` 长度前缀），**非 protobuf**；其 `MessageType` ~51 项 vs `control.proto` 57 条，需手工同步，易静默漂移 | `nevo_wire.py:435-522` |
| **High** | v2 重写无 i18n（8 个文件 0 处 `self.tr()`，硬编码中文）；设置 4 个占位页"即将推出"；屏幕共享仅未接线信号 | `v2/chat_panel.py:144`、`v2/settings_window.py:191-194`、`v2/video_call_window.py:68` |
| **High** | 测试仅 `tests/test_updater_fix.py`（5 个用例，仅覆盖 updater）；GUI/协议/语音/视频零覆盖 | `gui_python/tests/` |
| Medium | 256 处 `except Exception`，多处 `pass` 吞错（`main.py:60-71`） | 全目录 |
| Medium | 巨型单文件：`nevo_wire.py` 1615、`main_window.py` 1608、`nevo_client.py` 1213、`v2/main_window.py` 1148 | wc -l |
| Medium | `main_v2.py:17` 日志写入 `os.getcwd()`，相对 `main.py` 的可写目录逻辑是回归 | `main_v2.py:17` |
| Low | v1/v2 双窗口重复维护信号桥接；`debug_userlist.py` 调试脚本入库 | — |

### 2.3 完成度
- v1 ~90%（功能完整、i18n/主题/更新齐全）；v2 ~65%（缺 i18n/屏幕共享/设置页）。整体桌面端 ~85%。

---

## 3. Web 网关 + JS 客户端（`webclient/`）

### 3.1 现状
- `gateway.py`（1131 行）：纯标准库桥接器。手写 RFC 6455 WebSocket 服务器 + HTTP 静态服务；`ClientBridge` 包装 `NevoClient`（TCP 24430）转发 JSON 事件；`MediaBridge` 将浏览器媒体帧桥接到 UDP 语音/视频（protobuf 头 + NaCl 加密）。含 mock 模式。
- `app.js`（1670 行）：真实 Discord 风格浏览器客户端（连接/频道/聊天/语音/视频/管理/文件/屏幕共享/主题/i18n）。
- 音频方案：**WebCodecs over WebSocket**（非 WebRTC）—— mic → `AudioEncoder` Opus → base64 → WS → gateway UDP。
- 注：`web/` 是**另一个独立应用**（服务端管理面板，REST+SSE，TCP 24433，端口 8090），非用户客户端。

### 3.2 问题
| 等级 | 问题 | 证据 |
|------|------|------|
| **High** | 无 TLS：HTTP-only，WS 默认 `ws://`，**登录密码明文 JSON 传输**；全项目无 `wss://` 支持 | `gateway.py:1116,299`、`app.js:130,895` |
| **High** | 死代码/协议错配：`js/gateway.js`（97 行）实现的是另一套协议（`command`/`req_id`），与 `gateway.py`（`action`/`id`/`event`）不匹配，且未被 `index.html` 加载；`js/icons.js` 同样未引用 | `gateway.js:29,43-48`、`index.html:597-599` |
| Medium | 网关端点无鉴权（默认绑定 127.0.0.1 缓解）；解密后的语音/视频以明文 base64 经未加密 WS 转发 | `gateway.py:27` |
| Medium | 24 处宽泛 `except Exception`，媒体收发路径多处静默 `pass`（`gateway.py:760,808,835,887,960`） | — |
| Medium | 使用**已废弃** `ScriptProcessorNode`（注释写 AudioWorklet 但实现是 ScriptProcessor） | `media.js:192` |
| Medium | 测试仅 2 个 Playwright 波形 UI 用例，无网关/协议/媒体单测 | `webclient/tests/` |
| Low | `electron/node_modules` 入库（应 gitignore） | — |

### 3.3 完成度
- 功能 ~75%（功能面广但安全与死代码问题突出）。XSS 已缓解（聊天 `innerHTML` 转义），Electron 安全配置良好（contextIsolation）。

---

## 4. React 客户端（`nevo-client/`）— 纯 UI 原型

### 4.1 结论（Critical）
**nevo-client 是高保真 UI 原型 / 设计稿，不是功能客户端。** 全 `src/` **零网络代码**——无 WebSocket/fetch/axios/protobuf/WebRTC/getUserMedia；唯一的网络引用是 `index.css:1` 的 Google Fonts。所有 store 由 `src/data/mockData.ts` 驱动。

### 4.2 问题
| 等级 | 问题 | 证据 |
|------|------|------|
| **Critical** | 无任何后端集成；`sendMessage` 仅追加本地数组从不发送；`connected:true`、`latency:12` 硬编码 | `useChatStore.ts:15-27`、`useVoiceStore.ts:19-20` |
| **Critical** | 无真实音视频：无 `<video>/<audio>/<canvas>`；`VoiceBars.tsx` 是纯 CSS 动画；`SelfViewPiP.tsx` 是占位 div | — |
| High | 无登录路由（`App.tsx:9-15`）；`ConnectionBar.tsx` 仅静音/ deafen 按钮 + 硬编码延迟 | — |
| Medium | `tsconfig.json:19` `strict:false`（类型安全弱）；`vite.config.ts:19-27` 引入第三方推广插件 `vite-plugin-trae-solo-badge`，向生产构建注入 "Trae Solo" 广告徽章 | — |
| 优点 | 无障碍扎实（22 处 aria-label、语义化标签、真实 button）；`any` 0 处、TODO 0 处；组件分层清晰 | — |

### 4.3 完成度
- 作为"客户端" ~30%（仅 UI 外壳 + 本地状态）；作为"UI 设计交付物" ~90%。

---

## 5. Android 客户端（`mobile/android/`）

### 5.1 现状
- 75 个 Kotlin 文件，~8148 LOC。架构分层真实清晰（`core/` vs `feature/`，Hilt DI，消息处理拆分为 Dispatcher + Handler）。
- 功能模块齐全：连接/认证、频道、聊天（Room DAO）、语音、屏幕共享（MediaCodec/MediaProjection）、设置/更新（OkHttp + 镜像回退）。

### 5.2 问题
| 等级 | 问题 | 证据 |
|------|------|------|
| **Critical** | JNI `.so` 未接入 Gradle：`NativeAudioEngine.kt:13`、`CryptoManager.kt:18` 调用 `System.loadLibrary("nevo_jni")`，native C++ 源与 `native/CMakeLists.txt` 存在，但 `app/build.gradle.kts` **无 externalNativeBuild/cmake 块** → 运行时 `UnsatisfiedLinkError` 被捕获、`nativeAvailable=false` → Opus 编解码静默 no-op，**语音在发布包中失效** | `NativeAudioEngine.kt:38,48,59` |
| **Critical** | 加密退化为不安全 fallback：native 永不加载 → `fallbackEncryptSealed`（150-170）将 AES 会话密钥**明文嵌入输出**、忽略收件人公钥；`fallbackGenerateKeyPair`（141-148）返回两个无关随机数组（非真实 X25519 密钥对） | `CryptoManager.kt` |
| **Critical** | **零测试**：`app/src/test`、`app/src/androidTest` 不存在，尽管 gradle 已声明 junit5/mockk/turbine/truth 依赖、AGENTS.md 文档化 `./gradlew test` | Glob 无文件 |
| **High** | 协议手写非 protobuf：`NevoBuffer.kt`（小端长度前缀）+ `ProtocolSerializer.kt` 手工序列化 ~40 条消息映射到 caseValue 1-50，名称对齐 `control.proto` 但**线格式非 protobuf 编码**，无 protobuf 依赖，无 codegen 保证 | `core/protocol/`、`app/build.gradle.kts` |
| Medium | `AndroidManifest.xml:27` `usesCleartextTraffic="true"` + `REQUEST_INSTALL_PACKAGES`（APK 自更新），发布需复核 | — |
| Medium | 多处吞错：`ScreenShareEngine.kt:159,183` 空 catch；native 包装普遍 log-and-swallow | — |
| 优点 | 架构干净、`libs.versions.toml` 完整（Kotlin 2.0.21/AGP 8.7.3/Compose BOM 2024.12）、权限声明恰当、0 TODO | — |

### 5.3 完成度
- 结构 ~85%，但功能 ~55%（语音/加密因 native 未构建而失效，零测试）。

---

## 6. 设计稿目录（非客户端）

- `nevo-ui-design/`、`nevo-gui-design/`：静态 HTML 设计交付物（含 `orchestration-summary.json`、`colors_and_type.css`、`pages/*.html`），是 UI 设计产物而非可运行客户端。
- 注：`nevo-ui-design` 设计稿使用 Inter 字体、深色 teal 配色；`nevo-gui-design` 使用 emerald 配色。两套设计语言并存，与 React 原型、Python v2 之间缺乏统一的设计系统收敛。

---

## 7. 跨模块综合问题

| 等级 | 问题 | 影响 |
|------|------|------|
| **Critical** | 协议三轨制（protobuf / Python nevo_wire / Android NevoBuffer），无统一 codegen，手工同步 | 客户端-服务端静默不兼容，多端互通脆弱 |
| **Critical** | Android 语音/加密因 JNI 未构建而失效 + 零测试 | 移动端核心功能不可用 |
| **Critical** | React 客户端无后端，仅原型 | 若被误认为可用客户端将误导排期 |
| **High** | Web 客户端明文 ws + 密码明文 | 凭证泄露风险 |
| **High** | 6 套客户端并行，重复建设，UI/协议/测试标准不一 | 维护成本高、一致性差 |
| **High** | AGENTS.md 声明的 `src/ui` Qt 层缺失，文档与现状不符 | 架构认知偏差 |
| Medium | 各端测试普遍薄弱（Python 仅 updater、Web 仅波形、Android 零、React 无） | 回归风险高 |
| Medium | 多端宽泛异常吞错 | 故障难定位 |

---

## 8. 建议优先级

1. **P0**：修复 Android Gradle 接入 native 构建（恢复语音/加密）；或明确降级标注移动端为实验性。
2. **P0**：统一协议——以 `proto/*.proto` 为唯一真源，Python/Android 迁移到 protobuf codegen，消除手写编解码。
3. **P0**：Web 客户端启用 `wss://` + 网关鉴权，禁止密码明文。
4. **P1**：明确各客户端定位——建议收敛主力端（如 Python 桌面 + Web + Android），将 React `nevo-client` 标注为设计原型或接入真实后端。
5. **P1**：补齐 `src/ui` 或在 AGENTS.md 中更正架构描述。
6. **P1**：为协议序列化/加密/语音路径补单元测试（Python、Android、Web）。
7. **P2**：v2 GUI 补齐 i18n/屏幕共享/设置；清理 Web 死代码（gateway.js/icons.js）；移除 React 推广徽章插件；统一设计系统。

---

## 9. 证据与方法

- 方法：目录结构扫描 + LOC 统计 + 依赖清单审阅（requirements.txt / package.json / build.gradle.kts / libs.versions.toml）+ 4 个并行深度代码审计（Python GUI / Web / React / Android）+ C++ 核心直接审阅。
- 限制：未实际编译/运行各客户端（只读审计），完成度百分比为基于代码静态分析的估算，非运行时验证结果；Android JNI 失效、Web 明文等为代码级证据，未做真机/抓包复现。
