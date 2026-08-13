'use strict';
const assert = require('assert');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');
const U = require('../electron/updater.js');

// 测试专用 Ed25519 密钥对：运行时生成并注入 updater.js（仓库不保存任何私钥材料）。
// setPublicKey 需要 32 字节原始公钥 hex（从 SPKI DER 尾部取）。
const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
U.setPublicKey(publicKey.export({ type: 'spki', format: 'der' }).subarray(-32).toString('hex'));

/** 与 updater.js 的 canonicalManifestBytes 对应：JSON.stringify 后 sha256 再 Ed25519 签名。 */
function signManifest(obj) {
  const canonical = Buffer.from(JSON.stringify(obj), 'utf-8');
  const msg = crypto.createHash('sha256').update(canonical).digest();
  return crypto.sign(null, msg, privateKey).toString('hex');
}

let pass = 0, fail = 0;
function t(cond, msg) { if (cond) { pass++; } else { fail++; console.error('  FAIL:', msg); } }

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nevo-e2e-'));
  // 构造 mock 文件内容与清单
  const fileA = Buffer.from('hello delta world', 'utf-8');
  const fileASha = crypto.createHash('sha256').update(fileA).digest('hex');
  const setupBody = Buffer.alloc(100, 1);
  const setupSha = crypto.createHash('sha256').update(setupBody).digest('hex');

  // delta 包内 manifest（不含 delta 字段，避免自引用；单独签名）
  const innerManifestObj = {
    version: 'BETA0.0.2',
    files: [
      { path: 'nevo_gateway/_internal/js/app.js', sha256: fileASha, size: fileA.length },
    ],
    full_package: { url: 'http://127.0.0.1:0/setup.exe', size: 100, sha256: setupSha },
  };
  innerManifestObj.signature = signManifest(innerManifestObj);

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
  zipEntry('manifest.json', Buffer.from(JSON.stringify(innerManifestObj), 'utf-8'));
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0); // EOCD
  const zipBuf = Buffer.concat(zipParts.concat([eocd]));
  const zipSha = crypto.createHash('sha256').update(zipBuf).digest('hex');

  // 外部清单（latest.json 等价物）：签名在 URL 指向本地后计算
  const manifest = {
    version: 'BETA0.0.2',
    files: [
      { path: 'nevo_gateway/_internal/js/app.js', sha256: fileASha, size: fileA.length },
    ],
    // full_package.size 仅用于增量/全量决策（远大于 delta zip），全量下载路径本测试不触发
    full_package: { url: 'http://127.0.0.1:0/setup.exe', size: 100000, sha256: setupSha },
    delta: {
      from: 'BETA0.0.1',
      url: 'http://127.0.0.1:0/delta.zip',
      size: zipBuf.length,
      sha256: zipSha,
    },
  };

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
      res.writeHead(200, { 'Content-Length': String(setupBody.length) });
      res.end(setupBody);
      return;
    }
    res.writeHead(404); res.end();
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;
  const base = `http://127.0.0.1:${port}`;

  // 修正清单 URL 指向本地后签名（签名字段最后追加，规范化字节与发布端一致）
  manifest.delta.url = `${base}/delta.zip`;
  manifest.full_package.url = `${base}/setup.exe`;
  manifest.signature = signManifest(manifest);

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
  const badSha = '0000000000000000000000000000000000000000000000000000000000000000';
  let verifyErr = null;
  try {
    await U.downloadWithResume(`${base}/delta.zip`, path.join(tmp, 'v.zip'), {
      retries: 1, sha256: badSha, timeoutMs: 2000,
      shouldCancel: () => false,
    });
  } catch (e) { verifyErr = e; }
  t(!!verifyErr && /sha256/.test(verifyErr.message), 'sha256 mismatch throws');

  // --- 下载字节数与清单 size 不一致必须报错 ---
  let sizeErr = null;
  try {
    await U.downloadWithResume(`${base}/delta.zip`, path.join(tmp, 's.zip'), {
      retries: 0, sha256: zipSha, expectedSize: zipBuf.length + 10,
      shouldCancel: () => false,
    });
  } catch (e) { sizeErr = e; }
  t(!!sizeErr && /size mismatch/.test(sizeErr.message), 'expectedSize mismatch throws');

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
  t(dlRes.staged === true, 'delta auto-staged after download');
  t(engine._stagedCmd && fs.existsSync(engine._stagedCmd), 'staged cmd exists after auto-stage');

  // --- 增量应用（自动暂存后再手动 apply 验证幂等） ---
  const applyRes = await engine.applyDelta(dlRes.path);
  t(applyRes && applyRes.cmdPath && fs.existsSync(applyRes.cmdPath), 'apply delta generates cmd');
  const cmdText = fs.readFileSync(applyRes.cmdPath, 'utf-8');
  t(cmdText.includes('apply_update.js') && cmdText.includes('ELECTRON_RUN_AS_NODE'),
    'cmd delegates to apply_update.js in node mode');
  t(!cmdText.includes('_internal') && !cmdText.includes('nevo_gateway'),
    'cmd contains no dynamic file names (command injection fix)');
  const planPath = path.join(U.getUpdateDir(engine.baseDir), 'apply_manifest.json');
  t(fs.existsSync(planPath), 'apply plan json written');
  const plan = JSON.parse(fs.readFileSync(planPath, 'utf-8'));
  t(Array.isArray(plan.files) && plan.files.length === 1 && plan.files[0].path === 'nevo_gateway/_internal/js/app.js',
    'apply plan json carries file list');

  // --- 日志 ---
  const log = U.readUpdateLog(engine.baseDir);
  t(log.some((e) => e.event === 'check_ok'), 'log has check_ok');
  t(log.some((e) => e.event === 'download_complete'), 'log has download_complete');

  // --- 多线路故障转移 ---
  const badBody = Buffer.from('server error', 'utf-8');
  const goodBody = Buffer.from('hello-partial', 'utf-8');
  const badSrv = http.createServer((req, res) => { res.writeHead(500); res.end(badBody); });
  const goodSrv = http.createServer((req, res) => {
    if (req.headers.range) {
      res.writeHead(206, { 'Content-Range': 'bytes=0-' + (goodBody.length - 1) + '/' + goodBody.length, 'Content-Length': String(goodBody.length) });
      res.end(goodBody);
    } else {
      res.writeHead(200, { 'Content-Length': String(goodBody.length) });
      res.end(goodBody);
    }
  });
  await new Promise((r) => badSrv.listen(0, '127.0.0.1', r));
  await new Promise((r) => goodSrv.listen(0, '127.0.0.1', r));
  const dlDir = fs.mkdtempSync(path.join(os.tmpdir(), 'dlroutes-'));
  const failovers = [];
  const routeDest = path.join(dlDir, 'f.bin');
  const dl2 = await U.downloadWithRoutes(
    [`http://127.0.0.1:${badSrv.address().port}/f.bin`, `http://127.0.0.1:${goodSrv.address().port}/f.bin`],
    routeDest,
    { retries: 0, timeoutMs: 2000, onFailover: (i, u) => failovers.push(i) }
  );
  t(dl2 === routeDest, 'downloadWithRoutes returns dest');
  t(fs.readFileSync(routeDest, 'utf-8') === 'hello-partial', 'downloaded from second route after failover');
  t(failovers.length >= 1, 'failover event emitted');
  badSrv.close(); goodSrv.close(); fs.rmSync(dlDir, { recursive: true, force: true });

  server.close();
  console.log(`\nResult: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
