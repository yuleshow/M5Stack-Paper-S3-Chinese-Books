#!/usr/bin/env python3
"""
Convert fortune slip JPEG images into a binary pack file (.bin) for M5Stack Paper S3.

Binary format:
  Header (12 bytes):
    [0..3]   Magic: "FSLP"
    [4..5]   Count (uint16 LE) - number of slips
    [6..7]   Image width (uint16 LE)
    [8..9]   Image height (uint16 LE)
    [10..11] Flags (uint16 LE) - bit 0: has cover image, bit 1: has wording data
  Index (count * 8 bytes):
    count * uint32 LE offsets - absolute file offset to each JPEG blob
    count * uint32 LE sizes  - byte size of each JPEG blob
  Cover index (8 bytes, only if flags bit 0 set):
    uint32 LE cover_offset
    uint32 LE cover_size
  Wording index (8 bytes, only if flags bit 1 set):
    uint32 LE wording_offset - absolute file offset to wording block
    uint16 LE fields_per_slip
    uint16 LE reserved
  Data:
    Cover JPEG (if present) + concatenated slip JPEG blobs
  Wording block (if flags bit 1 set):
    count * uint32 LE offsets - absolute file offset to each slip's wording data
    Concatenated null-terminated UTF-8 strings (fields_per_slip per slip)

Usage:
  python3 convert_fortune_slips.py

Place the .bin files on SD card at /fortune_slips/kuanyin.bin and /fortune_slips/sensoji.bin

Fortune slip images sourced from www.chance.org.tw
"""

import csv
import io
import os
import struct
import sys
from pathlib import Path

# PIL for reading image dimensions and resizing
try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("PIL not found. Install with: pip3 install Pillow")
    sys.exit(1)

# Target display size for M5Stack Paper S3
TARGET_W = 540
TARGET_H = 960

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# Wording CSV fields to extract (by CSV column index).
# Each category can define its own field indices via "wording_field_indices".
# Default (kuanyin): 23 fields from 25-column CSV
KUANYIN_FIELD_INDICES = [1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]
# Sensoji: 11 fields from 12-column CSV (#,籤號,等級,詩曰,詩意,願望,疾病,遺失物,盼望的人,蓋新居搬家,結婚交往,旅行)
SENSOJI_FIELD_INDICES = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]

CATEGORIES = [
    {
        "name": "kuanyin",
        "input_dir": "assets/Fortune_Slips/kuanyin",
        "output": "sd_card/fortune_slips/kuanyin.bin",
        "cover": "assets/Fortune_Slips/kuanyin/cover.jpeg",
        "wording_csv": "assets/Fortune_Slips/kuanyin/kuanyin.csv",
        "wording_field_indices": KUANYIN_FIELD_INDICES,
        "pattern": "kuanyin-{:03d}.jpg",
        "count": 100,
        "start": 1,
    },
    {
        "name": "sensoji",
        "input_dir": "assets/Fortune_Slips/senso-ji",
        "output": "sd_card/fortune_slips/sensoji.bin",
        "cover": "assets/Fortune_Slips/senso-ji/cover.jpeg",
        "wording_csv": "assets/Fortune_Slips/senso-ji/sensoji.csv",
        "wording_field_indices": SENSOJI_FIELD_INDICES,
        "wording_as_image": True,
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


# ==================== Wording Image Pre-Rendering ====================

# Font search paths (project fonts preferred, then system fallbacks)
FONT_SEARCH = [
    'sd_card/fonts/MingLiU.ttf',
    'sd_card/fonts/mingliu.ttc',
    'sd_card/fonts/GenYoMinTW-Regular.ttf',
]
KAI_FONT_SEARCH = [
    'sd_card/fonts/TW-Kai-98_1.ttf',
]

LIGHT_GRAY = 200
DARK_GRAY = 100


def find_font(paths):
    """Find first existing font file from a list of paths."""
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def wrap_text_cjk(text, font, max_width):
    """Wrap CJK text to fit within max_width pixels."""
    lines = []
    current = ""
    for ch in text:
        test = current + ch
        w = font.getlength(test)
        if w > max_width and current:
            lines.append(current)
            current = ch
        else:
            current = test
    if current:
        lines.append(current)
    return lines


def render_wording_image(fields, category_name, font_path, kai_font_path=None):
    """Pre-render a wording page as 540x960 grayscale JPEG.

    For sensoji, fields = [籤號, 等級, 詩曰, 詩意, 願望, 疾病, 遺失物,
                           盼望的人, 蓋新居搬家, 結婚交往, 旅行]
    For kuanyin, fields = [籤號, 等級, 宮位, 詩曰一, 詩意, 解曰, 故事, 故事內容,
                           家宅, 自身, 求財, 交易, 婚姻, 六甲, 行人, 田蠶,
                           六畜, 尋人, 訟詞, 移徙, 失物, 疾病, 山墳]
    """
    W, H = TARGET_W, TARGET_H
    img = Image.new('L', (W, H), 255)
    draw = ImageDraw.Draw(img)

    pfont = kai_font_path or font_path
    title_font = ImageFont.truetype(font_path, 32)
    poem_font = ImageFont.truetype(pfont, 48)
    section_font = ImageFont.truetype(font_path, 26)
    cat_font = ImageFont.truetype(font_path, 24)
    section_label_font = ImageFont.truetype(font_path, 26)

    if category_name == 'sensoji':
        _render_sensoji(draw, fields, title_font, poem_font, section_font,
                        cat_font, section_label_font, W)
    else:
        _render_kuanyin(draw, fields, title_font, poem_font, section_font,
                        cat_font, section_label_font, W)

    buf = io.BytesIO()
    img.save(buf, format='JPEG', quality=90)
    return buf.getvalue()


def _draw_centered(draw, text, cx, y, font, fill=0):
    """Draw text horizontally centered at (cx, y)."""
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    draw.text((cx - tw / 2, y), text, fill=fill, font=font)


def _draw_vertical_poem(draw, poem_text, poem_font, start_y, W,
                        char_spacing=52, col_spacing=56):
    """Draw poem in vertical columns right-to-left. Returns y after poem."""
    poem_lines = [l for l in poem_text.split('\n') if l.strip()]
    if not poem_lines:
        return start_y

    max_chars = max(len(l) for l in poem_lines)
    if max_chars < 1:
        max_chars = 5
    poem_h = max_chars * char_spacing
    poem_w = len(poem_lines) * col_spacing
    poem_left = (W - poem_w) // 2

    # Decorative frame
    fx, fy = poem_left - 18, start_y - 12
    fw, fh = poem_w + 36, poem_h + 24
    draw.rounded_rectangle([fx, fy, fx + fw, fy + fh], radius=8,
                           outline=LIGHT_GRAY)

    for col_idx, line in enumerate(poem_lines):
        cx = poem_left + poem_w - col_spacing // 2 - col_idx * col_spacing
        for row_idx, char in enumerate(line):
            cy = start_y + row_idx * char_spacing + char_spacing // 2
            draw.text((cx, cy), char, fill=0, font=poem_font, anchor="mm")

    return start_y + poem_h


def _render_sensoji(draw, fields, title_font, poem_font, section_font,
                    cat_font, label_font, W):
    """Render sensoji wording layout."""
    # Fields: 0=籤號, 1=等級, 2=詩曰, 3=詩意,
    #         4=願望, 5=疾病, 6=遺失物, 7=盼望的人, 8=蓋新居搬家, 9=結婚交往, 10=旅行

    # Title
    title = f"{fields[0]}  {fields[1]}"
    _draw_centered(draw, title, W // 2, 45, title_font)
    draw.line([(40, 82), (500, 82)], fill=LIGHT_GRAY)

    # Poem (tighter spacing to fit long explanations)
    poem_end_y = _draw_vertical_poem(draw, fields[2], poem_font, 88, W,
                                     char_spacing=48, col_spacing=54)

    # 詩意
    sy = poem_end_y + 18
    draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
    sy += 6
    draw.text((30, sy), "【詩意】", fill=DARK_GRAY, font=label_font)
    sy += 30
    line_h = 30
    lines = wrap_text_cjk(fields[3], section_font, 480)
    for line in lines:
        if sy > 920:
            break
        draw.text((30, sy), line, fill=0, font=section_font)
        sy += line_h
    sy += 4

    # 聖意 categories
    cat_labels = ["願望", "疾病", "遺失物", "盼望的人", "蓋新居搬家",
                  "結婚交往", "旅行"]
    cat_values = fields[4:11]
    has_cats = any(v for v in cat_values)
    if has_cats and sy < 920:
        draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
        sy += 6
        cat_line_h = 28
        for label, value in zip(cat_labels, cat_values):
            if not value:
                continue
            if sy > 930:
                break
            label_str = f"{label}："
            label_w = cat_font.getlength(label_str)
            full_text = label_str + value
            cat_lines = wrap_text_cjk(full_text, cat_font, 480)
            for li, cl in enumerate(cat_lines):
                if sy > 930:
                    break
                if li == 0:
                    # Semi-bold label via double-draw with 1px offset
                    draw.text((30, sy), label_str, fill=0, font=cat_font)
                    draw.text((31, sy), label_str, fill=0, font=cat_font)
                    remainder = cl[len(label_str):]
                    if remainder:
                        draw.text((30 + label_w, sy), remainder, fill=0,
                                  font=cat_font)
                else:
                    draw.text((30, sy), cl, fill=0, font=cat_font)
                sy += cat_line_h


def _render_kuanyin(draw, fields, title_font, poem_font, section_font,
                    cat_font, label_font, W):
    """Render kuanyin wording layout."""
    # Fields: 0=籤號, 1=等級, 2=宮位, 3=詩曰一, 4=詩意, 5=解曰,
    #         6=故事, 7=故事內容, 8-22=聖意

    # Title
    title = f"{fields[0]}  {fields[1]}  {fields[2]}"
    _draw_centered(draw, title, W // 2, 45, title_font)
    draw.line([(40, 82), (500, 82)], fill=LIGHT_GRAY)

    # Poem
    poem_end_y = _draw_vertical_poem(draw, fields[3], poem_font, 88, W)

    # 詩意
    sy = poem_end_y + 20
    draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
    sy += 8
    draw.text((30, sy), "【詩意】", fill=DARK_GRAY, font=label_font)
    sy += 32
    lines = wrap_text_cjk(fields[4], section_font, 480)
    for line in lines:
        if sy > 855:
            break
        draw.text((30, sy), line, fill=0, font=section_font)
        sy += 32
    sy += 4

    # 解曰
    draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
    sy += 8
    draw.text((30, sy), "【解曰】", fill=DARK_GRAY, font=label_font)
    sy += 32
    lines = wrap_text_cjk(fields[5], section_font, 480)
    for line in lines:
        if sy > 855:
            break
        draw.text((30, sy), line, fill=0, font=section_font)
        sy += 32
    sy += 4

    # 聖意 grid
    sacred_labels = ["家宅", "自身", "求財", "交易", "婚姻",
                     "六甲", "行人", "田蠶", "六畜", "尋人",
                     "訟詞", "移徙", "失物", "疾病", "山墳"]
    if len(fields) > 8 and sy < 850:
        draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
        sy += 8
        grid_rows = 5
        cell_w = 170
        cell_h = 32
        for i, label in enumerate(sacred_labels):
            idx = 8 + i
            if idx >= len(fields) or not fields[idx]:
                continue
            gc = i // grid_rows
            gr = i % grid_rows
            x = 30 + gc * cell_w
            y = sy + gr * cell_h
            if y > 855:
                break
            draw.text((x, y), f"{label}：{fields[idx]}", fill=0, font=cat_font)


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

    # Process wording CSV if present
    wording_csv_path = category.get("wording_csv")
    field_indices = category.get("wording_field_indices", KUANYIN_FIELD_INDICES)
    num_fields = len(field_indices)
    wording_as_image = category.get("wording_as_image", False)
    wording_blobs = None
    has_wording = False
    if wording_csv_path and os.path.exists(wording_csv_path):
        wording_blobs = []
        with open(wording_csv_path, 'r', encoding='utf-8-sig') as csvf:
            reader = csv.reader(csvf)
            csv_header = next(reader)
            rows = []
            for row in reader:
                if not row or not row[0].strip():
                    continue
                rows.append(row)

        if wording_as_image:
            # Pre-render wording pages as JPEG images
            font_path = find_font(FONT_SEARCH)
            kai_font_path = find_font(KAI_FONT_SEARCH)
            if not font_path:
                print("ERROR: No Chinese font found for wording image rendering")
                print(f"  Searched: {FONT_SEARCH}")
            else:
                print(f"Wording font: {font_path}")
                if kai_font_path:
                    print(f"Poem font: {kai_font_path}")
                for row in rows:
                    fields = []
                    for idx in field_indices:
                        fields.append(row[idx].strip() if idx < len(row) else "")
                    blob = render_wording_image(fields, name, font_path, kai_font_path)
                    wording_blobs.append(blob)
        else:
            # Pack as null-terminated UTF-8 text fields
            for row in rows:
                blob = b""
                for idx in field_indices:
                    val = row[idx].strip() if idx < len(row) else ""
                    blob += val.encode('utf-8') + b'\x00'
                wording_blobs.append(blob)

        if len(wording_blobs) == actual_count:
            has_wording = True
            if wording_as_image:
                avg_size = sum(len(b) for b in wording_blobs) // len(wording_blobs)
                print(f"Wording images: {len(wording_blobs)} slips, avg {avg_size:,} bytes/image")
            else:
                print(f"Wording CSV: {wording_csv_path} ({len(wording_blobs)} slips, {num_fields} fields)")
        else:
            print(f"WARNING: Wording CSV has {len(wording_blobs)} rows but expected {actual_count}, skipping")
            wording_blobs = None

    # Calculate layout
    header_size = 12
    index_size = actual_count * 4 * 2  # offsets + sizes
    cover_index_size = 8 if has_cover else 0  # cover offset + size
    wording_index_size = 8 if has_wording else 0  # wording offset + fields_per_slip + reserved
    data_start = header_size + index_size + cover_index_size + wording_index_size

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

    # Wording block comes after all JPEG data
    wording_block_offset = current_offset
    wording_sub_offsets = []
    wording_sizes = []
    if has_wording:
        if wording_as_image:
            # Image mode: [count * 4 offsets] + [count * 4 sizes] + [JPEG data]
            wording_index_block_size = actual_count * 4 * 2
        else:
            # Text mode: [count * 4 offsets] + [text data]
            wording_index_block_size = actual_count * 4
        wording_data_start = wording_block_offset + wording_index_block_size
        cur = wording_data_start
        for blob in wording_blobs:
            wording_sub_offsets.append(cur)
            wording_sizes.append(len(blob))
            cur += len(blob)
        current_offset = cur

    total_size = current_offset
    flags = 0
    if has_cover: flags |= 1
    if has_wording: flags |= 2

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

        # Wording index (if present)
        if has_wording:
            f.write(struct.pack('<I', wording_block_offset))
            f.write(struct.pack('<H', 0 if wording_as_image else num_fields))
            f.write(struct.pack('<H', 0))  # reserved

        # Data: cover JPEG (if present) + slip JPEG blobs
        if has_cover:
            f.write(cover_blob)
        for blob in jpeg_blobs:
            f.write(blob)

        # Wording block (if present)
        if has_wording:
            for sub_off in wording_sub_offsets:
                f.write(struct.pack('<I', sub_off))
            if wording_as_image:
                for sz in wording_sizes:
                    f.write(struct.pack('<I', sz))
            for blob in wording_blobs:
                f.write(blob)

    print(f"Output: {output_path}")
    print(f"  Slips: {actual_count}")
    if has_cover:
        print(f"  Cover: {cover_size:,} bytes")
    if has_wording:
        wording_total = sum(len(b) for b in wording_blobs) + actual_count * (8 if wording_as_image else 4)
        if wording_as_image:
            print(f"  Wording: {wording_total:,} bytes (pre-rendered images)")
        else:
            print(f"  Wording: {wording_total:,} bytes ({num_fields} fields/slip)")
    print(f"  Total size: {total_size:,} bytes ({total_size / 1024 / 1024:.1f} MB)")
    print(f"  Header: {header_size} bytes")
    print(f"  Index: {index_size + cover_index_size + wording_index_size} bytes")
    data_size = total_size - data_start
    print(f"  Data: {data_size:,} bytes")
    jpeg_data_size = sum(sizes) + cover_size
    print(f"  Avg slip JPEG: {(jpeg_data_size - cover_size) // actual_count:,} bytes")

    return True


def combine_bins(categories, output_path):
    """Combine multiple FSLP .bin files into a single FSPK pack file.

    Combined format:
      Header (8 bytes):
        [0..3]   Magic: "FSPK"
        [4..5]   Number of categories (uint16 LE)
        [6..7]   Reserved (uint16 LE)
      Category TOC (N * 8 bytes):
        uint32 LE offset  - absolute file offset to FSLP block
        uint32 LE size    - byte size of FSLP block
      Data:
        Concatenated FSLP blocks (unchanged)
    """
    blobs = []
    for cat in categories:
        bin_path = cat["output"]
        if os.path.exists(bin_path):
            with open(bin_path, 'rb') as f:
                blobs.append(f.read())
        else:
            print(f"WARNING: {bin_path} not found, skipping")
            return False

    num_cats = len(blobs)
    header_size = 8 + num_cats * 8

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(b'FSPK')
        f.write(struct.pack('<H', num_cats))
        f.write(struct.pack('<H', 0))  # reserved
        pos = header_size
        for blob in blobs:
            f.write(struct.pack('<I', pos))
            f.write(struct.pack('<I', len(blob)))
            pos += len(blob)
        for blob in blobs:
            f.write(blob)

    total = os.path.getsize(output_path)
    print(f"\nCombined pack: {output_path}")
    print(f"  Categories: {num_cats}")
    for i, cat in enumerate(categories):
        print(f"    [{i}] {cat['name']}: {len(blobs[i]):,} bytes")
    print(f"  Total size: {total:,} bytes ({total / 1024 / 1024:.1f} MB)")
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

    # Combine into single pack file
    combined_output = "sd_card/fortune_slips/fortune_slips.bin"
    combine_bins(CATEGORIES, combined_output)

    print(f"\nCopy to SD card:")
    if os.path.exists(combined_output):
        size = os.path.getsize(combined_output)
        print(f"  {combined_output} -> SD:/fortune_slips/fortune_slips.bin ({size / 1024 / 1024:.1f} MB)")


if __name__ == "__main__":
    main()
