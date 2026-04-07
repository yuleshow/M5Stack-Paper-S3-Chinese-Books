#!/usr/bin/env python3
"""
Generate a single binary label file for the SD card (Unifont system font).

Usage:
  python3 convert_labels_sd.py [--font FONT_PATH]

Renders the same label strings as convert_labels.py but using the Unifont font,
and packs all bitmaps into a single binary file for SD card storage.

Binary format:
  Header:
    4 bytes: magic "SLBL"
    4 bytes: uint32_t count (LE)
  For each entry:
    2 bytes: uint16_t textLen (byte length of UTF-8 text, LE)
    textLen+1 bytes: text (UTF-8, null-terminated)
    2 bytes: uint16_t fontSize (LE)
    2 bytes: uint16_t w (LE)
    2 bytes: uint16_t h (LE)
    ceil(w/2)*h bytes: bitmap data (4-bit packed, same as PROGMEM labels)
"""

import os
import sys
import struct

# Add scripts dir to path so we can import from convert_labels
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_labels import LABELS, KAI_FONT_PATH, render_label, image_to_4bit

from PIL import ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ── Configuration ──────────────────────────────────────────────────────────────

SILVER_PATH = os.path.join("sd_card", "fonts", "Silver.ttf")
OUT_FILE = os.path.join("sd_card", "labels.bin")

# Silver renders smaller than GenYoMinTW, but the ratio varies by pt size
# (from ~1.32 to ~1.45).  Compute exact per-size scale factors dynamically.
GENYO_PATH = os.path.join("sd_card", "fonts", "GenYoMinTW-Regular.ttf")


def compute_silver_scale(font_size, genyo_font_cache, silver_font_cache):
    """Compute the exact scale factor for a given pt size by measuring both fonts."""
    test_text = "電子書"
    if font_size not in genyo_font_cache:
        genyo_font_cache[font_size] = ImageFont.truetype(GENYO_PATH, font_size)
    if font_size not in silver_font_cache:
        silver_font_cache[font_size] = ImageFont.truetype(SILVER_PATH, font_size)
    gi = render_label(test_text, font_size, genyo_font_cache[font_size])
    si = render_label(test_text, font_size, silver_font_cache[font_size])
    return gi.height / si.height if si.height > 0 else 1.0


def main():
    font_path = SILVER_PATH

    # Allow --font override
    for i, arg in enumerate(sys.argv[1:], 1):
        if arg == "--font" and i < len(sys.argv) - 1:
            font_path = sys.argv[i + 1]

    if not os.path.exists(font_path):
        print(f"ERROR: Font not found: {font_path}")
        print("Usage: python3 convert_labels_sd.py [--font /path/to/font.ttf]")
        sys.exit(1)

    if not os.path.exists(GENYO_PATH):
        print(f"ERROR: Reference font not found: {GENYO_PATH}")
        print("GenYoMinTW is needed to compute per-size scale factors.")
        sys.exit(1)

    print(f"Font: {font_path}")
    print(f"Reference: {GENYO_PATH}")
    print(f"Output: {OUT_FILE}")

    # Filter out Kai-font labels (they stay in PROGMEM regardless of system font)
    sd_labels = [entry for entry in LABELS if len(entry) <= 3]
    print(f"Labels: {len(sd_labels)} (skipping {len(LABELS) - len(sd_labels)} Kai-font labels)")

    # Compute per-size scale factors
    genyo_cache, silver_cache = {}, {}
    unique_sizes = sorted(set(e[1] for e in sd_labels))
    scale_map = {}
    print("\nPer-size scale factors:")
    for sz in unique_sizes:
        ratio = compute_silver_scale(sz, genyo_cache, silver_cache)
        scaled = round(sz * ratio)
        scale_map[sz] = (ratio, scaled)
        print(f"  {sz:3d}pt → render at {scaled:3d}pt  (×{ratio:.2f})")
    print()

    # Cache fonts by render size
    font_cache = {}
    entries = []  # (text, fontSize, w, h, bitmap_bytes)
    total_bytes = 0

    for entry in sd_labels:
        text, font_size, var_suffix = entry[0], entry[1], entry[2]

        # Render at per-size scaled size so Silver matches GenYoMinTW visually
        _, render_size = scale_map[font_size]

        if render_size not in font_cache:
            font_cache[render_size] = ImageFont.truetype(font_path, render_size)

        font = font_cache[render_size]
        img = render_label(text, render_size, font)
        w, h = img.size
        data = image_to_4bit(img)
        total_bytes += len(data)

        entries.append((text, font_size, w, h, data))
        print(f"  {text:30s}  size={font_size:2d}→{render_size:2d}  {w:3d}x{h:<3d}  {len(data):5d} bytes")

    # Write binary file
    os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
    with open(OUT_FILE, "wb") as f:
        # Header
        f.write(b"SLBL")
        f.write(struct.pack("<I", len(entries)))

        # Entries
        for text, fontSize, w, h, bitmap_data in entries:
            text_bytes = text.encode("utf-8")
            f.write(struct.pack("<H", len(text_bytes)))
            f.write(text_bytes)
            f.write(b"\x00")  # null terminator
            f.write(struct.pack("<HHH", fontSize, w, h))
            f.write(bitmap_data)

    file_size = os.path.getsize(OUT_FILE)
    print(f"\nOutput: {OUT_FILE}")
    print(f"Total: {len(entries)} labels, {total_bytes:,} bitmap bytes")
    print(f"File size: {file_size:,} bytes ({file_size/1024:.1f} KB)")
    print("Done!")


if __name__ == "__main__":
    main()
