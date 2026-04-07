#!/usr/bin/env python3
"""Inspect EPUB image structure for debugging."""
import zipfile, re, sys, os

epub_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), '..', 'assets', 'books',
    '丰子恺漫画精品集(修订版)(丰子恺[丰子恺]).epub')

zf = zipfile.ZipFile(epub_path)
names = zf.namelist()
print(f"Total ZIP entries: {len(names)}")

html_files = [n for n in names if n.endswith(('.xhtml', '.html', '.htm'))]
img_files = [n for n in names if any(n.lower().endswith(e) for e in ('.jpg','.jpeg','.png','.gif','.bmp','.svg'))]
print(f"HTML files: {len(html_files)}")
print(f"Image files: {len(img_files)}")

# Show OPF base path
opf_files = [n for n in names if n.endswith('.opf')]
if opf_files:
    opf = opf_files[0]
    opf_dir = os.path.dirname(opf) + '/' if '/' in opf else ''
    print(f"OPF: {opf} -> basePath: '{opf_dir}'")

print("\n--- HTML files with image references ---")
for h in html_files[:10]:
    content = zf.read(h).decode('utf-8', errors='replace')
    imgs = re.findall(r'<img[^>]+>', content, re.IGNORECASE)
    svgs = re.findall(r'<image[^>]+>', content, re.IGNORECASE)
    if imgs or svgs:
        print(f"\n  {h}")
        html_dir = os.path.dirname(h) + '/' if '/' in h else ''
        print(f"  (html_dir: '{html_dir}')")
        for tag in (imgs + svgs)[:3]:
            # Extract src/href
            m = re.search(r'(?:src|href|xlink:href)\s*=\s*["\']([^"\']+)', tag, re.IGNORECASE)
            if m:
                ref = m.group(1)
                full = html_dir + ref
                normalized = os.path.normpath(full)
                in_zip = normalized in names
                print(f"    ref='{ref}' -> full='{full}' -> normalized='{normalized}' -> in_zip={in_zip}")
            else:
                print(f"    TAG: {tag[:200]}")
    elif not imgs and not svgs:
        # check for any img-like references
        if 'cover' in h.lower() or html_files.index(h) < 3:
            print(f"\n  {h} (no img/image tags)")
            print(f"    Preview: {content[:200]}")
