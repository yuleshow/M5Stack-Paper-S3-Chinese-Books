#!/usr/bin/env python3
"""Convert all PNG icons in assets/icons/ to C header files in src/icons/."""

import os
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

ICON_DIR = os.path.join("assets", "icons")
OUT_DIR = os.path.join("src", "icons")

# Icons to embed
ICONS = [
    "icon1.png", "icon2.png", "icon3.png", "icon4.png",
    "icon5.png", "icon6.png", "icon7.png", "icon8.png",
    "back.png", "next.png", "return.png", "reader_toolbar.png",
    "sleep.png",
]


def png_to_header(png_path, header_path, var_name):
    with open(png_path, "rb") as f:
        data = f.read()

    size = len(data)

    with open(header_path, "w") as out:
        out.write(f"// Auto-generated from {os.path.basename(png_path)}\n")
        out.write("#pragma once\n\n")
        out.write("#include <pgmspace.h>\n\n")
        out.write(f"const size_t {var_name}_len = {size};\n\n")
        out.write(f"const uint8_t {var_name}[] PROGMEM = {{\n")

        for i in range(0, size, 16):
            chunk = data[i : i + 16]
            hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
            comma = "," if i + 16 < size else ""
            out.write(f"    {hex_vals}{comma}\n")

        out.write("};\n")

    print(f"  {os.path.basename(png_path):15s} -> {os.path.basename(header_path):20s} ({size} bytes)")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    print(f"Converting icons from {ICON_DIR} to {OUT_DIR}")
    for icon_file in ICONS:
        png_path = os.path.join(ICON_DIR, icon_file)
        if not os.path.exists(png_path):
            print(f"  WARNING: {icon_file} not found, skipping")
            continue

        # var name: icon1.png -> icon1_png
        var_name = icon_file.replace(".", "_")
        header_name = var_name + ".h"
        header_path = os.path.join(OUT_DIR, header_name)
        png_to_header(png_path, header_path, var_name)

    print("Done!")


if __name__ == "__main__":
    main()
