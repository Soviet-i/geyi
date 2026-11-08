# 联机证据（2026-11-08）

本目录证明**主机纯逻辑**、**固件编译/启动**与**双板 BLE 自动冒烟**；不证明真实触摸 UI 视觉、旁观、重连或容量。

| 文件 | 内容 | 覆盖 | 不覆盖 |
|---|---|---|---|
| `host_test.log` | 早期 `python tools/run_host.py` 输出 | 核心 26920、输入 60、参数 2517、参数页 85、AI 62、联机 242、app 70、平衡 10 断言 | 真实 BLE、桌面 LVGL、双板时序 |
| `firmware_build.log` | 主机逻辑版本编译日志 | `main.bin` 851,032 B | 未烧录 |
| `firmware_build_ble.log` | 接入 BLE/UI 后编译日志 | `main.bin` 1,026,432 B | 当时未烧录 |
| `host_test_after_stackfix.log` | 修复 STKOF 后主机测试 | 242 + 70 checks 及核心/输入/参数/AI/平衡全部 PASS | 仍不覆盖 BLE/双板 |
| `firmware_build_stackfix.log` | 修复 STKOF 后编译日志 | `main.bin` 1,026,472 B；`.bss` 114,592 B | 编译通过不等于联机可用 |
| `host_test_final.log` | 最终主机测试 | **`geyi-net-tests` 253 checks、app 70 checks PASS**；其余 26920/60/2517/85/62/10 全 PASS | 桌面 LVGL、真实 BLE 物理时序 |
| `firmware_build_final.log` | 最终固件编译 | `main.bin` **1,056,484 B** + LCPU `lcpu.bin` 6,384 B；RAM 余约 225 KB | 编译通过不等于联机可用 |
| `board/boardA_COM7_boot_20261108.log` | 首次烧录启动日志 | **失败证据**：main 线程栈溢出（STKOF） | 不能据此判断联机逻辑 |
| `board/boardA_COM7_boot_after_stackfix_20261108.log` | 修复后 COM7 启动 | `net=PASS`、`light=PASS`、LCD READY | 仅单板启动 |
| `board/boardB_COM8_boot_after_stackfix_20261108.log` | 修复后 COM8 启动 | 同上 | 仅单板启动 |
| `board/auto_*_211221.log` | 首轮双板自动化（失败） | B 未发现 A 房间；GATT 注册自锁证据 | 不能写成通过 |
| `board/auto_*_221612.log` | 最终双板自动化（通过） | A 广播、B 扫描/连接/服务发现/CCCD/JOIN；双方准备进入 PLAYING；A/B seq 6 同步 | 仅两名玩家、各一步；未覆盖旁观/重连/容量 |

## 联机主机测试覆盖

- 协议：信封/版本/类型/长度/截断、快照往返、各消息打包/解包。
- 房间：加入/满员、准备/取消、3s 倒计时、交换红蓝、房主等待期离开转移/关房、
  落子/放兵/认输、非法与错误回合拒绝、结算保留 10s、全量快照 seq 幂等。
- app 路由：交换红蓝后的 READY/MOVE 颜色、ROOM_STATE 与 SNAPSHOT 同 seq、
  MOVE_REQ 幂等、对局中 LEAVE 判负、重复 JOIN、满员大厅规则。
- 预览：只对本人/旁观可见，对手不可见；限流与 `PREVIEW_CLEAR` 由房间层状态覆盖。
- 断线：客方 10s 重连窗口、超时判负、等待期释放席位、旁观者不冻结、客方本地终局提示、
  STARTING 中断线退回 WAIT、旁观 10s 重连槽位不被顶掉。
- 大厅：插入/更新/3s TTL/点击规则；内存管道收发与满队列。

## 已验证与未验证

- **双板 BLE 自动冒烟已通过**：A 广播、B 扫描/连接/GATT 服务发现/CCCD、B 以蓝方加入、
  双方准备进入 PLAYING、A/B 各走一步并同步到 seq 6。
- 大厅/房间/对局 UI 已接入固件；**真实触摸 UI 视觉与操作仍需学生目测确认**；桌面 LVGL
  因本机缺 MSVC 未跑。
- **两块板已分别烧录并启动通过**：COM7/COM8 均 `net=PASS`；修复前 STKOF 失败日志保留。
- **未验证**：旁观加入/双方预览叠加、断线重连、房主掉线提示、主动退出判负、完整对局/结算。
- P-02 最大连接数、连接态广播、全量快照包长/吞吐未测；`CFG_MAX_BT_ACL_NUM` 暂按例程 2。

对应需求：REQ-501~REQ-513；测试计划：`docs/03` 的 TC-501~TC-513。
