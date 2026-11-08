# 联机板端证据（2026-11-08）

## 文件

| 文件 | 内容 | 结论 |
|---|---|---|
| `boardA_COM7_boot_20261108.log` | 修复前首次烧录启动日志（main.bin 1,026,432 B） | **失败**：`net` 自检期间 `hard fault on thread: main`，`SCB_CFSR_UFSR:0x10 STKOF`；main 线程栈 8,192 B，`gy_net_link_selftest()` 栈帧约 25 KB |
| `boardA_COM7_boot_after_stackfix_20261108.log` | 修复栈溢出后的 COM7 启动日志 | **通过（单板启动）**：`net=PASS`、`light=PASS`、LCD READY 390×450，无 hard fault |
| `boardB_COM8_boot_after_stackfix_20261108.log` | 修复栈溢出后的 COM8 启动日志（第二块板） | **通过（单板启动）**：同上 |
| `auto_A_COM7_20261108_211221.log` / `auto_B_COM8_20261108_211221.log` | 首轮双板自动化测试（失败） | **失败**：B 未发现 A 的房间；当时 GATT 注册在 BLE 事件回调里自锁，GAP 命令无完成事件 |
| `auto_A_COM7_20261108_221612.log` / `auto_B_COM8_20261108_221612.log` | 修复后的双板自动化测试（最终通过） | **通过**：A `adv started`、B `scan started`→`connected`→服务发现→CCCD；B 以蓝方加入，双方准备进入 PLAYING；A/B seq 6 双向同步 |

自动化脚本：`tools/net_board_test.py`。它通过 `gy_net host|lobby|join N|ready|swap|move D C|place C|state` shell 命令把动作排入 UI 主线程，执行路径与触摸点击一致。

## 已修复的板端问题

1. **STKOF**：net 自检的大对象从 main 线程 8 KB 栈移到函数内静态区；内存管道容量 16→4，释放 BSS。
2. **LCPU BLE 镜像缺失**：工程未调用 `AddLCPU()`，`lcpu_img_install()` 是空弱符号，BLE 控制器不启动。已按官方 BLE 例程在 `project/SConstruct` 加 `AddLCPU()`，生成并链接 `lcpu/lcpu.c`（6,384 B）。
3. **GATT 注册自锁**：`BLE_POWER_ON_IND` 回调里直接调用 `sibles_register_svc_128()` 会等待同一事件线程处理注册响应，导致后续 GAP 事件永远不来；改为主线程 `gy_net_ble_poll()` 注册。
4. **服务 UUID 字节序**：GATT 数据库/空口是 LSB-first，B 的搜索/特征匹配需要对应顺序；已统一并兼容正反序。
5. **远端服务索引 0**：`sibles_register_remote_svc()` 首个合法索引是 0，不能当失败；新增独立就绪标志，TX 线程不再因 `g_rmt_svc==0` 丢弃 JOIN。
6. **首次 JOIN 丢失**：CCCD 刚写入时 ACK 可能丢失；加入 1 s JOIN 重发，加入超时从 5 s 调到 12 s。
7. **字体/房间页**：字库按联机文案重新生成；房间页改成空位灰色、只显示“点击交换”、有人时左侧玩家名/右侧准备状态。

## 还不算通过

- 真实触摸 UI 交互与视觉（空位灰色/名字左/状态右）需学生目测确认；
- 旁观者加入与双方预览叠加；
- 断线重连、房主掉线提示、主动退出判负的板端时序；
- 容量 P-02（最大连接数、连接态广播、全量快照吞吐）；
- 自动测试只覆盖两个玩家、各走一步；未覆盖完整对局/结算。
