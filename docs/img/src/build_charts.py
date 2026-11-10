#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate statistical charts for docs 01/02/04.

Outputs vector PDF + 240 dpi PNG into docs/img/.  All values are taken from
docs/07, docs/04 and evidence/balance/20261028/*.csv; do not hand-edit the
numbers here without updating the docs and evidence.
"""
from pathlib import Path
import csv

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FuncFormatter

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
ROOT = HERE.parents[2]

INK = "#1F2937"
BLUE = "#2563EB"
GREEN = "#059669"
ORANGE = "#D97706"
RED = "#DC2626"
GRAY = "#6B7280"
PURPLE = "#7C3AED"

plt.rcParams.update({
    "font.sans-serif": ["Microsoft YaHei", "Noto Sans SC", "SimHei"],
    "axes.unicode_minus": False,
    "font.size": 9,
    "axes.titlesize": 10,
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


def clean(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(axis="y", color="#E5E7EB", linewidth=0.7)
    ax.set_axisbelow(True)


def save(fig, name):
    fig.tight_layout()
    fig.savefig(OUT / f"{name}.pdf", bbox_inches="tight")
    fig.savefig(OUT / f"{name}.png", dpi=240, bbox_inches="tight")
    plt.close(fig)
    print(f"OK {name}.pdf / {name}.png")


def chart_assertions():
    suites = ["规则核心", "参数模型", "联机协议/房间", "联机应用",
              "倾斜交互", "输入状态机", "参数页", "AI 搜索", "平衡扫描"]
    counts = [24572, 2517, 428, 358, 283, 105, 85, 59, 10]
    colors = [BLUE, BLUE, ORANGE, ORANGE, GREEN, GREEN, GREEN, GREEN, GRAY]
    fig, ax = plt.subplots(figsize=(6.6, 3.5))
    y = np.arange(len(suites))
    ax.barh(y, counts, color=colors, height=0.62)
    ax.set_yticks(y, suites)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(8, 60000)
    ax.set_xlabel("断言数（log 轴）")
    ax.set_title("主机自动化断言规模")
    for yi, v in zip(y, counts):
        ax.text(v * 1.12, yi, f"{v:,}", va="center", fontsize=8, color=INK)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{int(x):,}"))
    clean(ax)
    ax.grid(axis="x", color="#E5E7EB", linewidth=0.7)
    ax.grid(axis="y", visible=False)
    save(fig, "fig-results-assertions")


def chart_coverage():
    mods = ["导航", "参数页", "规则核心", "参数模型", "输入状态机", "AI 搜索", "平衡扫描"]
    vals = [100.0, 100.0, 99.5, 98.5, 91.3, 89.3, 76.5]
    colors = [GREEN, GREEN, BLUE, BLUE, GREEN, GREEN, GRAY]
    fig, ax = plt.subplots(figsize=(6.4, 3.2))
    x = np.arange(len(mods))
    ax.bar(x, vals, color=colors, width=0.58)
    ax.axhline(70, color=RED, linestyle="--", linewidth=1.0)
    for xi, v in zip(x, vals):
        ax.text(xi, v + 2.0, f"{v:.1f}%", ha="center", fontsize=8)
    ax.set_xticks(x, mods, rotation=18, ha="right")
    ax.set_ylim(0, 108)
    ax.set_ylabel("行覆盖率 (%)")
    ax.set_title("主机行覆盖率（2026-11-10）")
    clean(ax)
    save(fig, "fig-results-coverage")


def chart_latency():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.2, 3.2))
    samp = [1, 5, 10, 20]
    lat = [201, 205, 210, 220]
    ax1.plot(samp, lat, color=BLUE, marker="o", linewidth=1.8)
    for s, v in zip(samp, lat):
        ax1.annotate(f"{v} ms", (s, v), textcoords="offset points",
                     xytext=(0, 8), ha="center", fontsize=8)
    ax1.set_xlim(0, 22)
    ax1.set_ylim(195, 228)
    ax1.set_xticks(samp)
    ax1.set_xlabel("IMU 采样周期 (ms)")
    ax1.set_ylabel("倾斜到 SELECT (ms)")
    ax1.set_title("输入延迟：稳定窗口主导")
    clean(ax1)

    labels = ["房客加入", "房主关房", "客方退出"]
    before = [4.07, 3.66, 0.44]
    first = [1.06, 0.41, 0.43]
    edge = [0.90, 0.18, 0.31]
    x = np.arange(len(labels))
    w = 0.26
    ax2.bar(x - w, before, w, label="修复前", color=GRAY)
    ax2.bar(x, first, w, label="第一轮修复", color=ORANGE)
    ax2.bar(x + w, edge, w, label="链路修复后", color=BLUE)
    for xi, v in zip(x - w, before):
        ax2.text(xi, v + 0.08, f"{v:.2f}", ha="center", fontsize=7.2)
    for xi, v in zip(x, first):
        ax2.text(xi, v + 0.08, f"{v:.2f}", ha="center", fontsize=7.2)
    for xi, v in zip(x + w, edge):
        ax2.text(xi, v + 0.08, f"{v:.2f}", ha="center", fontsize=7.2)
    ax2.set_xticks(x, labels)
    ax2.set_ylim(0, 4.8)
    ax2.set_ylabel("时延 (s)")
    ax2.set_title("联机关键时延")
    ax2.legend(frameon=False, fontsize=7.5, loc="upper right")
    clean(ax2)
    save(fig, "fig-results-latency")


def chart_ai():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.2, 3.2))
    depth = [1, 2, 3, 4, 5]
    pruned = [8, 27, 116, 215, 607]
    full = [8, 54, 347, 2162, 13078]
    ax1.plot(depth, pruned, marker="o", color=GREEN, label="Alpha-Beta")
    ax1.plot(depth, full, marker="s", color=GRAY, label="全量搜索")
    ax1.set_yscale("log")
    ax1.set_xticks(depth)
    ax1.set_xlabel("搜索深度")
    ax1.set_ylabel("节点数（log）")
    ax1.set_title("剪枝节点数")
    ax1.annotate("607 vs 13,078\n−95.4%", (5, 607), textcoords="offset points",
                 xytext=(-46, 18), fontsize=7.8, color=INK)
    ax1.legend(frameon=False, fontsize=7.5)
    clean(ax1)

    d2 = [3, 4, 5]
    p2 = [0.2, 1.0, 3.2]
    f2 = [1.2, 9.6, 60.4]
    ax2.plot(d2, p2, marker="o", color=GREEN, label="Alpha-Beta")
    ax2.plot(d2, f2, marker="s", color=GRAY, label="全量搜索")
    ax2.set_yscale("log")
    ax2.set_xticks(d2)
    ax2.set_xlabel("搜索深度")
    ax2.set_ylabel("主机耗时 (ms, log)")
    ax2.set_title("剪枝耗时")
    for x, v in zip(d2, p2):
        ax2.annotate(f"{v:.1f}", (x, v), textcoords="offset points",
                     xytext=(0, 7), ha="center", fontsize=7.5, color=GREEN)
    for x, v in zip(d2, f2):
        ax2.annotate(f"{v:g}", (x, v), textcoords="offset points",
                     xytext=(0, -12), ha="center", fontsize=7.5, color=GRAY)
    ax2.legend(frameon=False, fontsize=7.5)
    clean(ax2)
    save(fig, "fig-results-ai")


def chart_balance():
    rows50 = list(csv.DictReader((ROOT / "evidence/balance/20261028/balance_scan.csv").open(encoding="utf-8")))
    rows500 = list(csv.DictReader((ROOT / "evidence/balance/20261028/balance_scan_500.csv").open(encoding="utf-8")))
    g = [int(r["group"]) for r in rows50]
    r50 = [100 * int(r["red_wins"]) / int(r["games"]) for r in rows50]
    r500 = [100 * int(r["red_wins"]) / int(r["games"]) for r in rows500]
    fig, ax = plt.subplots(figsize=(8.0, 3.3))
    ax.plot(g, r50, marker="o", markersize=4, color=ORANGE, linewidth=1.2, label="50 局")
    ax.plot(g, r500, marker="o", markersize=4, color=BLUE, linewidth=1.4, label="500 局")
    ax.axhline(50, color=GRAY, linestyle=":", linewidth=0.9)
    ax.axhline(72, color=RED, linestyle="--", linewidth=0.9)
    ax.text(0.0, 72.8, "72%", color=RED, fontsize=7.5, ha="left")
    ax.axvspan(14.5, 15.5, color=GREEN, alpha=0.08)
    ax.axvspan(18.5, 19.5, color=GREEN, alpha=0.08)
    ax.annotate("G15 73.8%", (15, r500[15]), textcoords="offset points",
                xytext=(-8, 10), fontsize=7.5, color=GREEN, ha="right")
    ax.annotate("G19 77.8%", (19, r500[19]), textcoords="offset points",
                xytext=(-12, 10), fontsize=7.5, color=GREEN, ha="right")
    ax.set_xticks(g)
    ax.set_xlim(-0.5, 19.5)
    ax.set_ylim(30, 90)
    ax.set_xlabel("参数组")
    ax.set_ylabel("红方胜率 (%)")
    ax.set_title("平衡扫描：红方胜率（基线贪心，非 AI）")
    ax.legend(frameon=False, fontsize=8, loc="lower right")
    clean(ax)
    save(fig, "fig-results-balance")


def chart_firmware():
    labels = ["10-15", "10-17", "10-18", "10-28", "11-07",
              "11-08", "11-09", "11-10\nv3", "11-10\nv4", "11-10\n重构"]
    sizes = [831152, 839584, 839552, 847344, 847944,
             1056484, 1085716, 1087220, 1093852, 1091788]
    kib = [s / 1024 for s in sizes]
    fig, ax = plt.subplots(figsize=(7.6, 3.2))
    ax.plot(range(len(kib)), kib, marker="o", color=BLUE, linewidth=1.8)
    ax.axhline(1536, color=RED, linestyle="--", linewidth=1.0)
    ax.text(0.2, 1552, "1.5 MiB 基线（范围变更后）", color=RED, fontsize=7.8)
    for i, v in enumerate(kib):
        ax.annotate(f"{v:.0f}", (i, v), textcoords="offset points",
                    xytext=(0, 7), ha="center", fontsize=7.0)
    ax.set_xticks(range(len(labels)), labels, fontsize=7.2)
    ax.set_ylim(790, 1600)
    ax.set_ylabel("main.bin (KiB)")
    ax.set_title("固件体积演进")
    clean(ax)
    save(fig, "fig-firmware-timeline")


if __name__ == "__main__":
    chart_assertions()
    chart_coverage()
    chart_latency()
    chart_ai()
    chart_balance()
    chart_firmware()
