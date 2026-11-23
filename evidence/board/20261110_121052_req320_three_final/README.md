# REQ-320 基础交互修正固件与三端回归（2026-11-10）

## 固件

- `main.bin` **1,089,428 B** + LCPU **6,384 B**。
- SHA256 `d11267264fa9f9d1707a2a27454b469f8095603285d8792f4114059fad79e067`。
- 来源：`fix/final-integration` 工作树（协议 v4，`worktree_dirty=true`）；逐文件哈希见
  `firmware.json`。

## 烧录与启动

- 端口 COM7 / COM8 / COM5 均 `sftool -p <port> -b 1000000 config sftool_param.json`
  返回 0，原始日志 `flash_COM*.log`。
- 三块板启动均含 `core/input/ai/param/paramui/net/light=PASS`、`[geyi:lcd] READY 390 x 450`；
  BLE 本地 ID 分别为 `F33A` / `03EA` / `B7B0`，原始串口 `serial_boot_COM*.log`。

## 三端自动回归

命令：

```bash
python tools/net_spectator_test.py --port-a COM7 --port-b COM8 --port-c COM5
```

结果：**PASS**（`net_spectator_suite.log`，12:13）。A=`F33A` 房主、B=`03EA` 房客、
C=`B7B0` 旁观。覆盖准备→对局→结算→第二局、旁观只读/双方预览、房客 WAIT 重启回收、
房客 PLAYING 超时判红方胜、旁观者看到蓝方断线且倒计时递减、旁观瞬断 REJOIN、中途加入。
本套件使用 `gy_net move/place` 的 aim=0/己方底线兼容路径，不能替代真机倾斜手感测试。

## REQ-320 待学生真机复测

自动化只能证明主机/桌面/协议/启动路径。以下四项必须由学生在真实 IMU、屏幕和按键上操作：

1. **第二局**：单机→倾斜预览→认输→返回首页→第二局，连续倾斜不闪动/回弹，KEY2 只落子一次。
2. **数字-only**：某方向士兵全被卡住、数字仍可动时，倾斜仍出现“0 个”预览并可确认。
3. **方位选兵**：底行左/中两兵，向上偏左选左兵，正上选中兵；不同深度时方位优先。
4. **外围放兵**：金币足够后 KEY2 进入放兵，倾斜到四边/角，目标占用时沿行/列内缩；
   白边只能出现在实际可放的外边沿首空格，不能画整圈。
5. **方位实时切换**：从“上偏左”缓慢转到“上偏右”，不需要先回正，选中兵应从左兵切到右兵。
6. **屏幕按钮冲突**：倾斜预览中点击取消/增减/方向/放兵，预览不能消失/回弹；点击确认应落子。
   按钮预览时倾斜板子不应改变方向/数量。

串口可用 `[geyi:tilt] ... aim=` 与 `[geyi:place] ... row= col=` 核对；抓串口请用
`python tools/capture_serial.py --port COMx`（它在 open 前压住 DTR/RTS，不重置板子）。未取得学生反馈前，
不得把上述四项写成板端通过。

## 限制

- 真实触摸/像素、四类手感、满旁观容量/双旁观者 P-02 未验证。
- 协议 v4 与 v2/v3 不兼容，三块板必须使用本目录同一固件；PC `tools/pc_ble_peer.py`
  已升到 VERSION=4。
- 房主重启房间 RAM-only 消失；本轮未覆盖。
