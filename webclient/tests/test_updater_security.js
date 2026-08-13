'use strict';
/**
 * 更新器安全加固测试：路径穿越 / zip 炸弹 / 强制 sha256 / 清单签名 /
 * apply_update.cmd 注入防护 / apply_update.js 路径校验与回滚。
 */
const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');
const U = require('../electron/updater.js');
const AU = require('../electron/apply_update.js');

// 测试专用 Ed25519 密钥对：运行时生成并注入 updater.js（仓库不保存任何私钥材料）。
const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
U.setPublicKey(publicKey.export({ type: 'spki', format: 'der' }).subarray(-32).toString('hex'));

/** 与 updater.js 的 canonicalManifestBytes 对应：JSON.stringify 后 sha256 再 Ed25519 签名。 */
function signManifest(obj) {
  const canonical = Buffer.from(JSON.stringify(obj), 'utf-8');
  const msg = crypto.createHash('sha256').update(canonical).digest();
  return crypto.sign(null, msg, privateKey).toString('hex');
}

function sha256buf(b) { return crypto.createHash('sha256').update(b).digest('hex'); }

/** 构造 store 方式 zip（local header + data），顺序写入。 */
function buildZip(entries) {
  const parts = [];
  for (const [nameStr, data] of entries) {
    const name = Buffer.from(nameStr);
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50, 0); // signature
    header.writeUInt16LE(0, 8);          // method store
    header.writeUInt32LE(data.length, 18);
    header.writeUInt16LE(name.length, 26);
    parts.push(Buffer.concat([header, name, data]));
  }
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0); // EOCD
  parts.push(eocd);
  return Buffer.concat(parts);
}

const H64 = 'ab'.repeat(32); // 合法 64 位 hex sha256 占位值

let pass = 0, fail = 0;
function t(cond, msg) { if (cond) { pass++; } else { fail++; console.error('  FAIL:', msg); } }

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nevo-sec-'));

  // ============================================================
  // extractZip：路径穿越与 zip 炸弹
  // ============================================================
  const exDir = path.join(tmp, 'ex');
  fs.mkdirSync(exDir, { recursive: true });
  const zipPath = (name) => path.join(tmp, name);

  // 合法 zip 正常解压
  fs.writeFileSync(zipPath('ok.zip'), buildZip([['app.asar', Buffer.from('data')]]));
  await U.extractZip(zipPath('ok.zip'), exDir);
  t(fs.readFileSync(path.join(exDir, 'app.asar'), 'utf-8') === 'data', 'extractZip extracts valid entry');
  fs.rmSync(exDir, { recursive: true, force: true });
  fs.mkdirSync(exDir, { recursive: true });

  // ../ 逃逸条目拒绝
  fs.writeFileSync(zipPath('t1.zip'), buildZip([['../evil.txt', Buffer.from('pwn')]]));
  let threw = false;
  try { await U.extractZip(zipPath('t1.zip'), exDir); } catch (e) { threw = /unsafe/.test(e.message); }
  t(threw, 'extractZip rejects ../ entry');
  t(!fs.existsSync(path.join(tmp, 'evil.txt')), 'no file escaped outDir');

  // 绝对路径条目拒绝（Unix 风格与 Windows 盘符风格）
  for (const absName of ['/abs.txt', 'C:\\Windows\\evil.txt', 'C:/Windows/evil.txt']) {
    fs.writeFileSync(zipPath('t2.zip'), buildZip([[absName, Buffer.from('pwn')]]));
    let th = false;
    try { await U.extractZip(zipPath('t2.zip'), exDir); } catch (e) { th = /unsafe/.test(e.message); }
    t(th, `extractZip rejects absolute entry ${absName}`);
  }

  // 条目数超限（临时调低上限）
  const backupEntries = U.CFG.zipMaxEntries;
  U.CFG.zipMaxEntries = 2;
  fs.writeFileSync(zipPath('t3.zip'), buildZip([
    ['a.txt', Buffer.from('a')], ['b.txt', Buffer.from('b')], ['c.txt', Buffer.from('c')],
  ]));
  threw = false;
  try { await U.extractZip(zipPath('t3.zip'), exDir); } catch (e) { threw = /count exceeds/.test(e.message); }
  t(threw, 'extractZip rejects entry count over limit');
  U.CFG.zipMaxEntries = backupEntries;

  // 解压总字节超限（临时调低上限）
  const backupBytes = U.CFG.zipMaxTotalBytes;
  U.CFG.zipMaxTotalBytes = 10;
  fs.writeFileSync(zipPath('t4.zip'), buildZip([['big.bin', Buffer.alloc(100, 1)]]));
  threw = false;
  try { await U.extractZip(zipPath('t4.zip'), exDir); } catch (e) { threw = /exceeds limit/.test(e.message); }
  t(threw, 'extractZip rejects total bytes over limit');
  U.CFG.zipMaxTotalBytes = backupBytes;

  // ============================================================
  // parseManifest：强制 sha256
  // ============================================================
  const baseManifest = () => ({
    version: 'BETA0.0.2',
    files: [{ path: 'app.asar', sha256: H64, size: 10 }],
    full_package: { url: 'https://x/Setup.exe', size: 100, sha256: H64 },
    delta: { from: 'BETA0.0.1', url: 'https://x/d.zip', size: 20, sha256: H64 },
  });
  const expectThrow = (obj, re, msg) => {
    let th = false;
    try { U.parseManifest(JSON.stringify(obj), { requireSignature: false }); }
    catch (e) { th = re.test(e.message); }
    t(th, msg);
  };
  const m1 = baseManifest(); delete m1.full_package.sha256;
  expectThrow(m1, /sha256/, 'manifest without full sha256 rejected');
  const m2 = baseManifest(); delete m2.delta.sha256;
  expectThrow(m2, /sha256/, 'manifest without delta sha256 rejected');
  const m3 = baseManifest(); delete m3.files[0].sha256;
  expectThrow(m3, /sha256/, 'manifest file entry without sha256 rejected');
  const m4 = baseManifest(); m4.full_package.sha256 = 'deadbeef';
  expectThrow(m4, /sha256/, 'manifest with malformed sha256 rejected');
  const m5 = baseManifest(); m5.files[0].path = '../evil.js';
  expectThrow(m5, /unsafe/, 'manifest with ../ file path rejected');

  // ============================================================
  // 清单签名（Ed25519）
  // ============================================================
  const pubCfgBackup = U.CFG.requireSignedManifest;
  U.CFG.requireSignedManifest = true;

  const unsigned = baseManifest();
  const signed = Object.assign({}, unsigned, { signature: signManifest(unsigned) });
  const parsed = U.parseManifest(JSON.stringify(signed));
  t(parsed.version === 'BETA0.0.2', 'validly signed manifest passes');

  // 篡改任一字段 → 验证失败
  const tamperedUrl = JSON.parse(JSON.stringify(signed));
  tamperedUrl.full_package.url = 'https://evil.example.com/malware.exe';
  let th = false;
  try { U.parseManifest(JSON.stringify(tamperedUrl)); } catch (e) { th = /signature/.test(e.message); }
  t(th, 'tampered manifest field fails signature verification');

  const tamperedVer = JSON.parse(JSON.stringify(signed));
  tamperedVer.version = 'BETA9.9.9';
  th = false;
  try { U.parseManifest(JSON.stringify(tamperedVer)); } catch (e) { th = /signature/.test(e.message); }
  t(th, 'tampered version fails signature verification');

  const tamperedSig = Object.assign({}, unsigned, { signature: 'ff'.repeat(64) });
  th = false;
  try { U.parseManifest(JSON.stringify(tamperedSig)); } catch (e) { th = /signature/.test(e.message); }
  t(th, 'invalid signature bytes rejected');

  // 非法长度签名
  th = false;
  try { U.parseManifest(JSON.stringify(Object.assign({}, unsigned, { signature: 'abcd' }))); }
  catch (e) { th = /signature/.test(e.message); }
  t(th, 'malformed signature field rejected');

  // requireSignedManifest=true 时无签名清单被拒
  th = false;
  try { U.parseManifest(JSON.stringify(unsigned)); } catch (e) { th = /signature/.test(e.message); }
  t(th, 'unsigned manifest rejected when requireSignedManifest=true');

  // 显式关闭时无签名清单可解析（兼容路径）
  const compatParsed = U.parseManifest(JSON.stringify(unsigned), { requireSignature: false });
  t(compatParsed.version === 'BETA0.0.2', 'unsigned manifest accepted when requireSignature=false');
  U.CFG.requireSignedManifest = pubCfgBackup;

  // 篡改清单 → 状态机进入 error 并拒绝下载
  const badEngine = new U.UpdateEngine({
    baseDir: path.join(tmp, 'badeng'),
    currentVersion: 'BETA0.0.1',
    fetcher: async (kind) => {
      if (kind === 'api') {
        return { assets: [{ name: 'latest.json', browser_download_url: 'https://x/latest.json' }] };
      }
      return JSON.stringify(tamperedUrl);
    },
  });
  let engErr = null;
  try { await badEngine.checkForUpdates(); } catch (e) { engErr = e; }
  t(!!engErr && /manifest rejected/.test(engErr.message) && badEngine.state === 'error',
    'tampered manifest -> engine error state, download refused');

  // ============================================================
  // apply_update.cmd：不含动态文件名
  // ============================================================
  const cmd = U.buildApplyCmd(
    12345,
    'C:\\Program Files\\NEVO\\NEVO Web Client.exe',
    'C:\\Program Files\\NEVO\\resources\\app.asar\\apply_update.js'
  );
  t(cmd.includes('apply_update.js') && cmd.includes('ELECTRON_RUN_AS_NODE'), 'cmd runs apply_update.js in node mode');
  t(cmd.includes('PID eq 12345'), 'cmd waits for app pid');
  t(!cmd.includes('xcopy') && !cmd.includes('copy /y') && !cmd.includes('STAGED') && !cmd.includes('BACKUP'),
    'cmd contains no per-file copy operations (injection surface removed)');
  t(!cmd.includes('& echo') && !cmd.includes('&&'), 'cmd contains no injected command chaining');

  // ============================================================
  // apply_update.js：路径校验 / sha256 / 回滚
  // ============================================================
  const appDir = path.join(tmp, 'appdir');
  const stagedDir = path.join(tmp, 'stageddir');
  const backupDir = path.join(tmp, 'backupdir');
  fs.mkdirSync(appDir, { recursive: true });
  fs.mkdirSync(stagedDir, { recursive: true });

  // 成功替换
  fs.writeFileSync(path.join(appDir, 'a.txt'), 'OLD');
  const newContent = Buffer.from('NEW');
  fs.writeFileSync(path.join(stagedDir, 'a.txt'), newContent);
  const r1 = AU.applyPlan({
    appDir, stagedDir, backupDir,
    files: [{ path: 'a.txt', sha256: sha256buf(newContent) }],
  });
  t(r1.ok === true && r1.replaced === 1, 'applyPlan replaces file');
  t(fs.readFileSync(path.join(appDir, 'a.txt'), 'utf-8') === 'NEW', 'replaced content correct');
  t(fs.readFileSync(path.join(backupDir, 'a.txt'), 'utf-8') === 'OLD', 'old file backed up');

  // ../ 与绝对路径拒绝
  for (const badPath of ['../evil.txt', 'C:\\evil.txt', '/evil.txt', 'a/../../b.txt']) {
    let th2 = false;
    try {
      AU.applyPlan({ appDir, stagedDir, backupDir, files: [{ path: badPath, sha256: H64 }] });
    } catch (e) { th2 = /unsafe|escapes/.test(e.message); }
    t(th2, `applyPlan rejects unsafe path ${badPath}`);
  }

  // sha256 不一致 → 拒绝执行且不触碰目标文件
  fs.writeFileSync(path.join(appDir, 'a.txt'), 'NEW');
  fs.writeFileSync(path.join(stagedDir, 'a.txt'), Buffer.from('WRONG'));
  let th3 = false;
  try {
    AU.applyPlan({ appDir, stagedDir, backupDir, files: [{ path: 'a.txt', sha256: H64 }] });
  } catch (e) { th3 = /sha256 mismatch/.test(e.message); }
  t(th3, 'applyPlan rejects staged sha256 mismatch');
  t(fs.readFileSync(path.join(appDir, 'a.txt'), 'utf-8') === 'NEW', 'target untouched after pre-check failure');

  // 替换中途失败 → 已替换文件回滚
  fs.writeFileSync(path.join(appDir, 'f1.txt'), 'OLD1');
  fs.writeFileSync(path.join(stagedDir, 'f1.txt'), Buffer.from('NEW1'));
  fs.mkdirSync(path.join(appDir, 'f2.txt')); // 目标为目录 → 备份/替换必然失败
  fs.writeFileSync(path.join(stagedDir, 'f2.txt'), Buffer.from('NEW2'));
  let th4 = null;
  try {
    AU.applyPlan({
      appDir, stagedDir, backupDir,
      files: [
        { path: 'f1.txt', sha256: sha256buf(Buffer.from('NEW1')) },
        { path: 'f2.txt', sha256: sha256buf(Buffer.from('NEW2')) },
      ],
    });
  } catch (e) { th4 = e; }
  t(!!th4 && /apply failed/.test(th4.message), 'mid-apply failure throws');
  t(fs.readFileSync(path.join(appDir, 'f1.txt'), 'utf-8') === 'OLD1', 'already-replaced file rolled back');

  // 缺 sha256 的 staged 条目拒绝执行
  let th5 = false;
  try {
    AU.applyPlan({ appDir, stagedDir, backupDir, files: [{ path: 'f1.txt', sha256: '' }] });
  } catch (e) { th5 = /missing sha256/.test(e.message); }
  t(th5, 'applyPlan rejects entry without sha256');

  // ============================================================
  // 下载文件名净化
  // ============================================================
  t(U.sanitizeFilename('setup.exe', 'fb') === 'setup.exe', 'sanitize keeps clean name');
  t(U.sanitizeFilename('..', 'fb') === 'fb', 'sanitize falls back for ..');
  t(U.sanitizeFilename('', 'fb') === 'fb', 'sanitize falls back for empty');
  t(U.sanitizeFilename('...', 'fb') === 'fb', 'sanitize falls back for dots');
  t(U.sanitizeFilename('evil&name.exe', 'fb') === 'evil_name.exe', 'sanitize strips metacharacters');
  t(U.sanitizeFilename('a b%20c..exe', 'fb') === 'a_b_c..exe', 'sanitize percent-decodes then strips');
  t(U.sanitizeFilename('con', 'fb') === 'con', 'sanitize preserves plain word');

  // ============================================================
  // isSafeRelPath
  // ============================================================
  t(U.isSafeRelPath('a/b.txt') === true, 'isSafeRelPath accepts normal path');
  t(U.isSafeRelPath('../a.txt') === false, 'isSafeRelPath rejects ..');
  t(U.isSafeRelPath('a/../../b.txt') === false, 'isSafeRelPath rejects deep ..');
  t(U.isSafeRelPath('/abs.txt') === false, 'isSafeRelPath rejects leading slash');
  t(U.isSafeRelPath('C:\\abs.txt') === false, 'isSafeRelPath rejects drive path');
  t(U.isSafeRelPath('a//b.txt') === false, 'isSafeRelPath rejects empty segment');
  t(U.isSafeRelPath('a/./b.txt') === false, 'isSafeRelPath rejects dot segment');
  t(U.isSafeRelPath('') === false, 'isSafeRelPath rejects empty');

  console.log(`\nResult: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
