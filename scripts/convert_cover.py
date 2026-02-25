#!/usr/bin/env python3
"""Convert s3cover.jpg to a C++ header file for embedding in firmware."""

from PIL import Image
import io
import os

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# Load and convert to grayscale (e-ink is grayscale anyway)
img = Image.open('assets/s3cover.jpg').convert('L')

# Save optimized grayscale JPEG
buf = io.BytesIO()
img.save(buf, 'JPEG', quality=80, optimize=True)
jpg_data = buf.getvalue()

print(f"Image size: {len(jpg_data)} bytes ({len(jpg_data)/1024:.1f} KB)")

# Generate C header
output_path = os.path.join('src', 's3cover_jpg.h')
with open(output_path, 'w') as f:
    f.write("// Auto-generated from assets/s3cover.jpg\n")
    f.write("// Grayscale optimized for e-ink display (540x960)\n")
    f.write("#pragma once\n\n")
    f.write("#include <pgmspace.h>\n\n")
    f.write(f"const size_t s3cover_jpg_len = {len(jpg_data)};\n\n")
    f.write("const uint8_t s3cover_jpg[] PROGMEM = {\n")

    for i in range(0, len(jpg_data), 16):
        chunk = jpg_data[i:i+16]
        hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
        if i + 16 < len(jpg_data):
            f.write(f"    {hex_str},\n")
        else:
            f.write(f"    {hex_str}\n")

    f.write("};\n")

print(f"Generated {output_path}")
