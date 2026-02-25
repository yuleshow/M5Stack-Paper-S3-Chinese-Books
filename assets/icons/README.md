# Icons

PNG icons for the dashboard and navigation buttons.

## Dashboard Icons

The main screen displays a 2×4 grid of icons. Each icon is a 120×120 PNG:

| File | Function |
|------|----------|
| `icon1.png` | 電子書 (Book Reader) |
| `icon2.png` | 日曆 (Calendar / Almanac) |
| `icon3.png` | 待辦事項 (Todo List) |
| `icon4.png` | 採辦 (Shopping List) |
| `icon5.png` | 天氣 (Weather) |
| `icon6.png` | 壁紙 (Wallpaper) |
| `icon7.png` | 設定 (Settings) |
| `icon8.png` | 睡眠 (Sleep / Motto) |

## Navigation Icons

| File | Function |
|------|----------|
| `back.png` | Back / previous page button |
| `next.png` | Next page button |
| `return.png` | Return to dashboard button |

## Subdirectories

| Directory | Description |
|-----------|-------------|
| `originals/` | Original high-resolution source icons |
| `icons.bak/` | Backup of previous icon set |

## Customization

Icons can be loaded from SD card or use embedded fallbacks (configurable in Settings → 圖標來源). To customize:

1. Place your PNG icons on the SD card at `/icons/`
2. Set icon source to "SD 卡優先" in Settings

Use `resize_icons.py` to batch-resize icons to the required 120×120 dimensions.
