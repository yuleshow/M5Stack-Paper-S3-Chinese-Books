#!/usr/bin/env python3
"""Check vertical punctuation coverage across fonts."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from fontTools.ttLib import TTFont

vert_punct = {
    0xFE10: 'VERTICAL COMMA',
    0xFE11: 'VERTICAL IDEOGRAPHIC COMMA',
    0xFE12: 'VERTICAL IDEOGRAPHIC FULL STOP',
    0xFE13: 'VERTICAL COLON',
    0xFE14: 'VERTICAL SEMICOLON',
    0xFE15: 'VERTICAL EXCLAMATION MARK',
    0xFE16: 'VERTICAL QUESTION MARK',
    0xFE17: 'VERTICAL LEFT WHITE LENTICULAR BRACKET',
    0xFE18: 'VERTICAL RIGHT WHITE LENTICULAR BRACKET',
    0xFE19: 'VERTICAL HORIZONTAL ELLIPSIS',
    0xFE30: 'VERTICAL TWO DOT LEADER',
    0xFE31: 'VERTICAL EM DASH',
    0xFE32: 'VERTICAL EN DASH',
    0xFE33: 'VERTICAL LOW LINE',
    0xFE34: 'VERTICAL WAVY LOW LINE',
    0xFE35: 'VERTICAL LEFT PARENTHESIS',
    0xFE36: 'VERTICAL RIGHT PARENTHESIS',
    0xFE37: 'VERTICAL LEFT CURLY BRACKET',
    0xFE38: 'VERTICAL RIGHT CURLY BRACKET',
    0xFE39: 'VERTICAL LEFT TORTOISE SHELL BRACKET',
    0xFE3A: 'VERTICAL RIGHT TORTOISE SHELL BRACKET',
    0xFE3B: 'VERTICAL LEFT BLACK LENTICULAR BRACKET',
    0xFE3C: 'VERTICAL RIGHT BLACK LENTICULAR BRACKET',
    0xFE3D: 'VERTICAL LEFT DOUBLE ANGLE BRACKET',
    0xFE3E: 'VERTICAL RIGHT DOUBLE ANGLE BRACKET',
    0xFE3F: 'VERTICAL LEFT ANGLE BRACKET',
    0xFE40: 'VERTICAL RIGHT ANGLE BRACKET',
    0xFE41: 'VERTICAL LEFT CORNER BRACKET',
    0xFE42: 'VERTICAL RIGHT CORNER BRACKET',
    0xFE43: 'VERTICAL LEFT WHITE CORNER BRACKET',
    0xFE44: 'VERTICAL RIGHT WHITE CORNER BRACKET',
    0xFE47: 'VERTICAL LEFT SQUARE BRACKET',
    0xFE48: 'VERTICAL RIGHT SQUARE BRACKET',
}

fonts = {
    'Huiwenmincho': 'sd_card/fonts/Huiwenmincho-improved.ttf',
    'Silver':       'sd_card/fonts/Silver.ttf',
    'GenYoMinTW':   'sd_card/fonts/GenYoMinTW-Regular.ttf',
}

for fname, fpath in fonts.items():
    tt = TTFont(fpath)
    cmap = tt.getBestCmap()
    missing = []
    present = []
    for cp, desc in sorted(vert_punct.items()):
        if cp in cmap:
            present.append(f'  OK  U+{cp:04X} {desc}')
        else:
            missing.append(f'  MISS U+{cp:04X} {desc}')
    total = len(vert_punct)
    print(f'\n=== {fname} ===  ({total - len(missing)}/{total} present, {len(missing)} missing)')
    if missing:
        for m in missing:
            print(m)
    tt.close()
