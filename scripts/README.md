# Scripts

Python build tools and utilities for asset conversion and analysis.

## Conversion Scripts

These scripts convert assets into C headers or binary formats for embedding in the firmware:

| Script | Description |
|--------|-------------|
| `convert_labels.py` | Renders Chinese UI strings to 4-bit grayscale bitmap C headers (`src/labels/`). The core build tool — generates ~1200 pre-rendered label bitmaps. |
| `convert_cover.py` | Converts `assets/s3cover.jpg` boot splash image to a C header (`src/s3cover_jpg.h`) |
| `convert_sleeping.py` | Converts `assets/sleeping.jpg` sleep screen image to a C header (`src/sleeping_jpg.h`) |
| `convert_icons.py` | Converts PNG icons in `assets/icons/` to C headers in `src/icons/` |
| `convert_cangjie.py` | Converts `data/cangjie5.dict.yaml` to binary lookup format (`assets/cangjie5.bin`) |
| `convert_ttf_to_bin.py` | Converts TTF fonts to pre-rendered BIN format for faster loading |

## Analysis & Testing Scripts

| Script | Description |
|--------|-------------|
| `analyze_labels.py` | Analyzes label image rendering for debugging |
| `analyze_cangjie.py` | Analyzes the Cangjie dictionary (character count, code distribution) |
| `check_missing_chars.py` | Checks for missing characters in font binary files |
| `test_font_header.py` | Tests and validates generated font header files |
| `verify_bazi.py` | Verifies 八字 (Four Pillars) calculations against reference data |

## Usage

All scripts auto-detect the project root directory, so they can be run from anywhere:

```bash
# From project root
python3 scripts/convert_labels.py

# Or from scripts directory
cd scripts && python3 convert_labels.py
```

## Requirements

- Python 3
- `Pillow` (PIL) — for image and font rendering
- A TTF font file (default: `assets/fonts/GenYoMinTW-Regular.ttf`)
