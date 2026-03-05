#include "globals.h"
#include "embedded_icons.h"
#include "labels/label_bitmaps.h"

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

// Draw battery icon + percentage at top-right corner
void drawBatteryIndicator() {
  int batLevel = M5.Power.getBatteryLevel();  // 0-100
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging);
  
  int w = M5.Display.width();
  int bx = w - 46;  // battery icon x
  int by = 6;       // battery icon y
  int bw = 34;      // battery body width
  int bh = 18;      // battery body height
  
  // Battery body outline
  M5.Display.drawRect(bx, by, bw, bh, TFT_BLACK);
  M5.Display.drawRect(bx + 1, by + 1, bw - 2, bh - 2, TFT_BLACK);  // thicker outline
  // Battery tip (positive terminal)
  M5.Display.fillRect(bx + bw, by + 4, 4, bh - 8, TFT_BLACK);
  
  // Fill level
  int fillW = (bw - 6) * batLevel / 100;
  if (fillW > 0) {
    M5.Display.fillRect(bx + 3, by + 3, fillW, bh - 6, TFT_BLACK);
  }
  
  // Percentage text — always at fixed position, 24px gap left of battery
  char batStr[8];
  snprintf(batStr, sizeof(batStr), "%d%%", batLevel);
  int textRightEdge = bx - 42;
  if (ofrFontLoaded) {
    ofr.setFontSize(18);
    ofr.setFontColor(TFT_BLACK, TFT_WHITE);
    int tw = ofr.getTextWidth(batStr);
    ofr.drawString(batStr, textRightEdge - tw, by + 1, TFT_BLACK, TFT_WHITE);
  } else {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.5);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(TR_DATUM);
    M5.Display.drawString(batStr, textRightEdge, by);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextSize(1);
  }
  
  // Charging indicator: bold "+" in the gap between text and battery
  if (charging) {
    int cx = bx - 10;  // closer to battery icon
    int cy = by + bh / 2;
    // Draw a thick plus sign
    M5.Display.fillRect(cx - 5, cy - 1, 10, 3, TFT_BLACK);  // horizontal bar
    M5.Display.fillRect(cx - 1, cy - 5, 3, 10, TFT_BLACK);  // vertical bar
  }
}

// Draw current time in the top-left corner
void drawCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  char timeStr[8];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  if (ofrFontLoaded) {
    ofr.setFontSize(20);
    ofr.setFontColor(TFT_BLACK, TFT_WHITE);
    ofr.drawString(timeStr, 8, 6, TFT_BLACK, TFT_WHITE);
  } else {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.5);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.drawString(timeStr, 8, 6);
    M5.Display.setTextSize(1);
  }
}

// Universal status bar: time (top-left) + battery % (top-right)
// Call this on every screen except the welcome page
void drawStatusBar() {
  drawCurrentTime();
  drawBatteryIndicator();
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

// Helper: draw Chinese/mixed text using system TTF font (OFR) if loaded, else built-in.
// Returns the pixel width of the drawn text (for chaining multiple draws).
int drawSystemText(const char* text, int x, int y, int size, uint16_t color, uint16_t bg) {
  // Check for pre-rendered label bitmap first (fastest path)
  const LabelBitmap* label = findLabelBitmap(text, size);
  if (label) {
    drawLabelBitmap(label, x, y, color, bg);
    return label->w;
  }
  // Ensure system font is active (not reading font)
  if (ofrFontLoaded && systemFontFile.length() > 0 && currentFontFile != systemFontFile) {
    loadSystemFont();
  }
  if (ofrFontLoaded) {
    ofr.setFontSize(size);
    ofr.setFontColor(color, bg);
    return (int)ofr.drawString(text, x, y, color, bg);
  } else {
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextColor(color);
    M5.Display.setCursor(x, y);
    M5.Display.print(text);
    return M5.Display.textWidth(text);
  }
}

// Helper: draw centered text using system TTF font
void drawSystemTextCentered(const char* text, int centerX, int y, int size, uint16_t color, uint16_t bg) {
  // Check for pre-rendered label bitmap first (fastest path)
  const LabelBitmap* label = findLabelBitmap(text, size);
  if (label) {
    int x = centerX - label->w / 2;
    drawLabelBitmap(label, x, y, color, bg);
    return;
  }
  // Ensure system font is active (not reading font)
  if (ofrFontLoaded && systemFontFile.length() > 0 && currentFontFile != systemFontFile) {
    loadSystemFont();
  }
  if (ofrFontLoaded) {
    ofr.setFontSize(size);
    ofr.setFontColor(color, bg);
    ofr.cdrawString(text, centerX, y, color, bg);
  } else {
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextColor(color);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.drawString(text, centerX, y);
    M5.Display.setTextDatum(TL_DATUM);
  }
}
