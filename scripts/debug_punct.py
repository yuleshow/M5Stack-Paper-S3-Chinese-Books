#!/usr/bin/env python3
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
            if t: self.texts.append(t)

parser = TextExtractor()
with zipfile.ZipFile('sd_card/books/pg24113-images-3.epub') as zf:
    for name in sorted(zf.namelist()):
        if name.endswith(('.htm','.html','.xhtml')):
            parser.feed(zf.read(name).decode('utf-8','ignore'))

good = [t for t in parser.texts if len(t) > 20 and any('\u4e00' <= c <= '\u9fff' for c in t)]
random.seed(42)
random.shuffle(good)
result = ''
for t in good:
    result += t + '\n'
    if len(result) >= 500:
        break

# Count punctuation
punct_list = [
    ('\uFF0C', 'comma'), ('\u3002', 'period'), ('\u3001', 'enum-comma'),
    ('\uFF1A', 'colon'), ('\uFF1B', 'semicolon'),
    ('\uFF01', 'excl'), ('\uFF1F', 'question'),
    ('\u300C', 'lquote'), ('\u300D', 'rquote'),
    ('\u300E', 'ldquote'), ('\u300F', 'rdquote'),
    ('\u2014', 'dash'), ('\u2026', 'ellipsis'),
]
print("Punctuation in extracted text:")
total = 0
for ch, name in punct_list:
    cnt = result.count(ch)
    total += cnt
    if cnt:
        print(f"  U+{ord(ch):04X} {name}: {cnt}")
print(f"Total punctuation: {total}")
print(f"\nFirst 200 chars:\n{result[:200]}")
