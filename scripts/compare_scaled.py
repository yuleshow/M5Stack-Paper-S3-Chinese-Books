#!/usr/bin/env python3
"""Compare GenYoMinTW vs Silver (raw) vs Silver (per-size scaled) side by side."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from convert_labels import render_label, FONT_PATH
from PIL import Image, ImageDraw, ImageFont

SILVER_PATH = os.path.join("sd_card", "fonts", "Silver.ttf")

# Compute per-size scale factors dynamically
def compute_scale_map(sizes):
    test_text = "電子書"
    scale_map = {}
    for sz in sizes:
        gf = ImageFont.truetype(FONT_PATH, sz)
        sf = ImageFont.truetype(SILVER_PATH, sz)
        gi = render_label(test_text, sz, gf)
        si = render_label(test_text, sz, sf)
        ratio = gi.height / si.height if si.height > 0 else 1.0
        scale_map[sz] = round(sz * ratio)
    return scale_map

SAMPLES = [
    ("電子書", 32),
    ("日曆", 32),
    ("設定", 40),
    ("待辦事項", 32),
    ("祈福 出行 納采 嫁娶", 24),
    ("天氣", 32),
    ("壁紙選擇", 36),
    ("星期五", 28),
    ("時辰吉凶", 24),
    ("甲子", 26),
    ("吉", 16),
    ("觀音靈籖", 36),
]

unique_sizes = sorted(set(s for _, s in SAMPLES))
scale_map = compute_scale_map(unique_sizes)

gen_cache, sil_cache = {}, {}
rows = []
for text, size in SAMPLES:
    scaled_size = scale_map[size]
    for sz in (size, scaled_size):
        if sz not in gen_cache:
            gen_cache[sz] = ImageFont.truetype(FONT_PATH, sz)
        if sz not in sil_cache:
            sil_cache[sz] = ImageFont.truetype(SILVER_PATH, sz)

    img_gen = render_label(text, size, gen_cache[size])
    img_sil = render_label(text, size, sil_cache[size])
    img_scaled = render_label(text, scaled_size, sil_cache[scaled_size])
    rows.append((text, size, scaled_size, img_gen, img_sil, img_scaled))

# Compose comparison image
PAD = 12
LABEL_W = 260
col1_w = max(r[3].width for r in rows) + 10
col2_w = max(r[4].width for r in rows) + 10
col3_w = max(r[5].width for r in rows) + 10
total_w = LABEL_W + col1_w + col2_w + col3_w + PAD * 5
total_h = sum(max(r[3].height, r[4].height, r[5].height) for r in rows) + PAD * (len(rows) + 4) + 50

out = Image.new("L", (total_w, total_h), 255)
draw = ImageDraw.Draw(out)

header_font = ImageFont.truetype(FONT_PATH, 18)
y = PAD

# Headers
x_col1 = LABEL_W + PAD
x_col2 = x_col1 + col1_w + PAD
x_col3 = x_col2 + col2_w + PAD
draw.text((x_col1, y), "GenYoMinTW", font=header_font, fill=0)
draw.text((x_col2, y), "Silver (raw)", font=header_font, fill=0)
draw.text((x_col3, y), "Silver (scaled)", font=header_font, fill=0)
y += 28
draw.line([(0, y), (total_w, y)], fill=100)
y += PAD

label_font = ImageFont.truetype(FONT_PATH, 14)

for text, size, scaled_size, img_gen, img_sil, img_scaled in rows:
    row_h = max(img_gen.height, img_sil.height, img_scaled.height)
    # Label column
    draw.text((PAD, y), f"{text}", font=label_font, fill=0)
    draw.text((PAD, y + 18), f"{size}pt → {scaled_size}pt", font=label_font, fill=80)
    # GenYoMinTW
    out.paste(img_gen, (x_col1, y))
    # Silver raw
    out.paste(img_sil, (x_col2, y))
    # Silver scaled
    out.paste(img_scaled, (x_col3, y))
    y += row_h + PAD
    draw.line([(LABEL_W, y - PAD // 2), (total_w, y - PAD // 2)], fill=220)

# Trim unused bottom space
out = out.crop((0, 0, total_w, y + PAD))
out.save("output/font_comparison_scaled.png")
print(f"Saved to output/font_comparison_scaled.png ({total_w}x{out.height})")
print(f"\nScale map used:")
for sz in unique_sizes:
    print(f"  {sz}pt → {scale_map[sz]}pt")
