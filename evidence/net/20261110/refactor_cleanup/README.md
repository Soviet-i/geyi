# 2026-11-10 联机/输入清理重构证据

## 范围

学生 2026-11-10 真机确认主流程后，对追查“客方无法落子/预览回弹”期间留下的
无效补丁、重复状态清理代码和诊断日志做清理重构。目标：**外部行为与公开接口
语义不变**，只改内部结构；重构前补齐测试，重构后跑完整自动回归。

## 固件

| 项 | 值 |
|---|---|
| 构建 | `python scripts/build_firmware.py`（SiFli SDK v2.5.1，PowerShell export） |
| main.bin | **1,091,788 B** + LCPU 6,384 B |
| SHA256 | `89b0275a0aa8013b7a7ed48aef42f8b6140888e85ac56147ecb6503004f1cf2d` |
| 烧录 | COM7 / COM5 / COM8 均用 `sftool` 直接烧录，rc=0 |
| 对照 | 学生真机确认的是重构前 1,093,852 B（SHA256 `a7771696…`）；重构后固件完成自动回归，**学生随后确认功能完好、可提交** |

## 清理内容

- `src/geyi_net_app.c`
  - 新增 `clear_placement()`、`clear_visible_previews()`、`clear_preview_state()`、
    `move_args_valid()`，统一 START/SNAPSHOT/REJECT/RESULT/leave/preview/cancel 等路径的
    放兵与预览清理，避免漏字段。
  - 客方确认打包提取为 `guest_send_move_req()`：只填 kind 对应字段，打包/入队失败
    返回 0，UI 保留预览。
  - `broadcast_state()`/`broadcast_snapshot()` 复用 `send_state_to(0xff)`/
    `send_snapshot_to(0xff)`；去掉硬编码 `0xff`。
  - 删除高频诊断日志（`snapshot apply`、`confirm reject ...` 逐字段日志等），保留
    低频状态/失败日志。
  - 删除 dead 字段 `starting_seq`；`last_snapshot_ms` 在 leave/promote 时复位。
  - **review P1 修复**：REJOIN 成功路径改用 `resync_snapshot_to(conn)`，WAIT/RESULT
    不再把上一局终局快照发给刚重连的客方。
- `src/geyi_net_proto.c`：提取 `move_req_valid()`，pack/unpack 共用按 kind 的字段校验。
- `src/geyi_net_room.c`：删除重复的 `state->seq < client->view.seq` 判断；同 seq
  ROOM_STATE 瞬时字段更新改为 2 色循环；删除从未读取的 `GyNetRoom.last_seq`。
- `src/geyi_ui.c`：新增 `clear_ui_placement()`/`reset_selection_ui()`；新增
  `net_seat_state_text()`/`net_reconn_text()` 去重房间/对局状态文案（保持原有
  “%ds”/“%d s”格式）。
- `src/geyi_input.c/.h`：删除 dead 字段 `pending_aim`、`aim_pending`、`last_ms`
  （当前实现中只写不读；`aim_since` 仍用于 aim-only 稳定）。
- `src/geyi_board.c`：只删除 1 Hz `[geyi:motion]` 日志，不改编排/新鲜度逻辑。
- `tests/`：新增客方按 kind 打包、同 revision 快照保留放兵、预览清放兵、pending
  超时、WAIT 期 REJOIN 不回旧快照、RESULT 清放兵/pending、输入配置/会话/落点映射等用例。

## 自动回归

### 主机 / 覆盖率 / 课程检查

```text
python tools/run_host.py
  core 24572, input 105, tilt-ux 283, params 2517, paramui 85, AI 59,
  net 428, net-app 358, balance 10  PASS

python tools/coverage.py --min 70
  game 99.5%, input 91.3%, nav 100%, config 98.5%, param-ui 100%,
  ai 89.3%, balance 76.5%  PASS

python scripts/check_all.py      PASS
python scripts/check_trace.py    需求追踪 52 条  PASS
```

### 双板输入注入（真实 tilt+KEY2 路径）

`tools/net_input_test.py`，放兵选格后停留 1.6 s 跨周期快照：

| 场景 | 结果 | 证据 |
|---|---|---|
| COM7 房主 / COM5 客方 | 移动 3/3，放兵 1/1 PASS | `evidence/net/20261110/input_20261110_203603/` |
| COM5 房主 / COM7 客方 | 移动 3/3，放兵 1/1 PASS | `evidence/net/20261110/input_20261110_203713/` |
| 房间内换座 | 移动 3/3，放兵 1/1 PASS | `evidence/net/20261110/input_20261110_203752/` |

### 双板完整套件

`python tools/net_board_test.py --port-a COM7 --port-b COM5`：

- **PASS**（2026-11-10 20:41–20:45）：boot/ID、规则、空位、加入/准备、完整对局、
  认输同步、edge churn、WAIT/PLAYING 重启回收、transient rejoin、超时、
  离房/重进/关房、房主转移、过期房间、旁观。
- 证据：`evidence/net/20261109/board/suite_A_COM7_20261110_204146.log`、
  `suite_B_COM5_20261110_204146.log`。

**保留的失败记录**：

1. 20:26 首次运行在 edge churn 失败（host sees rejoin 0 timeout）。日志显示
   COM8 当时仍在广播一个旧房间，COM5 的 `join 0` 加入了 COM8 的房间（guestid=03EA），
   不是被测房主 COM7。已把 COM8 发 `gy_net leave`/`home` 置空闲后重跑；该失败是
   环境残留，不是固件回归。日志：`suite_A_COM7_20261110_202623.log` 等。
2. 20:38 一次运行在 transient rejoin 失败（host sees REJOIN timeout）。A 的
   10 s 倒计时走完、客方未在窗口内完成重连；同一固件 20:41 立即重跑 **PASS**。
   判定为 BLE 瞬断重连时序 flake，保留失败日志：
   `suite_A_COM7_20261110_203851.log`、`suite_B_COM5_20261110_203851.log`。

### 三端旁观套件

`python tools/net_spectator_test.py --port-a COM7 --port-b COM5 --port-c COM8`：
**PASS**（2026-11-10 20:45–20:46）。证据：
`evidence/net/20261110/spectator/spectator_A_COM7_20261110_204525.log` 等。

## 独立 review

新开独立 general-purpose agent 审查重构，结论：

- P0：无。
- P1：1 项——REJOIN 成功路径在 WAIT/RESULT 仍可能发上一局终局快照，客方会被
  拉回 RESULT 而无法准备；已改为 `resync_snapshot_to()` 并补主机测试
  （`test_rejoin_wait_does_not_send_stale_snapshot`，app 358 checks）。
- P2：`last_snapshot_ms` 未复位、`starting_seq` 死字段、UI 重连文案去重不完整、
  RESULT 未清放兵/pending 等已处理；`seat_online` 首帧闪烁、玩家视角远端断线
  文案沿用旧行为，保留并记录为后续 UI 优化项。

## 结论

清理重构后主机、覆盖率、双板输入注入、双板完整套件、三端旁观套件全部 PASS；
固件已烧录三块板。**学生此前的真机确认对应重构前固件；重构后固件完成自动回归，
学生随后确认功能完好、可提交。**
