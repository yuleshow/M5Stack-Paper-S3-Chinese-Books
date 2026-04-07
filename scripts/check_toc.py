#!/usr/bin/env python3
"""Check TOC (NCX/nav.xhtml) in sample EPUBs."""
import zipfile, os, re

books_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sd_card', 'books')

for epub in sorted(os.listdir(books_dir)):
    if not epub.endswith('.epub'):
        continue
    path = os.path.join(books_dir, epub)
    try:
        with zipfile.ZipFile(path) as z:
            names = z.namelist()
            ncx = [n for n in names if n.endswith('.ncx')]
            nav = [n for n in names if 'nav' in n.lower() and n.endswith('.xhtml')]
            print(f"\n=== {epub[:60]} ===")
            if ncx:
                print(f"  NCX: {ncx[0]}")
                content = z.read(ncx[0]).decode('utf-8', errors='replace')
                points = re.findall(
                    r'<navPoint[^>]*>.*?<text>(.*?)</text>.*?<content\s+src="(.*?)"',
                    content, re.DOTALL
                )
                for label, src in points[:8]:
                    print(f"    {label.strip()} -> {src}")
                if len(points) > 8:
                    print(f"    ... ({len(points)} total)")
            if nav:
                print(f"  NAV: {nav}")
            if not ncx and not nav:
                print("  (no TOC found)")
    except Exception as e:
        print(f"  ERROR: {e}")
