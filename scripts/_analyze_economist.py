#!/usr/bin/env python3
"""Analyze Economist EPUBs for potential parsing issues."""
import zipfile, re, sys, os

for epub_path in sys.argv[1:]:
    print(f"\n=== {os.path.basename(epub_path)} ===")
    z = zipfile.ZipFile(epub_path)
    
    # Basic stats
    entries = z.namelist()
    htmls = [n for n in entries if n.endswith('.html') or n.endswith('.xhtml') or n.endswith('.htm')]
    imgs = [n for n in entries if any(n.lower().endswith(ext) for ext in ['.jpg','.jpeg','.png','.gif','.svg'])]
    print(f"Total entries: {len(entries)}")
    print(f"HTML/XHTML: {len(htmls)}")
    print(f"Images: {len(imgs)}")
    
    # Container.xml
    try:
        container = z.read('META-INF/container.xml').decode('utf-8')
        m = re.search(r'full-path="([^"]+)"', container)
        opf_path = m.group(1) if m else "NOT FOUND"
        print(f"OPF path: {opf_path}")
    except:
        print("ERROR: No container.xml")
        continue
    
    # Parse OPF
    opf = z.read(opf_path).decode('utf-8')
    opf_dir = '/'.join(opf_path.split('/')[:-1])
    if opf_dir: opf_dir += '/'
    
    # Language
    lang_m = re.search(r'<dc:language[^>]*>([^<]+)</dc:language>', opf)
    if lang_m: print(f"Language: {lang_m.group(1)}")
    
    # Manifest
    manifest = {}
    for m in re.finditer(r'<item\s+([^>]+?)/?>', opf):
        attrs = m.group(1)
        id_m = re.search(r'id="([^"]+)"', attrs)
        href_m = re.search(r'href="([^"]+)"', attrs)
        mt_m = re.search(r'media-type="([^"]+)"', attrs)
        if id_m and href_m:
            manifest[id_m.group(1)] = {
                'href': href_m.group(1),
                'media_type': mt_m.group(1) if mt_m else ''
            }
    
    # Spine
    spine_ids = re.findall(r'idref="([^"]+)"', opf[opf.index('<spine'):opf.index('</spine>')])
    print(f"Spine chapters: {len(spine_ids)}")
    
    # Check first 5 and last 3 chapters
    print("\nFirst 5 spine chapters:")
    for i, sid in enumerate(spine_ids[:5]):
        item = manifest.get(sid, {})
        href = item.get('href', '?')
        full = opf_dir + href
        # Check if exists in ZIP
        exists = full in entries
        size = z.getinfo(full).file_size if exists else -1
        # Read content to check
        if exists:
            content = z.read(full).decode('utf-8', errors='replace')
            text = re.sub(r'<[^>]+>', '', content).strip()
            text = re.sub(r'\s+', ' ', text)
            print(f"  {i}: {full} ({size}B) text={len(text)} chars: {text[:80]}...")
        else:
            print(f"  {i}: {full} NOT FOUND IN ZIP")
    
    # Check for special chars in filenames
    problem_files = [n for n in entries if ',' in n or '%' in n or '#' in n]
    if problem_files:
        print(f"\nFiles with special chars ({len(problem_files)}):")
        for pf in problem_files[:5]:
            print(f"  {pf}")
    
    # Check for large chapters that might cause memory issues
    big_chapters = []
    for sid in spine_ids:
        item = manifest.get(sid, {})
        href = item.get('href', '?')
        full = opf_dir + href
        if full in entries:
            info = z.getinfo(full)
            if info.file_size > 100000:
                big_chapters.append((full, info.file_size))
    if big_chapters:
        print(f"\nLarge chapters (>100KB):")
        for fn, sz in big_chapters[:10]:
            print(f"  {fn}: {sz} bytes")
    
    # Check total uncompressed size of all HTML chapters
    total_html_size = sum(z.getinfo(n).file_size for n in htmls if n in [e for e in entries])
    print(f"\nTotal HTML content: {total_html_size} bytes ({total_html_size/1024:.0f}KB)")
    
    # Check for nested HTML structure issues
    first_ch = opf_dir + manifest.get(spine_ids[0], {}).get('href', '') if spine_ids else ''
    if first_ch in entries:
        content = z.read(first_ch).decode('utf-8', errors='replace')
        # Check encoding declaration
        enc_m = re.search(r'encoding="([^"]+)"', content[:500])
        if enc_m: print(f"Encoding: {enc_m.group(1)}")
        # Check for DOCTYPE
        if '<!DOCTYPE' in content[:500]: print("Has DOCTYPE")
        # Check if XHTML namespace
        if 'xmlns=' in content[:1000]: print("Has xmlns")
        # Check HTML structure
        if '<body' not in content.lower(): print("WARNING: No <body> tag!")
