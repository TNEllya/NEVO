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

module.exports = {
  CFG, parseVersion, isNewerVersion,
  githubApiLatestUrl, proxyGithubUrl, parseManifest, decideMode,
  getUpdateDir, getLogPath, logUpdateEvent, readUpdateLog,
  computeRetryDelays, sleep, httpGet, sha256File, downloadWithResume,
  UpdateEngine, defaultBaseDir, defaultCurrentVersion, defaultFetcher,
};
