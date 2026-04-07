#!/usr/bin/env python3
"""Study pg*-images EPUBs to understand cover image + first chapter structure."""
import zipfile, re, os, glob

books_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), "sd_card", "books")
epubs = sorted(glob.glob(os.path.join(books_dir, "pg*-images*.epub")))

for epub_path in epubs:
    epub = os.path.basename(epub_path)
    try:
        z = zipfile.ZipFile(epub_path)
        container = z.read('META-INF/container.xml').decode('utf-8')
        opf_match = re.search(r'full-path="([^"]+)"', container)
        opf_path = opf_match.group(1) if opf_match else None
        if not opf_path:
            continue
        opf = z.read(opf_path).decode('utf-8')
        
        spine_section = opf[opf.index('<spine'):opf.index('</spine>')]
        spine_refs = re.findall(r'idref="([^"]+)"', spine_section)
        
        manifest = {}
        for m in re.finditer(r'<item\s+([^>]+)/?\s*>', opf):
            attrs = m.group(1)
            id_m = re.search(r'id="([^"]+)"', attrs)
            href_m = re.search(r'href="([^"]+)"', attrs)
            mtype_m = re.search(r'media-type="([^"]+)"', attrs)
            if id_m and href_m:
                manifest[id_m.group(1)] = (href_m.group(1), mtype_m.group(1) if mtype_m else '')
        
        base = opf_path.rsplit('/', 1)[0] + '/' if '/' in opf_path else ''
        
        print(f'\n=== {epub} ===')
        print(f'Spine ({len(spine_refs)} items): {spine_refs[:5]}')
        
        for i, ref in enumerate(spine_refs[:3]):
            if ref in manifest:
                href, mtype = manifest[ref]
                full = base + href
                try:
                    content = z.read(full).decode('utf-8', errors='replace')
                except KeyError:
                    print(f'  Ch{i}: {href} - NOT FOUND IN ZIP')
                    continue
                
                imgs = re.findall(r'src="([^"]+)"', content)
                stripped = re.sub(r'<[^>]+>', '', content)
                stripped = re.sub(r'\s+', ' ', stripped).strip()
                text_len = len(stripped)
                
                print(f'  Ch{i}: {href} ({len(content)}b html, {text_len}b text), imgs={imgs[:3]}')
                if i == 0 and text_len < 500:
                    print(f'    Text: {stripped[:200]}')
        z.close()
    except Exception as e:
        print(f'{epub}: ERROR {e}')
