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
import struct
import zipfile
import random
from bisect import bisect_left
from lxml import etree

from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


# ==================== BIN font reader ====================
class BinFont:
    """Read glyphs from a .bin font file (same format as device)."""

    HEADER_SIZE = 137
    ENTRY_SIZE = 20

    def __init__(self, bin_path):
        self.path = bin_path
        with open(bin_path, 'rb') as f:
            self.data = f.read()
        # Parse header
        self.char_count = struct.unpack_from('<I', self.data, 0)[0]
        self.font_size = self.data[4]
        self.version = struct.unpack_from('<I', self.data, 5)[0]
        self.family_name = self.data[9:73].split(b'\x00')[0].decode('utf-8', errors='replace')
        self.style_name = self.data[73:137].split(b'\x00')[0].decode('utf-8', errors='replace')
        # Build sorted unicode list for binary search
        self._unicodes = []
        self._entries = {}
        idx_start = self.HEADER_SIZE
        for i in range(self.char_count):
            off = idx_start + i * self.ENTRY_SIZE
            uni = struct.unpack_from('<I', self.data, off)[0]
            w = struct.unpack_from('<H', self.data, off + 4)[0]
            h = struct.unpack_from('<H', self.data, off + 6)[0]
            bmp_off = struct.unpack_from('<I', self.data, off + 8)[0]
            bmp_sz = struct.unpack_from('<I', self.data, off + 12)[0]
            bx = struct.unpack_from('<h', self.data, off + 16)[0]
            by = struct.unpack_from('<h', self.data, off + 18)[0]
            self._unicodes.append(uni)
            self._entries[uni] = (w, h, bmp_off, bmp_sz, bx, by)

    def has_glyph(self, codepoint):
        return codepoint in self._entries

    def get_glyph(self, codepoint):
        """Return (width, height, bearingX, bearingY, PIL Image) or None."""
        entry = self._entries.get(codepoint)
        if entry is None:
            return None
        w, h, bmp_off, bmp_sz, bx, by = entry
        if w == 0 or h == 0:
            return None
        # Decode 1-bit bitmap: flat bit stream, MSB-first, padded to byte at end
        glyph_img = Image.new('1', (w, h), 1)  # white background
        pixels = glyph_img.load()
        bit_idx = 0
        for row in range(h):
            for col in range(w):
                byte_pos = bmp_off + bit_idx // 8
                bit_pos = 7 - (bit_idx % 8)
                if byte_pos < len(self.data) and (self.data[byte_pos] >> bit_pos) & 1:
                    pixels[col, row] = 0  # black
                bit_idx += 1
        return (w, h, bx, by, glyph_img)


# ==================== Layout constants (globals.h) ====================
SCREEN_W = 540
SCREEN_H = 960
READING_AREA_TOP    = 65
READING_AREA_BOTTOM = 878
READING_AREA_LEFT   = 20
READING_AREA_RIGHT  = 530
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
def render_reading_page(text, font_path, font_size, output_path, page_num, total_pages,
                        silver_mode=False, render_size=None, fallback_path=None, fallback_size=None,
                        bin_font=None, fallback_bin=None):
    """
    Faithfully port drawReading() from book_reader.cpp.
    Renders one page of vertical CJK text with full device UI.
    When bin_font is provided, renders glyphs from BIN bitmaps.
    Returns (image, bytes_consumed) for pagination.
    """
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    draw = ImageDraw.Draw(img)

    # Silver/BIN mode: render at render_size, use fallback for missing glyphs
    actual_render_size = render_size if render_size else font_size

    # TTF fonts (used for Latin runs when in BIN mode, or all rendering in TTF mode)
    font = None
    fallback_font = None
    if bin_font is None:
        font = ImageFont.truetype(font_path, actual_render_size)
        if fallback_path and fallback_size:
            fallback_font = ImageFont.truetype(fallback_path, fallback_size)

    # Latin run font: for BIN mode, use fallback TTF at fallback_size (like device ofrRenderSize)
    latin_ttf_path = fallback_path if (bin_font and fallback_path) else font_path
    latin_ttf_size = fallback_size if (bin_font and fallback_size) else font_size
    # Device uses &fonts::Font2 (M5Unified built-in bitmap font) at textSize(2)
    # for time, battery %, page numbers, reading %.
    # Approximate with Helvetica which is visually close.
    ui_font = ImageFont.truetype('Helvetica.ttc', 16)
    ui_font_small = ImageFont.truetype('Helvetica.ttc', 12)

    # --- Layout (book_reader.cpp) ---
    # Use nominal font_size for layout so all fonts at same size have same grid
    charHeight = font_size + font_size // 5       # ~1.2x nominal size
    columnSpacing = font_size + font_size // 5    # ~1.2x
    rdLeft = READING_AREA_LEFT; rdRight = READING_AREA_RIGHT
    rdTop = READING_AREA_TOP;   rdMaxY = VERTICAL_TEXT_MAX_Y

    # Optimize: squeeze one more column if leftover space >= 40% of column width
    availW = rdRight - rdLeft
    numCols = availW // columnSpacing
    leftover = availW - numCols * columnSpacing
    if numCols > 0 and leftover * 5 >= columnSpacing * 2:
        numCols += 1
        columnSpacing = availW // numCols

    charsPerColumn = (rdMaxY - rdTop) // charHeight - 1
    # Ensure kinsoku overflow slot stays above progress bar / page numbers
    if rdTop + (charsPerColumn + 1) * charHeight > READING_AREA_BOTTOM:
        charsPerColumn -= 1
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
    # Anchor first row's em-square top at rdTop regardless of font size
    # Use int() for C++-style truncation (not // which floors negative values)
    startY = rdTop - int((charHeight - actual_render_size) / 2)
    bin_scale = actual_render_size / bin_font.font_size if (bin_font and bin_font.font_size > 0) else 1.0
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
                latin_font = ImageFont.truetype(latin_ttf_path, latin_ttf_size)
                lbbox = latin_font.getbbox(latin_run)
                textW = lbbox[2] - lbbox[0]
                spriteW = textW + 4
                spriteH = latin_ttf_size + 4
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
        vOffset = int((charHeight - actual_render_size) / 2)

        if bin_font:
            # BIN mode: look up glyph from BIN file
            glyph = bin_font.get_glyph(mapped)
            if glyph is None and fallback_bin:
                glyph = fallback_bin.get_glyph(mapped)
            if glyph is None:
                glyph = bin_font.get_glyph(unicode_val)
            if glyph is None and fallback_bin:
                glyph = fallback_bin.get_glyph(unicode_val)

            if glyph:
                gw, gh, gbx, gby, glyph_img = glyph
                emY = currentY + int((charHeight - actual_render_size) / 2)  # em-square top
                emX = columnX - actual_render_size // 2                       # em-square left
                # Use bearing offsets for v2 fonts, naive centering for v1
                if bin_font and bin_font.version >= 2:
                    drawX = emX + int(gbx * bin_scale + 0.5)
                    drawY = emY + int(gby * bin_scale + 0.5)
                else:
                    drawX = columnX - gw // 2
                    drawY = emY + (actual_render_size - gh) // 2
                # Paste 1-bit glyph (black on white)
                # Convert to mask: invert so black pixels become the mask
                mask = glyph_img.point(lambda p: 255 if p == 0 else 0)
                black = Image.new('L', (gw, gh), 0)
                img.paste(black, (drawX, drawY), mask)
        else:
            # TTF mode
            draw_char = chr(mapped)
            use_font = font
            if fallback_font:
                tb = draw.textbbox((0, 0), draw_char, font=font)
                if tb[2] - tb[0] <= 0 or tb[3] - tb[1] <= 0:
                    use_font = fallback_font
            tb = draw.textbbox((0, 0), draw_char, font=use_font)
            glyphW = tb[2] - tb[0]
            glyphH = tb[3] - tb[1]
            drawX = columnX - glyphW // 2 - tb[0]
            drawY = currentY + vOffset + (actual_render_size - glyphH) // 2 - tb[1]
            draw.text((drawX, drawY), draw_char, font=use_font, fill=0)
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
                    if bin_font:
                        kg = bin_font.get_glyph(mappedPeek)
                        if kg is None and fallback_bin:
                            kg = fallback_bin.get_glyph(mappedPeek)
                        if kg:
                            kw, kh, kbx, kby, k_img = kg
                            emY = currentY + int((charHeight - actual_render_size) / 2)
                            emX = columnX - actual_render_size // 2
                            if bin_font.version >= 2:
                                kDrawX = emX + int(kbx * bin_scale + 0.5)
                                kDrawY = emY + int(kby * bin_scale + 0.5)
                            else:
                                kDrawX = columnX - kw // 2
                                kDrawY = emY + (actual_render_size - kh) // 2
                            kmask = k_img.point(lambda p: 255 if p == 0 else 0)
                            kblack = Image.new('L', (kw, kh), 0)
                            img.paste(kblack, (kDrawX, kDrawY), kmask)
                    else:
                        k_char = chr(mappedPeek)
                        k_font = font
                        if fallback_font:
                            kb = draw.textbbox((0, 0), k_char, font=font)
                            if kb[2] - kb[0] <= 0 or kb[3] - kb[1] <= 0:
                                k_font = fallback_font
                        kb = draw.textbbox((0, 0), k_char, font=k_font)
                        kW = kb[2] - kb[0]
                        kH = kb[3] - kb[1]
                        vOff = int((charHeight - actual_render_size) / 2)
                        kDrawX = columnX - kW // 2 - kb[0]
                        kDrawY = currentY + vOff + (actual_render_size - kH) // 2 - kb[1]
                        draw.text((kDrawX, kDrawY), k_char, font=k_font, fill=0)
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
    charsPerColumn = (rdMaxY - rdTop) // charHeight - 1
    if rdTop + (charsPerColumn + 1) * charHeight > READING_AREA_BOTTOM:
        charsPerColumn -= 1
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


SAMPLE_TEXT = (
    '酆跎謅天地玄黃，宇宙洪荒。日月盈昃，辰宿列張。'
    '寒來暑往，秋收冬藏。閏餘成歲，律呂調陽。'
    '雲騰致雨，露結為霜。金生麗水，玉出崑岡。'
    '劍號巨闕，珠稱夜光。果珍李柰，菜重芥薑。'
    '海鹹河淡，鱗潛羽翔。龍師火帝，鳥官人皇。'
    '始制文字，乃服衣裳。'
    '弔民伐罪，周發殷湯。坐朝問道，垂拱平章。'
    '愛育黎首，臣伏戎羌。遐邇一體，率賓歸王。'
    '鳴鳳在竹，白駒食場。化被草木，賴及萬方。'
    '\n'
    '蓋此身髮，四大五常。恭惟鞠養，豈敢毀傷。'
    '女慕貞潔，男效才良。知過必改，得能莫忘。'
    '罔談彼短，靡恃己長。信使可覆，器欲難量。'
    '墨悲絲染，詩讚羔羊。景行維賢，克念作聖。'
    '\n'
    '「德建名立，形端表正。」空谷傳聲，虛堂習聽。'
    '禍因惡積，福緣善慶。尺璧非寶，寸陰是競。'
    '資父事君，曰嚴與敬。孝當竭力，忠則盡命。'
    '\n'
    '臨深履薄，夙興溫凊。似蘭斯馨，如松之盛。'
    '川流不息，淵澄取映。容止若思，言辭安定。'
    '篤初誠美，慎終宜令。榮業所基，籍甚無竟。'
    '\n'
    'He saw the boat on the river.'
    '學優登仕，攝職從政。存以甘棠，去而益詠。'
    '樂殊貴賤，禮別尊卑。上和下睦，夫唱婦隨。'
    '外受傅訓，入奉母儀。諸姑伯叔，猶子比兒。'
    '孔懷兄弟，同氣連枝。交友投分，切磨箴規。'
)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Generate sample reading page image')
    parser.add_argument('--bin', '-b', required=True,
                        help='BIN font file path (e.g. sd_card/fonts/Silver_36pt.bin)')
    parser.add_argument('--fallback-bin', default=None,
                        help='Fallback BIN font file for missing glyphs')
    parser.add_argument('--output', '-o', default=None,
                        help='Output PNG path (default: output/sample_<font>_<size>.png)')
    parser.add_argument('--text', '-t', default=None,
                        help='Custom text to render (default: built-in sample)')
    parser.add_argument('--fallback-ttf', default=None,
                        help='Fallback TTF for Latin text runs (auto-detected if not set)')
    args = parser.parse_args()

    bin_path = args.bin
    if not os.path.exists(bin_path):
        print(f"BIN font not found: {bin_path}")
        sys.exit(1)

    os.makedirs('output', exist_ok=True)

    # Load primary BIN font
    print(f"Loading BIN font: {bin_path}")
    bin_font = BinFont(bin_path)
    print(f"  {bin_font.family_name} {bin_font.style_name}, "
          f"{bin_font.char_count} glyphs, fontSize={bin_font.font_size}")

    # Detect Silver mode from font name
    font_base = os.path.splitext(os.path.basename(bin_path))[0]
    silver_mode = 'silver' in font_base.lower()
    font_size = bin_font.font_size  # render size from header

    # For Silver, derive nominal size from render size
    if silver_mode:
        silver_reverse = {44:32, 49:36, 55:40, 61:44, 66:48, 72:52, 77:56, 83:60, 88:64}
        nominal_size = silver_reverse.get(font_size, font_size)
        print(f"  Silver mode: render {font_size}pt → nominal {nominal_size}pt")
    else:
        nominal_size = font_size

    # Load fallback BIN if specified
    fallback_bin = None
    if args.fallback_bin:
        if os.path.exists(args.fallback_bin):
            fallback_bin = BinFont(args.fallback_bin)
            print(f"  Fallback BIN: {args.fallback_bin} ({fallback_bin.char_count} glyphs)")
    else:
        # Auto-detect: look for matching-size BIN from another font family
        bin_dir = os.path.dirname(bin_path) or '.'
        for fb_name in os.listdir(bin_dir):
            if fb_name.endswith('.bin') and fb_name != os.path.basename(bin_path):
                # Match by extracting pt size from filename
                fb_path = os.path.join(bin_dir, fb_name)
                try:
                    fb = BinFont(fb_path)
                    if fb.font_size == font_size:
                        # Skip same family
                        if fb.family_name != bin_font.family_name:
                            fallback_bin = fb
                            print(f"  Auto fallback BIN: {fb_name} ({fb.char_count} glyphs)")
                            break
                except Exception:
                    pass

    # Fallback TTF for Latin runs
    fallback_ttf = args.fallback_ttf
    fallback_ttf_size = nominal_size
    if not fallback_ttf:
        for fb in ['sd_card/fonts/GenYoMinTW-Regular.ttf',
                    'sd_card/fonts/Huiwenmincho-improved.ttf']:
            if os.path.exists(fb):
                fallback_ttf = fb
                break
    if fallback_ttf:
        # For Silver BIN, Latin fallback should match the glyph visual size, not render size
        if silver_mode:
            fallback_ttf_size = nominal_size
        print(f"  Latin TTF: {fallback_ttf} @ {fallback_ttf_size}pt")

    output_path = args.output or f'output/sample_{font_base}.png'
    page_text = args.text if args.text else SAMPLE_TEXT

    render_reading_page(page_text, fallback_ttf or '', nominal_size, output_path,
                        page_num=3, total_pages=500,
                        silver_mode=silver_mode,
                        render_size=font_size if silver_mode else None,
                        fallback_path=fallback_ttf,
                        fallback_size=fallback_ttf_size if fallback_ttf else None,
                        bin_font=bin_font,
                        fallback_bin=fallback_bin)
