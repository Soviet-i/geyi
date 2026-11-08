# 独立 review 记录（2026-11-08）

- 方式：独立上下文 review agent 通读两份课程文档、仓库文档/图/代码/测试/CI，
  运行 `check_all.py`、`run_host.py`、`coverage.py --min 70`、`verify_all.py`、
  `check_trace.py --final`，并逐张目检 6 张 PNG；未改文件。
- 结论（修正前）：需先修；自动检查通过，但文档状态有矛盾、图有与设计不符的连线、
  manifest 证据路径不可归档、11-07 物理验证漏登记。

## 主要发现与处理

| 级别 | 发现 | 处理 |
|---|---|---|
| 阻塞 | 覆盖率在 docs/01~04 写“未测”，docs/README/manifest/testing 写“76.2–100% 已实测” | 统一为“主机实测 76.2%–100%（2026-11-08，coverage.py --min 70 PASS）；CI 已接入、远端待确认；板端不适用” |
| 阻塞 | manifest 的 host/coverage 证据路径指向被 gitignore 的 `build/` | 复制到 `evidence/host/20261108/`、`evidence/coverage/20261108/` 并更新 manifest |
| 阻塞 | manifest 缺 2026-11-07 准备页板端照片 | 新增 `physical_verification` 与 `TC-317-BOARD-20261107`，注明学生提供、agent 未解码点击 |
| 高 | docs/00 C-01 仍写“按 BT PAN 设计” | 改为 BLE 外设/GATT 服务端；BT PAN 列为拓展 |
| 高 | 数据流图 `GyConfig→历史` 错误 | 删除；参数在新开一局写入 game/input/AI（cfg 框与架构图参数总线表达） |
| 高 | docs/04 同时写 847,344 B 与 847,944 B“当前代码” | 改为“当前分支未重编；10-28 847,344 B、11-07 847,944 B 为历史板端证据” |
| 高 | REQ-313 仍写“仅开始游戏/仅单机” | 加“后续变更”说明（REQ-408/315/317 已启用参数/人机） |
| 高 | NFR-09/14、NFR-UX 缺 TC/矩阵落点 | 补 TC-NF-14/15、TC-NF-UX-01~05；docs/08 增加 NFR-UX 表 |
| 高 | docs/06 表格残片 + R-019 重号 | 删除残片；准备页图标风险改 R-035，同步 devlog 11-07 |
| 中 | AI 自动提交画到 UI；页面流转缺单机/联机结算；房主掉线判负权威性；房主预览转发；计划页面未标灰；归档/个人能力清单缺失；多处过期状态；refs 写参数未实现；架构图缺参数/网络回流；旁观必达表述不一致 | 逐项修正：图 2-4/2-5/2-6、状态机灰虚线、REQ-510 本地终局提示、REQ-507 房主预览、docs/09 归档清单、状态同步、refs 更新、架构图参数总线/net→UI、范围统一 |
| 低 | 图例重叠、图号缺 2-4、Noto 许可/回退、IMU 型号、commit_range、板端准出用例、R-034、README 参数措辞、覆盖率措辞 | 已修或已在 docs/09 说明；commit_range 封版时替换为精确 SHA |

## 未完成/保留

- 联机/历史代码、板端烧录、双板 BLE 仍未执行；本轮只修文档/图/测试/CI。
- CI 远端 run 未在本机冒充；以 GitHub Actions 实际结果为准。
- 会话日志仍是 2026-10-28 快照；封版前重新导出。
