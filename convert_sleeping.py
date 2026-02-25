#!/usr/bin/env python3
"""Convert sleeping.jpg to a C++ header file for embedding in firmware."""

from PIL import Image
import io
import os

img = Image.open('assets/sleeping.jpg').convert('L')
print(f'Original size: {img.size}')

# Resize to 540x960 for e-ink display
img = img.resize((540, 960), Image.LANCZOS)
print(f'Resized to: {img.size}')

buf = io.BytesIO()
img.save(buf, 'JPEG', quality=75, optimize=True)
jpg_data = buf.getvalue()
print(f'JPEG size: {len(jpg_data)} bytes ({len(jpg_data)/1024:.1f} KB)')

output_path = os.path.join('src', 'sleeping_jpg.h')
with open(output_path, 'w') as f:
    f.write('// Auto-generated from assets/sleeping.jpg\n')
    f.write('// Grayscale optimized for e-ink sleep screen (540x960)\n')
    f.write('#pragma once\n\n')
    f.write('#include <pgmspace.h>\n\n')
    f.write(f'const size_t sleeping_jpg_len = {len(jpg_data)};\n\n')
    f.write('const uint8_t sleeping_jpg[] PROGMEM = {\n')
    for i in range(0, len(jpg_data), 16):
        chunk = jpg_data[i:i+16]
        hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
        if i + 16 < len(jpg_data):
            f.write(f'    {hex_str},\n')
        else:
            f.write(f'    {hex_str}\n')
    f.write('};\n')

print(f'Generated {output_path}')
