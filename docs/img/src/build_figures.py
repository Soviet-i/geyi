#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Compile fig-*.tex with XeLaTeX and export PDF + PNG into docs/img/.

Usage: python docs/img/src/build_figures.py [fig-name ...]
Requires: xelatex (TinyTeX/TeX Live) and PyMuPDF (import pymupdf).
"""
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
DPI = 240


def compile_one(tex: Path) -> None:
    log = tex.with_suffix(".build.log")
    with log.open("w", encoding="utf-8", errors="replace") as fh:
        proc = subprocess.run(
            ["xelatex", "-interaction=nonstopmode", "-halt-on-error", tex.name],
            cwd=HERE, stdout=fh, stderr=subprocess.STDOUT, text=True,
        )
    if proc.returncode != 0:
        print(f"FAIL {tex.name}; see {log}")
        raise SystemExit(proc.returncode)
    pdf = tex.with_suffix(".pdf")
    if not pdf.exists():
        print(f"FAIL {tex.name}: no PDF")
        raise SystemExit(1)
    target_pdf = OUT / (tex.stem + ".pdf")
    target_pdf.write_bytes(pdf.read_bytes())

    try:
        import pymupdf  # noqa: WPS433
    except ImportError:  # older PyMuPDF exposes the fitz module name
        import fitz as pymupdf  # type: ignore
    doc = pymupdf.open(target_pdf)
    pix = doc[0].get_pixmap(dpi=DPI, alpha=False)
    target_png = OUT / (tex.stem + ".png")
    pix.save(target_png)
    print(f"OK   {tex.name} -> {target_pdf.name} ({target_pdf.stat().st_size} B), "
          f"{target_png.name} ({target_png.stat().st_size} B)")


def main(argv):
    names = argv or sorted(p.stem for p in HERE.glob("fig-*.tex"))
    if not names:
        raise SystemExit("no fig-*.tex found")
    for name in names:
        tex = HERE / (name if name.endswith(".tex") else name + ".tex")
        if not tex.exists():
            raise SystemExit(f"missing {tex}")
        compile_one(tex)


if __name__ == "__main__":
    main(sys.argv[1:])
