"""Inspect BIN font glyph bitmaps for left-bar artifacts."""
import struct, os, sys
from PIL import Image

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fonts_dir = os.path.join(PROJ, 'sd_card', 'fonts')

bins = sorted(f for f in os.listdir(fonts_dir) if f.lower().endswith('.bin'))
print('BIN files:', bins)

test_chars = {0x65E5: '日', 0x81EA: '自', 0x76EE: '目', 0x53E3: '口', 0x904E: '過', 0x7684: '的'}

for bf in bins[:3]:
    path = os.path.join(fonts_dir, bf)
    with open(path, 'rb') as f:
        magic = f.read(4)
        version = struct.unpack('<B', f.read(1))[0]
        font_size = struct.unpack('<B', f.read(1))[0]
        char_count = struct.unpack('<I', f.read(4))[0]
        name = f.read(127).rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f'\n=== {bf}: v{version}, size={font_size}, chars={char_count}, name={name} ===')

        # Read all index entries
        entries = {}
        for i in range(char_count):
            data = f.read(20)
            if len(data) < 20:
                break
            uni, w, h, bx, by, off, sz = struct.unpack('<IHHbbIH', data)
            if uni in test_chars:
                entries[uni] = (w, h, bx, by, off, sz)

        # Now read and check bitmaps
        for uni in sorted(entries.keys()):
            w, h, bx, by, off, sz = entries[uni]
            f.seek(off)
            bitmap = f.read(sz)
            # Check leftmost and rightmost columns
            left_black = 0
            right_black = 0
            for py in range(h):
                # Left column (px=0)
                bit_pos = py * w + 0
                byte_idx = bit_pos // 8
                bit_off = 7 - (bit_pos % 8)
                if byte_idx < len(bitmap) and (bitmap[byte_idx] >> bit_off) & 1:
                    left_black += 1
                # Right column (px=w-1)
                bit_pos = py * w + (w - 1)
                byte_idx = bit_pos // 8
                bit_off = 7 - (bit_pos % 8)
                if byte_idx < len(bitmap) and (bitmap[byte_idx] >> bit_off) & 1:
                    right_black += 1
            ch = test_chars[uni]
            print(f'  {ch} U+{uni:04X}: {w}x{h} bx={bx} by={by} '
                  f'left_col={left_black}/{h} right_col={right_black}/{h}')
