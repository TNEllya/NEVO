'use strict';
const assert = require('assert');
const os = require('os');
const path = require('path');
const fs = require('fs');
const U = require('../electron/updater.js');

let pass = 0, fail = 0;
function t(cond, msg) { if (cond) { pass++; } else { fail++; console.error('  FAIL:', msg); } }

// 主进程必须在创建更新引擎前导入更新模块
const mainSource = fs.readFileSync(path.join(__dirname, '..', 'electron', 'main.js'), 'utf8');
const updaterImportAt = mainSource.indexOf("const updater = require('./updater.js');");
const updaterUseAt = mainSource.indexOf('new updater.UpdateEngine()');
t(updaterImportAt >= 0 && updaterImportAt < updaterUseAt, 'main imports updater before creating engine');

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

// 日志：写入、读取、截断
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

// 下载辅助：重试延迟
const delays = U.computeRetryDelays(3, [3000, 6000, 9000]);
t(delays.length === 3 && delays[0] === 3000 && delays[2] === 9000, 'computeRetryDelays');

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

  // --- 多源清单检测：镜像轮次复用 assetUrl，不重复调用 API ---
  const calls = [];
  const e4 = new U.UpdateEngine({
    currentVersion: 'BETA0.0.1',
    fetcher: async (kind, url, opts = {}) => {
      calls.push({ kind, url, isMirror: !!opts.isMirror });
      if (kind === 'api') {
        return { tag_name: 'BETA0.0.2', assets: [{ name: 'latest.json', browser_download_url: 'https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/latest.json' }] };
      }
      if (kind === 'manifest') {
        return JSON.stringify({ version: 'BETA0.0.2', full_package: { url: 'https://github.com/x/Setup.exe', size: 10, sha256: 'a' } });
      }
      throw new Error('unknown');
    },
  });
  const info4 = await e4.checkForUpdates();
  t(info4 && info4.version === 'BETA0.0.2', 'multi-round check finds update');
  const apiCalls = calls.filter((c) => c.kind === 'api');
  t(apiCalls.length === 1, 'mirror rounds do not re-call API (only github round calls API)');
  const manifestCalls = calls.filter((c) => c.kind === 'manifest');
  t(manifestCalls.length === 1, 'first manifest success stops further rounds');
  t(manifestCalls[0].url === 'https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/latest.json', 'github manifest uses assetUrl directly');

  // --- 主源清单失败时镜像可兜底 ---
  const calls2 = [];
  const e5 = new U.UpdateEngine({
    currentVersion: 'BETA0.0.1',
    fetcher: async (kind, url, opts = {}) => {
      calls2.push({ kind, url });
      if (kind === 'api') {
        return { tag_name: 'BETA0.0.2', assets: [{ name: 'latest.json', browser_download_url: 'https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/latest.json' }] };
      }
      if (kind === 'manifest') {
        if (!url.startsWith('https://ghproxy.com/')) throw new Error('request timeout');
        return JSON.stringify({ version: 'BETA0.0.2', full_package: { url: 'https://github.com/x/Setup.exe', size: 10, sha256: 'a' } });
      }
      throw new Error('unknown');
    },
  });
  const info5 = await e5.checkForUpdates();
  t(info5 && info5.source === 'mirror1', 'mirror manifest fallback when github times out');
  t(apiCalls2_guard(calls2), 'manifest tried github then mirror');

  // --- 多线路测速 ---
  t(U.proxyGithubUrl('https://api.github.com/repos/a/b', 'https://ghfast.top/') === 'https://ghfast.top/https://api.github.com/repos/a/b', 'proxy api.github.com');
  t(U.proxyGithubUrl('https://ghfast.top/https://github.com/a/b', 'https://ghfast.top/') === 'https://ghfast.top/https://github.com/a/b', 'do not double-proxy existing mirror');

  (async () => {
    const called = [];
    const routes = [
      { name: 'r1', label: 'R1', url: (u) => u },
      { name: 'r2', label: 'R2', url: (u) => 'M:' + u },
    ];
    const results = await U.probeRoutes(routes, 'https://x/file.bin', {
      attempts: 2,
      probeFn: (url) => {
        called.push(url);
        if (url.startsWith('M:')) return Promise.resolve({ ok: true, ttfbMs: 30, bytes: 32768, speedBps: 1000000, totalMs: 33 });
        return Promise.resolve({ ok: true, ttfbMs: 90, bytes: 32768, speedBps: 400000, totalMs: 82 });
      },
    });
    const ranked = results.slice().sort((a, b) => a.rank - b.rank);
    t(ranked[0].name === 'r2', 'lower latency route ranked first');
    t(ranked[1].name === 'r1', 'higher latency route ranked second');
    t(called.filter((u) => u.startsWith('M:')).length === 2, 'each route probed attempts times');
    t(results[0].status === 'ok' && results[0].latencyMs === 90, 'route result carries latency');

    const routes2 = [{ name: 'x', label: 'X', url: (u) => u }];
    const results2 = await U.probeRoutes(routes2, 'https://x/f.bin', {
      attempts: 1,
      probeFn: () => Promise.reject(new Error('request timeout')),
    });
    t(results2[0].status === 'unreachable', 'failed probe marked unreachable');
    t(results2[0].latencyMs === null, 'unreachable route has no latency');
    console.log(`\nResult: ${pass} passed, ${fail} failed`);
    process.exit(fail ? 1 : 0);
  })();
})();

function apiCalls2_guard(calls2) {
  return calls2.some((c) => c.kind === 'manifest' && !c.url.startsWith('https://ghproxy.com/'))
    && calls2.some((c) => c.kind === 'manifest' && c.url.startsWith('https://ghproxy.com/'));
}
