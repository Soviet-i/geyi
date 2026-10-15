#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""需求追踪检查：PRD 中的每个 REQ-xxx 必须在方案设计、测试计划中出现；--final 时测试报告也必须覆盖。

用法：
    python scripts/check_trace.py
    python scripts/check_trace.py --final
"""
import re
import sys

from _common import ROOT, setup_stdout

DOCS = {
    "prd": "docs/01-需求规格说明.md",
    "design": "docs/02-方案设计说明.md",
    "plan": "docs/03-测试计划.md",
    "report": "docs/04-测试报告.md",
}
REQ_RE = re.compile(r"REQ-\d{3}")


def ids(path):
    p = ROOT / path
    return set(REQ_RE.findall(p.read_text(encoding="utf-8"))) if p.exists() else None


def main(argv):
    setup_stdout()
    final = "--final" in argv
    sets = {k: ids(v) for k, v in DOCS.items()}
    for k, v in sets.items():
        if v is None:
            print(f"✗ 找不到 {DOCS[k]}")
            return 1
    prd = sets["prd"]
    if not prd:
        print("✗ 需求规格说明中没有任何 REQ-xxx 编号")
        return 1

    errors, warns = [], []
    for name, label in (("design", "方案设计"), ("plan", "测试计划")):
        miss = sorted(prd - sets[name])
        if miss:
            errors.append(f"{label}未覆盖需求：{', '.join(miss)}")
    miss = sorted(prd - sets["report"])
    if miss:
        (errors if final else warns).append(f"测试报告未覆盖需求：{', '.join(miss)}")
    for name, label in (("design", "方案设计"), ("plan", "测试计划"), ("report", "测试报告")):
        orphan = sorted(sets[name] - prd)
        if orphan:
            warns.append(f"{label}引用了PRD中不存在的编号：{', '.join(orphan)}")

    print(f"需求追踪：PRD 共 {len(prd)} 条需求。")
    for e in errors:
        print(f"  ✗ {e}")
    for w in warns:
        print(f"  ! {w}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
