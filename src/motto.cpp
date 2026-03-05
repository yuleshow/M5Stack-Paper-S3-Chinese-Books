#include "globals.h"
#include "sleeping_jpg.h"
#include <esp_random.h>

// RTC-persistent motto index — survives deep sleep
RTC_DATA_ATTR int mottoIndex = -1;  // -1 = not yet initialized (random first pick)

// Built-in default mottos (used when /mottos.txt is missing)
static const char* defaultMottos[] = {
"自戀的人會把自己的話寫到程序中去，比如這一條。——梅璽閣主",
"再好的AI，如果你喂它吃的是屎，你猜它能拉出什麼來？——梅璽閣主",
"如果一個罪犯，他犯的事沒人敢說，也不能公開討論，那麼他一定是個英雄。——梅璽閣主",
"如果有人說「我又不是為了錢」，那麼他一定是為了錢，否則的話，他連這句話都不會說的。——梅璽閣主",
"一切民俗，如果那十年也遵守也保持，那就是民俗，否則就是封建迷信。——梅璽閣主",
"各位小伙伴一定要切記，在網上撕逼的最終目的是：氣死對方，而不是說服對方！——梅璽閣主",
"可以與爹犟，可以和娘犟，但是千萬不能同麻將犟。——梅璽閣主",
"中餐很好吃，但是你要說營養，它真的沒什麼營養。——梅璽閣主",
"人可以做春夢，也可以在秋天做夢，但是不要做春秋大夢。——梅璽閣主",
"有時候，「不要和沒有出過村的人吵架」與「不要和沒有出過國的人吵架」，是同一個意思。——梅璽閣主"
};
static const int defaultMottoCount = 10;

// Load mottos: always include built-in defaults, then append from /mottos.txt
void loadMottos() {
  mottoCount = 0;

  // 1. Always load built-in defaults first
  for (int i = 0; i < defaultMottoCount && mottoCount < MAX_MOTTOS; i++) {
    mottoList[mottoCount] = defaultMottos[i];
    mottoCount++;
  }
  Serial.printf("Loaded %d built-in default mottos\n", mottoCount);

  // 2. Append mottos from SD card (skip duplicates)
  if (!sdCardAvailable) {
    Serial.println("SD card not available, using defaults only");
    return;
  }

  {
    ScopedSDLock lock;
    File f = SD.open("/mottos.txt", FILE_READ);
    if (!f) {
      Serial.println("No /mottos.txt found, using defaults only");
      return;
    }

    int sdCount = 0;
    while (f.available() && mottoCount < MAX_MOTTOS) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      // Skip duplicates (check against already loaded mottos)
      bool duplicate = false;
      for (int j = 0; j < mottoCount; j++) {
        if (mottoList[j] == line) { duplicate = true; break; }
      }
      if (!duplicate) {
        mottoList[mottoCount] = line;
        mottoCount++;
        sdCount++;
      }
    }
    f.close();
    Serial.printf("Appended %d mottos from SD card (total: %d)\n", sdCount, mottoCount);
  }
}

// Count UTF-8 characters
static int utf8CharCount(const String& s) {
  int count = 0;
  const char* p = s.c_str();
  while (*p) {
    if ((*p & 0x80) == 0) { p += 1; }
    else if ((*p & 0xE0) == 0xC0) { p += 2; }
    else if ((*p & 0xF0) == 0xE0) { p += 3; }
    else { p += 4; }
    count++;
  }
  return count;
}

// Extract one UTF-8 character starting at byte index j, advance j past it
static String utf8CharAt(const String& s, int& j) {
  int start = j;
  unsigned char c = s.charAt(j);
  if (c < 0x80) { j += 1; }
  else if ((c & 0xE0) == 0xC0) { j += 2; }
  else if ((c & 0xF0) == 0xE0) { j += 3; }
  else { j += 4; }
  if (j > (int)s.length()) j = s.length();
  return s.substring(start, j);
}

// Draw the motto vertically on the sleep screen (traditional Chinese: top-to-bottom, right-to-left)
// Call this AFTER drawing the wallpaper and BEFORE endWrite/display
void drawMottoOnSleep() {
  if (mottoCount == 0) {
    loadMottos();
  }
  if (mottoCount == 0) return;

  // Random motto selection using hardware RNG
  int idx;
  if (mottoCount == 1) {
    idx = 0;
  } else {
    // Pick a random index different from the last shown
    do {
      idx = esp_random() % mottoCount;
    } while (idx == mottoIndex && mottoCount > 1);
  }
  mottoIndex = idx;  // Remember for next time

  String motto = mottoList[idx];
  Serial.printf("Showing motto #%d: %s\n", idx, motto.c_str());

  int w = M5.Display.width();   // 540
  int h = M5.Display.height();  // 960

  // --- Vertical text layout parameters ---
  int charSize = 48;        // character cell size (pixels)
  int colSpacing = 58;      // horizontal spacing between columns
  int charSpacing = 56;     // vertical spacing between characters
  int padTop = 40;          // padding inside card (top)
  int padBottom = 40;       // padding inside card (bottom)
  int padSide = 30;         // padding inside card (left/right)

  // Split motto at "——" for separate attribution rendering (bottom-aligned)
  String mainText = motto;
  String attrText = "";
  int dashIdx = motto.indexOf("——");
  if (dashIdx >= 0) {
    mainText = motto.substring(0, dashIdx);
    attrText = motto.substring(dashIdx);  // includes "——"
  }

  int mainCharCount = utf8CharCount(mainText);
  int attrCharCount = utf8CharCount(attrText);

  // Calculate how many chars fit in one column
  int textAreaH = h - 200;  // leave room for margins
  int maxCharsPerCol = (textAreaH - padTop - padBottom) / charSpacing;
  if (maxCharsPerCol < 1) maxCharsPerCol = 1;

  // Columns for main text and attribution (attribution starts a new column)
  int mainCols = mainCharCount > 0 ? (mainCharCount + maxCharsPerCol - 1) / maxCharsPerCol : 0;
  int attrCols = attrCharCount > 0 ? (attrCharCount + maxCharsPerCol - 1) / maxCharsPerCol : 0;
  int numCols = mainCols + attrCols;
  if (numCols < 1) numCols = 1;

  // Effective rows for card height
  int effectiveRows = (mainCols > 1) ? maxCharsPerCol : mainCharCount;
  if (attrCharCount > 0 && attrCharCount > effectiveRows) {
    effectiveRows = min(attrCharCount, maxCharsPerCol);
  }
  if (effectiveRows < 1) effectiveRows = 1;

  // Card dimensions based on content
  int cardW = numCols * colSpacing + padSide * 2;
  int cardH = effectiveRows * charSpacing + padTop + padBottom;

  // Minimum card size
  if (cardW < 100) cardW = 100;
  if (cardH < 120) cardH = 120;

  // Center the card on screen
  int cardX = (w - cardW) / 2;
  int cardY = (h - cardH) / 2 - 20;  // slightly above center

  // Draw semi-transparent card background
  M5.Display.fillRoundRect(cardX, cardY, cardW, cardH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(cardX, cardY, cardW, cardH, 12, EPD_DARK_GRAY);
  M5.Display.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 11, EPD_DARK_GRAY);

  // Decorative vertical lines on left and right edges
  M5.Display.drawLine(cardX + 12, cardY + 15, cardX + 12, cardY + cardH - 15, EPD_LIGHT_GRAY);
  M5.Display.drawLine(cardX + cardW - 12, cardY + 15, cardX + cardW - 12, cardY + cardH - 15, EPD_LIGHT_GRAY);

  // --- Load TW-Kai font for motto rendering ---
  // Remember current font state so we can restore after
  String prevFontFile = currentFontFile;
  bool prevOfrLoaded = ofrFontLoaded;

  bool mottoFontLoaded = false;
  if (sdCardAvailable) {
    mottoFontLoaded = loadTTFFont("/fonts/TW-Kai-98_1.ttf", charSize);
    if (mottoFontLoaded) {
      Serial.println("Motto: loaded TW-Kai-98_1.ttf for vertical text");
    } else {
      Serial.println("Motto: TW-Kai-98_1.ttf not found, falling back to built-in font");
    }
  }

  // --- Draw main text vertically (top-aligned, right-to-left columns) ---
  int col = 0;
  int row = 0;
  int j = 0;

  while (j < (int)mainText.length()) {
    String ch = utf8CharAt(mainText, j);
    {
      int tmp = 0;
      uint32_t cp = utf8Decode(ch, tmp);
      uint32_t mapped = toVerticalPunct(cp);
      if (mapped != cp) {
        ch = "";
        utf8Encode(mapped, ch);
      }
    }

    int cx = cardX + cardW - padSide - col * colSpacing - colSpacing / 2;
    int cy = cardY + padTop + row * charSpacing;

    if (mottoFontLoaded) {
      ofr.setFontSize(charSize);
      ofr.setFontColor(TFT_BLACK, TFT_WHITE);
      ofr.cdrawString(ch.c_str(), cx, cy, TFT_BLACK, TFT_WHITE);
    } else {
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
      M5.Display.setTextSize(1);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString(ch.c_str(), cx, cy + charSpacing / 2);
    }

    row++;
    if (row >= maxCharsPerCol) {
      row = 0;
      col++;
    }
  }

  // --- Draw attribution vertically (bottom-aligned, new column to the left) ---
  if (attrCharCount > 0) {
    col = mainCols;
    // Bottom-align: position so last char aligns with bottom of card content
    int charsInFirstCol = min(attrCharCount, maxCharsPerCol);
    row = effectiveRows - charsInFirstCol;
    if (row < 0) row = 0;
    j = 0;

    while (j < (int)attrText.length()) {
      String ch = utf8CharAt(attrText, j);
      {
        int tmp = 0;
        uint32_t cp = utf8Decode(ch, tmp);
        uint32_t mapped = toVerticalPunct(cp);
        if (mapped != cp) {
          ch = "";
          utf8Encode(mapped, ch);
        }
      }

      int cx = cardX + cardW - padSide - col * colSpacing - colSpacing / 2;
      int cy = cardY + padTop + row * charSpacing;

      if (mottoFontLoaded) {
        ofr.setFontSize(charSize);
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
        ofr.cdrawString(ch.c_str(), cx, cy, TFT_BLACK, TFT_WHITE);
      } else {
        M5.Display.setFont(&fonts::efontTW_24);
        M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
        M5.Display.setTextSize(1);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString(ch.c_str(), cx, cy + charSpacing / 2);
      }

      row++;
      if (row >= effectiveRows) {
        row = 0;
        col++;
      }
    }
  }

  M5.Display.setTextDatum(TL_DATUM);

  // Restore previous font if needed
  if (mottoFontLoaded && prevOfrLoaded && prevFontFile.length() > 0) {
    loadTTFFont(prevFontFile.c_str(), 30);
  }
}

// Draw the full motto test screen (sleeping wallpaper + motto + return button)
void drawMottoScreen() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();

  int w = M5.Display.width();
  int h = M5.Display.height();

  // Draw sleeping wallpaper
  M5.Display.drawJpg(sleeping_jpg, sleeping_jpg_len, 0, 0, w, h);

  // Draw vertical motto
  drawMottoOnSleep();

  // Draw return button at universal location (bottom-right)
  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}
