#!/usr/bin/env python3
"""
Convert TTF font to M5ReadPaper binary format
Based on M5ReadPaper's font structure
"""

from PIL import Image, ImageDraw, ImageFont
import struct
import sys
import os
from fontTools.ttLib import TTFont

# When running as a bundled .app, __file__ points inside the bundle — skip chdir
if not getattr(sys, 'frozen', False):
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

def get_common_chinese_chars():
    """Get common Traditional Chinese characters for e-books"""
    chars = set()
    
    # Basic punctuation and ASCII
    chars.update(chr(i) for i in range(32, 127))
    
    # Extended Traditional Chinese characters
    # CJK Unified Ideographs main block (U+4E00 to U+9FFF)
    # This is the main block containing most commonly used Chinese characters
    common_ranges = [
        # Core block - most common 20,000+ characters
        (0x4E00, 0x9FFF),  # CJK Unified Ideographs main block
        # Additional punctuation and symbols commonly used in Chinese
        (0x3000, 0x303F),  # CJK Symbols and Punctuation
        (0xFF00, 0xFFEF),  # Halfwidth and Fullwidth Forms
        # Vertical presentation forms (used for vertical CJK text)
        (0xFE10, 0xFE1F),  # Vertical Forms
        (0xFE30, 0xFE4F),  # CJK Compatibility Forms (vertical brackets etc.)
        # General punctuation: smart quotes "" '', em/en dash —–, ellipsis …, bullet •
        (0x2000, 0x206F),  # General Punctuation
        # Latin-1 Supplement: non-breaking space, accented chars, ×, ·, etc.
        (0x00A0, 0x00FF),  # Latin-1 Supplement
        # Letterlike symbols: ™ etc.
        (0x2100, 0x214F),  # Letterlike Symbols
        # Geometric shapes: ○ □ ● ■ △ etc. (common in Chinese texts)
        (0x25A0, 0x25FF),  # Geometric Shapes
    ]
    
    for start, end in common_ranges:
        chars.update(chr(i) for i in range(start, end + 1))
    
    return sorted(list(chars), key=lambda x: ord(x))

def render_glyph(font, char, font_size):
    """Render a single character to bitmap, returning bearing offsets"""
    # Create a larger canvas to avoid clipping
    canvas_size = font_size * 3
    img = Image.new('1', (canvas_size, canvas_size), 1)  # 1-bit, white background
    draw = ImageDraw.Draw(img)
    
    # Draw at a safe offset so the entire glyph (including parts above the
    # ascender) stays within the image.  Without this margin, textbbox can
    # return negative top values and img.crop() fills out-of-bounds rows
    # with black, producing a spurious top bar on every glyph.
    origin = font_size
    
    try:
        # Get text bounding box
        bbox = draw.textbbox((origin, origin), char, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        if text_width <= 0 or text_height <= 0:
            return None, 0, 0, 0, 0
        
        # Draw text
        draw.text((origin, origin), char, font=font, fill=0)  # Black text
        
        # Crop to actual content
        img_crop = img.crop(bbox)
        
        # Return bearing offsets relative to the drawing origin
        bearing_x = bbox[0] - origin
        bearing_y = bbox[1] - origin
        
        return img_crop, text_width, text_height, bearing_x, bearing_y
    except Exception as e:
        print(f"Error rendering '{char}' (U+{ord(char):04X}): {e}")
        return None, 0, 0, 0, 0

def bitmap_to_bytes(img):
    """Convert PIL 1-bit image to packed bytes"""
    # Use getdata() for bulk pixel access (much faster than per-pixel getpixel)
    pixels = img.getdata()
    bits = [1 if p == 0 else 0 for p in pixels]  # Black = 1, White = 0
    
    # Pad to byte boundary
    pad = (8 - len(bits) % 8) % 8
    bits.extend([0] * pad)
    
    # Pack into bytes
    bytes_data = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            byte |= (bits[i + j] << (7 - j))
        bytes_data.append(byte)
    
    return bytes(bytes_data)

# Reverse mapping: vertical bracket forms → horizontal source character
# Only bracket-type paired punctuation should be rotated 90° CW.
# Non-bracket forms (ellipsis, dashes, etc.) are NOT rotated — borrow from fallback only.
VERT_BRACKETS_TO_HORIZ = {
    0xFE41: 0x300C,  # ﹁ ← 「
    0xFE42: 0x300D,  # ﹂ ← 」
    0xFE43: 0x300E,  # ﹃ ← 『
    0xFE44: 0x300F,  # ﹄ ← 』
    0xFE3F: 0x3008,  # ︿ ← 〈
    0xFE40: 0x3009,  # ﹀ ← 〉
    0xFE3D: 0x300A,  # ︽ ← 《
    0xFE3E: 0x300B,  # ︾ ← 》
    0xFE3B: 0x3010,  # ︻ ← 【
    0xFE3C: 0x3011,  # ︼ ← 】
    0xFE35: 0xFF08,  # ︵ ← （
    0xFE36: 0xFF09,  # ︶ ← ）
    0xFE17: 0x3016,  # ︗ ← 〖
    0xFE18: 0x3017,  # ︘ ← 〗
    0xFE39: 0x3014,  # ︹ ← 〔
    0xFE3A: 0x3015,  # ︺ ← 〕
    0xFE37: 0xFF5B,  # ︷ ← ｛
    0xFE38: 0xFF5D,  # ︸ ← ｝
    0xFE47: 0xFF3B,  # ﹇ ← ［
    0xFE48: 0xFF3D,  # ﹈ ← ］
}
# These vertical forms should NOT be rotated — only borrow from fallback font:
# 0xFE19 ︙ (vertical ellipsis), 0xFE30 ︰ (two-dot leader),
# 0xFE31 ︱ (em-dash), 0xFE34 ︴ (wavy low line)

def render_rotated_glyph(font, horiz_char, font_size):
    """Render a horizontal character and rotate 90° clockwise to create vertical form."""
    canvas_size = font_size * 3
    img = Image.new('L', (canvas_size, canvas_size), 255)  # Grayscale for better rotation
    draw = ImageDraw.Draw(img)
    
    try:
        bbox = draw.textbbox((0, 0), horiz_char, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        if text_width <= 0 or text_height <= 0:
            return None, 0, 0, 0, 0
        
        draw.text((0, 0), horiz_char, font=font, fill=0)
        img_crop = img.crop(bbox)
        
        # Rotate 90° clockwise
        img_rot = img_crop.rotate(-90, expand=True)
        
        # Convert to 1-bit
        img_1bit = img_rot.point(lambda p: 0 if p < 128 else 1, '1')
        
        rot_w, rot_h = img_1bit.size
        # Bearing: center the rotated glyph in the em-square
        bearing_x = (font_size - rot_w) // 2
        bearing_y = (font_size - rot_h) // 2
        
        return img_1bit, rot_w, rot_h, bearing_x, bearing_y
    except Exception as e:
        return None, 0, 0, 0, 0

# Unicode ranges considered "punctuation" for the force-fallback-punct option
PUNCT_RANGES = [
    (0x2000, 0x206F),   # General Punctuation
    (0x3000, 0x303F),   # CJK Symbols and Punctuation
    (0xFE10, 0xFE1F),   # Vertical Forms
    (0xFE30, 0xFE4F),   # CJK Compatibility Forms
    (0xFF00, 0xFFEF),   # Halfwidth and Fullwidth Forms
    (0x00A0, 0x00BF),   # Latin-1 punctuation subset
]

def _is_punct(cp):
    """Return True if codepoint falls in a punctuation range."""
    return any(s <= cp < e for s, e in PUNCT_RANGES)

def get_font_display_name(font_path):
    """Extract display name from a font file, preferring Traditional Chinese."""
    try:
        tt = TTFont(font_path, fontNumber=0)
        name_table = tt['name']
        font_family = None
        font_family_en = None
        # Prefer: Traditional Chinese (1028), then zh-HK (3076), zh-CN (2052), ja (1041), ko (1042)
        tc_lang_ids = {1028, 3076}  # Traditional Chinese
        cjk_lang_ids = {1028, 2052, 3076, 1041, 1042}
        for record in name_table.names:
            if record.nameID == 1:
                try:
                    decoded = record.toUnicode()
                    if decoded:
                        if record.platformID == 3 and record.langID in tc_lang_ids:
                            font_family = decoded
                            break  # Traditional Chinese — best match
                        elif record.platformID == 3 and record.langID in cjk_lang_ids:
                            if not font_family:
                                font_family = decoded
                        elif record.platformID == 1 and record.platEncID == 2:
                            if not font_family:
                                font_family = decoded
                        elif font_family_en is None:
                            font_family_en = decoded
                except Exception:
                    pass
        tt.close()
        if font_family:
            return font_family
        if font_family_en:
            # Map well-known English names to Traditional Chinese
            _NAME_OVERRIDES = {
                'Noto Sans TC': '思源黑體',
                'Noto Serif TC': '思源宋體',
            }
            return _NAME_OVERRIDES.get(font_family_en, font_family_en)
    except Exception:
        pass
    return os.path.splitext(os.path.basename(font_path))[0]

def convert_ttf_to_bin(ttf_path, output_path, font_size=30, fallback_path=None, render_size=None, target_size=None, force_fallback_punct=False):
    """Convert TTF to M5ReadPaper binary format.
    If fallback_path is given, borrow missing glyphs from the fallback font.
    If render_size is given, render glyphs at that size but store render_size in header.
    target_size is the nominal/equivalent size (e.g. 36 for Silver) for fallback sizing.
    If force_fallback_punct is True, all punctuation glyphs are taken from the fallback font."""
    if render_size is None:
        render_size = font_size
    if target_size is None:
        target_size = font_size
    # Header stores render_size so binScale=1.0 on device
    header_font_size = render_size
    print(f"Loading font: {ttf_path}")
    print(f"Font size: {font_size}pt (render at {render_size}pt)")
    if fallback_path:
        print(f"Fallback font: {fallback_path}")
    
    # Extract font family name (prefer Traditional Chinese) and cmap
    font_family = get_font_display_name(ttf_path)
    primary_cmap = set()
    try:
        tt = TTFont(ttf_path, fontNumber=0)
        for table in tt['cmap'].tables:
            primary_cmap.update(table.cmap.keys())
        tt.close()
        print(f"Font family: {font_family}")
        print(f"Primary font cmap: {len(primary_cmap)} codepoints")
    except Exception as e:
        print(f"Warning: Could not read cmap: {e}")
    
    try:
        font = ImageFont.truetype(ttf_path, render_size)
    except Exception as e:
        print(f"Error loading font: {e}")
        return False
    
    # Determine fallback render size: match primary font's actual glyph dimensions
    fallback_render_size = font_size
    if render_size != font_size:
        # Silver mode: measure primary font's average glyph width to size fallback accordingly
        sample_chars = '盡陀人心世郡第一大是國中不為'
        sample_img = Image.new('1', (render_size * 3, render_size * 3), 1)
        sample_draw = ImageDraw.Draw(sample_img)
        sample_widths = []
        for sc in sample_chars:
            sb = sample_draw.textbbox((0, 0), sc, font=font)
            sw = sb[2] - sb[0]
            if sw > 0:
                sample_widths.append(sw)
        if sample_widths:
            avg_glyph = sum(sample_widths) / len(sample_widths)
            fallback_render_size = int(avg_glyph + 0.5)
            print(f"Silver mode: primary avg glyph={avg_glyph:.1f}px, fallback will render at {fallback_render_size}pt")

    # Load fallback font if specified
    fallback_font = None
    if fallback_path:
        try:
            fallback_font = ImageFont.truetype(fallback_path, fallback_render_size)
        except Exception as e:
            print(f"Warning: Could not load fallback font: {e}")
    
    # Auto-detect fallback if not specified: use GenYoMinTW if available
    auto_fallback_path = None
    if not fallback_font:
        auto_fallback = 'sd_card/fonts/GenYoMinTW-Regular.ttf'
        if os.path.exists(auto_fallback) and os.path.abspath(auto_fallback) != os.path.abspath(ttf_path):
            try:
                fallback_font = ImageFont.truetype(auto_fallback, fallback_render_size)
                auto_fallback_path = auto_fallback
                print(f"Auto-detected fallback font: {auto_fallback}")
            except Exception:
                pass
    
    # Build fallback cmap so we can skip chars the fallback also lacks
    fallback_cmap = set()
    fb_cmap_path = fallback_path or auto_fallback_path
    if fallback_font and fb_cmap_path:
        try:
            tt_fb = TTFont(fb_cmap_path, fontNumber=0)
            for table in tt_fb['cmap'].tables:
                fallback_cmap.update(table.cmap.keys())
            tt_fb.close()
            print(f"Fallback font cmap: {len(fallback_cmap)} codepoints")
        except Exception:
            pass
    
    # Get character set
    print("Building character set...")
    chars = get_common_chinese_chars()
    print(f"Total characters: {len(chars)}")
    
    # Prepare index and bitmap data
    index_entries = []
    bitmap_data = bytearray()

    # Bpmf Zihi Kai glyphs are naturally rectangular; preserve native bearings
    # for primary glyphs to avoid forcing square-cell visual alignment.
    keep_native_bearing = is_bpmfzihi_font(ttf_path, font_family)
    if keep_native_bearing:
        print("Bpmf Zihi mode: preserving native glyph bearings for primary glyphs")
    
    # First pass: render all glyphs to get dimensions and bitmap data
    print("Rendering glyphs...")
    fallback_count = 0
    rotated_count = 0
    for i, char in enumerate(chars):
        if i % 100 == 0:
            print(f"  Progress: {i}/{len(chars)} ({100*i//len(chars)}%)")
        
        cp = ord(char)
        use_fallback_first = force_fallback_punct and fallback_font and _is_punct(cp)
        in_primary = cp in primary_cmap
        in_fallback = cp in fallback_cmap

        img = None
        glyph_from_primary = False
        if use_fallback_first and in_fallback:
            img, width, height, bearing_x, bearing_y = render_glyph(fallback_font, char, fallback_render_size)
            if img is not None:
                fallback_count += 1

        if img is None and in_primary:
            img, width, height, bearing_x, bearing_y = render_glyph(font, char, render_size)
            if img is not None:
                glyph_from_primary = True
        
        # For missing vertical bracket forms: try rotating the horizontal counterpart
        if img is None and cp in VERT_BRACKETS_TO_HORIZ:
            horiz_cp = VERT_BRACKETS_TO_HORIZ[cp]
            if horiz_cp in primary_cmap:
                img, width, height, bearing_x, bearing_y = render_rotated_glyph(font, chr(horiz_cp), render_size)
                if img is not None:
                    rotated_count += 1
                    glyph_from_primary = True
        
        # Try fallback font if still missing (skip if already tried above)
        if img is None and fallback_font and not use_fallback_first and in_fallback:
            img, width, height, bearing_x, bearing_y = render_glyph(fallback_font, char, fallback_render_size)
            if img is not None:
                fallback_count += 1
        
        if img is None:
            # Skip characters that can't be rendered
            continue
        
        # Default: center glyph in em-square for stable vertical CJK layout.
        # Bpmf Zihi only: keep native bearings for primary glyphs.
        if not (keep_native_bearing and glyph_from_primary):
            bearing_x = (font_size - width) // 2
            bearing_y = (font_size - height) // 2
        
        # Convert to bitmap bytes
        bitmap_bytes = bitmap_to_bytes(img)
        
        # Store glyph info (offset will be calculated later)
        unicode_val = ord(char)
        bitmap_size = len(bitmap_bytes)
        
        index_entries.append({
            'unicode': unicode_val,
            'width': width,
            'height': height,
            'size': bitmap_size,
            'bearing_x': bearing_x,
            'bearing_y': bearing_y,
            'bitmap': bitmap_bytes  # Store temporarily
        })
    
    print(f"Successfully rendered {len(index_entries)} glyphs ({rotated_count} rotated, {fallback_count} from fallback)")
    if fallback_count > len(index_entries) * 0.5:
        print(f"⚠ WARNING: {fallback_count}/{len(index_entries)} glyphs ({100*fallback_count//len(index_entries)}%) came from fallback font!")
        print(f"  This usually means the primary font's cmap could not be read (e.g. TTC without fontNumber).")
    
    # Second pass: calculate correct offsets and build bitmap data
    print("Calculating offsets...")
    header_size = 137
    index_size = len(index_entries) * 20
    current_offset = 0
    
    for entry in index_entries:
        entry['offset'] = header_size + index_size + current_offset
        bitmap_data.extend(entry['bitmap'])
        current_offset += entry['size']
        del entry['bitmap']  # Remove temporary bitmap data
    
    # Write binary file
    print(f"Writing binary font: {output_path}")
    with open(output_path, 'wb') as f:
        # Header (137 bytes)
        char_count = len(index_entries)
        version = 2
        family_name = font_family.encode('utf-8')[:64].ljust(64, b'\x00')
        style_name = b"Regular".ljust(64, b'\x00')
        
        # Write header in correct order
        f.write(struct.pack('<I', char_count))  # char_count (4 bytes, offset 0-3)
        f.write(struct.pack('<B', header_font_size))   # font_size (1 byte, offset 4)
        f.write(struct.pack('<I', version))     # version (4 bytes, offset 5-8)
        f.write(family_name)                    # family_name (64 bytes, offset 9-72)
        f.write(style_name)                     # style_name (64 bytes, offset 73-136)
        # Total: 4 + 1 + 4 + 64 + 64 = 137 bytes
        
        # Index (20 bytes per entry)
        for entry in index_entries:
            f.write(struct.pack('<I', entry['unicode']))      # 4 bytes
            f.write(struct.pack('<H', entry['width']))        # 2 bytes
            f.write(struct.pack('<H', entry['height']))       # 2 bytes
            f.write(struct.pack('<I', entry['offset']))       # 4 bytes
            f.write(struct.pack('<I', entry['size']))         # 4 bytes
            # Bearing offsets (v2: replaces padding)
            f.write(struct.pack('<h', entry['bearing_x']))    # 2 bytes (int16)
            f.write(struct.pack('<h', entry['bearing_y']))    # 2 bytes (int16)
        
        # Bitmap data
        f.write(bitmap_data)
    
    file_size = os.path.getsize(output_path)
    print(f"✓ Binary font created: {file_size:,} bytes")
    print(f"  Characters: {char_count}")
    print(f"  Index size: {char_count * 20:,} bytes")
    print(f"  Bitmap size: {len(bitmap_data):,} bytes")
    print(f"  Fallback glyphs: {fallback_count}")
    
    return (True, fallback_count, char_count)

# Silver font scale table (same as device-side silverScaleTable in ui_drawing.cpp)
# Maps nominal readingFontSize → actual render size needed
SILVER_SCALE_TABLE = {
    16: 23, 18: 25, 20: 27, 22: 29, 24: 32, 26: 35,
    28: 39, 32: 44, 34: 47, 36: 49, 38: 53, 40: 58, 64: 90
}
SILVER_SCALE_RATIO = 1.38

def silver_scaled_size(nominal):
    """Look up scaled render size for Silver font, matching device-side silverScaledSize()."""
    if nominal in SILVER_SCALE_TABLE:
        return SILVER_SCALE_TABLE[nominal]
    return int(nominal * SILVER_SCALE_RATIO + 0.5)

def is_silver_font(path):
    return 'silver' in os.path.basename(path).lower()

def is_bpmfzihi_font(path, family_name=None):
    """Detect Bpmf Zihi Kai Std font for font-specific glyph placement."""
    base = os.path.basename(path).lower()
    if 'bpmf' in base and 'zihi' in base:
        return True
    if family_name:
        fam = family_name.lower()
        if 'bpmf' in fam and 'zihi' in fam:
            return True
    return False

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Convert TTF font to M5ReadPaper binary format')
    parser.add_argument('ttf', nargs='?', help='Input TTF/TTC/OTF font file')
    parser.add_argument('-o', '--output', help='Output .bin file path (default: <font>_<size>pt.bin)')
    parser.add_argument('-s', '--size', type=int, default=30, help='Target font size in pt (default: 30). For Silver fonts, this is the nominal readingFontSize; the script auto-computes the actual render size using the built-in ratio.')
    parser.add_argument('-f', '--fallback', help='Fallback font for missing glyphs (auto-detects GenYoMinTW)')
    parser.add_argument('-r', '--render-size', type=int, help='Override render size (normally auto-calculated for Silver)')
    parser.add_argument('--gui', action='store_true', help='Launch graphical interface')
    args = parser.parse_args()

    if args.gui or args.ttf is None:
        # ---- GUI mode ----
        import threading
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox

        FONTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sd_card', 'fonts') if not getattr(sys, 'frozen', False) else os.path.expanduser('~/Desktop')
        GUI_SIZES = [36, 44, 52]

        _STRINGS = {
            'en': {
                'title': 'Convert TTF → BIN',
                'folders': 'Folders',
                'source': 'Source:',
                'target': 'Target:',
                'fallback': 'Fallback:',
                'browse': 'Browse…',
                'options': 'Options',
                'force_punct': 'Replace all punctuation from fallback font',
                'fonts': 'Fonts',
                'sizes': 'Sizes (pt)',
                'select_all': 'Select All',
                'deselect_all': 'Deselect All',
                'add_font': 'Add Font…',
                'convert': 'Convert',
                'log': 'Log',
                'lang_switch': '中文',
                'browse_source': 'Select source fonts folder',
                'browse_target': 'Select target folder',
                'browse_fallback': 'Select fallback font',
                'browse_add': 'Select a font file',
                'warn_no_fonts': 'No fonts',
                'warn_no_fonts_msg': 'Select at least one font.',
                'warn_no_sizes': 'No sizes',
                'warn_no_sizes_msg': 'Select at least one size.',
                'done': 'Done: {success} succeeded, {failed} failed out of {total} jobs.',
            },
            'zh': {
                'title': '轉換 TTF → BIN',
                'folders': '資料夾',
                'source': '來源：',
                'target': '目標：',
                'fallback': '備用字型：',
                'browse': '瀏覽…',
                'options': '選項',
                'force_punct': '以備用字型取代所有標點',
                'fonts': '字型',
                'sizes': '字級 (pt)',
                'select_all': '全選',
                'deselect_all': '取消全選',
                'add_font': '新增字型…',
                'convert': '轉換',
                'log': '紀錄',
                'lang_switch': 'English',
                'browse_source': '選擇來源字型資料夾',
                'browse_target': '選擇目標資料夾',
                'browse_fallback': '選擇備用字型',
                'browse_add': '選擇字型檔案',
                'warn_no_fonts': '未選字型',
                'warn_no_fonts_msg': '請至少選擇一個字型。',
                'warn_no_sizes': '未選字級',
                'warn_no_sizes_msg': '請至少選擇一個字級。',
                'done': '完成：{success} 成功、{failed} 失敗，共 {total} 個任務。',
            },
        }

        class _LogRedirector:
            """Redirects print() to the GUI log."""
            def __init__(self, callback):
                self.callback = callback
                self._buf = ""
            def write(self, text):
                self._buf += text
                while "\n" in self._buf:
                    line, self._buf = self._buf.split("\n", 1)
                    if line:
                        self.callback(line)
            def flush(self):
                if self._buf:
                    self.callback(self._buf)
                    self._buf = ""

        class ConvertGUI:
            def __init__(self, root):
                self.root = root
                self.lang = 'en'
                root.resizable(False, False)

                self.input_dir = tk.StringVar(value=FONTS_DIR)
                self.input_dir.trace_add('write', self._on_input_dir_changed)
                self.output_dir = tk.StringVar(value=FONTS_DIR)
                default_fallback = os.path.join(FONTS_DIR, 'GenYoMinTW-Regular.ttf')
                self.fallback_path = tk.StringVar(value=default_fallback if os.path.exists(default_fallback) else '')
                self.force_fallback_punct = tk.BooleanVar(value=False)
                self.font_vars = {}
                self.size_vars = {}

                self._build_ui()

            def _t(self, key):
                return _STRINGS[self.lang][key]

            def _build_ui(self):
                # Clear all widgets
                for w in self.root.winfo_children():
                    w.destroy()

                self.root.title(self._t('title'))

                # --- Language switch ---
                frame_lang = ttk.Frame(self.root, padding=2)
                frame_lang.pack(fill="x", padx=10, pady=(4, 0))
                ttk.Button(frame_lang, text=self._t('lang_switch'), command=self._toggle_lang).pack(side="right")

                # --- Folders ---
                frame_dirs = ttk.LabelFrame(self.root, text=self._t('folders'), padding=8)
                frame_dirs.pack(fill="x", padx=10, pady=(4, 4))
                frame_dirs.columnconfigure(1, weight=1)

                ttk.Label(frame_dirs, text=self._t('source')).grid(row=0, column=0, sticky="w", padx=(0, 4))
                ttk.Entry(frame_dirs, textvariable=self.input_dir, width=50).grid(row=0, column=1, sticky="ew", padx=2)
                ttk.Button(frame_dirs, text=self._t('browse'), command=self._browse_input).grid(row=0, column=2, padx=(4, 0))

                ttk.Label(frame_dirs, text=self._t('target')).grid(row=1, column=0, sticky="w", padx=(0, 4), pady=(4, 0))
                ttk.Entry(frame_dirs, textvariable=self.output_dir, width=50).grid(row=1, column=1, sticky="ew", padx=2, pady=(4, 0))
                ttk.Button(frame_dirs, text=self._t('browse'), command=self._browse_output).grid(row=1, column=2, padx=(4, 0), pady=(4, 0))

                # --- Fallback font ---
                ttk.Label(frame_dirs, text=self._t('fallback')).grid(row=2, column=0, sticky="w", padx=(0, 4), pady=(4, 0))
                ttk.Entry(frame_dirs, textvariable=self.fallback_path, width=50).grid(row=2, column=1, sticky="ew", padx=2, pady=(4, 0))
                ttk.Button(frame_dirs, text=self._t('browse'), command=self._browse_fallback).grid(row=2, column=2, padx=(4, 0), pady=(4, 0))

                # --- Options ---
                frame_opts = ttk.LabelFrame(self.root, text=self._t('options'), padding=8)
                frame_opts.pack(fill="x", padx=10, pady=4)
                ttk.Checkbutton(frame_opts, text=self._t('force_punct'),
                                variable=self.force_fallback_punct).pack(anchor="w")

                # --- Font list (scrollable) ---
                self.frame_fonts = ttk.LabelFrame(self.root, text=self._t('fonts'), padding=4)
                self.frame_fonts.pack(fill="x", padx=10, pady=4)
                self._font_canvas = tk.Canvas(self.frame_fonts, height=200, highlightthickness=0)
                self._font_scrollbar = ttk.Scrollbar(self.frame_fonts, orient="vertical", command=self._font_canvas.yview)
                self._font_inner = ttk.Frame(self._font_canvas)
                self._font_inner.bind("<Configure>", lambda e: self._font_canvas.configure(scrollregion=self._font_canvas.bbox("all")))
                self._font_canvas.create_window((0, 0), window=self._font_inner, anchor="nw")
                self._font_canvas.configure(yscrollcommand=self._font_scrollbar.set)
                self._font_canvas.pack(side="left", fill="both", expand=True)
                self._font_scrollbar.pack(side="right", fill="y")
                # Mouse wheel scrolling (scoped to font canvas area)
                def _on_mousewheel(event):
                    # Only scroll font list when cursor is over it
                    w = event.widget
                    while w:
                        if w is self._font_canvas or w is self._font_inner:
                            self._font_canvas.yview_scroll(-1 * (event.delta // 120 or (-1 if event.delta < 0 else 1)), "units")
                            break
                        w = w.master
                self.root.bind_all("<MouseWheel>", _on_mousewheel)
                self._build_font_list(self._font_inner)

                # --- Sizes ---
                frame_sizes = ttk.LabelFrame(self.root, text=self._t('sizes'), padding=8)
                frame_sizes.pack(fill="x", padx=10, pady=4)
                old_selections = {s: v.get() for s, v in self.size_vars.items()}
                self.size_vars = {}
                for i, s in enumerate(GUI_SIZES):
                    var = tk.BooleanVar(value=old_selections.get(s, True))
                    ttk.Checkbutton(frame_sizes, text=str(s), variable=var).grid(row=0, column=i, padx=8)
                    self.size_vars[s] = var

                # --- Buttons ---
                frame_btns = ttk.Frame(self.root, padding=4)
                frame_btns.pack(fill="x", padx=10, pady=4)
                ttk.Button(frame_btns, text=self._t('select_all'), command=self._select_all).pack(side="left", padx=4)
                ttk.Button(frame_btns, text=self._t('deselect_all'), command=self._deselect_all).pack(side="left", padx=4)
                ttk.Button(frame_btns, text=self._t('add_font'), command=self._add_font).pack(side="left", padx=4)
                self.btn_convert = ttk.Button(frame_btns, text=self._t('convert'), command=self._start_convert)
                self.btn_convert.pack(side="right", padx=4)

                # --- Progress ---
                self.progress = ttk.Progressbar(self.root, mode="determinate")
                self.progress.pack(fill="x", padx=10, pady=4)

                # --- Log ---
                frame_log = ttk.LabelFrame(self.root, text=self._t('log'), padding=4)
                frame_log.pack(fill="both", expand=True, padx=10, pady=(4, 10))
                self.log_text = tk.Text(frame_log, height=14, width=80, state="disabled",
                                        font=("Menlo", 11) if sys.platform == "darwin" else ("Consolas", 10))
                scrollbar = ttk.Scrollbar(frame_log, orient="vertical", command=self.log_text.yview)
                self.log_text.configure(yscrollcommand=scrollbar.set)
                self.log_text.pack(side="left", fill="both", expand=True)
                scrollbar.pack(side="right", fill="y")

            def _toggle_lang(self):
                self.lang = 'zh' if self.lang == 'en' else 'en'
                self._build_ui()

            def _on_input_dir_changed(self, *_args):
                self.output_dir.set(self.input_dir.get())

            def _browse_input(self):
                d = filedialog.askdirectory(title=self._t('browse_source'), initialdir=self.input_dir.get())
                if d:
                    self.input_dir.set(d)
                    self._build_font_list(self._font_inner)

            def _browse_output(self):
                d = filedialog.askdirectory(title=self._t('browse_target'), initialdir=self.output_dir.get())
                if d:
                    self.output_dir.set(d)

            def _browse_fallback(self):
                path = filedialog.askopenfilename(
                    title=self._t('browse_fallback'),
                    initialdir=os.path.dirname(self.fallback_path.get()) or FONTS_DIR,
                    filetypes=[("Font files", "*.ttf *.ttc *.otf"), ("All files", "*.*")])
                if path:
                    self.fallback_path.set(path)

            def _build_font_list(self, parent):
                for widget in parent.winfo_children():
                    widget.destroy()
                self.font_vars.clear()
                fonts_dir = self.input_dir.get()
                if not os.path.isdir(fonts_dir):
                    return
                ttf_files = sorted(
                    f for f in os.listdir(fonts_dir)
                    if f.lower().endswith(('.ttf', '.ttc', '.otf'))
                )
                # Filter to CJK fonts and get display names in a single pass
                cjk_fonts = []  # list of (filename, display_name)
                for f in ttf_files:
                    path = os.path.join(fonts_dir, f)
                    try:
                        tt = TTFont(path, fontNumber=0)
                        has_cjk = any(
                            0x4E00 <= cp <= 0x9FFF
                            for table in tt['cmap'].tables
                            for cp in table.cmap.keys()
                        )
                        if not has_cjk:
                            tt.close()
                            continue
                        # Extract display name from the already-open font
                        display = None
                        display_en = None
                        name_table = tt['name']
                        tc_lang_ids = {1028, 3076}
                        cjk_lang_ids = {1028, 2052, 3076, 1041, 1042}
                        for record in name_table.names:
                            if record.nameID == 1:
                                try:
                                    decoded = record.toUnicode()
                                    if decoded:
                                        if record.platformID == 3 and record.langID in tc_lang_ids:
                                            display = decoded
                                            break
                                        elif record.platformID == 3 and record.langID in cjk_lang_ids:
                                            if not display:
                                                display = decoded
                                        elif record.platformID == 1 and record.platEncID == 2:
                                            if not display:
                                                display = decoded
                                        elif display_en is None:
                                            display_en = decoded
                                except Exception:
                                    pass
                        tt.close()
                        if not display:
                            _NAME_OVERRIDES = {'Noto Sans TC': '思源黑體', 'Noto Serif TC': '思源宋體'}
                            display = _NAME_OVERRIDES.get(display_en, display_en) if display_en else os.path.splitext(f)[0]
                        cjk_fonts.append((f, display))
                    except Exception:
                        pass
                for i, (fname, display) in enumerate(cjk_fonts):
                    var = tk.BooleanVar(value=False)
                    path = os.path.join(fonts_dir, fname)
                    label = f"{display}  ({fname})"
                    ttk.Checkbutton(parent, text=label, variable=var).grid(
                        row=i, column=0, sticky="w", padx=4, pady=1)
                    self.font_vars[path] = var

            def _select_all(self):
                for v in self.font_vars.values(): v.set(True)

            def _deselect_all(self):
                for v in self.font_vars.values(): v.set(False)

            def _add_font(self):
                path = filedialog.askopenfilename(
                    title=self._t('browse_add'),
                    filetypes=[("Font files", "*.ttf *.ttc *.otf"), ("All files", "*.*")])
                if not path:
                    return
                var = tk.BooleanVar(value=True)
                row = len(self.font_vars)
                self.font_vars[path] = var
                display = get_font_display_name(path)
                label = f"{display}  ({os.path.basename(path)})"
                ttk.Checkbutton(self._font_inner, text=label, variable=var).grid(
                    row=row, column=0, sticky="w", padx=4, pady=1)

            def _log(self, msg):
                self.log_text.configure(state="normal")
                self.log_text.insert("end", msg + "\n")
                self.log_text.see("end")
                self.log_text.configure(state="disabled")
                self.root.update_idletasks()

            def _log_from_thread(self, msg):
                self.root.after(0, self._log, msg)

            def _update_progress(self, value):
                self.progress["value"] = value

            def _start_convert(self):
                selected_fonts = [p for p, v in self.font_vars.items() if v.get()]
                selected_sizes = [s for s, v in self.size_vars.items() if v.get()]
                if not selected_fonts:
                    messagebox.showwarning(self._t('warn_no_fonts'), self._t('warn_no_fonts_msg'))
                    return
                if not selected_sizes:
                    messagebox.showwarning(self._t('warn_no_sizes'), self._t('warn_no_sizes_msg'))
                    return
                fallback = self.fallback_path.get().strip() or None
                punct_opt = self.force_fallback_punct.get()
                self.btn_convert.configure(state="disabled")
                self.log_text.configure(state="normal")
                self.log_text.delete("1.0", "end")
                self.log_text.configure(state="disabled")
                threading.Thread(target=self._run_convert, args=(selected_fonts, selected_sizes, fallback, punct_opt), daemon=True).start()

            def _run_convert(self, fonts, sizes, fallback, force_punct):
                jobs = [(f, s) for f in fonts for s in sizes]
                total = len(jobs)
                self.progress["maximum"] = total
                self.progress["value"] = 0
                success = failed = 0
                old_stdout = sys.stdout
                sys.stdout = _LogRedirector(self._log_from_thread)
                for i, (font_path, size) in enumerate(jobs, 1):
                    base = os.path.splitext(os.path.basename(font_path))[0]
                    target_size = size
                    render_size = silver_scaled_size(size) if is_silver_font(font_path) else None
                    output_path = os.path.join(self.output_dir.get(), f"{base}_{target_size}pt.bin")
                    self._log_from_thread(f"\n{'='*60}")
                    self._log_from_thread(f"[{i}/{total}] {os.path.basename(font_path)} @ {size}pt → {os.path.basename(output_path)}")
                    self._log_from_thread(f"{'='*60}")
                    try:
                        result = convert_ttf_to_bin(font_path, output_path, size, fallback, render_size, target_size, force_fallback_punct=force_punct)
                        ok = result[0] if isinstance(result, tuple) else result
                        if ok:
                            success += 1
                            if isinstance(result, tuple) and len(result) >= 3:
                                fb_count, total_glyphs = result[1], result[2]
                                if total_glyphs > 0 and fb_count > total_glyphs * 0.5:
                                    self._log_from_thread(f"⚠ WARNING: {fb_count}/{total_glyphs} glyphs ({100*fb_count//total_glyphs}%) from fallback — font may not look correct!")
                        else:
                            failed += 1
                            self._log_from_thread(f"✗ Failed: {os.path.basename(font_path)} @ {size}pt")
                    except Exception as e:
                        failed += 1
                        self._log_from_thread(f"✗ Error: {e}")
                    self.root.after(0, self._update_progress, i)
                sys.stdout = old_stdout
                self._log_from_thread('\n' + self._t('done').format(success=success, failed=failed, total=total))
                self.root.after(0, lambda: self.btn_convert.configure(state="normal"))

        root = tk.Tk()
        ConvertGUI(root)
        root.mainloop()
    else:
        # ---- CLI mode ----
        ttf_path = args.ttf
        font_size = args.size
        fallback_path = args.fallback
        render_size = args.render_size

        # For Silver fonts, auto-compute render size from the scale table
        target_size = font_size  # Keep original nominal size
        if is_silver_font(ttf_path) and render_size is None:
            render_size = silver_scaled_size(font_size)
            print(f"Silver font detected: nominal {target_size}pt → render {render_size}pt (ratio {render_size/target_size:.2f})")

        base = os.path.splitext(ttf_path)[0]
        output_path = args.output if args.output else f"{base}_{target_size}pt.bin"
        
        if not os.path.exists(ttf_path):
            print(f"Error: Font file not found: {ttf_path}")
            sys.exit(1)
        
        result = convert_ttf_to_bin(ttf_path, output_path, font_size, fallback_path, render_size, target_size)
        ok = result[0] if isinstance(result, tuple) else result
        sys.exit(0 if ok else 1)
