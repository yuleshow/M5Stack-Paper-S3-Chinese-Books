#!/usr/bin/env python3
"""Generate silver_cover_jpg.h from assets/silver-cover.jpeg."""
import os
from PIL import Image

src = os.path.join(os.path.dirname(__file__), '..', 'assets', 'silver-cover.jpeg')
tmp = '/tmp/silver_cover_540x960.jpg'
dst = os.path.join(os.path.dirname(__file__), '..', 'src', 'silver_cover_jpg.h')

# Resize to 540x960 grayscale
img = Image.open(src)
img = img.resize((540, 960), Image.LANCZOS).convert('L')
img.save(tmp, 'JPEG', quality=85)
print(f'Resized: {img.size}, {os.path.getsize(tmp)} bytes')

with open(tmp, 'rb') as f:
    data = f.read()

with open(dst, 'w') as f:
    f.write('// Auto-generated from assets/silver-cover.jpeg\n')
    f.write('// Grayscale optimized for e-ink display (540x960)\n')
    f.write('#pragma once\n\n')
    f.write('#include <pgmspace.h>\n\n')
    f.write(f'const size_t silver_cover_jpg_len = {len(data)};\n\n')
    f.write('const uint8_t silver_cover_jpg[] PROGMEM = {\n')
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ', '.join(f'0x{b:02x}' for b in chunk)
        comma = ',' if i + 16 < len(data) else ''
        f.write(f'    {hex_vals}{comma}\n')
    f.write('};\n')

print(f'Generated {dst} ({len(data)} bytes embedded)')
