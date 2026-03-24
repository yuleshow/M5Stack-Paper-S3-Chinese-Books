#include "globals.h"
#include "dictionary.h"
#include "esp_task_wdt.h"

// Decode one UTF-8 codepoint from a String at position pos.
// Advances pos past the consumed bytes. Returns the Unicode codepoint.
static uint32_t decodeUTF8(const String &s, int &pos) {
  unsigned char c = (unsigned char)s.charAt(pos);
  if (c < 0x80) { pos++; return c; }
  uint32_t cp = 0; int extra = 0;
  if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
  else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
  else { pos++; return c; }  // Invalid lead byte, treat as Latin-1
  for (int j = 0; j < extra && pos + 1 + j < (int)s.length(); j++) {
    unsigned char cont = (unsigned char)s.charAt(pos + 1 + j);
    if ((cont & 0xC0) != 0x80) { pos++; return c; }  // Broken sequence
    cp = (cp << 6) | (cont & 0x3F);
  }
  pos += 1 + extra;
  return cp;
}

// Draw horizontal English/Latin text reading mode.
// Called from drawReading() after common preamble (screen clear, status bar).
// Expects: startWrite() already called, screen cleared, status bar drawn.
void drawEnglishReading() {
  // Nav bar: LTR layout (← = prev, → = next)
  {
    bool hasPrev = (currentPage > 0);
    bool hasNext = (currentPage < totalPages - 1);
    drawHorizontalNavBar(hasPrev, hasNext);
  }

  // Load OFR font for rendering
  bool silverReading = isReadingFontSilver();
  int fontSizePt = silverReading ? silverScaledSize(readingFontSize) : readingFontSize;
  if (fontSizePt < 8) fontSizePt = 16;

  // Load font for English rendering: embedded ET Book (from flash, no SD) or user-selected TTF
  {
    String activeFont = (readingFontFile.length() > 0) ? readingFontFile :
        ((readingFontIndex >= 0 && readingFontIndex < fontFileCount) ? fontFileList[readingFontIndex] : String(""));
    if (activeFont == "ETBook-embedded") {
      loadEmbeddedETBook(fontSizePt);
    } else {
      bool isBinFile = activeFont.endsWith(".bin") || activeFont.endsWith(".BIN");
      if (!isBinFile) {
        // Ensure Regular variant is loaded (may have been switched to Italic/Bold on previous page)
        if (!ofrFontLoaded || currentFontFile != activeFont) {
          loadTTFFont(activeFont.c_str(), fontSizePt);
        }
      } else {
        // Fallback to embedded ET Book
        loadEmbeddedETBook(fontSizePt);
      }
    }
  }
  ofr.setFontSize(fontSizePt);
  ofr.setFontColor(TFT_BLACK, TFT_WHITE);
  ofr.setDrawer(M5.Display);

  // Determine if font has style variants (Italic/Bold/BoldItalic TTF files)
  int fontGroupIdx = readingFontIndex;
  bool hasStyleVariants = false;
  String styleFonts[4] = {"", "", "", ""};
  if (fontGroupIdx >= 0 && fontGroupIdx < fontFileCount && !fontIsCJK[fontGroupIdx]) {
    for (int s = 0; s < 4; s++) {
      styleFonts[s] = fontStyleFiles[fontGroupIdx][s];
      if (s > 0 && styleFonts[s].length() > 0) hasStyleVariants = true;
    }
  }
  int currentStyle = 0; // bit 0 = italic, bit 1 = bold
  bool underlineActive = false;

  // Determine initial style by scanning text before current page
  if (epubFullText && currentPageByteOffset > 0) {
    size_t scanLimit = (currentPageByteOffset > 10000) ? currentPageByteOffset - 10000 : 0;
    for (size_t j = scanLimit; j < currentPageByteOffset; j++) {
      char c = epubFullText[j];
      if (c == STYLE_ITALIC_ON) currentStyle |= 1;
      else if (c == STYLE_ITALIC_OFF) currentStyle &= ~1;
      else if (c == STYLE_BOLD_ON) currentStyle |= 2;
      else if (c == STYLE_BOLD_OFF) currentStyle &= ~2;
      else if (c == STYLE_UNDERLINE_ON) underlineActive = true;
      else if (c == STYLE_UNDERLINE_OFF) underlineActive = false;
    }
  }

  // Helper: switch OFR font to match current style (only for SD font groups)
  auto switchStyleFont = [&](int style) {
    if (!hasStyleVariants) return;
    String fontFile = styleFonts[style];
    if (fontFile.length() == 0) fontFile = styleFonts[0]; // Fall back to Regular
    if (fontFile.length() > 0 && fontFile != currentFontFile) {
      loadTTFFont(fontFile.c_str(), fontSizePt);
      ofr.setFontColor(TFT_BLACK, TFT_WHITE);
      ofr.setDrawer(M5.Display);
    }
  };

  // Load initial style font if page starts mid-italic/bold
  if (hasStyleVariants && currentStyle != 0) {
    switchStyleFont(currentStyle);
  }

  String displayText = (currentPageContent.length() > 0) ? currentPageContent :
                      "This is a sample text for the horizontal English reader mode.";
  Serial.printf("Horizontal rendering page %d/%d, content: %d bytes, ofrLoaded=%d, font=%s, heap=%u, psram=%u\n",
                currentPage + 1, totalPages, displayText.length(),
                (int)ofrFontLoaded, currentFontFile.c_str(),
                ESP.getFreeHeap(), ESP.getFreePsram());

  int lineHeight = fontSizePt + fontSizePt / 4;  // 1.25x line spacing
  int rdLeft = READING_AREA_LEFT;
  int rdRight = READING_AREA_RIGHT;
  int rdTop = READING_AREA_TOP;
  int rdBottom = 830;  // Leave room for page number + progress bar below
  int availW = rdRight - rdLeft;
  int currentY = rdTop;
  int renderStopByte = displayText.length();
  int charsDrawn = 0;
  int i = 0;

  // Clear word positions for dictionary tap lookup
  engClearWords();

  // Pre-compute space width from cached advance widths
  int spaceW = getCharAdvanceW(32, fontSizePt);

  // Line-at-a-time rendering: accumulate words, measure with cached advances,
  // draw characters individually via PSRAM glyph cache (same cache as vertical
  // Chinese mode). After first page, all common glyphs are cached — no FreeType.
  // English text is ASCII — skip UTF-8 decoding entirely.
  String currentLine = "";
  int currentLineW = 0;
  bool pendingSpace = false;
  int lineStartByte = 0;  // byte offset where current line's first word started

  // Helper lambda: draw a line of text char-by-char via glyph cache,
  // handling style markers to switch fonts for italic/bold spans.
  auto drawLineCached = [&](const String &line, int x, int y) {
    int cx = x;
    int c = 0;
    while (c < (int)line.length()) {
      unsigned char lc = (unsigned char)line.charAt(c);
      // Handle style markers embedded in line
      if (lc == STYLE_ITALIC_ON)  { currentStyle |= 1; switchStyleFont(currentStyle); c++; continue; }
      if (lc == STYLE_ITALIC_OFF) { currentStyle &= ~1; switchStyleFont(currentStyle); c++; continue; }
      if (lc == STYLE_BOLD_ON)    { currentStyle |= 2; switchStyleFont(currentStyle); c++; continue; }
      if (lc == STYLE_BOLD_OFF)   { currentStyle &= ~2; switchStyleFont(currentStyle); c++; continue; }
      if (lc == STYLE_UNDERLINE_ON)  { underlineActive = true;  c++; continue; }
      if (lc == STYLE_UNDERLINE_OFF) { underlineActive = false; c++; continue; }
      uint32_t cp = decodeUTF8(line, c);
      int advance = drawOFRCharHoriz(cp, cx, y, TFT_BLACK, fontSizePt);
      if (underlineActive) {
        M5.Display.drawFastHLine(cx, y + lineHeight - 2, advance, TFT_BLACK);
      }
      cx += advance;
    }
  };

  while (i < (int)displayText.length()) {
    unsigned char ch = (unsigned char)displayText.charAt(i);

    // Skip carriage returns
    if (ch == '\r') { i++; continue; }

    // Handle EPUB image markers
    if (ch == EPUB_IMG_MARKER && currentBookIsEpub) {
      if (currentLine.length() > 0) {
        drawLineCached(currentLine, rdLeft, currentY);
        charsDrawn += currentLine.length();
        currentLine = "";
        currentLineW = 0;
        pendingSpace = false;
      }
      int pathStart = i + 1;
      int pathEnd = displayText.indexOf(EPUB_IMG_MARKER, pathStart);
      if (pathEnd > pathStart) {
        String imgPath = displayText.substring(pathStart, pathEnd);
        i = pathEnd + 1;
        M5.Display.endWrite();
        esp_task_wdt_reset();
        bool imgDrawn = epubExtractAndDrawImage(imgPath, rdLeft, rdTop, availW, rdBottom - rdTop);
        esp_task_wdt_reset();
        M5.Display.startWrite();
        if (imgDrawn) {
          renderStopByte = i;
          goto endHorizPage;
        }
      } else {
        i++;
      }
      continue;
    }

    // Newline → paragraph break
    if (ch == '\n') {
      i++;
      if (currentLine.length() > 0) {
        drawLineCached(currentLine, rdLeft, currentY);
        charsDrawn += currentLine.length();
        currentLine = "";
        currentLineW = 0;
        pendingSpace = false;
        currentY += lineHeight + lineHeight / 2;  // 1.5x spacing for paragraph gap
      }
      if (currentY + lineHeight > rdBottom) {
        renderStopByte = i;
        break;
      }
      continue;
    }

    // Handle style markers (italic/bold/underline on/off)
    if (ch == STYLE_ITALIC_ON || ch == STYLE_ITALIC_OFF ||
        ch == STYLE_BOLD_ON || ch == STYLE_BOLD_OFF ||
        ch == STYLE_UNDERLINE_ON || ch == STYLE_UNDERLINE_OFF) {
      if (hasStyleVariants || ch == STYLE_UNDERLINE_ON || ch == STYLE_UNDERLINE_OFF) {
        currentLine += (char)ch;  // Embed in line for drawLineCached to process
      }
      i++;
      continue;
    }

    // Skip other control characters
    if (ch < 0x20) { i++; continue; }

    // Whitespace: mark pending space for next word
    if (ch == ' ' || ch == '\t') {
      while (i < (int)displayText.length()) {
        unsigned char wc = (unsigned char)displayText.charAt(i);
        if (wc == ' ' || wc == '\t') { i++; } else break;
      }
      if (currentLine.length() > 0) pendingSpace = true;
      continue;
    }

    // Collect a word (bytes until whitespace/control, including UTF-8 sequences)
    int wordStart = i;
    String word = "";
    while (i < (int)displayText.length()) {
      unsigned char wc = (unsigned char)displayText.charAt(i);
      if (wc == ' ' || wc == '\t' || wc == '\n' || wc == '\r' || wc == EPUB_IMG_MARKER || (wc < 0x20)) break;
      // Include UTF-8 lead byte + all continuation bytes as one unit
      if (wc >= 0x80) {
        int seqLen = 1;
        if      ((wc & 0xE0) == 0xC0) seqLen = 2;
        else if ((wc & 0xF0) == 0xE0) seqLen = 3;
        else if ((wc & 0xF8) == 0xF0) seqLen = 4;
        // Convert UTF-8 smart quotes/dashes to ASCII equivalents
        if (wc == 0xE2 && seqLen == 3 && i + 2 < (int)displayText.length() &&
            (unsigned char)displayText.charAt(i + 1) == 0x80) {
          unsigned char b3 = (unsigned char)displayText.charAt(i + 2);
          if (b3 == 0x98 || b3 == 0x99) {       // \u2018 \u2019 → '
            word += '\''; i += 3;
          } else if (b3 == 0x9C || b3 == 0x9D) { // \u201C \u201D → "
            word += '"'; i += 3;
          } else if (b3 == 0x93 || b3 == 0x94) { // \u2013 \u2014 → --
            word += '-'; word += '-'; i += 3;
          } else if (b3 == 0xA6) {               // \u2026 → ...
            word += '.'; word += '.'; word += '.'; i += 3;
          } else {
            for (int b = 0; b < seqLen && i < (int)displayText.length(); b++) {
              word += displayText.charAt(i); i++;
            }
          }
        } else {
          for (int b = 0; b < seqLen && i < (int)displayText.length(); b++) {
            word += displayText.charAt(i); i++;
          }
        }
      } else {
        word += (char)wc;
        i++;
      }
    }

    if (word.length() == 0) continue;

    // Measure word width using cached advance widths — decode UTF-8 codepoints
    int wordW = 0;
    {
      int mc = 0;
      while (mc < (int)word.length()) {
        uint32_t cp = decodeUTF8(word, mc);
        wordW += getCharAdvanceW(cp, fontSizePt);
      }
    }
    int neededW = wordW + (pendingSpace ? spaceW : 0);

    // Word wrap: if word doesn't fit, flush current line to display
    if (currentLineW + neededW > availW && currentLine.length() > 0) {
      drawLineCached(currentLine, rdLeft, currentY);
      charsDrawn += currentLine.length();
      currentLine = "";
      currentLineW = 0;
      pendingSpace = false;
      currentY += lineHeight;
      if (currentY + lineHeight > rdBottom) {
        renderStopByte = wordStart;
        i = wordStart;
        break;
      }
      // WDT + yield once per line
      yield();
      esp_task_wdt_reset();
    }

    // Append space separator if pending
    if (pendingSpace && currentLine.length() > 0) {
      currentLine += " ";
      currentLineW += spaceW;
    }
    pendingSpace = false;

    // Track where this line starts (for page offset correction)
    if (currentLine.length() == 0) lineStartByte = wordStart;

    // Record word position for dictionary tap lookup
    engRecordWord(rdLeft + currentLineW, currentY, wordW, lineHeight, word.c_str());

    // Append word to current line
    currentLine += word;
    currentLineW += wordW;
  }

  // Draw remaining text on last line
  if (currentLine.length() > 0) {
    drawLineCached(currentLine, rdLeft, currentY);
    charsDrawn += currentLine.length();
  }
  endHorizPage:

  // Page offset correction (same logic as vertical)
  if (renderStopByte < (int)displayText.length() && pageByteOffsets) {
    size_t correctedNextStart = currentPageByteOffset + renderStopByte;
    if (currentPage + 1 < pageOffsetsCount) {
      if (correctedNextStart != pageByteOffsets[currentPage + 1]) {
        pageByteOffsets[currentPage + 1] = correctedNextStart;
      }
    } else if (currentPage + 1 == pageOffsetsCount && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      pageByteOffsets[pageOffsetsCount] = correctedNextStart;
      pageOffsetsCount++;
    }
    if (currentPage < pageOffsetsCount) {
      pageByteOffsets[currentPage] = currentPageByteOffset;
    }
    if (currentPage + 1 >= pageOffsetsCount && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      pageByteOffsets[pageOffsetsCount] = correctedNextStart;
      pageOffsetsCount = currentPage + 2;
    }
  }

  // Progress bar
  {
    int barX = PROGRESS_BAR_X;
    int barY = 878;
    int barW = M5.Display.width() - 60;
    int barH = 4;
    float progress = (totalPages > 1) ? (float)(currentPage) / (totalPages - 1) : 1.0f;
    int fillW = (int)(barW * progress);
    M5.Display.drawRect(barX, barY, barW, barH, TFT_BLACK);
    if (fillW > 0) M5.Display.fillRect(barX, barY, fillW, barH, TFT_BLACK);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(progress * 100));
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(BR_DATUM);
    M5.Display.drawString(pctStr, barX + barW, barY - 6);
    M5.Display.setTextDatum(TL_DATUM);
    char pageStr[20];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage + 1, totalPages);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(BL_DATUM);
    M5.Display.drawString(pageStr, barX, barY - 6);
    M5.Display.setTextDatum(TL_DATUM);
  }

  // Toolbar bitmap: [−A] [size] [+A] [Aa] [≡] [★]  (6 cells, 312×50)
  {
    int btnRowY = 905;
    int tbX = 150;
    int cellW = 52;
    drawNavIcon("reader_toolbar.png", tbX, btnRowY);

    // Font size number in cell 1
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawNumber(readingFontSize, tbX + cellW + cellW / 2, btnRowY + 25);
    M5.Display.setTextDatum(TL_DATUM);

    // Bookmark highlight (★ cell)
    bool isBookmarked = false;
    for (int bi = 0; bi < bookmarkCount; bi++) {
      if (bookmarks[bi].page == currentPage) { isBookmarked = true; break; }
    }
    if (isBookmarked) {
      int starX = tbX + cellW * 5;
      M5.Display.fillRect(starX + 1, btnRowY + 1, cellW - 2, 48, TFT_BLACK);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString("*", starX + cellW / 2, btnRowY + 25);
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setTextDatum(TL_DATUM);
    }
  }

  M5.Display.endWrite();
  M5.Display.display();
  Serial.printf("Horizontal page %d rendered: %d chars drawn, heap=%u psram=%u\n",
                currentPage + 1, charsDrawn, ESP.getFreeHeap(), ESP.getFreePsram());
}
