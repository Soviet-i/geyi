#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成研制总结报告用统计图（PNG，供 Markdown -> Word 使用）。

数据来源：
  - 提交/日期/作者：`git log`（交付分支 HEAD，2026-11-22）
  - 断言、覆盖率、时延、AI、固件体积：docs/04、docs/07
  - 平衡扫描：evidence/balance/20261028/*.csv
改数据前先更新对应文档，不要在脚本里随手调数字。
"""
from pathlib import Path
import csv

import matplotlib
matplotlib.use("Agg")
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FuncFormatter

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
ROOT = Path(r"D:\233\code\course_starter")

INK = "#1F2937"
BLUE = "#2563EB"
GREEN = "#059669"
ORANGE = "#D97706"
RED = "#B91C1C"
GRAY = "#6B7280"
LIGHT = "#E5E7EB"

plt.rcParams.update({
    "font.sans-serif": ["Microsoft YaHei", "Noto Sans SC", "SimHei", "SimSun"],
    "axes.unicode_minus": False,
    "font.size": 9,
    "axes.titlesize": 10.5,
    "axes.titleweight": "bold",
    "axes.labelsize": 9,
    "axes.edgecolor": "#D1D5DB",
    "axes.linewidth": 0.8,
    "xtick.color": INK,
    "ytick.color": INK,
    "text.color": INK,
    "axes.labelcolor": INK,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "savefig.facecolor": "white",
    "pdf.fonttype": 42,
    "ps.fonttype": 42,
})


def clean(ax, grid_axis="y"):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(axis=grid_axis, color=LIGHT, linewidth=0.7)
    ax.set_axisbelow(True)


# 文档里引用的文件名与报告用名不同：文档沿用建库时的旧名，这里做一次映射，
# 脚本直接产出文档引用的文件，重跑即可刷新 docs/01~04 里的图。
DOC_NAMES = {
    "fig-ai": "fig-results-ai",
    "fig-assertions": "fig-results-assertions",
    "fig-coverage": "fig-results-coverage",
    "fig-balance": "fig-results-balance",
    "fig-firmware": "fig-firmware-timeline",
}
DOC_ONLY = {"fig-input-latency", "fig-net-latency"}
REPORT_ONLY = {"fig-progress", "fig-commit-types", "fig-req", "fig-band-map"}


def save(fig, name, dpi=300):
    fig.tight_layout()
    out = DOC_NAMES.get(name, name)
    fig.savefig(OUT / f"{out}.png", dpi=dpi, bbox_inches="tight")
    fig.savefig(OUT / f"{out}.pdf", bbox_inches="tight")
    plt.close(fig)
    print(f"OK {out}.png")


def fig_progress():
    days = ["10-15", "10-17", "10-18", "10-28", "10-31", "11-03", "11-06", "11-07",
            "11-08", "11-09", "11-10", "11-12", "11-13", "11-21", "11-22"]
    commits = [10, 25, 4, 38, 12, 6, 4, 6, 35, 92, 12, 6, 30, 16, 4]
    x = np.arange(len(days))
    fig, ax = plt.subplots(figsize=(6.5, 3.0))
    ax.bar(x, commits, color=BLUE, width=0.62, zorder=3)
    for xi, v in zip(x, commits):
        ax.text(xi, v + 2.0, str(v), ha="center", va="bottom", fontsize=7.6, color=INK)
    ms = {"M0": 0, "M1": 1, "M2": 2, "M3": 10, "M4": 13}
    for name, idx in ms.items():
        ax.plot([idx, idx], [95.5, 98.5], color=RED, linewidth=1.0, zorder=4)
        ax.annotate(name, xy=(idx, 102.2), fontsize=8, color=RED, ha="center",
                    fontweight="bold", va="center")
    ax.set_xticks(x, days, rotation=45, ha="right", fontsize=8)
    ax.set_ylim(0, 106)
    ax.set_ylabel("当日提交数")
    ax.set_title("十五个开发日的提交节奏（红色短刻度为里程碑）")
    clean(ax)
    save(fig, "fig-progress")



def fig_commit_types():
    types = ["docs", "fix", "feat", "test", "hw", "chore", "ci", "build", "refactor", "revert"]
    counts = [135, 36, 31, 31, 25, 7, 2, 2, 1, 1]
    colors = [GRAY, ORANGE, BLUE, GREEN, "#7C3AED", GRAY, GRAY, GRAY, GRAY, GRAY]
    fig, ax = plt.subplots(figsize=(6.0, 2.7))
    y = np.arange(len(types))
    ax.barh(y, counts, color=colors, height=0.62, zorder=3)
    ax.set_yticks(y, types)
    ax.invert_yaxis()
    for yi, v in zip(y, counts):
        ax.text(v + 2, yi, str(v), va="center", fontsize=8, color=INK)
    ax.set_xlim(0, 152)
    ax.set_xlabel("提交数")
    ax.set_title("提交类型分布（271 个非合并提交）")
    clean(ax, "x")
    save(fig, "fig-commit-types")


def fig_req():
    mods = ["仓库协作", "单机规则", "输入与界面", "人机 AI", "参数配置", "BLE 联机", "历史记录"]
    total = [2, 8, 9, 3, 11, 13, 6]
    # 判据在开发中做过修订、或验证范围受限的条目：REQ-313/320-B（输入与界面）、
    # REQ-503（联机）、REQ-603/604（历史），共 5 条
    revised = [0, 0, 2, 0, 0, 1, 2]
    ok = [t - r for t, r in zip(total, revised)]
    assert sum(total) == 52 and sum(revised) == 5
    fig, ax = plt.subplots(figsize=(6.3, 3.0))
    y = np.arange(len(mods))
    ax.barh(y, ok, color=GREEN, height=0.58, label="通过", zorder=3)
    ax.barh(y, revised, left=ok, color=ORANGE, height=0.58,
            label="判据修订或验证范围受限", zorder=3)
    ax.set_yticks(y, mods)
    ax.invert_yaxis()
    for yi, a, b in zip(y, ok, revised):
        ax.text(a + b + 0.18, yi, f"{a+b}", va="center", fontsize=8, color=INK)
        if b:
            ax.text(a + b / 2, yi, str(b), va="center", ha="center", fontsize=6.8,
                    color="white", fontweight="bold")
    ax.set_xlim(0, 15)
    ax.set_xlabel("需求条目数")
    ax.set_title("52 条功能需求全部通过，其中 5 条判据修订或验证范围受限")
    ax.legend(frameon=False, fontsize=7.6, loc="upper center",
              bbox_to_anchor=(0.5, -0.22), ncol=2)
    clean(ax, "x")
    save(fig, "fig-req")



def fig_assertions():
    suites = ["规则核心", "参数模型", "联机应用", "联机协议/房间", "倾斜交互",
              "人机 AI", "输入状态机", "参数页", "历史记录", "平衡扫描"]
    counts = [24576, 2517, 453, 428, 283, 116, 105, 85, 84, 10]
    colors = [BLUE, BLUE, ORANGE, ORANGE, GREEN, GREEN, GREEN, GREEN, GREEN, GRAY]
    fig, ax = plt.subplots(figsize=(6.2, 3.2))
    y = np.arange(len(suites))
    ax.barh(y, counts, color=colors, height=0.6, zorder=3)
    ax.set_yticks(y, suites)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(8, 90000)
    ax.set_xlabel("断言数（对数轴）")
    ax.set_title("主机自动化断言规模（合计 28,657 条）")
    for yi, v in zip(y, counts):
        ax.text(v * 1.13, yi, f"{v:,}", va="center", fontsize=7.8, color=INK)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{int(x):,}"))
    clean(ax, "x")
    save(fig, "fig-assertions")


def fig_coverage():
    mods = ["导航", "参数页", "规则核心", "参数模型", "输入状态机", "AI 搜索", "平衡扫描"]
    vals = [100.0, 100.0, 99.5, 98.5, 91.3, 97.4, 76.5]
    colors = [GREEN, GREEN, BLUE, BLUE, GREEN, GREEN, GRAY]
    fig, ax = plt.subplots(figsize=(6.2, 2.8))
    x = np.arange(len(mods))
    ax.bar(x, vals, color=colors, width=0.56, zorder=3)
    ax.axhline(70, color=RED, linestyle="--", linewidth=1.0, zorder=4)
    ax.text(6.62, 70.5, "70% 门槛", color=RED, fontsize=7.4, va="center", ha="left", clip_on=False)
    for xi, v in zip(x, vals):
        ax.text(xi, v + 2.0, f"{v:.1f}", ha="center", fontsize=7.8, color=INK)
    ax.set_xticks(x, mods, rotation=18, ha="right")
    ax.set_ylim(0, 110)
    ax.set_ylabel("行覆盖率 (%)")
    ax.set_title("主机行覆盖率（2026-11-21）")
    clean(ax)
    save(fig, "fig-coverage")



def fig_input_latency():
    fig, ax = plt.subplots(figsize=(6.2, 2.7))
    samp = [1, 5, 10, 20]
    lat = [201, 205, 210, 220]
    ax.plot(samp, lat, color=BLUE, marker="o", linewidth=1.6, markersize=4.5, zorder=3)
    for s, v in zip(samp, lat):
        ax.annotate(f"{v}", (s, v), textcoords="offset points", xytext=(0, 9),
                    ha="center", fontsize=8)
    ax.annotate("", xy=(21.4, 201), xytext=(21.4, 220),
                arrowprops=dict(arrowstyle="<->", color=GRAY, linewidth=0.8))
    ax.text(21.8, 210.5, "只差 19 ms", fontsize=7.4, color=GRAY, va="center")
    ax.text(1.0, 226, "采样周期 20→1 ms（采样率 ×20）", fontsize=7.4, color=GRAY)
    ax.set_xlim(0, 24)
    ax.set_ylim(194, 230)
    ax.set_xticks(samp)
    ax.set_xlabel("IMU 采样周期 (ms)")
    ax.set_ylabel("倾斜到预览 (ms)")
    ax.set_title("输入延迟：200 ms 稳定窗口主导")
    clean(ax)
    save(fig, "fig-input-latency")



def fig_net_latency():
    fig, ax = plt.subplots(figsize=(6.2, 2.6))
    labels = ["房客加入", "房主关房", "客方退出"]
    before = [4.07, 3.66, 0.44]
    after = [0.90, 0.18, 0.31]
    x = np.arange(len(labels))
    w = 0.32
    ax.bar(x - w / 2, before, w, label="修复前", color=GRAY, zorder=3)
    ax.bar(x + w / 2, after, w, label="修复后", color=BLUE, zorder=3)
    for xi, v in zip(x - w / 2, before):
        ax.text(xi, v + 0.09, f"{v:.2f}", ha="center", fontsize=7.4)
    for xi, v in zip(x + w / 2, after):
        ax.text(xi, v + 0.09, f"{v:.2f}", ha="center", fontsize=7.4)
    ax.set_xticks(x, labels)
    ax.set_ylim(0, 4.9)
    ax.set_ylabel("时延 (s)")
    ax.set_title("联机关键时延")
    ax.legend(frameon=False, fontsize=8, loc="upper right")
    clean(ax)
    save(fig, "fig-net-latency")


def fig_ai():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(6.6, 2.75))
    depth = [1, 2, 3, 4, 5]
    pruned = [8, 27, 116, 215, 607]
    full = [8, 54, 347, 2162, 13078]
    ax1.plot(depth, pruned, marker="o", markersize=4, color=GREEN, label="Alpha-Beta")
    ax1.plot(depth, full, marker="s", markersize=4, color=GRAY, label="全量搜索")
    ax1.set_yscale("log")
    ax1.set_xticks(depth)
    ax1.set_xlabel("搜索深度")
    ax1.set_ylabel("节点数（对数轴）")
    ax1.set_title("（a）搜索节点数")
    ax1.legend(frameon=False, fontsize=7.5, loc="upper left")
    clean(ax1)

    d2 = [3, 4, 5]
    p2 = [0.2, 1.0, 3.2]
    f2 = [1.2, 9.6, 60.4]
    ax2.plot(d2, p2, marker="o", markersize=4, color=GREEN, label="Alpha-Beta")
    ax2.plot(d2, f2, marker="s", markersize=4, color=GRAY, label="全量搜索")
    ax2.set_yscale("log")
    ax2.set_xticks(d2)
    ax2.set_xlabel("搜索深度")
    ax2.set_ylabel("主机耗时 (ms, 对数轴)")
    ax2.set_title("（b）单次搜索耗时")
    for x, v in zip(d2, p2):
        ax2.annotate(f"{v:g}", (x, v), textcoords="offset points", xytext=(0, 8),
                     ha="center", fontsize=7.4, color=GREEN)
    for x, v in zip(d2, f2):
        ax2.annotate(f"{v:g}", (x, v), textcoords="offset points", xytext=(0, -13),
                     ha="center", fontsize=7.4, color=GRAY)
    ax2.legend(frameon=False, fontsize=7.5, loc="upper left")
    clean(ax2)
    save(fig, "fig-ai")


def fig_firmware():
    labels = ["10-15", "10-17", "10-18", "10-28", "11-07", "11-08",
              "11-09", "11-10\nv3", "11-10\nv4", "11-10\n重构", "11-13\nR-083"]
    sizes = [831152, 839584, 839552, 847344, 847944, 1056484,
             1085716, 1087220, 1093852, 1091788, 1099884]
    kib = [s / 1024 for s in sizes]
    fig, ax = plt.subplots(figsize=(6.4, 2.9))
    ax.plot(range(len(kib)), kib, marker="o", markersize=4, color=BLUE, linewidth=1.6, zorder=3)
    ax.axhline(1536, color=RED, linestyle="--", linewidth=1.0, zorder=2)
    ax.text(0.05, 1553, "资源基线 1.5 MiB（范围变更后重新基线化）", color=RED, fontsize=7.4)
    ax.axhline(1024, color=GRAY, linestyle=":", linewidth=0.9, zorder=2)
    ax.text(0.05, 1046, "早期目标 1 MiB（后由 1.5 MiB 取代）", color=GRAY, fontsize=7.4,
            va="bottom", bbox=dict(facecolor="white", edgecolor="none", pad=1.2))
    for i, v in enumerate(kib):
        ax.annotate(f"{v:.0f}", (i, v), textcoords="offset points", xytext=(0, 9),
                    ha="center", fontsize=6.8)
    ax.annotate("1,099,884 B = 1.049 MiB", xy=(10, kib[-1]), xytext=(6.7, 830),
                fontsize=7.4, color=BLUE,
                arrowprops=dict(arrowstyle="->", color=BLUE, linewidth=0.8,
                                connectionstyle="arc3,rad=-0.15"))
    ax.set_xticks(range(len(labels)), labels, fontsize=6.8)
    ax.set_ylim(780, 1680)
    ax.set_ylabel("main.bin 体积 (KiB)")
    ax.set_title("固件体积演进与资源基线")
    clean(ax)
    save(fig, "fig-firmware")



def fig_balance():
    """平衡扫描：左图按“棋盘 × 参数组合”排成热力图，右图比较同一组的两种样本量。

    数据来自 evidence/balance/20261028/；参数组编号 g 与组合的对应关系是
    g = 4*(棋盘阶数-4) + 列号，列号 0..3 固定为 A/B/C/D 四种组合。
    """
    base = ROOT / "evidence/balance/20261028"
    rows50 = list(csv.DictReader((base / "balance_scan.csv").open(encoding="utf-8")))
    rows500 = list(csv.DictReader((base / "balance_scan_500.csv").open(encoding="utf-8")))

    def rate(r):
        return 100.0 * int(r["red_wins"]) / int(r["games"])

    sizes = [4, 5, 6, 7, 8]
    conf = ["A", "B", "C", "D"]
    conf_desc = ["间隔 1 · 8/2", "间隔 3 · 8/2", "间隔 1 · 12/3", "间隔 5 · 6/1"]
    colors = ["#4C72B0", "#55A868", "#DD8452", "#C44E52"]

    grid = np.zeros((len(sizes), 4))
    for r in rows500:
        grid[sizes.index(int(r["size"])), int(r["group"]) % 4] = rate(r)

    fig, (ax1, ax2) = plt.subplots(
        1, 2, figsize=(7.0, 3.45), gridspec_kw={"width_ratios": [1.12, 1]})

    # ---------- （a）热力图 ----------
    norm = matplotlib.colors.TwoSlopeNorm(vcenter=50, vmin=45, vmax=80)
    im = ax1.imshow(grid, cmap="coolwarm", norm=norm, aspect="auto")
    for i in range(len(sizes)):
        for j in range(4):
            v = grid[i, j]
            ax1.text(j, i, f"{v:.1f}", ha="center", va="center", fontsize=8,
                     color="white" if abs(v - 50) > 8 else INK)
    ax1.set_xticks(np.arange(-0.5, 4, 1), minor=True)
    ax1.set_yticks(np.arange(-0.5, len(sizes), 1), minor=True)
    ax1.grid(which="minor", color="white", linewidth=1.2)
    ax1.tick_params(which="minor", length=0)
    ax1.set_xticks(range(4), [f"{c}\n{d}" for c, d in zip(conf, conf_desc)], fontsize=6.4)
    ax1.set_yticks(range(len(sizes)), [f"{s} 阶" for s in sizes], fontsize=7.5)
    ax1.set_xlabel("参数组合（生成间隔 · 血量/攻击）", fontsize=8)
    ax1.set_ylabel("棋盘", fontsize=8)
    ax1.set_title("（a）20 个参数组，每组 500 局", fontsize=9)
    cb = fig.colorbar(im, ax=ax1, fraction=0.045, pad=0.03)
    cb.set_label("红方胜率 (%)", fontsize=7.5)
    cb.ax.tick_params(labelsize=7)
    for s in ax1.spines.values():
        s.set_visible(False)

    # ---------- （b）50 局 vs 500 局 ----------
    ax2.plot([36, 86], [36, 86], color=GRAY, linestyle="--", linewidth=0.9, zorder=2)
    ax2.axhline(50, color=GRAY, linestyle=":", linewidth=0.8, zorder=1)
    ax2.axvline(50, color=GRAY, linestyle=":", linewidth=0.8, zorder=1)
    for r50, r500 in zip(rows50, rows500):
        j = int(r50["group"]) % 4
        ax2.scatter([rate(r50)], [rate(r500)], s=26, color=colors[j],
                    edgecolor="white", linewidth=0.5, zorder=4)
    notes = {"G3": (10, 11, "left"), "G16": (16, -14, "left"),
             "G15": (-30, -12, "right"), "G19": (2, 14, "center")}
    pos = {f"G{int(r['group'])}": (rate(r), rate(s))
           for r, s in zip(rows50, rows500)}
    for name, (dx, dy, ha) in notes.items():
        x, y = pos[name]
        ax2.annotate(name, (x, y), textcoords="offset points", xytext=(dx, dy),
                     fontsize=7.2, color=INK, ha=ha,
                     arrowprops=dict(arrowstyle="-", color=GRAY, linewidth=0.6,
                                     shrinkA=1, shrinkB=3))
    ax2.set_xlim(35, 86)
    ax2.set_ylim(44, 84)
    ax2.set_xlabel("每组 50 局的红方胜率 (%)", fontsize=8)
    ax2.set_ylabel("每组 500 局的红方胜率 (%)", fontsize=8)
    ax2.set_title("（b）同一组的 50 局与 500 局", fontsize=9)
    ax2.text(36.2, 83.0, "虚线为 $y=x$；点线为 50%", fontsize=6.4, color=GRAY, va="top")
    handles = [plt.Line2D([], [], marker="o", linestyle="", markersize=4.5,
                          color=c, label=f"{n}：{d}")
               for c, n, d in zip(colors, conf, conf_desc)]
    clean(ax2, "both")
    fig.legend(handles=handles, frameon=False, fontsize=6.8, ncol=4,
               loc="lower center", bbox_to_anchor=(0.55, -0.03),
               handletextpad=0.2, columnspacing=1.2)
    save(fig, "fig-balance")



def fig_band_map():
    """倾斜档位映射：把倾角画成一把“档位尺”。

    每行是一个可行动士兵数 N：先按 10° 一档，N 档放不下 54° 时按 54°/N 等分。
    统一用蓝色，颜色不承载分类含义；行名已说明 N。
    """
    enter, span, base_band = 6.0, 54.0, 10.0
    fig, ax = plt.subplots(figsize=(6.4, 2.55))
    rows = [(8, 2.0, "8 个兵（N=8）"), (4, 1.0, "4 个兵（N=4）"), (2, 0.0, "2 个兵（N=2）")]
    for n, y, name in rows:
        b = base_band if (n - 1) * base_band <= span else span / n
        for k in range(1, n + 1):
            x0 = enter + (k - 1) * b
            x1 = min(60.0, enter + k * b)
            ax.add_patch(mpatches.Rectangle((x0, y - 0.26), x1 - x0, 0.52, facecolor=BLUE,
                                            alpha=0.16, edgecolor=BLUE, linewidth=0.9, zorder=3))
            ax.text((x0 + x1) / 2, y, str(k), ha="center", va="center",
                    fontsize=7.6, color=INK, zorder=4)
        if enter + n * b < 60.0 - 1e-6:
            ax.text(enter + n * b + 0.8, y, f"更大倾角仍为 {n}",
                    fontsize=6.6, color=GRAY, va="center")
        tail = f"每档 {b:.2f}°" if abs(b - base_band) > 1e-6 else "每档 10°"
        ax.text(61.2, y, tail, fontsize=7.0, color=INK, va="center")
        ax.text(5.3, y, name, fontsize=7.4, color=INK, ha="right", va="center")
    ax.axvline(enter, color=GRAY, linestyle=":", linewidth=0.9, zorder=1)
    ax.text(enter + 0.5, 2.45, "进入门限 6°", fontsize=6.8, color=GRAY, va="center")
    ax.axvline(60, color=RED, linestyle="--", linewidth=0.9, zorder=1)
    ax.text(59.6, 2.45, "倾角上限 60°", fontsize=6.8, color=RED, ha="right", va="center")
    ax.set_xlim(0, 69)
    ax.set_ylim(-0.55, 2.7)
    ax.set_yticks([])
    ax.set_xticks(range(0, 61, 10))
    ax.set_xlabel("倾角 $\\theta$ (°)", fontsize=8.5)
    ax.set_title("档位映射：每个格子是一档士兵数，横轴是走到该档所需的倾角", fontsize=9.5)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.grid(axis="x", color=LIGHT, linewidth=0.7)
    ax.set_axisbelow(True)
    save(fig, "fig-band-map")




def compose_latency() -> None:
    """把输入延迟与联机时延两张图并排拼成文档用的 fig-results-latency.png。"""
    from PIL import Image, ImageDraw, ImageFont

    left = Image.open(OUT / "fig-input-latency.png").convert("RGB")
    right = Image.open(OUT / "fig-net-latency.png").convert("RGB")
    height = min(left.height, right.height, 780)
    left = left.resize((round(left.width * height / left.height), height), Image.LANCZOS)
    right = right.resize((round(right.width * height / right.height), height), Image.LANCZOS)
    gap, pad, head = 36, 12, 44
    canvas = Image.new("RGB", (pad * 2 + left.width + gap + right.width, head + height), "white")
    canvas.paste(left, (pad, head))
    canvas.paste(right, (pad + left.width + gap, head))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 28)
    ink = (31, 41, 55)
    draw.text((pad + 8, 6), "（a）输入延迟", font=font, fill=ink)
    draw.text((pad + left.width + gap + 8, 6), "（b）联机关键时延", font=font, fill=ink)
    canvas.save(OUT / "fig-results-latency.png", dpi=(300, 300), optimize=True)
    canvas.save(OUT / "fig-results-latency.pdf")
    print("OK fig-results-latency.png")


def main(argv) -> None:
    build_all = "--all" in argv
    fig_assertions()
    fig_coverage()
    fig_input_latency()
    fig_net_latency()
    compose_latency()
    fig_ai()
    fig_firmware()
    fig_balance()
    if build_all:
        fig_progress()
        fig_commit_types()
        fig_req()
        fig_band_map()


if __name__ == "__main__":
    import sys

    main(sys.argv[1:])
