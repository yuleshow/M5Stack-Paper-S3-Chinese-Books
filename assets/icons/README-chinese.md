# 圖標

![資產](https://img.shields.io/badge/類型-資產-orange)
![PNG](https://img.shields.io/badge/格式-PNG-blue)

主畫面及導覽按鈕的 PNG 圖標。

## 主畫面圖標 ![UI](https://img.shields.io/badge/分類-主畫面-blue)

主畫面顯示 2×4 網格圖標。每個圖標為 120×120 PNG：

| 檔案 | 功能 |
|------|------|
| `icon1.png` | 電子書（書籍閱讀器） |
| `icon2.png` | 日曆（農民曆） |
| `icon3.png` | 待辦事項 |
| `icon4.png` | 採辦（購物清單） |
| `icon5.png` | 天氣 |
| `icon6.png` | 壁紙（桌布） |
| `icon7.png` | 設定 |
| `icon8.png` | 睡眠（格言） |

## 導覽圖標 ![UI](https://img.shields.io/badge/分類-導覽-green)

| 檔案 | 功能 |
|------|------|
| `back.png` | 返回/上一頁按鈕 |
| `next.png` | 下一頁按鈕 |
| `return.png` | 返回主畫面按鈕 |

## 子目錄

| 目錄 | 說明 |
|------|------|
| `originals/` | 原始高解析度來源圖標 |
| `icons.bak/` | 舊版圖標集備份 |

## 自訂

圖標可從 SD 卡載入或使用嵌入式備用（可在設定 → 圖標來源中配置）。自訂方式：

1. 將您的 PNG 圖標放入 SD 卡的 `/icons/`
2. 在設定中將圖標來源設為「SD 卡優先」

使用 `resize_icons.py` 批次調整圖標至所需的 120×120 尺寸。
