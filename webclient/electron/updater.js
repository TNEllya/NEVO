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

module.exports = {
  CFG, parseVersion, isNewerVersion,
  githubApiLatestUrl, proxyGithubUrl, parseManifest, decideMode,
  getUpdateDir, getLogPath, logUpdateEvent, readUpdateLog,
};
