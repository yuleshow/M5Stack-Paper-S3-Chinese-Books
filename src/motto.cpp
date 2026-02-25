#include "globals.h"
#include "sleeping_jpg.h"
#include <esp_random.h>

// RTC-persistent motto index — survives deep sleep
RTC_DATA_ATTR int mottoIndex = -1;  // -1 = not yet initialized (random first pick)

// Built-in default mottos (used when /mottos.txt is missing)
static const char* defaultMottos[] = {
  "讀書破萬卷，下筆如有神。",
  "學而不思則罔，思而不學則殆。",
  "千里之行，始於足下。",
  "溫故而知新，可以為師矣。",
  "三人行，必有我師焉。",
  "不積跬步，無以至千里。",
  "天行健，君子以自強不息。",
  "己所不欲，勿施於人。",
  "靜以修身，儉以養德。",
  "海納百川，有容乃大。"
};
static const int defaultMottoCount = 10;

// Load mottos from /mottos.txt on SD card root (one motto per line)
void loadMottos() {
  mottoCount = 0;

  if (!sdCardAvailable) {
    Serial.println("SD card not available, using default mottos");
    goto useDefaults;
  }

  {
    ScopedSDLock lock;
    File f = SD.open("/mottos.txt", FILE_READ);
    if (!f) {
      Serial.println("No /mottos.txt found, using defaults");
      goto useDefaults;
    }

    while (f.available() && mottoCount < MAX_MOTTOS) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        mottoList[mottoCount] = line;
        mottoCount++;
      }
    }
    f.close();
  }

  if (mottoCount == 0) {
    Serial.println("mottos.txt was empty, using defaults");
    goto useDefaults;
  }

  Serial.printf("Loaded %d mottos from SD card\n", mottoCount);
  return;

useDefaults:
  for (int i = 0; i < defaultMottoCount && i < MAX_MOTTOS; i++) {
    mottoList[i] = defaultMottos[i];
  }
  mottoCount = defaultMottoCount;
  Serial.printf("Using %d built-in default mottos\n", mottoCount);
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

  int charCount = utf8CharCount(motto);

  // Calculate how many chars fit in one column
  int textAreaH = h - 200;  // leave room for margins
  int maxCharsPerCol = (textAreaH - padTop - padBottom) / charSpacing;
  if (maxCharsPerCol < 1) maxCharsPerCol = 1;

  // Calculate number of columns needed
  int numCols = (charCount + maxCharsPerCol - 1) / maxCharsPerCol;
  if (numCols < 1) numCols = 1;

  // Card dimensions based on content
  int cardW = numCols * colSpacing + padSide * 2;
  int cardH = min(charCount, maxCharsPerCol) * charSpacing + padTop + padBottom;

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

  // --- Draw motto characters vertically (right-to-left columns) ---
  int col = 0;
  int row = 0;
  int j = 0;  // byte index into motto string

  while (j < (int)motto.length()) {
    String ch = utf8CharAt(motto, j);
    {
      int tmp = 0;
      uint32_t cp = utf8Decode(ch, tmp);
      uint32_t mapped = toVerticalPunct(cp);
      if (mapped != cp) {
        ch = "";
        utf8Encode(mapped, ch);
      }
    }

    // Column x: rightmost column first (traditional Chinese reading order)
    int cx = cardX + cardW - padSide - col * colSpacing - colSpacing / 2;
    int cy = cardY + padTop + row * charSpacing;

    if (mottoFontLoaded) {
      ofr.setFontSize(charSize);
      ofr.setFontColor(TFT_BLACK, TFT_WHITE);
      // Use cdrawString to horizontally center each glyph in the column
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
