#!/usr/bin/env python3
"""Check JPEG format in EPUB."""
import struct, zipfile, sys

epub = "/Users/yuleshow/yuleshow-github/M5Stack Paper S3 for Chinese Books/assets/books/\u4e30\u5b50\u607a\u6f2b\u753b\u7cbe\u54c1\u96c6(\u4fee\u8ba2\u7248)(\u4e30\u5b50\u607a[\u4e30\u5b50\u607a]).epub"
zf = zipfile.ZipFile(epub)

for name in ['images/00001.jpeg', 'images/00002.jpeg', 'cover.jpeg']:
    imgdata = zf.read(name)
    i = 0
    while i < len(imgdata) - 1:
        if imgdata[i] != 0xFF:
            break
        marker = imgdata[i+1]
        if marker == 0xD8:
            i += 2
            continue
        if marker in (0xC0, 0xC2):
            h = struct.unpack('>H', imgdata[i+5:i+7])[0]
            w = struct.unpack('>H', imgdata[i+7:i+9])[0]
            mode = 'Progressive' if marker == 0xC2 else 'Baseline'
            print(f"{name}: {w}x{h}, {mode}, {len(imgdata)} bytes")
            break
        if marker == 0xD9:
            break
        length = struct.unpack('>H', imgdata[i+2:i+4])[0]
        i += 2 + length
