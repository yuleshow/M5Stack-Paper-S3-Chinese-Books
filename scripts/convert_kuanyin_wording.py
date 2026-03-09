#!/usr/bin/env python3
"""
Convert kuanyin.csv into kuanyin_wording.bin for M5Stack Paper S3.

Binary format:
  Header (12 bytes):
    [0..3]   Magic: "FSWP" (Fortune Slip Wording Pack)
    [4..5]   Count (uint16 LE)
    [6..7]   Number of fields per slip (uint16 LE)
    [8..11]  Reserved (uint32 LE, 0)
  Index (count * 4 bytes):
    uint32 LE offset - absolute file offset to each slip's data block
  Data (per slip):
    Each slip is a sequence of null-terminated UTF-8 strings.
    Fields in order:
      0: 籤號 (slip number, e.g. "第一籤")
      1: 等級 (rank, e.g. "上籤")
      2: 宮位 (palace, e.g. "子宮")
      3: 詩曰一 (poem 1, lines joined with \\n)
      4: 詩意 (poem meaning)
      5: 解曰 (interpretation)
      6: 故事 (story title)
      7: 故事內容 (story text)
      8..22: 聖意 fields: 家宅,自身,求財,交易,婚姻,六甲,行人,田蠶,六畜,尋人,訟詞,移徙,失物,疾病,山墳

Usage:
  python3 convert_kuanyin_wording.py

Reads:  assets/Fortune_Slips/kuanyin/kuanyin.csv
Writes: sd_card/fortune_slips/kuanyin_wording.bin
"""

import csv
import os
import struct
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

INPUT_CSV = "assets/Fortune_Slips/kuanyin/kuanyin.csv"
OUTPUT_BIN = "sd_card/fortune_slips/kuanyin_wording.bin"

# CSV columns: #,籤號,等級,宮位,詩曰一,詩曰二,詩意,解曰,故事,故事內容,
#              家宅,自身,求財,交易,婚姻,六甲,行人,田蠶,六畜,尋人,訟詞,移徙,失物,疾病,山墳

# Fields to extract (by CSV column index):
#  1=籤號, 2=等級, 3=宮位, 4=詩曰一, 6=詩意, 7=解曰, 8=故事, 9=故事內容,
#  10-24=聖意 (家宅,自身,求財,交易,婚姻,六甲,行人,田蠶,六畜,尋人,訟詞,移徙,失物,疾病,山墳)
FIELD_INDICES = [1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]
NUM_FIELDS = len(FIELD_INDICES)


def main():
    if not os.path.exists(INPUT_CSV):
        print(f"ERROR: CSV not found: {INPUT_CSV}")
        sys.exit(1)

    # Read CSV
    slips = []
    with open(INPUT_CSV, 'r', encoding='utf-8-sig') as f:
        reader = csv.reader(f)
        header = next(reader)
        print(f"CSV columns ({len(header)}): {header}")
        for row in reader:
            if not row or not row[0].strip():
                continue
            fields = []
            for idx in FIELD_INDICES:
                val = row[idx].strip() if idx < len(row) else ""
                fields.append(val)
            slips.append(fields)

    count = len(slips)
    print(f"Read {count} slips from CSV")

    # Encode each slip as concatenated null-terminated UTF-8 strings
    slip_blobs = []
    for fields in slips:
        blob = b""
        for field in fields:
            blob += field.encode('utf-8') + b'\x00'
        slip_blobs.append(blob)

    # Calculate layout
    header_size = 12
    index_size = count * 4
    data_start = header_size + index_size

    offsets = []
    current = data_start
    for blob in slip_blobs:
        offsets.append(current)
        current += len(blob)

    total_size = current

    # Write binary
    os.makedirs(os.path.dirname(OUTPUT_BIN), exist_ok=True)
    with open(OUTPUT_BIN, 'wb') as f:
        # Header
        f.write(b'FSWP')
        f.write(struct.pack('<H', count))
        f.write(struct.pack('<H', NUM_FIELDS))
        f.write(struct.pack('<I', 0))  # reserved

        # Index
        for off in offsets:
            f.write(struct.pack('<I', off))

        # Data
        for blob in slip_blobs:
            f.write(blob)

    print(f"\nWrote {OUTPUT_BIN}")
    print(f"  Slips: {count}")
    print(f"  Fields per slip: {NUM_FIELDS}")
    print(f"  Total size: {total_size:,} bytes ({total_size/1024:.1f} KB)")

    # Verify by reading back first slip
    with open(OUTPUT_BIN, 'rb') as f:
        magic = f.read(4)
        cnt, nf, _ = struct.unpack('<HHI', f.read(8))
        off0 = struct.unpack('<I', f.read(4))[0]
        f.seek(off0)
        # Read null-terminated strings
        print(f"\nVerification (slip #1):")
        labels = ['籤號','等級','宮位','詩曰一','詩意','解曰','故事','故事內容',
                  '家宅','自身','求財','交易','婚姻','六甲','行人','田蠶','六畜','尋人','訟詞','移徙','失物','疾病','山墳']
        for i in range(nf):
            s = b""
            while True:
                ch = f.read(1)
                if ch == b'\x00' or ch == b'':
                    break
                s += ch
            val = s.decode('utf-8')
            label = labels[i] if i < len(labels) else f"field{i}"
            display = val[:50] + "..." if len(val) > 50 else val
            print(f"  {label}: {display}")


if __name__ == '__main__':
    main()
