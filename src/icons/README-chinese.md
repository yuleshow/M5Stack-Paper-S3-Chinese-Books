# 嵌入式圖標

![自動產生](https://img.shields.io/badge/類型-自動產生-yellow)
![C++](https://img.shields.io/badge/語言-C++-blue)

自動產生的 C 標頭檔，包含 PNG 圖標資料作為 PROGMEM 位元組陣列。

由 `scripts/convert_icons.py` 從 `assets/icons/` 中的 PNG 檔案產生。

## 檔案

| 標頭檔 | 來源 | 說明 |
|--------|------|------|
| `icon1_png.h` – `icon8_png.h` | `assets/icons/icon1.png` – `icon8.png` | 主畫面選單圖標（共 8 個） |
| `back_png.h` | `assets/icons/back.png` | 返回/上一頁導覽 |
| `next_png.h` | `assets/icons/next.png` | 下一頁導覽 |
| `return_png.h` | `assets/icons/return.png` | 返回主畫面導覽 |

## 格式

每個標頭檔定義兩個常數：
```cpp
const unsigned char icon1_png[] PROGMEM = { ... };
const unsigned int icon1_png_len = ...;
```

這些透過 `embedded_icons.h` 引入，在 SD 卡圖標不可用時作為備用圖標。

## 重新產生

```bash
python3 scripts/convert_icons.py
```
