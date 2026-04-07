#!/usr/bin/env python3
"""Simulate the C++ epub reader's parsing of an EPUB to find failure points."""
import struct, zlib, os, re

def simulate_cpp_zip_read(filepath):
    with open(filepath, 'rb') as f:
        file_size = os.path.getsize(filepath)
        search_len = min(file_size, 65557)
        search_from = file_size - search_len
        f.seek(search_from)
        data = f.read(search_len)
        eocd_pos = None
        for i in range(len(data) - 4, -1, -1):
            if data[i:i+4] == b'\x50\x4b\x05\x06':
                eocd_pos = search_from + i
                break
        if eocd_pos is None:
            print("  EOCD not found!")
            return None
        f.seek(eocd_pos + 4)
        eocd = f.read(18)
        num_entries = struct.unpack_from('<H', eocd, 4)[0]
        cd_offset = struct.unpack_from('<I', eocd, 12)[0]
        print(f"  EOCD: {num_entries} entries, CD offset={cd_offset}")
        f.seek(cd_offset)
        entries = []
        for i in range(num_entries):
            hdr = f.read(46)
            if len(hdr) < 46 or hdr[:4] != b'\x50\x4b\x01\x02':
                print(f"  ERROR at entry {i}")
                break
            method = struct.unpack_from('<H', hdr, 10)[0]
            comp_size = struct.unpack_from('<I', hdr, 20)[0]
            uncomp_size = struct.unpack_from('<I', hdr, 24)[0]
            name_len = struct.unpack_from('<H', hdr, 28)[0]
            extra_len = struct.unpack_from('<H', hdr, 30)[0]
            comment_len = struct.unpack_from('<H', hdr, 32)[0]
            local_off = struct.unpack_from('<I', hdr, 42)[0]
            name = f.read(name_len)[:255].decode('utf-8', errors='replace')
            f.seek(f.tell() + extra_len + comment_len)
            entries.append({'filename': name, 'method': method, 'comp_size': comp_size,
                          'uncomp_size': uncomp_size, 'local_offset': local_off})
        return entries

def simulate_extract(filepath, entry):
    with open(filepath, 'rb') as f:
        f.seek(entry['local_offset'])
        lhdr = f.read(30)
        if lhdr[:4] != b'\x50\x4b\x03\x04':
            return None, "Bad local header"
        name_len = struct.unpack_from('<H', lhdr, 26)[0]
        extra_len = struct.unpack_from('<H', lhdr, 28)[0]
        f.seek(entry['local_offset'] + 30 + name_len + extra_len)
        if entry['method'] == 0:
            return f.read(entry['uncomp_size']), None
        elif entry['method'] == 8:
            comp_data = f.read(entry['comp_size'])
            try:
                return zlib.decompress(comp_data, -15), None
            except zlib.error as e:
                return None, f"Decompression: {e}"
        return None, f"Method {entry['method']}"

def simulate_epub_load(filepath):
    print(f"\n=== {os.path.basename(filepath)} ===")
    entries = simulate_cpp_zip_read(filepath)
    if not entries:
        return
    print(f"  ZIP: {len(entries)} entries")
    opf_path = ""
    for e in entries:
        if e['filename'] == 'META-INF/container.xml':
            data, err = simulate_extract(filepath, e)
            if err:
                print(f"  container.xml ERROR: {err}")
                return
            xml = data.decode('utf-8')
            idx = xml.find('full-path="')
            if idx >= 0:
                end = xml.find('"', idx + 11)
                opf_path = xml[idx+11:end]
            break
    if not opf_path:
        for e in entries:
            if e['filename'].endswith('.opf'):
                opf_path = e['filename']
                break
    slash = opf_path.rfind('/')
    base_path = opf_path[:slash+1] if slash >= 0 else ""
    print(f"  OPF: '{opf_path}', base: '{base_path}'")
    opf_content = ""
    for e in entries:
        if e['filename'] == opf_path:
            data, err = simulate_extract(filepath, e)
            if err:
                print(f"  OPF extract ERROR: {err}")
                return
            opf_content = data.decode('utf-8')
            break
    if not opf_content:
        print("  Empty OPF!")
        return
    manifest = {}
    for m in re.finditer(r'<item\s+([^>]*?)/>', opf_content):
        attrs = m.group(1)
        id_m = re.search(r'id="([^"]*)"', attrs)
        href_m = re.search(r'href="([^"]*)"', attrs)
        mt_m = re.search(r'media-type="([^"]*)"', attrs)
        if id_m and href_m and mt_m:
            manifest[id_m.group(1)] = {'href': href_m.group(1), 'is_html': 'html' in mt_m.group(1)}
    spine_match = re.search(r'<spine[^>]*>(.*?)</spine>', opf_content, re.DOTALL)
    spine_refs = re.findall(r'idref="([^"]*)"', spine_match.group(1)) if spine_match else []
    print(f"  Manifest: {len(manifest)}, Spine: {len(spine_refs)}")
    chapters = []
    for s in spine_refs:
        if s in manifest and manifest[s]['is_html']:
            fp = base_path + manifest[s]['href']
            for i, e in enumerate(entries):
                if e['filename'] == fp:
                    chapters.append({'idx': i, 'path': fp})
                    break
            else:
                print(f"  MISSING: '{s}' -> '{fp}'")
    print(f"  Chapters: {len(chapters)}")
    if not chapters:
        print("  *** 0 chapters - LOAD WOULD FAIL ***")
        return
    failures = 0
    for i, ch in enumerate(chapters):
        e = entries[ch['idx']]
        data, err = simulate_extract(filepath, e)
        if err:
            print(f"    Ch {i+1} FAILED: {ch['path']} - {err}")
            failures += 1
        elif i < 3:
            text = data.decode('utf-8', errors='replace')
            stripped = re.sub(r'<[^>]+>', '', text).strip()
            print(f"    Ch {i+1}: {ch['path']} - raw={len(data)}, text~{len(stripped.encode('utf-8'))}")
    print(f"  Result: {len(chapters)} chapters, {failures} failures")

base = 'sd_card/books/'
simulate_epub_load(base + 'Chu Liu Xiang Chuan Qi .Hua Mei Niao - Gu Long.epub')
simulate_epub_load(base + 'Chu Liu Xiang Chuan Qi .Xie Hai Piao Xiang - Gu Long.epub')
