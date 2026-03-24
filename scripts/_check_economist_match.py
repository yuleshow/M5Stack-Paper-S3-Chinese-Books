#!/usr/bin/env python3
"""Check Economist EPUB manifest-to-ZIP matching."""
import zipfile, re, sys
from xml.etree import ElementTree as ET

for epub_path in ['sd_card/books/The Economist, 2026-02-14.epub']:
    z = zipfile.ZipFile(epub_path)
    opf = z.read('content.opf').decode('utf-8')
    
    # Check manifest for URL-encoded chars
    hrefs = re.findall(r'href="([^"]+)"', opf)
    enc_count = 0
    for h in hrefs:
        if '%' in h or '#' in h or '&' in h:
            print(f'Encoded/special: {h}')
            enc_count += 1
    if enc_count == 0:
        print('No URL-encoded hrefs found')
    
    root = ET.fromstring(opf)
    ns_uri = re.match(r'\{([^}]+)\}', root.tag)
    ns_uri = ns_uri.group(1) if ns_uri else ''
    ns = {'opf': ns_uri}
    manifest = {}
    for item in root.findall('.//opf:manifest/opf:item', ns):
        manifest[item.attrib['id']] = item.attrib.get('href', '')
    
    spine_ids = [itemref.attrib['idref'] for itemref in root.findall('.//opf:spine/opf:itemref', ns)]
    zipnames = set(z.namelist())
    matched = 0
    unmatched = []
    for sid in spine_ids:
        href = manifest.get(sid, '')
        if href in zipnames:
            matched += 1
        else:
            unmatched.append((sid, href))
    
    print(f'Spine={len(spine_ids)}, matched={matched}, unmatched={len(unmatched)}')
    if unmatched:
        for sid, href in unmatched[:10]:
            print(f'  UNMATCHED: {sid} -> "{href}"')

    # Simulate what firmware does: htmlStripDirect with empty basePath
    # Load first 3 chapters and strip HTML, show output
    for sid in spine_ids[:3]:
        href = manifest.get(sid, '')
        if href not in zipnames:
            print(f'  SKIP (not in zip): {href}')
            continue
        content = z.read(href).decode('utf-8', errors='replace')
        # Simple HTML strip simulation
        text = re.sub(r'<script[^>]*>.*?</script>', '', content, flags=re.DOTALL|re.I)
        text = re.sub(r'<style[^>]*>.*?</style>', '', text, flags=re.DOTALL|re.I)
        # Remove head section
        text = re.sub(r'<head[^>]*>.*?</head>', '', text, flags=re.DOTALL|re.I)
        text = re.sub(r'<[^>]+>', '', text)
        text = re.sub(r'\s+', ' ', text).strip()
        print(f'\n  Chapter {sid} -> {href}: {len(content)} bytes HTML -> {len(text)} chars text')
        print(f'    First 100: {text[:100]}')
