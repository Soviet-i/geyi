#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""一键检查：提交规范 / 开发日志 / 需求追踪 / manifest（--final 时增加固件编译）。

用法：
    python scripts/check_all.py            # 日常自查（manifest 问题只给警告）
    python scripts/check_all.py --final    # 里程碑/期末提交前：追踪覆盖测试报告、manifest 必须通过，并编译固件（无 SDK 环境时跳过）
"""
import subprocess
import sys

from _common import ROOT, setup_stdout


def run(args):
    p = subprocess.run([sys.executable, *args], cwd=ROOT, capture_output=True, encoding="utf-8", errors="replace")
    return p.returncode, (p.stdout + p.stderr).strip()


def main(argv):
    setup_stdout()
    final = "--final" in argv
    steps = [
        ("提交规范", ["scripts/check_commit_msg.py", "--log", "--quiet"], False),
        ("开发日志", ["scripts/check_devlog.py", "--strict"], False),
        ("需求追踪", ["scripts/check_trace.py"] + (["--final"] if final else []), False),
        ("manifest", ["scripts/check_manifest.py"], not final),
    ]
    if final:
        steps.insert(3, ("固件编译", ["scripts/build_firmware.py"], False))
    rows, failed = [], False
    for name, args, soft in steps:
        rc, out = run(args)
        if name == "固件编译" and rc == 2:
            status = "SKIP"
        elif rc == 0:
            status = "PASS"
        elif soft:
            status = "WARN"
        else:
            status = "FAIL"
            failed = True
        rows.append((name, status, out))

    print("=" * 56)
    for name, status, out in rows:
        mark = {"PASS": "✓", "WARN": "!", "SKIP": "-", "FAIL": "✗"}[status]
        print(f"{mark} {name:<8} {status}")
        if status != "PASS":
            for ln in out.splitlines()[:15]:
                print("    " + ln)
    print("=" * 56)
    print("结论：" + ("存在必须修复的问题，请处理后再提交。" if failed else "必检项全部通过。"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
