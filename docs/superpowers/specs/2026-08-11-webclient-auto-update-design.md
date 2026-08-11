# V3 Electron 客户端在线更新 — 设计文档

日期：2026-08-11
状态：已批准
目标应用：V3 Electron 客户端（`webclient/electron/` + `webclient/`，打包产物 `test/V3/`）

## 1. 背景与目标

V3 是 NEVO 的 Electron 桌面客户端（NSIS 安装包），目前**没有**任何在线更新能力。
本设计为其集成自研在线更新模块，实现：

- 定期自动检测新版本；
- 双更新源：GitHub Releases 为主源，GitHub 加速代理（ghproxy 系列）为国内备用源；
- 主源失败或 5 秒超时时自动切换备用源；
- 增量更新与全量更新两种模式，按包大小自动择优；
- 下载进度显示、断点续传、异常处理；
- 更新完成后自动重启并应用新版本；
- 完整更新日志（时间、版本、更新源、结果）。

约束：不新增 npm 运行时依赖（Electron 打包环境离线）；不修改 Python 网关协议。

## 2. 架构与组件

```
渲染进程 (index.html + app.js)
   │  window.updaterAPI (contextBridge)
   ▼
preload.js ──ipcMain──► 主进程 updater.js
   │                        ├─ UpdateSources（GitHub 主源 / ghproxy 备用源）
   │                        ├─ Downloader（Range 断点续传 + 进度 + 重试 + SHA256）
   │                        ├─ UpdateEngine（状态机 + 增量/全量决策 + 应用/重启 + 日志）
   │                        └─ UpdateScheduler（定时检测）
   ▼
.nevo_update/（暂存、.part、备份、update_log.json）
```

| 组件 | 位置 | 说明 |
|------|------|------|
| `updater.js` | `webclient/electron/updater.js`（新增） | 核心更新引擎，Node 内置模块实现 |
| `main.js` | `webclient/electron/main.js`（修改） | 启动时初始化 updater、注册 IPC、接入退出流程 |
| `preload.js` | `webclient/electron/preload.js`（修改） | 暴露 `window.updaterAPI` |
| `index.html` | `webclient/index.html`（修改） | 设置页新增"关于与更新"区块（静态 DOM） |
| `app.js` | `webclient/js/app.js`（修改） | 更新 UI 逻辑（进度、弹窗、日志查看） |
| `theme.css` | `webclient/css/theme.css`（修改） | 更新区块样式（进度条等） |
| `make_release.py` | `webclient/tools/make_release.py`（新增） | 发布辅助：生成 `latest.json` 清单与可选 delta.zip |
| `test_updater.js` | `webclient/tests/test_updater.js`（新增） | 单元测试 |
| `test_updater_e2e.js` | `webclient/tests/test_updater_e2e.js`（新增） | 集成测试（本地 mock HTTP 服务器） |

## 3. 更新源与优先级策略

### 3.1 版本与清单

- 当前版本号取自 `package.json` 的 `buildVersion`（如 `BETA0.0.1`），语义化解析为 `(major, minor, patch)` 三元组比较。
- 每个 GitHub Release 附带资产 `latest.json`，结构：

```json
{
  "version": "BETA0.0.2",
  "min_version": "BETA0.0.1",
  "published_at": "2026-08-11T00:00:00Z",
  "changelog": "...",
  "files": [
    { "path": "app.asar", "sha256": "...", "size": 12345 },
    { "path": "_internal/js/app.js", "sha256": "...", "size": 1234 }
  ],
  "full_package": {
    "url": "https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/NEVO-Web-Client-BETA0.0.2-Setup.exe",
    "size": 52428800,
    "sha256": "..."
  },
  "delta": {
    "from": "BETA0.0.1",
    "url": "https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/NEVO-delta-BETA0.0.1-BETA0.0.2.zip",
    "size": 102400,
    "sha256": "..."
  }
}
```

### 3.2 检测流程（主源优先，5 秒超时）

1. **主源**：`GET https://api.github.com/repos/TNEllya/NEVO/releases/latest`，超时 5s。
   - 成功 → 在 `assets` 中定位 `latest.json` → 下载清单（超时 5s）。
   - 404（无 release）→ 记为无更新。
   - 403（限流）→ 记录日志并走备用源。
   - 超时/网络错误 → 走备用源。
2. **备用源（mirror）**：将主源检测/下载 URL 前置 ghproxy 代理：
   - 清单下载：`https://ghproxy.com/{latest.json 原始 URL}`
   - 文件下载：`https://ghproxy.com/{browser_download_url}`
   - 备用源同样 5s 超时，仍失败则本次检测结束并记录错误。
3. 每次检测与下载记录所用源：`github` / `mirror`。

### 3.3 版本比较

```
parse("BETA0.0.1") == (0,0,1)；parse("v1.2.3-beta") == (1,2,3)
新版本 > 当前版本 → 有更新；否则无更新。
```

## 4. 增量 / 全量自适应决策

```
if delta 存在
   and parse(delta.from) == parse(本地版本)
   and delta.size > 0
   and delta.size < full.size * 0.5      # 阈值可配置，默认 50%
   → 增量模式
else
   → 全量模式
```

- **增量模式**：下载 `delta.zip` → SHA256 校验 → 解压到 `.nevo_update/extracted` → 按清单（zip 内 manifest.json 或 latest.json 的 files 列表）把差异文件替换到安装目录（`resources/app.asar`、`resources/nevo_gateway/_internal/**`）→ 重启应用。
- **全量模式**：下载 `full_package`（NSIS Setup.exe）→ SHA256 校验 → 退出主进程 → `Setup.exe /S` 静默安装 → NSIS 完成后自动启动新版本。

## 5. 下载与断点续传

- 每个文件下载到 `<目标>.part`；已存在时请求 `Range: bytes=<已有大小>-`。
- 服务端返回 `206` → 追加写；返回 `200`（不支持 Range）→ 从 0 重下；`416` → 丢弃 `.part` 重下。
- 进度回调：`(percent, speed, downloaded, total)`，200ms 节流推送渲染进程。
- 重试：网络错误最多 3 次，退避 3s / 6s / 9s。
- SHA256 校验失败 → 删除该 `.part` 并报错，可重新开始。
- 下载可取消；取消保留 `.part`（下次可续传），`cancel_download` 亦提供"清空暂存"选项。

## 6. 应用更新与重启

### 6.1 全量模式

1. 主进程调用 `app.quit()` 前记录"pending_full_install"状态到 `.nevo_update/pending.json`；
2. `spawn('Setup.exe', ['/S'])`（`shell: true`，主进程先行退出）；
3. NSIS 静默安装完成时（`runAfterFinish` 默认）自动启动新版本。

### 6.2 增量模式

Windows 下文件被占用（当前进程加载中），采用辅助脚本方案（同 Python 端 `_build_windows_update_bat` 思路）：

1. 将差异文件暂存至 `.nevo_update/staged/<相对路径>`；
2. 替换前把原文件备份到 `.nevo_update/backup/<相对路径>`；
3. 生成 `apply_update.cmd`：等待当前进程 PID 退出 → 逐文件替换 → 删除备份 → 启动应用；
4. 替换失败时用备份回滚，并记录日志。

### 6.3 回滚

- 增量替换全程记录 manifest（原路径/备份路径）；任一文件替换失败即回滚已替换文件。
- 全量安装失败（Setup 进程退出码非 0）→ 记录错误，应用保持旧版本。

## 7. 更新日志

- 路径：`<安装目录>/.nevo_update/update_log.json`（与 Python 端 `get_update_dir()` 的 `.nevo_update` 约定一致；暂存/备份/清单也在此目录下）。
- 每条记录字段：`timestamp`、`event`、`current_version`、`target_version`、`source`（github/mirror）、`mode`（delta/full）、`result`（success/failed/cancelled）、`error`。
- 事件类型：`check_start`、`check_ok`、`no_update`、`check_error`、`switch_mirror`、`download_start`、`download_progress`（不逐条记录，仅汇总）、`download_complete`、`verify_fail`、`apply_start`、`apply_success`、`apply_error`、`rollback`、`restart`。
- 保留最近 200 条；渲染进程"查看更新日志"读取并展示。

## 8. UI 设计（设置页"关于与更新"区块）

- 显示：当前版本、自动检测开关（默认开）、最近检查时间。
- 按钮："检查更新"、"查看更新日志"。
- 检测到新版本：后台自动下载，区块内显示进度条（百分比 + 速度 + 已下载/总大小）。
- 下载完成：弹窗"新版本 BETA0.0.2 已就绪，是否立即重启应用？"，`[立即重启]` / `[稍后]`。
  - 立即重启 → 走应用流程（全量静默安装 / 增量替换 + 重启）。
  - 稍后 → 本次不应用，下次启动时若暂存未过期则直接应用。
- 无更新 / 出错：toast 提示（含错误摘要）。

## 9. 测试方案

### 9.1 单元测试（Node，`test_updater.js`）

- `parseVersion` / `isNewer`：常规、`v` 前缀、非标准格式。
- 清单解析：缺字段、非法 JSON、delta 缺失。
- 决策逻辑：delta 存在且 <50% → delta；否则 full；本地版本不连续 → full。
- ghproxy URL 拼接：github.com 下载 URL 前置代理。
- 日志写入/截断（200 条上限）。

### 9.2 集成测试（`test_updater_e2e.js`）

- 本地 `http.createServer` mock 源：提供 `latest.json`、增量 zip、全量文件、支持 `Range`。
- 场景：断点续传（中断后恢复）；sha256 校验失败重下；增量替换到临时安装目录；全量模式生成正确命令。
- 验证 `.part` 续传与日志内容。

### 9.3 手动验收

1. 打包新版本（`buildVersion` 提升），生成 `latest.json` + delta.zip 上传 Release；
2. 旧版本客户端启动 → 自动检测 → 进度 → 重启 → 版本号更新；
3. 断网/主源超时场景 → 自动切换镜像源；
4. 检查 `update_log.json` 记录完整。

## 10. 发布流程（`make_release.py`）

1. 输入：目标版本号、上一版本号、GitHub 资产文件名、打包产物路径；
2. 输出：
   - `latest.json`（自动计算每个文件 sha256/size，生成 full_package/delta 条目）；
   - `NEVO-delta-<from>-<to>.zip`（与上一版本解包目录 diff，仅含差异文件 + 内嵌 manifest.json）；
3. 人工上传至 GitHub Release 资产。

## 11. 版本号与缓存策略

- 前端静态资源已带 `?v=N` 版本参数；增量替换新文件后，`index.html?v` 一并升级避免缓存复用（沿用现有 bump 习惯）。

## 12. 风险与对策

| 风险 | 对策 |
|------|------|
| ghproxy 代理不稳定 | 可配置多个代理前缀列表，顺序尝试；全部失败则报错并保留旧版本 |
| 增量替换破坏 app.asar | 替换前备份 + 回滚；app.asar 替换失败立即回滚 |
| 静默安装卡住 | Setup 进程超时（默认 5min）后强制结束并记录日志，应用保持旧版本 |
| 5s 超时误判（国内访问 GitHub 慢） | 超时仅影响本次检测，备用源兜底；定期重试 |

## 13. 范围外（后续迭代）

- 签名验证（代码签名证书校验）；
- Electron 主程序二进制级差分（当前 app.asar 作为单文件整替）；
- 强制更新（最低支持版本强制升级）；
- Android / Python 客户端更新功能同步增强。
