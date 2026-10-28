# 参数页桌面/固件构建证据（2026-10-28）

本目录记录参数页与动态棋盘的**软件/构建**结果；板端结果尚未执行。
桌面 LVGL 与固件均由本机实际命令生成，不是模拟输出。

## 桌面 LVGL

命令：

```bash
VS_ROOT=D:/visual/community SIFLI_SDK=d:/233/SiFli-SDK/v2.5.1 python tools/build_desktop.py
```

结果：编译 459 个 SDK LVGL 源文件；`geyi-desktop-cn.exe --test` 全部 PASS，包括：

```text
PASS: TC-407 parameter page navigation/edit/reset/apply and size-7 board
PASS: TC-313 home/modes/disabled entries/input guard/single re-entry
PASS: TC-314 light timeline/selftest and real UI feedback events (no physical LED)
PASS: TC-315 AI modes, AI turn, input guard, stale result discard
PASS: TC-316 AI preview before delayed single commit, input guard, cancel
PASS: TC-317 prep default/swap/cancel/start with human red and human blue
```

文件：

- `desktop-test.log`：完整测试输出，SHA-256 `d3aa517fd2f1582d5dfaebf65cf93a6f179a0c4eeb04933fd36d262641889c4a`
- `param_page.png`：参数页第 1 页（单机、默认值、上一页灰/下一页可点），SHA-256 `05c6ee87c359d3f78b1a3cf90eca8b32affcb97b885ad09ed5a7cb727c84fe12`
- `param_page2.png`：参数页第 2 页（人机、DEPTH 可见、上一页可点/下一页灰），SHA-256 `146dfc66b2b74fba6bec6dde042e489f7e16486505fa604e8974fb90e2753732`
- `param_size7.png`：单机 size=7 开局棋盘，SHA-256 `03fb1d0b3fff288dda414deb2218595aa815ca228a0a836f67e5fdeeec31152e`

## 固件

命令（SDK export 后）：

```bash
python scripts/build_firmware.py
```

结果：编译成功；`main.bin` 844,200 B（含 param/paramui 返回码输出与静态自检局面）。
构建日志：`firmware_build.log`，SHA-256
`a41381f1f239172281a5b5d151e152493c2027311dab3ee312d495824bf22724`。

固件文件（构建产物，不入库）：`project/build_sf32lb52-lchspi-ulp_hcpu/main.bin`，
SHA-256 `14bcabcad48c87ba480c350a8064b7fe0113f2d6f28663806a6499bdb4ddf20b`。

## 板端启动（已执行）

- 烧录：CodeKit MCP `sifli_build_download`，COM7，1000000 波特率，verify 通过。
- 启动日志：`board_boot.log`，SHA-256
  `23893e0ba5fbde1738e8f7ae10f427c882c1ec4d9f04f1305e09e722adc13c31`。
- 连续 5 次 `reboot` 均看到：
  `[geyi:selftest] param=PASS rc=0`、`[geyi:selftest] paramui=PASS rc=0`、
  `[geyi:lcd] READY 390 x 450`、`[geyi:imu] WHO_AM_I=6a`。
- 说明：第一次烧录 844,176 B 版本曾出现一次 `param=FAIL/paramui=FAIL`；随后把
  `gy_param_ui_selftest` 的大局面改为静态、完整重编 844,200 B 并重新烧录，连续
  5 次启动 PASS。首次失败原因未完全定位，保留记录，不写成已彻底解决。

## 未执行

- 板端参数页进入/返回 10 次、size=5/7、生成间隔 3、缓冲阈值、搜索深度：
  需要学生观察屏幕/触摸/倾斜并回传照片、录像。
- 参数页真实布局/触摸点击、参数生效（REQ-407/410 板端段）尚未确认。
- AI 板端耗时/线程栈、帧率：尚未测量。

以上未执行项不得写成通过。
