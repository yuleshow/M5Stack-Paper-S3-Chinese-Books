# Assets

SD card content and build-time assets for the M5Stack Paper S3 firmware.

## Files

| File | Description |
|------|-------------|
| `config.ini` | Main configuration: WiFi credentials, timezone, weather API key (gitignored) |
| `config.ini.example` | Template for `config.ini` — copy to SD card and fill in your details |
| `weather.cfg` | Alternative weather-only configuration (gitignored) |
| `weather.cfg.example` | Template for `weather.cfg` |
| `cangjie5.bin` | Compiled Cangjie input method dictionary (binary, loaded at runtime from SD) |
| `mottos.txt` | Daily mottos displayed on the sleep screen, one per line |
| `shopping_list.csv` | Shopping list data (`group|item` format) |
| `todo_list.csv` | Todo list data (`date,task` format) |
| `s3cover.jpg` | Boot splash cover image (converted to C header at build time) |
| `sleeping.jpg` | Sleep screen background image (converted to C header at build time) |
| `labels.jpg` | Reference screenshot of label rendering (for development/debugging) |

## Subdirectories

| Directory | Description |
|-----------|-------------|
| `books/` | E-book files (TXT and EPUB) for the book reader |
| `fonts/` | TTF/TTC/BIN font files for text rendering |
| `icons/` | PNG dashboard icons and navigation buttons |

## SD Card Deployment

Copy the following to the root of your SD card:
- `config.ini` (from `config.ini.example`)
- `cangjie5.bin`
- `mottos.txt`
- `shopping_list.csv`, `todo_list.csv` (optional)
- `books/` directory with your e-books
- `fonts/` directory with at least one TTF font
