# 字型

![資產](https://img.shields.io/badge/類型-資產-orange)
![字型](https://img.shields.io/badge/格式-TTF_/_TTC_/_BIN-blue)

電子墨水螢幕文字渲染用的字型檔。將 TTF、TTC 或預渲染 BIN 字型檔放置於此。

## 支援格式

| 格式 | 說明 |
|------|------|
| `.ttf` | TrueType 字型 — 透過 OpenFontRender 函式庫渲染 |
| `.ttc` | TrueType Collection — 多字型合集，使用第一個字型面 |
| `.bin` | 預渲染二進位點陣圖字型 — 由 `scripts/convert_ttf_to_bin.py` 產生的自訂格式 |

## 建議字型

韌體優先使用 **`GenYoMinTW-Regular.ttf`**（源樣明體）作為系統 UI 字型。若找不到，依序使用任何可用的 TTF、最佳 BIN 字型、內建 efont。

## 字型角色

- **系統字型** — 用於所有 UI 文字渲染（選單、標籤、狀態）。開機時自動選取。預設：**`GenYoMinTW-Regular.ttf`**（源樣明體）。
- **格言字型** — 用於睡眠畫面的直排格言文字。預設：**`TW-Kai-98_1.ttf`**（全字庫正楷體），來自[CNS11643 全字庫](https://data.gov.tw/dataset/5961)開放資料，由數位發展部提供。OFL-1.1 授權。下載：[Fonts_Kai.zip](https://www.cns11643.gov.tw/opendata/Fonts_Kai.zip)。無此字型時使用內建 `efontTW_24`。
- **閱讀字型** — 使用者可透過字型選單為每本書選擇。儲存於偏好設定中。

## 備註

- 字型檔因檔案較大（通常 5–50 MB）已加入 gitignore
- 字型管理器在開機時掃描此目錄並建立可用字型列表
- 從 TTF 名稱表擷取中文字型家族名稱以供顯示
