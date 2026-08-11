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

module.exports = {
  CFG, parseVersion, isNewerVersion,
  githubApiLatestUrl, proxyGithubUrl, parseManifest, decideMode,
  getUpdateDir, getLogPath, logUpdateEvent, readUpdateLog,
  computeRetryDelays, sleep, httpGet, sha256File, downloadWithResume,
};
