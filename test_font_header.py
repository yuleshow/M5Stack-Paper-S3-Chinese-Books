#!/usr/bin/env python3
"""
Quick script to inspect MingLiU.bin header structure
M5ReadPaper binary font format (from their code):
- Header: 137 bytes
  - char_count: 4 bytes (uint32_t)
  - font_size: 1 byte (uint8_t)
  - version: 4 bytes (uint32_t)
  - family_name: 64 bytes (char array)
  - style_name: 64 bytes (char array)
"""

import struct
import sys

def read_bin_font_header(filepath):
    """Read M5ReadPaper binary font header"""
    try:
        with open(filepath, 'rb') as f:
            # Read first 137 bytes
            header = f.read(137)
            
            if len(header) < 137:
                print(f"❌ File too small: {len(header)} bytes")
                return None
            
            # Parse header
            char_count = struct.unpack('<I', header[0:4])[0]
            font_size = header[4]
            version = struct.unpack('<I', header[5:9])[0]
            family_name = header[9:73].decode('utf-8', errors='ignore').rstrip('\x00')
            style_name = header[73:137].decode('utf-8', errors='ignore').rstrip('\x00')
            
            print("=" * 60)
            print("M5ReadPaper Binary Font Header")
            print("=" * 60)
            print(f"Character Count: {char_count}")
            print(f"Font Size: {font_size} pt")
            print(f"Version: {version}")
            print(f"Family Name: '{family_name}'")
            print(f"Style Name: '{style_name}'")
            print()
            
            # Calculate expected sizes
            index_size = char_count * 20  # 20 bytes per glyph index
            header_and_index_size = 137 + index_size
            
            # Get actual file size
            f.seek(0, 2)
            file_size = f.tell()
            
            print(f"Header size: 137 bytes")
            print(f"Index size: {index_size} bytes ({char_count} glyphs × 20 bytes)")
            print(f"Expected header+index: {header_and_index_size} bytes")
            print(f"Actual file size: {file_size} bytes")
            print(f"Bitmap data: ~{file_size - header_and_index_size} bytes")
            print()
            
            # Read first few index entries
            print("First 5 glyph index entries:")
            print("-" * 60)
            f.seek(137)
            for i in range(min(5, char_count)):
                index_entry = f.read(20)
                if len(index_entry) == 20:
                    # Format: unicode(4) + width(2) + height(2) + offset(4) + size(4) + reserved(4)
                    unicode_val, width, height, offset, size = struct.unpack('<IHHII', index_entry[:16])
                    char = chr(unicode_val) if unicode_val < 0x10000 else '?'
                    print(f"  [{i}] U+{unicode_val:04X} '{char}' - {width}×{height}px, offset={offset}, size={size}")
            
            return {
                'char_count': char_count,
                'font_size': font_size,
                'version': version,
                'family_name': family_name,
                'style_name': style_name,
                'file_size': file_size
            }
            
    except Exception as e:
        print(f"❌ Error reading font: {e}")
        return None

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 test_font_header.py <path_to_font.bin>")
        sys.exit(1)
    
    read_bin_font_header(sys.argv[1])
