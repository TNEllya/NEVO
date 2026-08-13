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
  zipMaxEntries: 5000,                    // 解压条目数上限（zip 炸弹防护）
  zipMaxTotalBytes: 512 * 1024 * 1024,    // 解压总字节上限 512MB（zip 炸弹防护）
  requireSignedManifest: true,            // 强制清单签名（可被 package.json updater 配置覆盖）
};

// package.json 中的 updater 配置（如 requireSignedManifest），缺省保持安全默认值
try {
  const pkgJson = JSON.parse(fs.readFileSync(path.join(__dirname, 'package.json'), 'utf-8'));
  const updaterCfg = (pkgJson && pkgJson.updater) || {};
  if (typeof updaterCfg.requireSignedManifest === 'boolean') {
    CFG.requireSignedManifest = updaterCfg.requireSignedManifest;
  }
} catch (_) { /* 使用内置默认值 */ }

// ============================================================
// 清单签名（Ed25519，供应链防篡改）
// ============================================================
// Ed25519 SPKI DER 前缀：与 32 字节原始公钥拼接后构造 crypto KeyObject
const ED25519_SPKI_PREFIX = Buffer.from('302a300506032b6570032100', 'hex');
// 发布清单验证公钥（32 字节原始公钥，hex 编码）。
// 安全警告：当前为开发/测试公钥。生产发布前必须替换为正式发布密钥对的公钥，
// 对应私钥仅由发布流水线持有（环境变量 NEVO_RELEASE_KEY_HEX，64 字符 hex 种子），
// 严禁将私钥写入仓库或任何客户端代码。
let PUBLIC_KEY_HEX = '2b9c2782c3016a9e4bfedf75b1648fc6e54686a4b2529130eb946789d93dd6fc';

let _pubKeyObject = null;
function publicKeyObject() {
  if (!_pubKeyObject) {
    _pubKeyObject = crypto.createPublicKey({
      key: Buffer.concat([ED25519_SPKI_PREFIX, Buffer.from(PUBLIC_KEY_HEX, 'hex')]),
      format: 'der',
      type: 'spki',
    });
  }
  return _pubKeyObject;
}

/**
 * 注入/更换清单验证公钥（32 字节原始公钥，hex）。
 * 生产环境由发布配置注入正式公钥；测试环境注入运行时生成的测试公钥，
 * 从而仓库中不保存任何私钥材料。
 */
function setPublicKey(hex) {
  if (typeof hex !== 'string' || !/^[0-9a-f]{64}$/i.test(hex)) {
    throw new Error('Invalid Ed25519 public key hex (expected 64 hex chars)');
  }
  PUBLIC_KEY_HEX = hex.toLowerCase();
  _pubKeyObject = null;
}

/** 规范化字节：清单对象去除 signature 字段后的紧凑 JSON 序列化（键序与发布端 make_release.py 一致）。 */
function canonicalManifestBytes(parsed) {
  const copy = {};
  for (const k of Object.keys(parsed)) {
    if (k === 'signature') continue;
    copy[k] = parsed[k];
  }
  return Buffer.from(JSON.stringify(copy), 'utf-8');
}

/**
 * 验证清单 Ed25519 签名。message = sha256(去除 signature 字段后的规范化字节)。
 * sigHex 必须为 128 字符 hex（Ed25519 签名 64 字节）。
 */
function verifyManifestSignature(text, sigHex) {
  if (typeof sigHex !== 'string' || !/^[0-9a-f]{128}$/i.test(sigHex)) return false;
  let parsed;
  try { parsed = JSON.parse(text); } catch (_) { return false; }
  const msg = crypto.createHash('sha256').update(canonicalManifestBytes(parsed)).digest();
  return crypto.verify(null, msg, publicKeyObject(), Buffer.from(sigHex, 'hex'));
}

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
  if (/^https:\/\/(ghproxy|gh-proxy|ghfast|gh\.proxy)/.test(url)) return url;
  if (/^https:\/\/(github\.com|objects\.githubusercontent\.com|api\.github\.com)/.test(url)) {
    return p + url;
  }
  return url;
}

// ============================================================
// 清单解析
// ============================================================
/**
 * 相对路径安全性校验：必须是纯相对路径，拒绝空段、'.'/'..'、盘符前缀、
 * 反斜杠分隔符（统一按 '/' 处理）等一切可能逃逸目标目录的写法。
 */
function isSafeRelPath(rel) {
  if (typeof rel !== 'string' || rel.length === 0) return false;
  const parts = rel.replace(/\\/g, '/').split('/');
  if (parts.some((p) => p === '' || p === '.' || p === '..')) return false;
  if (/^[A-Za-z]:/.test(parts[0])) return false;
  return true;
}

const SHA256_HEX_RE = /^[0-9a-f]{64}$/i;

/**
 * 解析并校验更新清单。
 * opts.requireSignature：缺省取 CFG.requireSignedManifest（package.json 可配置，默认 true）。
 * 安全规则：
 *  - 含 signature 字段 → 必须通过 Ed25519 验证，失败即抛错；
 *  - 无 signature 且 requireSignature → 拒绝；
 *  - full/delta 条目缺 sha256（或非 64 位 hex）→ 拒绝（下载后必须能校验完整性）；
 *  - files 条目缺 path/sha256 或路径不安全 → 拒绝。
 */
function parseManifest(text, opts = {}) {
  const requireSig = opts.requireSignature === undefined
    ? CFG.requireSignedManifest
    : opts.requireSignature;
  let data;
  try { data = JSON.parse(text); } catch (_) { throw new Error('Invalid manifest: bad json'); }
  if (!data || typeof data !== 'object' || typeof data.version !== 'string') {
    throw new Error('Invalid manifest: version missing');
  }
  if (typeof data.signature === 'string') {
    if (!verifyManifestSignature(text, data.signature)) {
      throw new Error('Invalid manifest: signature verification failed');
    }
  } else if (requireSig) {
    throw new Error('Invalid manifest: signature missing');
  }
  if (!data.full_package || typeof data.full_package.url !== 'string') {
    throw new Error('Invalid manifest: full_package.url missing');
  }
  if (typeof data.full_package.sha256 !== 'string' || !SHA256_HEX_RE.test(data.full_package.sha256)) {
    throw new Error('Invalid manifest: full_package.sha256 missing or invalid');
  }
  if (!(Number.isFinite(data.full_package.size) && data.full_package.size > 0)) {
    throw new Error('Invalid manifest: full_package.size missing or invalid');
  }
  if (data.delta) {
    if (!data.delta.url || typeof data.delta.url !== 'string') {
      throw new Error('Invalid manifest: delta.url missing');
    }
    if (typeof data.delta.sha256 !== 'string' || !SHA256_HEX_RE.test(data.delta.sha256)) {
      throw new Error('Invalid manifest: delta.sha256 missing or invalid');
    }
    if (!(Number.isFinite(data.delta.size) && data.delta.size > 0)) {
      throw new Error('Invalid manifest: delta.size missing or invalid');
    }
  }
  const files = Array.isArray(data.files) ? data.files : [];
  for (const f of files) {
    if (!f || typeof f.path !== 'string' || !isSafeRelPath(f.path)) {
      throw new Error('Invalid manifest: file entry path missing or unsafe');
    }
    if (typeof f.sha256 !== 'string' || !SHA256_HEX_RE.test(f.sha256)) {
      throw new Error('Invalid manifest: file entry sha256 missing or invalid: ' + f.path);
    }
  }
  return {
    version: data.version,
    changelog: data.changelog || '',
    files,
    full: {
      url: data.full_package.url,
      size: data.full_package.size,
      sha256: data.full_package.sha256,
    },
    delta: data.delta
      ? { from: data.delta.from || '', url: data.delta.url, size: data.delta.size, sha256: data.delta.sha256 }
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
 * opts: { onProgress(percent,speed,downloaded,total), shouldCancel(), retries, timeoutMs, sha256, expectedSize }
 */
async function downloadWithResume(url, destPath, opts = {}) {
  const { onProgress, shouldCancel, sha256, expectedSize } = opts;
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
    // 完整性校验：sha256 必须与清单一致（强制），字节数必须与清单 size 一致；
    // 任一失败即删除产物（含残留 .part）并报错。
    if (sha256) {
      const actual = sha256File(destPath);
      if (actual !== sha256) {
        fs.unlinkSync(destPath);
        try { fs.unlinkSync(partPath); } catch (_) {}
        if (attempt < retries) { await sleep(delays[attempt]); continue; }
        throw new Error('sha256 mismatch');
      }
    }
    if (expectedSize && fs.statSync(destPath).size !== expectedSize) {
      fs.unlinkSync(destPath);
      try { fs.unlinkSync(partPath); } catch (_) {}
      if (attempt < retries) { await sleep(delays[attempt]); continue; }
      throw new Error('size mismatch');
    }
    return destPath;
  }
  throw new Error('unreachable');
}

/**
 * 多线路断点续传：按 urls 顺序尝试，统一使用 destPath + '.part'，
 * 线路失败自动切换下一线路；全部线路失败后按 retries 退避重试。
 * opts: { onProgress, shouldCancel, sha256, expectedSize, retries, timeoutMs, onFailover(urlIndex, url) }
 */
async function downloadWithRoutes(urls, destPath, opts = {}) {
  const { onProgress, shouldCancel, sha256, expectedSize, onFailover } = opts;
  const retries = opts.retries === undefined ? CFG.maxRetries : opts.retries;
  const timeoutMs = opts.timeoutMs === undefined ? CFG.timeoutMs : opts.timeoutMs;
  const partPath = destPath + '.part';
  const delays = computeRetryDelays(retries, CFG.retryDelaysMs);

  for (let attempt = 0; attempt <= retries; attempt++) {
    if (shouldCancel && shouldCancel()) throw new Error('cancelled');
    let existing = 0;
    try { existing = fs.existsSync(partPath) ? fs.statSync(partPath).size : 0; } catch (_) {}
    let lastErr = null;
    let lastUrl = null;

    for (let u = 0; u < urls.length; u++) {
      const url = urls[u];
      const headers = {};
      if (existing > 0) headers.Range = `bytes=${existing}-`;
      let res;
      try {
        res = await httpGet(url, headers, timeoutMs);
      } catch (err) {
        lastErr = err;
        lastUrl = url;
        if (u < urls.length - 1) { if (onFailover) onFailover(u, url); continue; }
        break;
      }
      if (res.statusCode === 416) {
        res.resume();
        fs.unlinkSync(partPath);
        existing = 0;
        lastUrl = url;
        if (u < urls.length - 1) { if (onFailover) onFailover(u, url); continue; }
        break;
      }
      if (res.statusCode !== 200 && res.statusCode !== 206) {
        res.resume();
        lastErr = new Error('HTTP ' + res.statusCode);
        lastUrl = url;
        if (u < urls.length - 1) { if (onFailover) onFailover(u, url); continue; }
        break;
      }
      if (res.statusCode === 200) existing = 0;

      const serverLen = parseInt(res.headers['content-length'] || '0', 10);
      const total = existing + serverLen;
      let downloaded = existing;
      let startTime = Date.now();
      let lastNotify = 0;
      const flags = existing > 0 ? 'a' : 'w';

      try {
        await new Promise((resolve, reject) => {
          const stream = fs.createWriteStream(partPath, { flags });
          res.pipe(stream);
          res.on('data', (chunk) => {
            downloaded += chunk.length;
            const now = Date.now();
            if (now - lastNotify >= CFG.progressThrottleMs && onProgress) {
              lastNotify = now;
              const elapsed = (now - startTime) / 1000 || 0.001;
              onProgress(total ? (downloaded / total) * 100 : 0, downloaded / elapsed, downloaded, total);
            }
          });
          res.on('error', reject);
          stream.on('error', reject);
          stream.on('finish', resolve);
        });
      } catch (err) {
        lastErr = err;
        lastUrl = url;
        if (u < urls.length - 1) { if (onFailover) onFailover(u, url); continue; }
        break;
      }

      if (shouldCancel && shouldCancel()) { fs.unlinkSync(partPath); throw new Error('cancelled'); }

      // 全部数据已写入：完整性校验（sha256 强制 + 字节数与清单 size 一致）后再改名
      if (sha256) {
        const tmp = destPath + '.sha';
        fs.renameSync(partPath, tmp);
        const actual = sha256File(tmp);
        if (actual !== sha256) {
          fs.unlinkSync(tmp);
          lastErr = new Error('sha256 mismatch');
          lastUrl = url;
          if (u < urls.length - 1) { if (onFailover) onFailover(u, url); continue; }
          break;
        }
        fs.renameSync(tmp, destPath);
      } else {
        fs.renameSync(partPath, destPath);
      }
      if (expectedSize && fs.statSync(destPath).size !== expectedSize) {
        fs.unlinkSync(destPath);
        try { fs.unlinkSync(partPath); } catch (_) {}
        lastErr = new Error('size mismatch');
        lastUrl = url;
        if (u < urls.length - 1) { if (onFailover) onFailover(u, url); continue; }
        break;
      }
      return destPath;
    }

    // 所有线路本轮均失败
    if (attempt < retries) await sleep(delays[attempt]);
    if (attempt === retries) {
      const err = lastErr || new Error('download failed');
      throw new Error((err.message === 'download failed' ? '' : err.message) + ' (route ' + (lastUrl || 'unknown') + ')');
    }
  }
  throw new Error('unreachable');
}

// ============================================================
// 多线路实时测速与选路
// ============================================================
function median(arr) {
  if (arr.length === 0) return 0;
  const s = arr.slice().sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

/** 单线路单次探测：Range 请求 32KB，统计 TTFB 与速度。 */
function probeOne(url, timeoutMs) {
  return new Promise((resolve, reject) => {
    let parsed;
    try { parsed = new URL(url); } catch (e) { reject(e); return; }
    const mod = parsed.protocol === 'https:' ? https : http;
    const start = Date.now();
    let firstByteAt = 0;
    let bytes = 0;
    const req = mod.request(parsed, {
      method: 'GET',
      headers: { Range: 'bytes=0-32767', 'User-Agent': 'NEVO-Client/Updater' },
      timeout: timeoutMs,
    }, (res) => {
      if (res.statusCode !== 200 && res.statusCode !== 206) {
        res.resume();
        reject(new Error('HTTP ' + res.statusCode));
        return;
      }
      res.on('data', (c) => {
        if (!firstByteAt) firstByteAt = Date.now();
        bytes += c.length;
      });
      res.on('end', () => {
        const ttfbMs = firstByteAt ? firstByteAt - start : Date.now() - start;
        const totalMs = (Date.now() - start) || 1;
        resolve({ ok: true, ttfbMs, bytes, speedBps: Math.round((bytes / totalMs) * 1000), totalMs });
      });
      res.on('error', reject);
    });
    req.on('timeout', () => req.destroy(new Error('request timeout')));
    req.on('error', reject);
    req.end();
  });
}

/** 并行测速所有线路（每条 attempts 次取中位值），返回带 rank 的排序结果。 */
async function probeRoutes(routes, probeUrl, opts = {}) {
  const attempts = opts.attempts || 2;
  const timeoutMs = opts.timeoutMs || CFG.timeoutMs;
  const probeFn = opts.probeFn || probeOne;
  const raw = await Promise.all(routes.map(async (route) => {
    const samples = [];
    for (let i = 0; i < attempts; i++) {
      try {
        const s = await probeFn(route.url(probeUrl), timeoutMs);
        if (s && s.ok) samples.push(s);
        else samples.push({ ok: false, error: (s && s.error) || 'probe failed' });
      } catch (err) {
        samples.push({ ok: false, error: err.message });
      }
    }
    const ok = samples.filter((s) => s.ok);
    return {
      name: route.name,
      label: route.label,
      status: ok.length === 0 ? 'unreachable' : 'ok',
      latencyMs: ok.length ? Math.round(median(ok.map((s) => s.ttfbMs))) : null,
      speedBps: ok.length ? Math.round(median(ok.map((s) => s.speedBps))) : null,
      samples,
    };
  }));
  const okRoutes = raw.filter((r) => r.status === 'ok').sort((a, b) => {
    const d = a.latencyMs - b.latencyMs;
    if (d !== 0) return d;
    return b.speedBps - a.speedBps;
  });
  const order = okRoutes.map((r) => r.name);
  okRoutes.forEach((r, i) => { r.rank = i; });
  return raw.map((r) => {
    const idx = order.indexOf(r.name);
    return Object.assign(r, { rank: idx === -1 ? okRoutes.length : idx });
  });
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
      let manifest;
      try {
        manifest = parseManifest(text);
      } catch (err) {
        // 清单被拒绝（签名验证失败 / 缺 sha256 / 结构非法）→ 进入 error，拒绝后续下载
        this._log('check_error', { error: 'manifest rejected: ' + err.message, result: 'failed' });
        await this._setState('error');
        throw new Error('manifest rejected: ' + err.message);
      }
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

  /** 下载并准备更新。返回 {mode, path, staged}。 */
  async downloadUpdate() {
    if (!this._manifest) throw new Error('no manifest, run checkForUpdates first');
    const manifest = this._manifest;
    const mode = this._mode || decideMode(manifest, this.currentVersion);
    this._mode = mode;
    const updateDir = getUpdateDir(this.baseDir);
    const target = mode === 'delta'
      ? manifest.delta.url
      : manifest.full.url;
    // sha256 为强制校验（parseManifest 已保证其存在且合法）
    const sha = mode === 'delta' ? manifest.delta.sha256 : manifest.full.sha256;
    const expectedSize = mode === 'delta' ? manifest.delta.size : manifest.full.size;
    // 下载文件名取自 URL path 末段，必须净化；空结果回退到固定文件名
    let rawName = '';
    try { rawName = new URL(target).pathname.split('/').pop() || ''; }
    catch (_) { rawName = String(target || '').split('/').pop() || ''; }
    const filename = sanitizeFilename(rawName, mode === 'delta' ? 'delta.zip' : 'setup.exe');
    const destPath = path.join(updateDir, filename);
    this._log('download_start', { mode, target_version: manifest.version, source: this._source });
    await this._setState('downloading');

    // 按评分排序的多线路 URL（主源优先，其次镜像）
    const urls = [target];
    for (let i = 0; i < CFG.mirrorPrefixes.length; i++) {
      const p = proxyGithubUrl(target, CFG.mirrorPrefixes[i]);
      if (p !== target) urls.push(p);
    }

    try {
      this._downloadedPath = await downloadWithRoutes(urls, destPath, {
        timeoutMs: CFG.timeoutMs,
        sha256: sha,
        expectedSize: expectedSize || undefined,
        shouldCancel: () => this._cancel,
        onProgress: (p, s, d, t) => this._emitProgress(p, s, d, t),
        onFailover: (idx, url) => this._log('route_failover', { target_version: manifest.version, route_index: idx, url }),
      });
    } catch (err) {
      this._log('download_error', { mode, error: err.message, result: 'failed' });
      await this._setState('error');
      throw err;
    }
    this._log('download_complete', { mode, size: fs.statSync(destPath).size });

    // delta 模式：下载完成后自动暂存（生成替换脚本），保证 restartToApply 可执行
    if (mode === 'delta') {
      try {
        await this.applyDelta(destPath);
      } catch (err) {
        this._log('apply_error', { mode, error: err.message, result: 'failed' });
        await this._setState('error');
        throw err;
      }
      return { mode, path: destPath, staged: true };
    }

    await this._setState('ready');
    return { mode, path: destPath };
  }

  /** 对当前更新包（或 latest.json）进行多线路实时测速，返回排序结果。 */
  async probeAllRoutes(probeUrl) {
    const target = probeUrl || ((this._mode === 'delta' && this._manifest && this._manifest.delta)
      ? this._manifest.delta.url
      : (this._manifest && this._manifest.full ? this._manifest.full.url : githubApiLatestUrl()));
    const routes = [
      { name: 'github', label: 'GitHub 直连', url: (u) => u },
    ];
    for (let i = 0; i < CFG.mirrorPrefixes.length; i++) {
      routes.push({ name: `mirror${i + 1}`, label: '镜像 ' + (i + 1), url: (u) => proxyGithubUrl(u, CFG.mirrorPrefixes[i]) });
    }
    const results = await probeRoutes(routes, target, { timeoutMs: CFG.timeoutMs });
    this._probeResults = results;
    return results;
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
      // 路径校验：拒绝 ../ 逃逸与绝对路径（parseManifest 已校验，此处纵深防御）
      if (!isSafeRelPath(rel)) throw new Error('unsafe file path in delta manifest: ' + f.path);
      const src = path.join(extracted, rel);
      if (!fs.existsSync(src)) continue;
      // 校验解压产物与清单 sha256 一致，防止镜像源篡改 delta 包内容
      if (f.sha256 && sha256File(src) !== f.sha256.toLowerCase()) {
        throw new Error('delta file sha256 mismatch: ' + rel);
      }
      const dst = path.join(staged, rel);
      fs.mkdirSync(path.dirname(dst), { recursive: true });
      fs.copyFileSync(src, dst);
      entries.push({ path: rel, sha256: f.sha256 });
    }
    if (entries.length === 0) throw new Error('delta package contains no files');

    // 落盘替换计划（固定文件名 apply_manifest.json，由 apply_update.js 读取）。
    // 文件操作不再写入 .cmd，避免批处理特殊字符注入。
    const pid = process.pid;
    const appExe = process.execPath;
    const applyJsPath = getApplyJsPath();
    const cmdPath = path.join(updateDir, 'apply_update.cmd');
    const backupDir = path.join(updateDir, 'backup');
    const plan = {
      appDir: resourcesDir,
      stagedDir: staged,
      backupDir: backupDir,
      files: entries,
    };
    fs.writeFileSync(path.join(updateDir, 'apply_manifest.json'), JSON.stringify(plan, null, 2), 'utf-8');
    const cmd = buildApplyCmd(pid, appExe, applyJsPath);
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

/** apply_update.js 路径：packaged 时位于 app.asar 内（Electron 支持以 node 模式执行 asar 内脚本）。 */
function getApplyJsPath() {
  if (electron && electron.app && electron.app.isPackaged) {
    return path.join(electron.app.getAppPath(), 'apply_update.js');
  }
  return path.join(__dirname, 'apply_update.js');
}

/**
 * 下载文件名净化：仅保留 [A-Za-z0-9._-]，非法字符替换为 '_'；
 * 纯点号（. / .. / ...）或空结果回退到 fallback。防止 URL 末段被用作路径穿越或保留名。
 */
function sanitizeFilename(name, fallback) {
  let s = '';
  try { s = decodeURIComponent(String(name || '')); } catch (_) { s = String(name || ''); }
  s = s.replace(/[^A-Za-z0-9._-]/g, '_');
  s = s.replace(/^\.+$/, '');
  if (!s) return fallback;
  return s;
}

/** 校验 zip 条目名并将其解析到 outDir 内；任何可能逃逸的条目直接抛错。 */
function resolveZipDest(outDir, name) {
  const root = path.resolve(outDir);
  if (typeof name !== 'string' || name.includes('\0')) {
    throw new Error('unsafe zip entry: ' + name);
  }
  if (!isSafeRelPath(name)) {
    throw new Error('unsafe zip entry: ' + name);
  }
  const dest = path.resolve(root, name.replace(/\\/g, '/'));
  if (dest !== root && !dest.startsWith(root + path.sep)) {
    throw new Error('unsafe zip entry: ' + name);
  }
  return dest;
}

// 纯 Node zip 解压（仅支持无压缩/存储与 deflate 的 zip，足以满足发布产物）
// 安全加固：条目名路径校验（防 ../ 逃逸与绝对路径）+ zip 炸弹防护（条目数与解压总字节上限）
function extractZip(zipPath, outDir) {
  const zlib = require('zlib');
  const buf = fs.readFileSync(zipPath);
  let offset = 0;
  let entryCount = 0;
  let totalBytes = 0;
  while (offset + 30 <= buf.length) {
    // 局部文件头签名 0x04034b50
    if (buf.readUInt32LE(offset) !== 0x04034b50) break;
    const method = buf.readUInt16LE(offset + 8);
    const compSize = buf.readUInt32LE(offset + 18);
    const uncompSize = buf.readUInt32LE(offset + 22);
    const nameLen = buf.readUInt16LE(offset + 26);
    const extraLen = buf.readUInt16LE(offset + 28);
    const name = buf.toString('utf-8', offset + 30, offset + 30 + nameLen);
    const dataStart = offset + 30 + nameLen + extraLen;
    const data = buf.slice(dataStart, dataStart + compSize);
    offset = dataStart + compSize;
    if (/\/$/.test(name)) continue;
    entryCount++;
    if (entryCount > CFG.zipMaxEntries) {
      throw new Error('zip entry count exceeds limit (' + CFG.zipMaxEntries + ')');
    }
    if (uncompSize > 0 && totalBytes + uncompSize > CFG.zipMaxTotalBytes) {
      throw new Error('zip total uncompressed size exceeds limit');
    }
    const dest = resolveZipDest(outDir, name);
    let payload;
    if (method === 0) payload = data;
    else if (method === 8) {
      // maxOutputLength 防止解压炸弹在内存中膨胀
      payload = zlib.inflateRawSync(data, { maxOutputLength: CFG.zipMaxTotalBytes - totalBytes });
    } else {
      throw new Error('unsupported zip method ' + method);
    }
    totalBytes += payload.length;
    if (totalBytes > CFG.zipMaxTotalBytes) {
      throw new Error('zip total uncompressed size exceeds limit');
    }
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, payload);
  }
  return Promise.resolve();
}

/**
 * 生成增量替换引导脚本。安全设计：脚本不再内嵌任何清单文件名（避免批处理
 * 特殊字符注入），只做三件事：
 *   1) 等待主进程退出（pid 为纯数字，安全）；
 *   2) 以 node 模式（ELECTRON_RUN_AS_NODE）执行固定路径的 apply_update.js，
 *      由它读取 updateDir/apply_manifest.json（固定文件名）完成备份→替换→回滚；
 *   3) 重启应用。
 * appExe/applyJsPath 为本地固定路径（% 转义为 %%），不含任何网络内容。
 */
function buildApplyCmd(pid, appExe, applyJsPath) {
  const pidStr = String(parseInt(pid, 10) || 0);
  const esc = (s) => String(s).replace(/\//g, '\\').replace(/%/g, '%%');
  const lines = ['@echo off', 'chcp 65001 >nul', 'setlocal enabledelayedexpansion', ''];
  lines.push('rem wait for app to exit');
  lines.push(':wait_loop');
  lines.push(`tasklist /fi "PID eq ${pidStr}" 2>nul | findstr /I "${pidStr}" >nul`);
  lines.push('if !errorlevel! == 0 (');
  lines.push('  timeout /t 1 /nobreak >nul');
  lines.push('  goto wait_loop');
  lines.push(')');
  lines.push('timeout /t 1 /nobreak >nul');
  lines.push('endlocal');
  lines.push('rem apply files: run apply_update.js in node mode (fixed path, no dynamic args)');
  lines.push('set "ELECTRON_RUN_AS_NODE=1"');
  lines.push(`"${esc(appExe)}" "${esc(applyJsPath)}" "%~dp0"`);
  lines.push('rem restart app');
  lines.push(`start "" "${esc(appExe)}"`);
  return lines.join('\r\n');
}

module.exports = {
  CFG, parseVersion, isNewerVersion,
  githubApiLatestUrl, proxyGithubUrl, parseManifest, decideMode,
  getUpdateDir, getLogPath, logUpdateEvent, readUpdateLog,
  computeRetryDelays, sleep, httpGet, sha256File, downloadWithResume,
  downloadWithRoutes,
  median, probeOne, probeRoutes,
  UpdateEngine, defaultBaseDir, defaultCurrentVersion, defaultFetcher,
  getResourcesDir, getApplyJsPath, sanitizeFilename, isSafeRelPath,
  resolveZipDest, extractZip, buildApplyCmd,
  PUBLIC_KEY_HEX, setPublicKey, canonicalManifestBytes, verifyManifestSignature,
};
