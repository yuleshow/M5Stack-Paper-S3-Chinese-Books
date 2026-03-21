"""Check if glyph bitmaps have a stray left column artifact."""
from PIL import Image, ImageDraw, ImageFont
import os, sys

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
font_dir = os.path.join(PROJ, 'sd_card', 'fonts')

fonts = sorted(f for f in os.listdir(font_dir) if f.lower().endswith(('.ttf', '.ttc')))
if not fonts:
    print("No fonts found"); sys.exit(1)

test_chars = '日自目口田由甲申曰回因'

for fname in fonts[:3]:
    fp = os.path.join(font_dir, fname)
    try:
        font = ImageFont.truetype(fp, 44)
    except Exception as e:
        print(f"Skip {fname}: {e}")
        continue
    print(f"\n=== {fname} ===")
    for ch in test_chars:
        canvas_size = 44 * 3
        img = Image.new('1', (canvas_size, canvas_size), 1)
        draw = ImageDraw.Draw(img)
        origin = 44
        bbox = draw.textbbox((origin, origin), ch, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        if tw <= 0 or th <= 0:
            print(f"  {ch} U+{ord(ch):04X}: empty glyph")
            continue
        draw.text((origin, origin), ch, font=font, fill=0)
        img_crop = img.crop(bbox)
        w, h = img_crop.size
        left_col_black = sum(1 for y in range(h) if img_crop.getpixel((0, y)) == 0)
        right_col_black = sum(1 for y in range(h) if img_crop.getpixel((w-1, y)) == 0)
        bearing_x = bbox[0] - origin
        bearing_y = bbox[1] - origin
        print(f"  {ch} U+{ord(ch):04X}: {w}x{h} bx={bearing_x} by={bearing_y} "
              f"left_col={left_col_black}/{h} right_col={right_col_black}/{h}")
