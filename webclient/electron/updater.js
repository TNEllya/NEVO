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
  mirrorPrefixes: ['https://ghproxy.com/', 'https://ghfast.top/', 'https://gh-proxy.com/'],
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
    this._busy = false;
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
    if (this._busy) throw new Error('check already in progress');
    this._busy = true;
    try {
      this._cancel = false;
      this._log('check_start', {});
      await this._setState('checking');
      const errors = [];

      // 1) 主源 API（404 = 无 release → 无更新）
      let assetUrl = null;
      let tagName = '';
      try {
        const apiData = await this._fetch('api', githubApiLatestUrl(), { timeoutMs: CFG.timeoutMs });
        const asset = (apiData.assets || []).find((a) => a.name === CFG.assetName);
        tagName = apiData.tag_name || '';
        assetUrl = asset ? asset.browser_download_url : null;
        if (!assetUrl) {
          this._log('no_update', { target_version: (tagName || '').replace(/^[vV]/, ''), reason: 'asset_missing' });
          await this._setState('idle');
          return null;
        }
      } catch (err) {
        if (err.message === 'HTTP 404') {
          this._log('no_update', { reason: 'no_release' });
          await this._setState('idle');
          return null;
        }
        errors.push(`github api: ${err.message}`);
        // 主源 API 失败：尝试镜像 API
        for (let i = 0; i < CFG.mirrorPrefixes.length; i++) {
          const prefix = CFG.mirrorPrefixes[i];
          try {
            const apiData = await this._fetch('api', githubApiLatestUrl(), { timeoutMs: CFG.timeoutMs, isMirror: true, mirrorPrefix: prefix });
            const asset = (apiData.assets || []).find((a) => a.name === CFG.assetName);
            tagName = apiData.tag_name || '';
            assetUrl = asset ? asset.browser_download_url : null;
            if (assetUrl) break;
          } catch (e) {
            errors.push(`mirror api: ${e.message}`);
          }
        }
        if (!assetUrl) {
          this._log('check_error', { error: errors.join(' | '), result: 'failed' });
          await this._setState('error');
          throw new Error(errors.join(' | '));
        }
      }

      // 2) 多源清单下载：主源直连 + 各镜像，复用同一 assetUrl
      const { text, source } = await this._fetchManifestMulti(assetUrl, errors);
      const manifest = parseManifest(text);
      this._manifest = manifest;
      this._source = source;
      if (!isNewerVersion(manifest.version, this.currentVersion)) {
        this._log('no_update', { target_version: manifest.version });
        await this._setState('idle');
        return null;
      }
      this._mode = decideMode(manifest, this.currentVersion);
      this._log('check_ok', { target_version: manifest.version, source, mode: this._mode });
      await this._setState('download_available');
      return { version: manifest.version, mode: this._mode, source, manifest, changelog: manifest.changelog };
    } finally {
      this._busy = false;
    }
  }

  /** 依次从主源与各镜像下载清单，首个成功即返回。 */
  async _fetchManifestMulti(assetUrl, errors) {
    const candidates = [
      { name: 'github', url: assetUrl },
    ];
    for (let i = 0; i < CFG.mirrorPrefixes.length; i++) {
      candidates.push({ name: `mirror${i + 1}`, url: proxyGithubUrl(assetUrl, CFG.mirrorPrefixes[i]) });
    }
    for (const c of candidates) {
      try {
        const text = await this._fetch('manifest', c.url, { timeoutMs: CFG.timeoutMs });
        return { text, source: c.name };
      } catch (err) {
        errors.push(`${c.name}: ${err.message}`);
      }
    }
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
}

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

module.exports = {
  CFG, parseVersion, isNewerVersion,
  githubApiLatestUrl, proxyGithubUrl, parseManifest, decideMode,
  getUpdateDir, getLogPath, logUpdateEvent, readUpdateLog,
  computeRetryDelays, sleep, httpGet, sha256File, downloadWithResume,
  UpdateEngine, defaultBaseDir, defaultCurrentVersion, defaultFetcher,
  getResourcesDir, extractZip, buildApplyCmd,
};
