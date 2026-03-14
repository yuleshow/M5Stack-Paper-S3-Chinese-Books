#!/usr/bin/env python3
"""Generate a side-by-side calendar comparison for March 20, 2026
using GenYoMinTW and Silver fonts, matching the firmware's layout."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from convert_labels import render_label, FONT_PATH
from PIL import Image, ImageDraw, ImageFont, ImageOps

SILVER_PATH = os.path.join("sd_card", "fonts", "Silver.ttf")
GENYO_PATH = FONT_PATH  # ~/Library/Fonts/GenYoMinTW-Regular.ttf

# Per-size scale factors for Silver
SILVER_SCALE = {
    16: 23, 18: 25, 20: 27, 22: 29, 24: 32, 26: 35,
    28: 39, 30: 41, 32: 44, 34: 47, 36: 49, 38: 53, 40: 58, 160: 229,
}

W = 540   # Display width
H = 960   # Display height
WHITE = 255
BLACK = 0
GRAY = 140
LIGHT_GRAY = 200

def load_font(path, size):
    return ImageFont.truetype(path, size)

def _get_font(size, font_cache, font_path, scale):
    """Resolve actual size (applying Silver scale) and return (font, actual_size)."""
    actual_size = size
    if scale and size in scale:
        actual_size = scale[size]
    elif scale:
        actual_size = int(size * 1.38 + 0.5)
    if (font_path, actual_size) not in font_cache:
        font_cache[(font_path, actual_size)] = load_font(font_path, actual_size)
    return font_cache[(font_path, actual_size)], actual_size

def draw_text(img, text, x, y, size, font_cache, font_path, color=BLACK, scale=None):
    """Draw text at (x, y) using Pillow's native text renderer (baseline-aligned).
    Returns the advance width."""
    font, actual_size = _get_font(size, font_cache, font_path, scale)
    draw = ImageDraw.Draw(img)
    fill = color
    # For WHITE-on-black, draw directly
    draw.text((x, y), text, font=font, fill=fill)
    # Measure width via textbbox
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox[2] - bbox[0]

def draw_text_centered(img, text, cx, y, size, font_cache, font_path, color=BLACK, scale=None):
    font, actual_size = _get_font(size, font_cache, font_path, scale)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    x = cx - tw // 2
    draw.text((x, y), text, font=font, fill=color)
    return tw

def draw_number_centered(img, number, cx, y, size, font_cache, font_path, scale=None):
    text = str(number)
    draw_text_centered(img, text, cx, y, size, font_cache, font_path, BLACK, scale)

def render_calendar(font_path, scale=None):
    """Render a calendar page for March 20, 2026."""
    img = Image.new("L", (W, H), WHITE)
    draw = ImageDraw.Draw(img)
    fc = {}  # font cache
    
    centerX = W // 2
    
    # ── Year ──
    draw_number_centered(img, 2026, centerX, 10, 32, fc, font_path, scale)
    
    # ── Month ──
    xp = centerX - 30
    xp += draw_text(img, "3", xp, 45, 26, fc, font_path, scale=scale)
    draw_text(img, "月", xp, 45, 26, fc, font_path, scale=scale)
    
    # Horizontal line
    draw.line([(20, 80), (W - 20, 80)], fill=BLACK)
    
    # ── Large date ──
    draw_number_centered(img, 20, centerX, 90, 160, fc, font_path, scale)
    
    # Left: Year GanZhi (丙午年, 馬年) - 2026 is 丙午 Horse
    xp = 12
    xp += draw_text(img, "丙", xp, 92, 40, fc, font_path, scale=scale)
    xp += draw_text(img, "午", xp, 92, 40, fc, font_path, scale=scale)
    draw_text(img, "年", xp, 92, 40, fc, font_path, scale=scale)
    xp = 12
    xp += draw_text(img, "馬", xp, 138, 40, fc, font_path, scale=scale)
    draw_text(img, "年", xp, 138, 40, fc, font_path, scale=scale)
    
    # Right: Solar term - 春分 is March 20
    draw_text(img, "春分", W - 150, 92, 34, fc, font_path, color=GRAY, scale=scale)
    
    # ── Divider ──
    draw.line([(20, 265), (W - 20, 265)], fill=BLACK)
    
    # ── Dark banner: lunar date + weekday ──
    draw.rectangle([(0, 270), (W, 325)], fill=BLACK)
    # Lunar: 二月初二 (March 20, 2026 = lunar 二月初二)
    xp = 20
    xp += draw_text(img, "二月", xp, 282, 30, fc, font_path, color=WHITE, scale=scale)
    draw_text(img, "初二", xp, 282, 30, fc, font_path, color=WHITE, scale=scale)
    # Weekday: Friday (March 20, 2026 is a Friday)
    draw_text(img, "星期五", W - 160, 282, 30, fc, font_path, color=WHITE, scale=scale)
    
    # Festival line
    xp = 20
    xp += draw_text(img, "民俗", xp, 333, 20, fc, font_path, color=GRAY, scale=scale)
    xp += 2
    xp += draw_text(img, "龍抬頭", xp, 333, 20, fc, font_path, scale=scale)
    
    # Lunar year info line
    xp = 50
    xp += draw_text(img, "農曆", xp, 360, 30, fc, font_path, scale=scale)
    xp += draw_text(img, "丙午", xp, 360, 30, fc, font_path, scale=scale)
    xp += draw_text(img, "馬", xp, 360, 30, fc, font_path, scale=scale)
    xp += draw_text(img, "年", xp, 360, 30, fc, font_path, scale=scale)
    xp += 6
    xp += draw_text(img, "辛卯", xp, 360, 30, fc, font_path, scale=scale)
    xp += draw_text(img, "月", xp, 360, 30, fc, font_path, scale=scale)
    xp += 6
    xp += draw_text(img, "庚辰", xp, 360, 30, fc, font_path, scale=scale)
    xp += draw_text(img, "日", xp, 360, 30, fc, font_path, scale=scale)
    xp += 6
    draw_text(img, "小", xp, 360, 30, fc, font_path, scale=scale)
    
    # ── Divider ──
    draw.line([(20, 390), (W - 20, 390)], fill=BLACK)
    
    # ── Info box ──
    secY = 400
    draw.rectangle([(18, secY), (W - 18, secY + 210)], outline=BLACK)
    
    # GanZhi pillars
    draw_text(img, "年柱：", 30, secY + 5, 22, fc, font_path, scale=scale)
    draw_text(img, "丙午", 100, secY + 5, 22, fc, font_path, scale=scale)
    draw_text(img, "天河水", 155, secY + 5, 22, fc, font_path, scale=scale)
    
    draw_text(img, "月柱：", 30, secY + 32, 22, fc, font_path, scale=scale)
    draw_text(img, "辛卯", 100, secY + 32, 22, fc, font_path, scale=scale)
    draw_text(img, "松柏木", 155, secY + 32, 22, fc, font_path, scale=scale)
    
    draw_text(img, "日柱：", 30, secY + 59, 22, fc, font_path, scale=scale)
    draw_text(img, "庚辰", 100, secY + 59, 22, fc, font_path, scale=scale)
    draw_text(img, "白蠟金", 155, secY + 59, 22, fc, font_path, scale=scale)
    
    # Right column
    draw_text(img, "六曜：", 300, secY + 5, 22, fc, font_path, scale=scale)
    draw_text(img, "友引", 370, secY + 5, 22, fc, font_path, scale=scale)
    
    xp = 300
    xp += draw_text(img, "沖狗", xp, secY + 32, 22, fc, font_path, scale=scale)
    xp += 6
    draw_text(img, "煞南", xp, secY + 32, 22, fc, font_path, scale=scale)
    
    draw_text(img, "節氣：", 300, secY + 59, 22, fc, font_path, scale=scale)
    draw_text(img, "今日", 370, secY + 59, 22, fc, font_path, color=GRAY, scale=scale)
    draw_text(img, "春分", 420, secY + 59, 22, fc, font_path, color=GRAY, scale=scale)
    
    # Vertical/horizontal dividers
    draw.line([(290, secY), (290, secY + 85)], fill=BLACK)
    draw.line([(18, secY + 86), (W - 18, secY + 86)], fill=BLACK)
    
    # 喜神 福神 財神
    draw_text(img, "喜神：", 30, secY + 92, 22, fc, font_path, scale=scale)
    draw_text(img, "西南", 100, secY + 92, 22, fc, font_path, scale=scale)
    draw_text(img, "福神：", 200, secY + 92, 22, fc, font_path, scale=scale)
    draw_text(img, "西北", 270, secY + 92, 22, fc, font_path, scale=scale)
    draw_text(img, "財神：", 370, secY + 92, 22, fc, font_path, scale=scale)
    draw_text(img, "正東", 440, secY + 92, 22, fc, font_path, scale=scale)
    
    draw.line([(18, secY + 120), (W - 18, secY + 120)], fill=BLACK)
    
    # 胎神
    draw_text(img, "胎神：", 30, secY + 126, 22, fc, font_path, scale=scale)
    draw_text(img, "佔碓磨外正南", 100, secY + 126, 22, fc, font_path, scale=scale)
    
    # 彭祖百忌
    draw_text(img, "彭祖：", 30, secY + 160, 22, fc, font_path, scale=scale)
    xp = 100
    xp += draw_text(img, "庚不經絡", xp, secY + 160, 22, fc, font_path, scale=scale)
    xp += 6
    draw_text(img, "辰不哭泣", xp, secY + 160, 22, fc, font_path, scale=scale)
    
    secY += 220
    
    # ── 宜 ──
    draw.rectangle([(18, secY), (78, secY + 55)], fill=BLACK)
    draw_text_centered(img, "宜", 48, secY + 10, 34, fc, font_path, color=WHITE, scale=scale)
    draw_text(img, "祈福 出行 納采 嫁娶", 90, secY + 8, 32, fc, font_path, scale=scale)
    
    secY += 65
    
    # ── 忌 ──
    draw.rectangle([(18, secY), (78, secY + 55)], fill=BLACK)
    draw_text_centered(img, "忌", 48, secY + 10, 34, fc, font_path, color=WHITE, scale=scale)
    draw_text(img, "動土 破土 安葬 開光", 90, secY + 8, 32, fc, font_path, scale=scale)
    
    secY += 65
    
    # ── Divider ──
    draw.line([(20, secY), (W - 20, secY)], fill=BLACK)
    secY += 5
    
    # ── 時辰吉凶 ──
    draw_text(img, "時辰吉凶", 20, secY, 24, fc, font_path, scale=scale)
    secY += 32
    
    hours_gz = ["丙子", "丁丑", "戊寅", "己卯", "庚辰", "辛巳",
                "壬午", "癸未", "甲申", "乙酉", "丙戌", "丁亥"]
    hours_jx = ["吉", "凶", "吉", "吉", "凶", "吉", "凶", "吉", "凶", "吉", "吉", "凶"]
    
    colW = (W - 20) // 12
    for idx in range(12):
        hx = 10 + idx * colW + colW // 2
        draw_text_centered(img, hours_gz[idx], hx, secY, 20, fc, font_path, scale=scale)
        jx_color = BLACK if hours_jx[idx] == "吉" else GRAY
        draw_text_centered(img, hours_jx[idx], hx, secY + 26, 20, fc, font_path, color=jx_color, scale=scale)
    
    return img


# ── Main ──
print("Rendering GenYoMinTW calendar...")
img_genyo = render_calendar(GENYO_PATH, scale=None)
print("Rendering Silver calendar (scaled)...")
img_silver = render_calendar(SILVER_PATH, scale=SILVER_SCALE)

# Compose side by side with labels
GAP = 30
HEADER_H = 40
total_w = W * 2 + GAP
total_h = H + HEADER_H

out = Image.new("L", (total_w, total_h), WHITE)
header_draw = ImageDraw.Draw(out)

# Headers
hf = ImageFont.truetype(GENYO_PATH, 24)
header_draw.text((W // 2 - 80, 8), "GenYoMinTW", font=hf, fill=BLACK)
header_draw.text((W + GAP + W // 2 - 40, 8), "Silver", font=hf, fill=BLACK)

# Separator line
header_draw.line([(0, HEADER_H - 2), (total_w, HEADER_H - 2)], fill=GRAY)

# Paste calendars
out.paste(img_genyo, (0, HEADER_H))
out.paste(img_silver, (W + GAP, HEADER_H))

# Vertical separator
header_draw.line([(W + GAP // 2, 0), (W + GAP // 2, total_h)], fill=GRAY)

out.save("output/calendar_comparison.png")
print(f"Saved: output/calendar_comparison.png ({total_w}x{total_h})")
