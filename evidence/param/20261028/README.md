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

结果：编译成功；`main.bin` 844,176 B；ROM 使用 876,138 B，RAM 使用 246,300 B。
构建日志：`firmware_build.log`，SHA-256
`2c7b24a58ecaa77b57dcc6d587beaf4dfe7660236e75a3ed3f679cf0f9e6bfe4`。

固件文件（构建产物，不入库）：`project/build_sf32lb52-lchspi-ulp_hcpu/main.bin`，
SHA-256 `95dffd236200c439726a9fd703f8a96bc37e45c927ed489d714057834b7a286b`。

## 未执行

- 烧录与启动：未执行；需要学生确认后烧录 COM7。
- 板端参数页进入/返回 10 次、size=5/7、生成间隔 3、缓冲阈值、搜索深度：
  需要学生观察屏幕/触摸/倾斜并回传照片、录像或串口日志。
- 启动串口 `param=PASS`、`paramui=PASS`：尚未采集。
- AI 板端耗时/线程栈、帧率：尚未测量。

以上未执行项不得写成通过。
