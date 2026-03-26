"""Debug script: simulate what the firmware would do when loading The Economist.
Show the first few pages' content to understand what triggers the crash."""
import zipfile, re, sys
import xml.etree.ElementTree as ET

epub_path = 'sd_card/books/The Economist, 2026-02-21.epub'
z = zipfile.ZipFile(epub_path)

# Find OPF
opf_name = None
for n in z.namelist():
    if n.endswith('.opf'):
        opf_name = n
        break

opf_data = z.read(opf_name).decode('utf-8')
root = ET.fromstring(opf_data)
ns = {'opf': 'http://www.idpf.org/2007/opf'}

manifest = {}
for item in root.findall('.//opf:manifest/opf:item', ns):
    manifest[item.get('id')] = item.get('href')

spine = root.findall('.//opf:spine/opf:itemref', ns)
print(f"Spine: {len(spine)} items")
print(f"OPF: {opf_name}")

def strip_html(html_text, base_path=""):
    """Simulate the firmware's htmlStripDirect function."""
    out = []
    i = 0
    in_script = False
    in_head = False
    last_was_nl = False
    last_was_space = False
    
    while i < len(html_text):
        c = html_text[i]
        
        if c == '<':
            tag_end = html_text.find('>', i+1)
            if tag_end < 0:
                break
            tag_content = html_text[i+1:tag_end]
            # Extract tag name
            tag_name = ''
            for ch in tag_content:
                if ch in ' \t\n\r':
                    if tag_name:
                        break
                    continue
                if ch == '/' and not tag_name:
                    tag_name += '/'
                    continue
                tag_name += ch.lower()
            
            # Image tags
            if base_path is not None and tag_name in ('img', 'image'):
                for attr in ['src=', 'href=', 'xlink:href=']:
                    idx = tag_content.lower().find(attr)
                    if idx >= 0:
                        val_start = idx + len(attr)
                        if val_start < len(tag_content):
                            quote = tag_content[val_start]
                            if quote in ('"', "'"):
                                val_start += 1
                                val_end = tag_content.find(quote, val_start)
                                if val_end > val_start:
                                    src = tag_content[val_start:val_end]
                                    out.append(f'\x01{base_path}{src}\x01\n')
                                    last_was_nl = True
                                    break
            
            if tag_name == 'head':
                in_head = True
            elif tag_name == '/head':
                in_head = False
            if tag_name in ('script', 'style'):
                in_script = True
            elif tag_name in ('/script', '/style'):
                in_script = False
            
            if not in_script:
                if tag_name in ('/p', '/div', 'br', 'br/') or tag_name.startswith('/h'):
                    if not last_was_nl and out:
                        out.append('\n')
                        last_was_nl = True
                        last_was_space = True
                if tag_name in ('/li', '/tr'):
                    if not last_was_nl and out:
                        out.append('\n')
                        last_was_nl = True
                        last_was_space = True
                # Style markers
                if tag_name in ('em', 'i'):
                    out.append('\x02')
                elif tag_name in ('/em', '/i'):
                    out.append('\x03')
                elif tag_name in ('strong', 'b'):
                    out.append('\x04')
                elif tag_name in ('/strong', '/b'):
                    out.append('\x05')
            
            i = tag_end + 1
            continue
        
        if in_script or in_head:
            i += 1
            continue
        
        # HTML entities
        if c == '&':
            semicol = html_text.find(';', i+1, i+12)
            if semicol >= 0:
                entity = html_text[i:semicol+1]
                i = semicol + 1
                entity_map = {
                    '&amp;': '&', '&lt;': '<', '&gt;': '>',
                    '&quot;': '"', '&apos;': "'", '&nbsp;': ' ',
                    '&mdash;': '—', '&ndash;': '–',
                }
                if entity in entity_map:
                    out.append(entity_map[entity])
                    last_was_nl = False
                    last_was_space = (entity == '&nbsp;')
                elif entity.startswith('&#'):
                    try:
                        if entity[2] == 'x':
                            code = int(entity[3:-1], 16)
                        else:
                            code = int(entity[2:-1])
                        out.append(chr(code))
                        last_was_nl = False
                    except:
                        pass
                continue
            i += 1
            continue
        
        # Normal characters
        if c in '\n\r \t':
            if not last_was_space and not last_was_nl:
                out.append(' ')
                last_was_space = True
            i += 1
            continue
        
        out.append(c)
        last_was_nl = False
        last_was_space = False
        i += 1
    
    return ''.join(out)

# Build full text from all chapters
full_text = ""
chapter_offsets = []
for si, ref in enumerate(spine):
    idref = ref.get('idref')
    href = manifest.get(idref, None)
    if not href:
        continue
    
    # Find the file in the ZIP
    if href in z.namelist():
        fname = href
    else:
        fname = None
        for n in z.namelist():
            if n.endswith(href.split('/')[-1]):
                fname = n
                break
    
    if not fname:
        print(f"  WARNING: {href} not found in ZIP")
        continue
    
    content = z.read(fname).decode('utf-8', errors='replace')
    
    # Get base path
    last_slash = href.rfind('/')
    base_path = href[:last_slash+1] if last_slash >= 0 else ""
    
    chapter_start = len(full_text)
    stripped = strip_html(content, base_path)
    full_text += stripped
    chapter_offsets.append((si, href, chapter_start, len(stripped)))

print(f"\nTotal stripped text: {len(full_text)} bytes")
print(f"Chapters: {len(chapter_offsets)}")

# Simulate page layout
BYTES_PER_PAGE = 866  # Matches firmware: font size 36

print("\n=== First 10 pages ===")
offset = 0
for page_num in range(10):
    if offset >= len(full_text):
        print(f"\nPage {page_num}: END OF TEXT")
        break
    
    page_end = min(offset + BYTES_PER_PAGE, len(full_text))
    page_content = full_text[offset:page_end]
    
    # Check for image markers
    has_image = '\x01' in page_content
    img_markers = []
    idx = 0
    while idx < len(page_content):
        if page_content[idx] == '\x01':
            end_idx = page_content.find('\x01', idx + 1)
            if end_idx > idx:
                img_path = page_content[idx+1:end_idx]
                img_markers.append(img_path)
                idx = end_idx + 1
                continue
        idx += 1
    
    # Count style markers
    style_count = sum(1 for c in page_content if c in '\x02\x03\x04\x05\x06\x07')
    
    # Show printable preview
    preview = ''
    for c in page_content[:200]:
        if c == '\x01':
            preview += '[IMG_MARKER]'
        elif c == '\x02':
            preview += '[ITALIC_ON]'
        elif c == '\x03':
            preview += '[ITALIC_OFF]'
        elif c == '\x04':
            preview += '[BOLD_ON]'
        elif c == '\x05':
            preview += '[BOLD_OFF]'
        elif ord(c) < 0x20 and c != '\n':
            preview += f'[\\x{ord(c):02x}]'
        else:
            preview += c
    
    # Find which chapter this page is in
    chapter_info = "?"
    for ci, ch_href, ch_start, ch_len in chapter_offsets:
        if ch_start <= offset < ch_start + ch_len:
            chapter_info = f"ch{ci}({ch_href})"
            break
    
    print(f"\nPage {page_num}: offset={offset}, len={len(page_content)}, "
          f"images={len(img_markers)}, styles={style_count}")
    print(f"  Chapter: {chapter_info}")
    if img_markers:
        for imp in img_markers:
            print(f"  Image: {imp}")
    print(f"  Preview: {preview[:150]}")
    
    # Simulate what the renderer would do
    if img_markers:
        # First image takes the whole page
        first_img_start = page_content.find('\x01')
        first_img_end = page_content.find('\x01', first_img_start + 1)
        if first_img_end > first_img_start:
            # renderStopByte = position after closing marker
            render_stop = first_img_end + 1
            # Skip newline after marker
            if render_stop < len(page_content) and page_content[render_stop] == '\n':
                render_stop += 1
            next_offset = offset + render_stop
            print(f"  => Image page, render_stop={render_stop}, next offset={next_offset}")
            offset = next_offset
            continue
    
    # Text page: advance by full page
    offset = page_end

print(f"\n=== Done. Total text: {len(full_text)} bytes ===")
