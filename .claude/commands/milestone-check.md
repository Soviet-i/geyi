---
description: 里程碑提交前自检：文档、追踪、测试、日志、manifest
---

请对里程碑 $ARGUMENTS 做提交前自检，并给出缺口清单（不要自行“修补”成虚假的内容）：

1. 运行 `python scripts/check_all.py --final`，逐项解释 FAIL/WARN。
2. 对照 `docs/00-项目计划与里程碑.md` 中该里程碑的“交付物”，逐项核对文档是否已更新、版本号与修订记录是否填写。
3. 检查需求→设计→测试→报告的追踪是否闭合（`python scripts/check_trace.py --final`）。
4. 检查 `manifest.yaml`：`physical_verification` 的每条证据文件是否真实存在；`test_results` 与 `docs/04-测试报告.md` 是否一致；`self_review` 是否如实。
5. 检查 `docs/devlog/` 在该里程碑期间是否存在断档（>7 天）并提示在日志中说明原因。
6. 汇总为表格：已完成 / 缺口 / 建议动作 / 需要学生本人完成的事（如真机验证）。
