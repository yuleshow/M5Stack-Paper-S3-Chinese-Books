#!/usr/bin/env python3
"""Generate the label entries for all missing labels, with unique var_suffix values."""
import os, sys, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from convert_labels import LABELS

existing = set()
existing_suffixes = set()
for entry in LABELS:
    existing.add((entry[0], entry[1]))
    existing_suffixes.add(entry[2])

# Scan firmware for literal drawSystemText calls
SRC_DIR = "src"
pattern = re.compile(
    r'drawSystemText(?:Centered)?\s*\(\s*"([^"]+)"\s*,\s*'
    r'[^,]+,\s*'
    r'[^,]+,\s*'
    r'(\d+)'
)

firmware_calls = set()
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
                    firmware_calls.add((text, size))

missing = sorted(firmware_calls - existing, key=lambda x: (x[1], x[0]))

def make_suffix(text, size, used):
    """Generate a unique var_suffix."""
    # Transliterate common patterns
    clean = text.replace(' ', '_').replace('：', '').replace(':', '')
    clean = clean.replace('•', '').replace('（', '_').replace('）', '')
    clean = clean.replace('「', '').replace('」', '').replace('【', '').replace('】', '')
    clean = clean.replace('.', '').replace('-', '_').replace('/', '_')
    clean = clean.strip('_')
    # Keep ASCII, replace CJK with pinyin-ish abbreviation
    base = f"s{size}_{clean[:20]}" if any(ord(c) > 127 for c in clean) else f"s{size}_{clean}"
    # Ensure unique
    suffix = base
    i = 2
    while suffix in used:
        suffix = f"{base}_{i}"
        i += 1
    used.add(suffix)
    return suffix

used_suffixes = set(existing_suffixes)

print(f"    # ── Missing labels found by find_missing_labels.py ──")
for text, size in missing:
    suffix = make_suffix(text, size, used_suffixes)
    print(f'    ("{text}", {size}, "{suffix}"),')
