#!/usr/bin/env python3
"""
Convert ECDICT stardict.db (SQLite) to sorted en-zh.txt for SD card dictionary.

Usage:
    python3 scripts/convert_ecdict.py path/to/stardict.db

Output:
    sd_card/dict/en-zh.txt  (tab-separated: word<TAB>chinese_definition)

The output file is sorted alphabetically for binary search on ESP32.
Only entries with Chinese translations are included.
Filters to common words (~30-50K entries, ~3-5MB).
"""

import sqlite3
import os
import re
import sys

try:
    from opencc import OpenCC
    s2t = OpenCC('s2t')  # Simplified to Traditional
except ImportError:
    print("Error: opencc-python-reimplemented not installed.")
    print("Install with: pip3 install opencc-python-reimplemented")
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/convert_ecdict.py <path/to/stardict.db>")
        print("\nDownload from: https://github.com/skywind3000/ECDICT/releases")
        sys.exit(1)

    db_path = sys.argv[1]
    if not os.path.exists(db_path):
        print(f"Error: {db_path} not found")
        sys.exit(1)

    # Output path
    out_dir = os.path.join(os.path.dirname(__file__), "..", "sd_card", "dict")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "en-zh.txt")

    print(f"Reading {db_path}...")
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    # Get column names to understand schema
    cur.execute("PRAGMA table_info(stardict)")
    columns = [row[1] for row in cur.fetchall()]
    print(f"Columns: {columns}")

    # Query entries that have a Chinese translation
    # ECDICT schema: word, phonetic, definition, translation, pos, collins, oxford,
    #                tag, bnc, frq, exchange, detail, audio
    # 'translation' = Chinese, 'definition' = English definition
    # 'tag' contains frequency tags like 'zk' (中考), 'gk' (高考), 'cet4', 'cet6', etc.
    # 'frq' = word frequency rank, 'bnc' = BNC corpus rank
    # 'collins' = Collins star rating (1-5)

    cur.execute("""
        SELECT word, translation, definition, phonetic, tag, bnc, frq, collins
        FROM stardict
        WHERE translation IS NOT NULL AND translation != ''
    """)

    entries = []
    seen = set()

    for row in cur.fetchall():
        word, translation, eng_def, phonetic, tag, bnc, frq, collins = row

        # Skip non-word entries (numbers, symbols, multi-word phrases > 3 words)
        if not word or not translation:
            continue
        word = word.strip()
        if not word:
            continue

        # Skip entries with digits or special chars at start
        if not word[0].isalpha():
            continue

        # Skip very long "words" (phrases)
        if len(word) > 40:
            continue

        # Allow up to 3-word phrases (common phrasal verbs like "give up")
        if word.count(' ') > 2:
            continue

        # Deduplicate (case-insensitive)
        key = word.lower()
        if key in seen:
            continue
        seen.add(key)

        # Clean up translation: take first line, limit length for SD card
        trans = translation.strip()
        # Remove "\\n" literal or actual newlines — keep first meaning
        trans = trans.replace("\\n", "\n")
        lines = trans.split("\n")
        # Take up to 3 meaning lines, join with semicolon
        meanings = []
        for line in lines:
            line = line.strip()
            if line:
                meanings.append(line)
            if len(meanings) >= 3:
                break
        trans = "; ".join(meanings)

        # Limit definition length (popup has limited space)
        if len(trans) > 200:
            trans = trans[:197] + "..."

        # Skip if translation is empty after cleanup
        if not trans:
            continue

        # Priority scoring for filtering
        # Lower score = higher priority (more common word)
        score = 100000
        if bnc and bnc > 0:
            score = min(score, bnc)
        if frq and frq > 0:
            score = min(score, frq)
        if collins and collins > 0:
            score -= collins * 5000  # Collins 5-star = -25000
        if tag:
            # Frequency tags boost priority
            if 'zk' in tag:   score -= 10000  # 中考 (middle school)
            if 'gk' in tag:   score -= 8000   # 高考 (college entrance)
            if 'cet4' in tag: score -= 6000
            if 'cet6' in tag: score -= 4000
            if 'ky' in tag:   score -= 3000   # 考研
            if 'toefl' in tag: score -= 2000
            if 'ielts' in tag: score -= 2000
            if 'gre' in tag:  score -= 1000

        entries.append((score, key, word, trans))

    conn.close()
    print(f"Total entries with Chinese translation: {len(entries)}")

    # Sort by priority, take top N
    entries.sort(key=lambda x: x[0])
    max_entries = 50000
    if len(entries) > max_entries:
        entries = entries[:max_entries]
        print(f"Filtered to top {max_entries} by frequency")

    # Sort alphabetically by lowercase word for binary search
    entries.sort(key=lambda x: x[1])

    # Write output (convert definitions to Traditional Chinese)
    with open(out_path, "w", encoding="utf-8") as f:
        for score, key, word, trans in entries:
            # Convert simplified Chinese to traditional Chinese
            trans_trad = s2t.convert(trans)
            # Use lowercase word for consistent lookup
            f.write(f"{key}\t{trans_trad}\n")

    file_size = os.path.getsize(out_path)
    print(f"Written {len(entries)} entries to {out_path}")
    print(f"File size: {file_size:,} bytes ({file_size/1024/1024:.1f} MB)")
    print(f"\nCopy sd_card/dict/en-zh.txt to your SD card's /dict/ folder.")


if __name__ == "__main__":
    main()
