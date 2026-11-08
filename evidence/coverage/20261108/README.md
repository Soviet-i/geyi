# 覆盖率证据（2026-11-08）

- 命令：`python tools/coverage.py --min 70`
- 结果：game 99.5%、input 87.0%、config 98.5%、param_ui 100%、ai 89.2%、balance 76.2%。
- 工具：MinGW-w64 gcc/gcov 14.2.0；每模块单独编译/运行/gcov，避免多二进制 gcda 覆盖。
- 说明：覆盖率是主机指标，不写成板端指标；未覆盖分支需在 `docs/04` 解释。
