#!/usr/bin/env python3
"""Analyze the Cangjie5 dictionary and generate binary lookup table."""
import os
import re
from collections import Counter, defaultdict

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

entries = []
in_data = False
with open('data/cangjie5.dict.yaml', 'r', encoding='utf-8') as f:
    for line in f:
        if line.strip() == '...':
            in_data = True
            continue
        if not in_data:
            continue
        parts = line.strip().split('\t')
        if len(parts) >= 2:
            char, code = parts[0], parts[1]
            if re.match(r'^[a-z]+$', code) and len(char) == 1:
                entries.append((char, code))

# Unicode range distribution
ranges = Counter()
for ch, _ in entries:
    cp = ord(ch)
    if 0x4E00 <= cp <= 0x9FFF:
        ranges['CJK Basic'] += 1
    elif 0x3400 <= cp <= 0x4DBF:
        ranges['CJK Ext-A'] += 1
    elif 0x2E80 <= cp <= 0x2FFF:
        ranges['Radicals'] += 1
    elif 0x3000 <= cp <= 0x303F:
        ranges['CJK Symbols'] += 1
    elif 0xF900 <= cp <= 0xFAFF:
        ranges['CJK Compat'] += 1
    else:
        ranges['Other'] += 1

print("Unicode range distribution:")
for r, c in sorted(ranges.items(), key=lambda x: -x[1]):
    print(f"  {r}: {c}")

# Unique codes
code_to_chars = defaultdict(list)
for ch, code in entries:
    code_to_chars[code].append(ch)

print(f"\nTotal entries: {len(entries)}")
print(f"Unique codes: {len(code_to_chars)}")
chars_per_code = Counter(len(v) for v in code_to_chars.values())
print("Chars per code distribution:")
for k in sorted(chars_per_code):
    print(f"  {k} chars: {chars_per_code[k]} codes")

# Size estimates
print(f"\nBinary size (7 bytes/entry): {len(entries)*7/1024:.1f} KB")
cjk_basic = [(ch, code) for ch, code in entries if 0x4E00 <= ord(ch) <= 0x9FFF]
print(f"CJK Basic only: {len(cjk_basic)} entries = {len(cjk_basic)*7/1024:.1f} KB")
