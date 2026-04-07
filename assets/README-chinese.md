# 資產

![資產](https://img.shields.io/badge/類型-資產-orange)
![SD 卡](https://img.shields.io/badge/儲存-SD_卡-blue)

M5Stack Paper S3 韌體的 SD 卡內容及建置時資產。

## 檔案

| 檔案 | 說明 |
|------|------|
| `config.ini` | 主設定檔：WiFi 憑證、時區、天氣 API 金鑰（已加入 gitignore） |
| `config.ini.example` | `config.ini` 範本 — 複製到 SD 卡並填入您的資訊 |
| `cangjie5.bin` | 編譯後的倉頡輸入法字典（二進位，執行時從 SD 載入） |
| `mottos.txt` | 睡眠畫面顯示的每日格言，每行一句 |
| `shopping_list.csv` | 購物清單資料（`分組|項目` 格式） |
| `todo_list.csv` | 待辦事項資料（`日期,任務` 格式） |
| `s3cover.jpg` | 開機啟動封面圖片（建置時轉換為 C 標頭檔） |
| `sleeping.jpg` | 睡眠畫面背景圖片（建置時轉換為 C 標頭檔） |
| `labels.jpg` | 標籤渲染參考截圖（供開發/除錯使用） |

## 子目錄

| 目錄 | 說明 |
|------|------|
| `books/` | 電子書閱讀器的電子書檔案（TXT 和 EPUB） |
| `fonts/` | 文字渲染用的 TTF/TTC/BIN 字型檔 |
| `icons/` | PNG 主畫面圖標及導覽按鈕 |

## SD 卡部署

將以下內容複製到 SD 卡根目錄：
- `config.ini`（從 `config.ini.example` 複製）
- `cangjie5.bin`
- `mottos.txt`
- `shopping_list.csv`、`todo_list.csv`（選用）
- `books/` 目錄含您的電子書
- `fonts/` 目錄含至少一個 TTF 字型
