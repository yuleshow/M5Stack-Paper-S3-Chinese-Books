#!/usr/bin/env python3
"""Diagnose why vertical punctuation disappears in PIL rendering."""
import os
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

font_path = 'sd_card/fonts/Huiwenmincho-improved.ttf'
font = ImageFont.truetype(font_path, 44)

# Test characters: original → vertical mapped
test_pairs = [
    ('\uFF0C', '\uFE10', '，', '︐'),
    ('\u3002', '\uFE12', '。', '︒'),
    ('\u3001', '\uFE11', '、', '︑'),
    ('\u300C', '\uFE41', '「', '﹁'),
    ('\u300D', '\uFE42', '」', '﹂'),
    ('\uFF1A', '\uFE13', '：', '︓'),
    ('\uFF1B', '\uFE14', '；', '︔'),
    ('\uFF01', '\uFF01', '！', '！'),  # no mapping now
    ('\uFF1F', '\uFF1F', '？', '？'),  # no mapping now
    ('\u2014', '\uFE31', '—', '︱'),
    ('\u2026', '\uFE19', '…', '︙'),
]

img = Image.new('L', (600, 800), 255)
draw = ImageDraw.Draw(img)

y = 10
draw.text((10, y), "Original", font=ImageFont.truetype(font_path, 20), fill=0)
draw.text((200, y), "Mapped", font=ImageFont.truetype(font_path, 20), fill=0)
draw.text((400, y), "Has glyph?", font=ImageFont.truetype(font_path, 20), fill=0)
y += 40

for orig, mapped, orig_name, mapped_name in test_pairs:
    # Draw original
    draw.text((10, y), orig, font=font, fill=0)
    draw.text((60, y), f"U+{ord(orig):04X} {orig_name}", font=ImageFont.truetype(font_path, 18), fill=0)
    
    # Draw mapped
    draw.text((200, y), mapped, font=font, fill=0)
    draw.text((260, y), f"U+{ord(mapped):04X} {mapped_name}", font=ImageFont.truetype(font_path, 18), fill=0)

    # Check if mapped glyph has any visible content
    test_img = Image.new('L', (60, 60), 255)
    test_draw = ImageDraw.Draw(test_img)
    test_draw.text((5, 5), mapped, font=font, fill=0)
    bbox = test_img.getbbox()
    has_glyph = bbox is not None
    
    status = "YES" if has_glyph else "NO - MISSING"
    draw.text((400, y), status, font=ImageFont.truetype(font_path, 18), fill=0)
    
    print(f"{orig_name} U+{ord(orig):04X} → {mapped_name} U+{ord(mapped):04X} : {'OK' if has_glyph else 'MISSING'}")
    y += 55

img.save('output/punct_diagnosis.png')
print("\nSaved: output/punct_diagnosis.png")
