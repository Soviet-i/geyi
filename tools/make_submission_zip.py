#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""打包课程提交 zip：保留完整 .git、docs 与 firmware，跳过构建产物。

用法（在仓库根目录）：
    python tools/make_submission_zip.py --out "../格弈-2024211401-2024211469.zip"

脚本会：
- 跳过 build/、project/build_*/、__pycache__、.pytest_cache、*.pyc、.sconsign.dblite；
- 保留 .git/（完整提交历史）、docs/、evidence/、logs/、firmware/*.bin；
- 检查 01/02/03/04/05 文档、README、.git、firmware/main.bin 是否存在；
- 打印文件数、压缩包大小，并提示 50 MB 限制。
"""
from __future__ import annotations

import argparse
import os
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIR_NAMES = {"build", "__pycache__", ".pytest_cache"}
SKIP_FILE_SUFFIXES = (".pyc", ".o", ".elf", ".map", ".hex", ".sconsign.dblite")
REQUIRED = [
    "README.md",
    ".git",
    "docs/01-需求规格说明.md",
    "docs/02-方案设计说明.md",
    "docs/03-测试计划.md",
    "docs/04-测试报告.md",
    "docs/05-研制总结.docx",
    "firmware/main.bin",
]


def is_skipped(rel: str, name: str) -> bool:
    parts = rel.split("/")
    if any(p in SKIP_DIR_NAMES for p in parts):
        return True
    if parts and parts[0] == "project" and len(parts) > 1 and parts[1].startswith("build_"):
        return True
    if name.endswith(SKIP_FILE_SUFFIXES):
        return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="打包课程提交 zip")
    ap.add_argument("--out", required=True, help="输出 zip 路径（建议放在仓库上一级）")
    ap.add_argument("--root-name", default="course_starter", help="zip 内顶层目录名")
    args = ap.parse_args()

    out = Path(args.out).expanduser().resolve()
    out.parent.mkdir(parents=True, exist_ok=True)

    missing = [p for p in REQUIRED if not (ROOT / p).exists()]
    if missing:
        print("缺少必需文件：")
        for p in missing:
            print(f"  - {p}")
        return 2

    count = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for dirpath, dirnames, filenames in os.walk(ROOT):
            rel_dir = os.path.relpath(dirpath, ROOT).replace(os.sep, "/")
            dirnames[:] = [
                d for d in dirnames
                if not is_skipped(f"{rel_dir}/{d}" if rel_dir != "." else d, d)
            ]
            for name in filenames:
                full = Path(dirpath) / name
                rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
                if is_skipped(rel, name):
                    continue
                try:
                    zf.write(full, f"{args.root_name}/{rel}")
                    count += 1
                except OSError as exc:
                    print(f"跳过无法读取的文件：{rel} ({exc})")

    size_mb = out.stat().st_size / 1024 / 1024
    print(f"已生成：{out}")
    print(f"文件数：{count}")
    print(f"大小：{size_mb:.1f} MB")
    if size_mb > 50:
        print("提示：超过课程建议的 50 MB。保留完整 .git 与 evidence 时，.git 本身已超过 50 MB。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
