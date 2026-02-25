#!/usr/bin/env python3
"""
Convert TTF font to M5ReadPaper binary format
Based on M5ReadPaper's font structure
"""

from PIL import Image, ImageDraw, ImageFont
import struct
import sys
import os

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
    ]
    
    for start, end in common_ranges:
        chars.update(chr(i) for i in range(start, end))
    
    return sorted(list(chars), key=lambda x: ord(x))

def render_glyph(font, char, font_size):
    """Render a single character to bitmap"""
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
            return None, 0, 0
        
        # Draw text
        draw.text((0, 0), char, font=font, fill=0)  # Black text
        
        # Crop to actual content
        img_crop = img.crop(bbox)
        
        return img_crop, text_width, text_height
    except Exception as e:
        print(f"Error rendering '{char}' (U+{ord(char):04X}): {e}")
        return None, 0, 0

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

def convert_ttf_to_bin(ttf_path, output_path, font_size=30):
    """Convert TTF to M5ReadPaper binary format"""
    print(f"Loading font: {ttf_path}")
    print(f"Font size: {font_size}pt")
    
    try:
        font = ImageFont.truetype(ttf_path, font_size)
    except Exception as e:
        print(f"Error loading font: {e}")
        return False
    
    # Get character set
    print("Building character set...")
    chars = get_common_chinese_chars()
    print(f"Total characters: {len(chars)}")
    
    # Prepare index and bitmap data
    index_entries = []
    bitmap_data = bytearray()
    
    # First pass: render all glyphs to get dimensions and bitmap data
    print("Rendering glyphs...")
    for i, char in enumerate(chars):
        if i % 100 == 0:
            print(f"  Progress: {i}/{len(chars)} ({100*i//len(chars)}%)")
        
        img, width, height = render_glyph(font, char, font_size)
        
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
            'bitmap': bitmap_bytes  # Store temporarily
        })
    
    print(f"Successfully rendered {len(index_entries)} glyphs")
    
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
        version = 1
        family_name = b"MingLiU".ljust(64, b'\x00')
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
            # Padding to 20 bytes
            f.write(struct.pack('<I', 0))                     # 4 bytes padding
        
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
        print("Usage: python3 convert_ttf_to_bin.py <font.ttf> [output.bin] [font_size]")
        print("Example: python3 convert_ttf_to_bin.py MingLiU.ttf MingLiU_30pt.bin 30")
        sys.exit(1)
    
    ttf_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else ttf_path.replace('.ttf', '_30pt.bin')
    font_size = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    
    if not os.path.exists(ttf_path):
        print(f"Error: Font file not found: {ttf_path}")
        sys.exit(1)
    
    success = convert_ttf_to_bin(ttf_path, output_path, font_size)
    sys.exit(0 if success else 1)
