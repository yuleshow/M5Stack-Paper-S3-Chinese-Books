#include "globals.h"

void drawFontMenu() {
  Serial.println("Drawing font menu...");
  
  // Scan SD card for fonts if not done yet
  if (fontFileCount == 0) {
    scanFontFiles();
  }
  numFonts = fontFileCount;  // Update for touch handler
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Status bar + nav bar first
  drawStatusBar();
  {
    int totalPages = (fontFileCount + FONTS_PER_PAGE - 1) / FONTS_PER_PAGE;
    bool showPrev = (fontMenuPage > 0);
    bool showNext = (fontMenuPage < totalPages - 1);
    drawNavBar(showPrev, showNext);
  }
  
  delay(100);
  
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);
  
  drawSystemText("選擇閱讀字型 Reading Font", 20, 30, 24);
  M5.Display.drawLine(20, 60, 520, 60, TFT_BLACK);
  
  // Pagination
  int totalPages = (fontFileCount + FONTS_PER_PAGE - 1) / FONTS_PER_PAGE;
  if (fontMenuPage >= totalPages) fontMenuPage = totalPages - 1;
  if (fontMenuPage < 0) fontMenuPage = 0;
  int startIdx = fontMenuPage * FONTS_PER_PAGE;
  int endIdx = min(startIdx + FONTS_PER_PAGE, fontFileCount);
  
  // Show dynamic font list for current page
  for (int i = startIdx; i < endIdx; i++) {
    // Check for nav touch between font items
    if (checkNavTouch()) {
      Serial.println("Nav touch during font menu render - aborting");
      return;
    }
    
    int y = 100 + ((i - startIdx) * 75);
    
    if (i == selectedFontIndex) {
      M5.Display.fillRect(30, y - 5, 480, 55, TFT_LIGHTGRAY);
    }
    
    // Use display name instead of filename
    String displayName = fontDisplayNames[i];
    String fileName = fontFileList[i];
    
    // Draw font type indicator + filename (small) on first line
    String typeLabel;
    if (fileName.endsWith(".bin") || fileName.endsWith(".BIN")) {
      typeLabel = "▣ ";
    } else {
      typeLabel = "Ⓣ ";
    }
    
    // Small filename line (using system font)
    String fileInfo = typeLabel + fileName;
    if (i == selectedFontIndex) {
      fileInfo += " ✓";
    }
    drawSystemText(fileInfo.c_str(), 50, y - 2, 16);
    
    // Render font display name using its own font face
    if (fileName.endsWith(".bin") || fileName.endsWith(".BIN")) {
      // For .bin fonts, just show the name with system font (can't easily swap bin fonts)
      String sampleLine = displayName + " \xE7\xAF\x84\xE4\xBE\x8B\xEF\xBC\x9A\xE3\x80\x8C\xE9\x80\x99\xE6\x97\xA5\xEF\xBC\x8C\xE3\x80\x82\xE3\x80\x8D";
      drawSystemText(sampleLine.c_str(), 50, y + 20, 24);
    } else {
      // TTF/TTC font: temporarily load and render the name in its own typeface
      bool loaded = loadTTFFont(fileName.c_str(), 24);
      if (loaded && ofrFontLoaded) {
        ofr.setFontColor(TFT_BLACK, (i == selectedFontIndex) ? TFT_LIGHTGRAY : TFT_WHITE);
        String sampleLine = displayName + " \xE7\xAF\x84\xE4\xBE\x8B\xEF\xBC\x9A\xE3\x80\x8C\xE9\x80\x99\xE6\x97\xA5\xEF\xBC\x8C\xE3\x80\x82\xE3\x80\x8D";
        ofr.drawString(sampleLine.c_str(), 50, y + 20);
      } else {
        String sampleLine = displayName + " \xE7\xAF\x84\xE4\xBE\x8B\xEF\xBC\x9A\xE3\x80\x8C\xE9\x80\x99\xE6\x97\xA5\xEF\xBC\x8C\xE3\x80\x82\xE3\x80\x8D";
        drawSystemText(sampleLine.c_str(), 50, y + 20, 24);
      }
    }
  }
  
  // Restore system font after rendering font previews
  loadSystemFont();
  
  // Status line at bottom - show system and reading font info
  String statusLine = "系統：" + (systemFontFile.length() > 0 ? systemFontFile : String("內建"));
  if (readingFontIndex >= 0 && readingFontIndex < fontFileCount) {
    statusLine += " | 閱讀：" + fontDisplayNames[readingFontIndex];
  }
  drawSystemText(statusLine.c_str(), 20, 830, 18);
  
  // Page indicator next to right arrow, larger font
  if (totalPages > 1) {
    M5.Display.setCursor(155, 910);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.printf("%d/%d", fontMenuPage + 1, totalPages);
  }
  
  Serial.println("Calling display()...");
  if (pendingNavTouch) {
    Serial.println("Skipping display() - nav touch pending");
    M5.Display.endWrite();
    return;
  }
  M5.Display.endWrite();
  M5.Display.display();
  
  delay(500);  // Brief wait for e-ink refresh
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
  int startY = 450;
  
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
  M5.Display.fillRect(20, 240, 500, 60, TFT_WHITE);
  M5.Display.drawRect(20, 240, 500, 60, TFT_BLACK);
  M5.Display.drawRect(21, 241, 498, 58, TFT_BLACK);
  
  // Show password as dots
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(30, 255);
  
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
  drawSystemText("檔案上傳伺服器", 20, 30, 40);
  
  // Current status
  drawSystemText("狀態：", 20, 120, 32);
  
  if (webServerRunning) {
    drawSystemText("執行中", 160, 120, 32, EPD_DARK_GRAY);
    
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
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(20, 300);
    M5.Display.print("Enter above IP in computer/phone browser");
  } else if (webServerEnabled) {
    drawSystemText("已啟用", 160, 120, 32, EPD_DARK_GRAY);
    
    if (WiFi.status() == WL_CONNECTED) {
      drawSystemText("正在啟動伺服器...", 20, 200, 28);
    } else {
      drawSystemText("等待 WiFi 連接...", 20, 200, 28);
    }
  } else {
    drawSystemText("未啟用", 160, 120, 32, TFT_DARKGRAY);
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
  drawSystemText("圖標來源", 20, 30, 40);
  
  // Current status
  drawSystemText("目前：", 20, 120, 32);
  if (useSDCardIcons) {
    drawSystemText("SD 卡優先", 160, 120, 32, EPD_DARK_GRAY);
  } else {
    drawSystemText("內建圖標", 160, 120, 32, EPD_DARK_GRAY);
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
  drawSystemText("曆法計算", 20, 30, 40);
  
  // Current status
  drawSystemText("目前：", 20, 120, 32);
  if (useSxwnlCalendar) {
    drawSystemText("壽星天文曆", 160, 120, 32, EPD_DARK_GRAY);
  } else {
    drawSystemText("Meeus 天文算法", 160, 120, 32, EPD_DARK_GRAY);
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
  drawSystemText("設定", 20, 25, 40);
  {
    char pageBuf[8];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", setupMenuPage + 1, totalPages);
    drawSystemText(pageBuf, 500, 30, 22);
  }
  bool showPrev = (setupMenuPage > 0);
  bool showNext = (setupMenuPage < totalPages - 1);
  drawNavBar(showPrev, showNext);

  int y = 85;
  int itemHeight = 100;

  if (setupMenuPage == 0) {
    // Page 1: 5 items
    
    // WiFi Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("WiFi 設定", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (WiFi.status() == WL_CONNECTED) {
      drawSystemText("已連接", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else if (wifiConfig.configured) {
      drawSystemText("已儲存", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未設定", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + 18;

    // Timezone Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("時區設定", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    drawSystemText("已設定", 40, y + 62, 22, TFT_BLACK, cardTextBg);

    y += itemHeight + 18;

    // Web Server Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("檔案上傳伺服器", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (webServerRunning) {
      drawSystemText("執行中", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else if (webServerEnabled) {
      drawSystemText("已啟用", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未啟用", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + 18;

    // USB Mass Storage item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("USB 外接磁碟", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (usbMSCActive) {
      drawSystemText("執行中 - 裝置停用", 40, y + 62, 22, EPD_DARK_GRAY, cardTextBg);
    } else {
      drawSystemText("未啟用", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + 18;

    // Icon Source Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("圖標來源", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (useSDCardIcons) {
      drawSystemText("SD 卡優先（可自訂圖標）", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("內建圖標（速度較快）", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }
  } else {
    // Page 2: 3 items
    
    // Calendar Calculation Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("曆法計算", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (useSxwnlCalendar) {
      drawSystemText("壽星天文曆（許劍偉）", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("Meeus 天文算法（精度較高）", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + 18;

    // Bluetooth Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("藍牙", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (bluetoothActive) {
      drawSystemText("已啟用", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未啟用", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }

    y += itemHeight + 18;

    // Auto-Sleep Settings item
    M5.Display.fillRoundRect(20, y, 500, itemHeight, 10, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(20, y, 500, itemHeight, 10, TFT_BLACK);
    M5.Display.drawRoundRect(21, y + 1, 498, itemHeight - 2, 9, TFT_BLACK);

    drawSystemText("自動休眠", 40, y + 18, 32, TFT_BLACK, cardTextBg);

    if (autoSleepEnabled) {
      drawSystemText("已啟用 - 10分鐘無操作自動休眠", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    } else {
      drawSystemText("未啟用 - 保持開啟", 40, y + 62, 22, TFT_BLACK, cardTextBg);
    }
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
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(20, 30);
  M5.Display.print("WiFi Setup");
  
  // Clock display at top-right (if time synced)
  if (timeConfig.timeSynced) {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextDatum(TR_DATUM);
    
    // Draw clock box with decorative border
    int clockX = M5.Display.width() - 10;
    int clockY = 10;
    int clockW = 220;
    int clockH = 140;
    
    // Outer shadow
    M5.Display.fillRoundRect(clockX - clockW + 3, clockY + 3, clockW, clockH, 8, TFT_DARKGRAY);
    
    // Main box
    M5.Display.fillRoundRect(clockX - clockW, clockY, clockW, clockH, 8, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(clockX - clockW, clockY, clockW, clockH, 8, TFT_BLACK);
    M5.Display.drawRoundRect(clockX - clockW + 1, clockY + 1, clockW - 2, clockH - 2, 7, TFT_BLACK);
    
    // Time (large)
    String timeStr = getCurrentTimeString();
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextSize(4);
    M5.Display.drawString(timeStr, clockX - clockW/2 + 110, clockY + 15);
    
    // Date
    String dateStr = getCurrentDateString();
    M5.Display.setTextSize(1);
    M5.Display.drawString(dateStr, clockX - clockW/2 + 110, clockY + 80);
    
    // Timezone label
    M5.Display.setTextSize(1);
    M5.Display.drawString(timezones[selectedTimezone].name, clockX - clockW/2 + 110, clockY + 105);
    
    // WiFi status indicator (small icon)
    if (WiFi.status() == WL_CONNECTED) {
      M5.Display.fillCircle(clockX - clockW + 15, clockY + 15, 6, EPD_DARK_GRAY);
      M5.Display.setTextSize(1);
      M5.Display.setTextDatum(TL_DATUM);
      M5.Display.drawString("WiFi", clockX - clockW + 25, clockY + 10);
      M5.Display.setTextDatum(TR_DATUM);
    }
    
    M5.Display.setTextDatum(TL_DATUM);
  } else if (WiFi.status() == WL_CONNECTED) {
    // WiFi connected but no time sync
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(TR_DATUM);
    
    int clockX = M5.Display.width() - 10;
    int clockY = 10;
    M5.Display.fillRoundRect(clockX - 220, clockY, 220, 60, 8, EPD_LIGHT_GRAY);
    M5.Display.drawRoundRect(clockX - 220, clockY, 220, 60, 8, TFT_BLACK);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.drawString("WiFi Connected", clockX - 15, clockY + 10);
    M5.Display.drawString("Syncing time...", clockX - 15, clockY + 35);
    
    M5.Display.setTextDatum(TL_DATUM);
  } else {
    // No WiFi connection
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(TR_DATUM);
    
    int clockX = M5.Display.width() - 10;
    int clockY = 10;
    M5.Display.fillRoundRect(clockX - 220, clockY, 220, 60, 8, TFT_LIGHTGRAY);
    M5.Display.drawRoundRect(clockX - 220, clockY, 220, 60, 8, TFT_BLACK);
    M5.Display.setTextColor(TFT_DARKGRAY);
    M5.Display.drawString("No WiFi", clockX - 15, clockY + 10);
    M5.Display.drawString("Connect to sync time", clockX - 15, clockY + 35);
    
    M5.Display.setTextDatum(TL_DATUM);
  }
  
  // If showing timezone selection
  if (showingTimezone) {
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 100);
    M5.Display.println("Select Timezone:");
    
    // Show timezone list
    int y = 160;
    for (int i = 0; i < timezoneCount; i++) {
      // Highlight selected timezone
      if (i == selectedTimezone) {
        M5.Display.fillRect(15, y - 5, 510, 60, TFT_LIGHTGRAY);
      }
      
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setTextSize(2);
      M5.Display.setCursor(30, y + 10);
      M5.Display.print(timezones[i].name);
      
      // Draw separator
      M5.Display.drawLine(20, y + 55, 520, y + 55, TFT_DARKGRAY);
      
      y += 70;
      if (y > 750) break;  // Don't overflow screen
    }
    
    // Save button
    int btnY = 820;
    M5.Display.fillRect(20, btnY, 200, 80, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(70, btnY + 25);
    M5.Display.print("Save");
    
    // Universal return button (lower-right)
    drawReturnButton();
    
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // If showing keyboard for password entry
  if (showingKeyboard) {
    // Show selected network info
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 100);
    M5.Display.print("Network:");
    M5.Display.setCursor(20, 140);
    M5.Display.setTextSize(2);
    M5.Display.println(scannedNetworks[selectedNetworkIndex].ssid);
    
    M5.Display.setCursor(20, 200);
    M5.Display.setTextSize(2);
    M5.Display.print("Password:");
    
    // Password input box
    M5.Display.fillRect(20, 240, 500, 60, TFT_WHITE);
    M5.Display.drawRect(20, 240, 500, 60, TFT_BLACK);
    M5.Display.drawRect(21, 241, 498, 58, TFT_BLACK);
    M5.Display.setCursor(30, 255);
    M5.Display.setTextSize(2);
    
    // Show password as dots for security
    String maskedPassword = "";
    for (int i = 0; i < passwordInput.length(); i++) {
      maskedPassword += "•";
    }
    M5.Display.print(maskedPassword);
    
    // Draw virtual keyboard
    drawVirtualKeyboard();
    
    // Connect button
    int btnY = 820;
    M5.Display.fillRect(20, btnY, 200, 80, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(50, btnY + 25);
    M5.Display.print("Connect");
    
    // Universal return button (lower-right)
    drawReturnButton();
    
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // Show scanning status or network list
  if (wifiScanning) {
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 120);
    M5.Display.println("Scanning for networks...");
  } else if (networkCount == 0) {
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 120);
    M5.Display.println("No networks found");
  } else {
    // Display network list (max 7 to avoid overlapping buttons at y=750)
    M5.Display.setTextSize(2);
    int y = 120;
    for (int i = 0; i < networkCount && i < 7; i++) {
      // Network name
      M5.Display.setCursor(60, y);
      M5.Display.print(scannedNetworks[i].ssid);
      
      // Signal strength
      int bars = 0;
      if (scannedNetworks[i].rssi > -50) bars = 4;
      else if (scannedNetworks[i].rssi > -60) bars = 3;
      else if (scannedNetworks[i].rssi > -70) bars = 2;
      else bars = 1;
      
      int barX = 20;
      for (int b = 0; b < bars; b++) {
        M5.Display.fillRect(barX, y + 15 - (b + 1) * 4, 6, (b + 1) * 4, TFT_BLACK);
        barX += 8;
      }
      
      // Lock icon if encrypted
      if (scannedNetworks[i].encrypted) {
        M5.Display.setCursor(470, y);
        M5.Display.print("[*]");
      }
      
      // Draw separator line
      M5.Display.drawLine(20, y + 40, 520, y + 40, TFT_LIGHTGRAY);
      
      y += 80;
    }
  }
  
  // Scan button
  int btnY = 750;
  M5.Display.fillRect(20, btnY, 150, 70, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(50, btnY + 20);
  M5.Display.print("Scan");
  
  // Timezone button
  M5.Display.fillRect(190, btnY, 150, 70, TFT_DARKGRAY);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(210, btnY + 15);
  M5.Display.print("Timezone");
  M5.Display.setCursor(210, btnY + 40);
  M5.Display.print(timezones[selectedTimezone].name);
  
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
  drawSystemText("藍牙", 20, 30, 40);
  
  // Current status
  drawSystemText("狀態：", 20, 120, 32);
  if (bleConnectedToDevice) {
    drawSystemText("已連接", 160, 120, 32, EPD_DARK_GRAY);
    
    // Show connected device info
    M5.Display.drawRect(15, 170, 500, 80, TFT_BLACK);
    M5.Display.drawRect(16, 171, 498, 78, TFT_BLACK);
    String connInfo = "已連接：" + bleConnectedName;
    drawSystemText(connInfo.c_str(), 25, 190, 22);
  } else if (bluetoothActive) {
    drawSystemText("已啟用", 160, 120, 32, EPD_DARK_GRAY);
    
    // Show device name
    M5.Display.drawRect(15, 170, 500, 80, TFT_BLACK);
    M5.Display.drawRect(16, 171, 498, 78, TFT_BLACK);
    drawSystemText("裝置名稱：M5Paper-BLE", 25, 190, 22);
  } else {
    drawSystemText("未啟用", 160, 120, 32, TFT_DARKGRAY);
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
