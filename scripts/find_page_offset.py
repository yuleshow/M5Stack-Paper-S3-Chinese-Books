#!/usr/bin/env python3
"""Find the exact text offset for the screenshot page."""
import os, sys
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# Import the extraction function from gen_sample_reading_page
sys.path.insert(0, 'scripts')
from gen_sample_reading_page import extract_epub_full_text

text = extract_epub_full_text('sd_card/books/pg24113-images-3.epub')

# Search for distinctive text from the screenshot
queries = ['一件大喜', '遇了一件大', '得知，今日', '公明了大人']
for q in queries:
    idx = text.find(q)
    if idx >= 0:
        start = max(0, idx - 300)
        end = min(len(text), idx + 100)
        print(f"Found '{q}' at offset {idx}")
        print(f"Context: ...{text[start:end]}...")
        print()
