# 格弈（geyi）—— 黄山派体感棋类游戏

> 在黄山派 SF32LB52 上，用“倾斜 + 按键”玩 6×6 数字合并战棋：倾斜选方向和士兵数量，
> 确认前看到高亮预览，手指不遮挡棋盘。支持同板双人、人机对战、参数自定义、BLE 联机/旁观与断线重连；
> 历史/计时 RAM 版已合入 develop；NOR + FlashDB 持久化、联机双端记录和四列格式修订在
> `fix/history` 软件段通过；2026-11-13 板端已有重启 count=10、联机“机主/对手”和清空
> 空状态照片；超过 10 条淘汰、详情页、清空后再重启已由学生本人确认，证据留存不足。
> 其余主流程已由学生 2026-11-10 真机确认。

## 功能状态

| 功能 | 状态 | 说明 |
|---|---|---|
| 单机同板双人 | 已实现 | 6×6 棋盘、数字合并、士兵移动/攻击、金币放兵、胜负/平局 |
| 人机对战 | 已实现 | negamax + Alpha-Beta，独立线程，先后手准备页，AI 先预览 500 ms 再落子 |
| 参数自定义 | 界面/主机已实现；板端生效为补测项 | 10 项参数、三套配置、重置；接线完成，TC-406 板端段（SPAWN/size/缓冲/AI 深度）见 `docs/08 §4.1` |
| 板端交互 | 已实现，学生真机确认 | 倾斜 + KEY2（KEY1 仅电源/下载）+ 触摸菜单；学生 2026-11-10 确认倾斜/走字/按钮流程流畅无异常 |
| RGB 反馈 | 已实现 | 有效行动短闪绿；终局按胜方红/蓝、平局黄各 3 闪 |
| 历史/计时 | 软件段已实现；学生确认 | 最近 10 局 + 单局计时；NOR + FlashDB KV 持久化（ADR-0013），联机双端各一条，四列显示类型/时长/胜方/胜方身份；重启 count=10、联机机主/对手、清空空状态有照片；超过10条/详情页/清空后再重启学生确认 |
| 联机/BLE | 已实现，学生真机确认；有修复待复测 | 房主权威/全量快照/旁观/10s 断线重连；协议 v4；学生 2026-11-10 确认进出房/旁观/断线重连；2026-11-13 确认扫描/建房；**房客认输卡对局页已加客户端超时回房兜底（R-078），双板待复测**；大厅显示的另一房间已确认是另一块板真实建房；满旁观容量为扩展项 |

## 硬件与软件

- 开发板：立创·黄山派 SF32LB52-ULP（SF32LB525UC6），板型 `sf32lb52-lchspi-ulp`
- SDK：SiFli SDK **v2.5.1**，RT-Thread，LVGL v9
- 屏幕：约 390×450，触摸；板载六轴 IMU、按键、RGB；无 WiFi、无马达、无电池、无 TF 卡
- 联机只走 BLE；双人对局需两块板，观战验证需三块板同时在线

## 构建与烧录

```bash
# 1. 安装/激活 SiFli SDK v2.5.1（见课程指导手册附录 F）
#    在已 export 的终端里：
python scripts/build_firmware.py
# 产物：project/build_sf32lb52-lchspi-ulp_hcpu/main.bin

# 2. 烧录（二选一）
#    - VS Code + SiFli-SDK-CodeKit：状态栏“烧录”
#    - 命令行：python tools/flash_and_capture.py --check   # 只读预检
#              python tools/flash_and_capture.py           # 输入 FLASH 后烧录并采集 120 s
```

烧录前请确认板型、串口和线缆；烧录会替换板内固件。物理验证必须由本人在真机上完成，
日志/照片存入 `evidence/`。

## 玩法

- 首页：开始游戏 / 自定义参数 / 历史记录（最近 10 局，重启保留；空状态“暂无对局”）。
- 模式页：单机（同板双人）、人机、联机（大厅/房间/旁观/断线重连）。
- 对局：回中 → 倾斜选方向/数量（方位角决定同方向内选哪个兵）→ 预览 → **KEY2** 确认；
  空闲时 **KEY2** 进入/退出放兵；回中取消预览；触摸用于菜单与辅助。
- 人机：准备页选择执红/执蓝；AI 回合显示思考与预览，人类输入屏蔽。
- 参数：进入参数页调整棋盘大小、生成间隔、血量、攻击、金币、AI 深度、缓冲阈值；
  新开一局生效，局中不改；参数存 RAM，重启回默认。

## 测试与检查

```bash
python tools/run_host.py              # 主机断言（规则/输入/倾斜/参数/参数页/AI/联机/平衡）
python tools/coverage.py --min 70     # 核心模块行覆盖率
python tools/verify_all.py            # 文档检查 + 主机测试（可加 --coverage/--desktop/--board）
python scripts/check_all.py           # 提交规范/开发日志/需求追踪/manifest
python scripts/build_firmware.py      # 固件编译（需 SDK export）
```

主机/桌面通过不等于板端通过；板端项必须留串口/照片/录屏证据。详见
`docs/guides/testing.md` 与 `docs/08-验收矩阵.md`。

## 文档

- 需求：`docs/01-需求规格说明.md`（L1–L4、用户交互、REQ-xxx）
- 设计：`docs/02-方案设计说明.md`（三张图、联机/历史设计）
- 测试：`docs/03-测试计划.md`、`docs/04-测试报告.md`
- 验收矩阵：`docs/08-验收矩阵.md`
- 量化测量：`docs/07-量化测量报告.md`
- 索引：`docs/README.md`

## 已知限制

- 历史/计时软件段已实现；2026-11-13 板端已有重启 count=10、联机双端机主/对手、四列和清空空状态照片；超过 10 条淘汰、详情页、清空后再重启已由学生本人确认，证据留存不足。联机/旁观与 REQ-320 已由学生 2026-11-10 真机确认；满旁观容量为扩展项（需 4 块以上板子）。
- 参数仍存 RAM、重启回默认；历史改用 FlashDB，重启保留最近 10 局（ADR-0013）。
- 无音频/震动；反馈只用 RGB；无 WiFi，互联网联机不在范围。
- 倾斜正确率、真实板端延迟、帧率、AI 板端耗时/栈为补证项；24 h soak 为扩展测试项：补测成本高，经讨论可作为扩展测试项。
- 已修待复测：房客认输后客户端 5s 超时回房间（R-078；主机测试通过，双板待复测）；AI 搜索层数 5/6 不再使用 300ms 提前截断并打印 max_depth/实际 depth（R-079；板端设置6已见 `depth=6`、ms=1844）。
- 当前板端证据包含历史构建与 2026-11-10 协议 v3 固件（1,089,236 B，三块板）；旧 v2 固件不兼容。

## 第三方资源

见 `docs/refs/third-party.md`。固件使用 SiFli SDK（Apache-2.0）、LVGL v9（MIT）、
思源黑体子集（OFL-1.1）；设计图使用 Noto Sans SC（OFL-1.1）。未使用第三方图片/音频/模型。

---

# 以下为课程脚手架说明（原始 README）

# course_starter —— 黄山派嵌入式产品开发课程脚手架

本小组仓库：[Soviet-i/geyi](https://github.com/Soviet-i/geyi)（公开）。默认分支为 `develop`，成员从它建立功能分支，并只通过 PR 合入 `develop`；负责人通过 `develop → main` 的 PR 同步稳定版本。两个分支禁止直接推送、强推及删除，合并需通过课程和 PR 目标检查。当前成员已加入，产品名为格弈；M2 人机对战已通过 PR 合入 `develop`，M3 进行中。具体状态见 `docs/00-项目计划与里程碑.md`。

这是课程的**统一起点**：既是入门训练的练习场，也是你自己产品项目的基础工程。它内置：

- **AI 软件工程过程文档模板**：计划、需求规格(PRD)、方案设计、测试计划、测试报告、风险台账、架构决策(ADR)、开发日志（Markdown），研制总结（Word）
- **AI agent 驱动开发支持**：`CLAUDE.md` / `AGENTS.md`、Claude Code 斜杠命令、协作指南与提示词
- **git 全程管理**：提交规范由钩子强制，开发日志与提交自动核对
- **入门训练**：T0 环境与规范、T1 用一个小功能完整走一遍“需求 → 设计 → 实现 → 测试 → 总结”
- **开箱即用的 SiFli SDK 工程**（`project/` + `src/`，结构与官方例程一致）：黄山派例程（串口打印 Hello world + 板载 RGB 灯红绿蓝闪烁），装好 SDK v2.5.1 后一条命令编译

> `training/` 目前只有 `README.md` 与 `progress.md`，README 引用的 T0/T1 任务正文不在仓库历史中；
> 见 `docs/06` R-003，需向教师索取完整材料，不能自行编造。

## 5 分钟上手

```bash
# 1. 前置：Git 2.30+、Python 3.8+、一个 AI agent 工具（Claude Code / Codex 等）
#    以及 SiFli SDK v2.5.1 开发环境（SDK + 工具链，安装步骤见 training/T0）
git config --global user.name  "你的姓名"
git config --global user.email "你的邮箱"

# 2. 进入本目录，安装依赖并初始化（创建仓库、启用钩子、做规范的首次提交）
pip install -r requirements.txt
python scripts/setup_course.py

# 3. 检查开发环境，编译内置例程（在已运行 SDK export 脚本的终端里）
python scripts/check_env.py
python scripts/build_firmware.py

# 4. 看初始状态，然后按 training/T0 开始
python scripts/check_all.py
```

## 目录结构

```
course_starter/
├── README.md                    本文件
├── CLAUDE.md / AGENTS.md        给 AI agent 的项目规范（内容一致，勿删勿改）
├── course.config.json           检查阈值（教师维护）
├── manifest.yaml                提交给教师评审 agent 的交接清单
├── .gitmessage                  提交信息模板
├── .githooks/                   commit-msg（强制规范）、pre-commit（日志提醒）、pre-push
├── .claude/commands/            Claude Code 斜杠命令：/commit /devlog /doc-sync /milestone-check /physical-verify
├── .github/workflows/           CI：push 时自动运行 check_all.py
├── docs/
│   ├── 00-项目计划与里程碑.md
│   ├── 01-需求规格说明.md        PRD，REQ-xxx 编号
│   ├── 02-方案设计说明.md
│   ├── 03-测试计划.md            TC-xxx 编号
│   ├── 04-测试报告.md
│   ├── 05-研制总结.docx          Word，M5 阶段填写
│   ├── 06-风险与问题台账.md
│   ├── adr/                     架构决策记录
│   ├── devlog/                  开发日志（每个开发日一篇）
│   └── guides/                  git 规范 / 日志规范 / agent 协作指南
├── training/                    入门训练 T0、T1 + 进度登记
├── project/                     SiFli SDK 工程目录：SConstruct、SConscript、Kconfig*、proj.conf、rtconfig.py
├── src/                         应用源码：main.c（Hello + RGB 闪烁）；新增 .c 自动参与编译
├── evidence/                    物理验证原始证据
├── logs/                        agent 会话记录
└── scripts/                     检查与辅助脚本
```

## 日常工作流（每次开发会话）

```
开工汇报 → 计划(你确认) → 文档先行 → 测试 → 实现 → 验证 → 小步提交 → 收工(日志 + 检查)
```

| 时点 | 做什么 | 命令 |
|---|---|---|
| 开工 | 让 agent 汇报当前状态 | 提示词见 `docs/guides/agent-collaboration.md` §6.1 |
| 开发中 | 每完成一小块就提交 | `git commit`（模板自动弹出）或 `/commit` |
| 需要真机 | agent 暂停，你操作并留证据 | `/physical-verify <事项>`，证据入 `evidence/` |
| 收工 | 写日志并自检 | `/devlog`，`python scripts/check_all.py` |
| 里程碑前 | 严格检查、打标签 | `python scripts/check_all.py --final`，`git tag -a M2-... ` |

## 提交信息速查

```
<type>(<scope>): <主题>

背景: 为什么改
改动: 改了什么
验证: 怎么验证的
AI协作: 否 | 是(工具/模型；采纳了什么；人工修正了什么)
```

type：`feat fix docs test refactor perf build ci chore revert hw`。`feat/fix/refactor/perf/test/hw` 四项全填，其他类型至少填“改动”与“AI协作”。详见 `docs/guides/git-workflow.md`。

## 检查脚本一览

| 命令 | 作用 |
|---|---|
| `python scripts/check_all.py [--final]` | 一键检查（提交规范 / 开发日志 / 需求追踪 / manifest；`--final` 加固件编译） |
| `python scripts/check_commit_msg.py --log` | 校验历史中所有提交信息 |
| `python scripts/new_devlog.py [--update]` | 创建/刷新今天的开发日志 |
| `python scripts/check_devlog.py [--json]` | 日志缺失、简略、断档检查 |
| `python scripts/check_trace.py [--final]` | REQ 在 PRD/设计/测试计划/测试报告间的追踪 |
| `python scripts/check_env.py` | 检查 SiFli SDK、scons、ARM 编译器等开发环境 |
| `python scripts/build_firmware.py` | 编译固件（在 project/ 下调用 scons），并提示下载命令 |
| `python scripts/check_manifest.py` | manifest 完整性与证据文件存在性 |

> 脚本通过只代表“字段齐全、规则满足”，**不代表内容质量合格**。评审时教师（及评审 agent）会核对日志、提交、证据三者是否互相印证。

## 重要提醒

1. **不要用 `git commit --no-verify`**，也不要删改 `.githooks/`、`scripts/check_*.py`、`course.config.json`、`CLAUDE.md`/`AGENTS.md`——这些改动会出现在历史里。
2. agent 写的文档/代码/日志，**你必须读过、验证过**再提交；`AI协作:` 里写的“人工修正”必须是真的。
3. 没有条件做的物理验证，如实写进 `known_issues`；编造证据按学术诚信处理。
4. 密钥、令牌、隐私信息不要进仓库，也不要出现在 `logs/` 里。
5. `src/` 下只有 `main.c` 含 `main()`；新增模块不要再定义 `main`。`project/` 下的 `SConstruct`、`Kconfig`、`rtconfig.py` 是 SDK 骨架，不要随意改。
6. 编译产物在 `project/` 下生成（`build_*/`、`sifli_sdk/`、`src/`，已在 .gitignore），不要提交。

## 常见问题

**Q：初始状态为什么有 FAIL？** 开发日志的 FAIL 来自还没写日志；manifest 的警告来自模板未填写，都是预期的，按训练逐个处理。

**Q：不在 GitHub 上，能用 Gitee 吗？** 可以。所有检查都是本地 Python 脚本，CI 可用任意平台的流水线执行 `python scripts/check_all.py`。

**Q：我的 agent 工具读不到 CLAUDE.md/AGENTS.md？** 把 `AGENTS.md` 全文粘贴到会话开头，并在 `manifest.yaml` 里如实填写 `dev_agent.tool`。

**Q：Windows 下钩子不生效？** 使用 Git for Windows，确认 `git config core.hooksPath` 为 `.githooks`；仍有问题时运行 `python scripts/check_commit_msg.py --log` 手动自查，并告知教师。
