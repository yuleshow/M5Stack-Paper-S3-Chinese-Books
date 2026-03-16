#!/usr/bin/env python3
"""Check font names in source fonts and bin files."""
import os, struct

FONTS_DIR = os.path.join(os.path.dirname(__file__), '..', 'sd_card', 'fonts')

# Check TTF/TTC source fonts
try:
    from fontTools.ttLib import TTFont, TTCollection
    for fn in sorted(os.listdir(FONTS_DIR)):
        path = os.path.join(FONTS_DIR, fn)
        if fn.lower().endswith(('.ttf', '.ttc', '.otf')):
            try:
                if fn.lower().endswith('.ttc'):
                    ttc = TTCollection(path)
                    for i, font in enumerate(ttc.fonts):
                        name = font['name'].getDebugName(1)
                        print(f'  {fn} [index {i}]: "{name}"')
                else:
                    font = TTFont(path)
                    name = font['name'].getDebugName(1)
                    print(f'  {fn}: "{name}"')
            except Exception as e:
                print(f'  {fn}: ERROR {e}')
except ImportError:
    print("fontTools not installed, skipping TTF check")

print()

# Check bin file headers
for fn in sorted(os.listdir(FONTS_DIR)):
    if fn.lower().endswith('.bin'):
        path = os.path.join(FONTS_DIR, fn)
        with open(path, 'rb') as f:
            h = f.read(137)
            char_count = struct.unpack('<I', h[0:4])[0]
            font_size = h[4]
            family = h[9:73].split(b'\x00')[0].decode('utf-8', errors='replace')
            style = h[73:137].split(b'\x00')[0].decode('utf-8', errors='replace')
            sz = os.path.getsize(path)
            print(f'  {fn}: family="{family}" style="{style}" size={font_size}pt chars={char_count} ({sz} bytes)')
