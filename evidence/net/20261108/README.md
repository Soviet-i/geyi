# 联机主机逻辑证据（2026-11-08）

本目录证明**主机纯逻辑**、**固件编译**与**单板启动**，不证明 BLE 连接、UI 交互或双板可用。

| 文件 | 内容 | 覆盖 | 不覆盖 |
|---|---|---|---|
| `host_test.log` | `python tools/run_host.py` 完整输出 | 核心 26920、输入 60、参数 2517、参数页 85、AI 62、联机 242、app 70、平衡 10 断言；`geyi-net-tests: 242 checks PASS`、`geyi-net-app-tests: 70 checks PASS` | 真实 BLE、桌面 LVGL、双板时序 |
| `firmware_build.log` | 主机逻辑版本的编译日志 | 新增 `src/geyi_net_*.c` 与 `main.c` 自检编译成功，`main.bin` 851,032 B | 未烧录；编译通过不等于 BLE/UI 可用 |
| `firmware_build_ble.log` | 接入 BLE/UI 后的编译日志 | 自定义 GATT、广播/扫描、联机页面编译成功，`main.bin` 1,026,432 B | 未烧录；BLE 实际连接/吞吐/双板未验证 |
| `host_test_after_stackfix.log` | 修复 STKOF 后重跑的主机测试 | 242 + 70 checks 及核心/输入/参数/AI/平衡全部 PASS | 仍不覆盖 BLE/双板 |
| `firmware_build_stackfix.log` | 修复 STKOF 后的编译日志 | `main.bin` 1,026,472 B；`.bss` 114,592 B，RAM 余约 206 KB | 编译通过不等于联机可用 |
| `board/boardA_COM7_boot_20261108.log` | 首次烧录启动日志 | **失败证据**：main 线程栈溢出（STKOF），停在 `net` 自检 | 不能据此判断联机逻辑 |
| `board/boardA_COM7_boot_after_stackfix_20261108.log` | 修复后 COM7 启动日志 | `net=PASS`、`light=PASS`、LCD READY 390×450，无 hard fault；`main.bin` 1,026,472 B | 仅单板启动；BLE、双板、UI 交互、重连、容量未执行 |

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

## 已实现但未物理验证（不得写成通过）

- BLE 广播/扫描/连接/GATT 服务与客户端、分片重组、CCCD 订阅；`proj.conf` 已按 SDK 例程启用。
- 大厅/房间/对局/旁观 UI 已接入固件；桌面 LVGL 因本机缺 MSVC 未跑。
- **已烧录单板并启动通过**：COM7 上 `net=PASS`、`light=PASS`、LCD READY；修复前首次
  烧录曾因 main 线程栈溢出（STKOF）失败，失败日志与修复后日志均在 `board/`。
- **双板 BLE 未验证**：真实 BLE 发现/连接/落子/预览私密/断线重连均未执行。
- P-02 最大连接数、连接态广播、全量快照包长/吞吐未测。

对应需求：REQ-501~REQ-513；测试计划：`docs/03` 的 TC-501~TC-513。
