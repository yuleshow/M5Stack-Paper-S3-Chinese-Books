# User Guide — 梅花小民 M5Stack Paper S3

![Documentation](https://img.shields.io/badge/Type-User_Guide-red)
![ESP32-S3](https://img.shields.io/badge/Device-M5Stack_Paper_S3-blue)

A complete setup and usage guide for the Chinese Book Reader & Almanac Calendar on M5Stack Paper S3.

## Table of Contents

- [Hardware Requirements](#hardware-requirements)
- [SD Card Setup](#sd-card-setup)
- [Flashing the Firmware](#flashing-the-firmware)
- [First Boot](#first-boot)
- [Dashboard Navigation](#dashboard-navigation)
- [E-Book Reader](#e-book-reader)
- [Chinese Almanac Calendar](#chinese-almanac-calendar)
- [Weather Dashboard](#weather-dashboard)
- [Todo List](#todo-list)
- [Shopping List](#shopping-list)
- [Cangjie Input Method](#cangjie-input-method)
- [Wallpaper Browser](#wallpaper-browser)
- [Settings](#settings)
- [Sleep & Power Management](#sleep--power-management)
- [Web File Manager](#web-file-manager)
- [USB Mass Storage](#usb-mass-storage)
- [Troubleshooting](#troubleshooting)

---

## Hardware Requirements

| Component | Details |
|-----------|---------|
| **Device** | M5Stack Paper S3 |
| **Processor** | ESP32-S3 @ 240 MHz |
| **Display** | 540×960 e-ink, 4-bit grayscale (16 levels) |
| **Touch** | GT911 capacitive touch |
| **Memory** | OPI PSRAM |
| **Storage** | MicroSD card slot |
| **Connectivity** | USB-C, WiFi 2.4 GHz |

**You will need:**
- A microSD card (FAT32 formatted, any size up to 32 GB recommended)
- A USB-C cable for flashing and charging
- A computer with [PlatformIO](https://platformio.org/) installed (for building firmware)

---

## SD Card Setup

Format your microSD card as **FAT32**, then create this folder structure:

```
SD Card Root/
├── config.ini          ← WiFi, timezone, weather API config
├── mottos.txt          ← One motto per line (optional)
├── shopping_list.csv   ← Shopping list data (optional)
├── todo_list.csv       ← Todo list data (optional)
├── cangjie5.bin        ← Cangjie input dictionary (optional)
├── books/
│   ├── 三國演義.txt     ← UTF-8 encoded text files
│   ├── MyBook.epub     ← EPUB e-books
│   └── ...
├── fonts/
│   ├── GenYoMinTW-Regular.ttf  ← Recommended system font
│   └── ...              ← Additional TTF/TTC/BIN fonts
├── wallpapers/
│   ├── scenery.jpg      ← JPG/PNG/BMP images
│   └── ...
└── icons/
    ├── icon1.png        ← Custom dashboard icons (optional)
    ├── icon2.png
    └── ... (up to icon8.png)
```

### Configuration File

Create `config.ini` on the SD card root:

```ini
[wifi]
ssid=YOUR_WIFI_SSID
password=YOUR_WIFI_PASSWORD

[time]
timezone=CST-8
gmtoffset=28800

[weather]
apikey=YOUR_OPENWEATHERMAP_API_KEY
city=Taipei,TW
units=metric
```

**Timezone presets** (use `timezone` and `gmtoffset` values):

| Location | timezone | gmtoffset |
|----------|----------|-----------|
| UTC+0 | `UTC0` | `0` |
| Beijing / Taipei +8 | `CST-8` | `28800` |
| Tokyo +9 | `JST-9` | `32400` |
| US West Coast -8/-7 | `PST8PDT` | `-28800` |
| US East Coast -5/-4 | `EST5EDT` | `-18000` |
| UK +0/+1 | `GMT0BST` | `0` |
| Sydney +10/+11 | `AEST-10AEDT` | `36000` |

**Weather API Key:** Get a free key at [openweathermap.org/api](https://openweathermap.org/api). The free tier supports current weather + forecast.

### Books

- **TXT files** must be UTF-8 encoded. Place in `/books/`.
- **EPUB files** are supported with built-in decompression. Title is auto-extracted from metadata.
- Maximum 20 books.

### Fonts

Place TTF, TTC, or pre-rendered BIN fonts in `/fonts/`. The device scans this folder on boot.

- **Recommended:** `GenYoMinTW-Regular.ttf` (a Traditional Chinese serif font)
- You can switch fonts from the reading screen's 字型 button.
- Supports up to 100 font files.

### Wallpapers

Place JPG, PNG, or BMP images in `/wallpapers/`.

- **Recommended resolution:** 540×960 pixels (matches the display)
- **JPG tips:** Export as baseline (non-progressive), quality 85–95
- Maximum file size: 2 MB per image, up to 30 files

---

## Flashing the Firmware

### Option A: Pre-built Binary

Download binaries from the [Releases](https://github.com/yuleshow/M5Stack-Paper-S3-Chinese-Books/releases) page. Two versions are provided:

#### 16 MB merged binary (recommended)

Contains the bootloader, partition table, and application — use this for fresh/blank devices or a full reflash:

```bash
pip install esptool  # if not already installed
esptool.py --chip esp32s3 --port /dev/cu.usbmodem* write_flash 0x0 M5Paper-S3-Chinese-Books-*-merged.bin
```

#### 8 MB app-only binary

Contains only the application firmware. Use this if the device already has a valid bootloader and partition table (e.g., from a previous `pio run -t upload`):

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem* write_flash 0x10000 M5Paper-S3-Chinese-Books-*-app-only.bin
```

> **Port note:** On macOS the port is typically `/dev/cu.usbmodem*`. On Linux use `/dev/ttyACM0`. On Windows use `COM3` (check Device Manager).

### Option B: Build from Source

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
2. Clone the repository:
   ```bash
   git clone https://github.com/yuleshow/M5Stack-Paper-S3-Chinese-Books.git
   ```
3. Connect the M5Stack Paper S3 via USB-C
4. Build and upload:
   ```bash
   cd M5Stack-Paper-S3-Chinese-Books
   pio run -t upload
   ```

Dependencies (`M5Unified`, `OpenFontRender`) are auto-installed by PlatformIO.

---

## First Boot

1. Insert the prepared SD card into the M5Stack Paper S3
2. Connect USB-C or press the power button
3. The **welcome screen** appears with the cover image
4. **Tap anywhere** to enter the dashboard

If WiFi is configured in `config.ini`, the device will:
- Connect to your network automatically
- Sync time via NTP (`pool.ntp.org`)
- Fetch weather data from OpenWeatherMap

The status bar at the top shows: WiFi status, battery percentage, and charging indicator.

---

## Dashboard Navigation

The dashboard shows **8 icons** in a 2×4 grid:

| Icon | Label | Function |
|------|-------|----------|
| 1 | 電子書 | E-Book Reader |
| 2 | 日曆 | Chinese Almanac Calendar |
| 3 | 待辦事項 | Todo List |
| 4 | 採辦 | Shopping List |
| 5 | 天氣 | Weather Dashboard |
| 6 | 壁紙 | Wallpaper Browser |
| 7 | 設定 | Settings |
| 8 | 睡眠 | Sleep / Motto Preview |

- **Todo** and **Shopping** icons show badge counts for unchecked items
- **Weather** icon shows current temperature and conditions when data is available
- Icons can be customized by placing PNG files (`icon1.png`–`icon8.png`) in `/icons/` on the SD card

**Tap any icon** to enter that feature. Use the **Return** button (lower-right corner) on any screen to go back.

---

## E-Book Reader

### Book List

Tap **電子書** on the dashboard. The book list shows all `.txt` and `.epub` files found in `/books/`. Tap a book title to open it.

### Reading Screen

Books are displayed in **traditional vertical CJK layout** — text flows top-to-bottom in columns, columns flow right-to-left:

```
┌──────────────────────────────┐
│  Status Bar                  │
├──────────────────────────────┤
│        │        │        │   │
│  Col 3 │  Col 2 │  Col 1 │   │
│        │        │        │   │
│  ↓     │  ↓     │  ↓     │   │
│        │        │        │   │
│     ←── Reading direction    │
│                              │
├──────────────────────────────┤
│  Progress: Page 5/120  4%   │
├──────────────────────────────┤
│ ◀  ▶  │ 字- 30 字+ 字型 BM │◉│
└──────────────────────────────┘
```

### Reading Controls

| Area | Action |
|------|--------|
| **Left half of screen** | Next page (forward in book) |
| **Right half of screen** | Previous page (backward) |
| **◀ Left arrow** | Next page |
| **▶ Right arrow** | Previous page |
| **字-** | Decrease font size (min 20px) |
| **字+** | Increase font size (max 52px) |
| **字型** | Open font selection menu |
| **BM** | Add/remove bookmark (filled = bookmarked) |
| **◉ Return** | Back to book list |

- Font size changes in steps of 4px and is saved across sessions
- Reading position is **auto-saved** to a `.pos` file alongside the book
- Up to **5 bookmarks** per book, saved to `.bm` files

### Font Selection

The font menu shows all fonts found in `/fonts/`. Each font displays a sample in its own typeface:
- **▣** = Pre-rendered BIN font (fastest rendering)
- **Ⓣ** = TTF/TTC font (reflow, any size)
- **✓** = Currently selected font

---

## Chinese Almanac Calendar

### Daily View

Tap **日曆** to see today's almanac. The screen shows:

- **Solar date** (公曆) and **Lunar date** (農曆)
- **天干地支 (Heavenly Stems & Earthly Branches)** — Year, Month, and Day pillars (八字)
- **生肖** — Zodiac animal
- **納音五行** — NaYin Five Elements
- **二十四節氣** — Solar terms (calculated astronomically)
- **宜/忌** — Auspicious and inauspicious activities for the day
- **喜神/福神/財神方位** — Lucky god directions
- **胎神** — Fetal god position
- **彭祖百忌** — Daily taboos
- **時辰吉凶** — 12 two-hour fortunes with GanZhi
- **沖煞** — Zodiac clash and direction
- **六曜** — Rokuyo daily fortune
- **節日** — Daoist, folk, and Buddhist festivals
- **朔/望** — New moon (🌑) and Full moon (🌕) markers

### Navigation

| Control | Action |
|---------|--------|
| **◀ Left arrow** | Next day |
| **▶ Right arrow** | Previous day |
| **Tap date number** | Open month calendar picker |
| **Return** | Back to dashboard |

### Month Calendar Picker

Shows a full month grid. Tap any day to view its almanac. Additional buttons:
- **今天** — Jump to today
- **Tap month/year title** — Open the year/month selector (number pad for year, grid for month)

### Calendar Methods

Two astronomical calculation methods available (changeable in Settings → 曆法計算):
- **Meeus 天文算法** — Accurate to approximately 1 minute
- **壽星天文曆** — 許劍偉's algorithm, faster computation

---

## Weather Dashboard

Requires WiFi and an OpenWeatherMap API key in `config.ini`.

### Display Layout

- **City name** and weather icon
- **Current temperature** (large, centered)
- **Feels like** temperature (體感)
- **Details grid:** Min/Max temp, humidity, wind speed, pressure, visibility, sunrise/sunset
- **Air Quality Index (AQI):** PM2.5, PM10, O₃, NO₂, CO with Chinese quality labels (優/良/中等/差/極差)
- **3-day forecast** with temperatures and Chinese descriptions

### Controls

| Control | Action |
|---------|--------|
| **↻ Refresh** (lower-left) | Force weather data refresh |
| **°C/°F** (lower-right) | Toggle temperature units |
| **Return** | Back to dashboard |

Weather auto-refreshes every 15 minutes when on the weather screen.

---

## Todo List

### CSV Format

The todo list is stored in `/todo_list.csv` on the SD card:

```csv
2/25/26,買牛奶
3/1/26,交報告
,打電話給媽媽
```

Each line: `date,task` — date is optional (`MM/DD/YY` format). Items are sorted by date.

### Controls

| Control | Action |
|---------|--------|
| **Tap checkbox** | Toggle done/undone (partial e-ink update) |
| **Tap date area** | Open date picker for that item |
| **+ button** | Add new item via Cangjie input |
| **清除** | Remove all checked items (appears when items are checked) |
| **◀ / ▶** | Page navigation |
| **Return** | Back to dashboard |

---

## Shopping List

### CSV Format

The shopping list is stored in `/shopping_list.csv` on the SD card:

```csv
1,食,超市,"牛奶 雞蛋 麵包"
1,食,COSTCO,"香蕉 水 Cream Cheese, 蛋糕"
```

Format: `number,category,groupName,"items"`. Within the quoted items field:
- **Chinese items** are separated by spaces
- **English items** are separated by commas

### Controls

Same as Todo List — tap checkboxes to mark items, use 清除 to remove checked items.

---

## Cangjie Input Method

The **倉頡輸入法 (Cangjie 5)** on-screen keyboard is used for adding items to Todo and Shopping lists.

### How to Use

1. Type Cangjie radical codes using the on-screen QWERTY keyboard
2. Each key corresponds to a Cangjie radical (e.g., `a`=日, `b`=月, `c`=金, `d`=木)
3. Matching candidates appear in the bar above the keyboard (up to 8 per page)
4. **Tap a candidate** to select it
5. Continue composing characters
6. Tap **確定** (Confirm) to add the composed text
7. Tap **取消** (Cancel) to discard

### Key-to-Radical Mapping

```
Q手  W田  E水  R口  T廿  Y卜  U山  I戈  O人  P心
 A日  S尸  D木  F火  G土  H竹  J十  K大  L中
  Z重  X難  C金  V女  B月  N弓  M一
```

**Special keys:** `←Del` (backspace), `Space` (space), candidate page arrows (`<` / `>`)

---

## Wallpaper Browser

Tap **壁紙** on the dashboard to browse wallpaper images from `/wallpapers/` on the SD card.

- Tap a filename to display it full-screen
- Tap anywhere on the displayed wallpaper to return to the list
- Supports JPG, PNG, and BMP formats (up to 2 MB each, 30 files max)
- For best results, use images sized 540×960 pixels

---

## BLE Proximity Unlock

Use your Paper S3 as a wireless key to auto-lock and unlock your Mac.

### How it works

1. The Paper S3 continuously advertises as a **BLE beacon** (no pairing required)
2. A macOS companion script monitors the Bluetooth signal strength (RSSI)
3. When you walk away → signal drops → Mac locks automatically
4. When you return → signal detected → companion script types your password locally

### Setup

**Step 1: Enable BLE on the device**

Add an `[unlock]` section to `config.ini` on your SD card:

```ini
[unlock]
enabled=true
device_name=M5Paper-BLE
```

| Field | Description |
|-------|-------------|
| `enabled` | `true` to enable BLE advertising on boot |
| `device_name` | BLE device name (used by the companion script to find the device) |

**Step 2: Install and run the companion script**

```bash
pip install bleak
python3 scripts/ble_unlock.py --password 'YOUR_MAC_PASSWORD'
```

The password is saved securely in **macOS Keychain** — it is never stored in any config file. Subsequent runs don't need `--password`:

```bash
python3 scripts/ble_unlock.py
```

**Step 3 (optional): Run as background service**

```bash
python3 scripts/ble_unlock.py --install-service
```

This creates a macOS LaunchAgent that starts automatically on login. The password is read from Keychain.

### Configuration options

| Flag | Default | Description |
|------|---------|-------------|
| `--password` | — | Mac login password (saved to macOS Keychain) |
| `--name` | `M5Paper-BLE` | BLE device name to monitor |
| `--lock-threshold` | `-85` | RSSI below this triggers lock (dBm) |
| `--unlock-threshold` | `-70` | RSSI above this triggers unlock (dBm) |
| `--lock-delay` | `15` | Seconds of absence before locking |
| `--scan-interval` | `3` | Seconds between BLE scans |
| `--debug` | — | Enable verbose logging |

### Security notes

- Your Mac password is stored in **macOS Keychain** (encrypted, never in plaintext files)
- No Bluetooth pairing required — the device is a passive beacon
- The companion script runs locally on your Mac and handles all lock/unlock logic

---

## Settings

Tap **設定** on the dashboard. Six settings are available:

### 1. WiFi 設定

- Scans for available networks
- Select a network from the list
- Enter password using the on-screen keyboard (3 modes: lowercase, uppercase, symbols)
- Connection status shown below

### 2. 時區設定

- Select from 7 timezone presets (UTC, Beijing/Taipei, Tokyo, US West, US East, UK, Sydney)
- Time re-syncs via NTP after changing

### 3. 檔案上傳伺服器 (Web Server)

- Toggle the built-in HTTP file manager on/off
- When running, displays the URL (e.g., `http://192.168.1.100`)
- Access from any browser on the same network
- See [Web File Manager](#web-file-manager) for details

### 4. USB 外接磁碟 (USB Mass Storage)

- Exposes the SD card as a USB drive to your computer
- **Warning:** While active, the device cannot access the SD card
- Disabling USB MSC **restarts the device**

### 5. 圖標來源 (Icon Source)

- **SD 卡優先** — Load custom icons from `/icons/`, fall back to embedded
- **內建圖標** — Use embedded icons (faster boot)

### 6. 曆法計算 (Calendar Method)

- **Meeus 天文算法** — Higher precision (~1 minute accuracy)
- **壽星天文曆** — Faster computation

---

## Sleep & Power Management

### Entering Sleep

- Tap **睡眠** on the dashboard to preview the sleep screen
- The device **auto-sleeps after 10 minutes** of inactivity (when not charging)
- Sleep is skipped when on the Clock screen or when charging via USB

### Sleep Screen

Displays a random Chinese motto from `/mottos.txt` in vertical calligraphy layout. 10 built-in mottos are available if the file is missing.

### Waking Up

- **Touch the screen** to wake up (GPIO 21 interrupt)
- The device resumes where you left off (mode, page, font size are preserved)

### Battery

- Battery percentage is shown in the top-right status bar
- A **+** symbol appears next to the battery when charging
- The device will not auto-sleep while charging

---

## Web File Manager

When the web server is enabled (Settings → 檔案上傳伺服器), you can manage files on the SD card from any web browser.

### Accessing

1. Enable the server in Settings
2. Note the URL shown (e.g., `http://192.168.1.100`)
3. Open that URL in a browser on the same WiFi network

### Features

| Feature | Description |
|---------|-------------|
| **Browse** | Navigate folders, see file sizes |
| **Upload** | Drag-and-drop or click to upload files to any directory |
| **Download** | Download files from the SD card |
| **Delete** | Remove files or folders |
| **Screenshot** | Capture the current e-ink display as an image |

### Screenshot Tool

Visit `http://<device_IP>/screen` for a live screen capture page with:
- **📸 Capture** — Take a screenshot
- **▶ Auto** — Auto-capture at 5/10/30/60 second intervals
- **💾 Save** — Download the capture as PNG

---

## USB Mass Storage

An alternative to the web file manager for transferring files.

1. Go to Settings → **USB 外接磁碟**
2. Enable USB MSC mode
3. The SD card appears as a removable drive on your computer (`Paper SD`)
4. Copy files directly via your file manager
5. **Disable USB MSC when done** — this will restart the device

> **Note:** While USB MSC is active, the device itself cannot access the SD card. All other features are unavailable until disabled.

---

## Troubleshooting

### Device won't boot / blank screen

- **Check SD card:** Remove and reinsert. Ensure it's FAT32 formatted.
- **Try without SD card:** The device can boot without one (limited functionality).
- **Re-flash firmware:** Connect via USB-C and run `pio run -t upload`.
- **Hard reset:** Hold the power button for 10+ seconds.

### WiFi won't connect

- Verify SSID and password in `config.ini` (case-sensitive)
- Only 2.4 GHz networks are supported (5 GHz won't appear)
- Try connecting through Settings → WiFi 設定 with the on-screen keyboard
- Check that special characters in the password are correct

### Weather not showing

- Verify your OpenWeatherMap API key in `config.ini`
- New API keys may take up to 2 hours to activate
- Check WiFi connection — weather requires internet
- Tap the refresh button (lower-left) on the weather screen
- Verify city format: `City,Country` (e.g., `Taipei,TW` or `Pasadena,CA,US`)

### Books not appearing

- Place files in `/books/` on the SD card (not a subfolder)
- Supported formats: `.txt` (UTF-8) and `.epub`
- Maximum 20 books
- Filenames starting with `.` are hidden (e.g., macOS `._` files)

### E-ink display ghosting

- This is normal for e-ink partial updates (checkbox toggles, popup interactions)
- Navigate to a different screen and back for a full refresh
- All full-screen redraws use high-quality mode to clear ghosting

### Font not rendering correctly

- Ensure the font file is a valid TTF or TTC
- Place fonts in `/fonts/` on the SD card
- Try the recommended font: `GenYoMinTW-Regular.ttf`
- If no custom font is loaded, the device falls back to a built-in Japanese Mincho font

### Touch not responding

- E-ink displays have a brief unresponsive period during screen refresh — wait for the refresh to complete
- If consistently unresponsive, try a hard reset (power button 10+ seconds)
- The touch sensor (GT911) connects via GPIO 21

### Device drains battery quickly

- The device auto-sleeps after 10 minutes of inactivity
- Disable WiFi if not using weather or web server features
- The Clock screen keeps the device awake — exit to allow auto-sleep
- Charging is indicated by the **+** symbol next to the battery icon

### SD card errors / data corruption

- Always safely eject before removing the SD card
- Disable USB MSC mode before physically removing the card
- Use high-quality microSD cards (Class 10 or UHS-I recommended)
- Do not access the SD card from the web server and USB MSC simultaneously

### Web file manager not accessible

- Ensure your computer and the device are on the same WiFi network
- Check the IP address shown in Settings → 檔案上傳伺服器
- Try `http://` (not `https://`)
- Firewall software may block port 80

### Cangjie input — character not found

- Cangjie 5 dictionary requires `/cangjie5.bin` on the SD card
- Not all characters are in the dictionary — try alternative code sequences
- Maximum code length is 5 radicals

---

## File Reference

| File | Location | Description |
|------|----------|-------------|
| `config.ini` | SD root | WiFi, timezone, weather configuration |
| `mottos.txt` | SD root | One motto per line for sleep screen |
| `shopping_list.csv` | SD root | Shopping list data |
| `todo_list.csv` | SD root | Todo list data |
| `shopping_checked.txt` | SD root | Shopping checked state (auto-generated) |
| `todo_checked.txt` | SD root | Todo checked state (auto-generated) |
| `cangjie5.bin` | SD root | Cangjie input dictionary |
| `books/*.txt` | `/books/` | Text books (UTF-8) |
| `books/*.epub` | `/books/` | EPUB e-books |
| `books/*.pos` | `/books/` | Reading position (auto-generated) |
| `books/*.bm` | `/books/` | Bookmarks (auto-generated) |
| `fonts/*.ttf` | `/fonts/` | TrueType fonts |
| `fonts/*.ttc` | `/fonts/` | TrueType Collection fonts |
| `fonts/*.bin` | `/fonts/` | Pre-rendered binary fonts |
| `wallpapers/*` | `/wallpapers/` | JPG/PNG/BMP wallpaper images |
| `icons/icon1-8.png` | `/icons/` | Custom dashboard icons |
