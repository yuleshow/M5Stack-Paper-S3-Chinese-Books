#include "globals.h"
#include "esp_task_wdt.h"

// Draw vertical Chinese/CJK text reading mode.
// Called from drawReading() after common preamble (screen clear, status bar).
// Expects: startWrite() already called, screen cleared, status bar drawn.
// Reading font already loaded via loadReadingFont() before startWrite().
void drawChineseReading() {
  // Nav bar
  {
    bool hasPrev = (currentPage > 0);
    bool hasNext = (currentPage < totalPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);
  }
  
  // Determine which font renderer to use based on user's font selection
  // (not ofrFontLoaded, since OFR may also hold a Latin font like EBGaramond)
  enum FontRenderer { FONT_BUILTIN, FONT_BINFONT, FONT_OFR };
  FontRenderer renderer = FONT_BUILTIN;
  
  {
    String activeFont = (readingFontFile.length() > 0) ? readingFontFile :
        ((readingFontIndex >= 0 && readingFontIndex < fontFileCount) ? fontFileList[readingFontIndex] : String(""));
    bool isBinFile = activeFont.endsWith(".bin") || activeFont.endsWith(".BIN");
    if (isBinFile && g_binFont.loaded) {
      renderer = FONT_BINFONT;
      Serial.println("Using binary font (BIN)");
    } else if (ofrFontLoaded) {
      renderer = FONT_OFR;
      Serial.printf("Using OpenFontRender TTF: %s\n", currentFontFile.c_str());
    } else if (g_binFont.loaded) {
      renderer = FONT_BINFONT;
      Serial.println("Using binary font (BIN) fallback");
    } else {
      Serial.println("No custom font loaded - built-in fallback");
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_BLACK);
    }
  }
  
  // Use current page content or sample text
  String displayText = (currentPageContent.length() > 0) ? currentPageContent :
                      "這是一個中文電子書閱讀器的測試文字"
                      "傳統中文書籍採用直式排版"
                      "文字由上而下由右而左排列"
                      "這種閱讀方式已經使用了數千年"
                      "每一列從右邊開始向左移動"
                      "每一個字從上方開始向下書寫"
                      "閱讀時視線自然地從右向左掃視"
                      "這就是傳統中文的優雅排版方式"
                      "可以使用觸控螢幕來翻頁"
                      "左側觸控是上一頁的功能"
                      "右側觸控是下一頁的功能"
                      "頂部左角有返回按鈕"
                      "可以返回到書籍列表頁面"
                      "這個閱讀器支持從SD卡載入書籍"
                      "將書籍放入SD卡的books資料夾"
                      "檔案格式必須是UTF8編碼的txt文字檔"
                      "系統會自動掃描並顯示所有書籍"
                      "目前正在顯示的是示範文字"
                      "用來展示直式排版的效果";
  
  Serial.printf("Rendering page %d/%d, content: %d bytes\n", currentPage + 1, totalPages, displayText.length());
  // Dump first bytes for diagnostics (hex for control chars, chars for printable)
  {
    int dumpLen = min(80, (int)displayText.length());
    Serial.printf("Page content (first %d bytes): ", dumpLen);
    for (int d = 0; d < dumpLen; d++) {
      unsigned char c = (unsigned char)displayText.charAt(d);
      if (c >= 0x20 && c < 0x7F) Serial.printf("%c", c);
      else Serial.printf("\\x%02X", c);
    }
    Serial.println();
  }
  
  String sampleText = displayText;
  
  // Vertical text rendering: right-to-left columns, top-to-bottom characters
  int fontSizePt;
  bool silverReading = isReadingFontSilver();
  if (renderer == FONT_OFR) {
    fontSizePt = silverReading ? silverScaledSize(readingFontSize) : readingFontSize;
    ofr.setFontSize(fontSizePt);
    ofr.setFontColor(TFT_BLACK, TFT_WHITE);
  } else if (renderer == FONT_BINFONT) {
    fontSizePt = silverReading ? silverScaledSize(readingFontSize) : readingFontSize;
    // Load a font into OFR for Latin text rendering.
    // GenYoMinTW renders English poorly — use embedded ET Book (from flash).
    // Use readingFontSize (not fontSizePt) so fallback matches Silver's visual size
    int ofrFallbackSize = readingFontSize;
    bool latinFontLoaded = false;
    if (systemFontChoice == 0) {
      latinFontLoaded = loadEmbeddedETBook(ofrFallbackSize);
    }
    if (!latinFontLoaded) {
      loadSystemFont();
    }
  } else {
    fontSizePt = DEFAULT_READING_FONT_SIZE;
  }
  
  int charHeight, columnSpacing;
  int rdLeft, rdRight, rdTop, rdMaxY;
  // Use nominal readingFontSize for layout so all fonts at same size have same grid
  int layoutSize = readingFontSize;
  charHeight = layoutSize + (layoutSize / 5);  // ~1.2x font size for spacing
  columnSpacing = layoutSize + (layoutSize / 5);
  rdLeft = READING_AREA_LEFT; rdRight = READING_AREA_RIGHT;
  rdTop = READING_AREA_TOP;   rdMaxY = VERTICAL_TEXT_MAX_Y;
  // Safety: guard against zero char dimensions (would cause div-by-zero or infinite loop)
  if (charHeight < 1) charHeight = 1;
  if (columnSpacing < 1) columnSpacing = 1;
  // Optimize: squeeze one more column if leftover space >= 40% of column width
  {
    int availW = rdRight - rdLeft;
    int numCols = availW / columnSpacing;
    int leftover = availW - numCols * columnSpacing;
    if (numCols > 0 && leftover * 5 >= columnSpacing * 2) {
      numCols++;
      columnSpacing = availW / numCols;
    }
  }
  int charsPerColumn = (rdMaxY - rdTop) / charHeight - 1;
  // Ensure kinsoku overflow slot stays above progress bar / page numbers
  if (rdTop + (charsPerColumn + 1) * charHeight > READING_AREA_BOTTOM)
    charsPerColumn--;
  if (charsPerColumn < 1) charsPerColumn = 1;
  int columnX = rdRight - columnSpacing / 2;
  // Anchor first row's em-square top at rdTop regardless of font size
  int startY = rdTop - (charHeight - fontSizePt) / 2;
  
  // BinFont scale factor: ratio of desired size to native bitmap size
  float binScale = (renderer == FONT_BINFONT && g_binFont.fontSize > 0) ?
    (float)fontSizePt / (float)g_binFont.fontSize : 1.0f;

  int charIndex = 0;
  int currentY = startY;
  bool lastWasSpace = true;   // Start true to skip leading spaces/U+3000 (paragraph indent)
  bool pageHasImage = false;  // Track if this page rendered a cover/inline image
  int indentCount = 0;        // Track paragraph indent spaces rendered (for paragraphIndent mode)
  
  Serial.printf("Font renderer: %d, fontSize: %d, charHeight: %d, silverReading: %d\n", renderer, fontSizePt, charHeight, silverReading);
  Serial.printf("Layout: rdLeft=%d rdRight=%d rdTop=%d rdMaxY=%d charsPerCol=%d columnX=%d\n",
                rdLeft, rdRight, rdTop, rdMaxY, charsPerColumn, columnX);
  int charsDrawn = 0;
  yield();
  esp_task_wdt_reset();
  
  // OFR font size for fallback rendering (Latin runs, punctuation, etc.)
  // For BIN fonts, use readingFontSize so fallback matches the BIN's visual size
  int ofrRenderSize = (renderer == FONT_BINFONT) ? readingFontSize : fontSizePt;
  
  int renderStopByte = sampleText.length();  // Track actual bytes consumed by rendering
  int loopSafety = 0;  // Safety counter to prevent infinite loops
  
  for (int i = 0; i < (int)sampleText.length(); ) {
    // Safety: detect infinite loop (no byte progress)
    if (++loopSafety > (int)sampleText.length() + 1000) {
      Serial.printf("HALT SAFETY: loop stuck at i=%d, breaking\n", i);
      renderStopByte = i;
      break;
    }
    // Check for nav touch every 50 characters
    if (charsDrawn > 0 && charsDrawn % 50 == 0) {
      yield();
      esp_task_wdt_reset();
      if (checkNavTouch()) {
        Serial.println("Nav touch during reading render - aborting");
        M5.Display.endWrite();  // Must close write transaction before returning
        M5.Display.display();
        return;
      }
    }
    
    // Get one UTF-8 character and its Unicode codepoint
    int charStart = i;
    uint32_t unicode = utf8Decode(sampleText, i);
    
    // Skip carriage returns and control characters
    if (unicode == '\r') continue;
    if (unicode < 0x20 && unicode != '\n' && unicode != EPUB_IMG_MARKER) continue;
    // Collapse consecutive spaces into one; render as blank cell
    if (unicode == 0x3000 || unicode == ' ') {
      // When paragraphIndent is enabled, allow up to 2 U+3000 at paragraph start
      if (paragraphIndent && unicode == 0x3000 && indentCount < 2) {
        indentCount++;
        currentY += charHeight;
        charIndex++;
        charsDrawn++;
        if (currentY + charHeight > rdMaxY || charIndex >= charsPerColumn) {
          columnX -= columnSpacing;
          currentY = startY;
          charIndex = 0;
          if (columnX - columnSpacing / 2 < rdLeft) {
            renderStopByte = i;
            break;
          }
        }
        continue;
      }
      if (lastWasSpace) continue;  // Skip consecutive spaces
      lastWasSpace = true;
      // Render space as empty cell (advance position without drawing)
      currentY += charHeight;
      charIndex++;
      charsDrawn++;
      // Check column overflow
      if (currentY + charHeight > rdMaxY || charIndex >= charsPerColumn) {
        columnX -= columnSpacing;
        currentY = startY;
        charIndex = 0;
        if (columnX - columnSpacing / 2 < rdLeft) {
          renderStopByte = i;
          break;
        }
      }
      continue;
    }
    lastWasSpace = false;

    // Detect chapter heading "第XXX回/章/節/篇/卷" → force page break (next page starts at 第)
    // Applied to all books: TXT, EPUBs with/without TOC.
    // Only triggers when charsDrawn > 0, so it won't break if 第 is already the first char.
    if (unicode == 0x7B2C && charsDrawn > 0) {  // 第
      int scanPos = i;
      bool hasNumber = false;
      while (scanPos < (int)sampleText.length()) {
        uint32_t numCp = utf8Decode(sampleText, scanPos);
        bool isCJKNum = (numCp == 0x4E00 || numCp == 0x4E8C || numCp == 0x4E09 || numCp == 0x56DB ||
                         numCp == 0x4E94 || numCp == 0x516D || numCp == 0x4E03 || numCp == 0x516B ||
                         numCp == 0x4E5D || numCp == 0x5341 || numCp == 0x767E || numCp == 0x5343 ||
                         numCp == 0x96F6 || numCp == 0x3007);
        bool isDigit = (numCp >= '0' && numCp <= '9') ||
                       (numCp >= 0xFF10 && numCp <= 0xFF19);
        if (isCJKNum || isDigit) {
          hasNumber = true;
        } else if (hasNumber &&
                   (numCp == 0x56DE ||   // 回
                    numCp == 0x7AE0 ||   // 章
                    numCp == 0x7BC0 ||   // 節
                    numCp == 0x7BC7 ||   // 篇
                    numCp == 0x5377)) {  // 卷
          renderStopByte = charStart;  // Next page starts at 第
          columnX = rdLeft - columnSpacing;
          Serial.printf("Chapter break: 第...%c at byte %d\n", (char)numCp, charStart);
          break;
        } else {
          break;
        }
      }
      if (columnX - columnSpacing / 2 < rdLeft) break;
    }
    
    // Latin text run: collect consecutive printable ASCII chars and render rotated 90° CW
    // Only trigger for runs containing at least one letter (skip lone digits/punctuation)
    // Must run BEFORE halfToFullWidth so ASCII values are preserved for detection
    // Disabled: English books use horizontal path; Chinese books render ASCII as individual chars
    if (false && unicode >= 0x21 && unicode <= 0x7E) {
      // Peek ahead to check if there's at least one letter in the run
      bool hasLetter = (unicode >= 'A' && unicode <= 'Z') || (unicode >= 'a' && unicode <= 'z');
      if (!hasLetter) {
        int peek_i = i;
        while (peek_i < (int)sampleText.length()) {
          unsigned char pc = (unsigned char)sampleText.charAt(peek_i);
          if ((pc >= 'A' && pc <= 'Z') || (pc >= 'a' && pc <= 'z')) { hasLetter = true; break; }
          if (pc >= 0x21 && pc <= 0x7E) { peek_i++; continue; }
          if (pc == ' ' && peek_i + 1 < (int)sampleText.length() &&
              (unsigned char)sampleText.charAt(peek_i + 1) >= 0x21 &&
              (unsigned char)sampleText.charAt(peek_i + 1) <= 0x7E) { peek_i++; continue; }
          break;
        }
      }
      if (!hasLetter) {
        // No letters — treat as normal character (draw vertically as-is)
        goto draw_normal_char;
      }
      // Rewind to collect the full run (we already decoded one char)
      i = charStart;
      int runStart = i;
      String latinRun = "";
      // Cap collection: only collect ~2x what a column can hold to avoid O(n²) splitting.
      // At font size F, each char is roughly F*0.6 pixels wide; after 90° rotation,
      // the run width becomes the column height. maxColumnH ≈ 792 pixels at default layout.
      int maxLatinChars = max(20, (rdMaxY - rdTop) / max(1, fontSizePt / 3) + 10);
      while (i < (int)sampleText.length() && (int)latinRun.length() < maxLatinChars) {
        unsigned char peek = (unsigned char)sampleText.charAt(i);
        if (peek >= 0x21 && peek <= 0x7E) {
          latinRun += (char)peek;
          i++;
        } else if (peek == ' ' && i + 1 < (int)sampleText.length() &&
                   (unsigned char)sampleText.charAt(i + 1) >= 0x21 &&
                   (unsigned char)sampleText.charAt(i + 1) <= 0x7E) {
          // Include spaces between Latin words
          latinRun += ' ';
          i++;
        } else {
          break;
        }
      }
      if (latinRun.length() == 0) continue;

      // Measure text width using OFR
      ofr.setFontSize(ofrRenderSize);
      uint32_t textW = ofr.getTextWidth(latinRun.c_str());
      int spriteW = (int)textW + 8;
      int spriteH = ofrRenderSize + 4;
      int rotatedH = spriteW;  // After 90° rotation, width becomes height
      int maxColumnH = rdMaxY - startY - charHeight;  // Max height available in a fresh column

      // If the run is too long even for a fresh column, split at word boundary
      if (rotatedH > maxColumnH && maxColumnH > 0) {
        String bestFit = "";
        int bestEnd = 0;
        // Binary search for the longest prefix that fits (O(log n) instead of O(n))
        int lo = 0, hi = latinRun.length();
        while (lo < hi) {
          int mid = (lo + hi + 1) / 2;
          // Find the nearest space at or before mid
          int splitAt = mid;
          while (splitAt > lo && latinRun.charAt(splitAt) != ' ') splitAt--;
          if (splitAt <= lo) {
            // No space between lo and mid — try the raw position
            String sub = latinRun.substring(0, mid);
            uint32_t subW = ofr.getTextWidth(sub.c_str());
            if ((int)subW + 8 <= maxColumnH) {
              lo = mid;
            } else {
              hi = mid - 1;
            }
          } else {
            String sub = latinRun.substring(0, splitAt);
            uint32_t subW = ofr.getTextWidth(sub.c_str());
            if ((int)subW + 8 <= maxColumnH) {
              bestFit = sub;
              bestEnd = splitAt + 1;
              lo = splitAt + 1;
            } else {
              hi = splitAt - 1;
            }
          }
          yield();
          esp_task_wdt_reset();
        }
        if (bestFit.length() > 0) {
          latinRun = bestFit;
          i = runStart + bestEnd;
        } else {
          // No word boundary works — force split by character count
          // Use proportional estimate then verify
          int estChars = max(1, (int)((long)latinRun.length() * maxColumnH / rotatedH));
          // Shrink until it fits
          while (estChars > 1) {
            String sub = latinRun.substring(0, estChars);
            uint32_t subW = ofr.getTextWidth(sub.c_str());
            if ((int)subW + 8 <= maxColumnH) break;
            estChars = estChars * 3 / 4;  // Reduce by 25%
          }
          if (estChars < 1) estChars = 1;
          latinRun = latinRun.substring(0, estChars);
          i = runStart + estChars;
        }
        textW = ofr.getTextWidth(latinRun.c_str());
        spriteW = (int)textW + 8;
        rotatedH = spriteW;
      }

      // Check if the rotated text fits in the remaining column space
      if (currentY + rotatedH > rdMaxY - charHeight) {
        // Doesn't fit — move to next column and try again
        columnX -= columnSpacing;
        currentY = startY;
        charIndex = 0;
        if (columnX - columnSpacing / 2 < rdLeft) {
          renderStopByte = runStart;  // Reprocess this run on next page
          i = runStart;
          break;
        }
      }

      // Render into sprite, then push rotated
      // OFR always has a font loaded here (reading font or system font)
      LGFX_Sprite sprite(&M5.Display);
      sprite.setPsram(true);  // Use PSRAM — internal heap may be tight with SD TTF fonts
      if (sprite.createSprite(spriteW, spriteH)) {
        sprite.fillSprite(TFT_WHITE);
        ofr.setDrawer(sprite);
        ofr.setFontSize(ofrRenderSize);
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
        ofr.cdrawString(latinRun.c_str(), spriteW / 2, 2, TFT_BLACK, TFT_WHITE);
        int destX = columnX;
        int destY = currentY + rotatedH / 2;
        sprite.pushRotateZoom(&M5.Display, destX, destY, 90, 1.0, 1.0);
        sprite.deleteSprite();
        ofr.setDrawer(M5.Display);
      }

      currentY += rotatedH;
      // Count equivalent character slots for the Latin run (not raw char count)
      int slotsUsed = (rotatedH + charHeight - 1) / charHeight;
      charIndex += slotsUsed;
      charsDrawn += latinRun.length();
      lastWasSpace = true;  // Suppress blank cell if space follows the run

      // Check column overflow
      if (charIndex >= charsPerColumn || currentY + charHeight > rdMaxY) {
        columnX -= columnSpacing;
        currentY = startY;
        charIndex = 0;
        if (columnX - columnSpacing / 2 < rdLeft) {
          renderStopByte = i;
          break;
        }
      }
      continue;
    }
    
    // Handle EPUB image markers: \x01<image_path>\x01
    if (unicode == EPUB_IMG_MARKER && currentBookIsEpub) {
      // Parse image path until next marker
      int pathStart = i;
      while (i < (int)sampleText.length() && sampleText.charAt(i) != EPUB_IMG_MARKER) i++;
      if (i < (int)sampleText.length()) {
        String imgPath = sampleText.substring(pathStart, i);
        i++;  // Skip closing marker
        
        Serial.printf("EPUB IMG marker found: '%s'\n", imgPath.c_str());
        
        // Use the full reading area for the image (not just remaining columns)
        int imgX = rdLeft;
        int imgY = rdTop;
        int imgW = rdRight - rdLeft;
        int imgH = rdMaxY - rdTop;
        
        // Release display bus during SD I/O + image decode to avoid SPI conflicts
        M5.Display.endWrite();
        esp_task_wdt_reset();
        Serial.printf("EPUB IMG: extracting '%s'...\n", imgPath.c_str());
        unsigned long imgStart = millis();
        bool imgDrawn = epubExtractAndDrawImage(imgPath, imgX, imgY, imgW, imgH);
        Serial.printf("EPUB IMG: extraction took %lu ms, drawn=%d\n", millis() - imgStart, imgDrawn);
        esp_task_wdt_reset();
        M5.Display.startWrite();
        
        if (imgDrawn) {
          pageHasImage = true;
          // Image drawn — this page is done, break to next page
          renderStopByte = i;
          Serial.printf("EPUB IMG: page %d endPageRender at renderStopByte=%d of %d, offset=%d, freePSRAM=%u\n",
                        currentPage, renderStopByte, (int)sampleText.length(),
                        (int)currentPageByteOffset, ESP.getFreePsram());
          goto endPageRender;
        } else {
          // Image failed — show placeholder text
          Serial.printf("EPUB: Image not rendered: %s\n", imgPath.c_str());
        }
      }
      continue;
    }
    
    // Hard newline → start a new column (paragraph break in vertical text)
    if (unicode == '\n') {
      lastWasSpace = true;  // Skip leading spaces/U+3000 at start of new column (paragraph indent)
      indentCount = 0;      // Reset indent counter for new paragraph
      if (charIndex > 0) {  // Only if current column has content
        columnX -= columnSpacing;
        currentY = startY;
        charIndex = 0;
        if (columnX - columnSpacing / 2 < rdLeft) {
          renderStopByte = i;
          break;
        }
      }
      continue;
    }
    
    // Convert half-width punctuation to full-width for CJK rendering
    // (only lone ASCII punctuation reaches here — Latin runs with letters are handled above)
    unicode = halfToFullWidth(unicode);
    // Apply vertical punctuation mapping
    unicode = toVerticalPunct(unicode);

    // Draw character using the selected renderer
    draw_normal_char:
    // Vertically center each glyph within its charHeight cell
    int vOffset = (charHeight - fontSizePt) / 2;
    if (renderer == FONT_OFR) {
      // OpenFontRender with PSRAM glyph cache — FreeType runs only on first encounter
      int drawY = currentY + vOffset;
      bool drawn = drawOFRCharCached(unicode, columnX - fontSizePt / 2, drawY, TFT_BLACK, fontSizePt);
      if (drawn) {
        charsDrawn++;
        currentY += charHeight;
        charIndex++;
      } else {
        // Character not in font, skip
        if (charsDrawn < 10) {
          Serial.printf("OFR: skip U+%04X (not rendered)\n", unicode);
        }
        continue;
      }
    } else if (renderer == FONT_BINFONT) {
      GlyphIndex* glyph = findGlyph(unicode);
      if (glyph && glyph->width > 0) {
        // Position glyph within em-square using bearing offsets (v2) or center fallback (v1)
        int emY = currentY + (charHeight - fontSizePt) / 2;  // em-square top edge
        int emX = columnX - fontSizePt / 2;                   // em-square left edge
        int scaledW = (int)(glyph->width * binScale + 0.5f);
        int scaledH = (int)(glyph->height * binScale + 0.5f);
        int drawX, drawY;
        if (g_binFont.version >= 2) {
          drawX = emX + (int)(glyph->bearingX * binScale + 0.5f);
          drawY = emY + (int)(glyph->bearingY * binScale + 0.5f);
        } else {
          drawX = columnX - scaledW / 2;
          drawY = emY + (fontSizePt - scaledH) / 2;
        }
        if (charsDrawn < 5) {
          Serial.printf("Drawing char %d: U+%04X at x=%d y=%d scale=%.2f bearing=(%d,%d)\n", charsDrawn, unicode, drawX, drawY, binScale, glyph->bearingX, glyph->bearingY);
        }
        if (drawBinFontChar(unicode, drawX, drawY, TFT_BLACK, binScale)) {
          charsDrawn++;
          currentY += charHeight;
          charIndex++;
        }
      } else {
        if (unicode > 0x20 && charsDrawn < 10) {
          Serial.printf("Skipping U+%04X (not in font)\n", unicode);
        }
        continue;
      }
    } else {
      // Built-in font fallback (ASCII only, CJK won't render)
      char chBuf2[5];
      int chLen2 = utf8Encode(unicode, chBuf2);
      chBuf2[chLen2] = '\0';
      int drawY = currentY + vOffset;
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setCursor(columnX - 8, drawY);
      M5.Display.print(chBuf2);
      charsDrawn++;
      currentY += charHeight;
      charIndex++;
    }
    
    // Move to next column when column is full
    if (charIndex >= charsPerColumn || currentY > rdMaxY) {
      // Kinsoku (禁則處理): peek at next character — if it's a punctuation mark
      // that should not start a new column, pull it into the reserved bottom slot
      if (i < (int)sampleText.length()) {
        int peekI = i;
        uint32_t peekUnicode = utf8Decode(sampleText, peekI);
        uint32_t mappedPeek = toVerticalPunct(peekUnicode);
        if (isColumnStartProhibited(peekUnicode) || isColumnStartProhibited(mappedPeek)) {
          String peekCh = sampleText.substring(i, peekI);
          applyVerticalPunct(peekCh, mappedPeek);

          // Draw single prohibited char at normal size in reserved slot
          int vOff = (charHeight - fontSizePt) / 2;
          if (renderer == FONT_OFR) {
            drawOFRCharCached(mappedPeek, columnX - fontSizePt / 2, currentY + vOff, TFT_BLACK, fontSizePt);
          } else if (renderer == FONT_BINFONT) {
            GlyphIndex* glyph = findGlyph(mappedPeek);
            if (glyph && glyph->width > 0) {
              int emY = currentY + (charHeight - fontSizePt) / 2;
              int emX = columnX - fontSizePt / 2;
              int kx, ky;
              if (g_binFont.version >= 2) {
                kx = emX + (int)(glyph->bearingX * binScale + 0.5f);
                ky = emY + (int)(glyph->bearingY * binScale + 0.5f);
              } else {
                int kScaledW = (int)(glyph->width * binScale + 0.5f);
                int kScaledH = (int)(glyph->height * binScale + 0.5f);
                kx = columnX - kScaledW / 2;
                ky = emY + (fontSizePt - kScaledH) / 2;
              }
              drawBinFontChar(mappedPeek, kx, ky, TFT_BLACK, binScale);
            }
          } else {
            M5.Display.setFont(&fonts::Font2);
            M5.Display.setTextSize(1);
            M5.Display.setCursor(columnX - 8, currentY + vOff);
            M5.Display.print(peekCh);
          }
          charsDrawn++;
          i = peekI;  // Consume the peeked character
        }
      }
      columnX -= columnSpacing;
      currentY = startY;
      charIndex = 0;
      
      if (columnX - columnSpacing / 2 < rdLeft) {
        renderStopByte = i;  // Record where rendering actually stopped
        break;
      }
    }
  }
  endPageRender:  // Jump target for image rendering page break
  
  // OFR Latin font (ET Book embedded) is intentionally kept loaded — avoids
  // re-initialisation on every page turn.  BIN font stays in
  // memory regardless; renderer selection uses readingFontFile extension.
  
  // Redraw nav bar on top of image — image rendering may have overlapped the nav area
  if (pageHasImage) {
    lastPageWasImage = true;
    bool hasPrev = (currentPage > 0);
    bool hasNext = (currentPage < totalPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);
    Serial.printf("EPUB IMG: Redrawn nav bar (hasPrev=%d, hasNext=%d, page=%d/%d)\n",
                  hasPrev, hasNext, currentPage, totalPages);
  } else {
    lastPageWasImage = false;
  }
  
  // Correct next page byte offset based on actual bytes rendered
  // This prevents text from being skipped between pages
  // Skip for image-based EPUBs (they use chapter-index pagination instead of byte offsets)
  if (!epubIsImageBased && renderStopByte < (int)sampleText.length() && pageByteOffsets) {
    size_t correctedNextStart = currentPageByteOffset + renderStopByte;
    
    // Always store/extend the corrected offset for the next page
    if (currentPage + 1 < pageOffsetsCount) {
      // Update existing tracked offset
      if (correctedNextStart != pageByteOffsets[currentPage + 1]) {
        Serial.printf("Page offset correction: page %d offset %d -> %d (rendered %d of %d bytes)\n",
                      currentPage + 1, (int)pageByteOffsets[currentPage + 1], (int)correctedNextStart,
                      renderStopByte, (int)sampleText.length());
        pageByteOffsets[currentPage + 1] = correctedNextStart;
      }
    } else if (currentPage + 1 == pageOffsetsCount && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      // Extend offset array — next page wasn't tracked yet
      pageByteOffsets[pageOffsetsCount] = correctedNextStart;
      pageOffsetsCount++;
      Serial.printf("Page offset extended: page %d offset %d (rendered %d of %d bytes)\n",
                    currentPage + 1, (int)correctedNextStart, renderStopByte, (int)sampleText.length());
    }
    
    // Also store current page offset if not yet tracked (e.g., jumped to saved page)
    if (currentPage >= pageOffsetsCount) {
      // Fill gap: estimate backward from the known current page offset
      // This is more accurate than N*bytesPerPage when early pages consume few bytes (e.g., cover images)
      while (pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
        int gap = currentPage - pageOffsetsCount;
        size_t est = (currentPageByteOffset > (size_t)(gap + 1) * bytesPerPage) ?
                     currentPageByteOffset - (size_t)(gap + 1) * bytesPerPage : 0;
        pageByteOffsets[pageOffsetsCount] = est;
        pageOffsetsCount++;
      }
    }
    if (currentPage < pageOffsetsCount) {
      pageByteOffsets[currentPage] = currentPageByteOffset;
    }
    // Extend for next page if needed after filling gaps
    if (currentPage + 1 >= pageOffsetsCount && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      pageByteOffsets[pageOffsetsCount] = correctedNextStart;
      pageOffsetsCount = currentPage + 2;
      Serial.printf("Page offset gap-filled: page %d offset %d\n",
                    currentPage + 1, (int)correctedNextStart);
    }
  }
  
  // Reading progress bar (thin bar above buttons)
  {
    int barX = PROGRESS_BAR_X;
    int barY = 878;  // Clear gap below text (850) and above buttons (910)
    int barW = M5.Display.width() - 60;  // 480px wide
    int barH = 4;
    float progress = (totalPages > 1) ? (float)(currentPage) / (totalPages - 1) : 1.0f;
    int fillW = (int)(barW * progress);
    M5.Display.drawRect(barX, barY, barW, barH, TFT_BLACK);
    if (fillW > 0) {
      M5.Display.fillRect(barX, barY, fillW, barH, TFT_BLACK);
    }
    // Percentage above bar on the right
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(progress * 100));
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(BR_DATUM);
    M5.Display.drawString(pctStr, barX + barW, barY - 6);
    M5.Display.setTextDatum(TL_DATUM);
    // Page number on the left side above progress bar (avoid overlapping font buttons)
    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage + 1, totalPages);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(BL_DATUM);
    M5.Display.drawString(pageStr, barX, barY - 6);
    M5.Display.setTextDatum(TL_DATUM);
  }
  
  // Toolbar bitmap: [−A] [size] [+A] [Aa] [≡] [★]  (6 cells, 312×50)
  // Drawn at (150, 905), each cell 52px wide
  int btnRowY = 905;
  int tbX = 150;     // toolbar X origin
  int cellW = 52;    // cell width (312/6)
  drawNavIcon("reader_toolbar.png", tbX, btnRowY);
  
  // Dynamic font size number in cell 1 (size display)
  {
    int sizeCell = tbX + cellW;  // x=202
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawNumber(readingFontSize, sizeCell + cellW / 2, btnRowY + 25);
    M5.Display.setTextDatum(TL_DATUM);
  }
  
  // Bookmark highlight overlay (fill ★ cell when bookmarked)
  {
    bool isBookmarked = false;
    for (int i = 0; i < bookmarkCount; i++) {
      if (bookmarks[i].page == currentPage) { isBookmarked = true; break; }
    }
    if (isBookmarked) {
      int starX = tbX + cellW * 5;  // x=410
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
  
  Serial.println("Calling display()...");
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Reading mode displayed");
}
