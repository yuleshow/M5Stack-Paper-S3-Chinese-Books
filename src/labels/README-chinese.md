# 預渲染標籤點陣圖

![自動產生](https://img.shields.io/badge/類型-自動產生-yellow)
![點陣圖](https://img.shields.io/badge/格式-4位元灰階-orange)

自動產生的 C 標頭檔，包含預渲染的中文文字 4 位元灰階點陣圖。

由 `scripts/convert_labels.py` 產生。

## 用途

在 ESP32 上透過 TTF 字型渲染中文文字速度較慢，特別是在電子墨水螢幕上。此系統在建置時將所有靜態 UI 字串預先渲染為點陣圖資料，可在執行時即時繪製 — 無需在執行時載入字型或光柵化字形。

## 運作原理

1. `scripts/convert_labels.py` 讀取中文字串列表及其目標字體大小
2. 使用 TTF 字型（Pillow/PIL）以指定大小渲染每個字串
3. 渲染的點陣圖編碼為 4 位元灰階（與電子墨水螢幕的 16 色階對應）
4. 每個點陣圖寫入為 C 標頭檔，像素資料作為 `PROGMEM` 位元組陣列
5. `label_bitmaps.h` 為主標頭檔，包含所有標籤並提供 `findLabelBitmap()` 查詢函式

## 關鍵檔案

- **`label_bitmaps.h`** — 主標頭檔包含：
  - 所有個別標籤標頭的 `#include`
  - `LabelBitmap` 結構定義（文字、大小、寬度、高度、資料指標）
  - `allLabels[]` 所有已註冊點陣圖的陣列
  - `findLabelBitmap(text, size)` — 依文字內容和字體大小的 O(n) 查詢

## 執行時使用

```cpp
// 在 ui_drawing.cpp — drawSystemText() 中：
const LabelBitmap* bmp = findLabelBitmap("設定", 32);
if (bmp) {
    // 繪製預渲染點陣圖 — 即時
} else {
    // 退而使用 TTF 渲染 — 較慢
}
```

## 統計

- 約 1200 個預渲染標籤
- 約 1 MB 點陣圖資料總計
- 涵蓋：主畫面標題、選單項目、日曆用語（天干/地支/節氣/宜忌）、天氣標籤、狀態訊息、數字、標點符號等

## 重新產生

```bash
python3 scripts/convert_labels.py
```
