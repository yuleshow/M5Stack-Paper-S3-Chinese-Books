#!/usr/bin/env python3
"""Check EPUB text encoding and BIN font coverage for 三体."""
import zipfile, os, struct, sys
from lxml import etree

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(PROJ)

epub_path = 'sd_card/books/【精】三体（全集）.epub'
bin_path = 'sd_card/fonts/GenSenRounded-R_44pt.bin'

# Load BIN font index
with open(bin_path, 'rb') as f:
    data = f.read()
HEADER_SIZE = 137
ENTRY_SIZE = 20
char_count = struct.unpack_from('<I', data, 0)[0]
font_size = data[4]
print(f"BIN font: {char_count} glyphs, size={font_size}")

bin_chars = set()
idx_start = HEADER_SIZE
for i in range(char_count):
    off = idx_start + i * ENTRY_SIZE
    uni = struct.unpack_from('<I', data, off)[0]
    bin_chars.add(uni)

# Extract EPUB text
with zipfile.ZipFile(epub_path) as zf:
    container = etree.fromstring(zf.read('META-INF/container.xml'))
    ns = {'c': 'urn:oasis:names:tc:opendocument:xmlns:container'}
    rootfile = container.find('.//c:rootfile', ns)
    opf_path = rootfile.get('full-path')
    opf_dir = os.path.dirname(opf_path)
    
    opf = etree.fromstring(zf.read(opf_path))
    opf_ns = opf.nsmap.get(None, '')
    
    manifest = {}
    for item in opf.iter('{%s}item' % opf_ns):
        manifest[item.get('id')] = item.get('href')
    
    spine_items = []
    for itemref in opf.iter('{%s}itemref' % opf_ns):
        idref = itemref.get('idref')
        if idref in manifest:
            spine_items.append(manifest[idref])
    
    print(f"EPUB spine: {len(spine_items)} items")
    
    # Extract text from first few chapters
    all_text = []
    for href in spine_items[:5]:
        path = (opf_dir + '/' + href) if opf_dir else href
        try:
            content = zf.read(path)
        except KeyError:
            path = os.path.normpath(path)
            try:
                content = zf.read(path)
            except KeyError:
                continue
        try:
            tree = etree.fromstring(content, etree.HTMLParser(encoding='utf-8'))
            body = tree.find('.//body')
            if body is not None:
                text = etree.tostring(body, method='text', encoding='unicode')
                all_text.append(text)
                print(f"\n--- {href} ({len(text)} chars) ---")
                print(text[:200])
        except Exception as e:
            print(f"Error parsing {href}: {e}")

# Check coverage
full_text = ''.join(all_text)
print(f"\n=== Coverage Analysis ===")
print(f"Total text chars: {len(full_text)}")

unique_chars = set(full_text)
# Filter to CJK and punctuation (skip whitespace, ASCII)
cjk_chars = {ch for ch in unique_chars if ord(ch) > 0x2000}
print(f"Unique CJK/punct chars: {len(cjk_chars)}")

missing = sorted([ch for ch in cjk_chars if ord(ch) not in bin_chars], key=ord)
found = len(cjk_chars) - len(missing)
print(f"Found in BIN: {found}/{len(cjk_chars)}")
if missing:
    print(f"Missing ({len(missing)}):")
    for ch in missing[:50]:
        count = full_text.count(ch)
        print(f"  U+{ord(ch):04X} '{ch}' (appears {count}x)")
else:
    print("All CJK chars covered!")
