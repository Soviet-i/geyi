# ADR-0012: 联机/输入模块清理重构

- 状态：已采纳
- 日期：2026-11-10
- 决策人：蔡融汉　协助：Claude Code / deepseek-v4-flash
- 关联需求：REQ-320、REQ-501~REQ-513、NFR-11

## 背景

追查“客方无法落子/预览回弹”时，先后加过多轮防御性补丁：UI 与 net-app 的放兵
状态各自清理、快照/REJECT/START 多处重复 reset、客方确认内联打包、逐字段诊断
日志、若干只写不读的状态字段。根因（协议对普通移动无条件校验 row/column，
把 `-1` 哨兵当成越界）修复后，这些补丁有的已经变成噪声，有的与最终修复重叠，
阅读和维护成本高。需要在不改变外部行为的前提下做一次清理重构。

## 备选方案

| 方案 | 优点 | 缺点 | 对需求/风险的影响 |
|---|---|---|---|
| A. 保持现状 | 零风险；不再改已验证代码 | 噪声/重复继续累积；后续改动容易漏字段；review 成本高 | 可维护性差 |
| B. 把追查期补丁整批回退 | 代码最少 | 会误删有测试支撑的防御（协议按 kind 校验、同 revision 快照不清放兵、发送失败返回、UI 放兵一致性），可能重新引入客方/断线问题 | 回归风险高 |
| C. 有测试支撑的定向清理：保留 load-bearing 修复，集中重复状态，删死代码/日志，补测试，独立 review | 外部行为不变；复用提高；噪声下降；有回归保障 | 需要新增测试与 review 时间；少量 P2 保留 | 满足 NFR-11；风险可控 |

## 决策

选择 **方案 C：定向清理重构**。

保留（有测试支撑、与根因/回归直接相关）：

- 协议 `move_req_valid()` 按 kind 只校验本类型字段；客方确认只填本类型字段。
- 同 revision/outcome 的快照不清客方放兵与预览（`board_changed`）。
- `gy_net_ble_send()` 失败返回 0，客方确认不把“没排进 TX 队列”当成功。
- UI 放兵入口/选格只在 net-app 接受后置 ready；`gy_net_app_preview()` 清放兵残留。
- 1 s PLAYING 周期快照作为 START/快照丢包的兜底，但同 revision 不清本地选择。

清理（不改变外部行为）：

- `geyi_net_app.c` 集中 `clear_placement()`/`clear_visible_previews()`/
  `clear_preview_state()`/`move_args_valid()`；客方确认提取为
  `guest_send_move_req()`；广播复用单播打包；删除高频诊断日志与 dead
  `starting_seq`；leave/promote 复位 `last_snapshot_ms`。
- `geyi_net_proto.c` 提取 `move_req_valid()` 供 pack/unpack 共用。
- `geyi_net_room.c` 删重复 seq 判断、同 seq 瞬时字段改 2 色循环、删 dead
  `GyNetRoom.last_seq`。
- `geyi_ui.c` 新增 `reset_selection_ui()`/`clear_ui_placement()` 与状态文案去重。
- `geyi_input.c/.h` 删只写不读的 `pending_aim`/`aim_pending`/`last_ms`。
- 补主机测试：客方 kind 打包、同 revision 快照保留放兵、预览清放兵、pending
  超时、WAIT 期 REJOIN 不回旧快照、RESULT 清理、输入配置/会话/落点映射。

## 后果

- 正面：重复清理点收敛，漏字段风险下降；协议/UI/输入职责更清楚；主机构建
  428+358 断言、input 105；覆盖率 76.5%–100%；固件 1,091,788 B（清理前
  1,093,852 B）；双板/三端/输入回归 PASS。
- 负面/代价：需要一次独立 review；BLE transient rejoin 仍有 1 次时序 flake
  （同固件重跑 PASS，日志保留）；`seat_online` 首帧闪烁、玩家视角远端断线文案
  沿用旧行为，记录为后续 UI 优化，不在本次改动外部行为。
- 需要同步修改：`docs/00/01/02/03/04/06/08/09`、`manifest.yaml`、`devlog`、
  `tests/README.md`；证据 `evidence/net/20261110/refactor_cleanup/`。

## 待实测/待定

- 重构后功能已由学生确认“完好、可提交”；三张联机截图来自重构前 1,093,852 B
  固件，未提供重构后新截图。
- 真实像素/触摸/倾斜手感、容量 P-02、房主掉电、精确 10 s、BLE 吞吐/丢包
  仍未量化。

## AI协作

agent 先审计追查期补丁，提出“保留 load-bearing 修复、只清噪声/重复/死代码”的
方案；学生确认问题已解决并要求清理。agent 补测试、重构、跑双板/三端回归；
新上下文独立 review 发现 WAIT 期 REJOIN 旧快照 P1，agent 修正并加测试；
学生最终确认重构后功能完好、同意提交。
