# REQ-315~317 人机对战开发证据（2026-10-17；2026-10-28 状态校准）

本目录保存软件验证日志、桌面渲染图、构建日志和网盘链接。真机证据是学生
2026-10-17 回传的网盘视频（`BaiduNetDisk.md`：`人机.zip`，说明为“选择先后手+
人机对战”），不是本地视频文件。agent 当前环境无法访问 `pan.baidu.com`，
未在线打开/解码；凡写“学生视频确认”处均按学生本人操作记录，不写成 agent
目测或仪器测量。

版本对应：837,192 B（基础 AI，无预览）→ 838,544 B（REQ-316 预览）→ 839,584 B
（REQ-317 准备页）。10-17 视频在时间上对应 839,584 B 构建；视频未显示哈希，
未独立核验。10-18 移动顺序修正（839,552 B）和 10-28 参数集成不在该视频范围内；
代码 HEAD `a3d1ad5`（本次文档校准不改固件）未重新编译/烧录。

## REQ-315 人机对战（2026-10-17）

- `host_test.log`、`desktop-test.log`、`firmware_build.log` 覆盖基础 AI；后续
  838,544 B / 839,584 B 版本包含预览和准备页。
- 2026-10-17 学生视频说明覆盖“选择先后手+人机对战”，确认 REQ-315 流程；agent 未解码。
- 串口 `[geyi:selftest] ai=PASS`、AI 板端耗时/线程栈/内存未测。

## REQ-316 AI 决策预览（2026-10-17）

- `desktop-test-tc316.log`：桌面 LVGL 测试 PASS，覆盖“AI 结果到达后先预览、
  100ms 不提交、约 600ms 后只提交一次、预览期间人类输入无效、首页取消后不落子”。
- `ai_preview.png` / `ai_red_preview.png`：AI 预览方向、数量、目标格高亮和
  “AI 预览 即将落子”状态；`ai_preview_after.png` / `ai_red_preview_after.png`：
  延迟后落子、回到人类回合。
- `firmware_build_tc316.log`：包含预览功能的新固件构建日志。
- 新固件：main.bin 838,544 B，SHA256
  `c3a006acfc8624a0bb9964c4e47db400d385f0761224e834fbceca98d9c70dcc`。
- 2026-10-17 学生视频确认人机流程；**500ms、只落子一次、预览中输入无效、
  取消不落子的逐项画面未由 agent 核验**；串口与计时待补。桌面截图不是真机照片。

## REQ-317 人机对战准备页（2026-10-17）

- `desktop-test-tc317.log`：桌面 LVGL 测试 PASS，覆盖单个人机入口、准备页默认
  “红先玩家/蓝后 AI”、点击方框互换、取消返回模式页、开始后人类执红/执蓝两种顺序。
- `modes_tc317.png`：模式页单个人机入口；`ai_prep.png`：默认玩家执红；
  `ai_prep_blue.png`：点击方框后玩家执蓝、AI 执红。均为电脑渲染，不是真机照片。
- `firmware_build_tc317.log`：包含准备页的新固件构建日志。
- 新固件：main.bin 839,584 B，SHA256
  `132703052e44b2a7fb9d072184d67a3a5d65c8f3667d830223dbdfa4961f731a`。
- 2026-10-17 学生视频确认准备页选择与对局流程；**方框布局、取消/开始等细节
  未逐帧核验**；串口/量化待补。

以下为 837,192 B 版本（不含预览）的原始记录，保留作历史证据。

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

- `main.bin` 大小：837,192 B（历史版本；后续 838,544 B / 839,584 B 见上）
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

1. 烧录当前 HEAD 或对应固件，串口确认 `[geyi:selftest] ai=PASS`。
2. 逐帧核验 REQ-316 的 500ms 预览、只落子一次、预览中输入无效、取消不落子。
3. 板端 AI 单次思考耗时、线程栈/内存高水位、UI 刷新/取消和帧率。
4. 倾斜/触摸/按键的量化数据（正确率、误触率、延迟）；视频未逐项覆盖。
5. 当前 HEAD 在 `a3eeb9c` 之后重新编译并留构建日志；现有视频只对应 10-17 的
   839,584 B 构建，不覆盖 10-18 移动顺序和 10-28 参数集成。
