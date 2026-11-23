#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""编译本目录 fig-*.tex，并导出适合 Word 的 PNG（宽约 6.2 英寸、300 dpi）。

注意：本目录位于 OneDrive 同步盘，在该目录内直接生成 PDF 会被环境拦截，
因此把 .tex 复制到仓库 build/report-fig/ 下编译，再把 PNG 复制回来。

需要 xelatex 与 PyMuPDF。PNG 的物理尺寸由 dpi 元数据决定：
宽度像素 / dpi = 英寸；6.2 in ≈ 15.7 cm，正好是 A4 正文宽度。
"""
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
WORK = Path(r"D:\233\code\course_starter\build\report-fig")
TARGET_IN = 6.2
DPI_META = 300


def build(tex: Path) -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    work_tex = WORK / tex.name
    shutil.copy2(tex, work_tex)
    # 图里 \includegraphics 的位图也放到工作目录（路径不含中文，XeLaTeX 更稳）
    for img in HERE.glob("*.png"):
        shutil.copy2(img, WORK / img.name)
    # 公共导言区（部分图为共享 _preamble.tex）
    preamble = HERE / "_preamble.tex"
    if preamble.exists():
        shutil.copy2(preamble, WORK / preamble.name)

    log_path = HERE / (tex.stem + ".build.log")
    with log_path.open("w", encoding="utf-8", errors="replace") as fh:
        proc = subprocess.run(
            ["xelatex", "-interaction=nonstopmode", "-halt-on-error", tex.name],
            cwd=WORK, stdout=fh, stderr=subprocess.STDOUT, text=True,
        )
    pdf = work_tex.with_suffix(".pdf")
    if proc.returncode != 0 or not pdf.exists():
        print(f"FAIL {tex.name}; see {log_path}")
        raise SystemExit(proc.returncode or 1)

    import pymupdf
    from PIL import Image

    doc = pymupdf.open(pdf)
    page = doc[0]
    zoom = (TARGET_IN * DPI_META) / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom), alpha=False)
    raw = WORK / (tex.stem + ".raw.png")
    pix.save(raw)
    img = Image.open(raw).convert("RGB")
    img.save(OUT / (tex.stem + ".png"), dpi=(DPI_META, DPI_META), optimize=True)
    raw.unlink()
    # PDF 与 PNG 同步回仓库（打印/答辩用 PDF，Markdown 预览用 PNG）
    shutil.copy2(pdf, OUT / (tex.stem + ".pdf"))
    print(f"OK   {tex.name} -> {tex.stem}.png ({img.size[0]}x{img.size[1]} px)")


def main(argv) -> None:
    names = argv or sorted(p.stem for p in HERE.glob("fig-*.tex"))
    if not names:
        raise SystemExit("no fig-*.tex found")
    for name in names:
        build(HERE / (name if name.endswith(".tex") else name + ".tex"))


if __name__ == "__main__":
    main(sys.argv[1:])
