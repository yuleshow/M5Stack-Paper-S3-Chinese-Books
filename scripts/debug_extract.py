#!/usr/bin/env python3
"""Debug: show extracted text with punctuation analysis."""
import os, zipfile, html.parser, random
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

class TextExtractor(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.texts = []
        self.in_p = False
    def handle_starttag(self, tag, attrs):
        if tag == 'p': self.in_p = True
    def handle_endtag(self, tag):
        if tag == 'p': self.in_p = False
    def handle_data(self, data):
        if self.in_p:
            t = data.strip()
            if t:
                self.texts.append(t)

parser = TextExtractor()
with zipfile.ZipFile('sd_card/books/pg24113-images-3.epub') as zf:
    for name in sorted(zf.namelist()):
        if name.endswith(('.htm', '.html', '.xhtml')):
            content = zf.read(name).decode('utf-8', errors='ignore')
            parser.feed(content)

good = [t for t in parser.texts if len(t) > 20 and any('\u4e00' <= c <= '\u9fff' for c in t)]
random.seed(42)
random.shuffle(good)

result = ''
for t in good:
    result += t + '\n'
    if len(result) >= 2000:
        break
result = result[:2000]

# Count punctuation
punct_chars = set('\uFF0C\u3002\u3001\u300C\u300D\uFF01\uFF1F\uFF1A\uFF1B\u2014\u2026')
found = {c: 0 for c in punct_chars}
for c in result:
    if c in found:
        found[c] += 1

print(f"Total chars: {len(result)}")
print(f"\nPunctuation counts:")
for c, cnt in sorted(found.items(), key=lambda x: -x[1]):
    print(f"  {c} U+{ord(c):04X} : {cnt}")

print(f"\nFirst 200 chars:")
print(repr(result[:200]))
