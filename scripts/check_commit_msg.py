#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""提交信息规范检查。

用法：
    python scripts/check_commit_msg.py <提交信息文件>      # 供 commit-msg 钩子调用
    python scripts/check_commit_msg.py --log [范围] [--quiet] # 检查已有历史（默认全部非合并提交）
    python scripts/check_commit_msg.py --text "完整提交信息"   # 直接检查一段文本

规范见 docs/guides/git-workflow.md。
"""
import re
import sys

from _common import commit_list, load_config, setup_stdout

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)\((?P<scope>[^)\s]+)\): (?P<subject>\S.*)$")
LABEL_RE = re.compile(r"^(背景|改动|验证|AI协作)\s*[:：]\s*(.*)$")
PLACEHOLDER_RE = re.compile(r"<[^>]*>")

EXAMPLE = (
    "feat(rgb): 新增 RGB 灯红绿蓝循环闪烁\n\n"
    "背景: REQ-002 要求用 RGB 灯指示设备运行状态。\n"
    "改动: main.c 中新增 rgb_led_init/set_color，按颜色表循环点亮。\n"
    "验证: 编译通过；上板后串口依次输出 red/green/blue，见 evidence/M2/rgb_log.txt。\n"
    "AI协作: 是(Claude Code)；采纳颜色表实现，人工修正了灯珠供电未打开的问题。"
)


def is_exempt(first_line):
    return first_line.startswith(("Merge ", "Revert \"", "fixup!", "squash!"))


def parse_sections(body_lines):
    sections, current = {}, None
    for line in body_lines:
        m = LABEL_RE.match(line.strip())
        if m:
            current = m.group(1)
            sections[current] = m.group(2).strip()
        elif current is not None and line.strip():
            sections[current] += " " + line.strip()
    return sections


def validate(msg, cfg):
    """返回错误列表；空列表表示通过。"""
    lines = [ln.rstrip() for ln in msg.splitlines() if not ln.lstrip().startswith("#")]
    while lines and not lines[0].strip():
        lines.pop(0)
    if not lines:
        return ["提交信息为空"]
    first = lines[0].strip()
    if is_exempt(first):
        return []

    errors = []
    m = HEADER_RE.match(first)
    if not m:
        errors.append(
            "标题格式应为 `<type>(<scope>): <主题>`（注意冒号后有一个空格），例如 `feat(rgb): 新增 RGB 灯红绿蓝循环闪烁`"
        )
        ctype = None
    else:
        ctype, subject = m.group("type"), m.group("subject").strip()
        if ctype not in cfg["commit_types"]:
            errors.append(f"type `{ctype}` 不在允许范围：{' '.join(cfg['commit_types'])}")
        if len(subject) < cfg["subject_min_len"]:
            errors.append(f"主题太短（至少 {cfg['subject_min_len']} 个字），请说清楚做了什么")
        if len(subject) > cfg["subject_max_len"]:
            errors.append(f"主题太长（{len(subject)} 字，上限 {cfg['subject_max_len']}），细节写进正文")
        if subject.lower() in cfg["vague_subjects"]:
            errors.append(f"主题 `{subject}` 过于笼统，请写出具体改了什么")
        if subject.endswith(("。", ".")):
            errors.append("主题末尾不要加句号")

    body_lines = lines[1:]
    if body_lines and body_lines[0].strip():
        errors.append("标题与正文之间需要空一行")
    sections = parse_sections(body_lines)

    required = ["改动", "AI协作"]
    if ctype in cfg["body_required_types"]:
        required = ["背景", "改动", "验证", "AI协作"]
    for label in required:
        if label not in sections:
            errors.append(f"缺少正文项 `{label}:`")
            continue
        content = sections[label]
        # “AI协作: 否” 本身很短但完全合法，其格式由下方专门规则检查
        if label != "AI协作" and len(re.sub(r"\s", "", content)) < cfg["section_min_chars"]:
            errors.append(f"`{label}:` 内容太少（至少 {cfg['section_min_chars']} 个字符），请写具体")
        if PLACEHOLDER_RE.search(content):
            errors.append(f"`{label}:` 中还有未替换的 <占位符>")

    ai = sections.get("AI协作")
    if ai is not None:
        if not ai.startswith(("否", "是")):
            errors.append("`AI协作:` 必须以“否”或“是”开头；选“是”需写明工具/模型、采纳了什么、人工修正了什么")
        elif ai.startswith("是") and len(re.sub(r"\s", "", ai)) < 10:
            errors.append("`AI协作: 是` 需写明工具/模型、采纳了什么、人工修正了什么（至少10个字符）")
    return errors


def report(errors, header):
    print(f"提交信息不符合规范：{header}", file=sys.stderr)
    for e in errors:
        print(f"  ✗ {e}", file=sys.stderr)
    print("\n正确示例：\n" + "\n".join("  " + ln for ln in EXAMPLE.splitlines()), file=sys.stderr)
    print("\n详见 docs/guides/git-workflow.md；可使用 `git config commit.template .gitmessage` 获得模板。", file=sys.stderr)


def check_log(rev_range, quiet):
    cfg = load_config()
    commits = [c for c in commit_list(rev_range) if not c["is_merge"]]
    bad = 0
    for c in commits:
        # git 的 %s/%b 已去掉标题与正文之间的空行，这里补回，保证与钩子校验口径一致
        msg = c["subject"] + "\n\n" + c["body"] if c["body"] else c["subject"]
        errs = validate(msg, cfg)
        if errs:
            bad += 1
            if not quiet or bad <= 5:
                print(f"✗ {c['hash'][:7]} {c['subject']}")
                for e in errs:
                    print(f"    - {e}")
    total = len(commits)
    print(f"提交规范：共检查 {total} 个提交，{bad} 个不合规。")
    return 1 if bad else 0


def main(argv):
    setup_stdout()
    cfg = load_config()
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "--log":
        quiet = "--quiet" in argv
        rest = [a for a in argv[1:] if not a.startswith("--")]
        return check_log(rest[0] if rest else None, quiet)
    if argv[0] == "--text":
        msg = argv[1] if len(argv) > 1 else ""
    else:
        with open(argv[0], encoding="utf-8", errors="replace") as f:
            msg = f.read()
    errors = validate(msg, cfg)
    if errors:
        first = next((ln for ln in msg.splitlines() if ln.strip() and not ln.startswith("#")), "")
        report(errors, first.strip())
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
