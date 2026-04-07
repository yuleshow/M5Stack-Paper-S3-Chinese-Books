#!/usr/bin/env python3
"""Render a sample page using real text from The Economist EPUB."""
import os, sys, zipfile
from PIL import Image, ImageDraw, ImageFont

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ── Extract text from EPUB ──
epub_path = 'sd_card/books/The Economist, 2026-02-21.epub'
z = zipfile.ZipFile(epub_path)
data = z.read('feed_1/article_0/index_u37.html').decode('utf-8', errors='replace')

def html_strip(html):
    out, in_script, in_head = [], False, False
    last_nl, last_sp = False, False
    i = 0
    while i < len(html):
        c = html[i]
        if c == '<':
            te = i + 1
            while te < len(html) and html[te] != '>': te += 1
            if te >= len(html): break
            tag = ''
            j = i + 1
            while j < te and len(tag) < 30:
                tc = html[j]
                if tc in ' \t\n\r':
                    if tag: break
                    j += 1; continue
                if tc == '/' and not tag: tag += '/'; j += 1; continue
                tag += tc.lower(); j += 1
            if tag == 'head': in_head = True
            elif tag == '/head': in_head = False
            if tag in ('script', 'style'): in_script = True
            elif tag in ('/script', '/style'): in_script = False
            if not in_script and tag in ('/p', '/div', 'br', 'br/') or (tag.startswith('/h') and len(tag) == 3):
                if not last_nl and out:
                    out.append('\n'); last_nl = True; last_sp = True
            i = te + 1; continue
        if in_script or in_head: i += 1; continue
        if c in '\n\r \t' or c == '\xa0':
            if not last_sp and not last_nl:
                out.append(' '); last_sp = True
        else:
            out.append(c); last_nl = False; last_sp = False
        i += 1
    return ''.join(out)

text = html_strip(data)
# Skip the nav line at top
if text.startswith(' |'):
    text = text[text.index('\n')+1:]

# ── Layout constants ──
W, H = 540, 960
RD_TOP, RD_BOT = 65, 878
RD_LEFT, RD_RIGHT = 20, 530
AVAIL_W = RD_RIGHT - RD_LEFT

font_path = 'sd_card/fonts/EBGaramond-Regular.ttf'
font_size = 30

font = ImageFont.truetype(font_path, font_size)
ui_font = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 16)
ui_sm = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 12)
ui_lg = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 28)

line_height = font_size + font_size // 4  # 1.25x

# ── Render using line-at-a-time approach (matches new firmware code) ──
img = Image.new('L', (W, H), 255)
draw = ImageDraw.Draw(img)

# Status bar
draw.text((8, 4), "19:41", fill=0, font=ui_lg)
bx, by, bw, bh = W - 46, 12, 34, 18
draw.rectangle([bx, by, bx+bw, by+bh], outline=0, width=2)
draw.rectangle([bx+bw, by+4, bx+bw+3, by+bh-4], fill=0)
draw.rectangle([bx+3, by+3, bx+bw-3, by+bh-3], fill=0)

# Separator line below status bar
draw.line([(0, 55), (W, 55)], fill=0, width=1)

# ── Word-wrap and render line by line ──
current_y = RD_TOP
words = []
i = 0
render_stop = len(text)

# Parse into tokens: words and newlines
tokens = []
while i < len(text):
    ch = text[i]
    if ch == '\n':
        tokens.append(('\n', 0))
        i += 1
        continue
    if ch in ' \t':
        while i < len(text) and text[i] in ' \t': i += 1
        continue
    # Collect word
    ws = i
    while i < len(text) and text[i] not in ' \t\n\r': i += 1
    word = text[ws:i]
    bbox = font.getbbox(word)
    ww = bbox[2] - bbox[0]
    tokens.append((word, ww))

# Now render line by line
current_line = ""
current_line_w = 0
space_w = font.getbbox("i i")[2] - font.getbbox("ii")[2]
if space_w <= 0: space_w = font_size // 3
pending_space = False
ti = 0

while ti < len(tokens):
    tok, tw = tokens[ti]
    
    if tok == '\n':
        # Paragraph break
        if current_line:
            draw.text((RD_LEFT, current_y), current_line, fill=0, font=font)
            current_line = ""
            current_line_w = 0
            pending_space = False
            current_y += line_height + line_height // 2
        if current_y + line_height > RD_BOT:
            break
        ti += 1
        continue
    
    needed = tw + (space_w if pending_space else 0)
    
    # Word wrap
    if current_line_w + needed > AVAIL_W and current_line:
        draw.text((RD_LEFT, current_y), current_line, fill=0, font=font)
        current_line = ""
        current_line_w = 0
        pending_space = False
        current_y += line_height
        if current_y + line_height > RD_BOT:
            break
    
    if pending_space and current_line:
        current_line += " "
        current_line_w += space_w
    pending_space = True
    
    current_line += tok
    current_line_w += tw
    ti += 1

# Draw remaining line
if current_line and current_y + line_height <= RD_BOT:
    draw.text((RD_LEFT, current_y), current_line, fill=0, font=font)

# Progress bar
bar_y = RD_BOT + 2
bar_w = W - 60
bar_x = 30
pct = 0.05
fill_w = int(bar_w * pct)
draw.rectangle([bar_x, bar_y, bar_x+bar_w, bar_y+3], fill=200)
if fill_w > 0:
    draw.rectangle([bar_x, bar_y, bar_x+fill_w, bar_y+3], fill=0)
draw.text((W//2, bar_y+8), "3/97", fill=0, font=ui_font, anchor="mt")
draw.text((W-30, bar_y+8), "3.1%", fill=0, font=ui_sm, anchor="rt")

# Nav arrows using actual SD card icons
from PIL import PngImagePlugin
try:
    # Load actual nav icons from sd_card/icons/
    back_icon = Image.open('sd_card/icons/back.png').convert('L')
    next_icon = Image.open('sd_card/icons/next.png').convert('L')
    return_icon = Image.open('sd_card/icons/return.png').convert('L')
    toolbar_icon = Image.open('sd_card/icons/reader_toolbar.png').convert('L')
    
    # LTR nav: left=prev (back.png), right=next (next.png)
    img.paste(back_icon, (10, 886))
    img.paste(next_icon, (84, 886))
    img.paste(return_icon, (466, 886))
    
    # Toolbar at (150, 905)
    img.paste(toolbar_icon, (150, 905))
    
    # Font size number in cell 1 (x=202, centered in 52px cell)
    draw.text((202 + 26, 905 + 25), "30", fill=0, font=ui_font, anchor="mm")
except Exception as e:
    print(f"Warning: Could not load icons: {e}")
    # Fallback: draw simple arrows
    def draw_arrow(x, y, sz, direction):
        m = 18
        if direction == 'left':
            draw.line([(x+sz-m, y+m), (x+m, y+sz//2)], fill=0, width=3)
            draw.line([(x+m, y+sz//2), (x+sz-m, y+sz-m)], fill=0, width=3)
        else:
            draw.line([(x+m, y+m), (x+sz-m, y+sz//2)], fill=0, width=3)
            draw.line([(x+sz-m, y+sz//2), (x+m, y+sz-m)], fill=0, width=3)
    draw_arrow(10, 886, 64, 'left')
    draw_arrow(84, 886, 64, 'right')
    draw_arrow(466, 886, 64, 'right')

out = 'output/sample_economist.png'
os.makedirs('output', exist_ok=True)
img.save(out)
print(f"Saved: {out} ({W}x{H})")
