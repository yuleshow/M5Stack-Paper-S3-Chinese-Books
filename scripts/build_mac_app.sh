#!/bin/bash
# Build macOS .app and .dmg for the Font Converter GUI
# Usage: ./scripts/build_mac_app.sh
#
# Prerequisites:
#   pip install pyinstaller Pillow fontTools

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_NAME="FontConverterBIN"
DIST_DIR="$PROJECT_DIR/dist"
BUILD_DIR="$PROJECT_DIR/build"

echo "=== Building $APP_NAME.app ==="
cd "$PROJECT_DIR"

# Find the Python with pyinstaller installed
if [[ -d "$PROJECT_DIR/.venv-1" ]]; then
    PYTHON="$PROJECT_DIR/.venv-1/bin/python"
elif [[ -d "$PROJECT_DIR/.venv" ]]; then
    PYTHON="$PROJECT_DIR/.venv/bin/python"
else
    PYTHON="python3"
fi

echo "Using Python: $PYTHON"

# Run PyInstaller
"$PYTHON" -m PyInstaller \
    --name "$APP_NAME" \
    --windowed \
    --onedir \
    --noconfirm \
    --clean \
    --hidden-import=PIL \
    --hidden-import=PIL.Image \
    --hidden-import=PIL.ImageDraw \
    --hidden-import=PIL.ImageFont \
    --hidden-import=fontTools \
    --hidden-import=fontTools.ttLib \
    --strip \
    scripts/convert_ttf_to_bin.py \
    -- --gui

# Fix: PyInstaller adds "-- --gui" to argv; instead we patch the launcher
# The script auto-launches GUI when no TTF arg is given, so this just works.

echo ""
echo "=== .app created at: $DIST_DIR/$APP_NAME.app ==="

# --- Create DMG ---
DMG_NAME="$APP_NAME"
DMG_PATH="$DIST_DIR/$DMG_NAME.dmg"
DMG_TEMP="$DIST_DIR/dmg_staging"

echo ""
echo "=== Creating $DMG_NAME.dmg ==="

# Clean up any previous staging
rm -rf "$DMG_TEMP" "$DMG_PATH"
mkdir -p "$DMG_TEMP"

# Copy the .app into staging (PyInstaller puts it at dist/APP_NAME.app)
cp -R "$DIST_DIR/$APP_NAME.app" "$DMG_TEMP/"

# Add a symlink to /Applications for drag-and-drop install
ln -s /Applications "$DMG_TEMP/Applications"

# Create the DMG
hdiutil create \
    -volname "$DMG_NAME" \
    -srcfolder "$DMG_TEMP" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

# Clean up staging
rm -rf "$DMG_TEMP"

echo ""
echo "=== Done! ==="
echo "  .app: $DIST_DIR/$APP_NAME.app"
echo "  .dmg: $DMG_PATH"
echo ""
echo "To test: open \"$DIST_DIR/$APP_NAME.app\""
