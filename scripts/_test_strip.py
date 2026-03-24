#!/usr/bin/env python3
"""Test htmlStripDirect logic to find why spaces are lost"""
import zipfile

z = zipfile.ZipFile('sd_card/books/The Economist, 2026-02-21.epub')
data = z.read('feed_1/article_0/index_u37.html').decode('utf-8', errors='replace')

# Simulate htmlStripDirect exactly as in C++
outBuf = []
inScript = False
inHead = False
lastWasNewline = False
lastWasSpace = False
i = 0
while i < len(data):
    c = data[i]
    if c == '<':
        tagEnd = i + 1
        while tagEnd < len(data) and data[tagEnd] != '>':
            tagEnd += 1
        if tagEnd >= len(data):
            break
        tagName = ''
        j = i + 1
        while j < tagEnd and len(tagName) < 30:
            tc = data[j]
            if tc in ' \t\n\r':
                if len(tagName) > 0:
                    break
                j += 1
                continue
            if tc == '/' and len(tagName) == 0:
                tagName += '/'
                j += 1
                continue
            tagName += tc.lower()
            j += 1
        if tagName == 'head':
            inHead = True
        elif tagName == '/head':
            inHead = False
        if tagName in ('script', 'style'):
            inScript = True
        elif tagName in ('/script', '/style'):
            inScript = False
        if not inScript:
            if tagName in ('/p', '/div', 'br', 'br/') or (tagName.startswith('/h') and len(tagName) == 3):
                if not lastWasNewline and len(outBuf) > 0:
                    outBuf.append('\n')
                    lastWasNewline = True
                    lastWasSpace = True
        i = tagEnd
        i += 1
        continue
    if inScript or inHead:
        i += 1
        continue
    if c in '\n\r \t' or c == '\xa0':
        if not lastWasSpace and not lastWasNewline:
            outBuf.append(' ')
            lastWasSpace = True
    else:
        outBuf.append(c)
        lastWasNewline = False
        lastWasSpace = False
    i += 1

text = ''.join(outBuf)
idx = text.lower().find('income')
if idx >= 0:
    print('=== Python simulation result ===')
    print(repr(text[max(0,idx-100):idx+300]))
else:
    print('income not found, showing first 500 chars:')
    print(repr(text[:500]))
