# -*- coding: utf-8 -*-
"""课程检查脚本共用工具（仅依赖标准库）。"""
import datetime
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def setup_stdout():
    """Windows 终端默认编码可能不是 UTF-8，统一设置，避免中文乱码/报错。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def load_config():
    with open(ROOT / "course.config.json", encoding="utf-8") as f:
        return json.load(f)


def git(*args, check=True):
    """运行 git 命令并返回标准输出文本；失败且 check=True 时抛出 RuntimeError。"""
    p = subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True,
        encoding="utf-8", errors="replace",
    )
    if p.returncode != 0 and check:
        raise RuntimeError(p.stderr.strip() or f"git {' '.join(args)} 失败")
    return p.stdout


def in_git_repo():
    try:
        return git("rev-parse", "--is-inside-work-tree", check=False).strip() == "true"
    except FileNotFoundError:
        return False


def has_commits():
    return bool(git("rev-list", "-n", "1", "HEAD", check=False).strip())


def commit_list(rev_range=None):
    """返回提交列表（新→旧）：hash、作者本地日期、标题、正文、是否合并提交。"""
    if not in_git_repo() or not has_commits():
        return []
    fmt = "%H%x1f%ad%x1f%P%x1f%s%x1f%b%x1e"
    args = ["log", f"--format={fmt}", "--date=format:%Y-%m-%d"]
    if rev_range:
        args.append(rev_range)
    out = git(*args, check=False)
    commits = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\n")
        if not rec.strip():
            continue
        parts = rec.split("\x1f")
        if len(parts) < 5:
            continue
        h, date, parents, subject, body = parts[:5]
        commits.append({
            "hash": h.strip(),
            "date": date.strip(),
            "subject": subject.strip(),
            "body": body.strip(),
            "is_merge": len(parents.split()) > 1,
        })
    return commits


def today():
    return datetime.date.today().isoformat()
