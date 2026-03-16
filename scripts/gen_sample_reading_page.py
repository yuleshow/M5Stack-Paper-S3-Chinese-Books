#!/usr/bin/env python3
"""
Generate a sample reading page that exactly matches the device rendering.
Faithfully ports drawReading() from book_reader.cpp including:
- Exact layout constants from globals.h
- toVerticalPunct() mapping from utf8_utils.h
- Latin text run detection & 90° rotation
- Kinsoku (禁則處理) column-start prohibition
- EPUB text extraction via spine order
"""

import os
import sys
import zipfile
import random
from lxml import etree

from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ==================== Layout constants (globals.h) ====================
SCREEN_W = 540
SCREEN_H = 960
READING_AREA_TOP    = 60
READING_AREA_BOTTOM = 850
READING_AREA_LEFT   = 50
READING_AREA_RIGHT  = 520
VERTICAL_TEXT_MAX_Y = 900
PROGRESS_BAR_X = 30
NAV_Y = 886
NAV_ICON_SIZE = 64
NAV_PREV_X = 10
NAV_NEXT_X = 84
NAV_RETURN_X = 466
TOOLBAR_X = 150
TOOLBAR_Y = 905

# ==================== toVerticalPunct (utf8_utils.h) ====================
VERT_PUNCT_MAP = {
    0x300C: 0xFE41,  # 「 → ﹁
    0x300D: 0xFE42,  # 」 → ﹂
    0x201C: 0xFE41,  # " → ﹁
    0x201D: 0xFE42,  # " → ﹂
    0x3008: 0xFE3F,  # 〈 → ︿
    0x3009: 0xFE40,  # 〉 → ﹀
    0x300E: 0xFE43,  # 『 → ﹃
    0x300F: 0xFE44,  # 』 → ﹄
    0x300A: 0xFE3D,  # 《 → ︽
    0x300B: 0xFE3E,  # 》 → ︾
    0x3010: 0xFE3B,  # 【 → ︻
    0x3011: 0xFE3C,  # 】 → ︼
    0xFF08: 0xFE35,  # （ → ︵
    0xFF09: 0xFE36,  # ） → ︶
    0x3016: 0xFE17,  # 〖 → ︗
    0x3017: 0xFE18,  # 〗 → ︘
    0x3014: 0xFE39,  # 〔 → ︹
    0x3015: 0xFE3A,  # 〕 → ︺
    0xFF5B: 0xFE37,  # ｛ → ︷
    0xFF5D: 0xFE38,  # ｝ → ︸
    0xFF3B: 0xFE47,  # ［ → ﹇
    0xFF3D: 0xFE48,  # ］ → ﹈
    0x2026: 0xFE19,  # … → ︙
    0x2025: 0xFE30,  # ‥ → ︰
    0x2014: 0xFE31,  # — → ︱
    0xFE4F: 0xFE34,  # ﹏ → ︴
}

def toVerticalPunct(cp):
    return VERT_PUNCT_MAP.get(cp, cp)

# ==================== isColumnStartProhibited (utf8_utils.h) ====================
COLUMN_START_PROHIBITED = {
    0x300D, 0x300F, 0x300B, 0x3009, 0x3011, 0x3015, 0x3017,
    0xFF09, 0xFF5D, 0xFF3D, 0x201D, 0x2019,
    0x3002, 0xFF0C, 0x3001, 0xFF1B, 0xFF1A, 0xFF01, 0xFF1F,
    0x30FB, 0x2026, 0x2025, 0x2014,
    0xFE42, 0xFE44, 0xFE3E, 0xFE40, 0xFE3C, 0xFE3A, 0xFE18,
    0xFE36, 0xFE38, 0xFE48, 0xFE19, 0xFE30, 0xFE31,
    0x002C, 0x002E, 0x003F, 0x0021, 0x003B, 0x003A, 0x0029, 0x005D,
}

def isColumnStartProhibited(cp):
    return cp in COLUMN_START_PROHIBITED


# ==================== EPUB text extraction (epub_reader.cpp style) ====================
def extract_epub_full_text(epub_path):
    """Extract full text from EPUB in spine reading order, like epub_reader.cpp."""
    with zipfile.ZipFile(epub_path) as zf:
        # Parse container.xml to find content.opf
        container = etree.fromstring(zf.read('META-INF/container.xml'))
        ns = {'c': 'urn:oasis:names:tc:opendocument:xmlns:container'}
        rootfile = container.find('.//c:rootfile', ns)
        opf_path = rootfile.get('full-path')
        opf_dir = os.path.dirname(opf_path)

        # Parse content.opf
        opf = etree.fromstring(zf.read(opf_path))
        opf_ns = opf.nsmap.get(None, '')
        ns_opf = {'opf': opf_ns} if opf_ns else {}

        # Build manifest id→href map
        manifest = {}
        for item in opf.iter('{%s}item' % opf_ns if opf_ns else 'item'):
            manifest[item.get('id')] = item.get('href')

        # Get spine order
        spine_items = []
        for itemref in opf.iter('{%s}itemref' % opf_ns if opf_ns else 'itemref'):
            idref = itemref.get('idref')
            if idref in manifest:
                spine_items.append(manifest[idref])

        # Extract text from each spine item
        full_text = []
        for href in spine_items:
            path = os.path.join(opf_dir, href).replace('\\', '/')
            # Normalize path
            path = os.path.normpath(path)
            if path not in zf.namelist():
                # Try without normalization
                path = (opf_dir + '/' + href) if opf_dir else href
            try:
                content = zf.read(path)
            except KeyError:
                continue

            # Parse HTML and extract text
            try:
                tree = etree.fromstring(content, etree.HTMLParser(encoding='utf-8'))
            except Exception:
                continue

            # Get text from <body>
            body = tree.find('.//body')
            if body is None:
                continue

            for elem in body.iter():
                if elem.tag in ('script', 'style'):
                    continue
                if elem.text and elem.text.strip():
                    full_text.append(elem.text.strip())
                if elem.tail and elem.tail.strip():
                    full_text.append(elem.tail.strip())
                if elem.tag in ('p', 'div', 'br', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6'):
                    full_text.append('\n')

        return ''.join(full_text)


# ==================== drawReading() port ====================
def render_reading_page(text, font_path, font_size, output_path, page_num, total_pages):
    """
    Faithfully port drawReading() from book_reader.cpp.
    Renders one page of vertical CJK text with full device UI.
    Returns (image, bytes_consumed) for pagination.
    """
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    draw = ImageDraw.Draw(img)

    font = ImageFont.truetype(font_path, font_size)
    # Device uses &fonts::Font2 (M5Unified built-in bitmap font) at textSize(2)
    # for time, battery %, page numbers, reading %.
    # Approximate with Helvetica which is visually close.
    ui_font = ImageFont.truetype('Helvetica.ttc', 16)
    ui_font_small = ImageFont.truetype('Helvetica.ttc', 12)

    # --- Layout (book_reader.cpp lines 880-900) ---
    charHeight = font_size + font_size // 5       # ~1.2x
    columnSpacing = font_size + font_size // 5    # ~1.2x
    rdLeft = READING_AREA_LEFT
    rdRight = READING_AREA_RIGHT
    rdTop = READING_AREA_TOP
    rdMaxY = VERTICAL_TEXT_MAX_Y

    charsPerColumn = (rdMaxY - rdTop) // charHeight - 2
    if charsPerColumn < 1:
        charsPerColumn = 1

    # --- Status bar (ui_drawing.cpp drawStatusBar) ---
    time_str = "12:19"
    bat_pct = 46
    draw.text((8, 4), time_str, font=ui_font, fill=0)

    bx = SCREEN_W - 46
    by = 12
    bw, bh = 34, 18
    bat_str = f"{bat_pct}%"
    bbox = ui_font.getbbox(bat_str)
    tw = bbox[2] - bbox[0]
    draw.text((bx - 8 - tw, 4), bat_str, font=ui_font, fill=0)
    draw.rectangle([bx, by, bx + bw, by + bh], outline=0, width=1)
    draw.rectangle([bx + 1, by + 1, bx + bw - 1, by + bh - 1], outline=0, width=1)
    draw.rectangle([bx + bw, by + 4, bx + bw + 3, by + bh - 4], fill=0)
    fill_w = int((bw - 6) * bat_pct / 100)
    if fill_w > 0:
        draw.rectangle([bx + 3, by + 3, bx + 3 + fill_w, by + bh - 3], fill=0)

    # --- Vertical text rendering (main loop, book_reader.cpp lines 912-1200) ---
    columnX = rdRight - columnSpacing // 2
    startY = rdTop
    currentY = startY
    charIndex = 0
    charsDrawn = 0
    i = 0
    renderStopByte = len(text)

    while i < len(text):
        ch = text[i]
        charStart = i
        unicode_val = ord(ch)
        i += 1

        # Apply vertical punctuation mapping
        mapped = toVerticalPunct(unicode_val)

        # Skip control chars, \r, ideographic space, ASCII space
        if unicode_val == 0x0D:
            continue
        if unicode_val < 0x20 and unicode_val != 0x0A:
            continue
        if unicode_val == 0x3000:
            continue
        if unicode_val == 0x20:
            continue

        # Latin text run detection (book_reader.cpp lines 947-1025)
        if unicode_val >= 0x21 and unicode_val <= 0x7E:
            # Peek ahead for letters
            has_letter = (0x41 <= unicode_val <= 0x5A) or (0x61 <= unicode_val <= 0x7A)
            if not has_letter:
                peek_i = i
                while peek_i < len(text):
                    pc = ord(text[peek_i])
                    if (0x41 <= pc <= 0x5A) or (0x61 <= pc <= 0x7A):
                        has_letter = True
                        break
                    if 0x21 <= pc <= 0x7E:
                        peek_i += 1
                        continue
                    if pc == 0x20 and peek_i + 1 < len(text) and 0x21 <= ord(text[peek_i + 1]) <= 0x7E:
                        peek_i += 1
                        continue
                    break

            if not has_letter:
                # No letters → draw as normal char (goto draw_normal_char)
                pass  # Fall through to normal char drawing below
            else:
                # Collect Latin run
                i = charStart
                latin_run = ""
                while i < len(text):
                    pc = ord(text[i])
                    if 0x21 <= pc <= 0x7E:
                        latin_run += text[i]
                        i += 1
                    elif pc == 0x20 and i + 1 < len(text) and 0x21 <= ord(text[i + 1]) <= 0x7E:
                        latin_run += ' '
                        i += 1
                    else:
                        break
                if not latin_run:
                    continue

                # Measure and render rotated 90° CW
                latin_font = ImageFont.truetype(font_path, font_size)
                lbbox = latin_font.getbbox(latin_run)
                textW = lbbox[2] - lbbox[0]
                spriteW = textW + 4
                spriteH = font_size + 4
                rotatedH = spriteW  # After 90° rotation

                # Check fit
                if currentY + rotatedH > rdMaxY - charHeight:
                    columnX -= columnSpacing
                    currentY = startY
                    charIndex = 0
                    if columnX - columnSpacing // 2 < rdLeft:
                        renderStopByte = charStart
                        i = charStart
                        break

                # Render into temp image, rotate 90° CW
                sprite = Image.new('L', (spriteW, spriteH), 255)
                sd = ImageDraw.Draw(sprite)
                sd.text((2 - lbbox[0], 2 - lbbox[1]), latin_run, font=latin_font, fill=0)
                rotated = sprite.rotate(-90, expand=True)

                # Paste centered on columnX
                paste_x = columnX - rotated.width // 2
                paste_y = currentY
                img.paste(rotated, (paste_x, paste_y))

                currentY += rotatedH
                charIndex += len(latin_run)
                charsDrawn += len(latin_run)

                # Column overflow
                if charIndex >= charsPerColumn or currentY > rdMaxY:
                    columnX -= columnSpacing
                    currentY = startY
                    charIndex = 0
                    if columnX - columnSpacing // 2 < rdLeft:
                        renderStopByte = i
                        break
                continue

        # Hard newline → new column
        if unicode_val == 0x0A:
            if charIndex > 0:
                columnX -= columnSpacing
                currentY = startY
                charIndex = 0
                if columnX - columnSpacing // 2 < rdLeft:
                    renderStopByte = i
                    break
            continue

        # --- draw_normal_char ---
        vOffset = (charHeight - font_size) // 2
        draw_char = chr(mapped)
        drawX = columnX - font_size // 2
        drawY = currentY + vOffset
        draw.text((drawX, drawY), draw_char, font=font, fill=0)
        charsDrawn += 1
        currentY += charHeight
        charIndex += 1

        # Column overflow + kinsoku (book_reader.cpp lines 1148-1200)
        if charIndex >= charsPerColumn or currentY > rdMaxY:
            # Kinsoku: peek at next char
            if i < len(text):
                peekUnicode = ord(text[i])
                mappedPeek = toVerticalPunct(peekUnicode)
                if isColumnStartProhibited(peekUnicode) or isColumnStartProhibited(mappedPeek):
                    # Draw prohibited char in reserved slot at full size
                    vOff = (charHeight - font_size) // 2
                    kDrawX = columnX - font_size // 2
                    kDrawY = currentY + vOff
                    draw.text((kDrawX, kDrawY), chr(mappedPeek), font=font, fill=0)
                    charsDrawn += 1
                    i += 1  # Consume peeked char

            columnX -= columnSpacing
            currentY = startY
            charIndex = 0
            if columnX - columnSpacing // 2 < rdLeft:
                renderStopByte = i
                break

    # --- Progress bar (book_reader.cpp lines 1240-1270) ---
    barX = PROGRESS_BAR_X
    barY = 878
    barW = SCREEN_W - 60  # 480
    barH = 4
    progress = page_num / max(total_pages - 1, 1)
    fillW = int(barW * progress)
    draw.rectangle([barX, barY, barX + barW, barY + barH], outline=0)
    if fillW > 0:
        draw.rectangle([barX, barY, barX + fillW, barY + barH], fill=0)

    # Percentage (BR_DATUM = right-bottom aligned)
    pct_str = f"{int(progress * 100)}%"
    pb = ui_font.getbbox(pct_str)
    draw.text((barX + barW - (pb[2] - pb[0]), barY - 6 - (pb[3] - pb[1])),
              pct_str, font=ui_font, fill=0)

    # Page counter (BL_DATUM = left-bottom aligned)
    page_str = f"{page_num + 1}/{total_pages}"
    pb2 = ui_font.getbbox(page_str)
    draw.text((barX, barY - 6 - (pb2[3] - pb2[1])),
              page_str, font=ui_font, fill=0)

    # --- Toolbar (book_reader.cpp lines 1272-1300) ---
    toolbar_path = 'assets/icons/reader_toolbar.png'
    if os.path.exists(toolbar_path):
        tb_img = Image.open(toolbar_path).convert('L')
        img.paste(tb_img, (TOOLBAR_X, TOOLBAR_Y))
    # Font size number in cell 1
    cellW = 52
    sizeX = TOOLBAR_X + cellW + cellW // 2
    sizeY = TOOLBAR_Y + 25
    size_str = str(font_size)
    sb = ui_font.getbbox(size_str)
    draw.text((sizeX - (sb[2] - sb[0]) // 2, sizeY - (sb[3] - sb[1]) // 2),
              size_str, font=ui_font, fill=0)

    # --- Nav icons (back/next arrows + return) ---
    btn = NAV_ICON_SIZE
    for icon_name, ix in [('back.png', NAV_PREV_X), ('next.png', NAV_NEXT_X)]:
        icon_path = f'assets/icons/{icon_name}'
        if os.path.exists(icon_path):
            ic = Image.open(icon_path)
            if ic.mode == 'RGBA':
                bg = Image.new('RGBA', ic.size, (255,255,255,255))
                ic = Image.alpha_composite(bg, ic)
            img.paste(ic.convert('L'), (ix, NAV_Y))
        else:
            # Draw arrow placeholder
            draw.rectangle([ix, NAV_Y, ix + btn, NAV_Y + btn], outline=0)
            if 'back' in icon_name:
                pts = [(ix + 15, NAV_Y + btn // 2),
                       (ix + btn - 15, NAV_Y + 15),
                       (ix + btn - 15, NAV_Y + btn - 15)]
            else:
                pts = [(ix + btn - 15, NAV_Y + btn // 2),
                       (ix + 15, NAV_Y + 15),
                       (ix + 15, NAV_Y + btn - 15)]
            draw.polygon(pts, fill=0)

    # Return button
    ret_path = 'assets/icons/return.png'
    if os.path.exists(ret_path):
        ic = Image.open(ret_path)
        if ic.mode == 'RGBA':
            bg = Image.new('RGBA', ic.size, (255,255,255,255))
            ic = Image.alpha_composite(bg, ic)
        img.paste(ic.convert('L'), (NAV_RETURN_X, NAV_Y))
    else:
        draw.rectangle([NAV_RETURN_X, NAV_Y, NAV_RETURN_X + btn, NAV_Y + btn], outline=0)
        m = 18
        draw.line([(NAV_RETURN_X + m, NAV_Y + m),
                   (NAV_RETURN_X + btn - m, NAV_Y + btn - m)], fill=0, width=3)
        draw.line([(NAV_RETURN_X + btn - m, NAV_Y + m),
                   (NAV_RETURN_X + m, NAV_Y + btn - m)], fill=0, width=3)

    img.save(output_path)
    print(f"Saved: {output_path}")
    print(f"  Font: {font_path} @ {font_size}px")
    print(f"  charHeight={charHeight}, columnSpacing={columnSpacing}, charsPerColumn={charsPerColumn}")
    print(f"  Chars drawn: {charsDrawn}")
    print(f"  Page: {page_num + 1}/{total_pages}")
    return renderStopByte


def paginate(text, font_path, font_size):
    """Simulate pagination: calculate byte offsets for each page."""
    charHeight = font_size + font_size // 5
    columnSpacing = font_size + font_size // 5
    rdLeft = READING_AREA_LEFT
    rdRight = READING_AREA_RIGHT
    rdTop = READING_AREA_TOP
    rdMaxY = VERTICAL_TEXT_MAX_Y
    charsPerColumn = (rdMaxY - rdTop) // charHeight - 2
    if charsPerColumn < 1:
        charsPerColumn = 1

    pages = [0]
    i = 0
    while i < len(text):
        columnX = rdRight - columnSpacing // 2
        currentY = rdTop
        charIndex = 0

        while i < len(text):
            ch = text[i]
            unicode_val = ord(ch)
            charStart = i
            i += 1
            mapped = toVerticalPunct(unicode_val)

            if unicode_val in (0x0D, 0x3000, 0x20):
                continue
            if unicode_val < 0x20 and unicode_val != 0x0A:
                continue

            # Latin run
            if 0x21 <= unicode_val <= 0x7E:
                has_letter = (0x41 <= unicode_val <= 0x5A) or (0x61 <= unicode_val <= 0x7A)
                if not has_letter:
                    peek_i = i
                    while peek_i < len(text):
                        pc = ord(text[peek_i])
                        if (0x41 <= pc <= 0x5A) or (0x61 <= pc <= 0x7A):
                            has_letter = True
                            break
                        if 0x21 <= pc <= 0x7E:
                            peek_i += 1
                            continue
                        if pc == 0x20 and peek_i + 1 < len(text) and 0x21 <= ord(text[peek_i + 1]) <= 0x7E:
                            peek_i += 1
                            continue
                        break
                if has_letter:
                    i = charStart
                    latin_run = ""
                    while i < len(text):
                        pc = ord(text[i])
                        if 0x21 <= pc <= 0x7E:
                            latin_run += text[i]
                            i += 1
                        elif pc == 0x20 and i + 1 < len(text) and 0x21 <= ord(text[i + 1]) <= 0x7E:
                            latin_run += ' '
                            i += 1
                        else:
                            break
                    # Approximate rotated height
                    rotatedH = len(latin_run) * font_size * 6 // 10
                    if currentY + rotatedH > rdMaxY - charHeight:
                        columnX -= columnSpacing
                        currentY = rdTop
                        charIndex = 0
                        if columnX - columnSpacing // 2 < rdLeft:
                            i = charStart
                            break
                    currentY += rotatedH
                    charIndex += len(latin_run)
                    if charIndex >= charsPerColumn or currentY > rdMaxY:
                        columnX -= columnSpacing
                        currentY = rdTop
                        charIndex = 0
                        if columnX - columnSpacing // 2 < rdLeft:
                            break
                    continue

            if unicode_val == 0x0A:
                if charIndex > 0:
                    columnX -= columnSpacing
                    currentY = rdTop
                    charIndex = 0
                    if columnX - columnSpacing // 2 < rdLeft:
                        break
                continue

            currentY += charHeight
            charIndex += 1
            if charIndex >= charsPerColumn or currentY > rdMaxY:
                if i < len(text):
                    peekU = ord(text[i])
                    mappedP = toVerticalPunct(peekU)
                    if isColumnStartProhibited(peekU) or isColumnStartProhibited(mappedP):
                        i += 1
                columnX -= columnSpacing
                currentY = rdTop
                charIndex = 0
                if columnX - columnSpacing // 2 < rdLeft:
                    break

        if i < len(text):
            pages.append(i)

    return pages


if __name__ == '__main__':
    epub_path = 'sd_card/books/pg24113-images-3.epub'
    font_path = 'sd_card/fonts/Huiwenmincho-improved.ttf'
    output_path = 'output/sample_reading_page.png'
    font_size = 44

    if not os.path.exists(epub_path):
        print(f"EPUB not found: {epub_path}")
        sys.exit(1)
    if not os.path.exists(font_path):
        print(f"Font not found: {font_path}")
        sys.exit(1)

    os.makedirs('output', exist_ok=True)

    # Extract full text from EPUB (spine order, like device)
    print(f"Extracting text from {epub_path}...")
    full_text = extract_epub_full_text(epub_path)
    print(f"Total text: {len(full_text)} chars")

    # Find the exact text shown on the device screenshot
    # The screenshot page starts with "得知，今日回家，遇了一件大喜"
    search_str = '\u5f97\u77e5\uff0c\u4eca\u65e5\u56de\u5bb6'  # 得知，今日回家
    page_start = full_text.find(search_str)
    if page_start < 0:
        print("ERROR: Could not find screenshot text in EPUB")
        sys.exit(1)
    print(f"Found screenshot text at offset {page_start}")

    # Use the device's page numbers: 57/804 (page index 56)
    device_page = 56
    device_total = 804
    page_text = full_text[page_start:]

    render_reading_page(page_text, font_path, font_size, output_path,
                        device_page, device_total)
