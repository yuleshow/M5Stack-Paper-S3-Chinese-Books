#!/usr/bin/env python3
"""
Simulate the exact device epubGenerateVirtualToc() algorithm.

Replicates the C++ logic from epub_reader.cpp byte-for-byte:
  1. Extract HTML chapters from EPUB ZIP (spine order)
  2. Strip HTML tags (simplified htmlStripDirect)
  3. Concatenate with EPUB_CHAPTER_BREAK (\x0E) between chapters
  4. Scan for 第X回/章/節/篇/卷 patterns with all the same filters:
     - Line-start check (skip mid-line 第)
     - Previous-line mid-sentence punctuation check
     - Paired punctuation check (avoid （第一回）)
     - Post-terminator whitespace requirement
     - Label extraction with punctuation-based trimming
     - Next-line merge for "第一回\\n靈根育孕源流出..."
     - Space insertion after terminator
     - Duplicate detection

Usage: python3 _simulate_device_toc.py [epub_path]
  Default: sd_card/books/cn/pg24113-images-3.epub
"""

import sys, os, re, zipfile, html as html_mod

EPUB_CHAPTER_BREAK = '\x0E'

# --- Punctuation tables (from utf8_utils.h) ---
OPENING_PUNCT = set('\u201C\u2018\uFF08\u300C\u300E\u300A\u3008\u3010\u3014\u3016([{')
CLOSING_PUNCT = set('\u201D\u2019\uFF09\u300D\u300F\u300B\u3009\u3011\u3015\u3017)]}')
PUNCTUATION = set(
    '\u3002\uFF0C\u3001\uFF1B\uFF1A\uFF01\uFF1F\u30FB\u2026\u2025\u2014\u2013'  # CJK sentence
    '\u300C\u300D\u300E\u300F\u300A\u300B\u3008\u3009\u3010\u3011\u3014\u3015\u3016\u3017'
    '\uFF08\uFF09\uFF5B\uFF5D\uFF3B\uFF3D\u201C\u201D\u2018\u2019'
    ',.!?:;)]}"\'{(['
)
# Add vertical presentation forms FE17-FE48
for cp in range(0xFE17, 0xFE49):
    PUNCTUATION.add(chr(cp))

CJK_NUMS = set('一二三四五六七八九十百千零〇')
TERMINATORS = set('回章節篇卷')
MID_SENTENCE_PUNCT = {'\uFF0C', ',', '\uFF1B', ';'}  # ，,；;

def is_digit(ch):
    return ('0' <= ch <= '9') or ('\uFF10' <= ch <= '\uFF19')

def is_cjk_num(ch):
    return ch in CJK_NUMS

def is_num(ch):
    return is_cjk_num(ch) or is_digit(ch)

def parse_cjk_number(label):
    """Parse 第X回/章/節/篇/卷 or 卷X → numeric value of X."""
    idx = label.find('第')
    if idx >= 0:
        idx += 1  # skip 第
    elif label.startswith('卷'):
        idx = 1  # skip 卷, parse number after it
    elif label == '序':
        return 0
    else:
        return 99999
    value = 0
    found = False
    while idx < len(label):
        ch = label[idx]
        idx += 1
        digit = -1
        if ch in '零〇': digit = 0
        elif ch == '一': digit = 1
        elif ch == '二': digit = 2
        elif ch == '三': digit = 3
        elif ch == '四': digit = 4
        elif ch == '五': digit = 5
        elif ch == '六': digit = 6
        elif ch == '七': digit = 7
        elif ch == '八': digit = 8
        elif ch == '九': digit = 9
        elif ch == '十':
            if not found: value = 10; found = True; continue
            else: value = value * 10; continue
        elif ch == '百':
            if value == 0: value = 1
            value *= 100; continue
        elif ch == '千':
            if value == 0: value = 1
            value *= 1000; continue
        elif '0' <= ch <= '9': digit = ord(ch) - ord('0')
        elif '\uFF10' <= ch <= '\uFF19': digit = ord(ch) - 0xFF10
        else: break  # terminator or unknown
        if digit >= 0:
            found = True
            if ch == '〇': value = value * 10 + digit
            elif value > 0 and value % 10 == 0 and digit > 0: value += digit
            elif value == 0: value = digit
            else: value = value * 10 + digit
    return value if found else 99999

# --- HTML stripping (simplified htmlStripDirect) ---
def strip_html(raw_html):
    """Strip HTML tags, decode entities, emit newlines for block elements."""
    out = []
    i = 0
    in_script = False
    in_head = False
    last_was_newline = False
    last_was_space = False

    while i < len(raw_html):
        c = raw_html[i]
        if c == '<':
            tag_end = raw_html.find('>', i + 1)
            if tag_end < 0:
                break
            # Extract tag name
            tag_content = raw_html[i+1:tag_end]
            tag_name = ''
            for tc in tag_content:
                if tc in ' \t\n\r':
                    if tag_name:
                        break
                    continue
                if tc == '/' and not tag_name:
                    tag_name += '/'
                    continue
                tag_name += tc.lower()
            # Track head/script
            if tag_name == 'head': in_head = True
            elif tag_name == '/head': in_head = False
            if tag_name in ('script', 'style'): in_script = True
            elif tag_name in ('/script', '/style'): in_script = False
            # Block elements → newline
            if not in_script:
                if tag_name in ('/p', '/div', 'br', 'br/', '/li', '/tr') or \
                   (tag_name.startswith('/h') and len(tag_name) <= 3):
                    if not last_was_newline and out:
                        out.append('\n')
                        last_was_newline = True
                        last_was_space = False
            i = tag_end + 1
            continue

        if in_script or in_head:
            i += 1
            continue

        # HTML entities
        if c == '&':
            semi = raw_html.find(';', i + 1, i + 12)
            if semi >= 0:
                entity = raw_html[i:semi+1]
                decoded = html_mod.unescape(entity)
                # &nbsp; decodes to \xa0 — treat as space like the C++ code
                if decoded == '\xa0':
                    if not last_was_space and not last_was_newline and out:
                        out.append(' ')
                        last_was_space = True
                else:
                    out.append(decoded)
                    last_was_newline = False
                    last_was_space = False
                i = semi + 1
                continue

        # Collapse whitespace (space, tab, \n, \r, \xa0) like the C++ code does
        if c in ' \t\n\r\xa0':
            if not last_was_space and not last_was_newline and out:
                out.append(' ')
                last_was_space = True
            i += 1
            continue

        out.append(c)
        last_was_newline = False
        last_was_space = False
        i += 1

    return ''.join(out)


def load_epub_text(epub_path):
    """Load and concatenate chapter text from EPUB, replicating device behavior."""
    with zipfile.ZipFile(epub_path) as z:
        names = z.namelist()

        # Find OPF
        opf_files = [n for n in names if n.endswith('.opf')]
        if not opf_files:
            print("ERROR: No OPF file found")
            return None, []

        opf_content = z.read(opf_files[0]).decode('utf-8', errors='replace')
        opf_dir = os.path.dirname(opf_files[0])
        if opf_dir:
            opf_dir += '/'

        # Build manifest: id → href
        manifest = {}
        for m in re.finditer(r'<item\b[^>]*>', opf_content):
            tag = m.group(0)
            id_m = re.search(r'id=["\']([^"\']*)["\']', tag)
            href_m = re.search(r'href=["\']([^"\']*)["\']', tag)
            mt_m = re.search(r'media-type=["\']([^"\']*)["\']', tag)
            if id_m and href_m and mt_m and 'xhtml' in mt_m.group(1).lower():
                manifest[id_m.group(1)] = href_m.group(1)

        # Get spine order
        spine_ids = re.findall(r'<itemref\b[^>]*idref=["\']([^"\']*)["\']', opf_content)
        spine_files = []
        for sid in spine_ids:
            if sid in manifest:
                path = opf_dir + manifest[sid]
                # Skip toc/nav files
                fn = os.path.basename(path).lower()
                if 'toc' in fn or 'nav' in fn:
                    continue
                spine_files.append(path)

        print(f"Spine: {len(spine_files)} chapter files")
        for sf in spine_files:
            print(f"  {sf}")

        # Extract and strip HTML, concatenate with chapter breaks
        full_text = ''
        chapter_info = []  # (filename, cumulative_offset)
        for i, sf in enumerate(spine_files):
            try:
                raw = z.read(sf).decode('utf-8', errors='replace')
            except KeyError:
                print(f"  WARNING: {sf} not found in ZIP")
                continue
            text = strip_html(raw)
            if i > 0:
                full_text += EPUB_CHAPTER_BREAK
            chapter_info.append((sf, len(full_text)))
            full_text += text

        print(f"\nTotal text: {len(full_text)} chars, {len(full_text.encode('utf-8'))} bytes")
        return full_text, chapter_info


def extract_label(text, heading_start, num_pos):
    """Extract chapter heading label from text at the given position."""
    label_end = num_pos

    # Check if rest of line after terminator is only whitespace → merge next line
    peek_pos = label_end
    rest_is_empty = True
    while peek_pos < len(text):
        pc = text[peek_pos]
        if pc in '\n\r' or pc == EPUB_CHAPTER_BREAK:
            break
        if pc not in ' \t\u3000':
            rest_is_empty = False
            break
        peek_pos += 1
    if rest_is_empty and peek_pos < len(text):
        if text[peek_pos] == '\r':
            peek_pos += 1
        if peek_pos < len(text) and text[peek_pos] == '\n':
            peek_pos += 1
        # Allow merging across chapter breaks
        if peek_pos < len(text) and text[peek_pos] == EPUB_CHAPTER_BREAK:
            peek_pos += 1
        if peek_pos < len(text):
            label_end = peek_pos

    # Skip colon right after terminator (e.g., 第一回：title → 第一回 title)
    if label_end < len(text) and text[label_end] in ':\uFF1A':
        label_end += 1

    # Scan line: title ends at line ending, or at last space before
    # first punctuation (or at punctuation itself if no space precedes it)
    char_count = 0
    first_punct = -1
    last_space_before_punct = -1
    scan_pos = label_end
    while scan_pos < len(text) and char_count < 60:
        ch = text[scan_pos]
        if ch in '\n\r' or ch == EPUB_CHAPTER_BREAK:
            break
        if ch in PUNCTUATION:
            if first_punct < 0:
                first_punct = scan_pos
        if first_punct < 0 and ch in ' \u3000':
            last_space_before_punct = scan_pos
        scan_pos += 1
        char_count += 1

    if first_punct < 0:
        title_end = scan_pos
    elif last_space_before_punct > num_pos:
        title_end = last_space_before_punct
    else:
        title_end = first_punct

    label = text[heading_start:title_end]
    label = label.replace('\r\n', ' ').replace('\r', ' ').replace('\n', ' ')
    label = label.replace(EPUB_CHAPTER_BREAK, ' ')
    # Replace fullwidth spaces and colons with ASCII space, then collapse runs
    label = label.replace('\u3000', ' ')
    label = label.replace('\uFF1A', ' ')  # fullwidth colon
    label = label.replace(':', ' ')       # ASCII colon
    import re as _re
    label = _re.sub(r'  +', ' ', label).strip()

    # Insert space after terminator (or after number for 卷X pattern) if none exists
    for ti, tc in enumerate(label):
        if tc in TERMINATORS:
            insert_pos = ti + 1
            # For 卷X pattern: 卷 followed by number → skip past numbers
            if tc == '卷' and insert_pos < len(label) and is_num(label[insert_pos]):
                while insert_pos < len(label) and is_num(label[insert_pos]):
                    insert_pos += 1
            if insert_pos < len(label) and label[insert_pos] not in ' \u3000':
                label = label[:insert_pos] + ' ' + label[insert_pos:]
            break

    return label


def generate_virtual_toc(text):
    """Replicate epubGenerateVirtualToc() logic exactly."""
    entries = []
    MAX_TOC = 512

    i = 0
    while i < len(text) and len(entries) < MAX_TOC:
        # Find next 第
        idx = text.find('第', i)
        if idx < 0:
            break

        heading_start = idx
        pos_after_di = idx + 1  # position after 第
        at_true_line_start = (heading_start == 0)  # start of text

        # --- Line-start check ---
        if heading_start > 0:
            at_line_start = False
            scan_back = heading_start - 1
            while scan_back >= 0:
                ch = text[scan_back]
                if ch in '\n\r' or ch == EPUB_CHAPTER_BREAK:
                    at_line_start = True
                    at_true_line_start = True
                    break
                if ch in ' \t\u3000':
                    scan_back -= 1
                    continue
                break  # Non-whitespace → mid-line
            if scan_back < 0:
                at_line_start = True
                at_true_line_start = True

            # If not at line start, check if preceded by sentence-ending punct
            # (。！？) + optional whitespace — common in EPUBs where chapter
            # headings are in the same <p> as the previous chapter's last line.
            SENTENCE_END_PUNCT = set('\u3002\uFF01\uFF1F\u2014')  # 。！？—
            if not at_line_start and heading_start > 0:
                # scan_back stopped at a non-whitespace char
                if text[scan_back] in SENTENCE_END_PUNCT:
                    at_line_start = True

            # If still not at line start, check if 第X[terminator] ends the line.
            # Catches headings embedded at end of a paragraph, e.g.:
            # "...撰於萬卷樓 第一卷\n" where 第一卷 is in same <p> as preface.
            if not at_line_start and heading_start > 0:
                peek_end = pos_after_di
                # Skip numbers
                while peek_end < len(text) and is_num(text[peek_end]):
                    peek_end += 1
                # Check terminator
                if peek_end < len(text) and text[peek_end] in TERMINATORS:
                    peek_end += 1
                    # Check if only whitespace follows until newline/EOF/break
                    line_ends = True
                    while peek_end < len(text):
                        c = text[peek_end]
                        if c in '\n\r' or c == EPUB_CHAPTER_BREAK:
                            break
                        if c in ' \t\u3000':
                            peek_end += 1
                            continue
                        line_ends = False
                        break
                    if line_ends:
                        at_line_start = True
                        at_true_line_start = True

            if not at_line_start:
                i = idx + 1
                continue

            # Previous-line mid-sentence punctuation check
            if at_line_start and scan_back > 0:
                prev_pos = scan_back - 1
                while prev_pos >= 0 and text[prev_pos] in '\n\r \t':
                    prev_pos -= 1
                if prev_pos >= 0:
                    prev_ch = text[prev_pos]
                    if prev_ch in MID_SENTENCE_PUNCT:
                        i = idx + 1
                        continue

            # Paired punctuation check
            # Peek forward past number+terminator to see if closing punct follows
            peek = pos_after_di
            while peek < len(text) and is_num(text[peek]):
                peek += 1
            if peek < len(text) and text[peek] in TERMINATORS:
                peek += 1  # skip terminator
            has_closing = peek < len(text) and text[peek] in CLOSING_PUNCT

            if has_closing:
                # Scan backward from heading_start across ≤1 newline for opening punct
                p_scan = heading_start
                newlines_seen = 0
                p_limit = max(0, heading_start - 200)
                found_opening = False
                while p_scan > p_limit:
                    prev = p_scan - 1
                    pc = text[prev]
                    if pc == EPUB_CHAPTER_BREAK:
                        break
                    if pc in '\n\r':
                        newlines_seen += 1
                        if newlines_seen > 1:
                            break
                    if pc in OPENING_PUNCT:
                        found_opening = True
                        break
                    p_scan = prev
                if found_opening:
                    i = idx + 1
                    continue

        # --- Scan number part ---
        num_pos = pos_after_di
        has_number = False
        terminator = None
        while num_pos < len(text):
            ch = text[num_pos]
            if is_num(ch):
                has_number = True
                num_pos += 1
            elif has_number and ch in TERMINATORS:
                terminator = ch
                num_pos += 1  # past terminator
                break
            else:
                break

        if not has_number or not terminator:
            i = idx + 1
            continue

        # --- Post-terminator whitespace check ---
        # Skip for true line starts (e.g. "第一卷轉運漢" — no space after 卷)
        if not at_true_line_start and num_pos < len(text):
            after_ch = text[num_pos]
            if after_ch not in ' \t\u3000\n\r' and after_ch != EPUB_CHAPTER_BREAK:
                i = idx + 1
                continue

        # --- Extract label ---
        label = extract_label(text, heading_start, num_pos)

        # Duplicate check
        if label and not any(e[0] == label for e in entries):
            entries.append((label, heading_start))

        i = num_pos  # continue scanning after this match

    # === Second scan: 卷X pattern (terminator before number) ===
    # Some books use 卷一, 卷二, ... instead of 第一卷, 第二卷, ...
    # Only for 卷 — other terminators don't use this reverse pattern.
    i = 0
    while i < len(text) and len(entries) < MAX_TOC:
        idx = text.find('卷', i)
        if idx < 0:
            break

        heading_start = idx

        # Check that 卷 is followed by a CJK/Arabic number
        num_pos = idx + 1
        has_number = False
        while num_pos < len(text) and is_num(text[num_pos]):
            has_number = True
            num_pos += 1
        if not has_number:
            i = idx + 1
            continue

        # After the number, require whitespace/newline/EOF
        if num_pos < len(text):
            after_ch = text[num_pos]
            if after_ch not in ' \t\u3000\n\r' and after_ch != EPUB_CHAPTER_BREAK:
                i = idx + 1
                continue

        # Line-start check
        at_line_start = (heading_start == 0)
        if heading_start > 0:
            scan_back = heading_start - 1
            while scan_back >= 0:
                ch = text[scan_back]
                if ch in '\n\r' or ch == EPUB_CHAPTER_BREAK:
                    at_line_start = True
                    break
                if ch in ' \t\u3000':
                    scan_back -= 1
                    continue
                break
            if scan_back < 0:
                at_line_start = True

            # Check if preceded by sentence-ending punct
            SENTENCE_END_PUNCT = set('\u3002\uFF01\uFF1F\u2014')
            if not at_line_start and heading_start > 0:
                if text[scan_back] in SENTENCE_END_PUNCT:
                    at_line_start = True

            if not at_line_start:
                i = idx + 1
                continue

            # Mid-sentence punctuation rejection
            if scan_back > 0:
                prev_pos = scan_back - 1
                while prev_pos >= 0 and text[prev_pos] in '\n\r \t':
                    prev_pos -= 1
                if prev_pos >= 0:
                    if text[prev_pos] in MID_SENTENCE_PUNCT:
                        i = idx + 1
                        continue

        # Extract label
        label = extract_label(text, heading_start, num_pos)
        if label and not any(e[0] == label for e in entries):
            entries.append((label, heading_start))

        i = num_pos

    # === Standalone 序 scan ===
    # Only if we found numbered chapters (structured book)
    if entries and len(entries) < MAX_TOC:
        import re as _re
        for m in _re.finditer(r'\n[ \t\u3000]*序[ \t\u3000]*\n', text):
            # Find the 序 character position (skip leading whitespace after \n)
            seq_start = m.start() + 1
            while seq_start < m.end() and text[seq_start] in ' \t\u3000':
                seq_start += 1
            if not any(e[0] == '序' for e in entries):
                entries.append(('序', seq_start))
            break  # Only first standalone 序

    # Sort by CJK number
    entries.sort(key=lambda e: parse_cjk_number(e[0]))

    # === Gap-filling second pass ===
    # If more than half of expected sequential entries were found,
    # search for missing numbers with relaxed position rules.
    if len(entries) >= 3:
        # Find dominant terminator
        term_counts = {}
        for label, _ in entries:
            for ch in label:
                if ch in TERMINATORS:
                    term_counts[ch] = term_counts.get(ch, 0) + 1
                    break
        dom_term = max(term_counts, key=term_counts.get) if term_counts else None

        if dom_term:
            # Parse found numbers
            found_nums = set()
            max_num = 0
            for label, _ in entries:
                n = parse_cjk_number(label)
                if 0 < n <= 200:
                    found_nums.add(n)
                    if n > max_num:
                        max_num = n

            found_count = len(found_nums)
            if max_num > found_count and found_count > max_num // 2:
                # Search for missing entries with relaxed rules
                i = 0
                while i < len(text) and len(entries) < MAX_TOC:
                    idx = text.find('第', i)
                    if idx < 0:
                        break
                    heading_start = idx
                    pos_after_di = idx + 1

                    # Parse number + terminator (must match dominant)
                    num_pos = pos_after_di
                    has_number = False
                    terminator = None
                    while num_pos < len(text):
                        ch = text[num_pos]
                        if is_num(ch):
                            has_number = True
                            num_pos += 1
                        elif has_number and ch in TERMINATORS:
                            terminator = ch
                            num_pos += 1
                            break
                        else:
                            break
                    if not has_number or terminator != dom_term:
                        i = idx + 1
                        continue

                    # Check if this number fills a gap
                    tmp_label = text[heading_start:num_pos]
                    num_value = parse_cjk_number(tmp_label)
                    if num_value <= 0 or num_value > max_num or num_value in found_nums:
                        i = idx + 1
                        continue

                    # Paired punct check — reject closing punct right after terminator
                    peek = num_pos
                    while peek < len(text) and text[peek] in ' \t\u3000':
                        peek += 1
                    if peek < len(text) and text[peek] in CLOSING_PUNCT:
                        i = idx + 1
                        continue

                    # Post-terminator whitespace check (always required)
                    if num_pos < len(text):
                        after_ch = text[num_pos]
                        if after_ch not in ' \t\u3000\n\r' and after_ch != EPUB_CHAPTER_BREAK:
                            i = idx + 1
                            continue

                    # Extract label and insert
                    label = extract_label(text, heading_start, num_pos)
                    if label and not any(e[0] == label for e in entries):
                        entries.append((label, heading_start))
                        found_nums.add(num_value)

                    i = num_pos

                # Re-sort after gap-filling
                entries.sort(key=lambda e: parse_cjk_number(e[0]))

    return entries


def main():
    # Default to pg24113
    if len(sys.argv) > 1:
        epub_path = sys.argv[1]
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        epub_path = os.path.join(script_dir, '..', 'sd_card', 'books', 'cn', 'pg24113-images-3.epub')

    if not os.path.exists(epub_path):
        print(f"File not found: {epub_path}")
        sys.exit(1)

    is_txt = epub_path.lower().endswith('.txt')

    if is_txt:
        print(f"TXT: {os.path.basename(epub_path)}")
        print("=" * 60)
        with open(epub_path, 'r', encoding='utf-8', errors='replace') as f:
            full_text = f.read()
        chapter_info = []
        print(f"Total text: {len(full_text)} chars, {len(full_text.encode('utf-8'))} bytes")
    else:
        print(f"EPUB: {os.path.basename(epub_path)}")
        print("=" * 60)
        full_text, chapter_info = load_epub_text(epub_path)
        if not full_text:
            print("Failed to load EPUB text")
            sys.exit(1)

    print("\n" + "=" * 60)
    print("Virtual TOC (device algorithm)")
    print("=" * 60)

    entries = generate_virtual_toc(full_text)

    for i, (label, offset) in enumerate(entries):
        if is_txt:
            print(f"  [{i+1:2d}] {label}")
        else:
            # Find which chapter file
            ch_file = '?'
            for sf, co in reversed(chapter_info):
                if offset >= co:
                    ch_file = os.path.basename(sf)
                    break
            print(f"  [{i+1:2d}] {label}")
            print(f"       offset={offset}  file={ch_file}")

    print(f"\nTotal: {len(entries)} entries")


if __name__ == '__main__':
    main()
