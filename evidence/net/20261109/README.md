# 联机房间交互与双板完整套件（2026-11-09）

本目录证明**主机逻辑**、**固件编译/烧录**与**双板完整自动套件**；不证明真实像素布局、
触摸手感、10 s 断线重连、房主掉电提示或容量 P-02。

| 文件 | 内容 | 覆盖 | 不覆盖 |
|---|---|---|---|
| `host_test_final.log` | `python tools/run_host.py` | 核心 26920、输入 60、参数 2517、参数页 85、AI 62、联机 307、app 202、平衡 10 断言 | 真实 BLE、桌面 LVGL |
| `firmware_build_final.log` | `scons --board=sf32lb52-lchspi-ulp` | `main.bin` 1,078,356 B + LCPU `lcpu.bin` 6,384 B | 编译通过不等于联机可用 |
| `evidence/ui/20261109/board_suite_key2.log` | 第五轮双板套件 stdout | 10 段全部 PASS（新增 rules/safe area）；A/B final 均 active=0、rooms=0 | 不是逐像素/触摸证据 |
| `board/suite_A_COM7_*.log`、`suite_B_COM8_*.log` | 双板串口原始日志 | 各轮套件；`152835` 为最终 PASS 轮（含规则 4 页/corner=0/rbottom<370 与完整联机流程） | 失败轮保留，不能只引用 PASS 轮 |
| `latency/latency_before_fix.log` | 延迟复现（修复前） | join 4.07s、close 3.66s FAIL | 不能写成通过 |
| `latency/latency_after_fix.log` | 延迟复测（修复后） | join 0.98s、close 0.18s、leave 0.43s PASS | 只证明本次双板环境 |
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
10. 延迟：join 0.98s、close 0.18s、leave 0.43s（`latency_after_fix.log`）。
11. 游戏规则/安全区（REQ-318/NFR-UX-06）：`gy_net rules` 实际进入 4 页；逐页
    `corner=0`、`rbottom=317/337/297/197`（<370）；首页/规则页四角安全；返回首页成功。
    规则正文逐字采用学生原文（含全角标点、10°~60°、KEY2）。桌面截图与字体日志在
    `evidence/ui/20261109/`。
12. 按键：板端只读 PA43/KEY2，PA34/KEY1（电源/下载）不再配置或读取；`gy_ui_key2()`
    的预览确认/放兵进入退出语义由桌面 `--test` 覆盖，真实 KEY2 手感待学生确认。

## 未验证

- 真实屏幕上的文字居中/右下角像素、空位灰色/准备框白色的观感、触摸点击手感；
- 新首页“格弈”观感、游戏规则 4 页的中文标点/圆角遮挡、触摸翻页与返回（自动坐标检查通过，但无照片）；
- 10 s 断线重连窗口、房主掉电提示、结算保留；
- 第三块板/手机的双方预览叠加；
- P-02 最大并发连接数、连接态广播、快照包长/吞吐、BLE 分片 ACK/重传。

对应需求：REQ-311/318/501/502/505/506/507/512/513、NFR-UX-06；测试计划：`docs/03` 的
TC-501-04、TC-502-05、TC-505-06~08、TC-506-07、TC-507-06。
