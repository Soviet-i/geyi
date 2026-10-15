---
description: 收工：创建/更新今天的开发日志，并与学生核对内容
---

请为今天（或 $ARGUMENTS 指定的日期）整理开发日志，步骤：

1. 运行 `python scripts/new_devlog.py`；若文件已存在则运行 `python scripts/new_devlog.py --update` 刷新提交列表。
2. 阅读 `git log` 中当天的提交与 `git diff`，**只依据真实发生的事**起草各小节：今日完成、遇到的问题与解决、AI协作记录、物理验证、明日计划、耗时。
3. “AI协作记录”必须如实：我（agent）提出了什么、学生采纳/修改/否决了什么、我出过哪些错。不确定的部分向学生提问，不要编造。
4. 把草稿展示给学生，**请学生逐节确认或修改**，确认后再写入文件。
5. 提交：`docs(devlog): ...`，正文按 `.gitmessage` 的四项填写。
6. 最后运行 `python scripts/check_devlog.py` 并汇报结果。
