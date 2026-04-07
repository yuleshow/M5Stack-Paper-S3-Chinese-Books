#!/usr/bin/env python3
"""Check RGBA icon handling."""
from PIL import Image

for name in ['back.png', 'next.png', 'return.png', 'reader_toolbar.png']:
    ic = Image.open(f'assets/icons/{name}')
    print(f'{name}: size={ic.size}, mode={ic.mode}')
    if ic.mode == 'RGBA':
        r, g, b, a = ic.split()
        alpha_vals = list(a.getdata())
        transparent = sum(1 for v in alpha_vals if v == 0)
        print(f'  transparent pixels: {transparent}/{len(alpha_vals)}')

        # Direct convert to L (loses alpha - transparent becomes dark)
        ic_l = ic.convert('L')
        dark_direct = sum(1 for v in ic_l.getdata() if v < 128)

        # Composite on white first (correct approach)
        bg = Image.new('RGBA', ic.size, (255, 255, 255, 255))
        composited = Image.alpha_composite(bg, ic)
        ic_l2 = composited.convert('L')
        dark_composited = sum(1 for v in ic_l2.getdata() if v < 128)

        print(f'  Dark pixels direct L: {dark_direct}')
        print(f'  Dark pixels composited: {dark_composited}')
