#!/usr/bin/env python3
"""Generate S2T (Simplified→Traditional) and T2S (Traditional→Simplified) binary tables.

Output format (little-endian):
  4 bytes: magic ("S2T\0" or "T2S\0")
  4 bytes: uint32 entry count
  count × 4 bytes: (uint16 source_codepoint, uint16 target_codepoint), sorted by source

Requires: pip3 install opencc-python-reimplemented
"""
import struct
import sys
import os

try:
    from opencc import OpenCC
except ImportError:
    print("Error: opencc-python-reimplemented not installed.")
    print("Install with: pip3 install opencc-python-reimplemented")
    sys.exit(1)


def build_char_map(converter):
    """Build a one-to-one character mapping by testing every CJK codepoint."""
    mapping = {}
    ranges = [
        (0x3400, 0x4DC0),   # CJK Extension A
        (0x4E00, 0xA000),   # CJK Unified Ideographs
        (0xF900, 0xFB00),   # CJK Compatibility Ideographs
    ]
    skipped = 0
    for start, end in ranges:
        for cp in range(start, end):
            src = chr(cp)
            dst = converter.convert(src)
            if len(dst) == 1 and dst != src:
                dst_cp = ord(dst)
                if cp <= 0xFFFF and dst_cp <= 0xFFFF:
                    mapping[cp] = dst_cp
                else:
                    skipped += 1
    if skipped:
        print(f"  (skipped {skipped} entries with codepoints > U+FFFF)")
    return mapping


def write_table(filepath, magic, mapping):
    """Write binary table with magic header, count, and sorted entries."""
    # Sort by source codepoint
    entries = sorted(mapping.items())
    with open(filepath, 'wb') as f:
        f.write(magic.encode('ascii'))
        f.write(b'\x00' * (4 - len(magic)))  # pad magic to 4 bytes
        f.write(struct.pack('<I', len(entries)))
        for src, dst in entries:
            f.write(struct.pack('<HH', src, dst))
    print(f"  {filepath}: {len(entries)} entries, {os.path.getsize(filepath)} bytes")


def main():
    output_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data')
    os.makedirs(output_dir, exist_ok=True)

    print("Building S2T mapping (Simplified → Traditional)...")
    s2t_cc = OpenCC('s2t')
    s2t_map = build_char_map(s2t_cc)

    print("Building T2S mapping (Traditional → Simplified)...")
    t2s_cc = OpenCC('t2s')
    t2s_map = build_char_map(t2s_cc)

    print(f"\nS2T: {len(s2t_map)} one-to-one mappings")
    print(f"T2S: {len(t2s_map)} one-to-one mappings")

    s2t_path = os.path.join(output_dir, 's2t.bin')
    t2s_path = os.path.join(output_dir, 't2s.bin')

    write_table(s2t_path, 'S2T', s2t_map)
    write_table(t2s_path, 'T2S', t2s_map)

    print("\nDone! Copy s2t.bin and t2s.bin to SD card root.")


if __name__ == '__main__':
    main()
