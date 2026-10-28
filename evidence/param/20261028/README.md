# 参数页桌面/固件/板端证据（2026-10-28）

本目录记录参数页、动态棋盘与板端启动的证据。桌面 LVGL 与固件均由本机实际命令生成；
板端照片为学生本人拍摄，agent 只核对可见内容，不代替目测手感。

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

截图（中文参数名、SPAWN 减一修复、AI 搜索层数在第 2 页末行）：

- `param_page.png`：第 1 页，单机；SHA-256
  `4aaeb7a5b07def0b5b9eac64aa978989cc0fccf726d522654d09861c731e42ac`
- `param_page2.png`：第 2 页，人机；AI 搜索层数在末行；SHA-256
  `9a1f96b6d24b006bd5e86541d0a005a27ebbe5984181fd88049b9b9f196c4911`
- `param_page2_single.png`：第 2 页，单机；末行隐藏，前 4 项连续；SHA-256
  `2dceb6b54016820a2725c6c480d1d020d3fe758ea86125c15b0db3fcdd8f8ad7`
- `param_size7.png`：单机 size=7 开局棋盘
- `desktop-test.log`：完整测试输出

## 固件

命令（SDK export 后）：

```bash
python scripts/build_firmware.py
```

结果：编译成功；`main.bin` **847,344 B**。
构建日志：`firmware_build.log`，SHA-256
`d8d2e7db53049976640ee299bd1b3528c136cab192577004e85167ed76247d90`。

固件文件（构建产物，不入库）：`project/build_sf32lb52-lchspi-ulp_hcpu/main.bin`，
SHA-256 `15083d75ed4d72016aa0b1a4cb9002e7a6e1608594f05bd7b4fc69637dd7be1a`。

## 板端启动（已执行）

- 烧录：CodeKit MCP `sifli_build_download`，COM7，1000000 波特率，verify 通过。
- 最终固件连续 3 次 reboot 均为：
  `[geyi:selftest] param=PASS rc=0`、`[geyi:selftest] paramui=PASS rc=0`、
  `[geyi:lcd] READY 390 x 450`、`[geyi:imu] WHO_AM_I=6a`。
- 启动日志：`board_boot.log`，SHA-256
  `754624ec9e6e43c11ecee3979b0b4f8fbc7377a0fb301873a68991193ee99777`。
- 说明：第一次烧录 844,176 B 版本曾出现一次 `param=FAIL/paramui=FAIL`；把自检局面
  改为静态、完整重编后连续多次 PASS。原因未完全定位，保留记录，见 `docs/06` R-022。

## 板端学生测试（2026-10-28 21:30 照片，修复前界面）

学生本人完成首页/参数页/单机 size=7 操作，并确认进入/返回参数页 10 次无崩溃。
这些照片记录的是**修复前**的英文参数名界面，保留作为问题证据：

- `board_home_param_enabled.jpg`：首页“自定义游戏参数”已可用。
- `board_param_page1_before_fix.jpg`：第 1 页仍为 SIZE/SPAWN/HP 等英文名；
  学生发现人机 SPAWN 只能加不能减。
- `board_param_page2_before_fix.jpg`：第 2 页 DEPTH 在中间，单机/联机隐藏时会留空；
  学生要求把 AI 搜索深度移到末行。
- `board_single_6x6.jpg`：单机默认 6×6 棋盘。
- `board_single_size7.jpg`：单机 size=7 棋盘，7×7 布局正常。
- `board_param_page1_final_chinese.jpg`：修复后最终中文第 1 页（棋盘大小/生成间隔/
  士兵血量/士兵攻击力/放置金币）；SHA-256
  `92ce875f649b1880c8a6fd16314ab35d4d8da024fd4c6f0f0f01ee995b9bb614`。

## 本轮修复（已构建/烧录）

- `GY_P_SPAWN_INTERVAL` 减 1 时跨到相邻奇数，不再被 clamp 回原值；主机新增 10 条断言。
- 参数名改为中文：棋盘大小、生成间隔、士兵血量、士兵攻击力、放置金币、合并金币、
  击杀金币、AI搜索层数、方向切换缓冲、数量切换缓冲。
- 第 2 页顺序改为：合并金币、击杀金币、方向切换缓冲、数量切换缓冲、AI搜索层数；
  单机/联机隐藏末行，不在中间留空。
- 桌面测试新增 SPAWN 减一、单机第 2 页末行隐藏断言；截图见上。

## 学生最终确认（2026-10-28 21:53）

- 学生回传修复后的中文第 1 页照片，确认参数名清晰；并口述确认 SPAWN 减一可用、
  AI 搜索层数在第二页末行、单机第二页无中间空洞。
- 当前阶段为测试与联调；联调完成后再开始联机。今天的内容不作为新里程碑。

## 未执行/待确认

- 第二页最终中文的板端照片（学生已口述确认，未单独拍照）。
- SPAWN=3 的实际生成间隔、方向/角度缓冲 0/50 的手感、人机搜索层数 6 的板端耗时。
- 参数生效（TC-406 板端段）与局中改参数不生效的完整验证。
- AI 板端耗时/线程栈、帧率：尚未测量。

以上未执行项不得写成通过。
