#!/usr/bin/env python3
"""Generate a sample reading page using Silver font at scaled size (61px render, 44px grid)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from gen_sample_reading_page import (
    render_reading_page, extract_epub_full_text,
    READING_AREA_TOP, READING_AREA_LEFT, READING_AREA_RIGHT, VERTICAL_TEXT_MAX_Y
)

SILVER_TTF = 'sd_card/fonts/Silver.ttf'
EPUB = 'sd_card/books/pg24113-images-3.epub'
OUTPUT = 'output/silver_sample.png'
FONT_SIZE = 44
RENDER_SIZE = 61  # Silver rendered at 61px to match other fonts

# Monkey-patch the render function to use Silver's scaled rendering
# The key: render at 61px but use 44px grid spacing
from gen_sample_reading_page import render_reading_page as _orig_render
from PIL import Image, ImageDraw, ImageFont

def render_silver_page(text, output_path, page_num, total_pages):
    """Render with Silver at 61px but 44px grid, like the device BIN font does."""
    from gen_sample_reading_page import (
        SCREEN_W, SCREEN_H, toVerticalPunct, isColumnStartProhibited,
        PROGRESS_BAR_X, NAV_Y, NAV_ICON_SIZE, NAV_PREV_X, NAV_NEXT_X,
        NAV_RETURN_X, TOOLBAR_X, TOOLBAR_Y
    )

    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    draw = ImageDraw.Draw(img)

    font = ImageFont.truetype(SILVER_TTF, RENDER_SIZE)
    # Fallback font for vertical punct forms Silver doesn't have
    GENYO_TTF = 'sd_card/fonts/GenYoMinTW-Regular.ttf'
    fallback_font = ImageFont.truetype(GENYO_TTF, FONT_SIZE)
    try:
        ui_font = ImageFont.truetype('Helvetica.ttc', 16)
    except:
        ui_font = ImageFont.load_default()

    # Check which chars need fallback
    from fontTools.ttLib import TTFont
    silver_tt = TTFont(SILVER_TTF)
    silver_cmap = set(silver_tt.getBestCmap().keys())
    silver_tt.close()

    # Layout uses FONT_SIZE (44px) for grid, not RENDER_SIZE
    charHeight = FONT_SIZE + FONT_SIZE // 5
    columnSpacing = FONT_SIZE + FONT_SIZE // 5
    rdLeft = READING_AREA_LEFT
    rdRight = READING_AREA_RIGHT
    rdTop = READING_AREA_TOP
    rdMaxY = VERTICAL_TEXT_MAX_Y

    charsPerColumn = (rdMaxY - rdTop) // charHeight - 2

    # Status bar
    draw.text((8, 4), "00:15", font=ui_font, fill=0)
    bx = SCREEN_W - 46
    by = 12
    bw, bh = 34, 18
    draw.text((bx - 30, 4), "93%", font=ui_font, fill=0)
    draw.rectangle([bx, by, bx + bw, by + bh], outline=0, width=2)
    draw.rectangle([bx + bw, by + 4, bx + bw + 3, by + bh - 4], fill=0)
    draw.rectangle([bx + 3, by + 3, bx + 25, by + bh - 3], fill=0)

    # Vertical text rendering
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

        mapped = toVerticalPunct(unicode_val)

        if unicode_val == 0x0D: continue
        if unicode_val < 0x20 and unicode_val != 0x0A: continue
        if unicode_val == 0x3000: continue
        if unicode_val == 0x20: continue

        # Latin run
        if 0x21 <= unicode_val <= 0x7E:
            has_letter = (0x41 <= unicode_val <= 0x5A) or (0x61 <= unicode_val <= 0x7A)
            if not has_letter:
                peek_i = i
                while peek_i < len(text):
                    pc = ord(text[peek_i])
                    if (0x41 <= pc <= 0x5A) or (0x61 <= pc <= 0x7A):
                        has_letter = True; break
                    if 0x21 <= pc <= 0x7E: peek_i += 1; continue
                    if pc == 0x20 and peek_i + 1 < len(text) and 0x21 <= ord(text[peek_i + 1]) <= 0x7E:
                        peek_i += 1; continue
                    break
            if has_letter:
                i = charStart
                latin_run = ""
                while i < len(text):
                    pc = ord(text[i])
                    if 0x21 <= pc <= 0x7E: latin_run += text[i]; i += 1
                    elif pc == 0x20 and i + 1 < len(text) and 0x21 <= ord(text[i + 1]) <= 0x7E:
                        latin_run += ' '; i += 1
                    else: break
                if not latin_run: continue
                latin_font = ImageFont.truetype(SILVER_TTF, RENDER_SIZE)
                lbbox = latin_font.getbbox(latin_run)
                textW = lbbox[2] - lbbox[0]
                spriteW = textW + 4
                spriteH = RENDER_SIZE + 4
                rotatedH = spriteW
                if currentY + rotatedH > rdMaxY - charHeight:
                    columnX -= columnSpacing; currentY = startY; charIndex = 0
                    if columnX - columnSpacing // 2 < rdLeft:
                        renderStopByte = charStart; i = charStart; break
                sprite = Image.new('L', (spriteW, spriteH), 255)
                sd = ImageDraw.Draw(sprite)
                sd.text((2 - lbbox[0], 2 - lbbox[1]), latin_run, font=latin_font, fill=0)
                rotated = sprite.rotate(-90, expand=True)
                paste_x = columnX - rotated.width // 2
                img.paste(rotated, (paste_x, currentY))
                currentY += rotatedH; charIndex += len(latin_run); charsDrawn += len(latin_run)
                if charIndex >= charsPerColumn or currentY > rdMaxY:
                    columnX -= columnSpacing; currentY = startY; charIndex = 0
                    if columnX - columnSpacing // 2 < rdLeft:
                        renderStopByte = i; break
                continue

        if unicode_val == 0x0A:
            if charIndex > 0:
                columnX -= columnSpacing; currentY = startY; charIndex = 0
                if columnX - columnSpacing // 2 < rdLeft:
                    renderStopByte = i; break
            continue

        # Draw character — use fallback for chars Silver doesn't have
        vOffset = (charHeight - FONT_SIZE) // 2
        draw_char = chr(mapped)

        # Choose font: Silver (scaled) or GenYoMinTW fallback (at FONT_SIZE)
        if mapped in silver_cmap:
            use_font = font  # Silver at RENDER_SIZE
            cell_size = FONT_SIZE
        else:
            use_font = fallback_font  # GenYoMinTW at FONT_SIZE
            cell_size = FONT_SIZE

        # Get glyph bbox to center it
        glyph_bbox = use_font.getbbox(draw_char)
        gw = glyph_bbox[2] - glyph_bbox[0]
        gh = glyph_bbox[3] - glyph_bbox[1]

        # Center within FONT_SIZE cell
        cx = (cell_size - gw) // 2
        cy = (cell_size - gh) // 2
        drawX = columnX - FONT_SIZE // 2 + cx - glyph_bbox[0]
        drawY = currentY + vOffset + cy - glyph_bbox[1]

        draw.text((drawX, drawY), draw_char, font=use_font, fill=0)
        charsDrawn += 1
        currentY += charHeight
        charIndex += 1

        if charIndex >= charsPerColumn or currentY > rdMaxY:
            if i < len(text):
                peekU = ord(text[i])
                mappedP = toVerticalPunct(peekU)
                if isColumnStartProhibited(peekU) or isColumnStartProhibited(mappedP):
                    k_font = font if mappedP in silver_cmap else fallback_font
                    glyph_bbox2 = k_font.getbbox(chr(mappedP))
                    gw2 = glyph_bbox2[2] - glyph_bbox2[0]
                    gh2 = glyph_bbox2[3] - glyph_bbox2[1]
                    cx2 = (FONT_SIZE - gw2) // 2
                    cy2 = (FONT_SIZE - gh2) // 2
                    kDrawX = columnX - FONT_SIZE // 2 + cx2 - glyph_bbox2[0]
                    kDrawY = currentY + (charHeight - FONT_SIZE) // 2 + cy2 - glyph_bbox2[1]
                    draw.text((kDrawX, kDrawY), chr(mappedP), font=k_font, fill=0)
                    charsDrawn += 1; i += 1
            columnX -= columnSpacing; currentY = startY; charIndex = 0
            if columnX - columnSpacing // 2 < rdLeft:
                renderStopByte = i; break

    # Progress bar
    barX = PROGRESS_BAR_X; barY = 878; barW = SCREEN_W - 60; barH = 4
    progress = page_num / max(total_pages - 1, 1)
    fillW = int(barW * progress)
    draw.rectangle([barX, barY, barX + barW, barY + barH], outline=0)
    if fillW > 0:
        draw.rectangle([barX, barY, barX + fillW, barY + barH], fill=0)
    pct_str = f"{int(progress * 100)}%"
    pb = ui_font.getbbox(pct_str)
    draw.text((barX + barW - (pb[2] - pb[0]), barY - 6 - (pb[3] - pb[1])), pct_str, font=ui_font, fill=0)
    page_str = f"{page_num + 1}/{total_pages}"
    pb2 = ui_font.getbbox(page_str)
    draw.text((barX, barY - 6 - (pb2[3] - pb2[1])), page_str, font=ui_font, fill=0)

    # Toolbar
    toolbar_path = 'assets/icons/reader_toolbar.png'
    if os.path.exists(toolbar_path):
        tb_img = Image.open(toolbar_path).convert('L')
        img.paste(tb_img, (TOOLBAR_X, TOOLBAR_Y))
    cellW = 52
    sizeX = TOOLBAR_X + cellW + cellW // 2
    sizeY = TOOLBAR_Y + 25
    sb = ui_font.getbbox(str(FONT_SIZE))
    draw.text((sizeX - (sb[2] - sb[0]) // 2, sizeY - (sb[3] - sb[1]) // 2), str(FONT_SIZE), font=ui_font, fill=0)

    # Nav icons
    for icon_name, ix in [('back.png', NAV_PREV_X), ('next.png', NAV_NEXT_X)]:
        icon_path = f'assets/icons/{icon_name}'
        if os.path.exists(icon_path):
            ic = Image.open(icon_path)
            if ic.mode == 'RGBA':
                bg = Image.new('RGBA', ic.size, (255,255,255,255))
                ic = Image.alpha_composite(bg, ic)
            img.paste(ic.convert('L'), (ix, NAV_Y))
    ret_path = 'assets/icons/return.png'
    if os.path.exists(ret_path):
        ic = Image.open(ret_path)
        if ic.mode == 'RGBA':
            bg = Image.new('RGBA', ic.size, (255,255,255,255))
            ic = Image.alpha_composite(bg, ic)
        img.paste(ic.convert('L'), (NAV_RETURN_X, NAV_Y))

    img.save(output_path)
    print(f"Saved: {output_path}")
    print(f"  Silver {RENDER_SIZE}px render in {FONT_SIZE}px grid")
    print(f"  Chars drawn: {charsDrawn}")

if __name__ == '__main__':
    os.makedirs('output', exist_ok=True)
    print(f"Extracting text from {EPUB}...")
    full_text = extract_epub_full_text(EPUB)
    print(f"Total text: {len(full_text)} chars")

    # Use a representative passage
    search_str = '\u5f97\u77e5\uff0c\u4eca\u65e5\u56de\u5bb6'
    page_start = full_text.find(search_str)
    if page_start < 0:
        page_start = 5000  # fallback
    page_text = full_text[page_start:]
    render_silver_page(page_text, OUTPUT, 56, 804)
