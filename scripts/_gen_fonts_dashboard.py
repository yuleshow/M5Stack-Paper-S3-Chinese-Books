#!/usr/bin/env python3
"""Generate a side-by-side fonts dashboard comparison image.

Renders the GenYoMinTW dashboard programmatically using icons + TTF font,
then places it side-by-side with the pre-rendered Silver dashboard cover.
"""
import os
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# --- Dashboard layout (mirrors dashboard.cpp layoutIcons) ---
DISP_W, DISP_H = 540, 960
MARGIN = 20
GAP = 12
COLS, ROWS = 2, 4
ICON_W = (DISP_W - MARGIN * 2 - GAP * (COLS - 1)) // COLS  # 244
ICON_H = (DISP_H - MARGIN * 2 - GAP * (ROWS - 1)) // ROWS  # 221

LABELS = ["電子書", "日曆", "待辦事項", "採辦", "天氣", "工具", "設定", "求籖"]

FONT_PATH = 'sd_card/fonts/GenYoMinTW-Regular.ttf'
ICON_DIR = 'sd_card/icons'

def render_dashboard():
    """Render the GenYoMinTW dashboard to a PIL Image."""
    img = Image.new('L', (DISP_W, DISP_H), 255)  # grayscale white
    draw = ImageDraw.Draw(img)

    label_font = ImageFont.truetype(FONT_PATH, 32)

    for i in range(8):
        row, col = divmod(i, COLS)
        # Swap row/col: i=0 → row0,col0; i=1 → row0,col1; etc.
        col, row = i % COLS, i // COLS
        cx = MARGIN + col * (ICON_W + GAP)
        cy = MARGIN + row * (ICON_H + GAP)

        # Load and paste icon centered in cell (above label area)
        icon_path = os.path.join(ICON_DIR, f'icon{i + 1}.png')
        if os.path.exists(icon_path):
            icon = Image.open(icon_path).convert('L')
            iw, ih = icon.size
            # Center icon in cell, leaving 20px at bottom for label
            ix = cx + (ICON_W - iw) // 2
            iy = cy + (ICON_H - 20 - ih) // 2
            img.paste(icon, (ix, iy))

        # Draw label centered below icon
        label = LABELS[i]
        bbox = draw.textbbox((0, 0), label, font=label_font)
        tw = bbox[2] - bbox[0]
        lx = cx + (ICON_W - tw) // 2
        ly = cy + ICON_H - 30
        draw.text((lx, ly), label, fill=0, font=label_font)

    return img.convert('RGB')


# --- Generate left (GenYoMinTW) dashboard ---
gen = render_dashboard()

# --- Load right (Silver) dashboard from pre-rendered cover ---
sil = Image.open('assets/silver-cover-2.png').convert('RGB')
sil = sil.resize((DISP_W, DISP_H), Image.LANCZOS)

# --- Compose side-by-side ---
W, H = DISP_W, DISP_H
gap = 30
label_h = 60
border = 2

canvas_w = W * 2 + gap + 40
canvas_h = H + label_h + 30
canvas = Image.new('RGB', (canvas_w, canvas_h), (255, 255, 255))
draw = ImageDraw.Draw(canvas)

# Header font
header_font = None
for fp in [
    '/System/Library/Fonts/PingFang.ttc',
    '/System/Library/Fonts/STHeiti Light.ttc',
    '/System/Library/Fonts/Helvetica.ttc',
]:
    if os.path.exists(fp):
        try:
            header_font = ImageFont.truetype(fp, 32)
            break
        except Exception:
            pass
if not header_font:
    header_font = ImageFont.load_default()

x_left = 20
x_right = 20 + W + gap

def draw_centered_label(text, cx, y):
    bbox = draw.textbbox((0, 0), text, font=header_font)
    tw = bbox[2] - bbox[0]
    draw.text((cx - tw // 2, y), text, fill=(0, 0, 0), font=header_font)

draw_centered_label("GenYoMinTW 源樣明體", x_left + W // 2, 8)
draw_centered_label("Silver 像素字型", x_right + W // 2, 8)

y_top = label_h
for x in [x_left, x_right]:
    draw.rectangle(
        [x - border, y_top - border, x + W + border - 1, y_top + H + border - 1],
        outline=(200, 200, 200), width=border,
    )

canvas.paste(gen, (x_left, y_top))
canvas.paste(sil, (x_right, y_top))

out_path = 'output/fonts_dashboard.png'
canvas.save(out_path, optimize=True)
print(f"Saved {out_path} ({canvas_w}x{canvas_h})")
