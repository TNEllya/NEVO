'use strict';
/**
 * NEVO Web Client — 增量更新落地脚本
 * 由 apply_update.cmd 以 node 模式（ELECTRON_RUN_AS_NODE=1）调用，零第三方依赖。
 *
 * 安全设计：文件名不再写入批处理脚本。updater.js 把替换计划写入
 * <updateDir>/apply_manifest.json（固定文件名），本脚本读取后：
 *   1) 对每个目标路径再次做路径校验（必须位于安装目录内，拒绝 ../ 与绝对路径）；
 *   2) 校验 staged 文件存在且 sha256 与计划一致；
 *   3) 备份 → 替换，任一步失败立即回滚；
 *   4) 结果写入 apply_result.json，成功退出码 0，失败 1。
 */
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const MANIFEST_NAME = 'apply_manifest.json';
const RESULT_NAME = 'apply_result.json';
const SHA256_HEX_RE = /^[0-9a-f]{64}$/i;

/** 相对路径校验：拒绝空段、'.'/'..'、盘符前缀与反斜杠分隔的逃逸形式。 */
function isSafeRelPath(rel) {
  if (typeof rel !== 'string' || rel.length === 0) return false;
  const parts = rel.replace(/\\/g, '/').split('/');
  if (parts.some((p) => p === '' || p === '.' || p === '..')) return false;
  if (/^[A-Za-z]:/.test(parts[0])) return false;
  return true;
}

/** 将 rel 解析到 root 内；越界或非法路径抛错。 */
function resolveWithin(root, rel) {
  const rootAbs = path.resolve(root);
  const safe = String(rel).replace(/\\/g, '/');
  const parts = safe.split('/');
  if (parts.length === 0 || parts.some((p) => p === '' || p === '.' || p === '..')) {
    throw new Error('unsafe path in apply manifest: ' + rel);
  }
  if (/^[A-Za-z]:/.test(parts[0])) {
    throw new Error('unsafe absolute path in apply manifest: ' + rel);
  }
  const dest = path.resolve(rootAbs, safe);
  if (dest !== rootAbs && !dest.startsWith(rootAbs + path.sep)) {
    throw new Error('path escapes install dir: ' + rel);
  }
  return dest;
}

/**
 * 应用增量替换计划。
 * plan: { appDir, stagedDir, backupDir, files: [{ path, sha256 }] }
 * 成功返回 { ok: true, replaced: n }；失败抛错（已尽力回滚）。
 */
function applyPlan(plan) {
  if (!plan || typeof plan !== 'object') throw new Error('invalid apply plan');
  const appDir = path.resolve(String(plan.appDir || ''));
  const stagedDir = path.resolve(String(plan.stagedDir || ''));
  const backupDir = path.resolve(String(plan.backupDir || ''));
  const files = Array.isArray(plan.files) ? plan.files : [];
  if (files.length === 0) throw new Error('apply plan has no files');

  // 1) 预校验全部目标路径（必须位于安装目录内）并解析绝对路径
  //    校验内联在调用点：每个 rel 先逐段检查，再解析并强制限定在目录内
  const resolved = [];
  for (const f of files) {
    const rel = (f && f.path) ? String(f.path).replace(/\\/g, '/') : '';
    if (rel.length === 0) throw new Error('empty path in apply manifest');
    const parts = rel.split('/');
    if (parts.some((p) => p === '' || p === '.' || p === '..')) {
      throw new Error('unsafe path in apply manifest: ' + rel);
    }
    if (/^[A-Za-z]:/.test(parts[0])) {
      throw new Error('unsafe absolute path in apply manifest: ' + rel);
    }
    const dst = path.resolve(appDir, rel);
    if (dst !== appDir && !dst.startsWith(appDir + path.sep)) {
      throw new Error('path escapes install dir: ' + rel);
    }
    const src = path.resolve(stagedDir, rel);
    if (src !== stagedDir && !src.startsWith(stagedDir + path.sep)) {
      throw new Error('staged path escapes staged dir: ' + rel);
    }
    resolved.push({ rel, src, dst, sha256: f && f.sha256 });
  }

  // 2) 校验 staged 文件存在且 sha256 一致（缺 sha256 拒绝执行）
  for (const r of resolved) {
    if (!fs.existsSync(r.src)) throw new Error('staged file missing: ' + r.rel);
    if (typeof r.sha256 !== 'string' || !SHA256_HEX_RE.test(r.sha256)) {
      throw new Error('staged file missing sha256: ' + r.rel);
    }
    // 内联计算 sha256（r.src 已在上方同函数内校验过）
    const hash = crypto.createHash('sha256');
    hash.update(fs.readFileSync(r.src));
    if (hash.digest('hex').toLowerCase() !== r.sha256.toLowerCase()) {
      throw new Error('staged file sha256 mismatch: ' + r.rel);
    }
  }

  // 3) 备份 → 替换；任一步失败立即回滚
  fs.mkdirSync(backupDir, { recursive: true });
  const done = [];
  let failed = null;
  try {
    for (const r of resolved) {
      fs.mkdirSync(path.dirname(r.dst), { recursive: true });
      if (fs.existsSync(r.dst)) {
        // 备份路径：构造后立即校验必须位于 backupDir 内（拒绝 ../ 逃逸）
        const bak = path.resolve(backupDir, r.rel);
        if (bak !== backupDir && !bak.startsWith(backupDir + path.sep)) {
          throw new Error('unsafe backup path in apply manifest: ' + r.rel);
        }
        fs.mkdirSync(path.dirname(bak), { recursive: true });
        fs.copyFileSync(r.dst, bak);
      }
      fs.copyFileSync(r.src, r.dst);
      done.push(r);
    }
  } catch (err) {
    failed = err;
  }

  // 4) 失败回滚（尽力恢复已替换文件）
  if (failed) {
    let rollbackOk = true;
    for (const r of done) {
      // 回滚路径：构造后立即校验必须位于 backupDir 内；非法则跳过并标记不完整
      const bak = path.resolve(backupDir, r.rel);
      if (bak !== backupDir && !bak.startsWith(backupDir + path.sep)) {
        rollbackOk = false;
        continue;
      }
      try {
        if (fs.existsSync(bak)) fs.copyFileSync(bak, r.dst);
        else fs.rmSync(r.dst, { force: true });
      } catch (_) { rollbackOk = false; }
    }
    throw new Error('apply failed: ' + failed.message + (rollbackOk ? '' : ' (rollback incomplete!)'));
  }
  return { ok: true, replaced: done.length };
}

/**
 * 解析并校验更新目录（argv[2] 是唯一外部输入，必须先过白名单校验再使用）。
 * 安全规则：必须为绝对路径、路径段必须含 .nevo_update（与 updater.js getUpdateDir
 * 约定一致）、必须存在且为目录。任一不满足即抛错 —— 拒绝把不可信命令行参数
 * 用于后续任何文件读取/替换操作。
 */
function resolveUpdateDir(arg) {
  if (typeof arg !== 'string' || arg.length === 0) {
    throw new Error('update dir argument missing');
  }
  if (!path.isAbsolute(arg)) {
    throw new Error('update dir must be absolute: ' + arg);
  }
  const resolved = path.resolve(arg);
  if (!resolved.split(path.sep).includes('.nevo_update')) {
    throw new Error('update dir must reside under .nevo_update: ' + resolved);
  }
  if (!fs.existsSync(resolved) || !fs.statSync(resolved).isDirectory()) {
    throw new Error('update dir is not a directory: ' + resolved);
  }
  return resolved;
}

/** 入口：argv[2] 为 updateDir（由 apply_update.cmd 以 %~dp0 传入）。 */
function main() {
  let updateDir;
  try {
    updateDir = resolveUpdateDir(process.argv[2] || path.join(path.dirname(__dirname), '.nevo_update'));
  } catch (err) {
    // 参数缺失/非法：不执行任何文件操作，直接失败退出
    console.error('[apply_update] rejected update dir:', err.message);
    return 1;
  }
  const planPath = path.join(updateDir, MANIFEST_NAME);
  const resultPath = path.join(updateDir, RESULT_NAME);
  let result;
  try {
    const plan = JSON.parse(fs.readFileSync(planPath, 'utf-8'));
    const r = applyPlan(plan);
    result = { ok: true, replaced: r.replaced, timestamp: new Date().toISOString() };
  } catch (err) {
    result = { ok: false, error: err.message, timestamp: new Date().toISOString() };
  }
  try { fs.writeFileSync(resultPath, JSON.stringify(result, null, 2), 'utf-8'); } catch (_) {}
  return result.ok ? 0 : 1;
}

if (require.main === module) {
  process.exit(main());
}

module.exports = { MANIFEST_NAME, RESULT_NAME, isSafeRelPath, resolveWithin, resolveUpdateDir, applyPlan, main };
