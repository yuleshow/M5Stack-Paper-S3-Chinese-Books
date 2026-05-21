#!/usr/bin/env python3
"""
Generate a TOC report for all Chinese EPUB books.

For each book the report shows:
  * whether the EPUB ships a real TOC (NCX or EPUB3 nav) and how many entries
    it has — the device uses that directly and never runs the virtual scanner
  * if the real TOC is empty/missing or only has a single root entry, the
    virtual TOC the device would generate, with per-terminator counts
    (回/章/節/篇/卷) and a [!] flag whenever the count is smaller than the
    highest chapter number parsed from labels

Output: output/all_toc_report.txt
"""

import os, re, sys, glob, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _simulate_device_toc import load_epub_text, generate_virtual_toc, parse_cjk_number

TERMINATORS = ['回', '章', '節', '篇', '卷']

# A real TOC with fewer than this many entries is considered degenerate
# (typically just a cover/root navPoint) — we still run the virtual scanner
# to give the reader useful navigation.
USEFUL_TOC_THRESHOLD = 2


def detect_real_toc(epub_path: str):
    try:
        with zipfile.ZipFile(epub_path) as z:
            names = z.namelist()
            opf_files = [n for n in names if n.endswith('.opf')]
            if not opf_files:
                return (None, 0)
            opf = z.read(opf_files[0]).decode('utf-8', errors='replace')
            opf_dir = os.path.dirname(opf_files[0])
            prefix = (opf_dir + '/') if opf_dir else ''

            manifest = {}
            for m in re.finditer(r'<item\b[^>]*>', opf):
                tag = m.group(0)
                id_m = re.search(r'id=["\']([^"\']*)["\']', tag)
                href_m = re.search(r'href=["\']([^"\']*)["\']', tag)
                mt_m = re.search(r'media-type=["\']([^"\']*)["\']', tag)
                props_m = re.search(r'properties=["\']([^"\']*)["\']', tag)
                if id_m and href_m:
                    manifest[id_m.group(1)] = (
                        href_m.group(1),
                        props_m.group(1) if props_m else '',
                        mt_m.group(1) if mt_m else '',
                    )

            ncx_href = None
            spine_m = re.search(r'<spine\b[^>]*toc=["\']([^"\']*)["\']', opf)
            if spine_m:
                tocid = spine_m.group(1)
                if tocid in manifest:
                    ncx_href = manifest[tocid][0]
            if not ncx_href:
                for _id, (href, _props, mt) in manifest.items():
                    if 'x-dtbncx' in mt.lower():
                        ncx_href = href
                        break

            if ncx_href:
                ncx_path = prefix + ncx_href
                if ncx_path in names:
                    ncx = z.read(ncx_path).decode('utf-8', errors='replace')
                    count = len(re.findall(r'<navPoint\b', ncx))
                    if count > 0:
                        return ('NCX', count)

            for _id, (href, props, _mt) in manifest.items():
                if 'nav' in props.split():
                    nav_path = prefix + href
                    if nav_path in names:
                        nav = z.read(nav_path).decode('utf-8', errors='replace')
                        block = re.search(
                            r'<nav[^>]*epub:type=["\']toc["\'][^>]*>(.*?)</nav>',
                            nav, flags=re.S | re.I)
                        if block:
                            count = len(re.findall(r'<a\b', block.group(1)))
                            if count > 0:
                                return ('EPUB3 nav', count)
    except (zipfile.BadZipFile, KeyError, OSError):
        return (None, 0)
    return (None, 0)


CJK_NUM_CHARS = '一二三四五六七八九十百千零〇○'
NUM_CHARS = CJK_NUM_CHARS + '0123456789' + '０１２３４５６７８９'
TERM_CHARS = ''.join(TERMINATORS)


def find_body_start(label: str) -> int:
    """Index into `label` where the title body (after 第<num><term>) begins."""
    if not label.startswith('第'):
        return 0
    i = 1
    while i < len(label) and label[i] in NUM_CHARS:
        i += 1
    # Allow 第X 回 (space between number and terminator)
    j = i
    while j < len(label) and label[j] in ' \t\u3000':
        j += 1
    if j < len(label) and label[j] in TERM_CHARS:
        i = j + 1
    while i < len(label) and label[i] in ' \t\u3000':
        i += 1
    return i


def detect_terminator(label: str) -> str:
    """Return the terminator that immediately follows 第<num>."""
    if label.startswith('卷') and (len(label) < 2 or label[1] not in TERM_CHARS):
        return '卷'
    if not label.startswith('第'):
        return '?'
    i = 1
    while i < len(label) and label[i] in NUM_CHARS:
        i += 1
    while i < len(label) and label[i] in ' \t\u3000':
        i += 1
    if i < len(label) and label[i] in TERM_CHARS:
        return label[i]
    return '?'


def analyze_entries(entries):
    by_term = {t: {'count': 0, 'max': 0, 'nums_seen': set()} for t in TERMINATORS}
    by_term['?'] = {'count': 0, 'max': 0, 'nums_seen': set()}
    for label, _off in entries:
        term = detect_terminator(label)
        num = _parse_num_loose(label)
        bucket = by_term[term]
        bucket['count'] += 1
        if num != 99999:
            bucket['nums_seen'].add(num)
            if num > bucket['max']:
                bucket['max'] = num
    return by_term


STRIPPABLE_PUNCT = set('：:,，、。.！!？?；;·-—_/\\|·')


def split_long_body(body: str) -> str:
    """Insert a space in the middle of a long bodies that have no separator."""
    if not body:
        return body
    if any(c in ' \t\u3000' for c in body):
        return body
    cjk_positions = [i for i, c in enumerate(body) if '\u4e00' <= c <= '\u9fff']
    if len(cjk_positions) < 12:
        return body
    mid = cjk_positions[(len(cjk_positions) + 1) // 2]
    return body[:mid] + ' ' + body[mid:]


def clean_labels(entries):
    """Strip repeated same-position punctuation; split long bodies at middle."""
    if not entries:
        return entries
    bodies = []
    for label, off in entries:
        s = find_body_start(label)
        head = label[:s].rstrip()
        body = label[s:]
        bodies.append([head, body, off])

    from collections import Counter
    first_chars = Counter()
    total = 0
    for _h, b, _o in bodies:
        if b:
            first_chars[b[0]] += 1
            total += 1
    strip_at_0 = set()
    if total:
        for ch, cnt in first_chars.items():
            if ch in STRIPPABLE_PUNCT and cnt / total > 0.5:
                strip_at_0.add(ch)

    cleaned = []
    for head, body, off in bodies:
        while body and body[0] in strip_at_0:
            body = body[1:].lstrip(' \u3000\t')
        body = split_long_body(body)
        new_label = (head + ' ' + body).rstrip() if body else head
        cleaned.append((new_label, off))
    return cleaned


LOOSE_CHAPTER_RE = re.compile(
    r'第([一二三四五六七八九十百千零〇○\d０-９]{1,8})[ \u3000]*([回章節篇卷])'
)

_CJK_DIGIT_MAP = {
    '零': 0, '〇': 0, '○': 0,
    '一': 1, '二': 2, '三': 3, '四': 4, '五': 5,
    '六': 6, '七': 7, '八': 8, '九': 9,
}


def _parse_num_loose(label: str) -> int:
    """Parse chapter number, handling both formal (百十一=111) and positional
    (一○一=101, 一〇一=101) CJK numerals, plus plain ASCII/fullwidth digits."""
    idx = label.find('第')
    if idx < 0:
        return parse_cjk_number(label)
    idx += 1
    num_chars = []
    while idx < len(label):
        ch = label[idx]
        if ch in CJK_NUM_CHARS or ch.isdigit() or '\uFF10' <= ch <= '\uFF19':
            num_chars.append(ch)
            idx += 1
        else:
            break
    if not num_chars:
        return 99999

    # All digits (ASCII/fullwidth) → direct numeric parse
    if all(c.isdigit() or '\uFF10' <= c <= '\uFF19' for c in num_chars):
        normalized = ''.join(
            chr(ord(c) - 0xFF10 + ord('0')) if '\uFF10' <= c <= '\uFF19' else c
            for c in num_chars
        )
        try:
            return int(normalized)
        except ValueError:
            return 99999

    # If any zero marker is present, treat as positional: each CJK digit 0-9.
    if any(c in '零〇○' for c in num_chars):
        result = 0
        for c in num_chars:
            if c in _CJK_DIGIT_MAP:
                result = result * 10 + _CJK_DIGIT_MAP[c]
            elif c == '十':
                # positional with 十 is unusual; fall back to formal
                return parse_cjk_number(label.replace('○', '〇'))
            elif c in '百千':
                return parse_cjk_number(label.replace('○', '〇'))
            else:
                return 99999
        return result

    # Formal CJK numerals (十, 百, 千): delegate to the shared parser.
    return parse_cjk_number(label)


def fill_gaps(entries, raw_text):
    """Second-pass scan: add missing chapter numbers for the dominant terminator.

    Only runs when we already found at least 50% of chapters for some
    terminator. Returns (merged_entries, set_of_filled_offsets, dominant_term).
    """
    by_term = analyze_entries(entries)
    dominant = max(TERMINATORS, key=lambda t: by_term[t]['count'])
    if by_term[dominant]['count'] == 0:
        return entries, set(), None

    seen_nums = set(by_term[dominant]['nums_seen'])
    max_seen = by_term[dominant]['max']
    if not seen_nums or max_seen == 0:
        return entries, set(), dominant
    if len(seen_nums) / max_seen < 0.5:
        return entries, set(), dominant

    filled = []
    # A chapter heading is typically at a line start, but in some books the
    # previous paragraph's sentence ends on the same line (e.g. "...下回分解。
    # 第一○一回 ...") — accept sentence-end punctuation as an equivalent boundary.
    SENT_END = set('。！？.!?｡')
    for m in LOOSE_CHAPTER_RE.finditer(raw_text):
        if m.group(2) != dominant:
            continue
        start = m.start()
        # Require line-start: previous non-space char must be \n, chapter break,
        # or sentence-ending punctuation.
        i = start - 1
        while i >= 0 and raw_text[i] in ' \t\u3000':
            i -= 1
        if i >= 0 and raw_text[i] not in '\n\x0E' and raw_text[i] not in SENT_END:
            continue
        n = _parse_num_loose(m.group(0))
        if n == 99999 or n in seen_nums:
            continue
        end = m.end()
        line_end = min(end + 80, len(raw_text))
        for brk in ('\n', '\x0E'):
            p = raw_text.find(brk, end, line_end)
            if p != -1:
                line_end = min(line_end, p)
        tail = raw_text[end:line_end].strip(' \u3000\t')
        label = m.group(0) + (' ' + tail if tail else '')
        filled.append((label, start))
        seen_nums.add(n)

    if not filled:
        return entries, set(), dominant
    filled_offsets = {off for _lbl, off in filled}
    merged = list(entries) + filled
    merged.sort(key=lambda x: x[1])
    return merged, filled_offsets, dominant


def format_gaps(nums_seen: set, max_seen: int) -> str:
    if max_seen == 0 or not nums_seen:
        return ''
    missing = [n for n in range(1, max_seen + 1) if n not in nums_seen]
    if not missing:
        return ''
    missing.sort()
    runs = []
    start = prev = missing[0]
    for n in missing[1:]:
        if n == prev + 1:
            prev = n
        else:
            runs.append(f"{start}" if start == prev else f"{start}-{prev}")
            start = prev = n
    runs.append(f"{start}" if start == prev else f"{start}-{prev}")
    return ', '.join(runs)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    books_dir = os.path.join(script_dir, '..', 'sd_card', 'books', 'cn')
    epubs = sorted(glob.glob(os.path.join(books_dir, '*.epub')))

    if not epubs:
        print("No EPUB files found in", books_dir)
        sys.exit(1)

    out_path = os.path.join(script_dir, '..', 'output', 'all_toc_report.txt')
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    real_useful = []
    virtual_only = []
    short_books = []

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("Chinese EPUB - virtual TOC report\n")
        f.write(f"A real TOC with >= {USEFUL_TOC_THRESHOLD} entries is treated as\n")
        f.write("useful navigation; the virtual scanner is not run for those books.\n")
        f.write("Flag [!] means the virtual scanner produced fewer entries than\n")
        f.write("the highest chapter number parsed from its labels.\n\n")

        for epub_path in epubs:
            title = os.path.splitext(os.path.basename(epub_path))[0]
            print(f"Processing: {title}")

            real_kind, real_count = detect_real_toc(epub_path)

            f.write('=' * 60 + '\n')
            f.write(f"{title}\n")
            f.write('=' * 60 + '\n')

            if real_kind and real_count >= USEFUL_TOC_THRESHOLD:
                f.write(f"  Real TOC: {real_kind}, {real_count} entries (device uses this directly)\n\n")
                real_useful.append((title, real_kind, real_count))
                continue

            if real_kind:
                f.write(f"  Real TOC: {real_kind}, {real_count} entry - insufficient for navigation,\n")
                f.write("  device falls back to virtual TOC below.\n")

            virtual_only.append(title)

            old_stdout = sys.stdout
            sys.stdout = open(os.devnull, 'w')
            try:
                result = load_epub_text(epub_path)
                text = result[0] if isinstance(result, tuple) else result
            finally:
                sys.stdout.close()
                sys.stdout = old_stdout

            if not text:
                f.write("  (failed to load)\n\n")
                continue

            entries = generate_virtual_toc(text)

            if not entries:
                f.write("  (no virtual TOC entries found)\n\n")
                continue

            entries = clean_labels(entries)
            entries, filled_offsets, _dom = fill_gaps(entries, text)

            for i, (label, off) in enumerate(entries):
                tag = ' [fill]' if off in filled_offsets else ''
                f.write(f"  [{i+1:3d}]{tag} {label}\n")

            by_term = analyze_entries(entries)
            total = len(entries)
            filled_count = len(filled_offsets)
            if filled_count:
                f.write(f"\n  Total: {total} entries ({filled_count} gap-filled)\n")
            else:
                f.write(f"\n  Total: {total} entries\n")

            book_flagged = False
            for term in TERMINATORS + ['?']:
                b = by_term[term]
                if b['count'] == 0:
                    continue
                max_seen = b['max']
                count = b['count']
                gaps = format_gaps(b['nums_seen'], max_seen) if max_seen else ''
                flag = ''
                if max_seen and count < max_seen:
                    flag = f"  [!] expected >= {max_seen}, got {count}"
                    if gaps:
                        flag += f" (missing: {gaps})"
                    book_flagged = True
                elif gaps:
                    flag = f"  [!] gaps in numbering (missing: {gaps})"
                    book_flagged = True
                f.write(f"    {term}: {count} entries, max #={max_seen or '-'}{flag}\n")

            if book_flagged:
                short_books.append((title, total, by_term))

            f.write("\n")

        f.write('\n' + '#' * 60 + '\n')
        f.write("SUMMARY\n")
        f.write('#' * 60 + '\n')
        f.write(f"  Total books:              {len(epubs)}\n")
        f.write(f"  With useful real TOC:     {len(real_useful)}\n")
        f.write(f"  Using virtual TOC:        {len(virtual_only)}\n")
        f.write(f"  Virtual TOC flagged:      {len(short_books)}\n\n")

        if real_useful:
            f.write("  Books with useful real TOC:\n")
            for title, kind, count in real_useful:
                f.write(f"    - {title}  [{kind}, {count} entries]\n")
            f.write("\n")

        if short_books:
            f.write("  Books with short virtual TOC:\n")
            for title, total, by_term in short_books:
                parts = []
                for term in TERMINATORS + ['?']:
                    b = by_term[term]
                    if b['count'] and b['max'] and b['count'] < b['max']:
                        parts.append(f"{term} {b['count']}/{b['max']}")
                f.write(f"    - {title}  -  total {total}  [{', '.join(parts)}]\n")

    print(f"\nReport written to: {out_path}")
    print(f"Total books:              {len(epubs)}")
    print(f"With useful real TOC:     {len(real_useful)}")
    print(f"Using virtual TOC:        {len(virtual_only)}")
    print(f"Virtual TOC flagged:      {len(short_books)}")
    for title, total, _ in short_books:
        print(f"  - {title} (total {total})")


if __name__ == '__main__':
    main()
