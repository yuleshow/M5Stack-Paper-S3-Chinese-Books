#!/usr/bin/env python3
"""Verify kuanyin.bin format with embedded wording data."""
import struct, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

with open('sd_card/fortune_slips/kuanyin.bin', 'rb') as f:
    magic = f.read(4)
    count, imgW, imgH, flags = struct.unpack('<HHHH', f.read(8))
    print(f"Magic: {magic}, count={count}, w={imgW}, h={imgH}, flags={flags}")
    print(f"  has_cover={bool(flags & 1)}, has_wording={bool(flags & 2)}")

    if flags & 2:
        pos = 12 + count * 8
        if flags & 1:
            pos += 8
        f.seek(pos)
        wo, fp, r = struct.unpack('<IHH', f.read(8))
        print(f"  Wording block at {wo}, fields_per={fp}")

        f.seek(wo)
        o0 = struct.unpack('<I', f.read(4))[0]
        f.seek(wo + 4)
        o1 = struct.unpack('<I', f.read(4))[0]
        dl = o1 - o0
        print(f"  Slip 0 wording: offset={o0}, len={dl}")

        f.seek(o0)
        d = f.read(dl)
        labels = ['sign','rank','palace','poem','meaning','interpretation',
                  'story_title','story_body',
                  'home','self','wealth','trade','marriage','pregnancy',
                  'traveler','silkworm','livestock','missing','lawsuit',
                  'relocation','lost','illness','tomb']
        for i, v in enumerate(d.split(b'\x00')[:fp]):
            t = v.decode('utf-8')
            print(f"    [{i:2d}] {labels[i]:14s}: {t[:60]}{'...' if len(t)>60 else ''}")
    else:
        print("  No wording data in this binary!")
