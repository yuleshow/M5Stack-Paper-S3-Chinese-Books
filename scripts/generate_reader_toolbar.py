#!/usr/bin/env python3
"""Generate the reader toolbar bitmap (字-, 字+, 字型, BM) as a PNG icon.

The bitmap covers the space between the navigation arrows and the return button
in the book reader's bottom bar. Dynamic content (font size number, bookmark
highlight) is drawn on top by the firmware.

Output: assets/icons/reader_toolbar.png  (220 × 40 px, RGBA)
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

WIDTH = 260
HEIGHT = 40

# Button layout: (x, width, label, font_size)
# Gap between buttons 0 and 2 (x=45..80) is left blank for dynamic font size
BUTTONS = [
    (0,   45, "−",  24),   # font decrease (minus sign)
    (80,  45, "+",  24),   # font increase
    (130, 45, "Aa", 20),   # font selection
    (180, 35, "≡",  22),   # index / TOC
    (220, 40, "★",  20),   # bookmark
]

# Find a CJK font
CJK_FONT_PATHS = [
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/Supplemental/Songti.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
    "/System/Library/Fonts/PingFang.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
]

LATIN_FONT_PATHS = [
    "/System/Library/Fonts/Helvetica.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]


def find_font(candidates, size):
    for fp in candidates:
        if os.path.exists(fp):
            try:
                return ImageFont.truetype(fp, size)
            except Exception:
                continue
    return ImageFont.load_default()


def main():
    img = Image.new("RGBA", (WIDTH, HEIGHT), (255, 255, 255, 255))
    draw = ImageDraw.Draw(img)

    for x, w, label, sz in BUTTONS:
        # Draw button border (1px black rect)
        draw.rectangle([x, 0, x + w - 1, HEIGHT - 1], outline="black", width=1)

        # Use a font that has the needed symbols
        font = find_font(CJK_FONT_PATHS + LATIN_FONT_PATHS, sz)

        # Center text in button
        bbox = draw.textbbox((0, 0), label, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = x + (w - tw) // 2 - bbox[0]
        ty = (HEIGHT - th) // 2 - bbox[1]
        draw.text((tx, ty), label, fill="black", font=font)

    out_path = os.path.join("assets", "icons", "reader_toolbar.png")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    print(f"Generated {out_path} ({WIDTH}x{HEIGHT})")


if __name__ == "__main__":
    main()
