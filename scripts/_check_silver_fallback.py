#!/usr/bin/env python3
"""Check Silver BIN fallback glyph sizes for consistency."""
from PIL import Image, ImageDraw, ImageFont
import struct, os

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

scale_table = {32:44, 36:49, 40:55, 44:61, 48:66, 52:72, 56:77, 60:83, 64:88}

for target in [36, 44, 52]:
    render_sz = scale_table.get(target, target)
    
    # Measure Silver native glyph size at render_sz
    silver = ImageFont.truetype('sd_card/fonts/Silver.ttf', render_sz)
    img = Image.new('1', (300, 300), 1)
    draw = ImageDraw.Draw(img)
    sample_widths = []
    for sc in '盡陀人心世郡第一大是國中不為':
        sb = draw.textbbox((0, 0), sc, font=silver)
        sw = sb[2] - sb[0]
        if sw > 0:
            sample_widths.append(sw)
    avg_glyph = sum(sample_widths) / len(sample_widths) if sample_widths else render_sz
    fb_size = int(avg_glyph + 0.5)
    
    # Check fallback at that size
    fb = ImageFont.truetype('sd_card/fonts/GenYoMinTW-Regular.ttf', fb_size)
    print(f"\n=== Target {target}pt (render {render_sz}pt, fallback@{fb_size}pt) ===")
    for ch in '酆跎人盡世':
        # Fallback
        bbox = draw.textbbox((0, 0), ch, font=fb)
        fw, fh = bbox[2]-bbox[0], bbox[3]-bbox[1]
        # Silver native
        bbox2 = draw.textbbox((0, 0), ch, font=silver)
        sw2, sh2 = bbox2[2]-bbox2[0], bbox2[3]-bbox2[1]
        src = "FALLBACK" if sw2 == 0 else "silver"
        w = fw if sw2 == 0 else sw2
        h = fh if sw2 == 0 else sh2
        print(f"  {ch} U+{ord(ch):04X}: {src:8s} {w}x{h}  (header fontSize={render_sz})")

    # Also read the actual BIN to check what's stored
    bin_path = f'sd_card/fonts/Silver_{target}pt.bin'
    if os.path.exists(bin_path):
        with open(bin_path, 'rb') as f:
            char_count = struct.unpack('<I', f.read(4))[0]
            font_size_byte = struct.unpack('B', f.read(1))[0]
            print(f"  BIN header: fontSize={font_size_byte}, charCount={char_count}")
            # Read a few glyph entries to check 酆 跎
            f.seek(137)  # after header
            for _ in range(min(char_count, 21500)):
                entry = f.read(20)
                if len(entry) < 20:
                    break
                uc, w, h, sz, bx, by, off = struct.unpack('<IHHIHHI', entry)
                if uc in [0x9146, 0x8DCE, 0x4EBA]:  # 酆 跎 人
                    print(f"  BIN glyph {chr(uc)} U+{uc:04X}: {w}x{h} bearing=({bx},{by})")
