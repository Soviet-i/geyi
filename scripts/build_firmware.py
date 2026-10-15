#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""编译板级工程（SDK 工程目录 project/，应用源码 src/；scons），并提示下载命令。

用法：
    python scripts/build_firmware.py                 # 按 course.config.json 的板型编译
    python scripts/build_firmware.py --board 板型名   # 指定其他板型
    python scripts/build_firmware.py --clean         # 清理后重编
    python scripts/build_firmware.py -j4             # 指定并行数（默认 8）

返回码：0 成功；1 编译失败；2 环境未就绪（先运行 check_env.py）。
编译日志保存到 build/last_build.log（不入库）；需要留证时把它复制到 evidence/ 再提交。
"""
import os
import shutil
import subprocess
import sys

from _common import ROOT, load_config, setup_stdout


def main(argv):
    setup_stdout()
    cfg = load_config()["sdk"]
    board, jobs, clean = cfg["board"], "8", "--clean" in argv
    for i, a in enumerate(argv):
        if a == "--board" and i + 1 < len(argv):
            board = argv[i + 1]
        if a.startswith("-j") and len(a) > 2:
            jobs = a[2:]

    if not os.environ.get("SIFLI_SDK") or not shutil.which("scons"):
        print("✗ SDK 环境未就绪（SIFLI_SDK 或 scons 缺失）。请先在 SDK 根目录运行 export 脚本，再运行 python scripts/check_env.py")
        return 2

    proj = ROOT / cfg["project_dir"]
    if not proj.is_dir():
        print(f"✗ 找不到工程目录 {proj}")
        return 2

    if clean:
        for d in list(proj.glob("build_*")) + [proj / "sifli_sdk", proj / "src"]:
            shutil.rmtree(d, ignore_errors=True)
        print("已清理旧的编译输出")

    cmd = ["scons", f"--board={board}", f"-j{jobs}"]
    print(f"在工程目录 project/ 中执行：{' '.join(cmd)}")
    log_dir = ROOT / "build"
    log_dir.mkdir(exist_ok=True)
    with open(log_dir / "last_build.log", "w", encoding="utf-8", errors="replace") as log:
        p = subprocess.Popen(cmd, cwd=proj, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             encoding="utf-8", errors="replace")
        for line in p.stdout:
            print(line, end="")
            log.write(line)
        p.wait()

    if p.returncode != 0:
        print("\n✗ 编译失败。完整日志：build/last_build.log。把错误原文交给 agent 前，先自己读一遍报错的第一处。")
        return 1

    out = next(iter(sorted(proj.glob(f"build_{board}*"))), None)
    ext = "bat" if os.name == "nt" else "sh"
    print("\n✓ 编译成功")
    if out:
        rel = out.relative_to(ROOT)
        print(f"  输出目录：{rel}")
        print(f"  下载（板子用 USB 连到 USB-to-UART 口）：{rel / ('uart_download.' + ext)}")
        print("  下载后用串口助手（波特率 1000000）查看日志，按 docs/guides/agent-collaboration.md 第 4 节留证。")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
