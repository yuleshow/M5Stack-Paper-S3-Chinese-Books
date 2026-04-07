#!/usr/bin/env python3
"""Analyze firmware ELF symbols and rank flash usage by category."""
import sys

categories = {}
for line in sys.stdin:
    parts = line.strip().split()
    if len(parts) < 4:
        continue
    size = int(parts[1], 16)
    name = parts[3]

    if 'efont_tw' in name or 'efont_cn' in name:
        cat = 'Built-in CJK bitmap fonts (efontTW/CN)'
    elif 'japan_mincho' in name or 'japan_gothic' in name:
        cat = 'Built-in Japanese fonts'
    elif 'Label' in name or 'label_' in name:
        cat = 'Pre-rendered label bitmaps'
    elif 's3cover_jpg' in name:
        cat = 'Boot cover JPEG'
    elif 'sleeping_jpg' in name:
        cat = 'Sleep screen JPEG'
    elif 'icon' in name and 'png' in name:
        cat = 'Built-in PNG icons'
    elif any(x in name for x in ['mbedtls', 'x509', 'cipher']):
        cat = 'TLS/SSL (mbedtls)'
    elif any(x in name.lower() for x in ['wifi', 'net80211', 'pp_', 'lmac', 'esp_phy']):
        cat = 'WiFi stack'
    elif any(x in name.lower() for x in ['ble', 'nimble', 'BLE']):
        cat = 'BLE/Bluetooth'
    elif any(x in name.lower() for x in ['lwip', 'tcp_', 'udp_', 'dns_', 'dhcp', 'pbuf']):
        cat = 'TCP/IP (lwIP)'
    elif any(x in name.lower() for x in ['http', 'webserver', 'asyncweb']):
        cat = 'HTTP/Web server'
    elif any(x in name.lower() for x in ['tusb', 'tud_', 'usbd_']):
        cat = 'USB/TinyUSB stack'
    elif 'M5GFX' in name or 'lgfx' in name or 'm5gfx' in name:
        cat = 'M5GFX display library'
    elif any(x in name for x in ['OpenFontRender', 'TT_Run', 'TT_Load', 'FT_', 'FreeType', 'tt_face', 'tt_cmap', 'ft_', 'af_', 'cf2_', 'cff_', 'sfnt', 'smooth', 'truetype']):
        cat = 'FreeType/OpenFontRender'
    elif 'bmi270' in name:
        cat = 'BMI270 IMU config'
    else:
        cat = 'Other (ESP-IDF, Arduino, misc)'

    categories[cat] = categories.get(cat, 0) + size

total_firmware = 8147041
accounted = sum(categories.values())

print(f"Total firmware: {total_firmware:,} bytes ({total_firmware/1024/1024:.1f} MB)")
print(f"Accounted by symbols: {accounted:,} bytes ({accounted/1024/1024:.1f} MB)")
print()
print(f"{'Rank':<4} {'Category':<42} {'Size (KB)':>10} {'% of 8MB':>8}")
print("-" * 68)
for i, (cat, size) in enumerate(sorted(categories.items(), key=lambda x: -x[1]), 1):
    pct = size / total_firmware * 100
    print(f"{i:<4} {cat:<42} {size/1024:>9.1f}  {pct:>6.1f}%")
