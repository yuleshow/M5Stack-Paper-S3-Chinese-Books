#!/usr/bin/env python3
"""
Resize icon images for M5Paper S3 dashboard.

Accepts any input size/format (PNG, JPG, BMP, WEBP, etc.) and outputs
correctly sized grayscale PNGs optimized for e-ink display.

Non-square images are scaled to fit within the target size while
preserving aspect ratio, centered on a white background.

Display: 540×960, 2×4 grid, margin=20, gap=12
Icon cell: 244×221 px
Default target: min(244-40, 221-80) = 141×141 px

Usage:
    python resize_icons.py                  # Resize to default 141×141
    python resize_icons.py --size 200       # Resize to custom 200×200
    python resize_icons.py --size 200x150   # Resize to custom 200×150
    python resize_icons.py --output ../out  # Output to different directory
    python resize_icons.py --preview        # Show size info without writing
    python resize_icons.py --format png     # Output format (png/bmp/jpg)
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow is required. Install with: pip install Pillow")
    sys.exit(1)

# M5Paper S3 dashboard layout constants
DISPLAY_W = 540
DISPLAY_H = 960
MARGIN = 20
GAP = 12
ROWS = 4
COLS = 2

ICON_CELL_W = (DISPLAY_W - MARGIN * 2 - GAP * (COLS - 1)) // COLS  # 244
ICON_CELL_H = (DISPLAY_H - MARGIN * 2 - GAP * (ROWS - 1)) // ROWS  # 221
LABEL_HEIGHT = 45   # Space reserved for label text below icon
PADDING = 10        # Padding around icon within cell
DEFAULT_TARGET = min(ICON_CELL_W - PADDING * 2,
                     ICON_CELL_H - LABEL_HEIGHT - PADDING)           # 176


def parse_size(s):
    """Parse size string: '141' -> (141,141), '200x150' -> (200,150)."""
    if "x" in s.lower():
        parts = s.lower().split("x")
        return int(parts[0]), int(parts[1])
    n = int(s)
    return n, n


def resize_icon(src: Path, dst: Path, target_w: int, target_h: int, fmt: str):
    """Resize a single icon to fit within target_w×target_h, preserving aspect ratio."""
    img = Image.open(src)
    original_size = img.size

    # Convert RGBA/P/LA to RGB with white background (e-ink has no transparency)
    if img.mode in ("RGBA", "LA", "P"):
        bg = Image.new("RGB", img.size, (255, 255, 255))
        if img.mode == "P":
            img = img.convert("RGBA")
        bg.paste(img, mask=img.split()[-1])
        img = bg
    elif img.mode != "RGB":
        img = img.convert("RGB")

    # Scale to fit within target while preserving aspect ratio
    src_w, src_h = img.size
    scale = min(target_w / src_w, target_h / src_h)
    new_w = round(src_w * scale)
    new_h = round(src_h * scale)
    img = img.resize((new_w, new_h), Image.LANCZOS)

    # Center on white canvas of exact target size
    canvas = Image.new("RGB", (target_w, target_h), (255, 255, 255))
    offset_x = (target_w - new_w) // 2
    offset_y = (target_h - new_h) // 2
    canvas.paste(img, (offset_x, offset_y))

    # Convert to 8-bit grayscale (optimal for e-ink)
    canvas = canvas.convert("L")

    # Save in requested format
    out_path = dst.with_suffix(f".{fmt}")
    if fmt == "bmp":
        canvas.save(out_path, "BMP")
    elif fmt == "jpg":
        canvas.save(out_path, "JPEG", quality=90)
    else:
        canvas.save(out_path, "PNG", optimize=True)

    src_kb = src.stat().st_size / 1024
    dst_kb = out_path.stat().st_size / 1024
    print(f"  {src.name}: {original_size[0]}×{original_size[1]} ({src_kb:.1f}KB)"
          f" → {target_w}×{target_h} ({dst_kb:.1f}KB) [{out_path.name}]")


def main():
    parser = argparse.ArgumentParser(description="Resize M5Paper S3 dashboard icons")
    parser.add_argument("--size", type=str, default=str(DEFAULT_TARGET),
                        help=f"Target size: N for square, WxH for rectangular (default: {DEFAULT_TARGET})")
    parser.add_argument("--output", type=str, default=None,
                        help="Output directory (default: overwrite in place)")
    parser.add_argument("--format", type=str, default="png", choices=["png", "bmp", "jpg"],
                        help="Output image format (default: png)")
    parser.add_argument("--preview", action="store_true",
                        help="Show info without writing files")
    parser.add_argument("files", nargs="*",
                        help="Specific icon files (default: all icon*.png/jpg/bmp)")
    args = parser.parse_args()

    target_w, target_h = parse_size(args.size)
    script_dir = Path(__file__).parent
    out_dir = Path(args.output) if args.output else script_dir

    # Find icon files (any common image format)
    if args.files:
        icons = [Path(f) for f in args.files]
    else:
        extensions = ("*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp")
        icons = []
        for ext in extensions:
            icons.extend(script_dir.glob(f"icon*{ext[1:]}"))
        icons = sorted(set(icons))

    if not icons:
        print("No icon files found (looking for icon*.png/jpg/bmp/webp).")
        sys.exit(1)

    print(f"Display: {DISPLAY_W}×{DISPLAY_H}")
    print(f"Icon cell: {ICON_CELL_W}×{ICON_CELL_H}")
    print(f"Target size: {target_w}×{target_h}")
    print(f"Output: {out_dir.resolve()} (format: {args.format})")
    print(f"Found {len(icons)} icon(s):\n")

    if args.preview:
        for icon in icons:
            img = Image.open(icon)
            kb = icon.stat().st_size / 1024
            ratio = f"{img.size[0]/img.size[1]:.2f}" if img.size[1] else "?"
            print(f"  {icon.name}: {img.size[0]}×{img.size[1]} ({kb:.1f}KB) ratio={ratio}")
        print(f"\nRun without --preview to resize to {target_w}×{target_h}")
        return

    out_dir.mkdir(parents=True, exist_ok=True)

    # Back up originals before overwriting
    if out_dir.resolve() == script_dir.resolve():
        backup_dir = script_dir / "originals"
        backup_dir.mkdir(exist_ok=True)
        for icon in icons:
            backup_path = backup_dir / icon.name
            if not backup_path.exists():
                import shutil
                shutil.copy2(icon, backup_path)
                print(f"  Backed up {icon.name} → originals/")
            else:
                print(f"  Backup already exists: originals/{icon.name}")
        print()

    for icon in icons:
        dst = out_dir / icon.with_suffix(f".{args.format}").name
        resize_icon(icon, dst, target_w, target_h, args.format)

    print(f"\nDone! {len(icons)} icon(s) resized to {target_w}×{target_h}")


if __name__ == "__main__":
    main()
