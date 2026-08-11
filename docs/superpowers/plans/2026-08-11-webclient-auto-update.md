# V3 Electron 客户端在线更新 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 V3 Electron 客户端集成自研在线更新功能：双更新源（GitHub 主源 / ghproxy 镜像，5s 超时自动切换）、增量/全量自适应、断点续传、进度显示、重启应用与完整更新日志。

**Architecture:** Electron 主进程新增 `updater.js`（Node 内置模块，零新增依赖）：纯函数部分（版本/清单/URL/决策/日志）与状态机引擎，检测走 GitHub API，下载走 Range 断点续传，增量用文件替换+`.cmd` 脚本，全量用 NSIS `/S` 静默安装。`preload.js` 桥接 `window.updaterAPI`，渲染进程设置页展示进度与交互。

**Tech Stack:** Node.js（内置 https/http/crypto/fs）、Electron IPC、Python 3（发布脚本）、NSIS（全量安装）。

**参考设计:** `docs/superpowers/specs/2026-08-11-webclient-auto-update-design.md`

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `webclient/electron/updater.js` | 创建 | 更新引擎：版本/清单/URL/日志/下载器/状态机/应用重启 |
| `webclient/electron/main.js` | 修改 | 初始化 updater、注册 IPC、退出流程接入 |
| `webclient/electron/preload.js` | 修改 | 暴露 `window.updaterAPI` |
| `webclient/index.html` | 修改 | 设置页新增"关于与更新"区块 |
| `webclient/js/app.js` | 修改 | 更新 UI 逻辑 |
| `webclient/css/theme.css` | 修改 | 更新区块样式 |
| `webclient/tools/make_release.py` | 创建 | 发布辅助：生成 latest.json + delta.zip |
| `webclient/tests/test_updater.js` | 创建 | 单元测试（纯 Node） |
| `webclient/tests/test_updater_e2e.js` | 创建 | 集成测试（本地 mock HTTP） |

测试运行方式：`node webclient/tests/test_updater.js` / `node webclient/tests/test_updater_e2e.js`（独立断言脚本，不依赖 npm）。

---

### Task 1: updater.js 骨架与版本解析/比较

**Files:**
- Create: `webclient/electron/updater.js`
- Test: `webclient/tests/test_updater.js`

- [ ] **Step 1: 写失败测试（版本解析与比较）**

创建 `webclient/tests/test_updater.js`：

```js
'use strict';
const assert = require('assert');
const U = require('../electron/updater.js');

let pass = 0, fail = 0;
function t(cond, msg) { if (cond) { pass++; } else { fail++; console.error('  FAIL:', msg); } }

// 版本解析
t(JSON.stringify(U.parseVersion('BETA0.0.1')) === '[0,0,1]', 'parseVersion BETA0.0.1');
t(JSON.stringify(U.parseVersion('v1.2.3-beta')) === '[1,2,3]', 'parseVersion v1.2.3-beta');
t(JSON.stringify(U.parseVersion('0.0.0')) === '[0,0,0]', 'parseVersion 0.0.0');
t(JSON.stringify(U.parseVersion('garbage')) === '[0,0,0]', 'parseVersion garbage');

// 版本比较
t(U.isNewerVersion('BETA0.0.2', 'BETA0.0.1') === true, '0.0.2 > 0.0.1');
t(U.isNewerVersion('BETA0.0.1', 'BETA0.0.1') === false, 'equal is not newer');
t(U.isNewerVersion('BETA0.0.1', 'BETA0.1.0') === false, '0.0.1 < 0.1.0');
t(U.isNewerVersion('1.10.0', '1.9.9') === true, 'patch compare');

console.log(`\nResult: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
```

- [ ] **Step 2: 运行测试确认失败**

Run: `node webclient/tests/test_updater.js`
Expected: 报错 `Cannot find module '../electron/updater.js'`（模块尚不存在）。

- [ ] **Step 3: 实现 updater.js 骨架与版本函数**

创建 `webclient/electron/updater.js`：

```js
'use strict';
/**
 * NEVO Web Client — 在线更新引擎
 * 零第三方依赖，全部使用 Node 内置模块。
 * 检测：GitHub API 主源（5s 超时）→ ghproxy 镜像源兜底
 * 下载：HTTP Range 断点续传 + SHA256 校验 + 重试
 * 应用：增量文件替换（辅助 .cmd 脚本）或全量 NSIS 静默安装
 */
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const { URL } = require('url');

let electron = null;
try { electron = require('electron'); } catch (_) { /* 纯 Node 测试环境 */ }

const CFG = {
  owner: 'TNEllya',
  repo: 'NEVO',
  timeoutMs: 5000,          // 更新源请求超时（需求：5 秒）
  checkIntervalMs: 3600 * 1000,
  maxRetries: 3,
  retryDelaysMs: [3000, 6000, 9000],
  deltaRatio: 0.5,          // 增量包小于全量包 50% 时用增量
  mirrorPrefixes: ['https://ghproxy.com/'],
  maxLogEntries: 200,
  chunkSize: 65536,
  progressThrottleMs: 200,
  setupTimeoutMs: 5 * 60 * 1000,
  assetName: 'latest.json',
};

// ============================================================
// 版本解析与比较
// ============================================================
function parseVersion(v) {
  const m = /(\d+)\.(\d+)(?:\.(\d+))?/.exec(String(v || '').replace(/^[vV]/, ''));
  if (!m) return [0, 0, 0];
  return [parseInt(m[1], 10), parseInt(m[2], 10), parseInt(m[3] || '0', 10)];
}

function isNewerVersion(candidate, current) {
  const a = parseVersion(candidate);
  const b = parseVersion(current);
  for (let i = 0; i < 3; i++) {
    if (a[i] !== b[i]) return a[i] > b[i];
  }
  return false;
}

module.exports = { CFG, parseVersion, isNewerVersion };
```

- [ ] **Step 4: 运行测试确认通过**

Run: `node webclient/tests/test_updater.js`
Expected: `Result: 8 passed, 0 failed`

- [ ] **Step 5: 提交**

```bash
git add webclient/electron/updater.js webclient/tests/test_updater.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add updater version parsing"
```

---

### Task 2: 更新源 URL 与清单解析

**Files:**
- Modify: `webclient/electron/updater.js`
- Test: `webclient/tests/test_updater.js`

- [ ] **Step 1: 追加失败测试**

在 `test_updater.js` 的 `process.exit` 前追加：

```js
// ghproxy URL 拼接
t(U.proxyGithubUrl('https://github.com/a/b/releases/download/v1/f.zip') === 'https://ghproxy.com/https://github.com/a/b/releases/download/v1/f.zip', 'proxy github download url');
t(U.proxyGithubUrl('https://ghproxy.com/https://github.com/a/b/x.zip') === 'https://ghproxy.com/https://github.com/a/b/x.zip', 'do not double-proxy');
t(U.proxyGithubUrl('https://example.com/x.zip') === 'https://example.com/x.zip', 'leave non-github url untouched');

// 清单解析
const goodManifest = JSON.stringify({
  version: 'BETA0.0.2',
  files: [{ path: 'app.asar', sha256: 'abc', size: 10 }],
  full_package: { url: 'https://github.com/x/Setup.exe', size: 100, sha256: 'f' },
  delta: { from: 'BETA0.0.1', url: 'https://github.com/x/d.zip', size: 20, sha256: 'd' },
});
const m = U.parseManifest(goodManifest);
t(m.version === 'BETA0.0.2', 'manifest version');
t(m.full.url.endsWith('Setup.exe'), 'manifest full url');
t(m.delta && m.delta.size === 20, 'manifest delta parsed');
let threw = false;
try { U.parseManifest('{"version":"x"}'); } catch (_) { threw = true; }
t(threw, 'manifest without full_package throws');
threw = false;
try { U.parseManifest('not json'); } catch (_) { threw = true; }
t(threw, 'manifest invalid json throws');
const noDelta = U.parseManifest(JSON.stringify({
  version: 'BETA0.0.2', files: [],
  full_package: { url: 'https://github.com/x/Setup.exe', size: 100, sha256: 'f' },
}));
t(noDelta.delta === null, 'manifest without delta -> null');

// 决策
t(U.decideMode(m, 'BETA0.0.1') === 'delta', 'small delta -> delta mode');
t(U.decideMode(m, 'BETA0.0.9') === 'full', 'from-version mismatch -> full mode');
const bigDelta = U.parseManifest(JSON.stringify({
  version: 'BETA0.0.2', files: [],
  full_package: { url: 'https://github.com/x/Setup.exe', size: 100, sha256: 'f' },
  delta: { from: 'BETA0.0.1', url: 'https://github.com/x/d.zip', size: 80, sha256: 'd' },
}));
t(U.decideMode(bigDelta, 'BETA0.0.1') === 'full', 'delta >= 50% full -> full mode');
t(U.decideMode(noDelta, 'BETA0.0.1') === 'full', 'no delta -> full mode');
```

- [ ] **Step 2: 运行确认失败**

Run: `node webclient/tests/test_updater.js`
Expected: FAIL（`U.proxyGithubUrl` 等方法不存在）。

- [ ] **Step 3: 实现 URL 拼接、清单解析、决策**

在 `updater.js` 的 `module.exports` 前追加：

```js
// ============================================================
// 更新源 URL
// ============================================================
function githubApiLatestUrl() {
  return `https://api.github.com/repos/${CFG.owner}/${CFG.repo}/releases/latest`;
}

function proxyGithubUrl(url, prefix) {
  const p = prefix || CFG.mirrorPrefixes[0];
  if (/^https:\/\/(ghproxy|gh-proxy)/.test(url)) return url;
  if (/^https:\/\/(github\.com|objects\.githubusercontent\.com)/.test(url)) {
    return p + url;
  }
  return url;
}

// ============================================================
// 清单解析
// ============================================================
function parseManifest(text) {
  const data = JSON.parse(text);
  if (!data || typeof data.version !== 'string') {
    throw new Error('Invalid manifest: version missing');
  }
  if (!data.full_package || typeof data.full_package.url !== 'string') {
    throw new Error('Invalid manifest: full_package.url missing');
  }
  return {
    version: data.version,
    changelog: data.changelog || '',
    files: Array.isArray(data.files) ? data.files : [],
    full: {
      url: data.full_package.url,
      size: data.full_package.size || 0,
      sha256: data.full_package.sha256 || '',
    },
    delta: (data.delta && data.delta.url)
      ? { from: data.delta.from || '', url: data.delta.url, size: data.delta.size || 0, sha256: data.delta.sha256 || '' }
      : null,
  };
}

// ============================================================
// 增量/全量决策
// ============================================================
function decideMode(manifest, currentVersion) {
  const full = manifest.full;
  if (manifest.delta && manifest.delta.size > 0 && full.size > 0) {
    const fromOk = !manifest.delta.from ||
      parseVersion(manifest.delta.from).join('.') === parseVersion(currentVersion).join('.');
    if (fromOk && manifest.delta.size < full.size * CFG.deltaRatio) {
      return 'delta';
    }
  }
  return 'full';
}

module.exports = {
  CFG, parseVersion, isNewerVersion,
  githubApiLatestUrl, proxyGithubUrl, parseManifest, decideMode,
};
```

- [ ] **Step 4: 运行确认通过**

Run: `node webclient/tests/test_updater.js`
Expected: 全部通过（14 项左右）。

- [ ] **Step 5: 提交**

```bash
git add webclient/electron/updater.js webclient/tests/test_updater.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add updater manifest parsing and mirror proxy"
```

---

### Task 3: 更新日志模块

**Files:**
- Modify: `webclient/electron/updater.js`
- Test: `webclient/tests/test_updater.js`

- [ ] **Step 1: 追加失败测试**

```js
// 日志：写入、读取、截断
const os = require('os');
const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'nevo-upd-'));
const baseDir = path.join(tmpDir, 'install');
fs.mkdirSync(baseDir, { recursive: true });
U.logUpdateEvent(baseDir, { event: 'check_ok', current_version: 'BETA0.0.1', result: 'success', source: 'github' });
let log = U.readUpdateLog(baseDir);
t(log.length === 1 && log[0].event === 'check_ok', 'log entry written');
t(typeof log[0].timestamp === 'string' && log[0].timestamp.length > 0, 'log has timestamp');
U.logUpdateEvent(baseDir, { event: 'download_complete', target_version: 'BETA0.0.2' });
log = U.readUpdateLog(baseDir);
t(log.length === 2, 'log appends');
// 截断：写入 5 条超过上限的日志（maxLogEntries 临时改小验证）
const backupMax = U.CFG.maxLogEntries;
U.CFG.maxLogEntries = 3;
for (let i = 0; i < 5; i++) U.logUpdateEvent(baseDir, { event: 'x' + i });
log = U.readUpdateLog(baseDir);
t(log.length === 3, 'log truncated to maxLogEntries');
t(log[0].event === 'x2', 'log keeps newest');
U.CFG.maxLogEntries = backupMax;
```

需要 `path` 与 `fs` 在测试头部已 require；补 `const os = require('os'); const path = require('path'); const fs = require('fs');`（如缺失）。

- [ ] **Step 2: 运行确认失败**

Run: `node webclient/tests/test_updater.js`
Expected: FAIL（`U.logUpdateEvent` 不存在）。

- [ ] **Step 3: 实现日志模块**

在 `updater.js` 追加：

```js
// ============================================================
// 更新日志
// ============================================================
function getUpdateDir(baseDir) {
  const dir = path.join(baseDir, '.nevo_update');
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

function getLogPath(baseDir) {
  return path.join(getUpdateDir(baseDir), 'update_log.json');
}

function logUpdateEvent(baseDir, entry) {
  const logPath = getLogPath(baseDir);
  let entries = [];
  try { entries = JSON.parse(fs.readFileSync(logPath, 'utf-8')); } catch (_) { /* 首次写入 */ }
  if (!Array.isArray(entries)) entries = [];
  entries.push(Object.assign({ timestamp: new Date().toISOString() }, entry));
  if (entries.length > CFG.maxLogEntries) entries = entries.slice(-CFG.maxLogEntries);
  try { fs.writeFileSync(logPath, JSON.stringify(entries, null, 2), 'utf-8'); }
  catch (e) { console.warn('[Updater] log write failed:', e.message); }
}

function readUpdateLog(baseDir) {
  try { return JSON.parse(fs.readFileSync(getLogPath(baseDir), 'utf-8')); }
  catch (_) { return []; }
}
```

更新 `module.exports`，追加：`getUpdateDir, getLogPath, logUpdateEvent, readUpdateLog`。

- [ ] **Step 4: 运行确认通过**

Run: `node webclient/tests/test_updater.js`
Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add webclient/electron/updater.js webclient/tests/test_updater.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add updater log module"
```

---

### Task 4: 断点续传下载器

**Files:**
- Modify: `webclient/electron/updater.js`
- Test: `webclient/tests/test_updater.js`

- [ ] **Step 1: 追加失败测试（纯逻辑部分：Range 头构造与重试计算）**

```js
// 下载辅助：重试延迟
const delays = U.computeRetryDelays(3, [3000, 6000, 9000]);
t(delays.length === 3 && delays[0] === 3000 && delays[2] === 9000, 'computeRetryDelays');
```

- [ ] **Step 2: 运行确认失败**

Run: `node webclient/tests/test_updater.js`
Expected: FAIL（`U.computeRetryDelays` 不存在）。

- [ ] **Step 3: 实现下载器**

在 `updater.js` 追加：

```js
// ============================================================
// 下载器（Range 断点续传 + 进度 + 重试 + SHA256）
// ============================================================
function computeRetryDelays(retries, delays) {
  const out = [];
  for (let i = 0; i < retries; i++) out.push(delays[i % delays.length]);
  return out;
}

function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

function httpGet(url, headers, timeoutMs) {
  return new Promise((resolve, reject) => {
    let parsed;
    try { parsed = new URL(url); } catch (e) { reject(e); return; }
    const mod = parsed.protocol === 'https:' ? https : http;
    const req = mod.request(parsed, { method: 'GET', headers, timeout: timeoutMs }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        httpGet(res.headers.location, headers, timeoutMs).then(resolve, reject);
        return;
      }
      resolve(res);
    });
    req.on('timeout', () => req.destroy(new Error('request timeout')));
    req.on('error', reject);
    req.end();
  });
}

function sha256File(filePath) {
  const h = crypto.createHash('sha256');
  const data = fs.readFileSync(filePath);
  h.update(data);
  return h.digest('hex');
}

/**
 * 断点续传下载。
 * opts: { onProgress(percent,speed,downloaded,total), shouldCancel(), retries, timeoutMs, sha256 }
 */
async function downloadWithResume(url, destPath, opts = {}) {
  const { onProgress, shouldCancel, sha256 } = opts;
  const retries = opts.retries === undefined ? CFG.maxRetries : opts.retries;
  const timeoutMs = opts.timeoutMs === undefined ? CFG.timeoutMs : opts.timeoutMs;
  const partPath = destPath + '.part';
  const delays = computeRetryDelays(retries, CFG.retryDelaysMs);

  for (let attempt = 0; attempt <= retries; attempt++) {
    if (shouldCancel && shouldCancel()) throw new Error('cancelled');

    let existing = 0;
    try { existing = fs.existsSync(partPath) ? fs.statSync(partPath).size : 0; } catch (_) {}

    const headers = {};
    if (existing > 0) headers.Range = `bytes=${existing}-`;

    let res;
    try {
      res = await httpGet(url, headers, timeoutMs);
    } catch (err) {
      if (attempt < retries) { await sleep(delays[attempt]); continue; }
      throw new Error('download network error: ' + err.message);
    }

    if (res.statusCode === 416) { // 已完整，丢弃续传标记
      res.resume();
      fs.unlinkSync(partPath);
      existing = 0;
      if (attempt < retries) continue;
      throw new Error('HTTP 416');
    }
    if (res.statusCode !== 200 && res.statusCode !== 206) {
      res.resume();
      if (attempt < retries) { await sleep(delays[attempt]); continue; }
      throw new Error('HTTP ' + res.statusCode);
    }
    if (res.statusCode === 200) existing = 0;

    const serverLen = parseInt(res.headers['content-length'] || '0', 10);
    const total = existing + serverLen;
    let downloaded = existing;
    let startTime = Date.now();
    let lastNotify = 0;
    const flags = existing > 0 ? 'a' : 'w';

    await new Promise((resolve, reject) => {
      const stream = fs.createWriteStream(partPath, { flags });
      res.pipe(stream);
      res.on('data', (chunk) => {
        downloaded += chunk.length;
        const now = Date.now();
        if (now - lastNotify >= CFG.progressThrottleMs && onProgress) {
          lastNotify = now;
          const elapsed = (now - startTime) / 1000 || 0.001;
          onProgress(total ? (downloaded / total) * 100 : 0,
                     downloaded / elapsed, downloaded, total);
        }
      });
      res.on('error', reject);
      stream.on('error', reject);
      stream.on('finish', resolve);
    });

    if (shouldCancel && shouldCancel()) {
      fs.unlinkSync(partPath);
      throw new Error('cancelled');
    }

    fs.renameSync(partPath, destPath);
    if (sha256) {
      const actual = sha256File(destPath);
      if (actual !== sha256) {
        fs.unlinkSync(destPath);
        if (attempt < retries) { await sleep(delays[attempt]); continue; }
        throw new Error('sha256 mismatch');
      }
    }
    return destPath;
  }
  throw new Error('unreachable');
}
```

更新 `module.exports`：追加 `computeRetryDelays, httpGet, sha256File, downloadWithResume`。

- [ ] **Step 4: 运行确认通过**

Run: `node webclient/tests/test_updater.js`
Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add webclient/electron/updater.js webclient/tests/test_updater.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add resumable downloader"
```

---

### Task 5: UpdateEngine 状态机与检测流程

**Files:**
- Modify: `webclient/electron/updater.js`
- Test: `webclient/tests/test_updater.js`

- [ ] **Step 1: 追加失败测试（状态机与检测）**

```js
// 状态机：状态流转与回调
const engine = new U.UpdateEngine({ baseDir: path.join(tmpDir, 'engine') });
const states = [];
engine.onState((oldS, newS) => states.push(newS));
(async () => {
  try {
    await engine._setState('checking');
    await engine._setState('idle');
  } catch (e) { /* ignore */ }
  t(states.includes('checking') && states.includes('idle'), 'state transitions fire callbacks');
  t(engine.state === 'idle', 'engine final state idle');

  // 检测：mock 拉取器
  const fakeFetch = async (kind, url, opts) => {
    if (kind === 'api') {
      return { assets: [{ name: 'latest.json', browser_download_url: 'https://github.com/x/latest.json' }] };
    }
    if (kind === 'manifest') {
      return JSON.stringify({
        version: 'BETA0.0.2', files: [],
        full_package: { url: 'https://github.com/x/Setup.exe', size: 1000, sha256: 'f' },
        delta: { from: 'BETA0.0.1', url: 'https://github.com/x/d.zip', size: 10, sha256: 'd' },
      });
    }
    throw new Error('unexpected fetch kind ' + kind);
  };
  const e2 = new U.UpdateEngine({ baseDir: path.join(tmpDir, 'e2'), fetcher: fakeFetch, currentVersion: 'BETA0.0.1' });
  const info = await e2.checkForUpdates();
  t(info && info.mode === 'delta', 'check detects newer and decides delta');
  t(e2.state === 'download_available', 'state download_available');
  t(info.source === 'github', 'source github recorded');
  const e3 = new U.UpdateEngine({ baseDir: path.join(tmpDir, 'e3'), fetcher: async () => { throw new Error('net down'); }, currentVersion: 'BETA0.0.1' });
  let err = null;
  try { await e3.checkForUpdates(); } catch (e) { err = e; }
  t(!!err && e3.state === 'error', 'check failure -> error state');
  console.log(`\nResult: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
```

- [ ] **Step 2: 运行确认失败**

Run: `node webclient/tests/test_updater.js`
Expected: FAIL（`U.UpdateEngine` 不存在）。

- [ ] **Step 3: 实现 UpdateEngine**

在 `updater.js` 追加（放在 `module.exports` 之前）：

```js
// ============================================================
// UpdateEngine — 状态机
// ============================================================
class UpdateEngine {
  /**
   * opts: {
   *   baseDir,           // 安装目录（含 .nevo_update 子目录）
   *   currentVersion,    // 当前版本号（默认读 package.json buildVersion）
   *   fetcher,           // 可注入的网络拉取器（测试用），默认走真实网络
   *   execFn,            // 可注入的 spawn（测试用）
   * }
   */
  constructor(opts = {}) {
    this.baseDir = opts.baseDir || defaultBaseDir();
    this.currentVersion = opts.currentVersion || defaultCurrentVersion();
    this._fetcher = opts.fetcher || defaultFetcher();
    this._exec = opts.execFn || ((cmd, args) => {
      const { spawn } = require('child_process');
      return spawn(cmd, args, { detached: true, stdio: 'ignore' });
    });
    this._state = 'idle';
    this._cancel = false;
    this._stateListeners = [];
    this._progressListeners = [];
    this._manifest = null;
    this._mode = null;
    this._source = 'github';
    this._downloadedPath = null;
    this._log = (ev, det) => logUpdateEvent(this.baseDir, Object.assign(
      { current_version: this.currentVersion }, det || {}, { event: ev, result: 'success' }));
  }

  get state() { return this._state; }

  onState(fn) { this._stateListeners.push(fn); }
  onProgress(fn) { this._progressListeners.push(fn); }
  cancel() { this._cancel = true; }

  async _setState(s) {
    const old = this._state;
    this._state = s;
    for (const fn of this._stateListeners) { try { fn(old, s); } catch (_) {} }
  }

  async _emitProgress(percent, speed, downloaded, total) {
    for (const fn of this._progressListeners) {
      try { fn(percent, speed, downloaded, total); } catch (_) {}
    }
  }

  async _fetch(kind, url, opts) {
    return this._fetcher(kind, url, opts);
  }

  /** 检测新版本。返回 {version, mode, source, manifest} 或 null（无更新）。 */
  async checkForUpdates() {
    this._cancel = false;
    this._log('check_start', {});
    await this._setState('checking');
    const errors = [];

    for (const attempt of ['github', 'mirror']) {
      const isMirror = attempt === 'mirror';
      this._source = attempt;
      try {
        let apiData;
        try {
          apiData = await this._fetch('api', githubApiLatestUrl(), { timeoutMs: CFG.timeoutMs });
        } catch (err) {
          errors.push(`${attempt} api: ${err.message}`);
          if (!isMirror) { this._log('switch_mirror', { reason: err.message }); }
          continue;
        }
        const asset = (apiData.assets || []).find((a) => a.name === CFG.assetName);
        if (!asset) {
          this._log('no_update', { target_version: (apiData.tag_name || '').replace(/^[vV]/, '') });
          await this._setState('idle');
          return null;
        }
        const manifestText = await this._fetch('manifest', asset.browser_download_url, {
          timeoutMs: CFG.timeoutMs,
          isMirror,
          source: attempt,
        });
        const manifest = parseManifest(manifestText);
        this._manifest = manifest;
        if (!isNewerVersion(manifest.version, this.currentVersion)) {
          this._log('no_update', { target_version: manifest.version });
          await this._setState('idle');
          return null;
        }
        this._mode = decideMode(manifest, this.currentVersion);
        this._log('check_ok', { target_version: manifest.version, source: attempt, mode: this._mode });
        await this._setState('download_available');
        return {
          version: manifest.version,
          mode: this._mode,
          source: attempt,
          manifest,
          changelog: manifest.changelog,
        };
      } catch (err) {
        errors.push(`${attempt}: ${err.message}`);
      }
    }

    this._log('check_error', { error: errors.join(' | '), result: 'failed' });
    await this._setState('error');
    throw new Error(errors.join(' | '));
  }

  /** 下载并准备更新。返回 {mode, path}。 */
  async downloadUpdate() {
    if (!this._manifest) throw new Error('no manifest, run checkForUpdates first');
    const manifest = this._manifest;
    const mode = this._mode || decideMode(manifest, this.currentVersion);
    this._mode = mode;
    const updateDir = getUpdateDir(this.baseDir);
    const target = mode === 'delta'
      ? manifest.delta.url
      : manifest.full.url;
    const sha = mode === 'delta' ? manifest.delta.sha256 : manifest.full.sha256;
    const filename = target.split('/').pop() || (mode === 'delta' ? 'delta.zip' : 'setup.exe');
    const destPath = path.join(updateDir, filename);
    this._log('download_start', { mode, target_version: manifest.version, source: this._source });
    await this._setState('downloading');
    try {
      this._downloadedPath = await downloadWithResume(target, destPath, {
        timeoutMs: CFG.timeoutMs,
        sha256: sha || undefined,
        shouldCancel: () => this._cancel,
        onProgress: (p, s, d, t) => this._emitProgress(p, s, d, t),
      });
    } catch (err) {
      this._log('download_error', { mode, error: err.message, result: 'failed' });
      await this._setState('error');
      throw err;
    }
    this._log('download_complete', { mode, size: fs.statSync(destPath).size });
    await this._setState('ready');
    return { mode, path: destPath };
  }
}
```

同时追加两个辅助函数与默认实现（`module.exports` 前）：

```js
function defaultBaseDir() {
  if (electron && electron.app) {
    return path.dirname(electron.app.getPath('exe'));
  }
  return process.cwd();
}

function defaultCurrentVersion() {
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(__dirname, 'package.json'), 'utf-8'));
    return pkg.buildVersion || pkg.version || '0.0.0';
  } catch (_) { return '0.0.0'; }
}

function defaultFetcher() {
  return async (kind, url, opts = {}) => {
    const headers = { 'User-Agent': 'NEVO-Client/Updater' };
    if (kind === 'api') {
      headers.Accept = 'application/vnd.github+json';
    }
    if (opts.isMirror) url = proxyGithubUrl(url);
    if (kind === 'api') {
      const res = await httpGet(url, headers, opts.timeoutMs || CFG.timeoutMs);
      return new Promise((resolve, reject) => {
        if (res.statusCode !== 200) { res.resume(); return reject(new Error('HTTP ' + res.statusCode)); }
        let body = '';
        res.setEncoding('utf-8');
        res.on('data', (c) => { body += c; });
        res.on('end', () => {
          try { resolve(JSON.parse(body)); } catch (e) { reject(new Error('bad json')); }
        });
        res.on('error', reject);
      });
    }
    if (kind === 'manifest') {
      const res = await httpGet(url, headers, opts.timeoutMs || CFG.timeoutMs);
      return new Promise((resolve, reject) => {
        if (res.statusCode !== 200) { res.resume(); return reject(new Error('HTTP ' + res.statusCode)); }
        let body = '';
        res.setEncoding('utf-8');
        res.on('data', (c) => { body += c; });
        res.on('end', () => resolve(body));
        res.on('error', reject);
      });
    }
    throw new Error('unknown fetch kind ' + kind);
  };
}
```

更新 `module.exports`：追加 `UpdateEngine, defaultBaseDir, defaultCurrentVersion, defaultFetcher`。

- [ ] **Step 4: 运行确认通过**

Run: `node webclient/tests/test_updater.js`
Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add webclient/electron/updater.js webclient/tests/test_updater.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add UpdateEngine state machine with dual-source check"
```

---

### Task 6: 增量应用/回滚与全量安装/重启

**Files:**
- Modify: `webclient/electron/updater.js`

- [ ] **Step 1: 在 updater.js 的 UpdateEngine 类内追加方法（增量应用）**

在 `downloadUpdate()` 方法之后、类结束之前追加：

```js
  /** 增量模式：解压 delta 包并按清单替换文件；本进程退出后由辅助脚本完成替换。 */
  async applyDelta(deltaZipPath) {
    const updateDir = getUpdateDir(this.baseDir);
    const extracted = path.join(updateDir, 'extracted');
    fs.rmSync(extracted, { recursive: true, force: true });
    fs.mkdirSync(extracted, { recursive: true });

    // 解压 zip（纯 Node 实现，避免引入依赖）
    await extractZip(deltaZipPath, extracted);

    // 校验 zip 内 manifest 与 latest.json 一致
    const innerManifestPath = path.join(extracted, 'manifest.json');
    if (!fs.existsSync(innerManifestPath)) throw new Error('delta package missing manifest.json');
    const inner = parseManifest(fs.readFileSync(innerManifestPath, 'utf-8'));
    if (inner.version !== this._manifest.version) {
      throw new Error('delta manifest version mismatch');
    }
    this._log('apply_start', { mode: 'delta', target_version: inner.version });
    await this._setState('installing');

    const resourcesDir = getResourcesDir();
    const staged = path.join(updateDir, 'staged');
    fs.rmSync(staged, { recursive: true, force: true });
    fs.mkdirSync(staged, { recursive: true });

    const entries = [];
    for (const f of inner.files) {
      const rel = f.path.replace(/\\/g, '/');
      const src = path.join(extracted, rel);
      if (!fs.existsSync(src)) continue;
      const dst = path.join(staged, rel);
      fs.mkdirSync(path.dirname(dst), { recursive: true });
      fs.copyFileSync(src, dst);
      entries.push({ rel, sha256: f.sha256 });
    }
    if (entries.length === 0) throw new Error('delta package contains no files');

    // 生成替换脚本：等主进程退出 → 备份 → 替换 → 失败回滚 → 启动
    const pid = process.pid;
    const appExe = process.execPath;
    const cmdPath = path.join(updateDir, 'apply_update.cmd');
    const backupDir = path.join(updateDir, 'backup');
    const cmd = buildApplyCmd(pid, appExe, staged, resourcesDir, backupDir, entries);
    fs.writeFileSync(cmdPath, cmd, { encoding: 'utf-8' });
    this._stagedCmd = cmdPath;
    this._log('apply_ready', { mode: 'delta', file_count: entries.length });
    await this._setState('ready_to_install');
    return { cmdPath };
  }

  /** 全量模式：spawn NSIS Setup /S 静默安装；主进程随后退出。 */
  applyFull(setupExePath) {
    this._log('apply_start', { mode: 'full', target_version: this._manifest.version });
    const { spawn } = require('child_process');
    const child = spawn(setupExePath, ['/S'], { detached: true, stdio: 'ignore' });
    child.unref();
    this._log('apply_success', { mode: 'full' });
    if (electron && electron.app) electron.app.quit();
  }

  /** 用户点击"立即重启"：执行已准备的增量替换脚本并退出主进程。 */
  restartToApply() {
    if (this._stagedCmd) {
      this._log('restart', { mode: 'delta' });
      this._exec('cmd.exe', ['/c', this._stagedCmd]);
      if (electron && electron.app) electron.app.quit();
      return;
    }
    if (this._downloadedPath && this._mode === 'full') {
      this._log('restart', { mode: 'full' });
      this.applyFull(this._downloadedPath);
    }
  }
```

同时追加模块级辅助函数（`module.exports` 前）：

```js
// ============================================================
// 增量应用辅助
// ============================================================
function getResourcesDir() {
  if (electron && electron.app && electron.app.isPackaged) {
    return electron.app.getAppPath(); // packaged: <resources>/app.asar 所在目录
  }
  return path.join(__dirname, '..');
}

// 纯 Node zip 解压（仅支持无压缩/存储与 deflate 的 zip，足以满足发布产物）
function extractZip(zipPath, outDir) {
  const zlib = require('zlib');
  const buf = fs.readFileSync(zipPath);
  let offset = 0;
  const pending = [];
  while (offset + 30 <= buf.length) {
    // 局部文件头签名 0x04034b50
    if (buf.readUInt32LE(offset) !== 0x04034b50) break;
    const method = buf.readUInt16LE(offset + 8);
    const compSize = buf.readUInt32LE(offset + 18);
    const nameLen = buf.readUInt16LE(offset + 26);
    const extraLen = buf.readUInt16LE(offset + 28);
    const name = buf.toString('utf-8', offset + 30, offset + 30 + nameLen);
    const dataStart = offset + 30 + nameLen + extraLen;
    const data = buf.slice(dataStart, dataStart + compSize);
    offset = dataStart + compSize;
    if (/\/$/.test(name)) continue;
    const dest = path.join(outDir, name);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    if (method === 0) fs.writeFileSync(dest, data);
    else if (method === 8) fs.writeFileSync(dest, zlib.inflateRawSync(data));
    else throw new Error('unsupported zip method ' + method);
  }
  if (pending) pending.length = 0;
  return Promise.resolve();
}

// 生成增量替换脚本（等待主进程退出后逐文件备份+替换+回滚+启动）
function buildApplyCmd(pid, appExe, stagedDir, resourcesDir, backupDir, entries) {
  const lines = ['@echo off', 'chcp 65001 >nul', 'setlocal enabledelayedexpansion', ''];
  lines.push('rem wait for app to exit');
  lines.push(':wait_loop');
  lines.push(`tasklist /fi "PID eq ${pid}" 2>nul | findstr /I "${pid}" >nul`);
  lines.push('if !errorlevel! == 0 (');
  lines.push('  timeout /t 1 /nobreak >nul');
  lines.push('  goto wait_loop');
  lines.push(')');
  lines.push('timeout /t 1 /nobreak >nul');
  lines.push('set "STAGED=' + stagedDir.replace(/\//g, '\\') + '"');
  lines.push('set "RES=' + resourcesDir.replace(/\//g, '\\') + '"');
  lines.push('set "BACKUP=' + backupDir.replace(/\//g, '\\') + '"');
  lines.push('set "FAILED=0"');
  lines.push('mkdir "%BACKUP%" >nul 2>&1');
  for (const e of entries) {
    const rel = e.rel.replace(/\//g, '\\');
    lines.push('');
    lines.push(`if exist "%RES%\\${rel}" (`);
    lines.push(`  copy /y "%RES%\\${rel}" "%BACKUP%\\${rel}" >nul 2>&1`);
    lines.push(')');
    lines.push(`if not exist "%STAGED%\\${rel}" ( echo MISSING %STAGED%\\${rel} & set "FAILED=1" )`);
    lines.push(`xcopy /y /q "%STAGED%\\${rel}" "%RES%\\${rel}" >nul 2>&1`);
    lines.push(`if errorlevel 1 ( set "FAILED=1" )`);
  }
  lines.push('');
  lines.push('if !FAILED! == 1 (');
  lines.push('  echo rollback...');
  lines.push('  set "FAILED=0"');
  for (const e of entries) {
    const rel = e.rel.replace(/\//g, '\\');
    lines.push(`  if exist "%BACKUP%\\${rel}" ( copy /y "%BACKUP%\\${rel}" "%RES%\\${rel}" >nul 2>&1 )`);
    lines.push(`  if errorlevel 1 ( set "FAILED=1" )`);
  }
  lines.push('  echo Update failed, rollback done.');
  lines.push(') else (');
  lines.push(`  start "" "${appExe.replace(/\//g, '\\')}"`);
  lines.push(')');
  lines.push('endlocal');
  return lines.join('\r\n');
}
```

> 注：替换完成后用完整 exe 路径 `appExe` 启动应用；回滚失败时保持 FAILED 状态便于诊断。

- [ ] **Step 2: 手动审阅生成的脚本示例**

Run: `node -e "const U=require('./webclient/electron/updater.js'); console.log(U.buildApplyCmd(12345,'C:\\NEVO Web Client.exe','C:\\app\\.nevo_update\\staged','C:\\app\\resources','C:\\app\\.nevo_update\\backup',[{rel:'app.asar'},{rel:'nevo_gateway/_internal/js/app.js'}]))"`
Expected: 输出包含 `:wait_loop`、`tasklist /fi "PID eq 12345"`、`copy /y` 与两个 `start ""` 的完整 cmd 脚本。

- [ ] **Step 3: 提交**

```bash
git add webclient/electron/updater.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add delta apply, rollback and full install"
```

---

### Task 7: 发布辅助脚本 make_release.py

**Files:**
- Create: `webclient/tools/make_release.py`

- [ ] **Step 1: 实现脚本**

创建 `webclient/tools/make_release.py`：

```python
#!/usr/bin/env python3
"""NEVO Web Client 发布辅助：生成 latest.json 清单与增量 delta.zip。

用法:
  python make_release.py \
    --to BETA0.0.2 \
    --to-dir <新版本解包目录> \      # 例如 build/win-unpacked/resources
    --from-dir <旧版本解包目录> \    # 旧版本 resources 目录（可省略，省略则全量 files）
    --full-url https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/NEVO-Web-Client-BETA0.0.2-Setup.exe \
    --full-size 52428800 \
    --out build/release
"""
import argparse
import hashlib
import json
import os
import zipfile
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def walk_files(root: Path) -> dict:
    files = {}
    for p in sorted(root.rglob("*")):
        if p.is_file():
            rel = p.relative_to(root).as_posix()
            files[rel] = p
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--to", required=True, help="目标版本号，如 BETA0.0.2")
    ap.add_argument("--from-version", default="", help="上一版本号（用于 delta.from，如 BETA0.0.1）")
    ap.add_argument("--to-dir", required=True, help="新版本 resources 解包目录")
    ap.add_argument("--from-dir", default=None, help="旧版本 resources 解包目录（用于增量）")
    ap.add_argument("--full-url", required=True, help="全量安装包 GitHub 下载 URL")
    ap.add_argument("--full-size", type=int, required=True, help="全量安装包字节数")
    ap.add_argument("--full-sha256", default="", help="全量安装包 SHA256（打包时计算，必填以启用校验）")
    ap.add_argument("--changelog", default="", help="更新说明")
    ap.add_argument("--out", default="build/release", help="输出目录")
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    to_dir = Path(args.to_dir)
    if not to_dir.is_dir():
        raise SystemExit(f"--to-dir 不存在: {to_dir}")

    new_files = walk_files(to_dir)
    files_meta = []
    for rel, p in new_files.items():
        files_meta.append({"path": rel, "sha256": sha256(p), "size": p.stat().st_size})

    full_sha = args.full_sha256
    full_package = {
        "url": args.full_url,
        "size": args.full_size,
        "sha256": full_sha,
    }

    delta = None
    if args.from_dir:
        from_dir = Path(args.from_dir)
        old_files = walk_files(from_dir)
        changed = {
            rel: p for rel, p in new_files.items()
            if rel not in old_files
            or sha256(old_files[rel]) != sha256(p)
        }
        delta_files = sorted(changed)
        manifest = {
            "version": args.to,
            "files": files_meta,
            "full_package": full_package,
        }
        # delta zip：仅含差异文件 + manifest.json
        delta_name = f"NEVO-delta-{args.from_version or 'prev'}-{args.to}.zip"
        delta_zip = out_dir / delta_name
        with zipfile.ZipFile(delta_zip, "w", zipfile.ZIP_DEFLATED) as z:
            for rel in delta_files:
                z.write(to_dir / rel, rel)
            z.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
        delta = {
            "from": args.from_version or "",
            "url": f"https://github.com/TNEllya/NEVO/releases/download/{args.to}/{delta_zip.name}",
            "size": delta_zip.stat().st_size,
            "sha256": sha256(delta_zip),
        }

    latest = {
        "version": args.to,
        "published_at": datetime.now(timezone.utc).isoformat(),
        "changelog": args.changelog,
        "files": files_meta,
        "full_package": full_package,
        "delta": delta,
    }
    latest_path = out_dir / "latest.json"
    latest_path.write_text(json.dumps(latest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"latest.json -> {latest_path}")
    print(f"files: {len(files_meta)}, delta files: {len(delta_files) if delta_files else 0}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: 冒烟验证（生成最小测试产物）**

Run:
```powershell
New-Item -ItemType Directory -Force -Path build/release, build/old_res, build/new_res | Out-Null
Set-Content build/old_res/a.txt "old-a"
Set-Content build/new_res/a.txt "new-a"
Set-Content build/new_res/b.txt "new-b"
python webclient/tools/make_release.py --to BETA0.0.2 --from-version BETA0.0.1 --to-dir build/new_res --from-dir build/old_res --full-url https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/NEVO-Setup.exe --full-size 1000 --full-sha256 0000000000000000000000000000000000000000000000000000000000000000 --out build/release
Get-Content build/release/latest.json
```
Expected: latest.json 含 files 2 项、delta 项；delta zip 内仅含 a.txt（差异）与 manifest.json。

- [ ] **Step 3: 提交**

```bash
git add webclient/tools/make_release.py
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add release manifest generator"
```

---

### Task 8: 主进程与 preload 集成

**Files:**
- Modify: `webclient/electron/main.js`
- Modify: `webclient/electron/preload.js`

- [ ] **Step 1: preload.js 暴露 updaterAPI**

将 `webclient/electron/preload.js` 修改为：

```js
const { contextBridge, ipcRenderer } = require('electron');

// Expose safe window-control APIs to the renderer process
contextBridge.exposeInMainWorld('electronAPI', {
  minimizeWindow: () => ipcRenderer.send('window-minimize'),
  maximizeWindow: () => ipcRenderer.send('window-maximize'),
  closeWindow: () => ipcRenderer.send('window-close'),
  onMaximizedChange: (callback) => {
    ipcRenderer.on('window-is-maximized', (_event, value) => callback(value));
  },
});

// Online updater API
contextBridge.exposeInMainWorld('updaterAPI', {
  checkNow: () => ipcRenderer.invoke('updater:check'),
  download: () => ipcRenderer.invoke('updater:download'),
  restartToApply: () => ipcRenderer.invoke('updater:restart'),
  getStatus: () => ipcRenderer.invoke('updater:status'),
  getLog: () => ipcRenderer.invoke('updater:log'),
  onState: (callback) => {
    const listener = (_event, data) => callback(data);
    ipcRenderer.on('updater:state', listener);
    return () => ipcRenderer.removeListener('updater:state', listener);
  },
  onProgress: (callback) => {
    const listener = (_event, data) => callback(data);
    ipcRenderer.on('updater:progress', listener);
    return () => ipcRenderer.removeListener('updater:progress', listener);
  },
});
```

- [ ] **Step 2: main.js 初始化 updater 并注册 IPC**

在 `main.js` 顶部（`const fs = require('fs');` 之后）添加：

```js
const updater = require('./updater.js');
```

在 `createWindow()` 的 `mainWindow.loadURL(GATEWAY_URL);` 之后添加：

```js
  // 在线更新：定时检测 + 注册 IPC
  startUpdaterService(mainWindow);
```

在文件末尾（`process.on('exit', ...)` 之后）追加：

```js
// ============================================================
// Online updater service
// ============================================================
const updateEngine = new updater.UpdateEngine();

function startUpdaterService(win) {
  const send = (channel, data) => {
    if (win && !win.isDestroyed()) win.webContents.send(channel, data);
  };
  updateEngine.onState((oldState, newState) => send('updater:state', { state: newState }));
  updateEngine.onProgress((percent, speed, downloaded, total) => {
    send('updater:progress', { percent, speed, downloaded, total });
  });
  send('updater:state', { state: updateEngine.state, currentVersion: updateEngine.currentVersion });

  ipcMain.handle('updater:check', async () => {
    try { return { ok: true, info: await updateEngine.checkForUpdates() }; }
    catch (e) { return { ok: false, error: e.message }; }
  });
  ipcMain.handle('updater:download', async () => {
    try { return { ok: true, result: await updateEngine.downloadUpdate() }; }
    catch (e) { return { ok: false, error: e.message }; }
  });
  ipcMain.handle('updater:restart', () => { updateEngine.restartToApply(); return { ok: true }; });
  ipcMain.handle('updater:status', () => ({
    state: updateEngine.state,
    currentVersion: updateEngine.currentVersion,
    info: updateEngine._manifest ? { version: updateEngine._manifest.version, mode: updateEngine._mode } : null,
  }));
  ipcMain.handle('updater:log', () => updater.readUpdateLog(updateEngine.baseDir));

  // 定时检测（首次 30s 后，之后每小时）
  setTimeout(() => {
    updateEngine.checkForUpdates().catch(() => {});
  }, 30000);
  setInterval(() => {
    updateEngine.checkForUpdates().catch(() => {});
  }, updater.CFG.checkIntervalMs);
}
```

- [ ] **Step 3: 语法检查**

Run: `node --check webclient/electron/main.js; node --check webclient/electron/preload.js`
Expected: 无输出（语法通过）。

- [ ] **Step 4: 提交**

```bash
git add webclient/electron/main.js webclient/electron/preload.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: wire updater into main process and preload"
```

---

### Task 9: 设置页 UI（index.html + app.js + theme.css）

**Files:**
- Modify: `webclient/index.html`
- Modify: `webclient/js/app.js`
- Modify: `webclient/css/theme.css`

- [ ] **Step 1: index.html 设置页新增"关于与更新"区块**

在设置页 `关于` section（`data-i18n="关于"` 的 `.settings-section`）之后追加：

```html
      <div class="settings-section">
        <div class="settings-section-title" data-i18n="软件更新">软件更新</div>
        <div class="settings-row">
          <div>
            <div class="sr-label" data-i18n="当前版本">当前版本</div>
            <div class="sr-desc mono" id="upd-current-version">—</div>
          </div>
          <button class="nevo-btn nevo-btn-sm" id="btn-check-update" data-i18n="检查更新">检查更新</button>
        </div>
        <div class="settings-row" id="upd-status-row" style="display:none;">
          <div>
            <div class="sr-label" data-i18n="更新状态">更新状态</div>
            <div class="sr-desc" id="upd-status-text">—</div>
          </div>
        </div>
        <div class="settings-row" id="upd-progress-row" style="display:none;">
          <div class="upd-progress">
            <div class="upd-progress-bar" id="upd-progress-bar"></div>
          </div>
          <span class="upd-progress-info" id="upd-progress-info"></span>
        </div>
        <div class="settings-row">
          <div>
            <div class="sr-label" data-i18n="自动检测">自动检测</div>
            <div class="sr-desc" data-i18n="每小时自动检查新版本">每小时自动检查新版本</div>
          </div>
          <div class="toggle on" data-setting="auto_check_update"></div>
        </div>
        <div class="settings-row">
          <button class="nevo-btn nevo-btn-sm" id="btn-view-update-log" data-i18n="查看更新日志">查看更新日志</button>
        </div>
      </div>
```

同步修改版本号资源链接 `?v=6` → `?v=7`（i18n.js / media.js / app.js / theme.css 共 4 处）。

- [ ] **Step 2: app.js 增加更新逻辑**

在 `app.js` 的 `initEventListeners()` 内（文件上传事件之后）追加：

```js
    // ---- Online updater UI ----
    const upd = window.updaterAPI;
    const updStateEl = $('upd-status-text');
    const updStatusRow = $('upd-status-row');
    const updProgressRow = $('upd-progress-row');
    const updBar = $('upd-progress-bar');
    const updInfo = $('upd-progress-info');
    const updVersionEl = $('upd-current-version');

    function updSetStatus(text) {
      if (!updStatusRow) return;
      updStatusRow.style.display = 'flex';
      if (updStateEl) updStateEl.textContent = text;
    }
    function updShowProgress(pct, speed, downloaded, total) {
      if (!updProgressRow) return;
      updProgressRow.style.display = 'flex';
      if (updBar) updBar.style.width = (pct || 0) + '%';
      if (updInfo) {
        const speedStr = speed ? (speed / 1024 / 1024).toFixed(1) + ' MB/s' : '';
        updInfo.textContent = `${Math.round(pct || 0)}%  ${speedStr}`;
      }
    }
    function updApplyToUI(data) {
      if (!updVersionEl && upd) {
        upd.getStatus().then((st) => {
          if (updVersionEl && st && st.currentVersion) updVersionEl.textContent = st.currentVersion;
        });
      } else if (updVersionEl && data && data.currentVersion) {
        updVersionEl.textContent = data.currentVersion;
      }
    }

    if (upd) {
      $('btn-check-update').addEventListener('click', async () => {
        $('btn-check-update').disabled = true;
        updSetStatus(t('正在检查更新...'));
        const res = await upd.checkNow();
        $('btn-check-update').disabled = false;
        if (res.ok && res.info) {
          updSetStatus(`${t('发现新版本')} ${res.info.version}（${res.info.mode === 'delta' ? t('增量') : t('全量')}）`);
          const dl = await upd.download();
          if (dl.ok) {
            updSetStatus(t('下载完成，是否立即重启应用？'));
            if (confirm(t('新版本已就绪，是否立即重启应用？'))) {
              await upd.restartToApply();
            } else {
              updSetStatus(t('已暂存，将在下次启动时应用'));
            }
          } else {
            updSetStatus(t('下载失败') + ': ' + (dl.error || ''));
          }
        } else if (res.ok) {
          updSetStatus(t('当前已是最新版本'));
        } else {
          updSetStatus(t('检查更新失败') + ': ' + (res.error || ''));
        }
      });

      upd.onState((data) => {
        if (data.state === 'downloading') updSetStatus(t('正在下载更新...'));
        if (data.state === 'ready') updSetStatus(t('下载完成，等待重启应用'));
        if (data.state === 'error') updSetStatus(t('更新出错'));
        updApplyToUI(data);
      });
      upd.onProgress((data) => updShowProgress(data.percent, data.speed, data.downloaded, data.total));

      $('btn-view-update-log').addEventListener('click', async () => {
        const log = await upd.getLog();
        const lines = (log || []).slice(-50).map((e) =>
          `${e.timestamp} [${e.event}] ver=${e.target_version || e.current_version || ''} src=${e.source || ''} result=${e.result || ''} ${e.error || ''}`
        ).join('\n');
        alert(t('更新日志') + '\n' + (lines || t('暂无日志')));
      });
    }
```

在 `applyStoredSettingsToUI()` 的 toggles 恢复逻辑后追加（默认开启自动检测开关）：

```js
    // 默认开启自动检测（若未设置过）
    const autoCheck = document.querySelector('.toggle[data-setting="auto_check_update"]');
    if (autoCheck && getSetting('auto_check_update', null) === null) {
      autoCheck.classList.add('on');
      saveSetting('auto_check_update', true);
    }
```

- [ ] **Step 3: theme.css 增加更新进度条样式**

在 `theme.css` 末尾追加：

```css
/* Online updater */
.upd-progress {
  flex: 1;
  height: 8px;
  border-radius: 4px;
  background: var(--color-border);
  overflow: hidden;
}
.upd-progress-bar {
  height: 100%;
  width: 0%;
  border-radius: 4px;
  background: var(--color-primary);
  transition: width 0.2s ease;
}
.upd-progress-info {
  font-size: var(--text-xs);
  color: var(--color-text-muted);
  min-width: 90px;
  text-align: right;
  font-family: var(--font-mono);
}
```

- [ ] **Step 4: 语法与结构检查**

Run: `node --check webclient/js/app.js`
Expected: 无输出。再人工核对 index.html 新区块 id 与 app.js 引用一致。

- [ ] **Step 5: 提交**

```bash
git add webclient/index.html webclient/js/app.js webclient/css/theme.css
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "feat: add updater UI in settings page"
```

---

### Task 10: 集成测试（本地 mock HTTP + 端到端）

**Files:**
- Create: `webclient/tests/test_updater_e2e.js`

- [ ] **Step 1: 实现集成测试**

创建 `webclient/tests/test_updater_e2e.js`：

```js
'use strict';
const assert = require('assert');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const U = require('../electron/updater.js');

let pass = 0, fail = 0;
function t(cond, msg) { if (cond) { pass++; } else { fail++; console.error('  FAIL:', msg); } }

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nevo-e2e-'));
  // 构造 mock 文件内容与清单
  const fileA = Buffer.from('hello delta world', 'utf-8');
  const fileASha = require('crypto').createHash('sha256').update(fileA).digest('hex');
  const manifest = {
    version: 'BETA0.0.2',
    files: [
      { path: 'nevo_gateway/_internal/js/app.js', sha256: fileASha, size: fileA.length },
    ],
    full_package: { url: 'http://127.0.0.1:0/setup.exe', size: 100000, sha256: '' },
    delta: {
      from: 'BETA0.0.1',
      url: 'http://127.0.0.1:0/delta.zip',
      size: 100,
      sha256: '',
    },
  };

  const server = http.createServer((req, res) => {
    if (req.url === '/manifest.json') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(manifest));
      return;
    }
    if (req.url === '/delta.zip') {
      // 构造 zip（store 方式：local header + data）
      const name = Buffer.from('nevo_gateway/_internal/js/app.js');
      const nameLen = name.length;
      const header = Buffer.alloc(30);
      header.writeUInt32LE(0x04034b50, 0); // signature
      header.writeUInt16LE(0, 8);          // method store
      header.writeUInt32LE(fileA.length, 18);
      header.writeUInt16LE(nameLen, 26);
      const body = Buffer.concat([header, name, fileA]);
      const localHeader = Buffer.alloc(22);
      localHeader.writeUInt32LE(0x06054b50, 0); // EOCD
      res.writeHead(200, { 'Content-Type': 'application/zip' });
      res.end(Buffer.concat([body, localHeader]));
      return;
    }
    if (req.url.startsWith('/setup.exe')) {
      res.writeHead(200, { 'Content-Length': '100' });
      res.end(Buffer.alloc(100, 1));
      return;
    }
    res.writeHead(404); res.end();
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;
  const base = `http://127.0.0.1:${port}`;

  // 修正清单 URL 指向本地
  manifest.delta.url = `${base}/delta.zip`;
  manifest.full.url = `${base}/setup.exe`;

  // --- 断点续传下载 ---
  const dlDest = path.join(tmp, 'out.zip');
  // 先直接下载一次（模拟完整），再测 .part 续传：
  // 首次写部分 .part 文件模拟中断
  fs.writeFileSync(dlDest + '.part', fileA.slice(0, 6));
  const done = await U.downloadWithResume(`${base}/delta.zip`, dlDest, { retries: 0 });
  t(done === dlDest, 'resume download completes');
  const got = fs.readFileSync(dlDest);
  t(got.length === fileA.length && got.toString() === fileA.toString(), 'resumed file content matches');

  // --- sha256 校验失败重试 ---
  let verifyFails = 0;
  const badSha = '0000000000000000000000000000000000000000000000000000000000000000';
  let verifyErr = null;
  try {
    await U.downloadWithResume(`${base}/delta.zip`, path.join(tmp, 'v.zip'), {
      retries: 1, sha256: badSha, timeoutMs: 2000,
      shouldCancel: () => false,
    });
  } catch (e) { verifyErr = e; }
  t(!!verifyErr && /sha256/.test(verifyErr.message), 'sha256 mismatch throws');

  // --- UpdateEngine 全流程（注入本地 fetcher） ---
  const engine = new U.UpdateEngine({
    baseDir: path.join(tmp, 'install'),
    currentVersion: 'BETA0.0.1',
    fetcher: async (kind, url) => {
      if (kind === 'api') {
        return { assets: [{ name: 'latest.json', browser_download_url: `${base}/manifest.json` }] };
      }
      if (kind === 'manifest') {
        const res = await U.httpGet(url, {}, 2000);
        return new Promise((resolve, reject) => {
          let b = ''; res.setEncoding('utf-8');
          res.on('data', (c) => { b += c; });
          res.on('end', () => resolve(b));
          res.on('error', reject);
        });
      }
      throw new Error('bad kind');
    },
  });
  const info = await engine.checkForUpdates();
  t(info && info.mode === 'delta' && info.source === 'github', 'e2e check -> delta github');
  const dlRes = await engine.downloadUpdate();
  t(dlRes.mode === 'delta' && fs.existsSync(dlRes.path), 'e2e download delta');

  // --- 增量应用 ---
  const applyRes = await engine.applyDelta(dlRes.path);
  t(applyRes && applyRes.cmdPath && fs.existsSync(applyRes.cmdPath), 'apply delta generates cmd');
  const cmdText = fs.readFileSync(applyRes.cmdPath, 'utf-8');
  t(cmdText.includes('app.asar') === false && cmdText.includes('_internal'), 'cmd contains staged replace lines');

  // --- 日志 ---
  const log = U.readUpdateLog(engine.baseDir);
  t(log.some((e) => e.event === 'check_ok'), 'log has check_ok');
  t(log.some((e) => e.event === 'download_complete'), 'log has download_complete');

  server.close();
  console.log(`\nResult: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
```

- [ ] **Step 2: 运行确认通过**

Run: `node webclient/tests/test_updater_e2e.js`
Expected: `Result: N passed, 0 failed`。若 `extractZip` 对"store + EOCD 尾部"解析有偏差，按测试输出修正 extractZip 的 EOCD 处理（确保循环在读到 EOCD 签名 0x06054b50 时停止）。

- [ ] **Step 3: 提交**

```bash
git add webclient/tests/test_updater_e2e.js
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "test: add updater e2e integration test"
```

---

### Task 11: 运行全部测试 + 同步打包产物到 test/V3

**Files:**
- Modify: `test/V3/resources/nevo_gateway/_internal/index.html` 等（打包同步）

- [ ] **Step 1: 运行全部测试**

Run: `node webclient/tests/test_updater.js; node webclient/tests/test_updater_e2e.js`
Expected: 两组测试全部 PASS。

- [ ] **Step 2: 同步前端源码到 test/V3（打包产物）**

复制以下文件到 `test/V3/resources/nevo_gateway/_internal/`：
- `index.html`（含 `?v=7` 与更新区块）
- `js/app.js`
- `js/media.js`
- `css/theme.css`
- `js/i18n.js`（若本轮未改，跳过）

Run（示例）：
```powershell
$dst="C:\Users\yzd20\Desktop\Project\NEVO\test\V3\resources\nevo_gateway\_internal"
Copy-Item webclient\index.html $dst\index.html -Force
Copy-Item webclient\js\app.js $dst\js\app.js -Force
Copy-Item webclient\css\theme.css $dst\css\theme.css -Force
```

- [ ] **Step 3: 记录手动验收步骤（不在此轮自动执行）**

1. 打包新版本：`npm run build`（`webclient/electron/`），`buildVersion` 提升为 `BETA0.0.2`；
2. 用 `make_release.py` 生成 `latest.json` + delta.zip，上传至 GitHub Release；
3. 旧版本客户端启动 → 设置页点击"检查更新" → 出现进度 → 弹窗重启 → 应用新版本；
4. 断网/主源超时 → 自动切换镜像源（观察日志 `switch_mirror`）；
5. 检查 `<安装目录>/.nevo_update/update_log.json`。

- [ ] **Step 4: 提交**

```bash
git add test/V3/resources/nevo_gateway/_internal webclient
git -c user.name=sk1rkevin -c user.email=2247936864@qq.com commit -m "chore: sync updater UI to V3 build output"
```

---

## 自审记录

- **Spec 覆盖**：双源与 5s 超时（Task 2/5）、增量/全量自适应（Task 2 决策 + Task 6 应用）、断点续传（Task 4）、进度显示（Task 4 回调 + Task 9 UI）、异常处理（重试/校验/回滚，Task 4/6）、重启（Task 6）、更新日志（Task 3/5）、发布脚本（Task 7）、集成测试（Task 10）。
- **类型一致性**：`UpdateEngine` 的 `checkForUpdates/downloadUpdate/applyDelta/restartToApply` 与 preload/main 的 IPC 命名（`updater:check/download/restart/status/log/state/progress`）在 Task 8/9 保持一致；`parseManifest` 返回的 `full/delta` 结构与 Task 2 测试一致。
