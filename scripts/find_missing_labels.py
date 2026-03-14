#!/usr/bin/env python3
"""Find literal drawSystemText/drawSystemTextCentered calls that don't have
matching PROGMEM labels in convert_labels.py."""
import os, sys, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from convert_labels import LABELS

# Build set of existing (text, size) pairs
existing = set()
for entry in LABELS:
    existing.add((entry[0], entry[1]))

print(f"Existing LABELS: {len(LABELS)} entries, {len(existing)} unique (text, size) pairs\n")

# Scan all .cpp files for drawSystemText/drawSystemTextCentered calls with literal strings
SRC_DIR = "src"
pattern = re.compile(
    r'drawSystemText(?:Centered)?\s*\(\s*"([^"]+)"\s*,\s*'  # text
    r'[^,]+,\s*'   # x or centerX
    r'[^,]+,\s*'   # y
    r'(\d+)'        # size
)

firmware_calls = set()
call_locations = {}  # (text, size) -> [(file, line)]

for root, dirs, files in os.walk(SRC_DIR):
    for fname in sorted(files):
        if not fname.endswith('.cpp'):
            continue
        fpath = os.path.join(root, fname)
        with open(fpath, 'r', encoding='utf-8') as f:
            for lineno, line in enumerate(f, 1):
                for m in pattern.finditer(line):
                    text = m.group(1)
                    size = int(m.group(2))
                    key = (text, size)
                    firmware_calls.add(key)
                    call_locations.setdefault(key, []).append((fpath, lineno))

print(f"Firmware literal calls: {len(firmware_calls)} unique (text, size) pairs\n")

missing = sorted(firmware_calls - existing, key=lambda x: (x[1], x[0]))
print(f"MISSING from LABELS: {len(missing)}\n")

for text, size in missing:
    locs = call_locations[(text, size)]
    loc_str = ", ".join(f"{f}:{l}" for f, l in locs)
    print(f"  ({repr(text):50s}, {size:3d})  # {loc_str}")

# Also show what IS already covered
covered = firmware_calls & existing
print(f"\nAlready covered: {len(covered)} labels")
