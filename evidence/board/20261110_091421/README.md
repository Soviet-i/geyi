# 2026-11-10 COM5 倾斜 / 放兵流程实测

## 固件

- `main.bin` 1,085,716 B + LCPU 6,384 B；烧录命令
  `sftool -p COM5 -b 1000000 config sftool_param.json`（直接串口，不依赖 VS Code/MCP）。
- 启动自检 core/input/param/paramui/net/light=PASS、LCD READY 390×450、
  `[geyi:imu] flat=gravity (Z inverted mount, no boot sampling)`；marker PASS。

## 60 秒日志（`serial_final.log`）

- 60 条 `[geyi:motion]`、23 条 `[geyi:tilt]`（11 SELECT + 12 UPDATE，**0 CANCEL**）、6 次 KEY2。
- 左右方向：左倾 x 为负（dir=3 LEFT），右倾 x 为正（dir=1 RIGHT）；向上 y 为负（dir=0 UP）。
- 1↔2：多次 `count=1↔2` 只发 `event=8` UPDATE，没有 `event=2` CANCEL。
- **放兵流程走通**：
  - `KEY2 was_placing=0 now_placing=1 revision=2 turn=0`（进入放兵）；
  - 放兵期间 motion x 在 -2233~+1688 之间变化（左右倾斜选列）；
  - `KEY2 was_placing=1 now_placing=0 revision=3 turn=1`（确认放兵，revision+1、换手）。
- 本轮 6 次 KEY2：2 次普通移动、1 次进入放兵、1 次确认放兵、2 次普通移动；
  每次换回合后仍倾斜都会再次 SELECT。

## 边界

- 这是真实 IMU/按键日志，不是照片/视频；**放兵高亮列是否跟手、屏幕取消/加减是否被忽略、
  屏幕确认是否只落子一次、左右方向最终目测**仍需学生确认。
- 没有量化倾斜正确率/延迟；COM7 的上一轮证据在 `../20261109_233306/`。
- 本次未记录放兵列号（固件没有该调试日志），只证明进入/确认流程与 revision 变化。
