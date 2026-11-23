# 联机客方无法落子：复现、根因与修复（2026-11-10）

## 现象

两块板联机：COM7 建房、COM5 加入。双方都能看到倾斜预览，但客方按 KEY2
后棋盘不前进，预览保留并提示“发送失败”；房主按 KEY2 正常。交换房主/房客
（含房间内换座）后，仍然是“客方”这一侧无法落子，与具体板子无关。

## 根因

`gy_net_app_preview()` 在每次移动预览时清理放兵状态：

```c
g.placing = 0;
g.place_row = -1;
g.place_col = -1;
```

客方 `gy_net_app_confirm()` 随后把所有字段塞进 `GyNetMoveReq`：

```c
mr.column = (uint8_t)g.place_col;   /* -1 -> 255 */
mr.row = (uint8_t)g.place_row;      /* -1 -> 255 */
```

而 `gy_net_pack_move_req()` 对普通移动也无条件校验
`column >= GY_MAX || row >= GY_MAX`，于是每条客方 MOVE_REQ 都在本地
打包阶段返回 `GY_NET_ERR_RANGE`。`gy_net_app_confirm()` 返回 0，UI 按设计
保留预览并提示“发送失败”，所以看起来是“按了没反应”。

房主路径不打包 MOVE_REQ，直接在本地 `gy_net_room_move()` 提交，因此房主
一直正常。放兵请求有对称问题：`mr.direction = (uint8_t)g.dir` 在进入放兵
时可能是 -1，协议层同样无条件校验 direction。

这个问题由 v4 增加 `row`/`aim` 字段、以及为修复 `placing` 残留而在预览时
写入 -1 哨兵这两次改动叠加引入，属于协议边界上的字段语义错误。

## 修复

1. `gy_net_pack_move_req()` / `gy_net_unpack_move_req()` 只校验请求类型
   真正使用的字段：
   - `kind=0`（移动）：direction/count/aim；
   - `kind=1`（放兵）：row/column。
   另一组字段是保留位，允许为 0xFF 哨兵。
2. 客方确认时只填写本类型使用的字段，不再把 -1 强转成 uint8_t；
   打包失败会打印 `confirm reject pack ...`，不再静默返回；同时检查
   `gy_net_ble_send()` 的返回值，发送队列满/正在离房时返回失败并保留预览，
   不再出现“UI 已清预览但包没进 TX 队列”。
3. `gy_net_app_preview()` 在把 direction/count/aim 转成 uint8_t/int8_t 之前
   校验原始 int，避免越界值回绕后看似合法。
4. 自动测试注入：`gy_net simtilt X Y` / `gy_net simkey` / `gy_net simoff`
   只替换 IMU 采样源与 KEY2 电平，后续的新鲜度判断、失效取消、按键防抖、
   `gy_input_update()`、`gy_ui_tilt_select()`、`GY_UI_CONFIRM` 路由完全不变。
   `gy_net simcoin N` 是测试夹具：由房主把双方金币设为 N 并广播快照，让放兵
   路径在第一回合就能被真实输入覆盖。
5. 修复 `gy_net place C` 测试命令：旧实现没有设置 UI 的 `ready/placing`，
   `gy_ui_action(GY_UI_CONFIRM)` 会在“没有可确认的选择”处提前返回，命令
   日志却仍打印 sent，属于假通过；现在走与触摸/倾斜选格相同的确认路径。
6. 新增 `tools/net_input_test.py`：自动烧录两块板、建房/加入/准备/开局，
   每一回合在轮到的一侧注入倾斜 + KEY2；开局后给双方各 4 金币，要求两块板
   都至少完成一次移动和一次放兵（放兵 = KEY2 进入、倾斜选外围格、KEY2 确认）。
   支持 `--host COM5`（COM5 当房主）与 `--swap`（房间内换座）。
7. `tests/test_net.c` 增加哨兵/分类型打包断言；旧代码会在该测试失败。

## 固件与测试

修复后固件（含同 revision 快照修复）：

- `main.bin` **1,093,852 B** + LCPU **6,384 B**
- SHA256 `a77716961d41e24434b07963302017e6115ff8de645fec32ebfa26824145cfc1`
- 烧录 COM7 / COM5 / COM8（COM8 未参与本轮联机断言，仅同步固件避免混版）。

主机测试：`python tools/run_host.py` 返回 0；`geyi-net-tests: 428 checks PASS`，
其余套件全部 PASS（`host_tests.log`）。

修复前复现（保留失败记录）：`repro_before_fix.log`，第 2 回合客方预览
`dir=0 count=0`、KEY2 已触发，但 revision 停在 1；客方串口只有
`[geyi:key]`，没有 `confirm reject`（打包失败在旧代码里静默返回）。
修复前固件为 1,092,268 B，SHA256
`0c2fe7da6bdab5de2c3f2424a4547bd1b5b3e1dcf2a27777ac1c43520ea2ca2f`。

修复后自动测试（均通过）：

| 场景 | 日志 | 结果 |
|---|---|---|
| COM7 房主 / COM5 客方 | `input_move_place_default.log` | 20 回合上限内完成；COM7 移动 8/放兵 2，COM5 移动 8/放兵 2 |
| COM5 房主 / COM7 客方 | `input_move_place_com5_host.log` | 15 回合；COM7 移动 6/放兵 1，COM5 移动 7/放兵 1 |
| 房间内换座（COM7 仍房主） | `input_move_place_seat_swap.log` | 19 回合；COM7 移动 7/放兵 2，COM5 移动 8/放兵 2 |
| 关机重启后复跑 | `input_postboot_*.log` | 两方向移动均通过（放兵覆盖见上面三组） |
| `gy_net place` 命令 | `place_command_both_roles.log` | 房主/客方各放兵一次，revision 均前进 |

每次放兵选格后故意停留 **1.6 s** 再按 KEY2，确保跨过房主 1 s 周期快照；
这验证了“同 revision 的周期快照不得清掉客方放兵状态”。

早期修复版（未含 review 跟进）的移动测试日志仍保留：`input_default.log`、
`input_com5_host.log`、`input_seat_swap.log`。

两板原有联机回归：`python tools/net_board_test.py --port-a COM7 --port-b COM5`
→ `PASS: two-board online suite`（`net_board_suite.log`）。

串口原始日志在 `serial/`；每个目录的 `firmware.json` 记录了当次固件哈希。

## 独立 review 跟进

第一轮独立 review（新上下文）结论：无 P0，核心修复成立；提出两条 P1：
1. `gy_net place C` 是静默 no-op，放兵修复未被双板套件真正覆盖；
2. `net_input_test.py` 可能在客方一次都没落子时打印 PASS。

两条均已修复：`place` 命令现在设置 UI ready/placing 并走同一确认路径；
测试脚本要求两块板各自至少完成一次移动和一次放兵，失败时也会发
`simtilt 0 0` + `simoff` 撤掉注入。review 另外指出的 aim 先转型后校验、
发送队列失败仍返回 1 也已处理。

第二轮独立 review（另一个新上下文）结论：无 P0/P1，核心修复成立，并确认
上一轮“stale IMU 会取消放兵”是**误报**（放兵期间 `nav.tilt` 恒为 0，
`gy_ui_tilt_cancel()` 直接返回）。它提出两条 P2：测试的 `counts` 把放兵也算
成移动、以及 `gy_ui_cell()` 忽略 `gy_net_app_place()` 的失败返回值。两条都已
修复：测试拆成 `moves`/`places` 两个计数器并分别断言；UI 只有网络层接受
放置时才设置 `ready`，否则提示“网络未就绪”，避免断线窗口重连后把放兵确认
错发成移动请求。为新增提示文字重新生成了 `geyi_font_cn16.c`。

增量复核又发现一个同区域 P2：房主每 1 s 的周期快照和 REJOIN 快照会推进
`seq` 但 `game.revision` 不变；旧代码仍把它当成“新棋盘”，清掉客方
`g.placing`，而 UI 的 `placing/ready` 还在，确认就会按移动请求发送。修复：
只有 `game.revision`、`outcome` 变化或原先 `game_valid=0` 的“真新棋盘”才清
放兵/预览状态；同 revision 的重同步快照只清 `pending_move`。测试在放兵选格后
停留 1.6 s 跨过周期快照再确认，覆盖此路径。

该增量修复又经同一独立 review 复核：**无 P0/P1/P2**。当前固件源码哈希与
`serial/input_20261110_194541/firmware.json` 一致。

## 学生真机确认（2026-11-10）

学生在最终固件（1,093,852 B）上完成真机复测，原话/结论：

> 现在真机测试很完美，各种倾斜、走字逻辑都很流畅且无异常。并且，联机功能也很正常，
> 各种进入退出房间、旁观、断线重连的功能也都正常。

据此关闭：真实倾斜/KEY2 落子、两种房主方向、房间内换座、放兵手势、第二局、
进出房间、旁观、断线重连的定性验收。该确认未附带仪器数值、独立截图或录屏，
因此不关闭：倾斜/触摸延迟与正确率、帧率、BLE 容量/吞吐、24 h soak、房主掉电/
精确 10 s 超时故障注入、参数板端生效、历史/计时。剩余项见 `docs/08 §4`。
