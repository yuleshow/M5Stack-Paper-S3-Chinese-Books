#!/usr/bin/env python3
"""Analyze stripped text from an EPUB to understand what the firmware renderer sees."""
import zipfile, re, sys, os

os.chdir('/Users/yuleshow/yuleshow-github/M5Stack Paper S3 for Chinese Books')
epub_path = 'sd_card/books/pg23839-images-3.epub'
epub = zipfile.ZipFile(epub_path)

chapters = ['OEBPS/wrap0000.xhtml',
            'OEBPS/9221026838312290540_23839-0-0.txt.xhtml',
            'OEBPS/9221026838312290540_23839-0-1.txt.xhtml']

for chapter_file in chapters:
    raw = epub.read(chapter_file).decode('utf-8')
    body_match = re.search(r'<body[^>]*>(.*)</body>', raw, re.DOTALL)
    html = body_match.group(1) if body_match else raw

    html = re.sub(r'<br\s*/?>', '\n', html, flags=re.I)
    html = re.sub(r'</(p|div|h[1-6]|section)>', '\n', html, flags=re.I)
    html = re.sub(r'<[^>]+>', '', html)
    html = html.replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>')
    html = html.replace('&nbsp;', ' ')
    html = re.sub(r'[ \t\r\xa0]+', ' ', html)
    lines = [l.strip() for l in html.split('\n') if l.strip()]

    name = chapter_file.split('/')[-1]
    print(f"=== {name} ({len(lines)} lines) ===")
    for i, line in enumerate(lines[:30]):
        ascii_c = sum(1 for c in line if 0x20 <= ord(c) < 0x7F)
        total = len(line)
        pct = ascii_c * 100 // total if total else 0
        tag = "EN" if pct > 70 else ("ZH" if pct < 30 else "MX")
        disp = line[:120] + ('...' if len(line) > 120 else '')
        print(f"  {tag} L{i:2d} ({total:4d}): {disp}")
    if len(lines) > 30:
        print(f"  ... ({len(lines)-30} more)")
    print()
