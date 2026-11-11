# 历史记录与计时 REQ-601~606 电脑端证据（2026-11-12）

这些文件由仓库内工具在开发机生成，**不是开发板照片**，不能作为板端验收证据。

生成方式（PowerShell，需 SDK 与 MSVC；本机 SDK 在 `D:\SiFli-SDK\v2.5.1`，VS 在 `E:\Virsual Studio\chanpin`）：

```powershell
$env:SIFLI_SDK='D:\SiFli-SDK\v2.5.1'
$env:VS_ROOT='E:\Virsual Studio\chanpin'
python tools/generate_ui_font.py     # 296 个非 ASCII 字形
python tools/run_host.py             # 主机回归 + geyi-history-tests（61 断言）
python tools/build_desktop.py        # 桌面 LVGL 构建并跑 --test
```

| 文件 | 内容 |
|---|---|
| `desktop-test.log` | 桌面 `--test` 输出，含 `TC-601~TC-606 history list/detail/clear and single record per match` 的 PASS 行 |
| `history_empty.png` | 首页进入“历史记录”后的空状态（“暂无对局”） |
| `history_list.png` | 一局结束后列表出现该局：`蓝方胜 单机 00:01` |
| `history_detail.png` | 详情页：结果/模式/棋盘/步数/时长/时间六项 |

PNG 由 `build/desktop/*.bmp`（LVGL 输出帧缓冲）转换，只做通道转换与底色叠加，未修改像素内容。

本轮范围：RAM 版（10 条、重启清空），单机与人机记录，联机对局不记录；对局页未新增计时标签。
板端固件未编译、未烧录（开发机没有 arm-none-eabi-gcc），重启清空与真实对局字段需要学生烧录后另行留证。
