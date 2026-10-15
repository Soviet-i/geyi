#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""开发日志检查：每个有提交的日期必须有对应的、内容充实的开发日志；并检测长期断档。

用法：
    python scripts/check_devlog.py             # 报告问题（默认不使退出码非0，除非 --strict 或 COURSE_STRICT=1）
    python scripts/check_devlog.py --strict
    python scripts/check_devlog.py --json      # 供教师侧评审agent读取
    python scripts/check_devlog.py --remind    # 供 pre-commit 钩子：今天没有日志时提醒
"""
import argparse
import datetime
import json
import os
import re
import sys

from _common import ROOT, commit_list, load_config, setup_stdout, today

AUTO_RE = re.compile(r"<!-- AUTO:COMMITS:START -->.*?<!-- AUTO:COMMITS:END -->", re.S)


def clean_lines(text):
    """去掉自动区块、HTML注释、引用提示、标题、占位符，仅保留学生真实写的文字。"""
    text = AUTO_RE.sub("", text)
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or s.startswith("#") or s.startswith(">") or set(s) <= set("|-: "):
            continue
        s = re.sub(r"<[^>]*>", "", s)
        s = re.sub(r"^[-*]\s*(\[[ xX]\])?\s*", "", s)
        s = s.replace("|", " ").strip()
        if s:
            out.append(s)
    return out


def split_sections(text):
    sections, cur = {}, None
    for ln in text.splitlines():
        m = re.match(r"^##\s+(.+?)\s*$", ln)
        if m:
            cur = m.group(1)
            sections[cur] = []
        elif cur is not None:
            sections[cur].append(ln)
    return {k: "\n".join(v) for k, v in sections.items()}


def effective_len(text):
    return len(re.sub(r"\s", "", "".join(clean_lines(text))))


def analyse(cfg):
    commits = [c for c in commit_list() if not c["is_merge"]]
    by_date = {}
    for c in commits:
        by_date.setdefault(c["date"], []).append(c)
    devlog_dir = ROOT / cfg["devlog_dir"]
    days = []
    for date in sorted(by_date):
        f = devlog_dir / f"{date}.md"
        item = {"date": date, "commits": len(by_date[date]), "devlog_exists": f.exists(),
                "effective_chars": 0, "issues": []}
        if not f.exists():
            item["issues"].append("缺少开发日志")
        else:
            text = f.read_text(encoding="utf-8")
            item["effective_chars"] = effective_len(text)
            if item["effective_chars"] < cfg["devlog_min_effective_chars"]:
                item["issues"].append(
                    f"日志过于简略（有效字数 {item['effective_chars']} < {cfg['devlog_min_effective_chars']}）")
            secs = split_sections(text)
            for name, need in cfg["devlog_required_sections"].items():
                got = effective_len(secs.get(name, ""))
                if name not in secs:
                    item["issues"].append(f"缺少小节「{name}」")
                elif got < need:
                    item["issues"].append(f"小节「{name}」内容不足（{got} < {need} 字）")
        days.append(item)

    gaps = []
    dates = [datetime.date.fromisoformat(d["date"]) for d in days]
    for a, b in zip(dates, dates[1:]):
        if (b - a).days > cfg["max_gap_days"]:
            gaps.append({"from": a.isoformat(), "to": b.isoformat(), "days": (b - a).days})
    stale = None
    if dates:
        idle = (datetime.date.today() - dates[-1]).days
        if idle > cfg["max_gap_days"]:
            stale = {"last_commit": dates[-1].isoformat(), "idle_days": idle}
    return {"days": days, "gaps": gaps, "stale": stale}


def remind():
    cfg = load_config()
    f = ROOT / cfg["devlog_dir"] / f"{today()}.md"
    if not f.exists():
        print(f"[提醒] 今天（{today()}）还没有开发日志。收工前请运行：python scripts/new_devlog.py", file=sys.stderr)
    return 0


def main():
    setup_stdout()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--remind", action="store_true")
    args = ap.parse_args()
    if args.remind:
        return remind()

    cfg = load_config()
    res = analyse(cfg)
    problems = [d for d in res["days"] if d["issues"]]
    summary = {
        "commit_days": len(res["days"]),
        "devlog_days": sum(1 for d in res["days"] if d["devlog_exists"]),
        "days_with_issues": len(problems),
        "gaps": len(res["gaps"]),
        "stale": bool(res["stale"]),
        "total_commits": sum(d["commits"] for d in res["days"]),
    }
    res["summary"] = summary

    if args.json:
        print(json.dumps(res, ensure_ascii=False, indent=2))
    else:
        print(f"开发日志：{summary['commit_days']} 个开发日（共 {summary['total_commits']} 次提交），"
              f"{summary['devlog_days']} 个有日志，{summary['days_with_issues']} 个有问题。")
        for d in problems:
            print(f"  ✗ {d['date']}（{d['commits']} 次提交）: " + "；".join(d["issues"]))
        for g in res["gaps"]:
            print(f"  ! {g['from']} → {g['to']} 间隔 {g['days']} 天无提交（阈值 {cfg['max_gap_days']} 天），请在日志中说明原因")
        if res["stale"]:
            s = res["stale"]
            print(f"  ! 距上次提交（{s['last_commit']}）已 {s['idle_days']} 天，请尽快恢复小步提交")
    strict = args.strict or os.environ.get("COURSE_STRICT") == "1"
    bad = bool(problems) or bool(res["gaps"]) or bool(res["stale"])
    return 1 if (strict and bad) else 0


if __name__ == "__main__":
    sys.exit(main())
