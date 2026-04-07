#!/usr/bin/env python3
"""List all nameID=1 records from fonts, showing Chinese names."""
import os
from fontTools.ttLib import TTFont, TTCollection

fonts_dir = os.path.join(os.path.dirname(__file__), '..', 'sd_card', 'fonts')

for fn in sorted(os.listdir(fonts_dir)):
    path = os.path.join(fonts_dir, fn)
    if fn.lower().endswith(('.ttf', '.otf')):
        font = TTFont(path)
        print(f'=== {fn} ===')
        for rec in font['name'].names:
            if rec.nameID in (1, 4):
                try:
                    val = rec.toUnicode()
                    print(f'  nameID={rec.nameID} platID={rec.platformID} encID={rec.platEncID} langID={rec.langID}: "{val}"')
                except:
                    pass
        font.close()
    elif fn.lower().endswith('.ttc'):
        ttc = TTCollection(path)
        for idx, font in enumerate(ttc.fonts):
            print(f'=== {fn} [index {idx}] ===')
            for rec in font['name'].names:
                if rec.nameID in (1, 4):
                    try:
                        val = rec.toUnicode()
                        print(f'  nameID={rec.nameID} platID={rec.platformID} encID={rec.platEncID} langID={rec.langID}: "{val}"')
                    except:
                        pass
        ttc.close()
