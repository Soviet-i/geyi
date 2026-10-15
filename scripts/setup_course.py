#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""一键初始化：检查环境 → git init → 启用提交钩子与提交模板 → 生成符合规范的首次提交。

用法：
    python scripts/setup_course.py               # 完整初始化
    python scripts/setup_course.py --no-commit   # 只配置，不做首次提交
"""
import os
import shutil
import stat
import subprocess
import sys

from _common import ROOT, git, has_commits, in_git_repo, setup_stdout

INIT_MSG = (
    "chore(init): 初始化课程脚手架工程\n\n"
    "背景: 基于 course_starter 模板建立项目仓库，作为后续所有开发与文档的版本基线。\n"
    "改动: 导入目录结构、过程文档模板、入门训练、提交规范与开发日志检查脚本。\n"
    "验证: 提交信息通过 commit-msg 钩子检查；可运行 python scripts/check_all.py 查看初始状态。\n"
    "AI协作: 否\n"
)


def ok(msg):
    print(f"  ✓ {msg}")


def warn(msg):
    print(f"  ! {msg}")


def main(argv):
    setup_stdout()
    print("[1/5] 检查环境")
    if sys.version_info < (3, 8):
        print("  ✗ 需要 Python 3.8+")
        return 1
    ok(f"Python {sys.version.split()[0]}")
    if not shutil.which("git"):
        print("  ✗ 未找到 git，请先安装：https://git-scm.com/")
        return 1
    ok("git 已安装")
    try:
        import yaml  # noqa: F401
        ok("pyyaml 已安装")
    except ImportError:
        warn("未安装 pyyaml（check_manifest.py 需要）：pip install -r requirements.txt")
    import check_env
    for name, status, detail, required in check_env.collect():
        if name == "git":
            continue
        (ok if status == check_env.OK else warn)(f"{name}：{detail}")
    warn("SiFli SDK 环境不影响仓库初始化；缺失项按 training/T0 补装后，用 python scripts/check_env.py 复查")

    print("[2/5] 初始化 git 仓库")
    if in_git_repo():
        ok("已是 git 仓库")
    else:
        p = subprocess.run(["git", "init", "-b", "main"], cwd=ROOT, capture_output=True, encoding="utf-8")
        if p.returncode != 0:
            subprocess.run(["git", "init"], cwd=ROOT, check=True, capture_output=True)
            subprocess.run(["git", "checkout", "-b", "main"], cwd=ROOT, capture_output=True)
        ok("已创建仓库（分支 main）")

    print("[3/5] 启用提交规范")
    git("config", "core.hooksPath", ".githooks")
    git("config", "commit.template", ".gitmessage")
    git("config", "core.autocrlf", "input" if os.name != "nt" else "true", check=False)
    for hook in (ROOT / ".githooks").iterdir():
        try:
            hook.chmod(hook.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        except OSError:
            pass
    ok("已启用 .githooks（commit-msg / pre-commit / pre-push）与 .gitmessage 模板")

    print("[4/5] 检查 git 身份")
    name = git("config", "user.name", check=False).strip()
    email = git("config", "user.email", check=False).strip()
    if name and email:
        ok(f"{name} <{email}>")
    else:
        warn("尚未设置 git 身份，请执行：\n"
             '      git config --global user.name "你的姓名"\n'
             '      git config --global user.email "你的邮箱"\n'
             "    然后重新运行本脚本。")

    print("[5/5] 首次提交")
    if "--no-commit" in argv:
        warn("已按要求跳过首次提交")
    elif has_commits():
        ok("仓库已有提交，跳过")
    elif not (name and email):
        warn("身份未设置，已跳过首次提交")
    else:
        git("add", "-A")
        p = subprocess.run(["git", "commit", "-F", "-"], cwd=ROOT, input=INIT_MSG,
                           capture_output=True, encoding="utf-8", errors="replace")
        if p.returncode == 0:
            ok("已完成首次提交（这条提交本身就是规范示例，用 git log 看看）")
        else:
            warn("首次提交失败：" + (p.stderr or p.stdout).strip())

    print("\n下一步：阅读 README.md，然后开始 training/T0-环境与仓库.md")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
