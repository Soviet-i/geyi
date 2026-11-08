# 联机板端证据（2026-11-08）

## 文件

| 文件 | 内容 | 结论 |
|---|---|---|
| `boardA_COM7_boot_20261108.log` | 修复前首次烧录启动日志（main.bin 1,026,432 B） | **失败**：`net` 自检期间 `hard fault on thread: main`，`SCB_CFSR_UFSR:0x10 STKOF`；main 线程栈 8,192 B，`gy_net_link_selftest()` 栈帧约 25 KB |
| `boardA_COM7_boot_after_stackfix_20261108.log` | 修复后 COM7 烧录启动日志（main.bin 1,026,472 B） | **通过（仅单板启动）**：`net=PASS`、`light=PASS`、`[geyi:lcd] READY 390 x 450`，无 hard fault |

## 修复

`gy_net_proto_selftest()`、`gy_net_room_selftest()`、`gy_net_link_selftest()` 原先把
`GyNetSnapshot`/`GyNetRoom`/`GyNetMemoryLink` 等大结构放在 main 线程栈上。PC 主机线程栈
有几 MB，因此主机测试不会暴露；板端 main 线程只有 8 KB，`GyNetMemoryLink` 单项约
25 KB，必然 STKOF。现改为函数内静态存储（自检只在启动时单线程运行），不改变联机逻辑。

## 不算通过的部分

单板启动只证明固件能启动、自检和 LCD 初始化完成。以下仍未执行：

- 双板 BLE 发现/连接/GATT 收发；
- 大厅/房间/对局/旁观真实 UI 操作；
- 预览私密、断线重连、房主掉线提示；
- 容量 P-02（最大连接数、连接态广播、快照吞吐）。
