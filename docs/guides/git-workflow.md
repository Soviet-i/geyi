# Git 工作流与提交规范

整个项目由 git 管理。**历史记录就是你的工作证据**：它要能让教师（和评审 agent）看出你什么时候、为什么、怎么做了什么。

## 1. 分支与标签

- `main`：保留稳定集成基线，始终可检查、可编译（`python scripts/build_firmware.py` 不应被你破坏）；仅接受同仓库 `develop` 发起的 PR。初始化时固件工具链尚未就绪，编译能力待验证。
- `develop`：小组日常集成分支，也是 GitHub 默认分支；成员的修改通过 PR 合入此分支。
- 功能分支：`feat/<需求编号>-<简述>`，如 `feat/req-002-rgb-status`；修复用 `fix/...`；文档用 `docs/...`。
- 所有改动（含文档、日志）从 `develop` 建分支，通过 PR 合入 `develop`。集成后由负责人发起 `develop → main` 的 PR，不直接推送两个受保护分支。
- `main` 与 `develop` 要求课程检查 `check` 和目标检查 `pr-policy` 通过，禁止强制推送与删除，管理员也适用。当前只有所有者，暂不强制额外审核票数；成员加入后再讨论审核人数。
- GitHub 无法禁止创建目标选错的 PR，但目标检查会阻止其合入 `main`。首次建立两个分支属于启用保护前的一次性初始化。
- 里程碑标签：`git tag -a M1-方案评审 -m "M1：PRD v1.0 与方案设计 v1.0 完成"`，推送 `git push --tags`。

## 2. 提交粒度

| 要做 | 不要做 |
|---|---|
| 一个提交只做一件事，能用一句话说清 | 把“新功能 + 顺手重构 + 改文档”塞进一个提交 |
| 做完一小块、测试通过就提交 | 攒一整天最后一次提交 |
| 文档与对应代码变更分开提交，或在同一提交里写清关系 | 提交无法编译/测试全红的代码到 `main` |
| 提交前 `git status`、`git diff` 自查 | 提交调试垃圾、编译产物、密钥 |

**频率要求**：每个开发会话收工前至少提交一次；有开发就有日志（见 `devlog-guide.md`）。检查脚本的断档阈值由 `course.config.json` 的 `max_gap_days` 控制，当前为 30 天（2026-10-31 经教师确认，课程没有更短的硬性间隔要求）。

## 3. 提交信息格式（钩子会强制检查）

```
<type>(<scope>): <主题>

背景: 为什么要改（问题/需求，如 REQ-002）
改动: 改了什么，涉及哪些文件/模块
验证: 怎么验证的（命令与结果；硬件写 evidence/ 路径）
AI协作: 否 | 是(工具/模型；采纳了什么；人工修正了什么)
```

### type

| type | 用于 |
|---|---|
| feat | 新增功能 |
| fix | 修复缺陷 |
| docs | 文档、开发日志、ADR |
| test | 新增/修改测试 |
| refactor | 重构（行为不变） |
| perf | 性能、功耗、资源优化 |
| build | 构建脚本、依赖、工程配置 |
| ci | 检查脚本、CI |
| chore | 杂项（初始化、整理） |
| revert | 回退 |
| hw | 硬件相关：板级验证、引脚/外设配置、证据入库 |

### scope

模块或领域的简短名，如 `rgb`、`debounce`、`ui`、`bt`、`devlog`、`adr`、`prd`、`init`。同一模块保持一致。

### 必填项

- 所有提交：`改动`、`AI协作`。
- `feat / fix / refactor / perf / test / hw`：`背景`、`改动`、`验证`、`AI协作` 全部必填。
- 每项至少 6 个字符，不能留 `<占位符>`。
- `AI协作` 必须以“否”或“是”开头；写“是”时说明用了什么工具/模型、采纳了什么、**你**修正了什么（至少 10 个字符）。

### 好与坏的对比

坏：

```
update code
```
```
fix(ui): 修复bug
```
```
feat(rgb): 新增灯光

改动: 加了文件
AI协作: 是
```

好：

```
fix(rgb): 修复 RGB 灯上电后不亮的问题

背景: 烧录后串口有 red/green/blue 日志，但灯不亮（见 evidence/M2/rgb_dark.jpg），推测灯珠供电未打开。
改动: rgb_led_init 中在查找设备前增加 HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO3_3V3, true, true)。
验证: 重新编译烧录，灯按红绿蓝闪烁，视频与日志见 evidence/M2/rgb_ok.txt。
AI协作: 是(Claude Code / claude-sonnet-5)；agent 最初怀疑 PWM 通道配置有误，我对照官方 rgb_led 例程发现缺少 LDO 使能，要求它按例程补上。
```

### 技巧

- `git config commit.template .gitmessage`（`setup_course.py` 已配置），执行 `git commit` 会弹出带说明的模板。
- 写错了刚提交的信息：`git commit --amend`（只限还没推送的提交）。
- 钩子拒绝时，输出会指出具体缺项，按提示改后重新提交。
- **不要用 `--no-verify`**。绕过钩子的提交会在 `check_all.py` 中被发现。

## 4. 与 agent 协作时的提交

- 让 agent 起草提交信息可以，但**你必须读过再确认**；`AI协作:` 里的“人工修正”必须是真的。
- 不要让 agent 一次性生成几十个文件再整体提交；让它按小步分多个提交完成。
- agent 做了你没验证过的事，`验证:` 就老实写“未验证，待真机确认”，并在日志里记账。

## 5. 常见操作速查

```bash
git status                      # 看状态
git diff                        # 看未暂存改动
git add -p                      # 分块暂存，帮助拆分提交
git log --oneline --graph -15   # 看历史
git restore <文件>              # 丢弃未暂存改动（谨慎）
git revert <hash>               # 用新提交回退某次提交（保留历史，推荐）
git stash / git stash pop       # 临时收起/取回改动
```

不要在已推送的 `main` 上 `git push --force` 或改写历史；需要撤销就用 `git revert`。

## 6. 提交什么、不提交什么

提交：源码、文档、测试、小体积证据（日志、截图）、开发日志。
不提交：编译产物、视频等大文件（放网盘，`evidence/` 里放链接说明）、密钥与令牌、个人隐私信息。
