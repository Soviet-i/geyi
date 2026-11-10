# 设计图源文件（TikZ / XeLaTeX / Matplotlib）

`docs/img/*.pdf` 与 `*.png` 由本目录的脚本生成；PNG 用于 Markdown 预览，PDF 用于打印/答辩。
图内文字使用开源的 **Noto Sans SC**（OFL-1.1）；没有该字体时回退到系统字体，便于他机复现。

## 编译

```bash
python docs/img/src/build_figures.py          # 全部 TikZ 图
python docs/img/src/build_figures.py fig-architecture
python docs/img/src/build_charts.py           # 全部统计图
```

依赖：

- `xelatex`（TinyTeX/TeX Live，含 `ctex`/`xeCJK`/`tikz`/`standalone`）
- Python `PyMuPDF`（`pip install pymupdf`；旧版也可 `import fitz`）
- Python `matplotlib` 与中文字体（Microsoft YaHei / Noto Sans SC）

## 图清单

| 图 | 源文件 | 用途 |
|---|---|---|
| 图 2-1 系统架构 | `fig-architecture.tex` | 四层架构、线程边界、BLE 外部角色 |
| 图 2-2 数据流 | `fig-dataflow.tex` | 输入→规则→AI→UI→联机；参数旁路 |
| 图 2-3 倾斜输入状态机 | `fig-input-state.tex` | 回中/稳定/预览/确认/放兵、6°/4° 回差 |
| 图 2-4 联机拓扑 | `fig-online-topology.tex` | 房主/对手/旁观、消息方向、预览隐私 |
| 图 2-5 房间阶段状态机 | `fig-room-fsm.tex` | WAIT→STARTING→PLAYING→RESULT、CLOSED |
| 图 2-6 联机时序 | `fig-online-sequence.tex` | 加入/准备/对局/预览/断线重连/结算 |
| 图 1-1 / 2-7 页面流转 | `fig-page-flow.tex` | 首页、模式、准备/大厅/房间、对局、结算 |
| 图 4-1 断言规模 | `build_charts.py` → `fig-results-assertions` | 主机测试规模（log 轴） |
| 图 4-2 覆盖率 | `build_charts.py` → `fig-results-coverage` | 主机行覆盖率与 70% 门槛 |
| 图 4-3 时延 | `build_charts.py` → `fig-results-latency` | 输入延迟与联机修复前后 |
| 图 4-4 AI 剪枝 | `build_charts.py` → `fig-results-ai` | 节点数与主机耗时 |
| 图 4-5 平衡扫描 | `build_charts.py` → `fig-results-balance` | 20 组参数红方胜率 |
| 图 4-6 固件体积 | `build_charts.py` → `fig-firmware-timeline` | 版本演进与 1.5 MiB 资源基线 |

## 修改约定

- 图中“计划/未实现”的模块用灰色或红色虚线；不要把计划模块画成已实现。
- 改图后重跑对应脚本，提交 `.tex`/`.py`、`.pdf`、`.png`；不要只提交 PNG。
- 图里的数字/结论必须与 `docs/01`/`docs/02`/`docs/04` 一致；不确定的写“待实测”。
- 统计图的数据源固定在脚本顶部；改数据前先更新 `docs/07` 或 `evidence/`，不要在脚本里手调数字。
