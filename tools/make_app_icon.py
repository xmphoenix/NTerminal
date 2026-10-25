#!/usr/bin/env python3
"""生成 NTerminal 应用图标（resources/icons/app.ico / app.png）。

设计：青绿到蓝的对角渐变圆角方块 + 白色终端提示符 ">_"，顶部内高光与
细内描边增加体积感；16px 下仍能清楚辨认。所有几何在 4096 画布上绘制，
再缩放导出，保证小尺寸边缘干净。

用法： python tools/make_app_icon.py
依赖： Pillow, numpy
"""

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "resources" / "icons"

# 超采样画布：1024 设计单位 × 4
SS = 4096
SCALE = SS / 1024.0

TEAL = np.array([0x12, 0xB5, 0xA4], dtype=np.float64)
BLUE = np.array([0x25, 0x63, 0xEB], dtype=np.float64)
CORNER_RADIUS = 232.0
STROKE = 92.0  # 设计稿 1024 单位下的提示符线宽


def gradient_canvas() -> Image.Image:
    yy, xx = np.mgrid[0:SS, 0:SS]
    t = (xx + yy) / (2.0 * (SS - 1))
    rgb = TEAL + (BLUE - TEAL) * t[..., None]

    # 左上柔光：让纯渐变有光源感
    cx, cy = 0.16 * SS, 0.10 * SS
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (0.95 * SS)
    glow = np.clip(1.0 - dist, 0.0, 1.0) ** 2.2 * 0.20
    rgb = rgb * (1.0 - glow[..., None]) + 255.0 * glow[..., None]

    # 右下轻微压暗，避免整块颜色发飘
    cx, cy = 0.92 * SS, 0.96 * SS
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (0.75 * SS)
    shade = np.clip(1.0 - dist, 0.0, 1.0) ** 2.0 * 0.12
    rgb = rgb * (1.0 - shade[..., None])

    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def rounded_mask() -> Image.Image:
    mask = Image.new("L", (SS, SS), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle((0, 0, SS - 1, SS - 1),
                           radius=int(CORNER_RADIUS * SCALE), fill=255)
    return mask


def draw_prompt(layer: Image.Image) -> None:
    draw = ImageDraw.Draw(layer)
    width = int(STROKE * SCALE)
    half = width / 2

    def p(x: float, y: float) -> tuple[float, float]:
        return (x * SCALE, y * SCALE)

    # ">" 两段折线 + 圆头线帽/圆角转折
    chevron = [p(306, 338), p(484, 512), p(306, 686)]
    draw.line(chevron, fill=(255, 255, 255, 255), width=width, joint="curve")
    for x, y in chevron:
        draw.ellipse((x - half, y - half, x + half, y + half),
                     fill=(255, 255, 255, 255))

    # "_" 下划线
    underline = [p(580, 678), p(718, 678)]
    draw.line(underline, fill=(255, 255, 255, 255), width=width)
    for x, y in underline:
        draw.ellipse((x - half, y - half, x + half, y + half),
                     fill=(255, 255, 255, 255))

    # 细内描边：增加"玻璃质感"
    inset = 10 * SCALE
    draw.rounded_rectangle((inset, inset, SS - inset, SS - inset),
                           radius=int((CORNER_RADIUS - 10) * SCALE),
                           outline=(255, 255, 255, 40),
                           width=max(2, int(4 * SCALE)))


def build_master() -> Image.Image:
    canvas = gradient_canvas()
    prompt = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    draw_prompt(prompt)
    canvas = Image.alpha_composite(canvas, prompt)
    canvas.putalpha(rounded_mask())
    return canvas.resize((1024, 1024), Image.Resampling.LANCZOS)


def main() -> None:
    master = build_master()

    # 运行时窗口图标（qrc 内嵌）
    master.resize((256, 256), Image.Resampling.LANCZOS).save(OUT_DIR / "app.png")

    # Windows 可执行文件图标：多尺寸 ICO，PIL 会为大图生成各档 LANCZOS 副本
    sizes = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
             (48, 48), (64, 64), (128, 128), (256, 256)]
    master.save(OUT_DIR / "app.ico", format="ICO", sizes=sizes)

    print(f"wrote {OUT_DIR / 'app.png'}")
    print(f"wrote {OUT_DIR / 'app.ico'}")


if __name__ == "__main__":
    main()
