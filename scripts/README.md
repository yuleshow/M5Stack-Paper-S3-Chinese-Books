# Scripts

![Python](https://img.shields.io/badge/Language-Python_3-green)
![Build Tools](https://img.shields.io/badge/Type-Build_Tools-orange)

Python build tools and utilities for asset conversion and analysis.

## Conversion Scripts ![Conversion](https://img.shields.io/badge/Category-Conversion-orange)

These scripts convert assets into C headers or binary formats for embedding in the firmware:

| Script | Description |
|--------|-------------|
| `convert_labels.py` | Renders Chinese UI strings to 4-bit grayscale bitmap C headers (`src/labels/`). The core build tool — generates ~1200 pre-rendered label bitmaps. |
| `convert_cover.py` | Converts `assets/s3cover.jpg` boot splash image to a C header (`src/s3cover_jpg.h`) |
| `convert_sleeping.py` | Converts `assets/sleeping.jpg` sleep screen image to a C header (`src/sleeping_jpg.h`) |
| `convert_icons.py` | Converts PNG icons in `assets/icons/` to C headers in `src/icons/` |
| `convert_cangjie.py` | Converts `data/cangjie5.dict.yaml` to binary lookup format (`assets/cangjie5.bin`) |
| `convert_ttf_to_bin.py` | Converts TTF fonts to pre-rendered BIN format for faster loading. Supports fallback font borrowing and render-size scaling (e.g. Silver at 61px → 44px grid). Auto-rotates horizontal bracket glyphs 90° CW to synthesize missing vertical forms. Each glyph is centered both horizontally and vertically within its em-square cell. Run with `--gui` for a graphical interface. |

### Font Converter GUI

![Font Converter GUI](convert_ttf_to_bin-gui.png)

Run `python3 convert_ttf_to_bin.py --gui` to launch the graphical converter. Supports EN/ZH language toggle, font preview, fallback font selection, and batch conversion.

| Script | Description |
|--------|-------------|
| `compile_all_bins.sh` | Batch-compiles all TTF fonts to BIN using `convert_ttf_to_bin.py`. Handles Silver's render-size scaling automatically. |

## Analysis & Testing Scripts ![Testing](https://img.shields.io/badge/Category-Analysis-purple)

| Script | Description |
|--------|-------------|
| `analyze_labels.py` | Analyzes label image rendering for debugging |
| `analyze_cangjie.py` | Analyzes the Cangjie dictionary (character count, code distribution) |
| `check_missing_chars.py` | Checks for missing characters in font binary files |
| `check_vert_punct.py` | Checks vertical punctuation glyph coverage across fonts |
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
