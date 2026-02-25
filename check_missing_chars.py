#!/usr/bin/env python3
import struct

# Characters from the user's text
test_text = "《西遊記》吳承恩（明）第一回靈根育孕源流出心性修持大道生詩曰混沌未分天地亂茫茫渺渺無人見自從盤古破鴻蒙開闢從茲清濁辨覆載羣生仰至仁明萬物皆成善欲知造化會元功須看西遊釋厄傳蓋聞天地之數有十二萬九千六百歲爲一元將一元分爲十二會乃子醜寅卯辰巳午未申酉"

# Read the font index
with open('output/NotoSerifTC.bin', 'rb') as f:
    # Read header
    f.seek(137)
    
    # Read all index entries
    unicodes_in_font = set()
    while True:
        data = f.read(20)
        if len(data) < 20:
            break
        unicode_val = int.from_bytes(data[0:4], 'little')
        unicodes_in_font.add(unicode_val)

# Check which characters are missing
missing = []
found = []
for char in test_text:
    unicode_val = ord(char)
    if unicode_val in unicodes_in_font:
        found.append(char)
    else:
        missing.append(f"{char} (U+{unicode_val:04X})")

print(f"Total characters in test: {len(set(test_text))}")
print(f"Characters found in font: {len(set(found))}")
print(f"Characters MISSING: {len(set(missing))}")
print(f"\nMISSING characters:")
for m in missing:
    print(f"  {m}")
