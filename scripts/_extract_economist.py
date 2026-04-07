#!/usr/bin/env python3
"""Extract a chapter from The Economist EPUB and render a sample page."""
import os, sys, zipfile, re

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

epub_path = 'sd_card/books/The Economist, 2026-02-21.epub'
z = zipfile.ZipFile(epub_path)

# Find the chapter with "income" text
target = 'feed_1/article_0/index_u37.html'
data = z.read(target).decode('utf-8', errors='replace')

# Simple HTML strip (same logic as device)
def html_strip(html):
    out = []
    in_script = False
    in_head = False
    last_newline = False
    last_space = False
    i = 0
    while i < len(html):
        c = html[i]
        if c == '<':
            tag_end = i + 1
            while tag_end < len(html) and html[tag_end] != '>':
                tag_end += 1
            if tag_end >= len(html): break
            tag = ''
            j = i + 1
            while j < tag_end and len(tag) < 30:
                tc = html[j]
                if tc in ' \t\n\r':
                    if len(tag) > 0: break
                    j += 1; continue
                if tc == '/' and len(tag) == 0: tag += '/'; j += 1; continue
                tag += tc.lower(); j += 1
            if tag == 'head': in_head = True
            elif tag == '/head': in_head = False
            if tag in ('script', 'style'): in_script = True
            elif tag in ('/script', '/style'): in_script = False
            if not in_script:
                if tag in ('/p', '/div', 'br', 'br/') or (tag.startswith('/h') and len(tag) == 3):
                    if not last_newline and len(out) > 0:
                        out.append('\n')
                        last_newline = True
                        last_space = True
            i = tag_end + 1
            continue
        if in_script or in_head: i += 1; continue
        if c in '\n\r \t' or c == '\xa0':
            if not last_space and not last_newline:
                out.append(' ')
                last_space = True
        else:
            out.append(c)
            last_newline = False
            last_space = False
        i += 1
    return ''.join(out)

text = html_strip(data)
print(text[:3000])
