# 开发日志

每个**有 git 提交的日期**必须有一篇 `YYYY-MM-DD.md`。创建：

```bash
python scripts/new_devlog.py            # 今天
python scripts/new_devlog.py --date 2026-10-12   # 补写某天（要在日志中注明“补写”及原因）
python scripts/new_devlog.py --update   # 只刷新“今日提交”自动区块
```

检查：`python scripts/check_devlog.py`（缺失、过于简略、小节为空、长期断档都会报告）。

写法要点见 `../guides/devlog-guide.md`。`_TEMPLATE.md` 是模板，不要删除，也不要把它当作日志提交。
