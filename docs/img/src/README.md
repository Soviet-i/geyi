# 设计图源文件（TikZ / XeLaTeX）

`docs/img/*.pdf` 与 `*.png` 由本目录的 `fig-*.tex` 生成；PNG 用于 Markdown 预览，
PDF 用于打印/答辩。图内文字使用开源的 **Noto Sans SC**（OFL-1.1）；没有该字体时
回退到系统字体，便于他机复现。

## 编译

```bash
python docs/img/src/build_figures.py          # 全部
python docs/img/src/build_figures.py fig-architecture
```

依赖：

- `xelatex`（TinyTeX/TeX Live，含 `ctex`/`xeCJK`/`tikz`/`standalone`）
- Python `PyMuPDF`（`pip install pymupdf`；旧版也可 `import fitz`）

## 图清单

| 图 | 源文件 | 用途 |
|---|---|---|
| 图 2-1 系统架构 | `fig-architecture.tex` | 模块、线程、外部角色、BLE 拓扑 |
| 图 2-2 数据流 | `fig-dataflow.tex` | 输入→规则→AI→UI→历史/联机；数据格式/周期 |
| 图 2-3 状态机 | `fig-state-machine.tex` | 页面/房间状态 + 回合/输入状态 |
| 图 1 页面流转 | `fig-page-flow.tex` | 用户交互专章：页面、主路径、分支 |
| 图 2-5 联机拓扑 | `fig-online-topology.tex` | 房主/对手/旁观、消息方向、预览隐私 |
| 图 2-6 联机时序 | `fig-online-sequence.tex` | 加入/准备/对局/断线/重连/结算 |

## 修改约定

- 图中“计划/未实现”的模块用灰色虚线；不要把计划模块画成已实现。
- 改图后重跑编译脚本，提交 `.tex`、`.pdf`、`.png` 三者；不要只提交 PNG。
- 图里的数字/结论必须与 `docs/01`/`docs/02` 一致；不确定的写“待实测”。
