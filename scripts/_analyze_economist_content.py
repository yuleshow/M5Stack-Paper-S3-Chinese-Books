#!/usr/bin/env python3
"""Analyze stripped text content of Economist EPUB chapters."""
import zipfile, html, re, sys, os
sys.path.insert(0, os.path.dirname(__file__))

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def strip_html(raw):
    text = raw.decode('utf-8', errors='replace')
    text = re.sub(r'<style[^>]*>.*?</style>', '', text, flags=re.DOTALL|re.IGNORECASE)
    text = re.sub(r'<script[^>]*>.*?</script>', '', text, flags=re.DOTALL|re.IGNORECASE)
    text = re.sub(r'<br[^>]*>', '\n', text, flags=re.IGNORECASE)
    text = re.sub(r'</(p|div|h[1-6]|li|tr)>', '\n', text, flags=re.IGNORECASE)
    text = re.sub(r'<[^>]+>', '', text)
    text = html.unescape(text)
    text = re.sub(r'[ \t]+', ' ', text)
    text = re.sub(r'\n\s*\n', '\n\n', text)
    return text.strip()

import xml.etree.ElementTree as ET

for epub_name in sorted(os.listdir(os.path.join(BASE, 'sd_card', 'books'))):
    if 'economist' not in epub_name.lower():
        continue
    path = os.path.join(BASE, 'sd_card', 'books', epub_name)
    print(f"\n{'='*60}")
    print(f"EPUB: {epub_name}")
    z = zipfile.ZipFile(path)
    
    opf_content = None
    for n in z.namelist():
        if n.endswith('.opf'):
            opf_content = z.read(n).decode('utf-8')
            break
    
    root = ET.fromstring(opf_content)
    ns = {'opf': 'http://www.idpf.org/2007/opf'}
    manifest = {}
    for item in root.findall('.//opf:manifest/opf:item', ns):
        manifest[item.get('id')] = item.get('href')
    
    spine_refs = [ref.get('idref') for ref in root.findall('.//opf:spine/opf:itemref', ns)]
    
    total_stripped = 0
    empty_chapters = []
    print(f"Spine items: {len(spine_refs)}")
    
    for i, ref in enumerate(spine_refs):
        href = manifest.get(ref, '?')
        try:
            raw = z.read(href)
        except KeyError:
            print(f"  Ch {i}: {href} - NOT FOUND in ZIP")
            continue
        stripped = strip_html(raw)
        total_stripped += len(stripped)
        
        if len(stripped) == 0:
            empty_chapters.append(i)
        
        # Print details for first 5 and any empty ones
        if i < 5 or len(stripped) == 0:
            print(f"  Ch {i}: {href}")
            print(f"    Raw: {len(raw)} bytes, Stripped: {len(stripped)} chars")
            if stripped:
                preview = repr(stripped[:150])
                print(f"    Preview: {preview}")
            else:
                print(f"    *** EMPTY ***")
    
    print(f"\nTotal stripped text: {total_stripped} chars")
    print(f"Empty chapters: {empty_chapters}")
    
    # Simulate firmware's bytesPerPage calculation for English horizontal
    # Using default font size 24
    font_size = 24
    char_h = font_size + font_size // 5  # 1.2x
    line_height = char_h
    avail_h = 830 - 88  # READING_AREA_BOTTOM approx - READING_AREA_TOP
    lines_per_page = avail_h // line_height
    avail_w = 500 - 40  # READING_AREA_RIGHT - READING_AREA_LEFT approx
    chars_per_line = avail_w // max(1, font_size * 3 // 5)
    total_chars = lines_per_page * chars_per_line
    bytes_per_page = max(200, total_chars + 50)
    total_pages = (total_stripped // bytes_per_page) + 1
    
    print(f"\nEstimated layout (fontSize={font_size}):")
    print(f"  bytesPerPage: {bytes_per_page}")
    print(f"  totalPages: {total_pages}")
    print(f"  linesPerPage: {lines_per_page}")
    print(f"  charsPerLine: {chars_per_line}")
    
    z.close()
