# 原始碼

![C++](https://img.shields.io/badge/語言-C++-blue)
![Arduino](https://img.shields.io/badge/框架-Arduino-green)
![ESP32-S3](https://img.shields.io/badge/目標-ESP32--S3-blue)

M5Stack Paper S3 中文電子書閱讀器與農民曆的 C++ 韌體原始碼。

## 主程式進入點 ![核心](https://img.shields.io/badge/模組-核心-red)

| 檔案 | 說明 |
|------|------|
| `main.cpp` | 進入點：`setup()`、`loop()`、深度睡眠管理、觸控事件路由、模式切換 |
| `globals.h` / `globals.cpp` | 共享型別、列舉（`Mode`）、常數、外部宣告、函式原型 |

## 功能模組 ![功能](https://img.shields.io/badge/模組-功能-blue)

| 檔案 | 說明 |
|------|------|
| `dashboard.cpp` | 開機歡迎畫面與 2×4 圖標主畫面含徽章計數 |
| `book_reader.cpp` | TXT 書籍閱讀器：直排中文排版、分頁、進度/書籤持久化 |
| `epub_reader.cpp` | EPUB 支援：ZIP 解析、HTML 轉純文字擷取、Deflate 解壓縮 |
| `calendar.cpp` | 完整農民曆：農曆轉換、八字、節氣、宜忌、節日等 |
| `weather.cpp` | OpenWeatherMap API：目前天氣、三日預報、空氣品質指數 |
| `cangjie_input.cpp` | 螢幕倉頡輸入法含候選字選擇 |
| `cangjie.cpp` / `cangjie.h` | 倉頡字典二進位查詢引擎 |
| `shopping_list.cpp` | 購物清單含分組項目及勾選持久化 |
| `todo_list.cpp` | 待辦事項含日期及勾選持久化 |
| `motto.cpp` | 每日格言，自 SD 卡或內建預設 |
| `wallpaper.cpp` | SD 卡 JPG 桌布瀏覽器 |
| `setup_ui.cpp` | 設定畫面（WiFi、時區、網頁伺服器、USB MSC、圖標、曆法）及類比時鐘 |

## 系統 / 基礎架構 ![系統](https://img.shields.io/badge/模組-系統-green)

| 檔案 | 說明 |
|------|------|
| `ui_drawing.cpp` | 通用 UI：狀態列、導覽列、`drawSystemText()` 優先使用點陣圖渲染 |
| `font_manager.cpp` | 字型掃描、TTF/TTC 名稱擷取、OpenFontRender SD I/O 回呼、BIN 字型載入 |
| `wifi_config.cpp` | WiFi 設定讀取自 `config.ini`、NTP 時間同步、時區管理 |
| `web_server_handler.cpp` | HTTP 檔案管理器：瀏覽、上傳、下載、刪除 SD 卡檔案 |
| `usb_msc_handler.cpp` | USB 大容量儲存：將 SD 卡暴露為 USB 磁碟、NVS 偏好設定輔助函式 |
| `cleanup.cpp` | macOS 點檔案清理（移除 SD 卡上的 `._*` 和 `.DS_Store` 檔案） |

## 純標頭檔 / 工具 ![工具](https://img.shields.io/badge/模組-工具-orange)

| 檔案 | 說明 |
|------|------|
| `utf8_utils.h` | UTF-8 字串工具（位元組長度偵測、字元迭代） |
| `embedded_icons.h` | 包含 `icons/` 中所有嵌入式圖標標頭 |
| `s3cover_jpg.h` | 自動產生：開機啟動畫面作為 PROGMEM 位元組陣列 |
| `sleeping_jpg.h` | 自動產生：睡眠畫面圖片作為 PROGMEM 位元組陣列 |

## 子目錄 ![自動產生](https://img.shields.io/badge/類型-自動產生-yellow)

| 目錄 | 說明 |
|------|------|
| `icons/` | 自動產生的嵌入式 PNG 圖標 C 標頭檔 |
| `labels/` | 自動產生的預渲染中文 UI 文字點陣圖 C 標頭檔 |
