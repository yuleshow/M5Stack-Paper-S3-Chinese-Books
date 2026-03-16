#!/usr/bin/env python3
"""Debug: render first 100 chars and annotate each cell."""
import os
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

font_path = 'sd_card/fonts/Huiwenmincho-improved.ttf'
font = ImageFont.truetype(font_path, 44)
small = ImageFont.truetype(font_path, 10)

VERT_PUNCT = {
    '\u3001': '\uFE11', '\u3002': '\uFE12', '\uFF0C': '\uFE10',
    '\u300C': '\uFE41', '\u300D': '\uFE42', '\u300E': '\uFE43',
    '\u300F': '\uFE44', '\u3010': '\uFE3B', '\u3011': '\uFE3C',
    '\uFF08': '\uFE35', '\uFF09': '\uFE36', '\uFF1A': '\uFE13',
    '\uFF1B': '\uFE14', '\u2014': '\uFE31', '\u2026': '\uFE19',
}

text = '天來看罷，再三致謝。智伯道：「梁兄可把他再三讀熟，牢記在心，到了堂上隨問隨答，不可有誤！」'
mapped = ''.join(VERT_PUNCT.get(c, c) for c in text)

FONT_SIZE = 44
CH = 52  # char height
CS = 52  # column spacing

img = Image.new('L', (540, 960), 255)
draw = ImageDraw.Draw(img)

col_x = 510
y = 60
ci = 0
for i, ch in enumerate(mapped):
    if ch.isspace():
        continue
    dx = col_x - FONT_SIZE // 2
    dy = y + 4
    # Cell outline
    draw.rectangle([dx, y, dx + FONT_SIZE, y + CH], outline=180)
    # Draw char
    draw.text((dx, dy), ch, font=font, fill=0)

    # Check actual bbox of rendered glyph
    bbox = font.getbbox(ch)
    print(f'  [{i:2d}] U+{ord(ch):04X}  bbox={bbox}  char={ch}')

    y += CH
    ci += 1
    if ci >= 15 or y + CH > 850:
        col_x -= CS
        y = 60
        ci = 0
    if col_x < 50:
        break

img.save('output/debug_render.png')
print('Saved: output/debug_render.png')
