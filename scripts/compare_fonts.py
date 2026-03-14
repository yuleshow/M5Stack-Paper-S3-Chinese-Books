#!/usr/bin/env python3
"""Compare GenYoMinTW vs Silver font rendering side by side."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from convert_labels import render_label, FONT_PATH
from PIL import Image, ImageDraw, ImageFont

SILVER_PATH = os.path.join("sd_card", "fonts", "Silver.ttf")

SAMPLES = [
    ("電子書", 32),
    ("日曆", 32),
    ("設定", 40),
    ("星期五", 30),
    ("農曆", 30),
    ("祈福 出行 納采 嫁娶", 24),
    ("3", 160),
    ("時辰吉凶", 24),
    ("天氣", 32),
    ("壁紙選擇", 36),
]

gen_cache = {}
sil_cache = {}

rows = []
for text, size in SAMPLES:
    if size not in gen_cache:
        gen_cache[size] = ImageFont.truetype(FONT_PATH, size)
    if size not in sil_cache:
        sil_cache[size] = ImageFont.truetype(SILVER_PATH, size)

    img_gen = render_label(text, size, gen_cache[size])
    img_sil = render_label(text, size, sil_cache[size])
    rows.append((text, size, img_gen, img_sil))

# Compose comparison image
PAD = 10
LABEL_W = 250
col1_w = max(r[2].width for r in rows)
col2_w = max(r[3].width for r in rows)
total_w = LABEL_W + col1_w + PAD + col2_w + PAD * 3
total_h = sum(max(r[2].height, r[3].height) for r in rows) + PAD * (len(rows) + 3) + 40

out = Image.new("L", (total_w, total_h), 255)
draw = ImageDraw.Draw(out)

# Header
header_font = ImageFont.truetype(FONT_PATH, 20)
y = PAD
draw.text((LABEL_W + PAD, y), "GenYoMinTW", font=header_font, fill=0)
draw.text((LABEL_W + col1_w + PAD * 2, y), "Silver", font=header_font, fill=0)
y += 30
draw.line([(0, y), (total_w, y)], fill=128)
y += PAD

for text, size, img_gen, img_sil in rows:
    row_h = max(img_gen.height, img_sil.height)
    # Label
    label_font = ImageFont.truetype(FONT_PATH, 16)
    draw.text((PAD, y + row_h // 2 - 8), f"{text} ({size}pt)", font=label_font, fill=0)
    # GenYoMinTW
    out.paste(img_gen, (LABEL_W + PAD, y))
    # Silver
    out.paste(img_sil, (LABEL_W + col1_w + PAD * 2, y))
    y += row_h + PAD
    draw.line([(LABEL_W, y - PAD // 2), (total_w, y - PAD // 2)], fill=220)

out.save("output/font_comparison.png")
print(f"Saved to output/font_comparison.png ({total_w}x{total_h})")
