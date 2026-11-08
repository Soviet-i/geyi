# 主机测试与覆盖率证据（2026-11-08）

- 提交：`docs/req-5xx-gap-fill` 分支；具体 HEAD 见提交说明。
- 命令：
  - `python tools/run_host.py`
  - `python tools/coverage.py --min 70`
  - `python tools/verify_all.py --coverage`
- 结果：6 组断言 26920/60/2517/85/62/10 PASS；覆盖率 game 99.5%、input 87.0%、
  config 98.5%、param_ui 100%、ai 89.2%、balance 76.2%；`verify_all` 三项 PASS。
- 说明：这是**主机**证据，用 gcc 14.2.0 构建；不代表板端/桌面 LVGL。
- 文件：`host_test.log`、`verify.log`；覆盖率表见 `evidence/coverage/20261108/coverage.txt`。
