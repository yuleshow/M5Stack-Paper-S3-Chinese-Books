#!/usr/bin/env python3
"""Analyze labels.jpg to find text positions."""
from PIL import Image

img = Image.open('assets/labels.jpg').convert('L')
w, h = img.size
print(f"Image: {w}x{h}")

# Scan for non-white rows (threshold 200)
dark_rows = []
for y in range(h):
    for x in range(w):
        if img.getpixel((x, y)) < 200:
            dark_rows.append(y)
            break

if not dark_rows:
    print("No dark pixels found!")
    exit()

print(f"Content Y range: {dark_rows[0]} to {dark_rows[-1]}")

# Find clusters with gaps > 10px
clusters = []
start = dark_rows[0]
prev = start
for y in dark_rows[1:]:
    if y - prev > 10:
        clusters.append((start, prev))
        start = y
    prev = y
clusters.append((start, prev))

print(f"Found {len(clusters)} text rows:")
for ci, (ys, ye) in enumerate(clusters):
    # Find X range for this cluster
    min_x, max_x = w, 0
    for y in range(ys, ye + 1):
        for x in range(w):
            if img.getpixel((x, y)) < 200:
                min_x = min(min_x, x)
                break
        for x in range(w - 1, -1, -1):
            if img.getpixel((x, y)) < 200:
                max_x = max(max_x, x)
                break
    print(f"  Cluster {ci}: y={ys}..{ye} (h={ye-ys+1}), x={min_x}..{max_x} (w={max_x-min_x+1})")

# Also try to split each cluster into left/right halves (since dashboard is 2 columns)
print("\nChecking for 2-column layout:")
mid_x = w // 2  # 270
for ci, (ys, ye) in enumerate(clusters):
    left_pixels = 0
    right_pixels = 0
    for y in range(ys, ye + 1):
        for x in range(mid_x):
            if img.getpixel((x, y)) < 200:
                left_pixels += 1
        for x in range(mid_x, w):
            if img.getpixel((x, y)) < 200:
                right_pixels += 1
    print(f"  Row {ci}: left={left_pixels}, right={right_pixels}")
