#include "globals.h"

// Save current reading position to SD card (.pos file)
void saveReadingPosition() {
  if (!sdCardAvailable || currentBookPath.isEmpty()) return;
  String posPath = currentBookPath + ".pos";
  ScopedSDLock lock;
  File f = SD.open(posPath, FILE_WRITE);
  if (!f) return;
  f.printf("%d\n", currentPage);
  f.close();
  Serial.printf("Saved reading position: page %d to %s\n", currentPage, posPath.c_str());
}

// Load saved reading position from SD card (.pos file)
int loadReadingPosition() {
  if (!sdCardAvailable || currentBookPath.isEmpty()) return 0;
  String posPath = currentBookPath + ".pos";
  ScopedSDLock lock;
  if (!SD.exists(posPath)) return 0;
  File f = SD.open(posPath, FILE_READ);
  if (!f) return 0;
  String line = f.readStringUntil('\n');
  f.close();
  line.trim();
  int page = line.toInt();
  Serial.printf("Loaded reading position: page %d from %s\n", page, posPath.c_str());
  return page;
}

void saveBookmarks() {
  if (!sdCardAvailable || currentBookPath.isEmpty()) return;
  String bmPath = currentBookPath + ".bm";
  ScopedSDLock lock;
  File f = SD.open(bmPath, FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < bookmarkCount; i++) {
    f.printf("%d\n", bookmarks[i].page);
  }
  f.close();
  Serial.printf("Saved %d bookmarks to %s\n", bookmarkCount, bmPath.c_str());
}

// Load bookmarks for current book from SD card
void loadBookmarks() {
  bookmarkCount = 0;
  if (!sdCardAvailable || currentBookPath.isEmpty()) return;
  String bmPath = currentBookPath + ".bm";
  ScopedSDLock lock;
  if (!SD.exists(bmPath)) return;
  File f = SD.open(bmPath, FILE_READ);
  if (!f) return;
  while (f.available() && bookmarkCount < 5) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      bookmarks[bookmarkCount].page = line.toInt();
      char label[8];
      snprintf(label, sizeof(label), "P%d", bookmarks[bookmarkCount].page + 1);
      bookmarks[bookmarkCount].label = String(label);
      bookmarkCount++;
    }
  }
  f.close();
  Serial.printf("Loaded %d bookmarks from %s\n", bookmarkCount, bmPath.c_str());
}

// Add current page as bookmark (replaces oldest if full)
void addBookmark() {
  // Check if already bookmarked
  for (int i = 0; i < bookmarkCount; i++) {
    if (bookmarks[i].page == currentPage) return;  // Already exists
  }
  if (bookmarkCount >= 5) {
    // Shift left, drop oldest
    for (int i = 0; i < 4; i++) bookmarks[i] = bookmarks[i + 1];
    bookmarkCount = 4;
  }
  bookmarks[bookmarkCount].page = currentPage;
  char label[8];
  snprintf(label, sizeof(label), "P%d", currentPage + 1);
  bookmarks[bookmarkCount].label = String(label);
  bookmarkCount++;
  saveBookmarks();
}

void scanBooks() {
  bookCount = 0;
  if (!sdCardAvailable) {
    Serial.println("SD card not available");
    return;
  }
  
  Serial.println("Scanning /books directory...");
  
  // Add a delay before accessing SD to let scheduler stabilize
  delay(200);
  yield();
  
  File booksDir;
  
  // Use mutex to protect SD.open() call
  if (sdMutex != NULL) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    booksDir = SD.open("/books");
    xSemaphoreGive(sdMutex);
  } else {
    booksDir = SD.open("/books");
  }
  
  if (!booksDir) {
    Serial.println("Could not open /books directory");
    return;
  }
  
  Serial.println("Reading entries...");
  
  int count = 0;
  File entry = booksDir.openNextFile();
  
  while (entry && bookCount < MAX_BOOKS) {
    const char* name = entry.name();
    if (name) {
      String filename = String(name);
      Serial.printf("[%d] %s\n", count, name);
      
      // Skip dot files (e.g. ._mybook.epub from macOS AppleDouble)
      if (filename.startsWith(".") || filename.startsWith("._")) {
        Serial.printf("  Skipping dot file: %s\n", filename.c_str());
      } else if (filename.endsWith(".txt") || filename.endsWith(".TXT") ||
          filename.endsWith(".epub") || filename.endsWith(".EPUB")) {
        if (bookCount >= MAX_BOOKS) { Serial.println("Book list full, skipping remaining"); break; }
        bookList[bookCount] = filename;
        // Set display name: for EPUB extract embedded title, for TXT strip extension
        String lowerName = filename;
        lowerName.toLowerCase();
        if (lowerName.endsWith(".epub")) {
          String epubTitle = epubGetTitle("/books/" + filename);
          if (epubTitle.length() > 0) {
            bookDisplayName[bookCount] = epubTitle;
          } else {
            // Fallback: filename without extension
            bookDisplayName[bookCount] = filename.substring(0, filename.length() - 5);
          }
        } else {
          // TXT: strip extension
          int dotPos = filename.lastIndexOf('.');
          bookDisplayName[bookCount] = (dotPos > 0) ? filename.substring(0, dotPos) : filename;
        }
        Serial.printf("  Display name: %s\n", bookDisplayName[bookCount].c_str());
        bookCount++;
      }
    }
    
    entry.close();
    entry = booksDir.openNextFile();
    count++;
    
    // Frequent yields to let system handle interrupts
    if (count % 3 == 0) {
      yield();
      delayMicroseconds(100);
    }
  }
  
  booksDir.close();
  Serial.printf("Found %d books\n", bookCount);
}

void updateBytesPerPage() {
  int charH = readingFontSize + readingFontSize / 2;
  int colSp = readingFontSize + readingFontSize / 2;
  // Must match rendering: charsPerColumn uses VERTICAL_TEXT_MAX_Y, not READING_AREA_BOTTOM
  int charsPerCol = (VERTICAL_TEXT_MAX_Y - READING_AREA_TOP) / charH;
  int numCols = (READING_AREA_RIGHT - READING_AREA_LEFT) / colSp;
  int totalChars = charsPerCol * numCols;
  bytesPerPage = max(200, totalChars * 3 + 50);
  Serial.printf("Font size %d -> bytesPerPage %d (chars %d)\n", readingFontSize, bytesPerPage, totalChars);
}

void recalculatePages() {
  updateBytesPerPage();
  // Image-based EPUBs use chapter count, not byte-based pagination
  if (currentBookIsEpub && epubIsImageBased) {
    totalPages = epubChapterCount;
    return;
  }
  if (totalBookBytes > 0) {
    totalPages = (totalBookBytes / bytesPerPage) + 1;
  }
  // Reset page offset cache (will be rebuilt as pages are loaded)
  if (!pageByteOffsets) {
    pageByteOffsets = (size_t*)ps_malloc(MAX_PAGE_OFFSETS * sizeof(size_t));
  }
  if (pageByteOffsets) {
    pageByteOffsets[0] = 0;  // First page always starts at byte 0
    pageOffsetsCount = 1;
  }
}

bool loadCurrentPage() {
  if (currentBookPath.isEmpty()) return false;
  
  // Image-based EPUB (manga): load one chapter per page
  if (currentBookIsEpub && epubIsImageBased && epubChapters) {
    if (currentPage < 0) currentPage = 0;
    if (currentPage >= epubChapterCount) currentPage = epubChapterCount - 1;
    totalPages = epubChapterCount;
    
    if (!epubLoadSingleChapter(currentPage)) {
      Serial.printf("EPUB IMG: Failed to load chapter %d\n", currentPage + 1);
      return false;
    }
    
    currentPageContent = String(epubFullText);
    Serial.printf("EPUB IMG page %d/%d: %d bytes\n",
                  currentPage + 1, totalPages, currentPageContent.length());
    return true;
  }
  
  // Use tracked page offset if available, otherwise fall back to estimate
  size_t pageOffset;
  if (pageByteOffsets && currentPage < pageOffsetsCount) {
    pageOffset = pageByteOffsets[currentPage];
  } else {
    pageOffset = (size_t)currentPage * bytesPerPage;
  }
  if (pageOffset >= totalBookBytes) {
    // Clamp to last known valid page instead of wrapping to page 0
    if (pageByteOffsets && pageOffsetsCount > 1) {
      currentPage = pageOffsetsCount - 1;
      pageOffset = pageByteOffsets[currentPage];
      totalPages = currentPage + 1;  // We've reached the end
      Serial.printf("Page offset overshoot - clamped to page %d (offset %d)\n", currentPage, pageOffset);
    } else {
      currentPage = 0;
      pageOffset = 0;
    }
  }
  currentPageByteOffset = pageOffset;  // Store for rendering correction
  
  // EPUB: read from PSRAM buffer (with chapter windowing for big files)
  if (currentBookIsEpub && epubChapters) {
    // Check if the requested offset is within the currently loaded chapter window
    bool needReload = !epubFullText ||
                      pageOffset < epubLoadedBaseOffset ||
                      pageOffset >= epubLoadedBaseOffset + epubFullTextLen;

    if (needReload) {
      // Find which chapter contains this offset
      int targetChapter = epubChapterForOffset(pageOffset);
      Serial.printf("EPUB: Page offset %u outside loaded range [%u, %u), reloading from chapter %d\n",
                    pageOffset, epubLoadedBaseOffset,
                    epubLoadedBaseOffset + epubFullTextLen, targetChapter + 1);

      // Load chapters starting from one before the target (for backward navigation)
      int loadFrom = max(0, targetChapter - 1);
      if (!epubLoadChapterRange(loadFrom)) {
        Serial.println("EPUB: Failed to reload chapters");
        return false;
      }

      // After reloading, the totalBookBytes estimate may have changed
      totalBookBytes = epubEstimatedTotalBytes;

      // Reset page offset cache (invalidated by chapter reload)
      if (pageByteOffsets) {
        pageByteOffsets[0] = pageOffset;
        pageOffsetsCount = 1;
        // Recalculate current page number based on offset
        if (bytesPerPage > 0) {
          totalPages = (totalBookBytes / bytesPerPage) + 1;
        }
      }
    }

    if (!epubFullText || epubFullTextLen == 0) return false;

    // Calculate local offset within the loaded buffer
    size_t localOffset;
    if (pageOffset >= epubLoadedBaseOffset) {
      localOffset = pageOffset - epubLoadedBaseOffset;
    } else {
      localOffset = 0;  // Shouldn't happen after reload, but safety
    }

    if (localOffset >= epubFullTextLen) {
      // We've reached beyond what's loaded — clamp
      if (pageByteOffsets && pageOffsetsCount > 1) {
        currentPage = pageOffsetsCount - 1;
        localOffset = pageByteOffsets[currentPage] - epubLoadedBaseOffset;
        if (localOffset >= epubFullTextLen) localOffset = 0;
        totalPages = currentPage + 1;
      } else {
        localOffset = 0;
      }
    }

    size_t remaining = epubFullTextLen - localOffset;
    size_t bytesToRead = min((size_t)(bytesPerPage + UTF8_READ_PADDING), remaining);
    
    // Find safe UTF-8 boundary
    size_t safeEnd = bytesToRead;
    if (bytesToRead >= (size_t)bytesPerPage && bytesToRead < remaining) {
      safeEnd = bytesPerPage;
      while (safeEnd > 0 && (epubFullText[localOffset + safeEnd] & 0xC0) == 0x80) {
        safeEnd--;
      }
    }
    
    char saved = epubFullText[localOffset + safeEnd];
    epubFullText[localOffset + safeEnd] = '\0';
    currentPageContent = String(epubFullText + localOffset);
    epubFullText[localOffset + safeEnd] = saved;
    
    // Track next page's actual byte offset (using virtual/absolute offset)
    size_t virtualNextOffset = pageOffset + safeEnd;
    if (pageByteOffsets && currentPage + 1 == pageOffsetsCount && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      pageByteOffsets[pageOffsetsCount] = virtualNextOffset;
      pageOffsetsCount++;
      // Update totalPages estimate based on actual offsets
      if (virtualNextOffset < epubEstimatedTotalBytes) {
        size_t remainingBytes = epubEstimatedTotalBytes - virtualNextOffset;
        int remainingPages = (remainingBytes / bytesPerPage) + 1;
        totalPages = currentPage + 1 + remainingPages;
      } else {
        totalPages = currentPage + 1;
      }
    }
    
    Serial.printf("EPUB page %d: %d bytes at offset %d (local %d, ch %d-%d)\n",
                  currentPage + 1, currentPageContent.length(), pageOffset,
                  localOffset, epubLoadedStartChapter + 1, epubLoadedEndChapter);
    return true;
  }
  
  // Plain text: read from SD card file
  // Read page content (extra bytes to avoid cutting UTF-8 chars)
  size_t bytesToRead = min((size_t)(bytesPerPage + UTF8_READ_PADDING), totalBookBytes - pageOffset);
  char* buffer = (char*)malloc(bytesToRead + 1);
  if (!buffer) {
    Serial.println("Failed to allocate page buffer");
    return false;
  }
  
  size_t bytesRead = 0;
  {
    ScopedSDLock lock;
    File file = SD.open(currentBookPath.c_str());
  
    if (!file) {
      Serial.println("Failed to open book for page load");
      free(buffer);
      return false;
    }
  
    // Seek to page position
    file.seek(pageOffset);
    bytesRead = file.read((uint8_t*)buffer, bytesToRead);
    file.close();
  }  // ScopedSDLock released here
  buffer[bytesRead] = '\0';
  
  // Find safe UTF-8 boundary (don't cut multi-byte characters)
  size_t safeEnd = bytesRead;
  if (bytesRead >= (size_t)bytesPerPage && bytesRead < bytesToRead) {
    safeEnd = bytesRead;
  } else if (bytesRead >= (size_t)bytesPerPage) {
    safeEnd = bytesPerPage;
    while (safeEnd > 0 && (buffer[safeEnd] & 0xC0) == 0x80) {
      safeEnd--;
    }
  }
  
  buffer[safeEnd] = '\0';
  currentPageContent = String(buffer);
  free(buffer);
  
  // Track next page's actual byte offset (only when extending sequentially)
  if (pageByteOffsets && currentPage + 1 == pageOffsetsCount && pageOffsetsCount < MAX_PAGE_OFFSETS) {
    pageByteOffsets[pageOffsetsCount] = pageOffset + safeEnd;
    pageOffsetsCount++;
    // Update totalPages estimate based on actual offsets
    if (pageOffset + safeEnd < totalBookBytes) {
      size_t remainingBytes = totalBookBytes - (pageOffset + safeEnd);
      int remainingPages = (remainingBytes / bytesPerPage) + 1;
      totalPages = currentPage + 1 + remainingPages;
    } else {
      totalPages = currentPage + 1;
    }
  }
  
  Serial.printf("Loaded page %d: %d bytes at offset %d\n", currentPage + 1, currentPageContent.length(), pageOffset);
  return true;
}

bool loadBook(int bookIndex) {
  if (!sdCardAvailable || bookIndex >= bookCount) return false;
  
  // bookList contains the actual filenames (can be Chinese UTF-8)
  String filename = bookList[bookIndex];
  currentBookPath = "/books/" + filename;
  Serial.printf("Opening book: %s\n", currentBookPath.c_str());
  
  // Check if EPUB
  String lowerName = filename;
  lowerName.toLowerCase();
  if (lowerName.endsWith(".epub")) {
    currentBookIsEpub = true;
    if (!epubLoad(currentBookPath)) {
      Serial.println("EPUB: Failed to load");
      currentBookIsEpub = false;
      return false;
    }
    // Use estimated total for page calculation (covers all chapters, not just loaded ones)
    if (epubIsImageBased) {
      // Image-based (manga): each chapter = one page
      totalPages = epubChapterCount;
      totalBookBytes = epubChapterCount;  // Not used for page calc but keep consistent
      Serial.printf("EPUB: Image-based mode — %d pages (1 chapter per page)\n", totalPages);
    } else {
      totalBookBytes = epubEstimatedTotalBytes;
      recalculatePages();
    }
    currentPage = loadReadingPosition();
    if (currentPage >= totalPages) currentPage = 0;
    loadBookmarks();
    return loadCurrentPage();
  }
  
  // Plain text file
  currentBookIsEpub = false;
  epubCleanup();  // Free any leftover EPUB data when switching to plain text
  
  {
    ScopedSDLock lock;
    File file = SD.open(currentBookPath.c_str());
    if (!file) {
      Serial.println("File not found");
      return false;
    }
    totalBookBytes = file.size();
    file.close();
  }
  
  recalculatePages();
  currentPage = loadReadingPosition();
  if (currentPage >= totalPages) currentPage = 0;
  loadBookmarks();
  
  // Load first page
  return loadCurrentPage();
}

void drawBookList() {
  Serial.println("Drawing book list...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();
  
  Serial.println("Waiting for screen clear...");
  delay(100);
  
  M5.Display.setTextColor(TFT_BLACK);
  
  // Draw title
  drawSystemText("電子書列表", 20, 30, 36);
  
  M5.Display.drawLine(20, 80, 520, 80, TFT_BLACK);
  
  if (sdCardAvailable && bookCount > 0) {
    // Show books from SD card
    for (int i = 0; i < min(bookCount, 10); i++) {
      drawSystemText(bookDisplayName[i].c_str(), 40, 120 + (i * 50), 24);
    }
    String bookInfo = "共 " + String(bookCount) + " 本書 | 觸控選擇";
    drawSystemText(bookInfo.c_str(), 20, 900, 18);
  } else {
    // Show sample books
    drawSystemText("示例書籍1.txt", 40, 120, 24);
    drawSystemText("示例書籍2.txt", 40, 170, 24);
    drawSystemText("示例書籍3.txt", 40, 220, 24);
    drawSystemText("觸控選擇書籍", 20, 860, 18);
  }
  
  Serial.println("Calling display()...");
  M5.Display.endWrite();
  M5.Display.display();
  
  delay(500);  // Brief wait for e-ink refresh
  Serial.println("Book list displayed");
}

void drawReading() {
  Serial.println("Drawing reading mode...");
  
  // Load the user-selected reading font (may differ from system font)
  loadReadingFont();
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Status bar + nav bar first
  drawStatusBar();
  {
    bool hasPrev = (currentPage > 0);
    bool hasNext = (currentPage < totalPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);
  }
  
  delay(100);
  
  // Determine which font renderer to use
  // Priority: OFR TTF > Binary font > Built-in
  enum FontRenderer { FONT_BUILTIN, FONT_BINFONT, FONT_OFR };
  FontRenderer renderer = FONT_BUILTIN;
  
  if (ofrFontLoaded) {
    renderer = FONT_OFR;
    Serial.printf("Using OpenFontRender TTF: %s\n", currentFontFile.c_str());
  } else if (g_binFont.loaded) {
    renderer = FONT_BINFONT;
    Serial.println("Using binary font (MingLiU.bin)");
  } else {
    Serial.println("Using fonts::lgfxJapanMincho_28 (Built-in)");
    M5.Display.setFont(&fonts::lgfxJapanMincho_28);
    M5.Display.setTextSize(1.4);
    M5.Display.setTextColor(TFT_BLACK);
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
  
  String sampleText = displayText;
  
  // Vertical text rendering: right-to-left columns, top-to-bottom characters
  int fontSizePt;
  if (renderer == FONT_OFR) {
    fontSizePt = readingFontSize;  // Use adjustable font size
    ofr.setFontSize(fontSizePt);
  } else if (renderer == FONT_BINFONT) {
    fontSizePt = g_binFont.fontSize;
  } else {
    fontSizePt = DEFAULT_READING_FONT_SIZE;
  }
  
  int charHeight = fontSizePt + (fontSizePt / 2);  // ~1.5x font size for spacing
  int columnSpacing = fontSizePt + (fontSizePt / 2);
  int charsPerColumn = (VERTICAL_TEXT_MAX_Y - READING_AREA_TOP) / charHeight;
  int columnX = READING_AREA_RIGHT - columnSpacing;
  int startY = READING_AREA_TOP;
  
  int charIndex = 0;
  int currentY = startY;
  
  Serial.printf("Font renderer: %d, fontSize: %d, charHeight: %d\n", renderer, fontSizePt, charHeight);
  int charsDrawn = 0;
  
  int renderStopByte = sampleText.length();  // Track actual bytes consumed by rendering
  
  for (int i = 0; i < (int)sampleText.length(); ) {
    // Check for nav touch every 10 characters
    if (charsDrawn > 0 && charsDrawn % 10 == 0) {
      if (checkNavTouch()) {
        Serial.println("Nav touch during reading render - aborting");
        return;
      }
    }
    
    // Get one UTF-8 character and its Unicode codepoint
    int charStart = i;
    uint32_t unicode = utf8Decode(sampleText, i);
    String ch = sampleText.substring(charStart, i);
    applyVerticalPunct(ch, unicode);
    
    // Skip carriage returns and control characters
    if (unicode == '\r') continue;
    if (unicode < 0x20 && unicode != '\n' && unicode != EPUB_IMG_MARKER) continue;
    if (unicode == 0x3000) continue;  // Ideographic space
    // Skip ASCII spaces (paragraph indentation is handled by column breaks)
    if (unicode == ' ') continue;
    
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
        int imgX = READING_AREA_LEFT;
        int imgY = READING_AREA_TOP;
        int imgW = READING_AREA_RIGHT - READING_AREA_LEFT;   // Full width: 470
        int imgH = READING_AREA_BOTTOM - READING_AREA_TOP;   // Full height: 790
        
        if (epubExtractAndDrawImage(imgPath, imgX, imgY, imgW, imgH)) {
          // Image drawn — this page is done, break to next page
          renderStopByte = i;
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
      if (charIndex > 0) {  // Only if current column has content
        columnX -= columnSpacing;
        currentY = startY;
        charIndex = 0;
        if (columnX < READING_AREA_LEFT) {
          renderStopByte = i;
          break;
        }
      }
      continue;
    }
    
    // Draw character using the selected renderer
    if (renderer == FONT_OFR) {
      // OpenFontRender: draw single character string at position
      // Center the character in the column
      int drawX = columnX;
      ofr.setCursor(drawX - fontSizePt / 2, currentY);
      ofr.setFontSize(fontSizePt);
      ofr.setFontColor(TFT_BLACK, TFT_WHITE);
      uint16_t drawn = ofr.cdrawString(ch.c_str(), drawX, currentY, TFT_BLACK, TFT_WHITE);
      if (drawn > 0) {
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
    } else if (renderer == FONT_BINFONT && ch.length() > 0) {
      GlyphIndex* glyph = findGlyph(unicode);
      if (glyph && glyph->width > 0) {
        if (charsDrawn < 5) {
          Serial.printf("Drawing char %d: U+%04X at x=%d y=%d\n", charsDrawn, unicode, columnX - glyph->width/2, currentY);
        }
        if (drawBinFontChar(unicode, columnX - glyph->width/2, currentY)) {
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
      // Built-in font
      M5.Display.setFont(&fonts::lgfxJapanMincho_28);
      M5.Display.setTextSize(1.4);
      M5.Display.setCursor(columnX - 15, currentY);
      M5.Display.print(ch);
      charsDrawn++;
      currentY += charHeight;
      charIndex++;
    }
    
    // Move to next column when column is full
    if (charIndex >= charsPerColumn || currentY > READING_AREA_BOTTOM) {
      columnX -= columnSpacing;
      currentY = startY;
      charIndex = 0;
      
      if (columnX < READING_AREA_LEFT) {
        renderStopByte = i;  // Record where rendering actually stopped
        break;
      }
    }
  }
  endPageRender:  // Jump target for image rendering page break
  
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
      // Fill gap: mark current page's actual offset
      while (pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
        pageByteOffsets[pageOffsetsCount] = (size_t)pageOffsetsCount * bytesPerPage;  // estimates for gaps
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
    // Page number above bar on the left
    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage + 1, totalPages);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(BL_DATUM);
    M5.Display.drawString(pageStr, barX, barY - 4);
    // Percentage above bar on the right
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(progress * 100));
    M5.Display.setTextDatum(BR_DATUM);
    M5.Display.drawString(pctStr, barX + barW, barY - 4);
    M5.Display.setTextDatum(TL_DATUM);
  }
  
  // Font size buttons (below progress bar)
  int btnRowY = 910;
  M5.Display.drawRect(155, btnRowY, 45, 40, TFT_BLACK);
  drawSystemText("字-", 160, btnRowY + 6);
  
  // Current font size indicator
  M5.Display.setFont(&fonts::lgfxJapanMincho_24);
  M5.Display.setTextSize(0.6);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(207, btnRowY + 15);
  M5.Display.printf("%d", readingFontSize);
  
  M5.Display.drawRect(230, btnRowY, 45, 40, TFT_BLACK);
  drawSystemText("字+", 235, btnRowY + 6);
  
  // Font selection button
  M5.Display.drawRect(280, btnRowY, 45, 40, TFT_BLACK);
  drawSystemText("字型", 284, btnRowY + 6, 20);
  
  // Bookmark button
  {
    bool isBookmarked = false;
    for (int i = 0; i < bookmarkCount; i++) {
      if (bookmarks[i].page == currentPage) { isBookmarked = true; break; }
    }
    M5.Display.drawRect(330, btnRowY, 45, 40, TFT_BLACK);
    if (isBookmarked) {
      M5.Display.fillRect(331, btnRowY + 1, 43, 38, TFT_BLACK);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString("BM", 352, btnRowY + 20);
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setTextDatum(TL_DATUM);
    } else {
      drawSystemText("BM", 340, btnRowY + 6, 20);
    }
  }
  
  Serial.println("Calling display()...");
  M5.Display.endWrite();
  M5.Display.display();
  
  delay(500);  // Brief wait for e-ink refresh
  Serial.println("Reading mode displayed");
}
