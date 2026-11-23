#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 Obsidian 笔记里的草图与场景照片整理成报告插图。

来源（笔记库根目录）：
  Pasted image 20261014214321.png   立项当天的手绘棋盘草图（深色底）
  45bd93c1cb581366ac65a4cef177f172.jpg  11-13 真机界面照片
  Pasted image 20261114112351.png   手机棋类应用截图（用户场景）
  Pasted image 20261114112420.png   实体桌游照片（用户场景）
  1bbc1bf5da0b3ce43b251518763761e2.jpg  立项初期的手绘状态机（深色底）

输出到上一级目录：fig-board-sketch.png、fig-device.png、fig-scenes.png、
fig-input-sketch.png；另外把状态机草图的白底版本写到本目录 sketch-input.png，
供 fig-state-iteration.tex 通过 \\includegraphics 引用。

两张手绘草图原本是深色底上的白笔迹，直接放进 Word 会整页发黑，因此统一做
“反相 + 白底归一化”：状态机草图是中性笔迹，按灰度反相；棋盘草图带红蓝标记，
按 HSV 的明度反相，保留笔迹色相。
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

SRC = Path(r"C:\Users\MR\OneDrive\笔记笔记\Obs笔记")
HERE = Path(__file__).resolve().parent
OUT = HERE.parent


def montage(paths, height, gap, dst):
    images = [Image.open(p).convert("RGB") for p in paths]
    images = [
        im.resize((round(im.width * height / im.height), height), Image.LANCZOS)
        for im in images
    ]
    width = sum(im.width for im in images) + gap * (len(images) - 1)
    canvas = Image.new("RGB", (width, height), "white")
    x = 0
    for im in images:
        im = ImageOps.expand(im, border=1, fill=(210, 214, 220))
        canvas.paste(im, (x, 0))
        x += im.width + gap
    canvas.save(OUT / dst, dpi=(300, 300), optimize=True)
    print(f"{dst} {canvas.size}")


def pair(left_path, right_path, dst, height=760, gap=26,
         label_l="（a）", label_r="（b）", font_size=30, top=54):
    """把两张竖图并排成一张，顶部留一行 (a)/(b) 标注，省版面。"""
    from PIL import ImageDraw, ImageFont

    font = None
    for path in (r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\simhei.ttf"):
        try:
            font = ImageFont.truetype(path, font_size)
            break
        except OSError:
            continue

    panels = []
    for p in (left_path, right_path):
        im = Image.open(p).convert("RGB")
        im = im.resize((round(im.width * height / im.height), height), Image.LANCZOS)
        panels.append(ImageOps.expand(im, border=1, fill=(210, 214, 220)))

    width = sum(im.width for im in panels) + gap
    canvas = Image.new("RGB", (width, height + top), "white")
    draw = ImageDraw.Draw(canvas)
    x = 0
    for i, im in enumerate(panels):
        canvas.paste(im, (x, top))
        text = label_l if i == 0 else label_r
        if font is not None:
            draw.text((x + 4, 10), text, fill=(31, 41, 55), font=font)
        x += im.width + gap
    canvas.save(OUT / dst, dpi=(300, 300), optimize=True)
    print(f"{dst} {canvas.size}")


def invert_neutral(image):
    """白笔迹深色底 → 黑笔迹白底（灰度反相并归一化到纯白背景）。"""
    grey = np.asarray(image.convert("L")).astype(np.float32)
    inv = 255.0 - grey
    white = float(np.percentile(inv, 90)) or 255.0
    out = np.clip(inv * (255.0 / white), 0, 255).astype(np.uint8)
    return Image.fromarray(out).convert("RGB")


def paper_white(image):
    """黑底白笔迹 → 白底黑笔迹，同时保留红/蓝标记的原色。

    逐像素判断：低饱和的暗像素是底色，置白；低饱和的亮像素是笔迹，置近黑；
    有饱和度的像素是彩色标记，压暗后保留色相，避免在白底上发白看不清。
    """
    rgb = np.asarray(image.convert("RGB")).astype(np.float32)
    hsv = np.asarray(image.convert("HSV")).astype(np.float32)
    s, v = hsv[:, :, 1] / 255.0, hsv[:, :, 2] / 255.0
    flat = s < 0.25
    out = rgb.copy()
    bg = flat & (v < 0.45)
    ink = flat & (v >= 0.45)
    out[bg] = 255.0
    out[ink] = 34.0
    colour = ~flat
    out[colour] = np.clip(rgb[colour] * 0.72, 0, 255)
    return Image.fromarray(out.astype(np.uint8), "RGB")


def save(image, dst, dpi=300):
    image.save(OUT / dst, dpi=(dpi, dpi), optimize=True)
    print(f"{dst} {image.size}")


def single(src, dst, dpi=300, crop=None):
    image = Image.open(src).convert("RGB")
    if crop:
        image = image.crop(crop)
    image.save(OUT / dst, dpi=(dpi, dpi), optimize=True)
    print(f"{dst} {image.size}")


def main():
    montage(
        [SRC / "Pasted image 20261114112351.png", SRC / "Pasted image 20261114112420.png"],
        900, 28, "fig-scenes.png",
    )
    # 棋盘草图：反相成白底，保留红蓝标记
    board = Image.open(SRC / "Pasted image 20261014214321.png")
    save(paper_white(board), "fig-board-sketch.png")
    # 真机照片：裁到机身与屏幕，去掉键盘、桌面与手指
    single(SRC / "45bd93c1cb581366ac65a4cef177f172.jpg", "fig-device.png",
           crop=(150, 60, 1279, 1480))
    # 草图与真机并排成一张，正文只占一个图位
    pair(OUT / "fig-board-sketch.png", OUT / "fig-device.png",
         "fig-sketch-to-device.png",
         label_l="（a）10-15 手绘草图", label_r="（b）11-13 真机界面")

    # 状态机草图：中性笔迹，灰度高反差反相
    sketch = Image.open(SRC / "1bbc1bf5da0b3ce43b251518763761e2.jpg")
    inv = invert_neutral(sketch)
    save(inv, "fig-input-sketch.png")
    inv.save(HERE / "sketch-input.png")
    print(f"sketch-input.png {inv.size}")


if __name__ == "__main__":
    main()
