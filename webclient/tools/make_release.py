#!/usr/bin/env python3
"""NEVO Web Client 发布辅助：生成 latest.json 清单与增量 delta.zip。

用法:
  python make_release.py \
    --to BETA0.0.2 \
    --to-dir <新版本解包目录> \
    --from-dir <旧版本解包目录> \
    --full-url <全量安装包 GitHub 下载 URL> \
    --full-size 52428800 \
    --out build/release

清单签名（Ed25519）:
  私钥从环境变量 NEVO_RELEASE_KEY_HEX 读取（64 字符 hex，Ed25519 32 字节种子）。
  未设置该变量时生成一次性密钥对（不落盘）并打印警告 —— 此类签名只供开发自测，
  客户端内置公钥无法验证，正式发布必须使用与客户端内置公钥匹配的私钥。
"""
import argparse
import hashlib
import json
import os
import zipfile
from datetime import datetime, timezone
from pathlib import Path

try:
    import nacl.signing as _nacl_signing
    NACL_AVAILABLE = True
except ImportError:  # pragma: no cover
    _nacl_signing = None
    NACL_AVAILABLE = False

_SIGNATURE_FIELD = "signature"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def walk_files(root: Path) -> dict:
    files = {}
    for p in sorted(root.rglob("*")):
        if p.is_file():
            rel = p.relative_to(root).as_posix()
            files[rel] = p
    return files


def canonical_bytes(manifest: dict) -> bytes:
    """规范化字节：去除 signature 字段后的紧凑 JSON 序列化。

    键序保持构造顺序（Python dict 与 JS JSON.parse/stringify 行为一致），
    与 updater.js 的 canonicalManifestBytes 完全对应。
    """
    obj = {k: v for k, v in manifest.items() if k != _SIGNATURE_FIELD}
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def load_signing_key() -> "_nacl_signing.SigningKey":
    """从 NEVO_RELEASE_KEY_HEX 加载私钥；缺失时生成一次性密钥对并警告（不落盘）。"""
    if not NACL_AVAILABLE:
        raise SystemExit(
            "错误：未安装 PyNaCl（pip install pynacl>=1.5.0），无法生成清单签名。"
        )
    hex_key = os.environ.get("NEVO_RELEASE_KEY_HEX", "").strip()
    if hex_key:
        if len(hex_key) != 64:
            raise SystemExit("错误：NEVO_RELEASE_KEY_HEX 必须为 64 字符 hex（32 字节 Ed25519 种子）")
        try:
            seed = bytes.fromhex(hex_key)
        except ValueError:
            raise SystemExit("错误：NEVO_RELEASE_KEY_HEX 不是合法 hex")
        return _nacl_signing.SigningKey(seed)
    key = _nacl_signing.SigningKey.generate()
    print("警告：未设置 NEVO_RELEASE_KEY_HEX，已生成一次性签名密钥（不落盘）。")
    print("  该签名的公钥不会被客户端内置公钥接受，仅用于本地自测；")
    print("  正式发布前请通过流水线注入正式私钥（64 字符 hex）。")
    return key


def sign_manifest(manifest: dict, key: "_nacl_signing.SigningKey") -> dict:
    """对规范化字节的 sha256 做 Ed25519 签名，返回带 signature 字段的新 dict。

    签名对象 = sha256(去除 signature 字段后的规范化 JSON 字节)，
    与 updater.js 的 crypto.verify(null, sha256(canonical), key, sig) 对应。
    """
    message = hashlib.sha256(canonical_bytes(manifest)).digest()
    signed = key.sign(message).signature
    out = dict(manifest)
    out[_SIGNATURE_FIELD] = signed.hex()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--to", required=True, help="目标版本号，如 BETA0.0.2")
    ap.add_argument("--from-version", default="", help="上一版本号（用于 delta.from，如 BETA0.0.1）")
    ap.add_argument("--to-dir", required=True, help="新版本 resources 解包目录")
    ap.add_argument("--from-dir", default=None, help="旧版本 resources 解包目录（用于增量）")
    ap.add_argument("--full-url", required=True, help="全量安装包 GitHub 下载 URL")
    ap.add_argument("--full-size", type=int, required=True, help="全量安装包字节数")
    ap.add_argument("--full-sha256", default="", help="全量安装包 SHA256（客户端强制校验，必填）")
    ap.add_argument("--changelog", default="", help="更新说明")
    ap.add_argument("--out", default="build/release", help="输出目录")
    args = ap.parse_args()

    # 安全要求：客户端（updater.js）强制校验 sha256，缺省值会导致所有客户端拒绝清单
    if not args.full_sha256:
        raise SystemExit("错误：必须提供 --full-sha256（客户端强制校验完整性，缺省值会导致清单被拒绝）")

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    to_dir = Path(args.to_dir)
    if not to_dir.is_dir():
        raise SystemExit(f"--to-dir 不存在: {to_dir}")

    signing_key = load_signing_key()

    new_files = walk_files(to_dir)
    files_meta = []
    for rel, p in new_files.items():
        files_meta.append({"path": rel, "sha256": sha256(p), "size": p.stat().st_size})

    full_package = {
        "url": args.full_url,
        "size": args.full_size,
        "sha256": args.full_sha256,
    }

    delta = None
    delta_files = []
    if args.from_dir:
        from_dir = Path(args.from_dir)
        old_files = walk_files(from_dir)
        changed = {
            rel: p for rel, p in new_files.items()
            if rel not in old_files
            or sha256(old_files[rel]) != sha256(p)
        }
        delta_files = sorted(changed)
        manifest = {
            "version": args.to,
            "files": files_meta,
            "full_package": full_package,
        }
        # delta 包内 manifest.json 同样签名（客户端解析时强制验证）
        manifest = sign_manifest(manifest, signing_key)
        # delta zip：仅含差异文件 + manifest.json
        delta_name = f"NEVO-delta-{args.from_version or 'prev'}-{args.to}.zip"
        delta_zip = out_dir / delta_name
        with zipfile.ZipFile(delta_zip, "w", zipfile.ZIP_DEFLATED) as z:
            for rel in delta_files:
                z.write(to_dir / rel, rel)
            z.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
        delta = {
            "from": args.from_version or "",
            "url": f"https://github.com/TNEllya/NEVO/releases/download/{args.to}/{delta_zip.name}",
            "size": delta_zip.stat().st_size,
            "sha256": sha256(delta_zip),
        }

    latest = {
        "version": args.to,
        "published_at": datetime.now(timezone.utc).isoformat(),
        "changelog": args.changelog,
        "files": files_meta,
        "full_package": full_package,
        "delta": delta,
    }
    latest = sign_manifest(latest, signing_key)
    latest_path = out_dir / "latest.json"
    latest_path.write_text(json.dumps(latest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"latest.json -> {latest_path}")
    print(f"files: {len(files_meta)}, delta files: {len(delta_files) if delta_files else 0}")
    print(f"签名公钥（hex）: {signing_key.verify_key.encode().hex()}")


if __name__ == "__main__":
    main()
