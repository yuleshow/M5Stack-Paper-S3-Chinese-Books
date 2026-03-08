# Source Code

![C++](https://img.shields.io/badge/Language-C++-blue)
![Arduino](https://img.shields.io/badge/Framework-Arduino-green)
![ESP32-S3](https://img.shields.io/badge/Target-ESP32--S3-blue)

C++ firmware source for the M5Stack Paper S3 Chinese Book Reader & Almanac Calendar.

## Main Entry Point ![Core](https://img.shields.io/badge/Module-Core-red)

| File | Description |
|------|-------------|
| `main.cpp` | Entry point: `setup()`, `loop()`, deep sleep management, touch event routing, mode switching |
| `globals.h` / `globals.cpp` | Shared types, enums (`Mode`), constants, extern declarations, function prototypes |

## Feature Modules ![Features](https://img.shields.io/badge/Module-Features-blue)

| File | Description |
|------|-------------|
| `dashboard.cpp` | Boot welcome screen and 2×4 icon dashboard with badge counts |
| `book_reader.cpp` | TXT book reader: vertical CJK layout, pagination, position/bookmark persistence |
| `epub_reader.cpp` | EPUB support: ZIP parsing, HTML-to-text extraction, deflate decompression |
| `calendar.cpp` | Full Chinese almanac (農民曆): lunar conversion, 八字, 節氣, 宜忌, festivals, and more |
| `weather.cpp` | OpenWeatherMap API: current conditions, 3-day forecast, AQI |
| `cangjie_input.cpp` | On-screen Cangjie (倉頡) Chinese input method with candidate selection |
| `cangjie.cpp` / `cangjie.h` | Cangjie dictionary binary lookup engine |
| `shopping_list.cpp` | Shopping list with grouped items and checkbox persistence |
| `todo_list.cpp` | Todo list with dates and checkbox persistence |
| `motto.cpp` | Daily mottos from SD card or built-in defaults |
| `wallpaper.cpp` | JPG wallpaper browser from SD card |
| `setup_ui.cpp` | Settings screens (WiFi, timezone, web server, USB MSC, icons, calendar method) and analog clock |

## System / Infrastructure ![System](https://img.shields.io/badge/Module-System-green)

| File | Description |
|------|-------------|
| `ui_drawing.cpp` | Common UI: status bar, navigation bar, `drawSystemText()` with bitmap-first rendering |
| `font_manager.cpp` | Font scanning, TTF/TTC name extraction, OpenFontRender SD I/O callbacks, BIN font loading |
| `wifi_config.cpp` | WiFi configuration from `config.ini`, NTP time sync, timezone management |
| `web_server_handler.cpp` | HTTP file manager: browse, upload, download, delete files on SD card |
| `usb_msc_handler.cpp` | USB Mass Storage: exposes SD card as USB drive, NVS preferences helpers |
| `cleanup.cpp` | macOS dot-file cleanup on SD card (removes `._*` and `.DS_Store` files) |

## Header-Only / Utility ![Utility](https://img.shields.io/badge/Module-Utility-orange)

| File | Description |
|------|-------------|
| `utf8_utils.h` | UTF-8 string utilities (byte length detection, character iteration) |
| `embedded_icons.h` | Includes all embedded icon headers from `icons/` |
| `s3cover_jpg.h` | Auto-generated: boot splash image as PROGMEM byte array |
| `sleeping_jpg.h` | Auto-generated: sleep screen image as PROGMEM byte array |

## Subdirectories ![Generated](https://img.shields.io/badge/Type-Auto--Generated-yellow)

| Directory | Description |
|-----------|-------------|
| `icons/` | Auto-generated C headers for embedded PNG icons |
| `labels/` | Auto-generated C headers for pre-rendered Chinese UI text bitmaps |
