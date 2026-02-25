#!/usr/bin/env python3
"""
Convert Cangjie5 dictionary (RIME YAML format) to compact binary lookup table.

Binary format:
  Header (8 bytes):
    - magic: "CJ5\0" (4 bytes)
    - entry_count: uint32_t little-endian
  Entries (7 bytes each, sorted by code for binary search):
    - code: 5 bytes (lowercase a-z, null-padded)
    - unicode: uint16_t little-endian (BMP codepoint)

Usage: python3 convert_cangjie.py data/cangjie5.dict.yaml assets/cangjie5.bin
"""

import os
import struct
import re
import sys
from collections import defaultdict

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def parse_cangjie_yaml(yaml_path):
    """Parse RIME cangjie5.dict.yaml and extract single-char entries."""
    entries = []
    in_data = False
    with open(yaml_path, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip() == '...':
                in_data = True
                continue
            if not in_data:
                continue
            parts = line.strip().split('\t')
            if len(parts) >= 2:
                char, code = parts[0], parts[1]
                # Only single characters, only a-z codes, max 5 chars
                if (re.match(r'^[a-z]{1,5}$', code) and
                        len(char) == 1 and
                        0x4E00 <= ord(char) <= 0x9FFF):  # CJK Basic block
                    entries.append((char, code))
    return entries


def generate_binary(entries, output_path):
    """Generate compact binary lookup table sorted by code."""
    # Sort by code (alphabetical), then by Unicode codepoint for stable order
    entries.sort(key=lambda e: (e[1], ord(e[0])))

    # Remove exact duplicates (same char + same code)
    seen = set()
    unique = []
    for char, code in entries:
        key = (char, code)
        if key not in seen:
            seen.add(key)
            unique.append((char, code))
    entries = unique

    print(f"Writing {len(entries)} entries to {output_path}")

    with open(output_path, 'wb') as f:
        # Header: magic + entry count
        f.write(b'CJ5\x00')
        f.write(struct.pack('<I', len(entries)))

        # Entries: 5-byte code (null-padded) + 2-byte unicode
        for char, code in entries:
            code_bytes = code.encode('ascii').ljust(5, b'\x00')[:5]
            unicode_val = ord(char)
            f.write(code_bytes)
            f.write(struct.pack('<H', unicode_val))

    file_size = 8 + len(entries) * 7
    print(f"Binary size: {file_size} bytes ({file_size / 1024:.1f} KB)")

    # Verification: read back and check
    with open(output_path, 'rb') as f:
        magic = f.read(4)
        count = struct.unpack('<I', f.read(4))[0]
        assert magic == b'CJ5\x00', f"Bad magic: {magic}"
        assert count == len(entries), f"Count mismatch: {count} vs {len(entries)}"

        # Verify first 5 entries
        print("\nFirst 10 entries:")
        for i in range(min(10, count)):
            code_raw = f.read(5)
            uni = struct.unpack('<H', f.read(2))[0]
            code_str = code_raw.rstrip(b'\x00').decode('ascii')
            print(f"  {chr(uni)} ({code_str})")

    # Stats
    code_to_chars = defaultdict(list)
    for char, code in entries:
        code_to_chars[code].append(char)

    print(f"\nUnique codes: {len(code_to_chars)}")
    from collections import Counter
    code_lens = Counter(len(e[1]) for e in entries)
    print("Code length distribution:")
    for k in sorted(code_lens):
        print(f"  {k}-letter codes: {code_lens[k]} entries")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <data/cangjie5.dict.yaml> <output.bin>")
        sys.exit(1)

    yaml_path = sys.argv[1]
    output_path = sys.argv[2]

    print(f"Parsing {yaml_path}...")
    entries = parse_cangjie_yaml(yaml_path)
    print(f"Found {len(entries)} CJK Basic entries")

    generate_binary(entries, output_path)
    print("\nDone!")


if __name__ == '__main__':
    main()
