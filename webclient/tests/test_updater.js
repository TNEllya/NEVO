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

console.log(`\nResult: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
