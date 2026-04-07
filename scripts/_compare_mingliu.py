#!/usr/bin/env python3
"""Check if mingliu.ttc covers the chars that MingLiU.ttf lacks."""
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from fontTools.ttLib import TTFont

ttf = TTFont('sd_card/fonts/MingLiU.ttf')
ttf_cmap = set(ttf.getBestCmap().keys())

# Target character ranges used by convert_ttf_to_bin.py
target_ranges = [(0x4E00,0x9FFF),(0x3000,0x303F),(0xFF00,0xFFEF),(0xFE10,0xFE1F),(0xFE30,0xFE4F)]
target_chars = set()
for s,e in target_ranges:
    target_chars.update(range(s,e))
target_chars.update(range(32,127))

missing_in_ttf = sorted(target_chars - ttf_cmap)
print(f"MingLiU.ttf: missing {len(missing_in_ttf)} chars from target ranges")

for i in range(3):
    f = TTFont('sd_card/fonts/mingliu.ttc', fontNumber=i)
    c = set(f.getBestCmap().keys())
    name = f['name'].getBestFamilyName()
    still_missing = [cp for cp in missing_in_ttf if cp not in c]
    recovered = [cp for cp in missing_in_ttf if cp in c]
    print(f"mingliu.ttc[{i}] ({name}): {len(recovered)} recovered, {len(still_missing)} still missing")

# Show what's missing
print("\nMissing codepoints (first 30):")
for cp in missing_in_ttf[:30]:
    print(f"  U+{cp:04X}")
