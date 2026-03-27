#include "globals.h"
#include <esp_task_wdt.h>

void drawFontMenu() {
  Serial.println("Drawing font menu...");
  
  // Scan SD card for fonts if not done yet
  if (fontFileCount == 0) {
    scanFontFiles();
  }
  numFonts = fontFileCount;  // Update for touch handler
  
  // Build filtered font list based on mode
  bool englishMode = (fontMenuReturnMode == MODE_READING && epubIsHorizontal);
  Serial.printf("Font menu: englishMode=%d, fontMenuReturnMode=%d, epubIsHorizontal=%d, fontFileCount=%d\n",
                englishMode, fontMenuReturnMode, epubIsHorizontal, fontFileCount);
  fontMenuFilteredCount = 0;
  if (englishMode) {
    for (int i = 0; i < fontFileCount; i++) {
      if (!fontIsCJK[i]) {
        fontMenuFilteredMap[fontMenuFilteredCount++] = i;
      }
    }
  } else {
    for (int i = 0; i < fontFileCount; i++) {
      if (fontIsCJK[i]) {
        fontMenuFilteredMap[fontMenuFilteredCount++] = i;
      }
    }
  }
  Serial.printf("Font menu: filteredCount=%d\n", fontMenuFilteredCount);
  int visibleCount = fontMenuFilteredCount;
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Status bar + nav bar first
  drawStatusBar();
  {
    int totalPages = (visibleCount + FONTS_PER_PAGE - 1) / FONTS_PER_PAGE;
    bool showPrev = (fontMenuPage > 0);
    bool showNext = (fontMenuPage < totalPages - 1);
    drawNavBar(showPrev, showNext);
  }
  
  delay(100);
  
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);
  
  if (englishMode)
    drawSystemText("Select Font", 20, 42, 40);
  else
    drawSystemText("選擇閱讀字型", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // Pagination
  int totalPages = (visibleCount + FONTS_PER_PAGE - 1) / FONTS_PER_PAGE;
  if (fontMenuPage >= totalPages) fontMenuPage = totalPages - 1;
  if (fontMenuPage < 0) fontMenuPage = 0;
  int startSlot = fontMenuPage * FONTS_PER_PAGE;
  int endSlot = min(startSlot + FONTS_PER_PAGE, visibleCount);
  
  // Layout: each item = [separator] [display name] [filename + BIN button] [sample text]
  // Item layout (ITEM_HEIGHT = 100px):
  //   0:      separator line
  //   5..27:  display name (22px, prominent) + ✓ if selected
  //   30..44: filename (14px, secondary) + [BIN] button if paired bin exists
  //   50..95: sample text (rendered in TTF, ~32px)
  const int ITEM_HEIGHT = 100;
  const int LIST_TOP = 90;
  const int BIN_BTN_H = 36;

  // ===== Pass 1: Draw all labels/buttons with system font (no font switching) =====
  // Pre-compute preview info for pass 2
  struct PreviewInfo {
    int y;
    int fontIdx;
    bool isSelected;
    bool isStandaloneBin;
    String fontFile;       // file to load for preview
    bool isBinPreview;     // true = BIN font, false = TTF/ETBook
  };
  PreviewInfo previews[7];  // max FONTS_PER_PAGE
  int previewCount = 0;

  for (int slot = startSlot; slot < endSlot; slot++) {
    int i = fontMenuFilteredMap[slot];  // Real font index
    yield(); esp_task_wdt_reset();
    if (checkNavTouch()) {
      Serial.println("Nav touch during font menu render - aborting");
      return;
    }
    
    int y = LIST_TOP + ((slot - startSlot) * ITEM_HEIGHT);

    // Separator line at top
    M5.Display.drawLine(30, y, 510, y, TFT_LIGHTGRAY);
    
    if (i == selectedFontIndex) {
      M5.Display.fillRect(30, y + 2, 480, ITEM_HEIGHT - 2, TFT_LIGHTGRAY);
    }
    
    String displayName = fontDisplayNames[i];
    String fileName = fontFileList[i];
    
    if (englishMode) {
      String nameInfo = displayName;
      if (i == selectedFontIndex) nameInfo += " \u2713";
      drawSystemText(nameInfo.c_str(), 50, y + 5, 28);
      // Store preview info for pass 2
      previews[previewCount] = {y, i, (i == selectedFontIndex), false, fileName, false};
      previewCount++;
    } else {
      // Chinese mode: full layout with filename, BIN/TTF buttons
      bool hasBinPair = (fontBinCount[i] > 0);
      int selectedBinIdx = -1;
      for (int b = 0; b < fontBinCount[i]; b++) {
        if (i == selectedFontIndex && readingFontFile == fontBinFiles[i][b]) {
          selectedBinIdx = b;
          break;
        }
      }
    
      String nameInfo = displayName;
      if (i == selectedFontIndex) nameInfo += " \u2713";
      drawSystemText(nameInfo.c_str(), 50, y + 5, 28);
    
      bool isStandaloneBin = (fileName.endsWith(".bin") || fileName.endsWith(".BIN"));
      String typeLabel = isStandaloneBin ? "\u25A3 " : "\u24C9 ";
      String fileInfo = typeLabel + fileName;
      drawSystemText(fileInfo.c_str(), 50, y + 30, 14, TFT_DARKGRAY);
    
      bool selectedIsTTF = (i == selectedFontIndex && !isStandaloneBin &&
                            readingFontFile == fontFileList[i]);
      if (hasBinPair || !isStandaloneBin) {
        int btnY = y + 25;
        int btnX = 510;
      
        if (!isStandaloneBin) {
          String ttfLabel = "TTF";
          int ttfBtnW = ttfLabel.length() * 12 + 16;
          btnX -= (ttfBtnW + 4);
          if (selectedIsTTF) {
            M5.Display.fillRoundRect(btnX, btnY, ttfBtnW, BIN_BTN_H, 6, TFT_BLACK);
            drawSystemText(ttfLabel.c_str(), btnX + 8, btnY + 8, 18, TFT_WHITE, TFT_BLACK);
          } else {
            M5.Display.drawRoundRect(btnX, btnY, ttfBtnW, BIN_BTN_H, 6, TFT_DARKGRAY);
            M5.Display.drawRoundRect(btnX + 1, btnY + 1, ttfBtnW - 2, BIN_BTN_H - 2, 5, TFT_DARKGRAY);
            drawSystemText(ttfLabel.c_str(), btnX + 8, btnY + 8, 18, TFT_DARKGRAY);
          }
        }
      
        if (hasBinPair) {
          bool isSilverFont = (fileName.indexOf("Silver") >= 0 || fileName.indexOf("silver") >= 0);
          for (int b = fontBinCount[i] - 1; b >= 0; b--) {
            int displaySize = isSilverFont ? silverNominalSize(fontBinSizes[i][b]) : (int)fontBinSizes[i][b];
            String label = String(displaySize);
            int btnW = label.length() * 12 + 16;
            btnX -= (btnW + 4);
            if (b == selectedBinIdx) {
              M5.Display.fillRoundRect(btnX, btnY, btnW, BIN_BTN_H, 6, TFT_BLACK);
              drawSystemTextCentered(label.c_str(), btnX + btnW / 2, btnY + (BIN_BTN_H - 18) / 2, 18, TFT_WHITE, TFT_BLACK);
            } else {
              M5.Display.drawRoundRect(btnX, btnY, btnW, BIN_BTN_H, 6, TFT_DARKGRAY);
              M5.Display.drawRoundRect(btnX + 1, btnY + 1, btnW - 2, BIN_BTN_H - 2, 5, TFT_DARKGRAY);
              drawSystemTextCentered(label.c_str(), btnX + btnW / 2, btnY + (BIN_BTN_H - 18) / 2, 18, TFT_DARKGRAY);
            }
          }
        }
      }

      // Determine which file to use for sample preview
      String previewFile = fileName;
      bool isBin = isStandaloneBin;
      if (isStandaloneBin) {
        if (i == selectedFontIndex && selectedBinIdx >= 0) {
          previewFile = fontBinFiles[i][selectedBinIdx];
        }
      }
      previews[previewCount] = {y, i, (i == selectedFontIndex), isStandaloneBin, previewFile, isBin};
      previewCount++;
    }
  }

  // ===== Pass 2: Draw font previews (no system font restores between items) =====
  // Ensure PSRAM-heavy buffers are freed before preview pass (each preview
  // loads/unloads a font with large alloc/free cycles).
  if (currentBookIsEpub && epubFullText) {
    free(epubFullText);
    epubFullText = nullptr;
    epubFullTextLen = 0;
  }
  freeGlyphCache();
  M5.Display.setAutoDisplay(false);
  M5.Display.endWrite();
  String sampleLineCJK = "\xE7\xAF\x84\xE4\xBE\x8B\xEF\xBC\x9A\xE3\x80\x8C\xE9\x80\x99\xE6\x97\xA5\xEF\xBC\x8C\xE3\x80\x82\xE3\x80\x8D";
  String sampleLineEn = "The quick brown fox jumps over";

  for (int p = 0; p < previewCount; p++) {
    yield(); esp_task_wdt_reset();
    auto &pv = previews[p];
    int sampleTop = pv.y + (englishMode ? 40 : 50);
    int sampleH = 32;
    uint16_t bg = pv.isSelected ? TFT_LIGHTGRAY : TFT_WHITE;
    const String &sampleLine = englishMode ? sampleLineEn : sampleLineCJK;

    if (pv.isBinPreview) {
      // BIN font preview — Silver fonts need scaled size to match visual height
      unloadBinaryFont();
      bool binLoaded = loadBinaryFont(pv.fontFile.c_str());
      if (binLoaded) {
        bool isSilver = (pv.fontFile.indexOf("Silver") >= 0 || pv.fontFile.indexOf("silver") >= 0);
        int targetH = isSilver ? silverScaledSize(sampleH) : sampleH;
        float scale = (float)targetH / g_binFont.fontSize;
        drawBinFontStringScaled(sampleLine, 50, sampleTop, scale, true);
      }
    } else if (pv.fontFile == "ETBook-embedded") {
      loadEmbeddedETBook(32);
      ofr.setFontColor(TFT_BLACK, bg);
      ofr.drawString(sampleLine.c_str(), 50, sampleTop);
    } else {
      // TTF preview — loadTTFFont internally unloads previous font
      bool loaded = loadTTFFont(pv.fontFile.c_str(), 32);
      if (loaded && ofrFontLoaded) {
        ofr.setFontColor(TFT_BLACK, bg);
        ofr.drawString(sampleLine.c_str(), 50, sampleTop);
      }
    }
  }

  // Restore system font (thorough cleanup)
  unloadBinaryFont();
  resetToSystemFont();
  M5.Display.startWrite();
  
  // Status at bottom — two lines, larger text
  if (englishMode) {
    String rdLine = "Font: ";
    if (readingFontIndex >= 0 && readingFontIndex < fontFileCount)
      rdLine += fontDisplayNames[readingFontIndex];
    else
      rdLine += "Default";
    drawSystemText(rdLine.c_str(), 20, 820, 22);
  } else {
    String sysLine = "系統：" + (systemFontFile.length() > 0 ? systemFontFile : String("內建"));
    drawSystemText(sysLine.c_str(), 20, 810, 22);
    if (readingFontIndex >= 0 && readingFontIndex < fontFileCount) {
      String rdLine = "閱讀：" + fontDisplayNames[readingFontIndex];
      drawSystemText(rdLine.c_str(), 20, 840, 22);
    }
  }
  
  // Page indicator (universal)
  drawPageIndicator(fontMenuPage + 1, totalPages);
  
  M5.Display.endWrite();
  M5.Display.setAutoDisplay(true);
  Serial.println("Calling display()...");
  if (pendingNavTouch) {
    Serial.println("Skipping display() - nav touch pending");
    return;
  }
  M5.Display.display();
  
  // Flush stale touch state after e-ink quality refresh to prevent phantom
  // touches from locking out subsequent input.
  delay(200);
  for (int i = 0; i < 3; i++) {
    M5.update();
    delay(50);
  }
  Serial.println("Font menu displayed");
}

// Shared keyboard layout helper
void getKeyboardRows(bool symbols, bool shift, const char* outRows[3]) {
  static const char* numRows[3] = {
    "1234567890",
    "-_@.~!#$%",
    "&*()+=:/?"
  };
  static const char* upperRows[3] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM"
  };
  static const char* lowerRows[3] = {
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm"
  };
  if (symbols) {
    outRows[0] = numRows[0]; outRows[1] = numRows[1]; outRows[2] = numRows[2];
  } else if (shift) {
    outRows[0] = upperRows[0]; outRows[1] = upperRows[1]; outRows[2] = upperRows[2];
  } else {
    outRows[0] = lowerRows[0]; outRows[1] = lowerRows[1]; outRows[2] = lowerRows[2];
  }
}

void drawVirtualKeyboard() {
  // Keyboard layout
  const char* rows[3];
  getKeyboardRows(keyboardSymbols, keyboardShift, rows);
  
  int keyW = 48;
  int keyH = 60;
  int keySpacing = 6;
  int startY = 320;
  
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  
  // Draw 3 rows of keys
  for (int row = 0; row < 3; row++) {
    int numKeys = strlen(rows[row]);
    int rowWidth = numKeys * keyW + (numKeys - 1) * keySpacing;
    int startX = (DISPLAY_WIDTH - rowWidth) / 2;
    int y = startY + row * (keyH + keySpacing);
    
    for (int i = 0; i < numKeys; i++) {
      int x = startX + i * (keyW + keySpacing);
      
      // Draw key background
      M5.Display.fillRect(x, y, keyW, keyH, TFT_LIGHTGRAY);
      M5.Display.drawRect(x, y, keyW, keyH, TFT_BLACK);
      
      // Draw letter
      M5.Display.setTextColor(TFT_BLACK);
      char keyChar[2] = {rows[row][i], '\0'};
      int textW = M5.Display.textWidth(keyChar);
      int textH = M5.Display.fontHeight();
      M5.Display.setCursor(x + (keyW - textW) / 2, y + (keyH - textH) / 2);
      M5.Display.print(keyChar);
    }
  }
  
  // Special keys row
  int specialY = startY + 3 * (keyH + keySpacing);
  
  // Shift/123 button
  int shiftW = 80;
  M5.Display.fillRect(20, specialY, shiftW, keyH, keyboardShift || keyboardSymbols ? TFT_DARKGRAY : TFT_LIGHTGRAY);
  M5.Display.drawRect(20, specialY, shiftW, keyH, TFT_BLACK);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(30, specialY + 20);
  M5.Display.print(keyboardSymbols ? "ABC" : "123");
  
  // Space button
  int spaceX = 110;
  int spaceW = 200;
  M5.Display.fillRect(spaceX, specialY, spaceW, keyH, TFT_LIGHTGRAY);
  M5.Display.drawRect(spaceX, specialY, spaceW, keyH, TFT_BLACK);
  M5.Display.setCursor(spaceX + 70, specialY + 20);
  M5.Display.print("Space");
  
  // Backspace button
  int backW = 100;
  int backX = 320;
  M5.Display.fillRect(backX, specialY, backW, keyH, TFT_LIGHTGRAY);
  M5.Display.drawRect(backX, specialY, backW, keyH, TFT_BLACK);
  M5.Display.setCursor(backX + 20, specialY + 20);
  M5.Display.print("<-Del");
  
  // Shift toggle (if not in symbols mode)
  if (!keyboardSymbols) {
    int shiftToggleX = 430;
    int shiftToggleW = 90;
    M5.Display.fillRect(shiftToggleX, specialY, shiftToggleW, keyH, keyboardShift ? TFT_DARKGRAY : TFT_LIGHTGRAY);
    M5.Display.drawRect(shiftToggleX, specialY, shiftToggleW, keyH, TFT_BLACK);
    M5.Display.setCursor(shiftToggleX + 15, specialY + 20);
    M5.Display.print("Shift");
  }
}

void updatePasswordDisplay() {
  // Only update the password field area without refreshing entire screen
  M5.Display.setEpdMode(epd_mode_t::epd_fast);  // Fast refresh for typing
  M5.Display.startWrite();
  
  // Clear password box
  M5.Display.fillRect(20, 200, 500, 60, TFT_WHITE);
  M5.Display.drawRect(20, 200, 500, 60, TFT_BLACK);
  M5.Display.drawRect(21, 201, 498, 58, TFT_BLACK);
  
  // Show password as dots
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(30, 215);
  
  String maskedPassword = "";
  for (int i = 0; i < passwordInput.length(); i++) {
    maskedPassword += "•";
  }
  M5.Display.print(maskedPassword);
  
  M5.Display.endWrite();
  M5.Display.display();
  M5.Display.setEpdMode(epd_mode_t::epd_quality);  // Back to quality mode
}

void drawClock() {
  Serial.println("Drawing analog clock screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();
  
  // Get current time
  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo);
  String dateStr = getCurrentDateString();
  
  // Center of screen
  int centerX = M5.Display.width() / 2;
  int centerY = M5.Display.height() / 2 - 40;  // Shift up a bit for date
  
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(MC_DATUM);
  
  if (timeConfig.timeSynced && hasTime) {
    // Analog clock face
    int clockRadius = 200;  // Main clock radius
    
    // Draw clock outer circles (decorative)
    M5.Display.drawCircle(centerX, centerY, clockRadius + 8, TFT_BLACK);
    M5.Display.drawCircle(centerX, centerY, clockRadius + 7, TFT_BLACK);
    M5.Display.drawCircle(centerX, centerY, clockRadius + 6, TFT_BLACK);
    M5.Display.fillCircle(centerX, centerY, clockRadius + 5, TFT_LIGHTGRAY);
    M5.Display.drawCircle(centerX, centerY, clockRadius + 5, TFT_BLACK);
    M5.Display.fillCircle(centerX, centerY, clockRadius, TFT_WHITE);
    M5.Display.drawCircle(centerX, centerY, clockRadius, TFT_BLACK);
    
    // Draw hour markers
    for (int i = 0; i < 12; i++) {
      float angle = (i * 30 - 90) * PI / 180;  // -90 to start from 12 o'clock
      
      // Outer point
      int x1 = centerX + (clockRadius - 10) * cos(angle);
      int y1 = centerY + (clockRadius - 10) * sin(angle);
      
      // Inner point (longer for 12, 3, 6, 9)
      int markerLength = (i % 3 == 0) ? 25 : 15;
      int x2 = centerX + (clockRadius - 10 - markerLength) * cos(angle);
      int y2 = centerY + (clockRadius - 10 - markerLength) * sin(angle);
      
      // Thicker lines for 12, 3, 6, 9
      int thickness = (i % 3 == 0) ? 3 : 2;
      for (int t = 0; t < thickness; t++) {
        M5.Display.drawLine(x1 + t, y1, x2 + t, y2, TFT_BLACK);
        M5.Display.drawLine(x1, y1 + t, x2, y2 + t, TFT_BLACK);
      }
    }
    
    // Calculate angles for clock hands
    int hour = timeinfo.tm_hour % 12;
    int minute = timeinfo.tm_min;
    int second = timeinfo.tm_sec;
    
    // Hour hand angle (30 degrees per hour + 0.5 degrees per minute)
    float hourAngle = ((hour * 30) + (minute * 0.5) - 90) * PI / 180;
    
    // Minute hand angle (6 degrees per minute)
    float minuteAngle = ((minute * 6) - 90) * PI / 180;
    
    // Second hand angle (6 degrees per second)
    float secondAngle = ((second * 6) - 90) * PI / 180;
    
    // Draw hour hand (thick, short)
    int hourLength = clockRadius * 0.5;
    int hourX = centerX + hourLength * cos(hourAngle);
    int hourY = centerY + hourLength * sin(hourAngle);
    for (int i = -3; i <= 3; i++) {
      for (int j = -3; j <= 3; j++) {
        M5.Display.drawLine(centerX + i, centerY + j, hourX + i, hourY + j, TFT_BLACK);
      }
    }
    
    // Draw minute hand (medium thickness, longer)
    int minuteLength = clockRadius * 0.75;
    int minuteX = centerX + minuteLength * cos(minuteAngle);
    int minuteY = centerY + minuteLength * sin(minuteAngle);
    for (int i = -2; i <= 2; i++) {
      for (int j = -2; j <= 2; j++) {
        M5.Display.drawLine(centerX + i, centerY + j, minuteX + i, minuteY + j, TFT_BLACK);
      }
    }
    
    // Draw second hand (thin, longest) - optional for e-ink
    int secondLength = clockRadius * 0.85;
    int secondX = centerX + secondLength * cos(secondAngle);
    int secondY = centerY + secondLength * sin(secondAngle);
    M5.Display.drawLine(centerX, centerY, secondX, secondY, TFT_DARKGRAY);
    
    // Center dot
    M5.Display.fillCircle(centerX, centerY, 8, TFT_BLACK);
    M5.Display.fillCircle(centerX, centerY, 5, TFT_WHITE);
    M5.Display.drawCircle(centerX, centerY, 5, TFT_BLACK);
    
    // Date and timezone below clock
    M5.Display.setTextSize(2);
    M5.Display.drawString(dateStr, centerX, centerY + clockRadius + 40);
    
    M5.Display.setTextSize(1);
    const char* weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    M5.Display.drawString(weekdays[timeinfo.tm_wday], centerX, centerY + clockRadius + 75);
    
    M5.Display.setTextSize(1);
    M5.Display.drawString(timezones[selectedTimezone].name, centerX, centerY + clockRadius + 100);
    
    // WiFi status indicator at top-right
    if (WiFi.status() == WL_CONNECTED) {
      M5.Display.setTextDatum(TR_DATUM);
      M5.Display.fillCircle(M5.Display.width() - 30, 30, 8, EPD_DARK_GRAY);
      M5.Display.setTextSize(1);
      M5.Display.drawString("WiFi", M5.Display.width() - 45, 24);
      M5.Display.setTextDatum(MC_DATUM);
    }
    
  } else {
    // No time sync - show message
    M5.Display.setTextSize(3);
    M5.Display.drawString("--:--", centerX, centerY);
    
    M5.Display.setTextSize(1);
    if (WiFi.status() == WL_CONNECTED) {
      M5.Display.drawString("Syncing time...", centerX, centerY + 50);
    } else {
      M5.Display.drawString("No WiFi Connection", centerX, centerY + 50);
      M5.Display.drawString("Go to Settings to connect", centerX, centerY + 80);
    }
  }
  
  // Title at top with background
  M5.Display.setTextDatum(TC_DATUM);
  M5.Display.fillRoundRect(centerX - 60, 10, 120, 50, 8, TFT_LIGHTGRAY);
  M5.Display.drawRoundRect(centerX - 60, 10, 120, 50, 8, TFT_BLACK);
  M5.Display.drawRoundRect(centerX - 59, 11, 118, 48, 7, TFT_BLACK);
  M5.Display.setTextColor(TFT_BLACK);
  drawSystemTextCentered("時鐘", centerX, 28, 28);
  
  // Universal return button (lower-right)
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.endWrite();
  M5.Display.display();
  
  // Update tracking
  if (hasTime) {
    lastClockMinute = timeinfo.tm_min;
  }
  lastClockUpdate = millis();
  
  Serial.println("Analog clock screen displayed");
}

void drawWebServerSetup() {
  Serial.println("Drawing web server setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + return button
  drawStatusBar();
  drawReturnButton();
  
  // Title
  drawSystemText("檔案上傳伺服器", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // Current status
  if (webServerRunning) {
    drawSystemText("狀態： 執行中", 20, 120, 32);
    
    // Display IP address prominently
    M5.Display.drawRect(15, 170, 510, 80, TFT_BLACK);
    M5.Display.drawRect(16, 171, 508, 78, TFT_BLACK);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.5);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(25, 185);
    M5.Display.print("IP: http://");
    M5.Display.print(WiFi.localIP().toString());
    
    drawSystemText("在電腦或手機瀏覽器中輸入上方 IP 位址", 20, 270, 20);
    drawSystemText("截圖: /screen", 20, 330, 24);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(20, 300);
    M5.Display.print("Enter above IP in computer/phone browser");
  } else if (webServerEnabled) {
    drawSystemText("狀態： 已啟用", 20, 120, 32);
    
    if (WiFi.status() == WL_CONNECTED) {
      drawSystemText("正在啟動伺服器...", 20, 200, 28);
    } else {
      drawSystemText("等待 WiFi 連接...", 20, 200, 28);
    }
  } else {
    drawSystemText("狀態： 未啟用", 20, 120, 32);
  }
  
  // Toggle button (full width, matching MSC layout)
  int btnY = 400;
  if (webServerEnabled) {
    M5.Display.fillRect(20, btnY, 500, 90, TFT_BLACK);
    drawSystemTextCentered("關閉伺服器", 270, btnY + 28, 36, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("啟用伺服器", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  }
  
  // Info section — large, filling the lower half
  int infoY = 530;
  M5.Display.drawRect(20, infoY, 500, 370, TFT_BLACK);
  
  drawSystemText("說明", 30, infoY + 15, 32);
  M5.Display.drawLine(20, infoY + 55, 520, infoY + 55, EPD_LIGHT_GRAY);
  
  drawSystemText("啟用後可通過網站上傳檔案", 40, infoY + 75, 24);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(40, infoY + 105);
  M5.Display.print("Upload files via web browser");
  
  drawSystemText("支援的檔案類型", 40, infoY + 145, 28);
  drawSystemText("• 電子書 (books/)", 40, infoY + 185, 24);
  drawSystemText("• 字體 (fonts/)", 40, infoY + 220, 24);
  drawSystemText("• 壁紙 (wallpapers/)", 40, infoY + 255, 24);
  drawSystemText("• 待辦/購物清單", 40, infoY + 290, 24);
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Web server setup displayed");
}

void drawIconSetup() {
  Serial.println("Drawing icon setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + return button
  drawStatusBar();
  drawReturnButton();
  
  // Title
  drawSystemText("圖標來源", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // Current status
  if (useSDCardIcons) {
    drawSystemText("目前： SD 卡優先", 20, 120, 32);
  } else {
    drawSystemText("目前： 內建圖標", 20, 120, 32);
  }
  
  // Toggle button (full width, matching MSC layout)
  int btnY = 400;
  if (useSDCardIcons) {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("切換為 內建圖標", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  } else {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("切換為 SD 卡優先", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  }
  
  // Info section — large, filling the lower half
  int infoY = 530;
  M5.Display.drawRect(20, infoY, 500, 370, TFT_BLACK);
  
  drawSystemText("說明", 30, infoY + 15, 32);
  M5.Display.drawLine(20, infoY + 55, 520, infoY + 55, EPD_LIGHT_GRAY);
  
  drawSystemText("SD 卡優先", 40, infoY + 75, 28);
  drawSystemText("• 先從 /icons/ 讀取", 40, infoY + 115, 24);
  drawSystemText("• 找不到則用內建圖標", 40, infoY + 150, 24);
  drawSystemText("• 可自行更換圖標", 40, infoY + 185, 24);
  
  drawSystemText("內建圖標", 40, infoY + 235, 28);
  drawSystemText("• 直接使用韌體內的圖標", 40, infoY + 275, 24);
  drawSystemText("• 啟動速度較快", 40, infoY + 310, 24);
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Icon setup screen displayed");
}

void drawCalendarSetup() {
  Serial.println("Drawing calendar setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + return button
  drawStatusBar();
  drawReturnButton();
  
  // Title
  drawSystemText("曆法計算", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // Current status
  if (useSxwnlCalendar) {
    drawSystemText("目前： 壽星天文曆", 20, 120, 32);
  } else {
    drawSystemText("目前： Meeus 天文算法", 20, 120, 32);
  }
  
  // Toggle button (full width, matching MSC layout)
  int btnY = 400;
  if (useSxwnlCalendar) {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("切換為 Meeus 天文算法", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  } else {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("切換為 壽星天文曆", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  }
  
  // Info section — large, filling the lower half
  int infoY = 530;
  M5.Display.drawRect(20, infoY, 500, 370, TFT_BLACK);
  
  drawSystemText("說明", 30, infoY + 15, 32);
  M5.Display.drawLine(20, infoY + 55, 520, infoY + 55, EPD_LIGHT_GRAY);
  
  drawSystemText("Meeus 天文算法", 40, infoY + 75, 28);
  drawSystemText("• 精度約 1 分鐘", 40, infoY + 115, 24);
  drawSystemText("• 二分搜索求解太陽黃經", 40, infoY + 150, 24);
  drawSystemText("• 與公佈曆書完全一致", 40, infoY + 185, 24);
  
  drawSystemText("壽星天文曆", 40, infoY + 235, 28);
  drawSystemText("• 許劍偉開源算法", 40, infoY + 275, 24);
  drawSystemText("• 最大誤差約 30 分鐘", 40, infoY + 310, 24);
  drawSystemText("• 計算速度較快", 40, infoY + 345, 24);
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Calendar setup screen displayed");
}

void drawSetupMenu() {
  Serial.println("Drawing setup menu...");
  // Compromise mode: mostly fast refresh, periodic quality refresh to clean ghosting
  setupFastRefreshCount++;
  bool doCleanRefresh = (setupFastRefreshCount % 5 == 0);
  const uint16_t cardTextBg = TFT_LIGHTGRAY;
  M5.Display.setEpdMode(doCleanRefresh ? epd_mode_t::epd_quality : epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();
  
  // Menu items (paged)
  const int totalPages = 2;

  // Title
  drawSystemText("設定", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  {
    char pageBuf[8];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", setupMenuPage + 1, totalPages);
    drawSystemText(pageBuf, 500, 30, 22);
  }
  bool showPrev = (setupMenuPage > 0);
  bool showNext = (setupMenuPage < totalPages - 1);
  drawNavBar(showPrev, showNext);

  int y = 92;
  int itemHeight = 78;
  int itemGap = 8;

  if (setupMenuPage == 0) {
    // Page 1: 8 items
    
    // WiFi Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("WiFi 設定", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (WiFi.status() == WL_CONNECTED) {
      String wifiInfo = "已連接 - " + WiFi.SSID();
      drawSystemText(wifiInfo.c_str(), 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else if (wifiConfig.configured) {
      drawSystemText("已儲存", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未設定", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Timezone Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("時區設定", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    drawSystemText("已設定", 40, y + 50, 22, TFT_BLACK, cardTextBg);

    y += itemHeight + itemGap;

    // USB Mass Storage item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("USB 外接磁碟", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (!sdCardAvailable) {
      drawSystemText("未插入 SD 卡", 40, y + 50, 22, EPD_DARK_GRAY, cardTextBg);
    } else if (usbMSCActive) {
      drawSystemText("執行中 - 裝置停用", 40, y + 50, 22, EPD_DARK_GRAY, cardTextBg);
    } else {
      drawSystemText("未啟用", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Icon Source Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("圖標來源", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (useSDCardIcons) {
      drawSystemText("SD 卡優先（可自訂圖標）", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("內建圖標（速度較快）", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Calendar Calculation Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("曆法計算", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (useSxwnlCalendar) {
      drawSystemText("壽星天文曆（許劍偉）", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("Meeus 天文算法（精度較高）", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Bluetooth Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("藍牙", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (bluetoothActive) {
      drawSystemText("已啟用", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未啟用", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Auto-Sleep Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("自動休眠", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (autoSleepEnabled) {
      drawSystemText("已啟用 - 10分鐘無操作自動休眠", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未啟用 - 保持開啟", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Comic Zoom Mode item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("漫畫縮放模式", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (comicZoomMode == 1) {
      drawSystemText("自由定位 - 點擊處為中心", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("四分區 - 點擊顯示該象限", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }
  } else if (setupMenuPage == 1) {
    // Page 2: 5 items

    // Web Server Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("檔案上傳伺服器", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (webServerRunning) {
      String wsInfo = "執行中 - " + WiFi.localIP().toString();
      drawSystemText(wsInfo.c_str(), 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else if (webServerEnabled) {
      drawSystemText("已啟用 - 等待連接", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未啟用", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Page Refresh Mode item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("翻頁刷新模式", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (pageRefreshMode == 1) {
      drawSystemText("每頁全刷新", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else if (pageRefreshMode == 2) {
      drawSystemText("每10頁全刷新", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("系統預設", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // System Font Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("系統字體", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (systemFontChoice == 0) {
      drawSystemText("源樣明體 GenYoMinTW", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("Silver（像素風格字體）", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // Paragraph Indent Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("段落縮進", 40, y + 12, 32, TFT_BLACK, cardTextBg);

    if (paragraphIndent) {
      drawSystemText("首行縮進（兩個全形空格）", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("首行不縮進", 40, y + 50, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + itemGap;

    // About item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("關於", 40, y + 12, 32, TFT_BLACK, cardTextBg);
    drawSystemText("版本資訊與裝置狀態", 40, y + 50, 22, TFT_BLACK, cardTextBg);
  }
  
  // Universal return button (lower-right)
  drawReturnButton();
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Setup menu displayed");
}

void drawWiFiSetup() {
  Serial.println("Drawing WiFi setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar first
  drawStatusBar();
  
  // Draw title
  drawSystemText("\xe7\x84\xa1\xe7\xb7\x9a\xe7\xb6\xb2\xe7\xb5\xa1\xe8\xa8\xad\xe5\xae\x9a", 20, 42, 40);  // 無線網絡設定
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // If showing timezone selection — draw as its own page
  if (showingTimezone) {
    // Clear and redraw as independent page
    M5.Display.fillScreen(TFT_WHITE);
    drawStatusBar();

    // Title + top separator (single line)
    drawSystemText("\xe6\x99\x82\xe5\x8d\x80\xe8\xa8\xad\xe5\xae\x9a", 20, 42, 40);  // 時區設定
    M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
    
    // Show timezone list with paging
    int rowH = 62;
    int listTop = 100;
    int listBottom = 830;
    int maxVisible = (listBottom - listTop) / rowH;  // ~11 items visible
    int totalPages = (timezoneCount + maxVisible - 1) / maxVisible;
    int currentPage = tzScrollOffset / maxVisible + 1;

    // Clamp scroll offset to page boundaries
    if (tzScrollOffset < 0) tzScrollOffset = 0;
    if (tzScrollOffset > timezoneCount - maxVisible) tzScrollOffset = timezoneCount - maxVisible;
    if (tzScrollOffset < 0) tzScrollOffset = 0;

    int y = listTop;
    for (int i = tzScrollOffset; i < timezoneCount && y + rowH <= listBottom; i++) {
      // Highlight selected timezone
      if (i == selectedTimezone) {
        M5.Display.fillRoundRect(15, y, 510, rowH - 4, 6, TFT_LIGHTGRAY);
      }

      M5.Display.setTextColor(TFT_BLACK);
      drawSystemText(timezones[i].name, 30, y + 10, 30);

      // Draw separator
      M5.Display.drawLine(20, y + rowH - 3, 520, y + rowH - 3, TFT_LIGHTGRAY);

      y += rowH;
    }

    // 保存 button above lower separator
    M5.Display.fillRoundRect(20, 832, 100, 44, 8, TFT_BLACK);
    drawSystemTextCentered("\xe4\xbf\x9d\xe5\xad\x98", 70, 840, 28, TFT_WHITE, TFT_BLACK);  // 保存

    // Page indicator (right-aligned, above lower separator)
    if (totalPages > 1) {
      char pageBuf[16];
      snprintf(pageBuf, sizeof(pageBuf), "%d/%d", currentPage, totalPages);
      int pw = getSystemTextWidth(pageBuf, 28);
      drawSystemText(pageBuf, 510 - pw, 850, 28);
    }

    // Lower separator (double line)
    M5.Display.drawLine(20, 878, 520, 878, TFT_BLACK);
    M5.Display.drawLine(20, 881, 520, 881, TFT_BLACK);

    // Nav bar: left/right arrows for paging + return
    bool hasPrev = (tzScrollOffset > 0);
    bool hasNext = (tzScrollOffset + maxVisible < timezoneCount);
    drawHorizontalNavBar(hasPrev, hasNext);
    
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // If showing keyboard for password entry
  if (showingKeyboard) {
    // Show selected network name
    String netLabel = String("\xe7\xb6\xb2\xe7\xb5\xa1\xef\xbc\x9a ") + scannedNetworks[selectedNetworkIndex].ssid;  // 網絡：
    drawSystemText(netLabel.c_str(), 20, 105, 32);

    // Password label
    drawSystemText("\xe5\xaf\x86\xe7\xa2\xbc\xef\xbc\x9a", 20, 155, 32);  // 密碼：
    
    // Password input box
    M5.Display.fillRect(20, 200, 500, 60, TFT_WHITE);
    M5.Display.drawRect(20, 200, 500, 60, TFT_BLACK);
    M5.Display.drawRect(21, 201, 498, 58, TFT_BLACK);
    M5.Display.setCursor(30, 215);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    
    // Show password as dots for security
    String maskedPassword = "";
    for (int i = 0; i < passwordInput.length(); i++) {
      maskedPassword += "•";
    }
    M5.Display.print(maskedPassword);
    
    // Draw virtual keyboard
    drawVirtualKeyboard();
    
    // Connect button
    int btnY = 620;
    M5.Display.fillRoundRect(20, btnY, 200, 70, 8, TFT_BLACK);
    drawSystemText("\xe9\x80\xa3\xe7\xb7\x9a", 50, btnY + 18, 34, TFT_WHITE, TFT_BLACK);  // 連線
    
    // Universal return button (lower-right)
    drawReturnButton();
    
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // Show scanning status or network list
  int listStartY = 100;  // right after title separator

  // Show currently connected WiFi first
  bool hasConnected = (WiFi.status() == WL_CONNECTED);
  String connectedSSID = hasConnected ? WiFi.SSID() : "";
  if (hasConnected && connectedSSID.length() > 0) {
    // Highlighted connected row
    M5.Display.fillRoundRect(15, listStartY, 510, 68, 8, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(15, listStartY, 510, 68, 8, TFT_BLACK);

    // Signal strength bars for connected network
    int rssi = WiFi.RSSI();
    int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : 1;
    int barX = 28;
    for (int b = 0; b < bars; b++) {
      M5.Display.fillRect(barX, listStartY + 40 - (b + 1) * 5, 7, (b + 1) * 5, TFT_BLACK);
      barX += 10;
    }

    // SSID
    drawSystemText(connectedSSID.c_str(), 72, listStartY + 10, 32);

    // "已連線" label on the right
    drawSystemText("\xe5\xb7\xb2\xe9\x80\xa3\xe7\xb7\x9a", 430, listStartY + 14, 26);  // 已連線

    listStartY += 80;
  }

  if (wifiScanning) {
    drawSystemText("\xe6\x8e\x83\xe6\x8f\x8f\xe4\xb8\xad\xe2\x80\xa6", 20, listStartY + 10, 32);  // 掃描中…
  } else if (networkCount == 0) {
    drawSystemText("\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0\xe7\xb6\xb2\xe7\xb5\xa1", 20, listStartY + 10, 32);  // 未找到網絡
  } else {
    // Display other networks (skip the currently connected one)
    int y = listStartY;
    int maxY = 830;  // leave room for buttons
    for (int i = 0; i < networkCount; i++) {
      if (y > maxY) break;

      // Skip the currently connected network (already shown above)
      if (hasConnected && scannedNetworks[i].ssid == connectedSSID) continue;

      // Signal strength bars
      int bars = 0;
      if (scannedNetworks[i].rssi > -50) bars = 4;
      else if (scannedNetworks[i].rssi > -60) bars = 3;
      else if (scannedNetworks[i].rssi > -70) bars = 2;
      else bars = 1;

      int barX = 28;
      for (int b = 0; b < bars; b++) {
        M5.Display.fillRect(barX, y + 40 - (b + 1) * 5, 7, (b + 1) * 5, TFT_BLACK);
        barX += 10;
      }

      // Network name
      drawSystemText(scannedNetworks[i].ssid.c_str(), 72, y + 10, 32);

      // Lock icon if encrypted
      if (scannedNetworks[i].encrypted) {
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.setCursor(490, y + 15);
        M5.Display.print("*");
      }

      // Separator line
      M5.Display.drawLine(20, y + 64, 520, y + 64, TFT_LIGHTGRAY);

      y += 68;
    }
  }
  
  // Scan button (above lower separator)
  M5.Display.fillRoundRect(20, 832, 100, 44, 8, TFT_BLACK);
  drawSystemTextCentered("\xe6\x8e\x83\xe6\x8f\x8f", 70, 840, 28, TFT_WHITE, TFT_BLACK);  // 掃描
  
  // Lower separator (double line)
  M5.Display.drawLine(20, 878, 520, 878, TFT_BLACK);
  M5.Display.drawLine(20, 881, 520, 881, TFT_BLACK);
  
  // Universal return button (lower-right)
  drawReturnButton();
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("WiFi setup screen displayed");
}

void drawBluetoothSetup() {
  Serial.println("Drawing Bluetooth setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + return button
  drawStatusBar();
  drawReturnButton();
  
  // Title
  drawSystemText("藍牙", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // Current status
  if (bleConnectedToDevice) {
    drawSystemText("狀態： 已連接", 20, 120, 32);
    
    // Show connected device info
    M5.Display.drawRect(15, 170, 500, 80, TFT_BLACK);
    M5.Display.drawRect(16, 171, 498, 78, TFT_BLACK);
    String connInfo = "已連接：" + bleConnectedName;
    drawSystemText(connInfo.c_str(), 25, 190, 22);
  } else if (bluetoothActive) {
    drawSystemText("狀態： 已啟用", 20, 120, 32);
    
    // Show device name
    M5.Display.drawRect(15, 170, 500, 80, TFT_BLACK);
    M5.Display.drawRect(16, 171, 498, 78, TFT_BLACK);
    drawSystemText("裝置名稱：M5Paper-BLE", 25, 190, 22);
  } else {
    drawSystemText("狀態： 未啟用", 20, 120, 32);
  }
  
  // If showing scan results
  if (bleShowingScan) {
    drawSystemText("附近藍牙裝置：", 20, 280, 28);
    
    if (bleScanning) {
      drawSystemText("掃描中...", 20, 320, 22);
    } else if (bleDeviceCount == 0) {
      drawSystemText("未發現裝置", 20, 320, 22);
    } else {
      // Show device list (max 7 items)
      int listY = 320;
      int maxShow = min(bleDeviceCount, 7);
      for (int i = 0; i < maxShow; i++) {
        // Highlight selected
        if (i == bleSelectedDevice) {
          M5.Display.fillRect(18, listY - 2, 500, 55, TFT_LIGHTGRAY);
        }
        
        // Device name
        drawSystemText(bleDevices[i].name.c_str(), 40, listY, 22);
        
        // RSSI signal bars
        int bars = 0;
        if (bleDevices[i].rssi > -50) bars = 4;
        else if (bleDevices[i].rssi > -65) bars = 3;
        else if (bleDevices[i].rssi > -80) bars = 2;
        else bars = 1;
        
        int barX = 440;
        for (int b = 0; b < bars; b++) {
          M5.Display.fillRect(barX, listY + 28 - (b + 1) * 5, 8, (b + 1) * 5, TFT_BLACK);
          barX += 12;
        }
        
        // Address (small)
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_DARKGRAY);
        M5.Display.setCursor(40, listY + 28);
        M5.Display.print(bleDevices[i].address);
        M5.Display.setTextColor(TFT_BLACK);
        
        // Separator
        M5.Display.drawLine(20, listY + 50, 520, listY + 50, TFT_LIGHTGRAY);
        
        listY += 55;
      }
    }
    
    // Scan / Re-scan button
    int scanBtnY = 750;
    M5.Display.fillRect(20, scanBtnY, 150, 70, TFT_BLACK);
    drawSystemText("掃描", 45, scanBtnY + 20, 28, TFT_WHITE, TFT_BLACK);
    
    // Connect button (if a device is selected)
    if (bleSelectedDevice >= 0 && bleSelectedDevice < bleDeviceCount) {
      M5.Display.fillRect(190, scanBtnY, 150, 70, EPD_DARK_GRAY);
      drawSystemText("連接", 215, scanBtnY + 20, 28, TFT_WHITE, EPD_DARK_GRAY);
    }
    
    // Universal return button (lower-right)
    drawReturnButton();
    
    M5.Display.endWrite();
    M5.Display.display();
    Serial.println("Bluetooth scan view displayed");
    return;
  }
  
  // Toggle button
  int btnY = 300;
  if (bluetoothActive) {
    M5.Display.fillRect(20, btnY, 240, 90, TFT_BLACK);
    drawSystemText("關閉", 75, btnY + 30, 32, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Display.fillRect(20, btnY, 240, 90, EPD_DARK_GRAY);
    drawSystemText("啟用", 75, btnY + 30, 32, TFT_WHITE, EPD_DARK_GRAY);
  }
  
  // Scan button
  int scanBtnY = btnY + 110;
  M5.Display.fillRect(20, scanBtnY, 240, 90, EPD_DARK_GRAY);
  drawSystemText("掃描裝置", 50, scanBtnY + 30, 28, TFT_WHITE, EPD_DARK_GRAY);
  
  // Info box
  M5.Display.drawRect(280, btnY, 240, 200, TFT_BLACK);
  drawSystemText("說明：", 290, btnY + 10, 22);
  drawSystemText("啟用後可通過", 290, btnY + 40, 20);
  drawSystemText("藍牙低功耗傳輸", 290, btnY + 70, 20);
  drawSystemText("資料到裝置", 290, btnY + 100, 20);
  drawSystemText("• BLE UART 模式", 290, btnY + 140, 20);
  drawSystemText("• 可掃描並連接裝置", 290, btnY + 170, 20);
  
  // Universal return button (lower-right)
  drawReturnButton();
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Bluetooth setup displayed");
}

void drawSystemFontSetup() {
  Serial.println("Drawing system font setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + return button
  drawStatusBar();
  drawReturnButton();
  
  // Title
  drawSystemText("系統字體", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  // Current status
  if (systemFontChoice == 0) {
    drawSystemText("目前： 源樣明體", 20, 120, 32);
  } else {
    drawSystemText("目前： Silver", 20, 120, 32);
  }
  
  // Toggle button (full width)
  int btnY = 400;
  if (systemFontChoice == 0) {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("切換為 Silver", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  } else {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("切換為 源樣明體", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  }
  
  // Info section
  int infoY = 530;
  M5.Display.drawRect(20, infoY, 500, 370, TFT_BLACK);
  
  drawSystemText("說明", 30, infoY + 15, 32);
  M5.Display.drawLine(20, infoY + 55, 520, infoY + 55, EPD_LIGHT_GRAY);
  
  drawSystemText("源樣明體 GenYoMinTW", 40, infoY + 75, 28);
  drawSystemText("• 預設系統字體", 40, infoY + 115, 24);
  drawSystemText("• 標籤內建於韌體中", 40, infoY + 150, 24);
  drawSystemText("• 適合繁體中文閱讀", 40, infoY + 185, 24);
  
  drawSystemText("Silver", 40, infoY + 235, 28);
  drawSystemText("• 像素風格點陣字體", 40, infoY + 275, 24);
  drawSystemText("• 標籤從 SD 卡載入", 40, infoY + 310, 24);
  drawSystemText("• 切換後需重新啟動", 40, infoY + 345, 24);
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("System font setup displayed");
}

void drawAboutPage() {
  Serial.println("Drawing about page...");

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();
  drawReturnButton();

  // Title
  drawSystemText("關於", 20, 42, 40);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);

  int y = 110;
  int lineH = 46;

  // App name (split into two lines at size 36 to fit width)
  drawSystemText("M5Stack Paper S3", 20, y, 36);
  y += 42;
  drawSystemText("中文電子書閱讀器", 20, y, 36);
  y += lineH + 10;

  // Build date
  M5.Display.drawLine(20, y - 5, 520, y - 5, EPD_LIGHT_GRAY);
  drawSystemText(("編譯日期： " + String(__DATE__) + " " + __TIME__).c_str(), 20, y, 32);
  y += lineH;

  // Hardware
  drawSystemText("硬體： M5Paper S3 (ESP32-S3)", 20, y, 32);
  y += lineH;

  // CPU frequency
  drawSystemText(("CPU： " + String(ESP.getCpuFreqMHz()) + " MHz").c_str(), 20, y, 32);
  y += lineH;

  // Flash
  drawSystemText(("Flash： " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB").c_str(), 20, y, 32);
  y += lineH;

  // PSRAM
  drawSystemText(("PSRAM： " + String(ESP.getPsramSize() / 1024 / 1024) + " MB（剩 " + String(ESP.getFreePsram() / 1024) + " KB）").c_str(), 20, y, 32);
  y += lineH;

  // Heap (internal SRAM)
  drawSystemText(("記憶體： " + String(ESP.getHeapSize() / 1024) + " KB（剩餘 " + String(ESP.getFreeHeap() / 1024) + " KB）").c_str(), 20, y, 32);
  y += lineH;

  // SD card
  if (sdCardAvailable) {
    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();
    drawSystemText(("SD 卡： " + String((uint32_t)(usedBytes / 1024 / 1024)) + " / " + String((uint32_t)(totalBytes / 1024 / 1024)) + " MB").c_str(), 20, y, 32);
  } else {
    drawSystemText("SD 卡： 未插入", 20, y, 32);
  }
  y += lineH;

  // Display
  drawSystemText("螢幕： 540×960 電子紙", 20, y, 32);
  y += lineH;

  // WiFi
  if (WiFi.status() == WL_CONNECTED) {
    drawSystemText(("WiFi： " + WiFi.SSID() + " " + WiFi.localIP().toString()).c_str(), 20, y, 32);
  } else {
    drawSystemText("WiFi： 未連接", 20, y, 32);
  }
  y += lineH + 10;

  // Separator
  M5.Display.drawLine(20, y - 5, 520, y - 5, EPD_LIGHT_GRAY);

  // Author / project
  drawSystemText("GitHub: yuleshow", 20, y, 32, EPD_DARK_GRAY);

  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("About page displayed");
}
