#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""开发环境检查：以 SiFli SDK 为核心（黄山派 SF32LB52 + RT-Thread）。

用法：
    python scripts/check_env.py          # 检查并给出缺失项的处理办法
    python scripts/check_env.py --json   # 机器可读输出

必需项：git、SiFli SDK（环境变量 SIFLI_SDK 指向正确版本）、scons、ARM 交叉编译器。
可选项：uv（安装 SDK 时需要）。

注意：scons 与交叉编译器要在“运行过 export.ps1 / export.sh 的终端”里才找得到。
"""
import json
import os
import pathlib
import shutil
import sys

from _common import load_config, setup_stdout

OK, WARN, BAD = "ok", "warn", "missing"


def _which_any(names):
    for n in names:
        if n and shutil.which(n):
            return n
    return None


def collect():
    """返回 [(名称, 状态, 说明, 是否必需)]。"""
    cfg = load_config().get("sdk", {})
    prefix = cfg.get("version_prefix", "")
    items = []

    items.append(("git", OK if shutil.which("git") else BAD,
                  "已安装" if shutil.which("git") else "请安装 Git：https://git-scm.com/", True))

    sdk = os.environ.get("SIFLI_SDK", "")
    if not sdk:
        items.append(("SiFli SDK", BAD,
                      "环境变量 SIFLI_SDK 未设置。请在 SDK 根目录运行 export.ps1（Windows）/ . ./export.sh（Linux/macOS）；"
                      "尚未安装 SDK 则见 training/T0 的步骤", True))
    else:
        root = pathlib.Path(sdk)
        ver_file = root / "version.txt"
        if not root.is_dir() or not ver_file.is_file():
            items.append(("SiFli SDK", BAD, f"SIFLI_SDK={sdk} 不是有效的 SDK 目录（缺少 version.txt）", True))
        else:
            ver = ver_file.read_text(encoding="utf-8", errors="replace").strip()
            if prefix and not ver.startswith(prefix):
                items.append(("SiFli SDK", WARN,
                              f"版本 {ver} 与课程要求的 {cfg.get('sdk_tag', prefix)} 不一致（{sdk}）。请切换到该版本：git checkout {cfg.get('sdk_tag', '')}", True))
            else:
                items.append(("SiFli SDK", OK, f"版本 {ver}（{sdk}）", True))
            if not (root / "example" / "rt_device" / "rgb_led").is_dir():
                items.append(("SDK 完整性", WARN, "找不到 example/rt_device/rgb_led，请确认克隆完整（含子模块）", False))

    scons = shutil.which("scons")
    items.append(("scons", OK if scons else BAD,
                  scons or "未找到。请先在当前终端运行 SDK 的 export 脚本，再运行本检查", True))
    arm = _which_any(["arm-none-eabi-gcc"])
    items.append(("ARM 编译器", OK if arm else BAD,
                  "arm-none-eabi-gcc 可用" if arm else "未找到 arm-none-eabi-gcc。运行 SDK 的 install 脚本后再 export", True))

    items.append(("uv", OK if shutil.which("uv") else WARN,
                  "已安装" if shutil.which("uv") else "SDK 安装脚本需要 uv（见 T0 步骤）；已装好 SDK 环境可忽略", False))
    return items


def main(argv):
    setup_stdout()
    items = collect()
    if "--json" in argv:
        print(json.dumps([{"name": n, "status": s, "detail": d, "required": r} for n, s, d, r in items],
                         ensure_ascii=False, indent=2))
    else:
        mark = {OK: "✓", WARN: "!", BAD: "✗"}
        for n, s, d, r in items:
            print(f"{mark[s]} {n:<12}{'' if r else '(可选)'} {d}")
    required_bad = [n for n, s, d, r in items if r and s == BAD]
    if required_bad:
        if "--json" not in argv:
            print("\n必需项缺失：" + "、".join(required_bad) + "。修复方法见 training/T0-环境与仓库.md。")
        return 1
    if "--json" not in argv:
        print("\n环境就绪。下一步：python scripts/build_firmware.py")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
