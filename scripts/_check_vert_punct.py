#!/usr/bin/env python3
"""Check which vertical punctuation glyphs Silver and Huiwen have."""
import os, sys
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from fontTools.ttLib import TTFont

# Vertical form targets from toVerticalPunct() in utf8_utils.h
vert_targets = [
    (0xFE35, "parenthesis-L"),  (0xFE36, "parenthesis-R"),
    (0xFE37, "brace-L"),        (0xFE38, "brace-R"),
    (0xFE39, "tortoise-L"),     (0xFE3A, "tortoise-R"),
    (0xFE3B, "lenticular-L"),   (0xFE3C, "lenticular-R"),
    (0xFE3D, "double-angle-L"), (0xFE3E, "double-angle-R"),
    (0xFE3F, "angle-L"),        (0xFE40, "angle-R"),
    (0xFE41, "corner-L"),       (0xFE42, "corner-R"),
    (0xFE43, "white-corner-L"), (0xFE44, "white-corner-R"),
    (0xFE47, "bracket-L"),      (0xFE48, "bracket-R"),
    (0xFE17, "lenticular-2-L"), (0xFE18, "lenticular-2-R"),
    (0xFE19, "vert-ellipsis"),  (0xFE30, "two-dot-leader"),
    (0xFE31, "em-dash"),        (0xFE34, "wavy-low-line"),
]

# Common CJK punct (not remapped by toVerticalPunct)
common_punct = [
    (0xFF0C, "comma"),     (0x3002, "period"),
    (0xFF01, "exclam"),    (0xFF1F, "question"),
    (0xFF1A, "colon"),     (0xFF1B, "semicolon"),
    (0x3001, "ideographic-comma"),
]

fonts = [
    ("Silver", "sd_card/fonts/Silver.ttf"),
    ("Huiwen", "sd_card/fonts/Huiwenmincho-improved.ttf"),
    ("GenYoMin", "sd_card/fonts/GenYoMinTW-Regular.ttf"),
]

for name, path in fonts:
    font = TTFont(path)
    cmap = font.getBestCmap()
    print(f"\n=== {name} ===")
    
    ok = 0
    miss = 0
    print("  Vertical form targets:")
    for cp, desc in vert_targets:
        has = cp in cmap
        if has: ok += 1
        else: miss += 1
        status = "YES" if has else "MISSING"
        print(f"    U+{cp:04X} {desc:20s} {status}")
    
    print("  Common CJK punct:")
    for cp, desc in common_punct:
        has = cp in cmap
        if has: ok += 1
        else: miss += 1
        status = "YES" if has else "MISSING"
        print(f"    U+{cp:04X} {desc:20s} {status}")
    
    print(f"  Total: {ok} present, {miss} missing")
