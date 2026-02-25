# 梅花小民
# M5Stack Paper S3 — Chinese Book Reader & Almanac Calendar
![alt text](assets/s3cover.jpg)
A feature-rich Chinese e-ink application for the **M5Stack Paper S3** (ESP32-S3, 540×960 4-bit grayscale e-ink display). Combines a traditional Chinese book reader with a full-featured Chinese almanac calendar (農民曆/老黃曆), weather dashboard, Cangjie input method, and more.

## Features

### 📖 Book Reader
- **TXT and EPUB** support with vertical CJK text layout (right-to-left columns)
- EPUB parsing with built-in ZIP/deflate decompression and HTML-to-text extraction
- Configurable font size (20–52px)
- Reading position auto-saved to SD card (`.pos` sidecar files)
- Bookmarks support (up to 5 per book, saved as `.bm` files)
- Multiple font support: TTF, TTC, and pre-rendered BIN fonts

### 📅 Chinese Almanac Calendar (農民曆)
A complete traditional Chinese almanac with:
- **Solar-to-Lunar conversion** — Lookup table covering 1900–2100
- **天干地支 (Heavenly Stems & Earthly Branches)** — Year, month, and day pillars (八字)
- **24 Solar Terms (二十四節氣)** — Two calculation methods:
  - **Meeus astronomical algorithm** — Accurate to ~1 minute
  - **壽星天文曆 (sxwnl)** — 許劍偉's open-source algorithm, selectable in settings
- **生肖 (Zodiac animals)** — Based on 干支 year
- **納音五行 (NaYin Five Elements)** — 60 Jiazi cycle lookup
- **宜/忌 (Auspicious / Inauspicious activities)** — Daily dos and don'ts
- **喜神/福神/財神方位** — Lucky god directions based on day stem
- **胎神 (Fetal god position)** — Traditional pregnancy taboos
- **彭祖百忌** — Daily taboos from 天干/地支
- **時辰吉凶** — 12 two-hour period GanZhi with fortune indicators
- **沖煞 (Clash animals)** — Daily zodiac clash
- **六曜 (Rokuyo)** — Daily fortune cycle
- **節日 (Festivals)** — 道教, 民俗, and 佛教 festivals
- **朔/望 markers** — New moon and full moon indicators
- Date picker with month calendar grid and year/month selector

### 🌤️ Weather Dashboard
- **OpenWeatherMap API** integration
- Current conditions: temperature, humidity, wind, pressure, visibility
- 3-day forecast with min/max temperatures
- Air Quality Index (AQI) with PM2.5, PM10, O₃, NO₂, CO readings
- Chinese weather descriptions and quality labels (優/良/中/差/很差)
- Sunrise/sunset times
- Weather icons drawn programmatically
- Auto-refresh every 15 minutes

### ⌨️ Cangjie Input Method (倉頡輸入法)
- On-screen touch keyboard for Cangjie radical codes
- Candidate character list with pagination
- Used for adding items to Todo and Shopping lists
- Binary dictionary lookup from `cangjie5.dict.yaml`

### 📝 Todo & Shopping Lists
- CSV-based storage on SD card
- Checkbox persistence
- Shopping list with group headers
- Todo list with date fields
- Cangjie input for adding new items

### 🖼️ Wallpaper Browser
- Browse and display JPG wallpapers from SD card
- Full-screen e-ink display

### 💤 Sleep Mode with Daily Mottos
- Deep sleep with touch wake-up (GPIO 21)
- Displays a random motto from `/mottos.txt` (10 built-in defaults if file missing)
- Preserves state across deep sleep via RTC-persistent variables

### ⚙️ Settings
- **WiFi** — Network scanning, on-screen keyboard for password
- **Timezone** — 20+ presets (Asia, Americas, Europe, etc.)
- **Web Server** — HTTP file manager for browsing/uploading/downloading/deleting files on SD card
- **USB Mass Storage** — Expose SD card as USB drive for direct file access
- **Icon Source** — SD card (customizable) or embedded (faster boot)
- **Calendar Method** — Meeus vs 壽星天文曆 algorithm

## Hardware

- **M5Stack Paper S3**
  - ESP32-S3 @ 240MHz
  - 540×960 e-ink display, 4-bit grayscale (16 levels)
  - Capacitive touch
  - OPI PSRAM
  - SD card slot
  - USB-C

## Build & Flash

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)

### Build

```bash
pio run
```

### Upload

Connect the M5Stack Paper S3 via USB-C, then:

```bash
pio run -t upload
```

### Dependencies (auto-installed by PlatformIO)

- `m5stack/M5Unified@^0.2.13`
- [OpenFontRender](https://github.com/takkaO/OpenFontRender)
- `espressif32@6.5.0` (Arduino framework)

## SD Card Setup

Place the following files on the SD card:

| Path | Required | Description |
|------|----------|-------------|
| `config.ini` | Recommended | WiFi, timezone, and weather API configuration |
| `fonts/GenYoMinTW-Regular.ttf` | Recommended | System UI font (any TTF/TTC works as fallback) |
| `books/*.txt` | Optional | Plain text books (UTF-8 encoded) |
| `books/*.epub` | Optional | EPUB e-books |
| `mottos.txt` | Optional | One motto per line for sleep screen |
| `shopping_list.csv` | Optional | Shopping list (`group\|item` format) |
| `todo_list.csv` | Optional | Todo list (`date,task` format) |
| `wallpapers/*.jpg` | Optional | JPG wallpapers |
| `icons/icon1.png`–`icon8.png` | Optional | Custom dashboard icons (PNG) |
| `weather.cfg` | Optional | Alternative weather-only config |

### Configuration

Copy `assets/config.ini.example` to SD card as `config.ini` and fill in your details:

```ini
[wifi]
ssid=YOUR_WIFI_SSID
password=YOUR_WIFI_PASSWORD

[time]
timezone=PST8PDT
gmtoffset=-28800

[weather]
apikey=YOUR_OPENWEATHERMAP_API_KEY
city=Pasadena,CA,US
units=metric
```

Get a free API key at [OpenWeatherMap](https://openweathermap.org/api).

## Architecture

### Pre-Rendered Bitmap Font System

All static Chinese UI text is **pre-rendered at build time** into 4-bit grayscale bitmap C headers using `convert_labels.py`. This eliminates runtime font rendering for UI elements, providing instant text display on the e-ink screen.

- `convert_labels.py` renders ~1200 label strings at various sizes → individual `.h` files in `src/labels/`
- `findLabelBitmap()` provides O(1) lookup by text + size
- `drawSystemText()` tries bitmap first, falls back to TTF rendering

### Key Source Files

| File | Description |
|------|-------------|
| `main.cpp` | Entry point, touch routing, mode switching, deep sleep |
| `globals.h` / `globals.cpp` | Shared types, enums, constants, extern declarations |
| `calendar.cpp` | Full Chinese almanac: lunar conversion, 八字, 節氣, 宜忌 |
| `book_reader.cpp` | TXT reader with vertical CJK layout |
| `epub_reader.cpp` | EPUB ZIP parsing, HTML extraction, deflate decompression |
| `weather.cpp` | OpenWeatherMap integration with bitmap-rendered UI |
| `cangjie_input.cpp` | On-screen Cangjie Chinese input method |
| `font_manager.cpp` | Font scanning, TTF name extraction, OpenFontRender I/O |
| `setup_ui.cpp` | Settings screens and analog clock |
| `ui_drawing.cpp` | Status bar, nav bar, system text rendering |
| `wifi_config.cpp` | WiFi config, NTP sync, timezone management |
| `web_server_handler.cpp` | HTTP file manager |
| `usb_msc_handler.cpp` | USB Mass Storage mode |
| `dashboard.cpp` | Welcome screen and 2×4 icon dashboard |
| `convert_labels.py` | Build tool: renders Chinese strings to bitmap headers |

### Build Stats

- Flash: ~89% of 8MB
- RAM: ~24% of 320KB
- ~1200 pre-rendered bitmap labels (~1 MB)

## Regenerating Label Bitmaps

If you modify UI strings or add new labels, regenerate the bitmap headers:

```bash
python3 convert_labels.py
```

Requires Python 3 with `Pillow` and a TTF font (default: `assets/fonts/GenYoMinTW-Regular.ttf`).

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).

## Acknowledgments

- [壽星天文曆 (sxwnl)](https://github.com/sxwnl) by 許劍偉 — Chinese astronomical calendar algorithms
- [OpenFontRender](https://github.com/takkaO/OpenFontRender) by takkaO — TTF font rendering for embedded systems
- [M5Unified](https://github.com/m5stack/M5Unified) by M5Stack — Hardware abstraction library
- [OpenWeatherMap](https://openweathermap.org/) — Weather data API
- Jean Meeus, *Astronomical Algorithms* — Solar longitude calculations
