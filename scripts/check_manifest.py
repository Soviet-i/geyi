#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
manifest.yaml 完整性自检脚本

用法:
    python scripts/check_manifest.py [manifest文件路径]
    不传路径时默认检查仓库根目录下的 manifest.yaml

依赖:
    pip install pyyaml

用途:
    在提交前快速检查 manifest.yaml 是否还有占位符（形如 <...>）没有替换、
    引用的证据文件是否真实存在。不能替代人工/Eval Agent的完整评审，
    但可以在提交前先自查一遍，避免因为忘填字段被打回。
"""
import sys
import pathlib

try:
    import yaml
except ImportError:
    print("缺少依赖，请先运行: pip install pyyaml")
    sys.exit(1)


def is_placeholder(value):
    """判断字段是否还是模板占位符（空值，或形如 <...> 的提示文字）"""
    if value is None:
        return True
    if isinstance(value, str):
        s = value.strip()
        if s == "":
            return True
        if s.startswith("<") and s.endswith(">"):
            return True
    return False


def file_exists(repo_root, rel_path):
    if not rel_path or is_placeholder(rel_path):
        return None  # 无法判断，交由上层报告"未填写"
    return (repo_root / rel_path).exists()


def check(data, repo_root):
    issues = []
    warnings = []

    if not data:
        issues.append("manifest.yaml 为空或解析失败")
        return issues, warnings

    # 1. submission 基本信息
    sub = data.get("submission") or {}
    for field in ["student_id", "student_name", "group_id", "milestone", "submitted_at"]:
        if is_placeholder(sub.get(field)):
            issues.append(f"submission.{field} 未填写")

    # 2. dev_agent
    dev = data.get("dev_agent") or {}
    for field in ["tool", "model", "session_log_path"]:
        if is_placeholder(dev.get(field)):
            issues.append(f"dev_agent.{field} 未填写")
    log_ok = file_exists(repo_root, dev.get("session_log_path"))
    if log_ok is False:
        issues.append(f"dev_agent.session_log_path 指向的文件不存在: {dev.get('session_log_path')}")

    # 3. requirements
    req = data.get("requirements") or {}
    if is_placeholder(req.get("prd_path")):
        issues.append("requirements.prd_path 未填写")
    else:
        ok = file_exists(repo_root, req.get("prd_path"))
        if ok is False:
            issues.append(f"requirements.prd_path 指向的文件不存在: {req.get('prd_path')}")
    if is_placeholder(req.get("hardware_constraints_ref")):
        warnings.append("requirements.hardware_constraints_ref 未填写（建议引用黄山派硬件规格文档）")

    # 4. design
    design = data.get("design") or {}
    if is_placeholder(design.get("architecture_doc_path")):
        issues.append("design.architecture_doc_path 未填写")
    else:
        ok = file_exists(repo_root, design.get("architecture_doc_path"))
        if ok is False:
            issues.append(f"design.architecture_doc_path 指向的文件不存在: {design.get('architecture_doc_path')}")

    key_decisions = design.get("key_decisions") or []
    if len(key_decisions) == 0:
        issues.append("design.key_decisions 为空——至少应记录1条关键决策及取舍理由")
    for i, kd in enumerate(key_decisions):
        for field in ["decision", "alternatives_considered", "rationale", "decided_by"]:
            if is_placeholder((kd or {}).get(field)):
                issues.append(f"design.key_decisions[{i}].{field} 未填写")

    # 5. code
    code = data.get("code") or {}
    if is_placeholder(code.get("repo_url")):
        warnings.append("code.repo_url 未填写")
    if is_placeholder(code.get("commit_range")):
        issues.append("code.commit_range 未填写")

    # 6. physical_verification —— 全课程最核心的一项，不能为空
    pv = data.get("physical_verification") or []
    if len(pv) == 0:
        issues.append("physical_verification 为空——至少应有1条物理验证记录（这是本课程最重要的真实性证据）")
    for i, item in enumerate(pv):
        item = item or {}
        for field in ["item", "required_action", "executed_at", "evidence_path", "agent_conclusion"]:
            if is_placeholder(item.get(field)):
                issues.append(f"physical_verification[{i}].{field} 未填写")
        ok = file_exists(repo_root, item.get("evidence_path"))
        if ok is False:
            issues.append(f"physical_verification[{i}].evidence_path 指向的文件不存在: {item.get('evidence_path')}")

    # 7. test_results
    tr = data.get("test_results") or []
    if len(tr) == 0:
        issues.append("test_results 为空——至少应有1条测试记录")
    for i, t in enumerate(tr):
        t = t or {}
        for field in ["test_id", "description", "expected", "actual"]:
            if is_placeholder(t.get(field)):
                issues.append(f"test_results[{i}].{field} 未填写")
        if not isinstance(t.get("pass"), bool):
            issues.append(f"test_results[{i}].pass 应为 true/false 布尔值，当前未正确填写")
        ok = file_exists(repo_root, t.get("evidence_path"))
        if ok is False:
            issues.append(f"test_results[{i}].evidence_path 指向的文件不存在: {t.get('evidence_path')}")

    # 8. known_issues
    ki = data.get("known_issues") or []
    if len(ki) == 0:
        warnings.append("known_issues 为空——真实项目通常都有已知局限，主动说明比不写更可信")

    # 9. self_review
    if is_placeholder(data.get("self_review")):
        issues.append("self_review 未填写")

    # 10. human_checkpoints
    hc = data.get("human_checkpoints") or []
    if len(hc) == 0:
        warnings.append("human_checkpoints 为空——建议至少记录1条学生对关键决策的确认说明")

    return issues, warnings


def main():
    manifest_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("manifest.yaml")
    if not manifest_path.exists():
        print(f"找不到文件: {manifest_path}")
        sys.exit(1)

    repo_root = manifest_path.resolve().parent
    data = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    issues, warnings = check(data, repo_root)

    print(f"检查文件: {manifest_path}\n")
    if not issues and not warnings:
        print("完整性检查通过，未发现缺失字段。")
    else:
        if issues:
            print(f"发现 {len(issues)} 项必须补全的问题：")
            for i in issues:
                print(f"  [必须] {i}")
        if warnings:
            print(f"\n{len(warnings)} 项建议补充（不阻塞提交，但会影响过程分评价）：")
            for w in warnings:
                print(f"  [建议] {w}")
    print()
    sys.exit(1 if issues else 0)


if __name__ == "__main__":
    main()
