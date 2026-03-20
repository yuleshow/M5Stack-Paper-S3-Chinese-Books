#include "globals.h"
#include "embedded_icons.h"
#include "labels/label_bitmaps.h"
#include "labels/silver/silver_label_bitmaps.h"

// Silver font renders smaller than GenYoMinTW at the same pt size.
// Per-size scale table: {nominal_size, render_size} computed from actual glyph measurements.
static const struct { uint8_t nominal; uint8_t scaled; } silverScaleTable[] = {
  {16, 23}, {18, 25}, {20, 27}, {22, 29}, {24, 32}, {26, 35},
  {28, 39}, {32, 44}, {34, 47}, {36, 49}, {38, 53}, {40, 58}, {64, 90}
};
static const int silverScaleCount = sizeof(silverScaleTable) / sizeof(silverScaleTable[0]);

// Look up scaled font size for Silver; falls back to ×1.38 for unlisted sizes.
int silverScaledSize(int size) {
  for (int i = 0; i < silverScaleCount; i++) {
    if (silverScaleTable[i].nominal == size)
      return silverScaleTable[i].scaled;
  }
  return (int)(size * 1.38f + 0.5f);
}

// Reverse map: given an actual rendered size, find the equivalent nominal size.
// e.g. 49→36, 61→44, 72→52. Falls back to ÷1.38 for unlisted sizes.
int silverNominalSize(int scaledSize) {
  for (int i = 0; i < silverScaleCount; i++) {
    if (silverScaleTable[i].scaled == scaledSize)
      return silverScaleTable[i].nominal;
  }
  return (int)(scaledSize / 1.38f + 0.5f);
}

// ==================== Mid-Render Touch Detection ====================
// Poll touch hardware during long rendering operations.
// If a nav button (return, prev, next) is touched, store coordinates
// and return true so the draw function can exit early.
bool checkNavTouch() {
  if (pendingNavTouch) return true;  // Already have a pending touch
  
  // Debounce: ignore touches within 300ms of last processed touch
  // Prevents ghost touches from e-ink refresh noise
  if (millis() - lastTouchProcessedTime < 300) return false;
  
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    int x = t.x;
    int y = t.y;
    // Check if touch is in the nav bar area
    if (y >= NAV_TOUCH_Y_MIN && y <= NAV_TOUCH_Y_MAX) {
      if (touchedReturnButton(x, y) || touchedPrevPage(x, y) || touchedNextPage(x, y)) {
        pendingNavTouch = true;
        pendingTouchX = x;
        pendingTouchY = y;
        Serial.printf("Nav touch detected mid-render: %d, %d\n", x, y);
        return true;
      }
    }
  }
  return false;
}

// Draw a PNG icon at specified position
// Priority: SD card (if enabled) -> embedded in firmware
bool drawNavIcon(const char* iconName, int x, int y) {
  // Try SD card first (user can override embedded icons)
  if (useSDCardIcons && sdCardAvailable) {
    char iconPath[48];
    snprintf(iconPath, sizeof(iconPath), "/icons/%s", iconName);
    
    File iconFile;
    if (sdMutex != NULL) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      iconFile = SD.open(iconPath);
      xSemaphoreGive(sdMutex);
    } else {
      iconFile = SD.open(iconPath);
    }
    
    if (iconFile) {
      size_t fileSize = iconFile.size();
      uint8_t* buffer = (uint8_t*)malloc(fileSize);
      if (buffer) {
        size_t bytesRead = iconFile.read(buffer, fileSize);
        iconFile.close();
        if (bytesRead == fileSize) {
          M5.Display.drawPng(buffer, fileSize, x, y);
          free(buffer);
          return true;
        }
        free(buffer);
      } else {
        iconFile.close();
      }
    }
  }
  
  // Fall back to embedded icon
  const EmbeddedIcon* icon = findEmbeddedIcon(iconName);
  if (icon) {
    M5.Display.drawPng(icon->data, icon->length, x, y);
    return true;
  }
  
  return false;
}

// Draw return button at lower-right corner using return.png icon
void drawReturnButton() {
  drawNavIcon("return.png", NAV_RETURN_X, NAV_Y);
}

// Draw page navigation buttons at lower-left corner using back.png/next.png icons
// Universal: left (←) = next/forward, right (→) = prev/backward
void drawPageButtons(bool showPrev, bool showNext) {
  if (showNext) {
    drawNavIcon("back.png", NAV_PREV_X, NAV_Y);   // ← at left = next
  }
  if (showPrev) {
    drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);   // → at right = prev
  }
}

// Draw full navigation bar: page buttons (lower-left) + return button (lower-right)
// For screens with pagination
void drawNavBar(bool showPrev, bool showNext) {
  drawPageButtons(showPrev, showNext);
  drawReturnButton();
}

// Vertical CJK nav bar: swap next/prev icons for right-to-left page flow
// Left button = next (forward), Right button = prev (backward)
// Arrow directions: ← left arrow = next (continue left), → right arrow = prev (go back right)
void drawVerticalNavBar(bool hasPrev, bool hasNext) {
  if (hasNext) drawNavIcon("back.png", NAV_PREV_X, NAV_Y);  // ← left arrow = next page (CJK forward)
  if (hasPrev) drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);  // → right arrow = prev page (CJK backward)
  drawReturnButton();
}

// Touch detection helpers for nav bar
bool touchedReturnButton(int x, int y) {
  return (x >= NAV_RETURN_X - 10 && x <= NAV_RETURN_X + NAV_ICON_SIZE + 10 &&
          y >= NAV_TOUCH_Y_MIN && y <= NAV_TOUCH_Y_MAX);
}

bool touchedPrevPage(int x, int y) {
  return (x >= NAV_PREV_X - 5 && x <= NAV_PREV_X + NAV_ICON_SIZE + 5 &&
          y >= NAV_TOUCH_Y_MIN && y <= NAV_TOUCH_Y_MAX);
}

bool touchedNextPage(int x, int y) {
  return (x >= NAV_NEXT_X - 5 && x <= NAV_NEXT_X + NAV_ICON_SIZE + 5 &&
          y >= NAV_TOUCH_Y_MIN && y <= NAV_TOUCH_Y_MAX);
}

// ==================== End Navigation Bar ====================

// Universal status bar: time (top-left) + battery icon & % (top-right)
// Always uses built-in font for consistent appearance regardless of OFR state.
void drawStatusBar() {
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);

  // Time (top-left)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.drawString(timeStr, 8, 4);
    M5.Display.setTextSize(1);
  }

  // Battery (top-right)
  int batLevel = M5.Power.getBatteryLevel();  // 0-100
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging);

  int w = M5.Display.width();
  int bx = w - 46, by = 12, bw = 34, bh = 18;

  // Icon outline + tip
  M5.Display.drawRect(bx, by, bw, bh, TFT_BLACK);
  M5.Display.drawRect(bx + 1, by + 1, bw - 2, bh - 2, TFT_BLACK);
  M5.Display.fillRect(bx + bw, by + 4, 4, bh - 8, TFT_BLACK);

  // Fill level
  int fillW = (bw - 6) * batLevel / 100;
  if (fillW > 0) M5.Display.fillRect(bx + 3, by + 3, fillW, bh - 6, TFT_BLACK);

  // Percentage text
  char batStr[8];
  snprintf(batStr, sizeof(batStr), "%d%%", batLevel);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.drawString(batStr, bx - 8, 4);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(TL_DATUM);

  // Charging "+" indicator
  if (charging) {
    int cx = bx - 4, cy = by + bh / 2;
    M5.Display.fillRect(cx - 4, cy - 1, 8, 3, TFT_BLACK);
    M5.Display.fillRect(cx - 1, cy - 4, 3, 8, TFT_BLACK);
  }
}

// Draw a pre-rendered label bitmap at (x, y) with color support.
// The bitmap is 4-bit packed: high nibble = left pixel, low nibble = right pixel.
// Source values: 0=black (text), 15=white (background).
// The sprite palette maps foreground pixels to `color` and background to `bg`,
// so white-on-black rendering is handled by palette, not by flipping thresholds.
static void drawLabelBitmap(const LabelBitmap* label, int x, int y, uint16_t color, uint16_t bg) {
  uint16_t w = label->w;
  uint16_t h = label->h;
  const uint8_t* bmp = label->bitmap;
  // E-ink threshold: snap grayscale to pure black/white (no gray outline)
  // Bitmap values: 0=text(darkest), 15=background(lightest). Values 0-7 are foreground.
  // The sprite palette handles color swapping (palette[0]=bg, palette[1]=color).
  const uint8_t threshold = 8;  // 0-7 = foreground, 8-15 = background/transparent
  
  // Use a sprite for efficient rendering
  LGFX_Sprite sprite(&M5.Display);
  sprite.setColorDepth(1);  // 1-bit for crisp e-ink rendering
  if (!sprite.createSprite(w, h)) {
    // Not enough memory for sprite — fall back to pixel-by-pixel
    int idx = 0;
    for (int py = 0; py < h; py++) {
      for (int px = 0; px < w; px += 2) {
        uint8_t packed = pgm_read_byte(&bmp[idx++]);
        uint8_t hi = (packed >> 4) & 0x0F;
        uint8_t lo = packed & 0x0F;
        if (hi < 14) {
          bool isFg = (hi < threshold);
          if (isFg) M5.Display.drawPixel(x + px, y + py, color);
          else M5.Display.drawPixel(x + px, y + py, bg);
        }
        if (px + 1 < w && lo < 14) {
          bool isFg = (lo < threshold);
          if (isFg) M5.Display.drawPixel(x + px + 1, y + py, color);
          else M5.Display.drawPixel(x + px + 1, y + py, bg);
        }
      }
    }
    return;
  }
  
  sprite.setPaletteColor(0, bg);
  sprite.setPaletteColor(1, color);
  sprite.fillSprite(0);  // fill with background
  
  // Draw pixels into sprite (binary: foreground or background)
  int idx = 0;
  for (int py = 0; py < h; py++) {
    for (int px = 0; px < w; px += 2) {
      uint8_t packed = pgm_read_byte(&bmp[idx++]);
      uint8_t hi = (packed >> 4) & 0x0F;
      uint8_t lo = packed & 0x0F;
      if (hi < 14) {
        if (hi < threshold) sprite.drawPixel(px, py, 1);
      }
      if (px + 1 < w && lo < 14) {
        if (lo < threshold) sprite.drawPixel(px + 1, py, 1);
      }
    }
  }
  
  sprite.pushSprite(x, y);
  sprite.deleteSprite();
}

// Search SD-card labels (Unifont mode). Returns matching entry or nullptr.
static const SDLabelEntry* findSDLabel(const char* text, uint16_t fontSize) {
  if (sdLabelCount > 0 && sdLabelEntries) {
    for (int i = 0; i < sdLabelCount; i++) {
      if (sdLabelEntries[i].fontSize == fontSize &&
          strcmp(sdLabelEntries[i].text, text) == 0) {
        return &sdLabelEntries[i];
      }
    }
  }
  return nullptr;
}

// Draw an SD label bitmap (same layout as LabelBitmap; pgm_read_byte works on
// regular RAM on ESP32).
static void drawSDLabel(const SDLabelEntry* e, int x, int y, uint16_t color, uint16_t bg) {
  // Reinterpret as LabelBitmap — identical struct layout
  drawLabelBitmap(reinterpret_cast<const LabelBitmap*>(e), x, y, color, bg);
}

// Helper: draw Chinese/mixed text using system TTF font (OFR) if loaded, else built-in.
// Returns the pixel width of the drawn text (for chaining multiple draws).
int drawSystemText(const char* text, int x, int y, int size, uint16_t color, uint16_t bg) {
  // Check SD-card labels first (Unifont mode overrides PROGMEM labels)
  const SDLabelEntry* sdLabel = findSDLabel(text, size);
  if (sdLabel) {
    int yOff = (sdLabel->h < (uint16_t)size) ? (size - sdLabel->h) : 0;
    drawSDLabel(sdLabel, x, y + yOff, color, bg);
    return sdLabel->w;
  }
  // Check for pre-rendered PROGMEM labels (only for default GenYoMinTW font)
  if (systemFontChoice == 0) {
    const LabelBitmap* label = findLabelBitmap(text, size);
    if (label) {
      int yOff = (label->h < (uint16_t)size) ? (size - label->h) : 0;
      drawLabelBitmap(label, x, y + yOff, color, bg);
      return label->w;
    }
  }
  // Check for pre-rendered Silver PROGMEM labels
  if (systemFontChoice == 1) {
    const LabelBitmap* label = findSilverLabelBitmap(text, size);
    if (label) {
      int yOff = (label->h < (uint16_t)size) ? (size - label->h) : 0;
      drawLabelBitmap(label, x, y + yOff, color, bg);
      return label->w;
    }
  }
  // Ensure system font is active (not reading font)
  if (ofrFontLoaded && systemFontFile.length() > 0 && currentFontFile != systemFontFile) {
    loadSystemFont();
  }
  if (ofrFontLoaded) {
    int renderSize = (systemFontChoice == 1) ? silverScaledSize(size) : size;
    ofr.setFontSize(renderSize);
    ofr.setFontColor(color, bg);
    return (int)ofr.drawString(text, x, y, color, bg);
  } else {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(color);
    M5.Display.setCursor(x, y);
    M5.Display.print(text);
    return M5.Display.textWidth(text);
  }
}

// Helper: measure text width using the same lookup chain as drawSystemText
int getSystemTextWidth(const char* text, int size) {
  const SDLabelEntry* sdLabel = findSDLabel(text, size);
  if (sdLabel) return sdLabel->w;
  if (systemFontChoice == 0) {
    const LabelBitmap* label = findLabelBitmap(text, size);
    if (label) return label->w;
  }
  if (systemFontChoice == 1) {
    const LabelBitmap* label = findSilverLabelBitmap(text, size);
    if (label) return label->w;
  }
  if (ofrFontLoaded) {
    int renderSize = (systemFontChoice == 1) ? silverScaledSize(size) : size;
    ofr.setFontSize(renderSize);
    return (int)ofr.getTextWidth(text);
  }
  return 0;
}

// Helper: draw centered text using system TTF font
void drawSystemTextCentered(const char* text, int centerX, int y, int size, uint16_t color, uint16_t bg) {
  // Check SD-card labels first (Unifont mode overrides PROGMEM labels)
  const SDLabelEntry* sdLabel = findSDLabel(text, size);
  if (sdLabel) {
    int x = centerX - sdLabel->w / 2;
    drawSDLabel(sdLabel, x, y, color, bg);
    return;
  }
  // Check for pre-rendered PROGMEM labels (only for default GenYoMinTW font)
  if (systemFontChoice == 0) {
    const LabelBitmap* label = findLabelBitmap(text, size);
    if (label) {
      int x = centerX - label->w / 2;
      drawLabelBitmap(label, x, y, color, bg);
      return;
    }
  }
  // Check for pre-rendered Silver PROGMEM labels
  if (systemFontChoice == 1) {
    const LabelBitmap* label = findSilverLabelBitmap(text, size);
    if (label) {
      int x = centerX - label->w / 2;
      drawLabelBitmap(label, x, y, color, bg);
      return;
    }
  }
  // Ensure system font is active (not reading font)
  if (ofrFontLoaded && systemFontFile.length() > 0 && currentFontFile != systemFontFile) {
    loadSystemFont();
  }
  if (ofrFontLoaded) {
    int renderSize = (systemFontChoice == 1) ? silverScaledSize(size) : size;
    ofr.setFontSize(renderSize);
    ofr.setFontColor(color, bg);
    ofr.cdrawString(text, centerX, y, color, bg);
  } else {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(color);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.drawString(text, centerX, y);
    M5.Display.setTextDatum(TL_DATUM);
  }
}

// Progress bar for book loading — call with 0..100
void updateLoadProgress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const int barX = 70, barW = 400, barH = 30;
  int barY = DISPLAY_HEIGHT / 2 + 50;
  int border = 3;
  int fillableW = barW - border * 2;
  int fillW = (fillableW * percent) / 100;
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  M5.Display.fillRect(barX + border, barY + border,
                      fillW, barH - border * 2, TFT_BLACK);
  M5.Display.display();
  yield();
}
