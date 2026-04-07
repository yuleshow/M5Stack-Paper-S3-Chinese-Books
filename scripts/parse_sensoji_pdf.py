#!/usr/bin/env python3
"""Parse Senso-ji fortune slip PDF into structured CSV.

The PDF has 4 overlapping text layers and 3 logical columns:
  - Level/number column (x < 70): fortune level chars and slip numbers
  - Poem column (70 <= x < 135): 5-char poem lines (title + 3 more)
  - Text column (x >= 135): explanation + 聖意 categories
"""
import pdfplumber
import csv
import re
import os
from collections import defaultdict

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

PDF_PATH = 'assets/Fortune_Slips/senso-ji/日本東京淺草觀音寺一百籤(2011年11月重譯).pdf'
CSV_PATH = 'assets/Fortune_Slips/senso-ji/sensoji.csv'

# Column x-boundaries (consistent across all pages)
LEVEL_X_MAX = 70
POEM_X_MAX = 135

# Fortune levels found in Senso-ji slips
LEVEL_MAP = {
    '大吉': '大吉', '吉': '吉', '小吉': '小吉',
    '半吉': '半吉', '末吉': '末吉', '末小吉': '末小吉',
    '凶': '凶', '兇': '凶',
}


def group_into_lines(chars, y_tolerance=5):
    """Group characters into lines by y-position, return sorted list of (y, text)."""
    if not chars:
        return []
    chars = sorted(chars, key=lambda c: (c['top'], c['x0']))
    lines = []
    cur_line = [chars[0]]
    cur_y = chars[0]['top']
    for c in chars[1:]:
        if abs(c['top'] - cur_y) <= y_tolerance:
            cur_line.append(c)
        else:
            text = ''.join(ch['text'] for ch in sorted(cur_line, key=lambda x: x['x0'])).strip()
            if text:
                lines.append((cur_y, text))
            cur_line = [c]
            cur_y = c['top']
    if cur_line:
        text = ''.join(ch['text'] for ch in sorted(cur_line, key=lambda x: x['x0'])).strip()
        if text:
            lines.append((cur_y, text))
    return lines


def extract_columns(pdf_path):
    """Extract text separated into 3 columns across all pages."""
    level_lines = []  # (global_y, text)
    poem_lines = []
    text_lines = []

    with pdfplumber.open(pdf_path) as pdf:
        page_y_offset = 0
        for page in pdf.pages:
            dp = page.dedupe_chars(tolerance=2, extra_attrs=())
            page_height = page.height

            level_chars = [c for c in dp.chars if c['x0'] < LEVEL_X_MAX]
            poem_chars = [c for c in dp.chars if LEVEL_X_MAX <= c['x0'] < POEM_X_MAX]
            text_chars = [c for c in dp.chars if c['x0'] >= POEM_X_MAX]

            for y, text in group_into_lines(level_chars):
                level_lines.append((y + page_y_offset, text))
            for y, text in group_into_lines(poem_chars):
                poem_lines.append((y + page_y_offset, text))
            # Filter page footer numbers from text column
            page_text_lines = group_into_lines(text_chars)
            for y, text in page_text_lines:
                # Page footer is a standalone number (1-10) near bottom of page
                if y > page_height * 0.9 and re.match(r'^\d{1,2}$', text.strip()):
                    continue
                text_lines.append((y + page_y_offset, text))

            page_y_offset += page_height

    return level_lines, poem_lines, text_lines


def find_slip_boundaries(level_lines):
    """Find y positions where each slip starts (where level column has a number).

    Page footer numbers are filtered out by checking for duplicate slip numbers
    and keeping only the first occurrence of each number.
    """
    raw_boundaries = []
    for y, text in level_lines:
        text = text.strip()
        if re.match(r'^\d+$', text):
            raw_boundaries.append((y, int(text)))

    # Filter: keep only first occurrence of each slip number
    # (page numbers at bottom of pages create duplicates)
    seen = set()
    boundaries = []
    for y, num in sorted(raw_boundaries, key=lambda x: x[0]):
        if num not in seen and 1 <= num <= 100:
            seen.add(num)
            boundaries.append((y, num))
    return boundaries


def get_lines_in_range(lines, y_start, y_end):
    """Get all lines within a y-range.

    Uses a small tolerance for y_start because text column chars
    can be ~1.5px before the level column number on the same visual line.
    """
    return [(y, text) for y, text in lines if y_start - 3 <= y < y_end - 3]


def parse_seimei(text):
    """Parse 聖意 categories from text.

    Categories: 願望, 疾病, 遺失物, 盼望的人, 蓋新居/搬家, 結婚/結親緣/嫁娶,
                交往, 旅行
    """
    categories = {}
    # Normalize separators
    text = text.replace('：', ':')

    # This regex finds all category labels followed by ':'
    # The category label can be a combination like 蓋新居、搬家 or 結親緣、旅行
    # We need to split compound categories into individual ones
    cat_pattern = (
        r'(願望|疾病|遺失物|盼望的人|'
        r'蓋新居[、搬家]*|搬家|'
        r'結[婚親]緣?[、和喜慶祝賀交往旅行嫁娶]*|嫁娶[、旅行交往等]*|婚事[、交往]*|'
        r'交往|旅行(?:[、交往]*))[：:]'
    )
    matches = list(re.finditer(cat_pattern, text))

    for i, m in enumerate(matches):
        cat_text = m.group(1)
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        value = text[start:end].strip()
        # Clean trailing punctuation
        value = re.sub(r'[。，、]+$', '', value).strip()

        # Handle compound categories like "結親緣、旅行" or "蓋新居、搬家"
        if '蓋新居' in cat_text or '搬家' in cat_text:
            categories['蓋新居搬家'] = value
        elif '旅行' in cat_text and ('結' in cat_text or '嫁娶' in cat_text):
            # Compound like "結親緣、旅行" - value applies to both
            categories['結婚交往'] = value
            categories['旅行'] = value
        elif any(k in cat_text for k in ['結婚', '結親', '嫁娶', '婚事']):
            if '交往' in cat_text:
                categories['結婚交往'] = value
            elif '旅行' in cat_text:
                categories['結婚交往'] = value
                categories['旅行'] = value
            else:
                categories['結婚交往'] = value
        elif '願望' in cat_text:
            categories['願望'] = value
        elif '疾病' in cat_text:
            categories['疾病'] = value
        elif '遺失物' in cat_text:
            categories['遺失物'] = value
        elif '盼望' in cat_text:
            categories['盼望的人'] = value
        elif '旅行' in cat_text:
            categories['旅行'] = value
        elif '交往' in cat_text:
            categories['交往'] = value

    return categories


def parse_slips(pdf_path):
    """Parse all 100 fortune slips from the PDF."""
    level_lines, poem_lines, text_lines = extract_columns(pdf_path)

    # Find slip boundaries
    boundaries = find_slip_boundaries(level_lines)
    print(f"Found {len(boundaries)} slip boundaries")

    slips = []
    for i, (y_start, slip_num) in enumerate(boundaries):
        y_end = boundaries[i + 1][0] if i + 1 < len(boundaries) else float('inf')

        # Get level characters (non-numeric entries in level column)
        lvl_entries = get_lines_in_range(level_lines, y_start, y_end)
        level_chars = []
        for y, text in lvl_entries:
            text = text.strip()
            if not re.match(r'^\d+$', text):
                level_chars.append(text)
        level = ''.join(level_chars)
        # Normalize
        level = level.replace('兇', '凶')
        if level not in LEVEL_MAP.values():
            # Try common patterns
            for k, v in LEVEL_MAP.items():
                if k in level:
                    level = v
                    break

        # Get poem lines
        poem_entries = get_lines_in_range(poem_lines, y_start, y_end)
        poems = []
        for y, text in poem_entries:
            text = text.strip()
            if text:
                poems.append(text)

        # Get text content
        text_entries = get_lines_in_range(text_lines, y_start, y_end)
        full_text = ''
        for y, text in text_entries:
            full_text += text.strip()

        # Split text into explanation and 聖意
        # 聖意 starts with 願望：
        seimei_start = -1
        for marker in ['願望：', '願望:']:
            pos = full_text.find(marker)
            if pos >= 0:
                seimei_start = pos
                break

        explanation = ''
        seimei_text = ''
        if seimei_start >= 0:
            explanation = full_text[:seimei_start].strip()
            # 聖意 text ends before any extra commentary that follows the last category
            seimei_raw = full_text[seimei_start:]
            # Find last category value ending (look for last 。or 吧 pattern)
            # The 聖意 section ends right after the last recognized category value
            last_cat_end = 0
            for m in re.finditer(r'(?:願望|疾病|遺失物|盼望的人|蓋新居|搬家|結[婚親]|嫁娶|婚事|交往|旅行)[：:]', seimei_raw):
                # Find the end of this category's value (next category start or end of text)
                pass
            # Simpler: find the last 。吧 pattern followed by non-category text
            seimei_text = seimei_raw
        else:
            explanation = full_text.strip()

        # Parse 聖意 categories
        categories = parse_seimei(seimei_text) if seimei_text else {}

        slip = {
            'number': slip_num,
            'level': level,
            'poems': poems,
            'explanation': explanation,
            'categories': categories,
        }
        slips.append(slip)

    return slips


def write_csv(slips, csv_path):
    """Write parsed slips to CSV with sensoji-specific columns."""
    header = [
        '#', '籤號', '等級', '詩曰',
        '詩意',
        '願望', '疾病', '遺失物', '盼望的人', '蓋新居搬家',
        '結婚交往', '旅行',
    ]

    cat_keys = ['願望', '疾病', '遺失物', '盼望的人', '蓋新居搬家',
                '結婚交往', '旅行']

    with open(csv_path, 'w', encoding='utf-8-sig', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(header)

        for slip in slips:
            poem = '\n'.join(slip['poems'])
            cats = slip['categories']
            # Merge 交往 into 結婚交往 if present and 結婚交往 is empty
            if cats.get('交往') and not cats.get('結婚交往'):
                cats['結婚交往'] = cats['交往']
            row = [
                slip['number'],
                f"第{slip['number']}籤",
                slip['level'],
                poem,
                slip['explanation'],
            ]
            for key in cat_keys:
                row.append(cats.get(key, ''))
            writer.writerow(row)


def main():
    slips = parse_slips(PDF_PATH)

    # Print summary
    for slip in slips:
        poems_str = ' / '.join(slip['poems'])
        cats = slip['categories']
        cat_str = ', '.join(f"{k}:{v[:10]}" for k, v in cats.items())
        print(f"#{slip['number']:3d} [{slip['level']:3s}] {poems_str}")
        print(f"     解說: {slip['explanation'][:60]}...")
        if cat_str:
            print(f"     聖意: {cat_str}")
        print()

    # Write CSV
    write_csv(slips, CSV_PATH)
    print(f"\nWrote {len(slips)} slips to {CSV_PATH}")


if __name__ == '__main__':
    main()
