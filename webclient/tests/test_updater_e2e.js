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

  // 构造 zip（store 方式：local header + data），含差异文件与 manifest.json
  const zipParts = [];
  function zipEntry(nameStr, data) {
    const name = Buffer.from(nameStr);
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50, 0); // signature
    header.writeUInt16LE(0, 8);          // method store
    header.writeUInt32LE(data.length, 18);
    header.writeUInt16LE(name.length, 26);
    zipParts.push(Buffer.concat([header, name, data]));
  }
  zipEntry('nevo_gateway/_internal/js/app.js', fileA);
  zipEntry('manifest.json', Buffer.from(JSON.stringify(manifest), 'utf-8'));
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0); // EOCD
  const zipBuf = Buffer.concat(zipParts.concat([eocd]));

  const server = http.createServer((req, res) => {
    if (req.url === '/manifest.json') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(manifest));
      return;
    }
    if (req.url === '/delta.zip') {
      res.writeHead(200, { 'Content-Type': 'application/zip' });
      res.end(zipBuf);
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
  manifest.full_package.url = `${base}/setup.exe`;

  // --- 断点续传下载 ---
  const dlDest = path.join(tmp, 'out.zip');
  // 先直接下载一次（模拟完整），再测 .part 续传：
  // 首次写部分 .part 文件模拟中断
  fs.writeFileSync(dlDest + '.part', fileA.slice(0, 6));
  const done = await U.downloadWithResume(`${base}/delta.zip`, dlDest, { retries: 0 });
  t(done === dlDest, 'resume download completes');
  const got = fs.readFileSync(dlDest);
  t(got.length === zipBuf.length && got.equals(zipBuf), 'resumed file content matches');

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
