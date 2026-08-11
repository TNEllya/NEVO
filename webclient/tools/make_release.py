#!/usr/bin/env python3
"""NEVO Web Client 发布辅助：生成 latest.json 清单与增量 delta.zip。

用法:
  python make_release.py \
    --to BETA0.0.2 \
    --to-dir <新版本解包目录> \      # 例如 build/win-unpacked/resources
    --from-dir <旧版本解包目录> \    # 旧版本 resources 目录（可省略，省略则全量 files）
    --full-url https://github.com/TNEllya/NEVO/releases/download/BETA0.0.2/NEVO-Web-Client-BETA0.0.2-Setup.exe \
    --full-size 52428800 \
    --out build/release
"""
import argparse
import hashlib
import json
import os
import zipfile
from datetime import datetime, timezone
from pathlib import Path


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--to", required=True, help="目标版本号，如 BETA0.0.2")
    ap.add_argument("--from-version", default="", help="上一版本号（用于 delta.from，如 BETA0.0.1）")
    ap.add_argument("--to-dir", required=True, help="新版本 resources 解包目录")
    ap.add_argument("--from-dir", default=None, help="旧版本 resources 解包目录（用于增量）")
    ap.add_argument("--full-url", required=True, help="全量安装包 GitHub 下载 URL")
    ap.add_argument("--full-size", type=int, required=True, help="全量安装包字节数")
    ap.add_argument("--full-sha256", default="", help="全量安装包 SHA256（打包时计算，必填以启用校验）")
    ap.add_argument("--changelog", default="", help="更新说明")
    ap.add_argument("--out", default="build/release", help="输出目录")
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    to_dir = Path(args.to_dir)
    if not to_dir.is_dir():
        raise SystemExit(f"--to-dir 不存在: {to_dir}")

    new_files = walk_files(to_dir)
    files_meta = []
    for rel, p in new_files.items():
        files_meta.append({"path": rel, "sha256": sha256(p), "size": p.stat().st_size})

    full_sha = args.full_sha256
    full_package = {
        "url": args.full_url,
        "size": args.full_size,
        "sha256": full_sha,
    }

    delta = None
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
    latest_path = out_dir / "latest.json"
    latest_path.write_text(json.dumps(latest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"latest.json -> {latest_path}")
    print(f"files: {len(files_meta)}, delta files: {len(delta_files) if delta_files else 0}")


if __name__ == "__main__":
    main()
