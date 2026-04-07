#!/usr/bin/env python3
"""
Generate Silver font pre-rendered label bitmaps as PROGMEM C headers.

Usage:
  python3 convert_labels_silver.py

Renders the same label strings as convert_labels.py but using the Silver TTF
font with per-size scaling to match GenYoMinTW visual size.  Outputs go to
src/labels/silver/ with a master header silver_label_bitmaps.h.

This is a TEMPORARY solution for development.  Eventually the Silver labels
will be served from SD card (labels.bin).
"""

import os
import sys
import math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_labels import (
    LABELS, KAI_FONT_PATH, render_label, image_to_4bit, write_header,
)

from PIL import ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ── Configuration ──────────────────────────────────────────────────────────────

SILVER_PATH = os.path.join("sd_card", "fonts", "Silver.ttf")
GENYO_PATH  = os.path.join("sd_card", "fonts", "GenYoMinTW-Regular.ttf")
OUT_DIR     = os.path.join("src", "labels", "silver")


def compute_silver_scale(font_size, genyo_cache, silver_cache):
    """Compute exact scale factor so Silver matches GenYoMinTW visual height."""
    test_text = "電子書"
    if font_size not in genyo_cache:
        genyo_cache[font_size] = ImageFont.truetype(GENYO_PATH, font_size)
    if font_size not in silver_cache:
        silver_cache[font_size] = ImageFont.truetype(SILVER_PATH, font_size)
    gi = render_label(test_text, font_size, genyo_cache[font_size])
    si = render_label(test_text, font_size, silver_cache[font_size])
    return gi.height / si.height if si.height > 0 else 1.0


def main():
    if not os.path.exists(SILVER_PATH):
        print(f"ERROR: Silver font not found: {SILVER_PATH}")
        sys.exit(1)
    if not os.path.exists(GENYO_PATH):
        print(f"ERROR: Reference font not found: {GENYO_PATH}")
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)

    # Include ALL labels — Kai-font labels are rendered with Silver when Silver is active
    silver_labels = [(e[0], e[1], e[2]) for e in LABELS]
    print(f"Silver labels: {len(silver_labels)} (including Kai-font labels rendered with Silver)")

    # Compute per-size scale factors
    genyo_cache, silver_cache = {}, {}
    unique_sizes = sorted(set(e[1] for e in silver_labels))
    scale_map = {}
    print("\nPer-size scale factors:")
    for sz in unique_sizes:
        ratio = compute_silver_scale(sz, genyo_cache, silver_cache)
        scaled = round(sz * ratio)
        scale_map[sz] = (ratio, scaled)
        print(f"  {sz:3d}pt → render at {scaled:3d}pt  (×{ratio:.2f})")
    print()

    # Render all labels
    font_cache = {}
    labels_info = []  # (text, fontSize, var_suffix, filename)
    total_bytes = 0

    for entry in silver_labels:
        text, font_size, var_suffix = entry[0], entry[1], entry[2]
        _, render_size = scale_map[font_size]

        if render_size not in font_cache:
            font_cache[render_size] = ImageFont.truetype(SILVER_PATH, render_size)

        font = font_cache[render_size]
        img = render_label(text, render_size, font)
        w, h = img.size
        data = image_to_4bit(img)
        total_bytes += len(data)

        var_name = f"slabel_{var_suffix}"
        filename = f"slabel_{var_suffix}.h"
        header_path = os.path.join(OUT_DIR, filename)
        write_header(var_name, data, w, h, header_path, text, font_size)

        labels_info.append((text, font_size, var_suffix, filename))
        print(f"  {text:30s}  size={font_size:2d}→{render_size:2d}  {w:3d}x{h:<3d}  {len(data):5d} bytes")

    # Write master header
    master_path = os.path.join(OUT_DIR, "silver_label_bitmaps.h")
    with open(master_path, "w", encoding="utf-8") as f:
        f.write("// Auto-generated Silver font label bitmaps (TEMPORARY — will move to SD card)\n")
        f.write("// Do not edit — regenerate with convert_labels_silver.py\n")
        f.write("#pragma once\n\n")
        f.write("#include <pgmspace.h>\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <string.h>\n\n")

        for text, size, var_suffix, filename in labels_info:
            f.write(f'#include "{filename}"\n')

        f.write("\n")
        f.write("// Reuse LabelBitmap struct from label_bitmaps.h\n")
        f.write(f"const int kSilverLabelBitmapCount = {len(labels_info)};\n\n")
        f.write("const LabelBitmap kSilverLabelBitmaps[] PROGMEM = {\n")
        for text, size, var_suffix, filename in labels_info:
            var = f"slabel_{var_suffix}"
            c_text = text.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'  {{"{c_text}", {size}, {var}_w, {var}_h, {var}_bitmap}},\n')
        f.write("};\n\n")

        f.write("// Find a Silver pre-rendered label bitmap by text and font size.\n")
        f.write("// Returns nullptr if not found.\n")
        f.write("inline const LabelBitmap* findSilverLabelBitmap(const char* text, uint16_t fontSize) {\n")
        f.write("  for (int i = 0; i < kSilverLabelBitmapCount; i++) {\n")
        f.write("    if (kSilverLabelBitmaps[i].fontSize == fontSize && strcmp(kSilverLabelBitmaps[i].text, text) == 0) {\n")
        f.write("      return &kSilverLabelBitmaps[i];\n")
        f.write("    }\n")
        f.write("  }\n")
        f.write("  return nullptr;\n")
        f.write("}\n")

    print(f"\nGenerated {len(labels_info)} Silver label headers in {OUT_DIR}/")
    print(f"Total bitmap data: {total_bytes:,} bytes ({total_bytes/1024:.1f} KB)")
    print(f"Master header: {master_path}")
    print("Done!")


if __name__ == "__main__":
    main()
