#!/usr/bin/env python3
"""Verify labels.bin format integrity."""
import struct, os

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

f = open('sd_card/labels.bin', 'rb')
magic = f.read(4)
count = struct.unpack('<I', f.read(4))[0]
print(f'Magic: {magic}, Count: {count}')

for i in range(count):
    tl = struct.unpack('<H', f.read(2))[0]
    text = f.read(tl).decode('utf-8')
    f.read(1)  # null
    fs, w, h = struct.unpack('<HHH', f.read(6))
    bmp_size = ((w + 1) // 2) * h
    bmp = f.read(bmp_size)
    if i < 5 or i >= count - 3 or i in [500, 800, 1000]:
        print(f'  [{i:4d}] "{text:20s}" size={fs:3d} {w:3d}x{h:<3d} bmp={bmp_size:5d}B')

pos = f.tell()
f.seek(0, 2)
end = f.tell()
print(f'\nParsed {count} entries, read {pos} of {end} bytes, leftover={end-pos}')
f.close()
