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
    ]
    
    for start, end in common_ranges:
        chars.update(chr(i) for i in range(start, end))
    
    return sorted(list(chars), key=lambda x: ord(x))

def render_glyph(font, char, font_size):
    """Render a single character to bitmap, returning bearing offsets"""
    # Create a larger canvas to avoid clipping
    canvas_size = font_size * 3
    img = Image.new('1', (canvas_size, canvas_size), 1)  # 1-bit, white background
    draw = ImageDraw.Draw(img)
    
    try:
        # Get text bounding box
        bbox = draw.textbbox((0, 0), char, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        if text_width <= 0 or text_height <= 0:
            return None, 0, 0, 0, 0
        
        # Draw text
        draw.text((0, 0), char, font=font, fill=0)  # Black text
        
        # Crop to actual content
        img_crop = img.crop(bbox)
        
        # Return bearing offsets (position of tight crop within the em-square)
        bearing_x = bbox[0]  # X offset from origin to glyph left edge
        bearing_y = bbox[1]  # Y offset from origin to glyph top edge
        
        return img_crop, text_width, text_height, bearing_x, bearing_y
    except Exception as e:
        print(f"Error rendering '{char}' (U+{ord(char):04X}): {e}")
        return None, 0, 0, 0, 0

def bitmap_to_bytes(img):
    """Convert PIL 1-bit image to packed bytes"""
    width, height = img.size
    bits = []
    
    for y in range(height):
        for x in range(width):
            pixel = img.getpixel((x, y))
            bits.append(1 if pixel == 0 else 0)  # Black = 1, White = 0
    
    # Pad to byte boundary
    while len(bits) % 8 != 0:
        bits.append(0)
    
    # Pack into bytes
    bytes_data = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            if i + j < len(bits):
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

def convert_ttf_to_bin(ttf_path, output_path, font_size=30, fallback_path=None, render_size=None):
    """Convert TTF to M5ReadPaper binary format.
    If fallback_path is given, borrow missing glyphs from the fallback font.
    If render_size is given, render glyphs at that size but store font_size in header."""
    if render_size is None:
        render_size = font_size
    print(f"Loading font: {ttf_path}")
    print(f"Font size: {font_size}pt (render at {render_size}pt)")
    if fallback_path:
        print(f"Fallback font: {fallback_path}")
    
    # Extract real font family name from the TTF/TTC file
    # Prefer Chinese name: Traditional Chinese (langID=1028), Simplified Chinese (langID=2052)
    try:
        tt = TTFont(ttf_path)
        name_table = tt['name']
        font_family = None
        font_family_en = None
        # Collect all nameID=1 records, prefer CJK languages
        cjk_lang_ids = {1028, 2052, 3076, 1041, 1042}  # zh-TW, zh-CN, zh-HK, ja, ko
        for record in name_table.names:
            if record.nameID == 1:
                try:
                    decoded = record.toUnicode()
                    if decoded:
                        if record.platformID == 3 and record.langID in cjk_lang_ids:
                            font_family = decoded
                            break  # Found CJK name, use it
                        elif record.platformID == 1 and record.platEncID == 2:
                            # Mac platform with CJK encoding
                            font_family = decoded
                        elif font_family_en is None:
                            font_family_en = decoded
                except:
                    pass
        tt.close()
        if not font_family:
            font_family = font_family_en
        if not font_family:
            font_family = os.path.splitext(os.path.basename(ttf_path))[0]
        print(f"Font family: {font_family}")
    except Exception as e:
        print(f"Warning: Could not extract font name: {e}")
        font_family = os.path.splitext(os.path.basename(ttf_path))[0]
    
    try:
        font = ImageFont.truetype(ttf_path, render_size)
    except Exception as e:
        print(f"Error loading font: {e}")
        return False
    
    # Load fallback font if specified
    # Fallback renders at font_size (not render_size) since it already has correct visual size
    fallback_font = None
    if fallback_path:
        try:
            fallback_font = ImageFont.truetype(fallback_path, font_size)
        except Exception as e:
            print(f"Warning: Could not load fallback font: {e}")
    
    # Auto-detect fallback if not specified: use GenYoMinTW if available
    if not fallback_font:
        auto_fallback = 'sd_card/fonts/GenYoMinTW-Regular.ttf'
        if os.path.exists(auto_fallback) and os.path.abspath(auto_fallback) != os.path.abspath(ttf_path):
            try:
                fallback_font = ImageFont.truetype(auto_fallback, font_size)
                print(f"Auto-detected fallback font: {auto_fallback}")
            except:
                pass
    
    # Get character set
    print("Building character set...")
    chars = get_common_chinese_chars()
    print(f"Total characters: {len(chars)}")
    
    # Prepare index and bitmap data
    index_entries = []
    bitmap_data = bytearray()
    
    # First pass: render all glyphs to get dimensions and bitmap data
    print("Rendering glyphs...")
    fallback_count = 0
    rotated_count = 0
    for i, char in enumerate(chars):
        if i % 100 == 0:
            print(f"  Progress: {i}/{len(chars)} ({100*i//len(chars)}%)")
        
        img, width, height, bearing_x, bearing_y = render_glyph(font, char, render_size)
        
        # When render_size != font_size (e.g. Silver), re-center bearing within font_size cell
        if img is not None and render_size != font_size:
            bearing_x = (font_size - width) // 2
            bearing_y = (font_size - height) // 2
        
        # For missing vertical bracket forms: try rotating the horizontal counterpart
        if img is None and ord(char) in VERT_BRACKETS_TO_HORIZ:
            horiz_cp = VERT_BRACKETS_TO_HORIZ[ord(char)]
            img, width, height, bearing_x, bearing_y = render_rotated_glyph(font, chr(horiz_cp), render_size)
            if img is not None:
                rotated_count += 1
                if render_size != font_size:
                    bearing_x = (font_size - width) // 2
                    bearing_y = (font_size - height) // 2
        
        # Try fallback font if still missing (at font_size, not render_size)
        if img is None and fallback_font:
            img, width, height, bearing_x, bearing_y = render_glyph(fallback_font, char, font_size)
            if img is not None:
                fallback_count += 1
        
        if img is None:
            # Skip characters that can't be rendered
            continue
        
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
        f.write(struct.pack('<B', font_size))   # font_size (1 byte, offset 4)
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
    
    return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 convert_ttf_to_bin.py <font.ttf> [output.bin] [font_size] [fallback.ttf] [render_size]")
        print("Example: python3 convert_ttf_to_bin.py MingLiU.ttf MingLiU_30pt.bin 30")
        print("Fallback: Missing glyphs are borrowed from fallback font (auto-detects GenYoMinTW)")
        print("render_size: Render at this size but store font_size in header (for small fonts like Silver)")
        sys.exit(1)
    
    ttf_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else ttf_path.replace('.ttf', '_30pt.bin')
    font_size = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    fallback_path = sys.argv[4] if len(sys.argv) > 4 else None
    render_size = int(sys.argv[5]) if len(sys.argv) > 5 else None
    
    if not os.path.exists(ttf_path):
        print(f"Error: Font file not found: {ttf_path}")
        sys.exit(1)
    
    success = convert_ttf_to_bin(ttf_path, output_path, font_size, fallback_path, render_size)
    sys.exit(0 if success else 1)
