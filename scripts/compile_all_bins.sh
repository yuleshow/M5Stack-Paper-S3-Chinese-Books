#!/bin/bash
# Compile all Chinese fonts to BIN at 44px
# GenYoMinTW is used as fallback for missing glyphs (including vertical punctuation)
# Silver renders smaller than other fonts, so it's rendered at 61px (44 * 1.38)
cd "$(dirname "$0")/.."

TARGET_SIZE=44
SILVER_RENDER_SIZE=61  # 44 * 1.38 ratio to match other fonts' visual size
FALLBACK_TTF="sd_card/fonts/GenYoMinTW-Regular.ttf"
PYTHON_BIN="python3"
if [[ -x ".venv/bin/python" ]]; then
  PYTHON_BIN=".venv/bin/python"
fi

FONTS=(
  "sd_card/fonts/Silver.ttf"
  "sd_card/fonts/BpmfZihiKaiStd-Regular.ttf"
  "sd_card/fonts/Huiwenmincho-improved.ttf"
  "sd_card/fonts/MingLiU.ttf"
  "sd_card/fonts/TW-Kai-98_1.ttf"
  "sd_card/fonts/GenYoMinTW-Regular.ttf"
)

for ttf in "${FONTS[@]}"; do
  base=$(basename "$ttf" .ttf)
  # Remove "-Regular" and "-improved" suffixes for cleaner output names
  base=$(echo "$base" | sed 's/-Regular//; s/-improved//')
  bin="sd_card/fonts/${base}.bin"
  echo ""
  echo "========================================"

  if [[ "$base" == "Silver" ]]; then
    echo "Compiling: $ttf -> $bin (header=${TARGET_SIZE}px, render=${SILVER_RENDER_SIZE}px)"
    echo "========================================"
    "$PYTHON_BIN" scripts/convert_ttf_to_bin.py "$ttf" -o "$bin" -s $TARGET_SIZE -f "$FALLBACK_TTF" -r $SILVER_RENDER_SIZE
  elif [[ "$ttf" == "$FALLBACK_TTF" ]]; then
    echo "Compiling: $ttf -> $bin (${TARGET_SIZE}px, no fallback)"
    echo "========================================"
    "$PYTHON_BIN" scripts/convert_ttf_to_bin.py "$ttf" -o "$bin" -s $TARGET_SIZE
  else
    echo "Compiling: $ttf -> $bin (${TARGET_SIZE}px, fallback=${FALLBACK_TTF})"
    echo "========================================"
    "$PYTHON_BIN" scripts/convert_ttf_to_bin.py "$ttf" -o "$bin" -s $TARGET_SIZE -f "$FALLBACK_TTF"
  fi
  echo "Done: $bin"
done

echo ""
echo "All fonts compiled. Listing results:"
ls -lh sd_card/fonts/*.bin
