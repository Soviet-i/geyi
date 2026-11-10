# 测试与验证指南（本项目）

> 目标：让“需求 → 用例 → 执行 → 证据”可重复、可追踪；不追求测试框架的花哨，
> 追求**同一条命令能复现**、**失败不被覆盖**、**主机/桌面/板端分层说清楚**。

## 1. 测试阶梯

| 层 | 命令 | 覆盖 | 不能证明 |
|---|---|---|---|
| L0 文档/追踪 | `python scripts/check_all.py` | 提交规范、日志、REQ 追踪、manifest | 代码正确性 |
| L1 主机逻辑 | `python tools/run_host.py` | 规则/输入/参数/参数页/AI/平衡，固定种子 | 真实 I/O、调度、内存 |
| L1b 覆盖率 | `python tools/coverage.py --min 70` | 核心模块行覆盖率 | 分支/路径覆盖、板端 |
| L2 桌面 UI | `python tools/build_desktop.py --test` | 页面导航、布局、鼠标/触摸路径 | 真机触摸/屏幕/帧率 |
| L3 编译 | `python scripts/build_firmware.py` | SDK v2.5.1 目标接口、固件体积 | 功能正确性 |
| L4 板端启动 | `python tools/flash_and_capture.py --assert` | 启动自检、LCD/IMU 标记 | 屏幕/触摸/倾斜手感 |
| L4b 板端功能 | 人工按 TC 操作 + 串口/照片/录屏 | 真实 UI、按键、IMU、联机 | 长时稳定性 |
| L5 长时 | 计划中的 `--soak` | 重启/内存/功能漂移 | 72–168 h 结论（本轮不做） |

一键入口：

```bash
python tools/verify_all.py                 # L0 + L1
python tools/verify_all.py --coverage      # + L1b
python tools/verify_all.py --desktop       # + L2（需 SDK + MSVC）
python tools/verify_all.py --board         # + L4（需板，人工输入 FLASH）
```

## 2. 新增一条主机测试

1. 先改 `docs/01` 的 REQ（如需要）与 `docs/03` 的 TC，写清预期/判据；
2. 在 `tests/` 新增 `test_xxx.c`，用 `CHECK(...)` 断言，`main()` 返回非 0 表示失败；
3. 在 `tools/run_host.py` 的 `targets` 增加一行（源码列表 + 可执行名）；
4. 运行 `python tools/run_host.py`，把输出贴到 `docs/04` 对应 TC；
5. 提交类型用 `test(scope)`，正文写清“怎么跑、期望什么、实际什么”。

要求：

- 测试直接链接 `src/` 的真实实现，不复制一份规则；
- 固定种子/固定局面，结果可复现；
- 失败必须保留原始输出，修好后新增“重测”记录，不覆盖失败记录；
- 不为通过而放宽断言；需求变更要先改 REQ/TC。

## 3. 新增一条 TC

`docs/03` 的 TC 编号按关联 REQ 组织（如 `TC-602-01` 对应 REQ-602）。每条至少写：

| 字段 | 示例 |
|---|---|
| 编号/关联需求 | `TC-602-01` / `REQ-602` |
| 前置条件 | 单机 6×6，红方先手，固定种子 |
| 步骤与输入 | 走 3 步后认输 |
| 预期 | 历史新增 1 条，字段 mode/size/result/... 正确 |
| 失败行为 | 不写记录/不崩溃/给提示 |
| 状态 | 未实现 / 未执行 / 通过 / 失败 |
| 证据 | `evidence/.../README.md`、串口/照片/日志 |

状态口径见 `docs/01` 的“状态口径”。**计划中的测试不能写成通过。**

## 4. 板端与物理验证

- 烧录、复位、传感器读数、屏幕效果、马达/焊接、蓝牙联网必须由学生在真机上完成；
  agent 只能准备脚本、解析日志、核对照片，不能假设通过。
- 烧录前先跑 `python tools/flash_and_capture.py --check` 做只读预检；
- 正式烧录用 `--assert`，缺 `core/input/param/paramui/lcd/imu` 标记时退出码 1；
- 证据放 `evidence/<模块>/<YYYYMMDD>/`，写 `README.md` 说明命令、时间、固件 SHA、
  照片/日志哈希；原始日志不要手工修饰；
- 失败/异常也保留，登记 `docs/06`。

## 5. 覆盖率

`tools/coverage.py` 为每个测试二进制单独编译、运行、gcov，按模块报告行覆盖率。
当前基线（2026-11-10，MinGW gcc 14.2.0；清理重构后）：

| 模块 | 行覆盖率 |
|---|---:|
| `src/geyi_game.c` | 99.5% |
| `src/geyi_input.c` | 91.3% |
| `src/geyi_config.c` | 98.5% |
| `src/geyi_param_ui.c` | 100.0% |
| `src/geyi_ai.c` | 89.3% |
| `tools/geyi_balance.c` | 76.5% |

CI 使用 `--min 70`；低于阈值会让 CI 失败。覆盖率不是目的：未覆盖的分支要能解释
（例如硬件错误路径、未实现的联机/历史分支）。

## 6. CI 做什么、不做什么

`course-check` 工作流在 Ubuntu 上运行：

- `check_all.py`（提交/日志/追踪/manifest）
- `run_host.py`（6 组主机断言）
- `coverage.py --min 70`（核心模块覆盖率）

**不跑**：桌面 LVGL（需要 SDK 3–4 GB + MSVC）、板端（无硬件）、双板 BLE。
这些必须在本地按 L2/L4 执行，并把证据写入 `evidence/`。

## 7. 常见坑

- **主机通过 ≠ 板端通过**：`run_host.py` 用 gcc，桌面用 MSVC/LVGL，板端用 ARM GCC；
  三者的调度、内存、屏幕完全不同。
- **编译通过 ≠ 功能正确**：编译只证明接口/链接，不证明行为。
- **桌面截图 ≠ 真机截图**：桌面 LVGL 渲染不等于 390×450 实际屏幕。
- **不要把计划写成通过**：未实现/未执行必须显式标注。
- **不要为了覆盖率写空断言**：断言要能失败，最好有一个“反例”测试。
- **`.gitignore` 的 `build/` 会匹配任意层级**：证据不要放 `evidence/build/`，
  放 `evidence/<模块>/`，提交后 `git status` 确认文件真的入库。
