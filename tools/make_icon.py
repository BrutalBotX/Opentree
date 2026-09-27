"""Generates the OpenTree application icon.

The mark is a stylised tree: a trunk that branches into three nodes, on a rounded graphite
square that matches the neutral theme. Run from the repository root:

    python tools/make_icon.py

It rewrites appicon.ico (embedded by resources/opentree.rc and the .qrc), assets/opentree.ico
and assets/opentree-icon.png.
"""
from __future__ import annotations

import os

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCALE = 4  # supersampling factor for crisp edges
SIZES = [256, 128, 64, 48, 32, 24, 16]

# Neutral palette (matches the built-in dark theme).
BACKDROP_TOP = (48, 50, 55, 255)      # #303237
BACKDROP_BOTTOM = (28, 29, 32, 255)   # #1c1d20
EDGE = (74, 77, 84, 255)              # #4a4d54
BRANCH = (150, 168, 194, 255)         # #96a8c2
NODE = (203, 216, 234, 255)           # #cbd8ea
NODE_ACCENT = (122, 156, 205, 255)    # #7a9ccd


def rounded_mask(size: int, radius: int) -> Image.Image:
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)
    return mask


def vertical_gradient(size: int, top: tuple, bottom: tuple) -> Image.Image:
    gradient = Image.new("RGBA", (1, size))
    for y in range(size):
        ratio = y / max(1, size - 1)
        gradient.putpixel((0, y), tuple(
            int(top[channel] + (bottom[channel] - top[channel]) * ratio) for channel in range(4)
        ))
    return gradient.resize((size, size))


def draw_mark(image: Image.Image, size: int) -> None:
    draw = ImageDraw.Draw(image)
    unit = size / 256.0

    def point(x: float, y: float) -> tuple[float, float]:
        return (x * unit, y * unit)

    line_width = max(2, int(11 * unit))
    node_radius = 16 * unit
    accent_radius = 18 * unit

    trunk_base = point(128, 212)
    trunk_top = point(128, 138)
    left_end = point(58, 104)
    right_end = point(198, 104)
    top_end = point(128, 46)

    # branches
    draw.line([trunk_base, trunk_top], fill=BRANCH, width=line_width, joint="curve")
    draw.line([trunk_top, left_end], fill=BRANCH, width=line_width, joint="curve")
    draw.line([trunk_top, right_end], fill=BRANCH, width=line_width, joint="curve")
    draw.line([trunk_top, top_end], fill=BRANCH, width=line_width, joint="curve")

    for center, radius, color in (
        (left_end, node_radius, NODE),
        (right_end, node_radius, NODE),
        (top_end, accent_radius, NODE_ACCENT),
    ):
        x, y = center
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=color)


def build_icon(size: int) -> Image.Image:
    work = size * SCALE
    backdrop = vertical_gradient(work, BACKDROP_TOP, BACKDROP_BOTTOM).convert("RGBA")
    mask = rounded_mask(work, radius=int(work * 0.22))
    canvas = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    canvas.paste(backdrop, (0, 0), mask)

    outline = ImageDraw.Draw(canvas)
    outline.rounded_rectangle(
        (1 * SCALE, 1 * SCALE, work - 1 - 1 * SCALE, work - 1 - 1 * SCALE),
        radius=int(work * 0.22),
        outline=EDGE,
        width=max(1, SCALE),
    )

    draw_mark(canvas, work)
    return canvas.resize((size, size), Image.LANCZOS)


def main() -> None:
    frames = {size: build_icon(size) for size in SIZES}

    ico_path = os.path.join(REPO, "appicon.ico")
    frames[256].save(ico_path, format="ICO", sizes=[(size, size) for size in SIZES])
    ico_asset = os.path.join(REPO, "assets", "opentree.ico")
    os.makedirs(os.path.dirname(ico_asset), exist_ok=True)
    frames[256].save(ico_asset, format="ICO", sizes=[(size, size) for size in SIZES])
    frames[256].save(os.path.join(REPO, "assets", "opentree-icon.png"), format="PNG")

    print("icon written:", ico_path)
    print("icon written:", ico_asset)
    print("image written:", os.path.join(REPO, "assets", "opentree-icon.png"))


if __name__ == "__main__":
    main()
