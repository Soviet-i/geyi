# 联机房间交互与双板完整套件（2026-11-09）

本目录证明**主机逻辑**、**固件编译/烧录**与**双板完整自动套件**；不证明真实像素布局、
触摸手感、10 s 断线重连、房主掉电提示或容量 P-02。

| 文件 | 内容 | 覆盖 | 不覆盖 |
|---|---|---|---|
| `host_test_final.log` | `python tools/run_host.py` | 核心 26920、输入 60、参数 2517、参数页 85、AI 62、联机 291、app 159、平衡 10 断言 | 真实 BLE、桌面 LVGL |
| `firmware_build_final.log` | `scons --board=sf32lb52-lchspi-ulp` | `main.bin` 1,063,356 B + LCPU `lcpu.bin` 6,384 B | 编译通过不等于联机可用 |
| `board_suite_final.log` | 最终双板套件 stdout | 9 段全部 PASS；A/B final 均 active=0、rooms=0 | 不是逐像素/触摸证据 |
| `board/suite_A_COM7_*.log`、`suite_B_COM8_*.log` | 双板串口原始日志 | 各轮套件；`120852` 为最终 PASS 轮（含结算回房/第二局/倒计时/旁观无准备） | 失败轮保留，不能只引用 PASS 轮 |
| `latency/latency_before_fix.log` | 延迟复现（修复前） | join 4.07s、close 3.66s FAIL | 不能写成通过 |
| `latency/latency_after_fix.log` | 延迟复测（修复后） | join 1.06s、close 0.41s、leave 0.43s PASS | 只证明本次双板环境 |
| `latency/latency_A_*.log`、`latency_B_*.log` | 延迟测试串口原始日志 | 各轮耗时与状态 | 失败/过渡轮保留 |
| `board/auto_*_101624.log` | 早期冒烟尝试 | 保留为失败/过渡证据 | 不能写成通过 |

## 最终 PASS 覆盖

1. 启动自检 `net=PASS`；A/B 唯一 ID 分别为 `F33A`/`03EA`（芯片 UID 派生）。
2. 空位灰底只显示“点击交换”、房主名带 `(房主)`、不出现 `(对手)`。
3. 加入房间后双方显示彼此真实 ID；A 准备只让 A 座位 `已准备`，B 仍 `未准备`；
   双方准备后进入 PLAYING。
4. `gy_net suggest` 驱动完整对局直到 RESULT，A/B `result` 一致；结算 4s 后双方回同一房间 WAIT，再准备可开第二局。
5. `3 s/2 s/1 s` 倒计时字段在双板可见；旁观不显示准备按钮。
6. 客方退出后房主立即 `players=1`；重进仍为玩家。
7. 单人房主退出后另一板大厅 `rooms=0`；双人房主退出后客方 `active=1 role=0`，`hostid` 换成本机 ID，房间继续。
8. 点击已关房返回 `active=0`，不变成房主，条目移除。
9. 强制旁观 `role=2 local=255`，不占玩家位、不发操作；退出后观战数归零，最终关房后大厅清空。
10. 延迟：join 1.06s、close 0.41s、leave 0.43s（`latency_after_fix.log`）。

## 未验证

- 真实屏幕上的文字居中/右下角像素、空位灰色/准备框白色的观感、触摸点击手感；
- 10 s 断线重连窗口、房主掉电提示、结算保留；
- 第三块板/手机的双方预览叠加；
- P-02 最大并发连接数、连接态广播、快照包长/吞吐、BLE 分片 ACK/重传。

对应需求：REQ-501/502/505/506/507/512/513；测试计划：`docs/03` 的
TC-501-04、TC-502-05、TC-505-06~08、TC-506-07、TC-507-06。
