#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""创建/刷新某天的开发日志。

用法：
    python scripts/new_devlog.py                  # 创建今天的日志（已存在则提示）
    python scripts/new_devlog.py --date 2026-10-12
    python scripts/new_devlog.py --update         # 只刷新“今日提交”自动区块，不动你写的内容

“今日提交”区块由脚本从 git 历史自动生成（位于 AUTO 标记之间），不计入日志有效字数，
“今日完成/遇到的问题/AI协作记录/明日计划”需要你（或在你确认后由agent）如实填写。
"""
import argparse
import datetime
import re
import sys

from _common import ROOT, commit_list, git, in_git_repo, load_config, setup_stdout, today

START = "<!-- AUTO:COMMITS:START -->"
END = "<!-- AUTO:COMMITS:END -->"
WEEKDAYS = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]


def commit_block(date):
    lines = [START, "<!-- 以下由 new_devlog.py 自动生成，请勿手改；用 --update 刷新 -->"]
    commits = [c for c in commit_list() if c["date"] == date and not c["is_merge"]]
    if commits:
        for c in reversed(commits):
            lines.append(f"- `{c['hash'][:7]}` {c['subject']}")
    else:
        lines.append("- （当日暂无提交）")
    lines.append(END)
    return "\n".join(lines)


def author_name():
    if not in_git_repo():
        return "<姓名>"
    name = git("config", "user.name", check=False).strip()
    return name or "<姓名>"


def main():
    setup_stdout()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--date", default=today())
    ap.add_argument("--update", action="store_true", help="仅刷新自动生成的提交区块")
    args = ap.parse_args()

    cfg = load_config()
    try:
        d = datetime.date.fromisoformat(args.date)
    except ValueError:
        print("日期格式应为 YYYY-MM-DD", file=sys.stderr)
        return 2
    devlog_dir = ROOT / cfg["devlog_dir"]
    target = devlog_dir / f"{args.date}.md"
    block = commit_block(args.date)

    if target.exists():
        if not args.update:
            print(f"{target.relative_to(ROOT)} 已存在；如只想刷新提交列表请加 --update。")
            return 0
        text = target.read_text(encoding="utf-8")
        if START in text and END in text:
            text = re.sub(re.escape(START) + r".*?" + re.escape(END), lambda _: block, text, flags=re.S)
            target.write_text(text, encoding="utf-8")
            print(f"已刷新 {target.relative_to(ROOT)} 的提交列表。")
            return 0
        print("日志中找不到 AUTO 标记，无法自动刷新，请手动补充提交列表。", file=sys.stderr)
        return 1

    tpl = (devlog_dir / "_TEMPLATE.md").read_text(encoding="utf-8")
    content = (tpl.replace("{{DATE}}", args.date)
                  .replace("{{WEEKDAY}}", WEEKDAYS[d.weekday()])
                  .replace("{{AUTHOR}}", author_name()))
    content = re.sub(re.escape(START) + r".*?" + re.escape(END), lambda _: block, content, flags=re.S)
    devlog_dir.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")
    print(f"已创建 {target.relative_to(ROOT)}，请补全各小节后随代码一起提交（type 用 docs，scope 用 devlog）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
