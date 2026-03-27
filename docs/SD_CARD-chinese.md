# SD 卡檔案結構

本文件說明 M5Stack Paper S3 韌體在 SD 卡上需要的所有檔案和目錄。

> SD 卡必須格式化為 **FAT32**。
>
> 所有 SD 卡檔案保存在本儲存庫的 `sd_card/` 資料夾中。將 `sd_card/` 的內容複製到 SD 卡的根目錄即可。

## 目錄結構

```
SD 卡根目錄/
├── config.ini                ← WiFi、時區、天氣、BLE 解鎖設定
├── weather.cfg               ← 舊版天氣設定（選用備用）
├── mottos.txt                ← 睡眠畫面格言，每行一句
├── cangjie5.bin              ← 倉頡輸入法字典
├── shopping_list.csv         ← 購物清單
├── todo_list.csv             ← 待辦事項
├── calendar_events.csv       ← 自訂日曆事件
├── books/
│   ├── *.txt                 ← UTF-8 純文字書籍
│   └── *.epub                ← EPUB 電子書
├── fonts/
│   ├── *.ttf                 ← TrueType 字型
│   ├── *.ttc                 ← TrueType Collection 字型
│   └── *.bin                 ← 預渲染二進位字型
├── wallpapers/
│   ├── *.jpg / *.jpeg        ← JPEG 桌布圖片
│   ├── *.png                 ← PNG 桌布圖片
│   └── *.bmp                 ← BMP 桌布圖片
├── icons/
│   └── icon1.png – icon8.png ← 自訂主畫面圖標
├── dict/
│   └── en-zh.txt              ← 英漢字典（長按查詞用）
└── fortune_slips/
    ├── kuanyin.bin            ← 觀音靈籖
    └── sensoji.bin            ← 淺草寺靈籖
```

---

## 設定檔

### `config.ini`（建議使用）

主設定檔。控制 WiFi、時區、天氣及 BLE 解鎖。

```ini
[wifi]
ssid=你的WiFi名稱
password=你的WiFi密碼

[time]
timezone=PST8PDT
gmtoffset=-28800

[weather]
apikey=你的OPENWEATHERMAP_API金鑰
city=Pasadena,CA,US
units=metric

[unlock]
enabled=false
device_name=M5Paper-BLE
```

範本請參見 `assets/config.ini.example`。

### `weather.cfg`（選用）

舊版天氣專用設定檔。當 `config.ini` 未包含天氣設定時作為備用。

```
apikey=你的OPENWEATHERMAP_API金鑰
city=Los Angeles,US
units=metric
```

### `mottos.txt`（選用）

每行一句格言（UTF-8 編碼）。顯示於桌布/睡眠畫面。若檔案不存在，使用內建預設格言。

### `cangjie5.bin`（選用）

倉頡輸入法二進位字典。由 `data/cangjie5.dict.yaml` 透過 `scripts/convert_cangjie.py` 產生。

---

## 資料檔案

### `shopping_list.csv`（選用，可讀寫）

購物清單，每行格式為 `分組|項目`：

```csv
水果|蘋果
水果|香蕉
飲料|牛奶
```

韌體會自動建立 `shopping_checked.txt` 以保存勾選狀態。

### `todo_list.csv`（選用，可讀寫）

待辦事項，每行格式為 `日期,任務`：

```csv
2026-03-07,買菜
2026-03-08,寫報告
```

韌體會自動建立 `todo_checked.txt` 以保存勾選狀態。

### `calendar_events.csv`（選用）

自訂日曆事件，顯示於日曆畫面。

---

## 書籍 — `/books/`

| 檔案類型 | 說明 |
|----------|------|
| `*.txt`  | UTF-8 純文字書籍 |
| `*.epub` | EPUB 電子書 |

- 最多顯示 **20** 本書。
- 韌體會在每本書旁自動建立附屬檔案：
  - `<書名>.txt.pos` / `<書名>.epub.pos` — 閱讀進度
  - `<書名>.txt.bm` / `<書名>.epub.bm` — 書籤（最多 5 個）

---

## 字型 — `/fonts/`

| 檔案類型 | 說明 |
|----------|------|
| `*.ttf`  | TrueType 字型 |
| `*.ttc`  | TrueType Collection（多字型合集） |
| `*.bin`  | 預渲染二進位字型（由 `scripts/convert_ttf_to_bin.py` 產生） |

- 最多掃描 **100** 個字型檔案。

### 系統字型

韌體依以下優先順序尋找系統字型：

1. **`GenYoMinTW-Regular.ttf`** — 首選系統字型（源樣明體）。若找到則自動載入。
2. 任何其他 `.ttf` 或 `.ttc` 檔案 — 源樣明體不存在時作為備用。
3. `.bin` 字型 — 最後選擇，優先使用 `MingLiU` 相關檔案。

系統字型用於所有 UI 文字：選單、標籤、閱讀器、主畫面、日曆等。

### 格言字型（楷體）

格言/醒世格言功能尋找：

- **`TW-Kai-98_1.ttf`** — 傳統楷書字型，用於桌布和睡眠畫面的直排格言文字渲染。

若找不到，韌體會使用內建字型。為獲得最佳格言視覺效果，請將此檔案放入 `/fonts/`。

### Silver 字型

Silver 是一套像素風格的替代系統字型。使用方式：

- 將 **`Silver.ttf`** 放入 `/fonts/`。
- 韌體會自動偵測檔名含有「Silver」的字型並切換為 Silver 模式。
- 預渲染 `.bin` 檔案的命名格式為 **`Silver_<尺寸>pt.bin`**（如 `Silver_36pt.bin`）。

Silver 字形會以比標稱尺寸**大約 38%** 的比例渲染，以與源樣明體在相同設定下保持一致的視覺大小。韌體會自動處理此縮放，無須手動調整。

可透過裝置上的字型選擇選單在源樣明體與 Silver 之間切換。

---

## 桌布 — `/wallpapers/`

| 檔案類型 | 說明 |
|----------|------|
| `*.jpg` / `*.jpeg` | JPEG 圖片 |
| `*.png` | PNG 圖片 |
| `*.bmp` | BMP 圖片 |

- 最多 **30** 個桌布檔案。
- 建議解析度：**540 × 960** 像素（與電子墨水螢幕相同）。
- 每張最大檔案大小：**2 MB**。

---

## 圖標 — `/icons/`

自訂主畫面圖標以取代內建預設。

| 檔案 | 圖標位置 |
|------|----------|
| `icon1.png` | 左上（閱讀） |
| `icon2.png` | 右上（字型） |
| `icon3.png` | 第二列左（待辦） |
| `icon4.png` | 第二列右（購物） |
| `icon5.png` | 第三列左（天氣） |
| `icon6.png` | 第三列右（桌布） |
| `icon7.png` | 左下（日曆） |
| `icon8.png` | 右下（求籤） |

圖標應為 **PNG** 格式。若未找到自訂圖標，則使用內建的嵌入式圖標。

---

## 字典 — `/dict/`

英漢字典，用於英文閱讀模式中的長按查詞功能。

| 檔案 | 說明 |
|------|------|
| `en-zh.txt` | Tab 分隔的英漢字典，按字母排序 |

閱讀英文書籍時，長按任意單字即可彈出中文翻譯。

### 產生字典

字典資料來自 [ECDICT](https://github.com/skywind3000/ECDICT)（MIT 授權，約 77 萬條目）：

1. 從 [ECDICT releases 頁面](https://github.com/skywind3000/ECDICT/releases) 下載 `ecdict-sqlite-28.zip`
2. 解壓獲得 `stardict.db`
3. 執行轉換腳本：

```bash
python3 scripts/convert_ecdict.py path/to/stardict.db
```

產生 `sd_card/dict/en-zh.txt`（約 2.5 MB，依詞頻排名前 50,000 個單字）。將 `dict/` 資料夾複製到 SD 卡即可。

---

## 求籤 — `/fortune_slips/`

二進位封裝檔案，包含預調大小（540 × 960）的 JPEG 籤詩圖片。

籤詩圖片來源：[www.chance.org.tw](https://www.chance.org.tw)。
