# NEVO Web Client — 更新下载多线路优化设计

日期：2026-08-12
状态：设计稿待评审

## 1. 背景与问题

### 1.1 更新异常（当前故障）

用户实测“检查更新”时出现：

```text
检查更新失败: github: request timeout | mirror: request timeout
```

根因（已通过静态链路分析确认）：

1. 清单（`latest.json`）从 GitHub Release 直连下载超时（5 秒）。
2. 备用源仅配置一个 `https://ghproxy.com/`，该镜像下载同样超时。
3. **镜像轮次的 GitHub API 请求仍然访问 `api.github.com` 主源**，`checkForUpdates()` 调用 API 时未传入 `isMirror`（[updater.js:329](file:///C:/Users/yzd20/Desktop/Project/NEVO/webclient/electron/updater.js#L327-L334)），而 `proxyGithubUrl()` 也不匹配 `api.github.com`。
4. `proxyGithubUrl()` 只处理 `github.com` 与 `objects.githubusercontent.com`，`api.github.com` 原样返回，因此 API 层没有真正的备用源。
5. 检测流程固定为 `github`、`mirror` 两轮，无重试、无多镜像。
6. `maxRetries` 只用于文件下载重试，清单下载失败不会触发 3/6/9 秒退避。

### 1.2 其它已确认的更新链路缺陷

- **增量包下载后不进入应用流程**：UI 下载完成后直接调用 `restartToApply()`（[app.js:1638-1643](file:///C:/Users/yzd20/Desktop/Project/NEVO/webclient/js/app.js#L1636-L1648)），但从未调用 `applyDelta()`，导致 `_stagedCmd` 为空，增量更新实际不会生效。
- **“自动检测”开关未控制主进程定时任务**：UI 保存 `auto_check_update`，但主进程 [main.js:308-313](file:///C:/Users/yzd20/Desktop/Project/NEVO/webclient/electron/main.js#L307-L313) 无条件定时检查。
- **自动检查与手动检查可并发**：`checkForUpdates()` 无互斥，可能相互覆盖状态。
- **窗口重建会重复注册 IPC handler 与定时器**：`startUpdaterService()` 在每次 `createWindow()` 时调用（[main.js:186-187](file:///C:/Users/yzd20/Desktop/Project/NEVO/webclient/electron/main.js#L186-L187)），macOS 重建窗口会重复 `ipcMain.handle`。
- **404 被当作错误**：设计文档要求“404（无 release）→ 无更新”，实际会报 `HTTP 404`。
- **找不到 `latest.json` 资产时误报“已是最新版本”**：无法区分“发布不完整”与“确实最新”。
- **下载更新包未使用选中镜像**：即使清单通过镜像获取，`downloadUpdate()` 仍直连 `github.com`。

### 1.3 用户需求

- 修复 GitHub 与镜像清单同时超时的问题。
- 多线路（GitHub 直连 + 多个国内镜像）自动测速。
- 自动选择延迟最低的线路进行下载。
- 线路切换对用户透明、无明显感知延迟。
- 提供线路状态显示与手动切换按钮。
- 新增实时测速逻辑。

## 2. 设计目标

| 目标 | 说明 |
|------|------|
| 更新可靠 | GitHub 直连与多个镜像均失败时才报错；单线路失败自动切换 |
| 智能选路 | 基于真实下载探测（TTFB + 首段下载速度）评分选优 |
| 体验透明 | 自动模式下载前静默选路，切换不断点续传，无需用户干预 |
| 可干预 | 设置页展示线路状态，支持手动固定线路 |
| 修复存量缺陷 | 增量应用链路、自动检测开关、IPC 重复注册、并发互斥、404 语义 |

**边界**：多线路仅作用于**软件更新下载**，不改变语音/媒体服务器连接。

## 3. 技术方案

### 3.1 更新源模型重构（多源多轮）

引入“源轮次（rounds）”概念，取代固定两轮：

```
rounds = [
  { name: 'github',        api: 使用 api.github.com,            manifest: 直连,             download: 直连 },
  { name: 'mirror:ghproxy', api: 由 github 轮次结果回填,        manifest: ghproxy 前缀,     download: ghproxy 前缀 },
  { name: 'mirror:ghfast', api: 由 github 轮次结果回填,         manifest: ghfast.top 前缀,  download: ghfast.top 前缀 },
  ...可扩展
]
```

**API 获取策略**：

- GitHub 主轮次通过 `GET https://api.github.com/repos/{owner}/{repo}/releases/latest` 获取 release 信息，进而得到 `latest.json` 的 `browser_download_url`（记为 `assetUrl`）。
- **镜像轮次复用主源已取得的 `assetUrl`**，直接以镜像前缀下载 `latest.json`，无需重复请求 API。这修复了截图场景（API 成功、清单下载超时）下镜像轮次仍访问主源 API 的缺陷。
- 仅当主源 API 本身失败时，才尝试以镜像前缀代理 `api.github.com`（`proxyGithubUrl()` 增加对 `api.github.com` 的匹配）。若某镜像不支持 API 代理，该线路记为不可用，不阻塞流程。
- 若主源 API 失败且所有镜像 API 均不可用，报错并写更新日志（记录各源失败原因）。

**清单获取策略**：

- 每个轮次使用各自的前缀从 `asset.browser_download_url` 拉取 `latest.json`。
- 每轮超时 5 秒（保持需求）。
- 任一轮成功即停止，不再重复请求。

**超时与失败语义修正**：

- `404`（release 不存在）→ 返回“无更新”，不报错。
- 找不到 `latest.json` 资产 → 返回“无更新”，但在日志中记录 `reason: asset_missing`，UI 文案区分“已是最新”与“发布不完整”。

### 3.2 多线路测速（实时测速）

新增 `probeRoutes(routes, probeUrl, opts)`：

- 对每条线路执行**真实下载探测**：向 `probeUrl` 发送 `Range: bytes=0-32767`（32KB），统计：
  - TTFB（首字节延迟）
  - 首段下载耗时 → 速度（bytes/sec）
  - 是否成功 / 失败原因（timeout / DNS / HTTP status）
- 每条线路探测 **2 次**，取中位值，避免单次抖动误判。
- **并行探测**所有线路（`Promise.allSettled`），避免 N 条串行累加超时。
- 探测目标：优先探测当前选中的更新包 URL（delta 或 full）；探测前若尚不知更新包 URL，则探测 `latest.json` URL。

**选路评分**：

```
score = weight_latency × TTFB 归一化 + weight_speed × 速度归一化 + 可用性惩罚
```

简单且可测试的实现：

- 不可用线路直接淘汰。
- 可用线路按 `TTFB` 排序；`TTFB` 相差 ≤ 20% 时再比较下载速度。
- 记录连续 3 次探测的成功率，成功率低的线路降权。

**防抖策略（自动模式）**：

- 仅当“当前线路失败”或“新线路得分明显优于当前（≥ 20%）”时才自动切换。
- 切换前完成探测；切换对用户透明（下载尚未开始或处于 `.part` 断点续传状态）。

**实时测速**：

- 下载过程中已有 `onProgress(percent, speed, ...)` 实时上报速度。
- 新增 `probe:result` IPC 事件，探测完成后推送各线路状态（名称、TTFB、速度、状态）到渲染进程，设置页实时刷新。
- 下载开始前自动完成一次探测；用户可点击“重新测速”手动触发。

### 3.3 断点续传 + 多线路故障转移

`downloadWithResume` 扩展为 `downloadWithRoutes(urls, destPath, opts)`：

- `urls` 为有序线路 URL 数组（已按评分排序）。
- 依次尝试：第一线路失败（网络/超时/HTTP 错误）→ 自动使用下一线路继续下载。
- **同一 `.part` 文件**，切换线路后发送 `Range: bytes=<existing>-` 续传，不重复下载已下载部分。
- SHA256 校验失败视为该线路下载无效，清空 `.part` 换线路重试。
- 每次线路切换写更新日志 `route_failover`。

### 3.4 增量应用链路修复

- `downloadUpdate()` 在 delta 模式下载完成后**自动调用 `applyDelta()`**，生成替换脚本与 `_stagedCmd`。
- UI 侧流程简化为：`checkNow()` → `download()`（内部完成 delta 暂存）→ `restartToApply()`。
- 状态机新增 `ready_to_install`（delta 已暂存待重启）。

### 3.5 主进程修复

- `startUpdaterService()` 改为应用级**单例初始化**（`app.on('ready')` 中执行一次），只注册一次 IPC handler 与定时器；`send()` 通过当前有效窗口引用发送。
- 新增 IPC：
  - `updater:probe` → 触发测速并返回线路状态
  - `updater:set-auto-check(enabled)` → 控制定时检测的启停
  - `updater:select-route(name)` → 手动固定线路（自动模式仍允许故障转移）
- `checkForUpdates()` 增加互斥：`checking`/`downloading` 状态下再次调用直接返回“忙”。
- 自动检测开关状态由渲染进程在设置变更时同步到主进程。

### 3.6 UI 集成（线路状态显示 + 手动切换）

依据已确认的草图，在设置页“软件更新”区域下新增“更新线路”区块：

```
更新线路（自动/手动）
┌────────────────────────────────────────┐
│ 当前线路：<名称>      TTFB x ms · 速度  │
│ ────────────────────────────────────── │
│ ○ github   直连        42ms   2.1MB/s  切换│
│ ● mirror1  ghproxy    18ms   9.8MB/s  当前│
│ ○ mirror2  ghfast     25ms   8.1MB/s  切换│
│ ○ mirror3  超时        —      —        不可用│
│ [重新测速]  [自动选择开关]                │
└────────────────────────────────────────┘
```

- 自动模式：自动选择最优线路，状态显示为“自动（当前线路 xxx）”。
- 手动模式：点击“切换”固定线路；固定线路故障时临时故障转移并在 UI 提示。
- “重新测速”按钮触发 `updater:probe`，实时刷新各行延迟/速度。
- 下载中禁用切换（或提示“下载中不可切换”）。

### 3.7 内置线路配置

客户端内置以下更新线路（可配置数组，便于后续调整）：

| 线路名 | 前缀 | 说明 |
|--------|------|------|
| github | （无） | GitHub 直连（主源） |
| mirror-ghproxy | `https://ghproxy.com/` | 常用加速镜像 |
| mirror-ghfast | `https://ghfast.top/` | 备用加速镜像 |
| mirror-gh-proxy | `https://gh-proxy.com/` | 备用加速镜像 |

> 镜像前缀列表在 `CFG.mirrorPrefixes` 中配置；若某些镜像失效可后续在版本中调整，无需改协议。

## 4. 状态机

```
idle ──check──> checking ──(有更新)──> download_available
                                    │
                                    ▼
             download_available ──download──> downloading ──(delta 暂存完成)──> ready_to_install
                                                       └──(full 下载完成)──> ready
ready_to_install / ready ──restart──> 退出/应用
checking / downloading ──失败──> error
```

## 5. 文件改动清单

| 文件 | 改动 |
|------|------|
| `webclient/electron/updater.js` | 多轮检测、API 镜像、404 语义、`probeRoutes`、选路评分、多线路断点下载、delta 自动暂存 |
| `webclient/electron/main.js` | 单例化更新服务、新增 IPC（probe / set-auto-check / select-route）、互斥 |
| `webclient/electron/preload.js` | 暴露 `probeRoutes`、`setAutoCheck`、`selectRoute`、`onProbeResult` |
| `webclient/index.html` | “软件更新”区新增“更新线路”区块 |
| `webclient/js/app.js` | 线路状态渲染、自动/手动切换逻辑、重新测速、自动检测开关同步主进程 |
| `webclient/css/theme.css` | 线路列表、状态点、切换按钮样式 |
| `webclient/js/i18n.js` | 新增多语言文案 |
| `webclient/tests/test_updater.js` | 新增单测（多轮、镜像 API、评分、404、互斥） |
| `webclient/tests/test_updater_e2e.js` | 新增 e2e（多线路故障转移、断点续传切换、delta 自动暂存） |

## 6. 测试策略

- 单元：多轮检测（github 成功即停）、镜像 API URL 生成、404 → 无更新、`probeRoutes` 评分与防抖、`downloadWithRoutes` 故障转移（mock HTTP）、互斥、delta 自动暂存。
- E2E：本地 mock HTTP 服务器提供多条“线路”，模拟首线路超时→自动切换；Range 续传切换；SHA256 校验失败换线路。
- 手工验证：打包后启动 V3 客户端，检查更新 → 观察线路状态显示与自动选路；强制断网某镜像后观察故障转移；手动切换线路。
- 全量回归：`node webclient/tests/test_updater.js`、`node webclient/tests/test_updater_e2e.js` 全绿。

## 7. 风险与应对

| 风险 | 应对 |
|------|------|
| 个别镜像不支持 `api.github.com` 代理 | 镜像 API 失败只记该线路不可用，不阻塞主源流程 |
| 探测流量 | 探测仅 32KB × 2 次 × 线路数，且并行；不频繁自动探测 |
| 下载中线路切换丢进度 | 统一 `.part` 文件 + Range 续传，切换零重传 |
| 多线路导致 UI 复杂 | 默认自动模式，手动仅为辅助；状态列表默认折叠为当前线路摘要 |

## 8. 验收标准

1. 全部镜像可用时，检查更新自动选择延迟最低线路并完成下载。
2. 任一线路（含主源）失败时自动切换，用户无感知；`.part` 断点续传不重传已下载部分。
3. 设置页显示各线路名称、延迟、速度、可用状态；支持手动切换与重新测速。
4. 关闭“自动检测”后主进程停止定时检查。
5. 增量更新：下载完成 → 暂存 → 重启 → 新版本生效（验证 `ready_to_install` 状态与替换脚本）。
6. 修复后的回归测试全绿；V3 客户端启动无主进程错误。
