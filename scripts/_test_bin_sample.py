#!/usr/bin/env python3
"""Quick test: render sample text from a BIN font to verify glyph rendering."""
import struct, os, sys
from PIL import Image

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fonts_dir = os.path.join(PROJ, 'sd_card', 'fonts')
output_dir = os.path.join(PROJ, 'output')

bin_path = os.path.join(fonts_dir, 'GenSenRounded-R_44pt.bin')
with open(bin_path, 'rb') as f:
    data = f.read()

# Parse header (gen_sample_reading_page.py BinFont format)
HEADER_SIZE = 137
ENTRY_SIZE = 20
char_count = struct.unpack_from('<I', data, 0)[0]
font_size = data[4]
version = struct.unpack_from('<I', data, 5)[0]
family_name = data[9:73].split(b'\x00')[0].decode('utf-8', errors='replace')
style_name = data[73:137].split(b'\x00')[0].decode('utf-8', errors='replace')
print(f'Font: {family_name} {style_name}, size={font_size}, version={version}, chars={char_count}')
print(f'File size: {len(data)} bytes')

# Build index
entries = {}
idx_start = HEADER_SIZE
for i in range(char_count):
    off = idx_start + i * ENTRY_SIZE
    if off + ENTRY_SIZE > len(data):
        break
    uni = struct.unpack_from('<I', data, off)[0]
    w = struct.unpack_from('<H', data, off + 4)[0]
    h = struct.unpack_from('<H', data, off + 6)[0]
    bmp_off = struct.unpack_from('<I', data, off + 8)[0]
    bmp_sz = struct.unpack_from('<I', data, off + 12)[0]
    bx = struct.unpack_from('<h', data, off + 16)[0]
    by = struct.unpack_from('<h', data, off + 18)[0]
    entries[uni] = (w, h, bmp_off, bmp_sz, bx, by)

# Sample text from 三体
sample = '给岁月以文明，而不是给文明以岁月。弱小和无知不是生存的障碍，傲慢才是。'
print(f'Total glyphs in font: {len(entries)}')

# Check which chars are available
missing = []
for ch in sample:
    cp = ord(ch)
    if cp in entries:
        w, h, bmp_off, bmp_sz, bx, by = entries[cp]
        print(f'  {ch} U+{cp:04X}: {w}x{h} bx={bx} by={by} off={bmp_off} sz={bmp_sz}')
    else:
        print(f'  {ch} U+{cp:04X}: MISSING')
        missing.append(ch)

if missing:
    print(f'\nMISSING chars: {"".join(missing)}')

# Render sample image — 2 rows
chars_per_row = 16
rows = 2
img_w = 10 + chars_per_row * font_size + 10
img_h = 10 + rows * (font_size + 10) + 10
img = Image.new('L', (img_w, img_h), 255)

ci = 0
for row in range(rows):
    x = 10
    y_base = 10 + row * (font_size + 10)
    for col in range(chars_per_row):
        if ci >= len(sample):
            break
        ch = sample[ci]
        ci += 1
        cp = ord(ch)
        entry = entries.get(cp)
        if entry is None:
            x += font_size
            continue
        w, h, bmp_off, bmp_sz, bx, by = entry
        if w == 0 or h == 0:
            x += font_size // 2
            continue
        # Decode 1-bit bitmap
        for py in range(h):
            for px in range(w):
                bit_idx = py * w + px
                byte_pos = bmp_off + bit_idx // 8
                bit_pos = 7 - (bit_idx % 8)
                if byte_pos < len(data) and (data[byte_pos] >> bit_pos) & 1:
                    dx = x + bx + px
                    dy = y_base + (font_size - by) + py
                    if 0 <= dx < img_w and 0 <= dy < img_h:
                        img.putpixel((dx, dy), 0)
        x += font_size

out_path = os.path.join(output_dir, 'gen_sen_rounded_44_sample.png')
img.save(out_path)
print(f'\nSaved to {out_path}')

# Also render with TTF for comparison
try:
    from PIL import ImageDraw, ImageFont
    ttf_path = os.path.join(fonts_dir, 'GenSenRounded-R.ttc')
    if os.path.exists(ttf_path):
        ttf_font = ImageFont.truetype(ttf_path, font_size)
        img2 = Image.new('L', (img_w, img_h), 255)
        draw2 = ImageDraw.Draw(img2)
        ci = 0
        for row in range(rows):
            x = 10
            y_base = 10 + row * (font_size + 10)
            for col in range(chars_per_row):
                if ci >= len(sample):
                    break
                ch = sample[ci]
                ci += 1
                draw2.text((x, y_base), ch, font=ttf_font, fill=0)
                x += font_size
        out2 = os.path.join(output_dir, 'gen_sen_rounded_44_ttf_sample.png')
        img2.save(out2)
        print(f'TTF comparison saved to {out2}')
except Exception as e:
    print(f'TTF comparison skipped: {e}')
