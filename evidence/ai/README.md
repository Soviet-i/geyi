# REQ-315 人机对战开发证据（2026-10-17）

本目录保存本轮软件验证的原始日志，不包含真机验证。**尚未烧录开发板**，不能据此
宣称 AI 在屏幕上可玩、线程实时性或灯效正常。

## REQ-316 AI 决策预览（2026-10-17）

- `desktop-test-tc316.log`：桌面 LVGL 测试 PASS，覆盖“AI 结果到达后先预览、
  100ms 不提交、约 600ms 后只提交一次、预览期间人类输入无效、首页取消后不落子”。
- `ai_preview.png` / `ai_red_preview.png`：AI 预览方向、数量、目标格高亮和
  “AI 预览 即将落子”状态；`ai_preview_after.png` / `ai_red_preview_after.png`：
  延迟后落子、回到人类回合。
- `firmware_build_tc316.log`：包含预览功能的新固件构建日志。
- 新固件：main.bin 838,544 B，SHA256
  `c3a006acfc8624a0bb9964c4e47db400d385f0761224e834fbceca98d9c70dcc`。
- 真实屏幕上的 500ms 观感、只落子一次、取消行为仍待学生烧录取证；桌面截图
  不是真机照片。

以下是 837,192 B 版本（不含预览）的原始记录，保留作历史证据。

## 环境

- SDK：SiFli-SDK v2.5.1，路径 `D:\IDE\OpenSiFli\SiFli-SDK\v2.5.1`
- ARM GCC：14.2.1（SDK 管理的工具链）
- 主机：Windows，`tools/run_host.py` 自动选择本机可用的 C 编译器
- 桌面 UI：SDK LVGL v9 + Win32 宿主，`tools/build_desktop.py`

## 主机测试

命令：`python tools/run_host.py`

- `PASS: 27246 assertions; TC-301..307; TC-308 host selftest only`
- `PASS: input 60 assertions; TC-309 simulated angles/time only`
- `PASS: 62 AI assertions (legal win, self-damage, RNG independence, pruning, cancel)`

AI 测试覆盖：一步获胜、避免明显自伤、不同 RNG 种子下决策一致、
有/无剪枝节点数对比与根分数一致、取消、30 手 AI 自对弈走真实
`gy_preview`/`gy_commit`/`gy_place` 路径。

原始日志：`host_test.log`。

## 桌面 LVGL 测试

命令：`python tools/build_desktop.py`（需 `SIFLI_SDK` 指向 v2.5.1）

- `PASS: TC-315 AI red/blue modes, AI turn, input guard, stale result discard`
- 原有 TC-313/TC-314、单机预览/确认/放置/重开/认输回归仍通过。

这是电脑端 LVGL + Win32 鼠标测试，不是开发板触摸/倾斜/线程实测。
原始日志：`desktop-test.log`。

电脑渲染截图（由 `--test` 生成，非真机照片）：

- `modes.png`：模式页两个“人机对战”入口，联机仍禁用；
- `ai_human_red.png`：人类执红，红方回合显示“(你)”；
- `ai_thinking.png`：人类走子后 AI 执蓝思考中，显示“AI 思考中...”；
- `ai_after.png`：AI 落子后回到人类回合（注意蓝方获得 1 金币）；
- `ai_red_first.png`：人类执蓝，AI 执红先手思考中；
- `ai_red_after.png`：AI 执红落子后回到人类蓝方回合。

## 固件编译

命令（在已配置 SDK 环境的终端）：

```bash
python scripts/build_firmware.py
```

结果：编译成功。

- `main.bin` 大小：837,192 B
- SHA256：`6cbc56351e6891b1c28f7e22726f213da7ae9962ec3c787c1baa7f3e70580b8f`
- RAM 使用：242,908 / 523,264 B；BSS 42,352 B
- SDK 上游链接警告保留（RWX LOAD 段、newlib `_close/_lseek/_read/_write`
  未实现、ftab 入口符号），未屏蔽；完整日志见 `firmware_build.log`。

## 有/无剪枝基准（主机）

命令：`build/host/geyi-ai-bench.exe --depth N [--no-pruning]`

固定种子 20261015、同一初始局面、同一评估、不设时间/节点上限：

| 深度 | 有剪枝节点 | 无剪枝节点 | 有剪枝 cutoffs | 根分数 | 结果一致性 |
|---:|---:|---:|---:|---:|---|
| 3 | 129 | 304 | 16 | -20 | 相同 |
| 4 | 382 | 1801 | 66 | 40 | 相同 |

这是主机 CPU 数据，不能写成开发板耗时；真机耗时需由学生烧录后测量。
原始输出：`ai_bench.log`。

## 尚未验证（必须由学生完成）

1. 烧录本固件，串口确认 `[geyi:selftest] ai=PASS`。
2. 人机模式我执红/我执蓝两个入口，AI 自动走子，AI 回合人类操作无效。
3. AI 思考期间界面可刷新，主页/重开能取消；AI 返回时间可接受、无看门狗复位。
4. 真实屏幕文字（“人机对战”“AI 思考中”等）无缺字/裁切。
5. 物理证据（视频/照片/串口日志）存入 `evidence/` 后再更新
   `manifest.yaml` 的 `physical_verification`。
