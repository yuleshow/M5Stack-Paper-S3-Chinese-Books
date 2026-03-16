#!/usr/bin/env python3
"""Minimal test: render a few chars + punctuation vertically with Huiwenmincho."""
import os
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

font = ImageFont.truetype('sd_card/fonts/Huiwenmincho-improved.ttf', 44)

# Vertical punct mapping (same as gen_sample_reading_page.py)
VERT_PUNCT = {
    '\u3001': '\uFE11', '\u3002': '\uFE12', '\uFF0C': '\uFE10',
    '\u300C': '\uFE41', '\u300D': '\uFE42', '\u300E': '\uFE43',
    '\u300F': '\uFE44', '\u3010': '\uFE3B', '\u3011': '\uFE3C',
    '\uFF08': '\uFE35', '\uFF09': '\uFE36', '\uFF1A': '\uFE13',
    '\uFF1B': '\uFE14', '\u2014': '\uFE31', '\u2026': '\uFE19',
}

# Test string with known punctuation
test = '天來看罷，再三致謝。智伯道：「梁兄！」'

FONT_SIZE = 44
CHAR_HEIGHT = FONT_SIZE + FONT_SIZE // 5  # 52
COL_SPACING = CHAR_HEIGHT

img = Image.new('L', (540, 960), 255)
draw = ImageDraw.Draw(img)

# Column 1: original (no mapping)
col_x = 500
y = 60
draw.text((col_x - 30, 30), 'Original', font=ImageFont.truetype('sd_card/fonts/Huiwenmincho-improved.ttf', 16), fill=0)
for ch in test:
    if ch == '\n' or ch.isspace():
        continue
    draw.text((col_x - FONT_SIZE // 2, y), ch, font=font, fill=0)
    y += CHAR_HEIGHT

# Column 2: with vertical mapping
col_x2 = 400
y = 60
draw.text((col_x2 - 30, 30), 'Mapped', font=ImageFont.truetype('sd_card/fonts/Huiwenmincho-improved.ttf', 16), fill=0)
mapped = ''.join(VERT_PUNCT.get(c, c) for c in test)
for ch in mapped:
    if ch == '\n' or ch.isspace():
        continue
    draw.text((col_x2 - FONT_SIZE // 2, y), ch, font=font, fill=0)
    y += CHAR_HEIGHT

# Column 3: character-by-character with bboxes drawn
col_x3 = 280
y = 60
draw.text((col_x3 - 30, 30), 'BBox', font=ImageFont.truetype('sd_card/fonts/Huiwenmincho-improved.ttf', 16), fill=0)
for ch in mapped:
    if ch == '\n' or ch.isspace():
        continue
    dx = col_x3 - FONT_SIZE // 2
    # Draw cell outline
    draw.rectangle([dx, y, dx + FONT_SIZE, y + CHAR_HEIGHT], outline=180)
    # Draw char
    draw.text((dx, y + 4), ch, font=font, fill=0)
    # Check if glyph rendered anything
    test_img = Image.new('L', (FONT_SIZE, FONT_SIZE), 255)
    td = ImageDraw.Draw(test_img)
    td.text((0, 0), ch, font=font, fill=0)
    bbox = test_img.getbbox()
    if bbox is None:
        draw.text((dx + FONT_SIZE + 2, y), f'U+{ord(ch):04X} EMPTY', font=ImageFont.truetype('sd_card/fonts/Huiwenmincho-improved.ttf', 12), fill=0)
    else:
        draw.text((dx + FONT_SIZE + 2, y), f'U+{ord(ch):04X} {bbox[2]}x{bbox[3]}', font=ImageFont.truetype('sd_card/fonts/Huiwenmincho-improved.ttf', 12), fill=0)
    y += CHAR_HEIGHT

img.save('output/punct_test.png')
print('Saved: output/punct_test.png')
