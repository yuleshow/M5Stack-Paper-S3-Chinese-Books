#!/usr/bin/env python3
"""Preview fortune slip wording/story pages with Silver font (enlarged).

Generates multi-page previews showing how the device renders with Silver at size 40
vs the default GenYoMinTW at size 26/28.
"""
import csv
import os
import sys
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

W, H = 540, 960
LIGHT_GRAY = 200
DARK_GRAY = 100

SILVER_PATH = 'sd_card/fonts/Silver.ttf'
GENYO_PATH = 'sd_card/fonts/GenYoMinTW-Regular.ttf'
KAI_PATH = 'sd_card/fonts/TW-Kai-98_1.ttf'

# Silver scale table (matches device-side)
SILVER_SCALE = {
    16: 23, 18: 25, 20: 27, 22: 29, 24: 32, 26: 35,
    28: 39, 32: 44, 34: 47, 36: 49, 38: 53, 40: 58, 64: 90
}

def silver_scaled(size):
    return SILVER_SCALE.get(size, int(size * 1.38 + 0.5))


def wrap_text(text, font, max_width):
    lines = []
    current = ""
    for ch in text:
        test = current + ch
        if font.getlength(test) > max_width and current:
            lines.append(current)
            current = ch
        else:
            current = test
    if current:
        lines.append(current)
    return lines


def draw_vertical_poem(draw, poem_text, poem_font, start_y):
    poem_lines = [l for l in poem_text.split('\n') if l.strip()]
    if not poem_lines:
        return start_y
    max_chars = max(len(l) for l in poem_lines)
    if max_chars < 1:
        max_chars = 5
    char_spacing, col_spacing = 52, 56
    poem_h = max_chars * char_spacing
    poem_w = len(poem_lines) * col_spacing
    poem_left = (W - poem_w) // 2

    for col_idx, line in enumerate(poem_lines):
        cx = poem_left + poem_w - col_spacing // 2 - col_idx * col_spacing
        for row_idx, char in enumerate(line):
            cy = start_y + row_idx * char_spacing + char_spacing // 2
            draw.text((cx, cy), char, fill=0, font=poem_font, anchor="mm")

    return start_y + poem_h


def render_sensoji_silver(fields, slip_idx):
    """Render multi-page Sensoji wording with Silver font (same layout as GenYoMinTW). Returns list of images."""
    pages = []
    text_size = 26
    cat_size = 24
    label_size = 26
    render_text = silver_scaled(text_size)
    render_cat = silver_scaled(cat_size)
    render_label = silver_scaled(label_size)
    margin = 30
    max_w = 480

    silver_text = ImageFont.truetype(SILVER_PATH, render_text)
    silver_cat = ImageFont.truetype(SILVER_PATH, render_cat)
    silver_label = ImageFont.truetype(SILVER_PATH, render_label)
    title_font = ImageFont.truetype(SILVER_PATH, silver_scaled(32))
    kai_font = ImageFont.truetype(KAI_PATH, 48) if os.path.exists(KAI_PATH) else None
    poem_font = kai_font or ImageFont.truetype(SILVER_PATH, 48)

    title = f"{fields[0]}  {fields[1]}"

    # ---- PAGE 1: Title + Poem + 詩意 ----
    img1 = Image.new('L', (W, H), 255)
    d1 = ImageDraw.Draw(img1)

    bbox = d1.textbbox((0, 0), title, font=title_font)
    tw = bbox[2] - bbox[0]
    d1.text((W // 2 - tw // 2, 45), title, fill=0, font=title_font)
    d1.line([(40, 82), (500, 82)], fill=LIGHT_GRAY)

    poem_end_y = draw_vertical_poem(d1, fields[2], poem_font, 88)

    sy = poem_end_y + 20
    d1.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
    sy += 8
    d1.text((margin, sy), "【詩意】", fill=DARK_GRAY, font=silver_label)
    sy += 32
    line_h = text_size + 6
    explanation_lines = wrap_text(fields[3], silver_text, max_w)
    overflow_line_idx = None
    for i, line in enumerate(explanation_lines):
        if sy > 855:
            overflow_line_idx = i
            break
        d1.text((margin, sy), line, fill=0, font=silver_text)
        sy += line_h

    has_more = overflow_line_idx is not None

    # Categories on page 1 if text fits
    if not has_more:
        cat_labels = ["願望", "疾病", "遺失物", "盼望的人", "蓋新居搬家",
                      "結婚交往", "旅行"]
        cat_values = fields[4:11]
        if any(v for v in cat_values) and sy < 850:
            sy += 4
            d1.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
            sy += 8
            cat_cell_h = 32
            for label, value in zip(cat_labels, cat_values):
                if not value or sy > 855:
                    continue
                cell = f"{label}：{value}"
                d1.text((margin, sy), cell, fill=0, font=silver_cat)
                sy += cat_cell_h

    pages.append(img1)

    # ---- Additional pages for overflow ----
    remaining_idx = overflow_line_idx
    while remaining_idx is not None and remaining_idx < len(explanation_lines):
        img = Image.new('L', (W, H), 255)
        d = ImageDraw.Draw(img)

        d.text((W // 2 - tw // 2, 45), title, fill=0, font=title_font)
        d.line([(40, 82), (500, 82)], fill=LIGHT_GRAY)

        sy = 90
        d.text((margin, sy), "【詩意】", fill=DARK_GRAY, font=silver_label)
        sy += 32
        next_overflow = None
        for i in range(remaining_idx, len(explanation_lines)):
            if sy > 855:
                next_overflow = i
                break
            d.text((margin, sy), explanation_lines[i], fill=0, font=silver_text)
            sy += line_h
        sy += 4

        # Categories if all text done
        if next_overflow is None:
            cat_labels = ["願望", "疾病", "遺失物", "盼望的人", "蓋新居搬家",
                          "結婚交往", "旅行"]
            cat_values = fields[4:11]
            if any(v for v in cat_values) and sy < 850:
                d.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
                sy += 8
                cat_cell_h = 32
                for label, value in zip(cat_labels, cat_values):
                    if not value or sy > 855:
                        continue
                    cell = f"{label}：{value}"
                    d.text((margin, sy), cell, fill=0, font=silver_cat)
                    sy += cat_cell_h

        pages.append(img)
        remaining_idx = next_overflow

    # Add page numbers
    if len(pages) > 1:
        sm_font = ImageFont.truetype(SILVER_PATH, 20)
        for i, pg in enumerate(pages):
            d = ImageDraw.Draw(pg)
            d.text((W // 2 - 30, H - 40), f"{i+1} / {len(pages)}", fill=DARK_GRAY, font=sm_font)

    return pages


def render_sensoji_default(fields):
    """Render single-page Sensoji wording with GenYoMinTW (original layout)."""
    img = Image.new('L', (W, H), 255)
    draw = ImageDraw.Draw(img)

    font_path = GENYO_PATH
    title_font = ImageFont.truetype(font_path, 32)
    section_font = ImageFont.truetype(font_path, 26)
    cat_font = ImageFont.truetype(font_path, 24)
    label_font = ImageFont.truetype(font_path, 26)
    kai_font = ImageFont.truetype(KAI_PATH, 48) if os.path.exists(KAI_PATH) else None
    poem_font = kai_font or ImageFont.truetype(font_path, 48)

    title = f"{fields[0]}  {fields[1]}"
    bbox = draw.textbbox((0, 0), title, font=title_font)
    tw = bbox[2] - bbox[0]
    draw.text((W // 2 - tw // 2, 45), title, fill=0, font=title_font)
    draw.line([(40, 82), (500, 82)], fill=LIGHT_GRAY)

    poem_end_y = draw_vertical_poem(draw, fields[2], poem_font, 88)

    sy = poem_end_y + 18
    draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
    sy += 6
    draw.text((30, sy), "【詩意】", fill=DARK_GRAY, font=label_font)
    sy += 30
    lines = wrap_text(fields[3], section_font, 480)
    for line in lines:
        if sy > 920:
            break
        draw.text((30, sy), line, fill=0, font=section_font)
        sy += 30
    sy += 4

    cat_labels = ["願望", "疾病", "遺失物", "盼望的人", "蓋新居搬家",
                  "結婚交往", "旅行"]
    cat_values = fields[4:11]
    if any(v for v in cat_values) and sy < 920:
        draw.line([(40, sy), (500, sy)], fill=LIGHT_GRAY)
        sy += 6
        for label, value in zip(cat_labels, cat_values):
            if not value or sy > 930:
                continue
            cell = f"{label}：{value}"
            draw.text((30, sy), cell, fill=0, font=cat_font)
            sy += 28

    return img


VERT_PUNCT_MAP = {
    0x300C: 0xFE41, 0x300D: 0xFE42, 0x201C: 0xFE41, 0x201D: 0xFE42,
    0x3008: 0xFE3F, 0x3009: 0xFE40, 0x300E: 0xFE43, 0x300F: 0xFE44,
    0x300A: 0xFE3D, 0x300B: 0xFE3E, 0x3010: 0xFE3B, 0x3011: 0xFE3C,
    0xFF08: 0xFE35, 0xFF09: 0xFE36, 0x3014: 0xFE39, 0x3015: 0xFE3A,
    0xFF5B: 0xFE37, 0xFF5D: 0xFE38, 0xFF3B: 0xFE47, 0xFF3D: 0xFE48,
    0x2026: 0xFE19, 0x2025: 0xFE30, 0x2014: 0xFE31,
}
COLUMN_START_PROHIBITED = {
    0x300D, 0x300F, 0x300B, 0x3009, 0x3011, 0x3015, 0x3017,
    0xFF09, 0xFF5D, 0xFF3D, 0x201D, 0x2019,
    0x3002, 0xFF0C, 0x3001, 0xFF1B, 0xFF1A, 0xFF01, 0xFF1F,
    0x30FB, 0x2026, 0x2025, 0x2014,
    0xFE42, 0xFE44, 0xFE3E, 0xFE40, 0xFE3C, 0xFE3A, 0xFE18,
    0xFE36, 0xFE38, 0xFE48, 0xFE19, 0xFE30, 0xFE31,
    0x002C, 0x002E, 0x003F, 0x0021, 0x003B, 0x003A, 0x0029, 0x005D,
}


def render_kuanyin_story_vertical(title, body, font_path, is_silver=False):
    """Render multi-page Kuanyin story with vertical text (like epub reading). Returns list of images."""
    pages = []
    font_size = 44
    render_size = silver_scaled(font_size) if is_silver else font_size
    char_height = font_size + font_size // 5  # 52
    col_spacing = char_height

    rd_left, rd_right = 20, 530
    rd_top, rd_max_y = 65, 878

    # Squeeze extra column
    avail_w = rd_right - rd_left
    nc = avail_w // col_spacing
    leftover = avail_w - nc * col_spacing
    if nc > 0 and leftover * 5 >= col_spacing * 2:
        nc += 1
        col_spacing = avail_w // nc
    chars_per_col = (rd_max_y - rd_top) // char_height
    if rd_top + chars_per_col * char_height > rd_max_y:
        chars_per_col -= 1
    if chars_per_col < 1:
        chars_per_col = 1

    text_font = ImageFont.truetype(font_path, render_size)
    title_font = ImageFont.truetype(font_path, render_size)

    # Paginate
    chars = list(body)
    char_idx_global = 0
    page_num = 0

    while char_idx_global < len(chars):
        img = Image.new('L', (W, H), 255)
        d = ImageDraw.Draw(img)

        col_x = rd_right - col_spacing // 2

        # Page 0: draw title vertically in rightmost column, centered
        if page_num == 0:
            title_chars = list(title)
            title_h = len(title_chars) * char_height
            title_start_y = rd_top + (rd_max_y - rd_top - title_h) // 2
            if title_start_y < rd_top:
                title_start_y = rd_top
            for ch in title_chars:
                cp = ord(ch)
                cp = VERT_PUNCT_MAP.get(cp, cp)
                d.text((col_x, title_start_y + (char_height - font_size) // 2),
                       chr(cp), fill=0, font=title_font, anchor="mt")
                title_start_y += char_height
            # Vertical divider
            div_x = col_x - col_spacing // 2 - 4
            d.line([(div_x, rd_top), (div_x, rd_max_y)], fill=LIGHT_GRAY)
            col_x -= col_spacing

        # Vertical columns for body
        cur_y = rd_top
        char_idx = 0

        while char_idx_global < len(chars):
            ch = chars[char_idx_global]
            cp = ord(ch)

            if ch == '\r':
                char_idx_global += 1
                continue
            if cp < 0x20 and ch != '\n':
                char_idx_global += 1
                continue

            if ch == '\n':
                char_idx_global += 1
                if char_idx > 0:
                    col_x -= col_spacing
                    cur_y = rd_top
                    char_idx = 0
                    if col_x - col_spacing // 2 < rd_left:
                        break
                continue

            # Skip ideographic space at column start
            if (cp == 0x3000 or ch == ' ') and char_idx == 0:
                char_idx_global += 1
                continue

            # Vertical punctuation
            cp = VERT_PUNCT_MAP.get(cp, cp)
            draw_ch = chr(cp)

            d.text((col_x, cur_y + (char_height - font_size) // 2), draw_ch,
                   fill=0, font=text_font, anchor="mt")
            char_idx_global += 1
            cur_y += char_height
            char_idx += 1

            if char_idx >= chars_per_col or cur_y + char_height > rd_max_y:
                # Kinsoku
                if char_idx_global < len(chars):
                    peek_cp = ord(chars[char_idx_global])
                    mapped = VERT_PUNCT_MAP.get(peek_cp, peek_cp)
                    if peek_cp in COLUMN_START_PROHIBITED or mapped in COLUMN_START_PROHIBITED:
                        d.text((col_x, cur_y + (char_height - font_size) // 2),
                               chr(mapped), fill=0, font=text_font, anchor="mt")
                        char_idx_global += 1

                col_x -= col_spacing
                cur_y = rd_top
                char_idx = 0
                if col_x - col_spacing // 2 < rd_left:
                    break

        pages.append(img)
        page_num += 1

    # Page numbers
    if len(pages) > 1:
        sm_font = ImageFont.truetype(font_path, 20)
        for i, pg in enumerate(pages):
            d = ImageDraw.Draw(pg)
            d.text((W // 2 - 30, H - 40), f"{i+1} / {len(pages)}", fill=DARK_GRAY, font=sm_font)

    return pages


def main():
    # ---- Sensoji ----
    csv_path = 'assets/Fortune_Slips/senso-ji/sensoji.csv'
    if not os.path.exists(csv_path):
        print(f"ERROR: {csv_path} not found")
        sys.exit(1)

    with open(csv_path, 'r', encoding='utf-8-sig') as f:
        reader = csv.reader(f)
        header = next(reader)
        rows = [r for r in reader if r and r[0].strip()]

    # Pick slip #31 (index 30) — longest explanation text
    field_indices = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    row = rows[30]
    fields = [row[i].strip() if i < len(row) else "" for i in field_indices]

    os.makedirs('output', exist_ok=True)

    # Render default (GenYoMinTW) version
    if os.path.exists(GENYO_PATH):
        default_img = render_sensoji_default(fields)
        default_img.save('output/sensoji_wording_default.png')
        print(f"Default (GenYoMinTW): output/sensoji_wording_default.png")

    # Render Silver version (multi-page)
    if os.path.exists(SILVER_PATH):
        silver_pages = render_sensoji_silver(fields, 0)
        for i, page in enumerate(silver_pages):
            path = f'output/sensoji_wording_silver_p{i+1}.png'
            page.save(path)
            print(f"Silver page {i+1}: {path}")
        print(f"  Silver: {len(silver_pages)} page(s)")
    else:
        print(f"WARNING: Silver font not found at {SILVER_PATH}")

    # ---- Kuanyin story ----
    kuanyin_csv = 'assets/Fortune_Slips/kuanyin/kuanyin.csv'
    if os.path.exists(kuanyin_csv) and os.path.exists(SILVER_PATH):
        with open(kuanyin_csv, 'r', encoding='utf-8-sig') as f:
            reader = csv.reader(f)
            header = next(reader)
            krows = [r for r in reader if r and r[0].strip()]
        # Find a slip with a long story — pick slip #1 (index 0) or longest
        best_idx = 0
        best_len = 0
        for i, r in enumerate(krows):
            if len(r) > 9 and len(r[9]) > best_len:
                best_len = len(r[9])
                best_idx = i
        kr = krows[best_idx]
        story_title = kr[8].strip() if len(kr) > 8 else "故事"
        story_body = kr[9].strip() if len(kr) > 9 else ""
        if story_body:
            # Silver vertical
            story_pages = render_kuanyin_story_vertical(story_title, story_body, SILVER_PATH, is_silver=True)
            for i, page in enumerate(story_pages):
                path = f'output/kuanyin_story_silver_p{i+1}.png'
                page.save(path)
                print(f"Kuanyin story Silver page {i+1}: {path}")
            print(f"  Kuanyin story Silver: {len(story_pages)} page(s), slip #{best_idx+1} ({best_len} chars)")

            # Default (GenYoMinTW) vertical
            if os.path.exists(GENYO_PATH):
                def_pages = render_kuanyin_story_vertical(story_title, story_body, GENYO_PATH, is_silver=False)
                for i, page in enumerate(def_pages):
                    path = f'output/kuanyin_story_default_p{i+1}.png'
                    page.save(path)
                    print(f"Kuanyin story Default page {i+1}: {path}")
                print(f"  Kuanyin story Default: {len(def_pages)} page(s)")


if __name__ == "__main__":
    main()
