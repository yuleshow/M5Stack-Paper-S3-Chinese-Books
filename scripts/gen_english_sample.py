#!/usr/bin/env python3
"""
Generate a sample English reading page that matches the device's
horizontal LTR rendering (epubIsHorizontal path in drawReading()).
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# Layout constants (globals.h)
SCREEN_W = 540
SCREEN_H = 960
READING_AREA_TOP    = 65
READING_AREA_BOTTOM = 878
READING_AREA_LEFT   = 20
READING_AREA_RIGHT  = 530
NAV_Y = 886
NAV_ICON_SIZE = 64
NAV_PREV_X = 10
NAV_NEXT_X = 84
NAV_RETURN_X = 466

SAMPLE_TEXT = """\
The world is a book, and those who do not travel read only one page. \
There is no friend as loyal as a book. A reader lives a thousand lives \
before he dies. The man who never reads lives only one.

Reading gives us someplace to go when we have to stay where we are. \
Once you learn to read, you will be forever free. The more that you read, \
the more things you will know. The more that you learn, the more places \
you'll go.

I have always imagined that Paradise will be a kind of library. \
A book is a dream that you hold in your hand. So many books, so little time. \
Books are a uniquely portable magic. A room without books is like a body \
without a soul.

In the case of good books, the point is not to see how many of them you \
can get through, but rather how many can get through to you. \
Think before you speak. Read before you think. \
There is more treasure in books than in all the pirate's loot on \
Treasure Island.

The reading of all good books is like a conversation with the finest \
minds of past centuries. Education is not the filling of a pail, but the \
lighting of a fire. A book must be the axe for the frozen sea within us. \
We read to know we're not alone.
"""


def render_english_page(text, font_path, font_size, output_path, page_num=0, total_pages=10):
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    draw = ImageDraw.Draw(img)

    # Load fonts
    font = ImageFont.truetype(font_path, font_size)
    ui_font = ImageFont.truetype('Helvetica.ttc', 16)
    ui_font_small = ImageFont.truetype('Helvetica.ttc', 12)

    # Status bar: time + battery
    draw.text((8, 4), "19:41", fill=0, font=ImageFont.truetype('Helvetica.ttc', 28))
    # Battery percentage
    draw.text((410, 4), "+100%", fill=0, font=ImageFont.truetype('Helvetica.ttc', 28), anchor="rt")
    # Battery icon
    bx, by, bw, bh = SCREEN_W - 46, 12, 34, 18
    draw.rectangle([bx, by, bx + bw, by + bh], outline=0, width=2)
    draw.rectangle([bx + bw, by + 4, bx + bw + 3, by + bh - 4], fill=0)
    draw.rectangle([bx + 3, by + 3, bx + bw - 3, by + bh - 3], fill=0)

    # Horizontal word-wrapped rendering (matches device drawReading horizontal path)
    line_height = font_size + font_size // 4  # 1.25x line spacing
    rd_left = READING_AREA_LEFT
    rd_right = READING_AREA_RIGHT
    rd_top = READING_AREA_TOP
    rd_bottom = READING_AREA_BOTTOM
    avail_w = rd_right - rd_left

    current_x = rd_left
    current_y = rd_top
    render_stop = len(text)
    i = 0

    while i < len(text):
        ch = text[i]

        if ch == '\r':
            i += 1
            continue

        # Newline → paragraph break
        if ch == '\n':
            i += 1
            if current_x > rd_left:
                current_y += line_height + line_height // 2  # 1.5x for paragraph gap
                current_x = rd_left
            if current_y + line_height > rd_bottom:
                render_stop = i
                break
            continue

        if ord(ch) < 0x20:
            i += 1
            continue

        # Collect a word or whitespace
        word_start = i
        if ch in (' ', '\t'):
            while i < len(text) and text[i] in (' ', '\t'):
                i += 1
            word = " "
        else:
            word = ""
            while i < len(text) and text[i] not in (' ', '\t', '\n', '\r') and ord(text[i]) >= 0x20:
                word += text[i]
                i += 1

        if not word:
            continue

        # Measure word width
        bbox = font.getbbox(word)
        word_w = bbox[2] - bbox[0]

        # Skip leading space on line
        if word == " " and current_x <= rd_left:
            continue

        # Word wrap
        if current_x + word_w > rd_right and current_x > rd_left:
            current_y += line_height
            current_x = rd_left
            if current_y + line_height > rd_bottom:
                render_stop = word_start
                i = word_start
                break
            if word == " ":
                continue

        # Draw word
        if word != " ":
            draw.text((current_x, current_y), word, fill=0, font=font)
        current_x += word_w

    # Progress bar at bottom
    bar_y = READING_AREA_BOTTOM + 2
    bar_w = SCREEN_W - 60
    bar_x = 30
    pct = (page_num + 1) / max(total_pages, 1)
    fill_w = int(bar_w * pct)
    draw.rectangle([bar_x, bar_y, bar_x + bar_w, bar_y + 3], fill=200)
    if fill_w > 0:
        draw.rectangle([bar_x, bar_y, bar_x + fill_w, bar_y + 3], fill=0)

    # Page number + percentage
    page_str = f"{page_num + 1}/{total_pages}"
    pct_str = f"{pct * 100:.1f}%"
    draw.text((SCREEN_W // 2, bar_y + 8), page_str, fill=0, font=ui_font, anchor="mt")
    draw.text((SCREEN_W - 30, bar_y + 8), pct_str, fill=0, font=ui_font_small, anchor="rt")

    # Nav bar: ← → and ↩ icons
    btn = NAV_ICON_SIZE
    m = 18
    # ← prev
    nx, ny = NAV_PREV_X, NAV_Y
    draw.line([(nx + btn - m, ny + m), (nx + m, ny + btn // 2)], fill=0, width=3)
    draw.line([(nx + m, ny + btn // 2), (nx + btn - m, ny + btn - m)], fill=0, width=3)
    # → next
    nx = NAV_NEXT_X
    draw.line([(nx + m, ny + m), (nx + btn - m, ny + btn // 2)], fill=0, width=3)
    draw.line([(nx + btn - m, ny + btn // 2), (nx + m, ny + btn - m)], fill=0, width=3)
    # ↩ return
    nx = NAV_RETURN_X
    draw.line([(nx + btn - m, ny + m), (nx + m, ny + btn // 2)], fill=0, width=3)
    draw.line([(nx + m, ny + btn // 2), (nx + btn - m, ny + btn - m)], fill=0, width=3)

    img.save(output_path)
    print(f"Saved: {output_path}")
    print(f"  Font: {font_path} @ {font_size}px")
    print(f"  Rendered {render_stop} / {len(text)} bytes")


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Generate English reading page sample')
    parser.add_argument('--font', '-f', default='sd_card/fonts/EBGaramond-Regular.ttf',
                        help='TTF font path (default: EBGaramond-Regular.ttf)')
    parser.add_argument('--size', '-s', type=int, default=30,
                        help='Font size in pt (default: 30)')
    parser.add_argument('--output', '-o', default='output/sample_english.png',
                        help='Output PNG path')
    parser.add_argument('--text', '-t', default=None,
                        help='Custom text to render')
    args = parser.parse_args()

    if not os.path.exists(args.font):
        print(f"Font not found: {args.font}")
        sys.exit(1)

    os.makedirs('output', exist_ok=True)
    text = args.text if args.text else SAMPLE_TEXT
    render_english_page(text, args.font, args.size, args.output)
