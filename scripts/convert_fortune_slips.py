#!/usr/bin/env python3
"""
Convert fortune slip JPEG images into a binary pack file (.bin) for M5Stack Paper S3.

Binary format:
  Header (12 bytes):
    [0..3]   Magic: "FSLP"
    [4..5]   Count (uint16 LE) - number of slips
    [6..7]   Image width (uint16 LE)
    [8..9]   Image height (uint16 LE)
    [10..11] Flags (uint16 LE) - bit 0: has cover image
  Index (count * 8 bytes):
    count * uint32 LE offsets - absolute file offset to each JPEG blob
    count * uint32 LE sizes  - byte size of each JPEG blob
  Cover index (8 bytes, only if flags bit 0 set):
    uint32 LE cover_offset
    uint32 LE cover_size
  Data:
    Cover JPEG (if present) + concatenated slip JPEG blobs

Usage:
  python3 convert_fortune_slips.py

Place the .bin files on SD card at /fortune_slips/kuanyin.bin and /fortune_slips/sensoji.bin
"""

import io
import os
import struct
import sys
from pathlib import Path

# PIL for reading image dimensions and resizing
try:
    from PIL import Image
except ImportError:
    print("PIL not found. Install with: pip3 install Pillow")
    sys.exit(1)

# Target display size for M5Stack Paper S3
TARGET_W = 540
TARGET_H = 960

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

CATEGORIES = [
    {
        "name": "kuanyin",
        "input_dir": "assets/Fortune_Slips/kuanyin",
        "output": "sd_card/fortune_slips/kuanyin.bin",
        "cover": "assets/Fortune_Slips/kuanyin/cover.jpeg",
        "pattern": "kuanyin-{:03d}.jpg",
        "count": 100,
        "start": 1,
    },
    {
        "name": "sensoji",
        "input_dir": "assets/Fortune_Slips/senso-ji",
        "output": "sd_card/fortune_slips/sensoji.bin",
        "cover": "assets/Fortune_Slips/senso-ji/cover.jpeg",
        "pattern": None,  # Use sorted directory listing
        "count": 100,
        "start": 0,
    },
]


def get_sorted_files(directory, exclude=None):
    """Get sorted list of JPEG files in a directory, excluding specific files."""
    exclude_base = os.path.basename(exclude) if exclude else None
    files = []
    for f in sorted(os.listdir(directory)):
        if f.lower().endswith(('.jpg', '.jpeg')) and not f.startswith('.'):
            if exclude_base and f == exclude_base:
                continue
            files.append(os.path.join(directory, f))
    return files


def build_bin(category):
    """Build a .bin pack file for one fortune slip category."""
    name = category["name"]
    input_dir = category["input_dir"]
    output_path = category["output"]
    count = category["count"]

    print(f"\n{'='*60}")
    print(f"Building {name}: {input_dir} -> {output_path}")
    print(f"{'='*60}")

    if not os.path.isdir(input_dir):
        print(f"ERROR: Input directory not found: {input_dir}")
        return False

    # Collect input files
    if category["pattern"]:
        # Use explicit pattern
        files = []
        for i in range(category["start"], category["start"] + count):
            filename = category["pattern"].format(i)
            filepath = os.path.join(input_dir, filename)
            if os.path.exists(filepath):
                files.append(filepath)
            else:
                print(f"WARNING: Missing file: {filepath}")
    else:
        # Use sorted directory listing (exclude cover file)
        files = get_sorted_files(input_dir, exclude=category.get("cover"))

    if not files:
        print(f"ERROR: No JPEG files found in {input_dir}")
        return False

    actual_count = len(files)
    print(f"Found {actual_count} JPEG files")

    # Get reference dimensions from first image
    ref_img = Image.open(files[0])
    ref_w, ref_h = ref_img.size
    ref_img.close()
    print(f"Original dimensions: {ref_w} x {ref_h}")
    print(f"Target dimensions: {TARGET_W} x {TARGET_H}")

    # Read, resize, and re-encode all JPEG images
    jpeg_blobs = []
    for filepath in files:
        img = Image.open(filepath)
        # Convert to RGB if necessary (handles RGBA, palette, etc.)
        if img.mode != 'RGB' and img.mode != 'L':
            img = img.convert('RGB')
        # Stretch to fill the entire screen
        canvas = img.resize((TARGET_W, TARGET_H), Image.LANCZOS)
        # Re-encode as JPEG
        buf = io.BytesIO()
        canvas.save(buf, format='JPEG', quality=85)
        jpeg_blobs.append(buf.getvalue())
        img.close()

    # Process cover image if present
    cover_path = category.get("cover")
    cover_blob = None
    has_cover = False
    if cover_path and os.path.exists(cover_path):
        img = Image.open(cover_path)
        if img.mode != 'RGB' and img.mode != 'L':
            img = img.convert('RGB')
        canvas = img.resize((TARGET_W, TARGET_H), Image.LANCZOS)
        buf = io.BytesIO()
        canvas.save(buf, format='JPEG', quality=90)
        cover_blob = buf.getvalue()
        has_cover = True
        img.close()
        print(f"Cover image: {cover_path} ({len(cover_blob):,} bytes)")

    # Calculate layout
    header_size = 12
    index_size = actual_count * 4 * 2  # offsets + sizes
    cover_index_size = 8 if has_cover else 0  # cover offset + size
    data_start = header_size + index_size + cover_index_size

    # Cover goes first in data section
    current_offset = data_start
    cover_offset = 0
    cover_size = 0
    if has_cover:
        cover_offset = current_offset
        cover_size = len(cover_blob)
        current_offset += cover_size

    # Build offsets and sizes for slips
    offsets = []
    sizes = []
    for blob in jpeg_blobs:
        offsets.append(current_offset)
        sizes.append(len(blob))
        current_offset += len(blob)

    total_size = current_offset
    flags = 1 if has_cover else 0

    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    # Write binary file
    with open(output_path, 'wb') as f:
        # Header
        f.write(b'FSLP')
        f.write(struct.pack('<H', actual_count))
        f.write(struct.pack('<H', TARGET_W))
        f.write(struct.pack('<H', TARGET_H))
        f.write(struct.pack('<H', flags))

        # Index: offsets
        for off in offsets:
            f.write(struct.pack('<I', off))

        # Index: sizes
        for sz in sizes:
            f.write(struct.pack('<I', sz))

        # Cover index (if present)
        if has_cover:
            f.write(struct.pack('<I', cover_offset))
            f.write(struct.pack('<I', cover_size))

        # Data: cover JPEG (if present) + slip JPEG blobs
        if has_cover:
            f.write(cover_blob)
        for blob in jpeg_blobs:
            f.write(blob)

    print(f"Output: {output_path}")
    print(f"  Slips: {actual_count}")
    if has_cover:
        print(f"  Cover: {cover_size:,} bytes")
    print(f"  Total size: {total_size:,} bytes ({total_size / 1024 / 1024:.1f} MB)")
    print(f"  Header: {header_size} bytes")
    print(f"  Index: {index_size + cover_index_size} bytes")
    data_size = total_size - data_start
    print(f"  Data: {data_size:,} bytes")
    print(f"  Avg slip: {(data_size - cover_size) // actual_count:,} bytes")

    return True


def main():
    print("Fortune Slips Binary Converter")
    print("==============================")

    success = 0
    for cat in CATEGORIES:
        if build_bin(cat):
            success += 1

    print(f"\n{'='*60}")
    print(f"Done: {success}/{len(CATEGORIES)} categories converted")
    print(f"\nCopy to SD card:")
    for cat in CATEGORIES:
        out = cat["output"]
        sd_path = f"/fortune_slips/{os.path.basename(out)}"
        if os.path.exists(out):
            size = os.path.getsize(out)
            print(f"  {out} -> SD:{sd_path} ({size / 1024 / 1024:.1f} MB)")


if __name__ == "__main__":
    main()
