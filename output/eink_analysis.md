# E-Ink Display Usage — Comprehensive Codebase Analysis

**Hardware**: M5Stack Paper S3 — 540×960 pixels, 4-bit grayscale  
**Library**: M5Unified (LGFX backend) — `epd_mode_t` enum, `M5.Display` API  
**Display**: `M5.Display` — wraps LGFX e-paper driver

---

## Table of Contents

1. [EPD Mode Usage](#1-epd-mode-usage)
2. [Display Flush Calls](#2-display-flush-calls)
3. [fillScreen Calls](#3-fillscreen-calls)
4. [Sprite Usage (LGFX_Sprite)](#4-sprite-usage)
5. [Partial Refresh Patterns](#5-partial-refresh-patterns)
6. [Ghosting Mitigation (Two-Pass Flash)](#6-ghosting-mitigation)
7. [Color Depth & Grayscale Usage](#7-color-depth--grayscale-usage)
8. [Inconsistencies & Potential Issues](#8-inconsistencies--potential-issues)

---

## 1. EPD Mode Usage

### Modes Used

| Mode | Description | Usage Pattern |
|------|-------------|---------------|
| `epd_quality` | Full high-quality refresh | Default mode for all full-screen redraws |
| `epd_fast` | Fast partial refresh | Used exclusively for partial updates |
| `epd_fastest` | Not used | — |
| `epd_text` | Not used | — |

### Per-File EPD Mode Calls

| File | `setEpdMode` Calls | Mode(s) Used |
|------|-------------------|--------------|
| **main.cpp** | `setup()`, `enterDeepSleep()`, loading screen, shopping/todo checkbox toggles, shopping/todo "清除" button, todo error popup, USB MSC status messages, year-month popup partial updates | `epd_quality` (setup, sleep, loading), `epd_fast` (checkbox toggles, clear actions, year display, month grid, error popups) |
| **dashboard.cpp** | `drawWelcome()`, `drawDashboard()` | `epd_quality` only |
| **book_reader.cpp** | `drawBookList()`, `drawReading()` | `epd_quality` only |
| **calendar.cpp** | `drawCalendar()`, `drawCalendarPicker()`, `drawCalendarYearMonth()` | `epd_quality` only |
| **weather.cpp** | `drawWeather(bool fast)`, `redrawWeatherUnits()` | `epd_quality` or `epd_fast` (parameterized), `epd_fast` (units redraw) |
| **shopping_list.cpp** | `drawShoppingList()` | `epd_quality` only (partial updates done in main.cpp) |
| **todo_list.cpp** | `drawTodoList()`, `drawTodoDatePicker()` | `epd_quality` only (partial updates done in main.cpp) |
| **cangjie_input.cpp** | `drawCangjieInput()`, `updateCangjieInputArea()` | `epd_quality` (full draw), `epd_fast` (partial update) |
| **wallpaper.cpp** | `drawWallpaperList()`, `drawWallpaper()` | `epd_quality` only |
| **motto.cpp** | `drawMottoScreen()` | `epd_quality` only |
| **setup_ui.cpp** | `drawFontMenu()`, `updatePasswordDisplay()`, `drawClock()`, `drawSetupMenu()`, `drawWiFiSetup()`, `drawWebServerSetup()`, `drawIconSetup()`, `drawCalendarSetup()` | `epd_quality` (all full draws), `epd_fast` (password field only) |
| **usb_msc_handler.cpp** | `drawUSBMSCSetup()` | `epd_quality` only |
| **ui_drawing.cpp** | None | No direct EPD mode calls — utility functions only |
| **cleanup.cpp** | None | No display code |
| **epub_reader.cpp** | None | No display code (data processing only) |
| **font_manager.cpp** | None | No display code |
| **globals.cpp** | None | No display code |

### Mode Restoration Pattern

Functions that switch to `epd_fast` consistently restore `epd_quality` afterward:

- ✅ `updateCangjieInputArea()` — restores at end
- ✅ `updatePasswordDisplay()` — restores at end
- ✅ Shopping checkbox toggle (main.cpp) — restores after `return`
- ✅ Todo checkbox toggle (main.cpp) — restores after `return`
- ✅ Shopping "清除" (main.cpp) — restores at end of block
- ✅ Todo "清除" (main.cpp) — restores at end of block
- ✅ Year-month popup `updateYearDisplay` lambda (main.cpp) — no explicit restore, but the next touch triggers either another partial update or a full `epd_quality` redraw
- ✅ Year-month popup month grid (main.cpp) — no explicit restore, same reasoning
- ⚠️ `redrawWeatherUnits()` — does NOT explicitly restore `epd_quality` (relies on the next full redraw to set it)

---

## 2. Display Flush Calls

### `M5.Display.display()` Calls Per File

| File | Count | Context |
|------|-------|---------|
| **main.cpp** | ~20+ | Loading screen, deep sleep screen, checkbox partial updates (multiple per toggle), clear button updates, error popups, WiFi connect result, USB MSC status messages, year-month popup partial updates |
| **dashboard.cpp** | 2 | `drawWelcome()`, `drawDashboard()` |
| **book_reader.cpp** | 2 | `drawBookList()`, `drawReading()` |
| **calendar.cpp** | 3 | `drawCalendar()`, `drawCalendarPicker()`, `drawCalendarYearMonth()` |
| **weather.cpp** | 5 | `drawWeather()` ×2 (loading + final), error screen, `redrawWeatherUnits()` ×2 (pass 1 + pass 2) |
| **shopping_list.cpp** | 1 | `drawShoppingList()` (skipped if `pendingNavTouch`) |
| **todo_list.cpp** | 2 | `drawTodoList()` (skipped if `pendingNavTouch`), `drawTodoDatePicker()` |
| **cangjie_input.cpp** | 2 | `drawCangjieInput()`, `updateCangjieInputArea()` |
| **wallpaper.cpp** | 3 | `drawWallpaperList()`, `drawWallpaper()`, error screen |
| **motto.cpp** | 1 | `drawMottoScreen()` |
| **setup_ui.cpp** | 8 | `drawFontMenu()`, `updatePasswordDisplay()`, `drawClock()`, `drawSetupMenu()`, `drawWiFiSetup()` ×3 (timezone/keyboard/network list paths), `drawWebServerSetup()` |
| **usb_msc_handler.cpp** | 1 | `drawUSBMSCSetup()` |

### `pendingNavTouch` Skip Pattern

Two files skip `display()` when the user has already tapped a navigation button during rendering:

- `shopping_list.cpp:drawShoppingList()` — skips if `pendingNavTouch == true`
- `todo_list.cpp:drawTodoList()` — skips if `pendingNavTouch == true`
- `setup_ui.cpp:drawFontMenu()` — skips if `pendingNavTouch == true`

This avoids a wasted e-ink refresh when the screen will be immediately redrawn.

### Intermediate `display()` Calls

- **weather.cpp** `drawWeather()`: Shows "載入中..." loading message with first `display()`, fetches data, then draws full weather with second `display()`. This gives user feedback during network operation.
- **main.cpp** USB MSC toggle: Shows "啟動 USB 中..." then calls `display()` before `startUSBMSC()`, then shows result with another `display()`.

---

## 3. fillScreen Calls

Every full-screen draw function follows the pattern:
```cpp
M5.Display.setEpdMode(epd_mode_t::epd_quality);
M5.Display.fillScreen(TFT_WHITE);
M5.Display.setTextColor(TFT_BLACK);
// ... draw content ...
M5.Display.display();
```

**All fillScreen calls use `TFT_WHITE`** — no exceptions. There are no `fillScreen(TFT_BLACK)` calls.

| File | fillScreen in Functions |
|------|----------------------|
| dashboard.cpp | `drawWelcome()`, `drawDashboard()` |
| book_reader.cpp | `drawBookList()`, `drawReading()` |
| calendar.cpp | `drawCalendar()`, `drawCalendarPicker()`, `drawCalendarYearMonth()` |
| weather.cpp | `drawWeather()`, error recovery |
| shopping_list.cpp | `drawShoppingList()` |
| todo_list.cpp | `drawTodoList()`, `drawTodoDatePicker()` |
| cangjie_input.cpp | `drawCangjieInput()` |
| wallpaper.cpp | `drawWallpaperList()`, `drawWallpaper()` |
| motto.cpp | `drawMottoScreen()` |
| setup_ui.cpp | `drawFontMenu()`, `drawClock()`, `drawSetupMenu()`, `drawWiFiSetup()`, `drawWebServerSetup()`, `drawIconSetup()`, `drawCalendarSetup()` |
| usb_msc_handler.cpp | `drawUSBMSCSetup()` |
| main.cpp | Loading screen, `enterDeepSleep()`, WiFi connect result |

### Notable: `drawWelcome()` Skips fillScreen

`drawWelcome()` calls `M5.Display.drawJpg(s3cover_jpg, ...)` which fills the entire screen with the cover image, so no `fillScreen()` is needed.

---

## 4. Sprite Usage (LGFX_Sprite)

### Usage Locations

| File | Function | Purpose | Color Depth | Size |
|------|----------|---------|-------------|------|
| **shopping_list.cpp** | `drawShoppingList()` | Rotate ASCII text 90° CW for vertical CJK layout | 16-bit | Dynamic (text height × width) |
| **shopping_list.cpp** | `drawVerticalMixedText()` | Rotate ASCII runs 90° CW within mixed CJK/ASCII vertical text | 16-bit | `(asciiRun.length()*fontSize*0.6) × fontSize` |
| **todo_list.cpp** | `drawTodoList()` | Rotate date characters 90° CW, rotate ASCII task text 90° CW | 16-bit | Dynamic |
| **ui_drawing.cpp** | `drawLabelBitmap()` | Render pre-rendered label bitmaps efficiently | **1-bit** (`setColorDepth(1)`) | Bitmap dimensions (w × h) |

### Sprite Lifecycle Pattern

All sprite usage follows a consistent create-use-destroy pattern:
```cpp
LGFX_Sprite sprite(&M5.Display);
sprite.setColorDepth(16);  // or 1
sprite.createSprite(w, h);
sprite.fillSprite(TFT_WHITE);
// ... draw on sprite ...
sprite.pushRotateZoom(cx, cy, angle, 1.0, 1.0, TFT_WHITE);  // or pushSprite
sprite.deleteSprite();
```

### Sprite Failure Handling

- **ui_drawing.cpp** `drawLabelBitmap()`: Falls back to pixel-by-pixel rendering if `createSprite()` fails (returns `nullptr`). This is the only sprite usage with failure handling.
- **shopping_list.cpp** and **todo_list.cpp**: No fallback — if `createSprite()` fails on low memory, the rotated text simply won't appear.

### Rotation Angles

- All rotated text uses **90° clockwise** (`pushRotateZoom(..., 90, ...)`) for vertical CJK reading direction.
- Label bitmaps use `pushSprite()` (no rotation).

---

## 5. Partial Refresh Patterns

### Summary of All Partial Updates

| Location | Trigger | Region Updated | Mode |
|----------|---------|----------------|------|
| `cangjie_input.cpp:updateCangjieInputArea()` | Key press, candidate select, backspace | Y=42–280 (composed text + input code + candidates) | `epd_fast` |
| `setup_ui.cpp:updatePasswordDisplay()` | Key press, backspace | Password field rect only (20, 240, 500, 60) | `epd_fast` |
| `weather.cpp:redrawWeatherUnits()` | °C/°F toggle | Collected rects of temp/wind values | `epd_fast` + two-pass |
| main.cpp: shopping checkbox toggle | Touch on checkbox | Checkbox area (cbSize + 12px padding) + 清除 button | `epd_fast` + two-pass |
| main.cpp: todo checkbox toggle | Touch on checkbox | Checkbox area (cbSize + 12px padding) + 清除 button | `epd_fast` + two-pass |
| main.cpp: shopping "清除" clear | Touch on clear button | Each checked item's area + clear button itself | `epd_fast` + two-pass |
| main.cpp: todo "清除" clear | Touch on clear button | Each checked item's area + clear button itself | `epd_fast` + two-pass |
| main.cpp: todo error popup | Failed to load Cangjie table | Error message rect (100, 400, 340, 80) | `epd_fast` + two-pass |
| main.cpp: year-month `updateYearDisplay` | Number pad input | Year display row (centerX-100, 65, 220, 55) | `epd_fast` + two-pass |
| main.cpp: year-month month grid | Month selection | Old + new month cells only | `epd_fast` + two-pass |

### Partial Update Techniques

**Technique 1: Simple Partial (Clear + Redraw)**
Used by `updateCangjieInputArea()` and `updatePasswordDisplay()`:
```cpp
M5.Display.setEpdMode(epd_mode_t::epd_fast);
M5.Display.fillRect(x, y, w, h, TFT_WHITE);  // Clear area
// ... redraw content ...
M5.Display.display();
M5.Display.setEpdMode(epd_mode_t::epd_quality);  // Restore
```

**Technique 2: Two-Pass Flash (Black-then-White)**
Used by checkbox toggles, clear actions, weather unit toggle, year-month popup:
```cpp
M5.Display.setEpdMode(epd_mode_t::epd_fast);
// Pass 1: Flash area black
M5.Display.fillRect(x, y, w, h, TFT_BLACK);
M5.Display.display();
// Pass 2: Draw white + content
M5.Display.fillRect(x, y, w, h, TFT_WHITE);
// ... redraw content ...
M5.Display.display();
M5.Display.setEpdMode(epd_mode_t::epd_quality);
```

---

## 6. Ghosting Mitigation

### Techniques Used

1. **Two-Pass Flash** (see Section 5, Technique 2): Flashes affected area to BLACK first, then WHITE + content. This forces all e-ink particles to a known state, eliminating ghost remnants from previous content. Used in 7 locations.

2. **Full-Screen Clear**: Every full redraw starts with `fillScreen(TFT_WHITE)`, ensuring no ghosting from the previous screen.

3. **`cfg.clear_display = false`** (main.cpp `setup()`): Deliberately disables the M5Unified automatic display clear on boot. This prevents a partial buffer flash artifact when waking from deep sleep (the sleeping wallpaper would briefly flash).

4. **`delay()` After `display()`**: Allows the e-ink controller time to complete the refresh cycle before proceeding:
   - `book_reader.cpp`: `delay(500)` after both `drawBookList()` and `drawReading()`
   - `setup_ui.cpp`: `delay(500)` after `drawFontMenu()`
   - `main.cpp`: `delay(3000)` after WiFi connect result, `delay(2000)` after USB MSC status, `delay(2000)` after Cangjie error popup
   - `main.cpp`: `delay(100)` in main `loop()` (general touch debounce)

### Areas Without Ghosting Mitigation

- `updateCangjieInputArea()` — uses simple clear (Technique 1), no black flash. This is acceptable because the updated region (text + candidates) changes frequently and residual ghosting is minor.
- `updatePasswordDisplay()` — same simple clear pattern.

---

## 7. Color Depth & Grayscale Usage

### Color Constants Defined (globals.h)

| Constant | Value | Hex RGB565 | Usage |
|----------|-------|------------|-------|
| `TFT_BLACK` | 0x0000 | — | Primary text, borders, filled buttons, checkmarks |
| `TFT_WHITE` | 0xFFFF | — | Background, text on dark buttons |
| `TFT_LIGHTGRAY` | (LGFX built-in) | — | Key backgrounds (keyboard), menu item backgrounds |
| `TFT_DARKGRAY` | (LGFX built-in) | — | Confirm buttons, WiFi status, shadow effects |
| `EPD_DARK_GRAY` | 0x4208 | — | Solar terms, date text in todo, status text, decorative elements |
| `EPD_MID_GRAY` | 0x7BEF | — | 凶 (inauspicious) hour fortune text |
| `EPD_LIGHT_GRAY` | 0xC618 | — | WiFi status box background, card border decoration |
| `EPD_HIGHLIGHT` | 0x4208 | — | Same as EPD_DARK_GRAY (alias) |
| `0x5AEB` | (inline) | — | Shopping group name background (gray banner) |

### Color Usage by Semantic Role

| Role | Color(s) Used |
|------|---------------|
| Primary text | `TFT_BLACK` |
| Background | `TFT_WHITE` |
| Secondary/status text | `EPD_DARK_GRAY`, `TFT_DARKGRAY` |
| Subtle indicators | `EPD_MID_GRAY` |
| UI element fills | `TFT_LIGHTGRAY` (keys), `EPD_DARK_GRAY` (toggle buttons) |
| Selected state (inverted) | `TFT_BLACK` bg + `TFT_WHITE` text |
| Group headers | `0x5AEB` bg + `TFT_WHITE` text |
| Decorative borders | `EPD_LIGHT_GRAY`, `EPD_DARK_GRAY` |
| Shadow effects | `TFT_DARKGRAY` (WiFi clock box shadow) |
| Badge/count circles | `TFT_BLACK` circle + `TFT_WHITE` text |

### Effective Grayscale Levels on Screen

The display renders 4-bit grayscale (16 levels), but the codebase uses only ~5–6 distinct gray levels:
1. Pure black (`TFT_BLACK`)
2. Dark gray (`EPD_DARK_GRAY` / 0x4208)
3. Mid gray (`EPD_MID_GRAY` / 0x7BEF or `0x5AEB`)
4. Light gray (`TFT_LIGHTGRAY` / `EPD_LIGHT_GRAY`)
5. Dark-ish gray (`TFT_DARKGRAY`)
6. Pure white (`TFT_WHITE`)

---

## 8. Inconsistencies & Potential Issues

### 8.1 Inconsistent Delay After display()

| Function | Delay | Notes |
|----------|-------|-------|
| `drawBookList()` | 500ms | ✅ |
| `drawReading()` | 500ms | ✅ |
| `drawFontMenu()` | 500ms | ✅ |
| `drawDashboard()` | None | ⚠️ No delay |
| `drawCalendar()` | None | ⚠️ No delay |
| `drawWeather()` | None | ⚠️ No delay |
| All other draw functions | None | ⚠️ No delay |

The 500ms delays in book_reader and drawFontMenu appear to be for touch debounce (preventing accidental double-taps during the e-ink refresh period), not for e-ink completion. Most other screens rely on the main loop's `delay(50)` + `M5.update()` cycle. This is **functionally fine** but **inconsistent** — either all screens should debounce or none should.

### 8.2 No Explicit Mode Restore in redrawWeatherUnits()

`redrawWeatherUnits()` sets `epd_fast` but never restores to `epd_quality`. The next interaction (return to dashboard, refresh) happens to call a function that sets `epd_quality`, so this doesn't cause bugs in practice. However, it's fragile — if a new weather interaction were added that doesn't set a mode, it would inherit `epd_fast`.

### 8.3 No Explicit Mode Restore in Year-Month Popup Partial Updates

Both `updateYearDisplay()` lambda and the month grid partial update in `MODE_CALENDAR_YEAR_MONTH` touch handler (main.cpp) set `epd_fast` but don't restore `epd_quality`. Same reasoning as 8.2 — subsequent interactions always set their own mode.

### 8.4 Missing Sprite Creation Failure Handling

`shopping_list.cpp` and `todo_list.cpp` create sprites for rotated text without checking if `createSprite()` succeeded. On low-memory conditions, `pushRotateZoom()` on a failed sprite could cause visual artifacts or crashes. Only `ui_drawing.cpp:drawLabelBitmap()` has proper fallback handling.

### 8.5 Font Mixing in drawWiFiSetup()

`drawWiFiSetup()` is the only screen that uses M5.Display's built-in font API (`setFont()`, `setTextSize()`, `setCursor()`, `print()`) extensively instead of the project's `drawSystemText()` / `drawSystemTextCentered()` helpers. This creates visual inconsistency — the WiFi setup screen uses `fonts::Font2` at various sizes while all other screens use bitmap labels or OFR-rendered Chinese fonts. The clock display box in WiFi setup also uses raw `drawString()` calls.

Similarly, `usb_msc_handler.cpp:drawUSBMSCSetup()` mixes `drawSystemText()` with raw `M5.Display.setFont()` + `M5.Display.println()` calls for the English text portions.

### 8.6 startWrite() / endWrite() Usage

`drawMottoScreen()` in `motto.cpp` uses `startWrite()` and `endWrite()` to batch SPI transactions:
```cpp
M5.Display.startWrite();
M5.Display.drawJpg(...);
drawMottoOnSleep();
drawReturnButton();
M5.Display.endWrite();
M5.Display.display();
```
No other draw function uses this pattern. For e-ink displays, `startWrite()/endWrite()` affects the SPI bus locking but doesn't change refresh behavior (the `display()` call is what triggers the actual e-ink update). Including it is harmless but inconsistent with the rest of the codebase.

### 8.7 No Partial Refresh for Page Turns in Book Reader

`drawReading()` performs a full `epd_quality` refresh for every page turn. Given that the reading area content changes entirely between pages, a full refresh is appropriate for quality. However, the 500ms delay after each page turn compounds with the e-ink refresh time (~1–2s for quality mode), creating noticeable lag. Using `epd_fast` for rapid page-turning sequences could improve UX, similar to how `drawWeather(bool fast)` accepts a speed parameter.

### 8.8 Redundant setTextColor Calls

Almost every draw function calls `M5.Display.setTextColor(TFT_BLACK)` at the top, but then uses `drawSystemText()` which sets its own color per call. The global `setTextColor` only matters for raw `M5.Display.print()` / `M5.Display.drawString()` calls, which are rare outside `drawWiFiSetup()`.

### 8.9 drawDashboard() Badge Rendering

`drawDashboard()` draws badge counts (todo/shopping item counts) using:
```cpp
M5.Display.fillCircle(badgeX, badgeY, 14, TFT_BLACK);
// ... draws white number text on black circle
```
This is one of the few places using `fillCircle` with `TFT_BLACK` as fill color. It renders well but at small sizes may appear blocky on the 4-bit grayscale display.

### 8.10 Potential Race: checkNavTouch() During Rendering

`book_reader.cpp:drawReading()` calls `checkNavTouch()` every 10 characters during rendering. If a touch is detected, it sets `pendingNavTouch = true` and the current draw continues but skips the final `display()`. The next loop iteration handles the navigation. This is clever but has a subtle issue: if the user touches during the early phase of rendering, significant CPU time is spent drawing content that will never be flushed to the screen.

---

## Summary Matrix

| File | EPD Mode | display() | fillScreen | Sprite | Partial | Two-Pass | Grayscale |
|------|----------|-----------|------------|--------|---------|----------|-----------|
| main.cpp | quality + fast | ~20+ | YES | — | YES (5) | YES | BLACK, WHITE, DARK_GRAY |
| dashboard.cpp | quality | 2 | YES | — | — | — | BLACK, WHITE, LIGHTGRAY |
| book_reader.cpp | quality | 2 | YES | — | — | — | BLACK, WHITE, DARK_GRAY |
| calendar.cpp | quality | 3 | YES | — | — | — | BLACK, WHITE, DARK_GRAY, MID_GRAY |
| weather.cpp | quality/fast | 5 | YES | — | YES (1) | YES | BLACK, WHITE, DARK_GRAY |
| shopping_list.cpp | quality | 1 | YES | YES (2) | — | — | BLACK, WHITE, 0x5AEB |
| todo_list.cpp | quality | 2 | YES | YES (2) | — | — | BLACK, WHITE, DARK_GRAY |
| cangjie_input.cpp | quality + fast | 2 | YES | — | YES (1) | — | BLACK, WHITE, LIGHTGRAY, DARKGRAY |
| wallpaper.cpp | quality | 3 | YES | — | — | — | BLACK, WHITE |
| motto.cpp | quality | 1 | — | — | — | — | BLACK, WHITE, DARK_GRAY, LIGHT_GRAY |
| setup_ui.cpp | quality + fast | 8 | YES | — | YES (1) | — | BLACK, WHITE, LIGHTGRAY, DARKGRAY, DARK_GRAY, LIGHT_GRAY |
| usb_msc_handler.cpp | quality | 1 | YES | — | — | — | BLACK, WHITE, DARKGRAY, DARK_GRAY |
| ui_drawing.cpp | — | — | — | YES (1) | — | — | BLACK, WHITE |
| cleanup.cpp | — | — | — | — | — | — | — |
| epub_reader.cpp | — | — | — | — | — | — | — |
