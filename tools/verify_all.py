#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""One-command verification entry point.

Default (no flags): docs checks + host tests.
Optional layers:
    --coverage   per-module gcov coverage (threshold 70%)
    --desktop    desktop LVGL --test (requires SDK + MSVC)
    --board      flash + boot self-test assertions (requires board; asks FLASH)
    --evidence   also copy the log to evidence/verify/YYYYMMDD/

Examples:
    python tools/verify_all.py
    python tools/verify_all.py --coverage
    python tools/verify_all.py --desktop --evidence
"""
from pathlib import Path
import datetime
import os
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
PY = sys.executable
LOG = ROOT / "build" / "verify" / "verify.log"


def run(name, args, allow_fail=False):
    print(f"\n===== {name} =====", flush=True)
    proc = subprocess.run(args, cwd=ROOT, text=True, encoding="utf-8",
                          errors="replace", stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    print(proc.stdout, end="", flush=True)
    ok = proc.returncode == 0
    if not ok and not allow_fail:
        print(f"[FAIL] {name} (exit {proc.returncode})", flush=True)
    return name, ok, proc.returncode, proc.stdout


def main(argv):
    results = []
    results.append(run("课程检查（提交/日志/追踪/manifest）", [PY, "scripts/check_all.py"]))
    results.append(run("主机测试（6 组断言）", [PY, "tools/run_host.py"]))
    if "--coverage" in argv:
        results.append(run("覆盖率（核心模块 ≥70%）", [PY, "tools/coverage.py", "--min", "70"]))
    if "--desktop" in argv:
        results.append(run("桌面 LVGL --test", [PY, "tools/build_desktop.py", "--test"]))
    if "--board" in argv:
        print("\n[注意] --board 会调用 flash_and_capture.py，需要人工输入 FLASH 并烧录。",
              flush=True)
        results.append(run("板端烧录 + 启动自检断言",
                           [PY, "tools/flash_and_capture.py", "--assert"]))

    lines = ["", "===== 汇总 ====="]
    failed = 0
    for name, ok, code, _ in results:
        lines.append(f"{'PASS' if ok else 'FAIL'}  {name}  (exit {code})")
        failed += 0 if ok else 1
    lines.append(f"结论：{'全部通过' if failed == 0 else f'{failed} 项失败'}")
    text = "\n".join(lines) + "\n"
    print(text, end="")

    LOG.parent.mkdir(parents=True, exist_ok=True)
    body = "\n".join(["# verify_all log", datetime.datetime.now().isoformat(timespec="seconds"),
                      *[out for _, _, _, out in results], text])
    LOG.write_text(body, encoding="utf-8")
    print(f"log: {LOG}")
    if "--evidence" in argv:
        stamp = datetime.date.today().isoformat()
        target = ROOT / "evidence" / "verify" / stamp
        target.mkdir(parents=True, exist_ok=True)
        (target / "verify.log").write_text(body, encoding="utf-8")
        print(f"evidence: {target / 'verify.log'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
