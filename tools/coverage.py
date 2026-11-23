#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Per-module line coverage for host-testable C modules.

Each test binary is built in its own directory with gcc --coverage, run once,
then gcov is invoked on the module under test. This avoids merging gcda files
from different binaries and gives a per-module number that is easy to explain.

Usage:
    python tools/coverage.py            # report only
    python tools/coverage.py --min 70   # fail if any module is below 70%
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "coverage"
CC = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
GCOV = os.environ.get("GCOV") or shutil.which("gcov")

TARGETS = [
    # name, sources, module-under-test
    ("geyi-tests", ["src/geyi_game.c", "src/geyi_input.c", "tests/test_geyi.c"], "src/geyi_game.c"),
    ("input-tests", ["src/geyi_game.c", "src/geyi_input.c", "tests/test_input.c"], "src/geyi_input.c"),
    ("tilt-ux-tests", ["src/geyi_game.c", "src/geyi_input.c", "src/geyi_nav.c",
                       "tests/test_tilt_ux.c"], "src/geyi_nav.c"),
    ("config-tests", ["src/geyi_game.c", "src/geyi_input.c", "src/geyi_config.c", "tests/test_config.c"], "src/geyi_config.c"),
    ("param-ui-tests", ["src/geyi_game.c", "src/geyi_config.c", "src/geyi_param_ui.c", "tests/test_param_ui.c"], "src/geyi_param_ui.c"),
    ("ai-tests", ["src/geyi_game.c", "src/geyi_ai.c", "tests/test_ai.c"], "src/geyi_ai.c"),
    ("balance-tests", ["src/geyi_game.c", "tools/geyi_balance.c", "tests/test_balance.c"], "tools/geyi_balance.c"),
]
LINE_RE = re.compile(r"Lines executed:\s*([0-9.]+)%\s*of\s*(\d+)")


def run(args, cwd):
    proc = subprocess.run(args, cwd=cwd, text=True, encoding="utf-8",
                          errors="replace", stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    if proc.returncode:
        print(proc.stdout)
        raise SystemExit(f"command failed ({proc.returncode}): {' '.join(map(str, args))}")
    return proc.stdout


def measure(name, sources, module):
    build = OUT / name
    build.mkdir(parents=True, exist_ok=True)
    exe = build / (name + (".exe" if os.name == "nt" else ""))
    srcs = [str(ROOT / s) for s in sources]
    run([CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0", "-g", "--coverage",
         "-I" + str(ROOT / "src"), "-I" + str(ROOT / "tools"), *srcs, "-o", str(exe)], build)
    run([str(exe)], build)
    # GCC prefixes gcno/gcda with the executable name when compiling sources
    # directly to an executable; gcov expects <module>.gcno. Copy to the
    # conventional name without deleting the originals.
    stem = Path(module).stem
    for ext in (".gcno", ".gcda"):
        prefixed = build / f"{name}-{stem}{ext}"
        if prefixed.exists():
            shutil.copy2(prefixed, build / f"{stem}{ext}")
    output = run([GCOV, "-b", "-c", "-o", str(build), str(ROOT / module)], build)
    m = LINE_RE.search(output)
    if not m:
        return module, None, None, "gcov output not recognized"
    return module, float(m.group(1)), int(m.group(2)), ""


def main(argv):
    if not CC or not GCOV:
        print("gcc/gcov not found; install a C toolchain or set CC/GCOV.")
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, sources, module in TARGETS:
        print(f"== {name} -> {module}")
        rows.append(measure(name, sources, module))

    lines = ["| 模块 | 行覆盖率 | 可执行行 | 说明 |", "|---|---:|---:|---|"]
    failed = False
    min_pct = 0.0
    if "--min" in argv:
        min_pct = float(argv[argv.index("--min") + 1])
    for module, pct, total, note in rows:
        if pct is None:
            lines.append(f"| `{module}` | n/a | {total or 0} | {note} |")
            continue
        lines.append(f"| `{module}` | {pct:.1f}% | {total} | |")
        if pct < min_pct:
            failed = True
    text = "\n".join(lines)
    print("\n" + text)
    (OUT / "coverage.txt").write_text(text + "\n", encoding="utf-8")
    print(f"\nlog: {OUT / 'coverage.txt'}")
    if min_pct > 0:
        print(f"threshold: {min_pct:.1f}%")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
