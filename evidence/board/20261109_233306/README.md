# 2026-11-09 倾斜输入板端实测（COM7）

## 固件

- 最终 `main.bin` 1,085,716 B + LCPU 6,384 B，`source_head`/源文件哈希见 `firmware.json`。
- 烧录命令：`sftool -p COM7 -b 1000000 config sftool_param.json`（不依赖 VS Code/MCP）。
- 启动自检：core/input/param/paramui/net/light=PASS、LCD READY 390×450、`[geyi:imu] flat=gravity (Z inverted mount, no boot sampling)`。

## 诊断与修正过程（同一晚，逐步留证）

| 目录/日志 | 现象 | 结论 |
|---|---|---|
| `../20261109_225142/serial_boot.log` | `calibrated zero_x=17873`，`serial_diag.log` 中 x 在 -17917~+17888，60 条 motion、0 条 tilt | 开机采样把“斜着开机”当成零点；板端 `|x|>9000` 直接丢弃样本，倾斜完全无反应 |
| `../20261109_232423/serial_boot.log` | `zero=0,0` 后 x 仍约 ±178° | 只把零点设 0 不够：这块板 Z 轴朝下，`atan2(ax, az)` 平放就是 180° |
| `../20261109_233107/serial_boot.log` | `x=atan2(ax,-az)` 后平放 x=-90，左倾 -1187/-2290、右倾 +1214；tilt 事件出现 | 固定安装方向写进公式后，平放=0，倾斜正常；但左右相反 |
| 本目录 `serial_boot.log` | X 轴取反后左倾 x<0（dir=3）、右倾 x>0（dir=1） | 左右方向与屏幕一致（待学生目测确认） |
| 本目录 `serial_axis.log` | 四向各 3 秒：`x=-2845 dir=3`、`x=+2928 dir=1`、`y=-3080 dir=0`；向上 2→1→2→1 只发 UPDATE | 四向符号正确；1↔2 档位变化无 CANCEL |
| 本目录 `serial_turn.log` | 90 秒、91 条 motion、26 条 tilt（21 SELECT + 5 UPDATE，**0 CANCEL**）、7 次 KEY2；每次 KEY2 换回合后仍倾斜会再次 SELECT | 1↔2 不再闪“0 个兵”；换回合自动重预览；未出现 CANCEL |

## 边界

- 这是**真实 IMU + 真实按键**的串口日志，不是主机仿真；没有照片/视频，屏幕方向仍需学生目测确认。
- 本次未测：放兵选列（7 次 KEY2 均为 `now_placing=0`，未攒到 4 金币）、倾斜预览时点屏幕取消/增减/确认的冲突、真实倾斜正确率/延迟量化。
- `20261109_232523` 是另一块板（COM5）的旁观测试证据，不属于本次倾斜验证。
