#!/usr/bin/env python3
"""Check all EPUBs in sd_card/books for compatibility with the device."""
import os, zipfile, re, sys

books_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sd_card", "books")

TITLE_MAX_ENTRIES = 200  # Device limit for title scan

for fname in sorted(os.listdir(books_dir)):
    if not fname.lower().endswith('.epub'):
        continue
    fpath = os.path.join(books_dir, fname)
    print(f"=== {fname} ===")
    try:
        z = zipfile.ZipFile(fpath)
        entries = z.namelist()
        num_entries = len(entries)
        print(f"  ZIP entries: {num_entries}")
        
        # Check if container.xml is within first 200 entries
        container_idx = None
        for i, e in enumerate(entries):
            if 'container.xml' in e:
                container_idx = i
                break
        print(f"  container.xml: found at index {container_idx}" if container_idx is not None else "  container.xml: NOT FOUND")
        if container_idx is not None and container_idx >= TITLE_MAX_ENTRIES:
            print(f"  *** WARNING: container.xml at index {container_idx} >= {TITLE_MAX_ENTRIES}, title scan will miss it!")
        
        # OPF files
        opf_files = [e for e in entries if e.endswith('.opf')]
        opf_in_first_200 = [e for i, e in enumerate(entries) if e.endswith('.opf') and i < TITLE_MAX_ENTRIES]
        print(f"  OPF files: {opf_files}")
        if opf_files and not opf_in_first_200:
            print(f"  *** WARNING: OPF not in first {TITLE_MAX_ENTRIES} entries, title may not be extracted!")
        
        # Title
        title = None
        if opf_files:
            opf_content = z.read(opf_files[0]).decode('utf-8', errors='replace')
            m = re.search(r'<dc:title[^>]*>(.*?)</dc:title>', opf_content, re.DOTALL)
            if m:
                title = m.group(1).strip()
        print(f"  Title: {title}" if title else "  Title: NOT FOUND")
        if title:
            print(f"  Title length: {len(title)} chars")
        
        # Content stats
        html_files = [e for e in entries if e.lower().endswith(('.html', '.xhtml', '.htm'))]
        img_files = [e for e in entries if e.lower().endswith(('.jpg', '.jpeg', '.png', '.gif', '.webp'))]
        print(f"  HTML/XHTML: {len(html_files)}, Images: {len(img_files)}")
        
        # File size
        fsize = os.path.getsize(fpath)
        print(f"  File size: {fsize:,} bytes ({fsize/1024/1024:.1f} MB)")
        if fsize < 100:
            print(f"  *** WARNING: File too small (<100 bytes), title extraction will be skipped!")
        if fsize > 100*1024*1024:
            print(f"  *** WARNING: File too large (>100MB), title extraction will be skipped!")
        
        # Check for very long filenames in ZIP
        long_names = [e for e in entries if len(e) > 255]
        if long_names:
            print(f"  *** WARNING: {len(long_names)} entries with names >255 chars!")
        
        # Manga detection: >70% image-only chapters
        if len(html_files) > 0 and len(img_files) / max(1, len(html_files)) > 2:
            print(f"  NOTE: Likely manga/image-based EPUB (img/html ratio = {len(img_files)/len(html_files):.1f})")
        
        z.close()
    except Exception as ex:
        print(f"  ERROR: {ex}")
    print()
