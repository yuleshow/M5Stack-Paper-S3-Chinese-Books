#!/usr/bin/env python3
"""Calculate per-size scale factor: Silver needs to be rendered at what size
to match GenYoMinTW's visual height at the same nominal size."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from convert_labels import FONT_PATH, render_label
from PIL import ImageFont

SILVER = os.path.join("sd_card", "fonts", "Silver.ttf")

# Test with representative CJK text at each unique size used in labels
SIZES = sorted(set([16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 64, 160]))
TEST_TEXT = "電子書"  # representative CJK string

gen_cache, sil_cache = {}, {}
print(f"{'Size':>4}  {'GenYo H':>8}  {'Silver H':>8}  {'Ratio':>6}  {'Silver@scaled':>14}  {'Scaled Size':>11}")
print("-" * 65)

ratios = []
for size in SIZES:
    gf = ImageFont.truetype(FONT_PATH, size)
    sf = ImageFont.truetype(SILVER, size)
    gi = render_label(TEST_TEXT, size, gf)
    si = render_label(TEST_TEXT, size, sf)
    ratio = gi.height / si.height if si.height > 0 else 1.0
    scaled_size = round(size * ratio)
    ratios.append(ratio)
    # Verify: render Silver at scaled size
    sf2 = ImageFont.truetype(SILVER, scaled_size)
    si2 = render_label(TEST_TEXT, scaled_size, sf2)
    print(f"{size:4d}  {gi.height:8d}  {si.height:8d}  {ratio:6.2f}  {si2.height:14d}  {scaled_size:11d}")

avg_ratio = sum(ratios) / len(ratios)
print(f"\nAverage ratio: {avg_ratio:.3f}")
print(f"Suggested multiplier: {avg_ratio:.2f}")
print(f"\nThis means: Silver at size {round(32 * avg_ratio)} ≈ GenYoMinTW at size 32")
