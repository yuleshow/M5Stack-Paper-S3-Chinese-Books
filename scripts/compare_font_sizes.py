#!/usr/bin/env python3
"""Compare rendered bitmap sizes between GenYoMinTW and Silver."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from convert_labels import LABELS, FONT_PATH, render_label, image_to_4bit
from PIL import ImageFont

SILVER = os.path.join("sd_card", "fonts", "Silver.ttf")
gen_cache, sil_cache = {}, {}
gen_bytes, sil_bytes = 0, 0
gen_area, sil_area = 0, 0

for entry in LABELS:
    if len(entry) > 3: continue
    text, size = entry[0], entry[1]
    if size not in gen_cache:
        gen_cache[size] = ImageFont.truetype(FONT_PATH, size)
    if size not in sil_cache:
        sil_cache[size] = ImageFont.truetype(SILVER, size)
    gi = render_label(text, size, gen_cache[size])
    si = render_label(text, size, sil_cache[size])
    gen_area += gi.size[0] * gi.size[1]
    sil_area += si.size[0] * si.size[1]
    gen_bytes += len(image_to_4bit(gi))
    sil_bytes += len(image_to_4bit(si))

print("=== Rendered Label Size Comparison ===\n")
print(f"GenYoMinTW total pixel area: {gen_area:>10,} px")
print(f"Silver total pixel area:     {sil_area:>10,} px")
print(f"Silver is {(1-sil_area/gen_area)*100:.1f}% smaller in pixel area\n")
print(f"GenYoMinTW label bitmaps: {gen_bytes:>10,} bytes ({gen_bytes/1024:.1f} KB)")
print(f"Silver label bitmaps:     {sil_bytes:>10,} bytes ({sil_bytes/1024:.1f} KB)")
print(f"Difference:               {gen_bytes-sil_bytes:>10,} bytes ({(gen_bytes-sil_bytes)/1024:.1f} KB)")
print(f"Silver is {(1-sil_bytes/gen_bytes)*100:.1f}% smaller in bitmap data")
