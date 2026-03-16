#include "globals.h"
#include "esp_task_wdt.h"

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
          // Sanity check: skip very large EPUBs during boot scan (title extraction)
          File epubCheck;
          String epubCheckPath = "/books/" + filename;
          if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); epubCheck = SD.open(epubCheckPath.c_str()); xSemaphoreGive(sdMutex); }
          else { epubCheck = SD.open(epubCheckPath.c_str()); }
          bool skipTitle = false;
          if (epubCheck) {
            size_t epubSize = epubCheck.size();
            epubCheck.close();
            if (epubSize < 100 || epubSize > 100UL * 1024 * 1024) {
              Serial.printf("  Skipping title extraction: file size %u too large/small\n", epubSize);
              skipTitle = true;
            }
          } else {
            skipTitle = true;
          }
          String epubTitle = "";
          if (!skipTitle) {
            epubTitle = epubGetTitle("/books/" + filename);
          }
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
    
    // Periodic yield to let system handle interrupts
    if (count % 10 == 0) {
      yield();
    }
  }
  
  booksDir.close();
  Serial.printf("Found %d books\n", bookCount);
}

// Check if the current reading font is Silver
static bool isReadingFontSilver() {
  if (readingFontIndex >= 0 && readingFontIndex < fontFileCount) {
    String fname = fontFileList[readingFontIndex];
    fname.toLowerCase();
    return fname.indexOf("silver") >= 0;
  }
  // No reading font selected — falls back to system font
  return systemFontChoice == 1;
}

void updateBytesPerPage() {
  bool silverReading = isReadingFontSilver();
  int renderSize = silverReading ? silverScaledSize(readingFontSize) : readingFontSize;
  if (renderSize < 1) renderSize = DEFAULT_READING_FONT_SIZE;  // safety
  int charH, colSp;
  int areaTop, areaLeft, areaRight, maxY;
  if (silverReading) {
    charH = renderSize;  // Silver: no extra gap
    colSp = renderSize;
    areaTop = SILVER_AREA_TOP; areaLeft = SILVER_AREA_LEFT;
    areaRight = SILVER_AREA_RIGHT; maxY = SILVER_MAX_Y;
  } else {
    charH = renderSize + renderSize / 5;  // 1.2x font size
    colSp = renderSize + renderSize / 5;
    areaTop = READING_AREA_TOP; areaLeft = READING_AREA_LEFT;
    areaRight = READING_AREA_RIGHT; maxY = VERTICAL_TEXT_MAX_Y;
  }
  if (charH < 1) charH = 1;
  if (colSp < 1) colSp = 1;
  int charsPerCol = (maxY - areaTop) / charH;
  int numCols = (areaRight - areaLeft) / colSp;
  int totalChars = charsPerCol * numCols;
  bytesPerPage = max(200, totalChars * 3 + 50);
  Serial.printf("Font size %d (render %d, silver=%d) -> bytesPerPage %d (chars %d = %d×%d)\n",
                readingFontSize, renderSize, silverReading, bytesPerPage, totalChars, charsPerCol, numCols);
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
  
  // Image-based EPUB (manga): one chapter per page, show first image
  if (currentBookIsEpub && epubIsImageBased && epubChapters) {
    if (currentPage < 0) currentPage = 0;
    if (currentPage >= epubChapterCount) currentPage = epubChapterCount - 1;
    totalPages = epubChapterCount;
    
    if (!epubLoadSingleChapter(currentPage)) {
      Serial.printf("EPUB IMG: Failed to load chapter %d\n", currentPage + 1);
      return false;
    }
    
    currentPageContent = String(epubFullText);
    // Verify image marker presence for diagnostics
    int markerIdx = currentPageContent.indexOf(EPUB_IMG_MARKER);
    Serial.printf("EPUB IMG page %d/%d: %d bytes, marker at %d, freePSRAM=%u\n",
                  currentPage + 1, totalPages, currentPageContent.length(),
                  markerIdx, ESP.getFreePsram());
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

    // Safety: check if localOffset lands inside a \x01...\x01 image marker pair
    // Count marker bytes from buffer start to detect odd/even parity
    if (localOffset > 0 && localOffset < epubFullTextLen) {
      int markerCount = 0;
      for (size_t k = 0; k < localOffset; k++) {
        if (epubFullText[k] == EPUB_IMG_MARKER) markerCount++;
      }
      if (markerCount & 1) {
        // Odd markers = we're inside a marker pair, skip past closing marker
        while (localOffset < epubFullTextLen && epubFullText[localOffset] != EPUB_IMG_MARKER)
          localOffset++;
        if (localOffset < epubFullTextLen) localOffset++;  // skip the marker
        if (localOffset < epubFullTextLen && epubFullText[localOffset] == '\n') localOffset++;
        Serial.printf("EPUB: Adjusted offset past image marker boundary (was mid-path)\n");
      }
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

String lastLoadError = "";  // Stores the specific failure reason for on-screen display

bool loadBook(int bookIndex) {
  lastLoadError = "";
  Serial.printf("\n=== loadBook(%d) === Free heap: %u, Free PSRAM: %u, OFR files: %d\n",
                bookIndex, ESP.getFreeHeap(), ESP.getFreePsram(), (int)ofr_file_list.size());
  if (!sdCardAvailable || bookIndex >= bookCount) {
    Serial.printf("loadBook: early exit - sdCardAvailable=%d, bookIndex=%d, bookCount=%d\n",
                  sdCardAvailable, bookIndex, bookCount);
    lastLoadError = "SD/index err";
    return false;
  }
  comicZoomQuadrant = -1;  // Reset zoom state

  // Clean up any previous book state before loading new one
  Serial.printf("loadBook: cleanup - epubFullText=%p, epubZipEntries=%p, epubChapters=%p\n",
                epubFullText, epubZipEntries, epubChapters);
  epubCleanup();
  // Unload OFR font to free its SD file handle — prevents SD bus contention
  // during heavy EPUB chapter loading. Will be reloaded after loading completes.
  if (ofrFontLoaded) {
    ofr.unloadFont();
    ofrFontLoaded = false;
    Serial.printf("loadBook: unloaded OFR font, ofr_file_list.size=%d\n", (int)ofr_file_list.size());
  }
  currentBookIsEpub = false;
  currentPageContent = "";
  currentPage = 0;
  totalPages = 1;
  totalBookBytes = 0;
  pageOffsetsCount = 0;
  currentPageByteOffset = 0;
  bookmarkCount = 0;
  Serial.printf("loadBook: after cleanup - Free heap: %u, Free PSRAM: %u\n",
                ESP.getFreeHeap(), ESP.getFreePsram());

  // bookList contains the actual filenames (can be Chinese UTF-8)
  String filename = bookList[bookIndex];
  currentBookPath = "/books/" + filename;
  Serial.printf("Opening book: %s\n", currentBookPath.c_str());
  
  // Verify file exists and is readable before loading
  {
    ScopedSDLock lock;
    File checkFile = SD.open(currentBookPath.c_str());
    if (!checkFile) {
      Serial.printf("loadBook: file not found on SD: %s\n", currentBookPath.c_str());
      char detail[60];
      snprintf(detail, sizeof(detail), "找不到: %.40s", filename.c_str());
      lastLoadError = String(detail);
      return false;
    }
    size_t fsize = checkFile.size();
    checkFile.close();
    Serial.printf("loadBook: file verified, size=%u bytes\n", fsize);
    if (fsize < 100) {
      lastLoadError = "file too small";
      return false;
    }
  }
  
  // Check if EPUB
  String lowerName = filename;
  lowerName.toLowerCase();
  if (lowerName.endsWith(".epub")) {
    currentBookIsEpub = true;
    if (!epubLoad(currentBookPath)) {
      Serial.println("EPUB: Failed to load");
      if (lastLoadError.isEmpty()) lastLoadError = "epubLoad fail";
      currentBookIsEpub = false;
      return false;
    }
    // Use estimated total for page calculation (covers all chapters, not just loaded ones)
    if (epubIsImageBased) {
      // Image-based (manga): each chapter = one page
      totalPages = epubChapterCount;
      totalBookBytes = epubChapterCount;  // Not used for page calc but keep consistent
      Serial.printf("EPUB: Image-based mode — %d pages (1 chapter per page)\n", totalPages);
      
      // Show warning for multi-image chapters — wait for tap to dismiss
      if (epubHasMultiImageChapters) {
        M5.Display.setEpdMode(epd_mode_t::epd_fast);
        M5.Display.startWrite();
        M5.Display.fillScreen(TFT_WHITE);
        drawSystemText("此EPUB每頁含多張圖片", 60, 380, 32);
        drawSystemText("僅顯示每頁第一張圖片", 60, 430, 32);
        drawSystemText("點擊螢幕繼續", 160, 520, 24);
        M5.Display.endWrite();
        M5.Display.display();
        // Wait for touch
        while (true) {
          M5.update();
          auto t = M5.Touch.getDetail();
          if (t.wasPressed()) break;
          delay(50);
        }
      }
    } else {
      totalBookBytes = epubEstimatedTotalBytes;
      recalculatePages();
    }
    currentPage = loadReadingPosition();
    Serial.printf("EPUB: Restored page=%d, totalPages=%d, isImageBased=%d, chapterCount=%d\n",
                  currentPage, totalPages, epubIsImageBased, epubChapterCount);
    if (currentPage >= totalPages) currentPage = 0;
    loadBookmarks();
    yield();
    esp_task_wdt_reset();
    if (!loadCurrentPage()) { lastLoadError = "epub loadPage"; return false; }
    Serial.println("EPUB: loadCurrentPage OK, ready to draw");
    return true;
  }
  
  // Plain text file
  {
    ScopedSDLock lock;
    File file = SD.open(currentBookPath.c_str());
    if (!file) {
      Serial.println("File not found");
      lastLoadError = "txt open fail";
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
  if (!loadCurrentPage()) { lastLoadError = "txt loadPage"; return false; }
  return true;
}

void drawBookList() {
  Serial.println("Drawing book list...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();
  
  M5.Display.setTextColor(TFT_BLACK);
  
  // Draw title
  drawSystemText("電子書列表", 20, 30, 36);
  
  M5.Display.drawLine(20, 80, 520, 80, TFT_BLACK);
  
  if (sdCardAvailable && bookCount > 0) {
    int totalBookPages = (bookCount + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
    if (bookListPage >= totalBookPages) bookListPage = totalBookPages - 1;
    if (bookListPage < 0) bookListPage = 0;
    int startIdx = bookListPage * BOOKS_PER_PAGE;
    int endIdx = min(startIdx + BOOKS_PER_PAGE, bookCount);
    
    // Show books for current page
    for (int i = startIdx; i < endIdx; i++) {
      int row = i - startIdx;
      // Truncate long names to fit display width (540px - 40px left margin - 20px right margin)
      String displayName = bookDisplayName[i];
      const int maxDisplayWidth = DISPLAY_WIDTH - 40 - 20;  // 480px
      // Fast truncation: estimate max UTF-8 characters that fit, then verify once
      // At font size 28: CJK chars ~28px wide, ASCII ~14px wide
      // Conservative estimate: assume all chars are 16px wide → ~30 chars max
      int len = displayName.length();
      if (len > 30) {
        // Count UTF-8 characters
        int charCount = 0;
        int bytePos = 0;
        while (bytePos < len) {
          uint8_t c = (uint8_t)displayName[bytePos];
          if (c < 0x80) bytePos += 1;
          else if (c < 0xE0) bytePos += 2;
          else if (c < 0xF0) bytePos += 3;
          else bytePos += 4;
          charCount++;
        }
        if (charCount > 17) {
          // Likely too long — truncate to ~16 UTF-8 chars and add ellipsis
          int targetChars = 16;
          bytePos = 0;
          int count = 0;
          while (bytePos < len && count < targetChars) {
            uint8_t c = (uint8_t)displayName[bytePos];
            if (c < 0x80) bytePos += 1;
            else if (c < 0xE0) bytePos += 2;
            else if (c < 0xF0) bytePos += 3;
            else bytePos += 4;
            count++;
          }
          displayName = displayName.substring(0, bytePos) + "\xe2\x80\xa6";  // "…"
        }
      }
      drawSystemText(displayName.c_str(), 40, 120 + (row * BOOK_ROW_HEIGHT), 28);
    }
    
    // Draw pagination nav arrows if needed
    bool hasPrev = (bookListPage > 0);
    bool hasNext = (bookListPage < totalBookPages - 1);
    if (totalBookPages > 1) {
      drawVerticalNavBar(hasPrev, hasNext);
    }
    
    // Status info
    String bookInfo = "共 " + String(bookCount) + " 本書";
    if (totalBookPages > 1) {
      bookInfo += " | 第 " + String(bookListPage + 1) + "/" + String(totalBookPages) + " 頁";
    }
    drawSystemText(bookInfo.c_str(), 20, 840, 28);
  } else {
    // Show sample books
    drawSystemText("示例書籍1.txt", 40, 120, 28);
    drawSystemText("示例書籍2.txt", 40, 175, 28);
    drawSystemText("示例書籍3.txt", 40, 230, 28);
    drawSystemText("觸控選擇書籍", 20, 840, 28);
  }
  
  Serial.println("Calling display()...");
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Book list displayed");
}

// Determine e-ink refresh mode based on pageRefreshMode setting
static epd_mode_t getReadingEpdMode() {
  if (pageRefreshMode == 1) {
    // Mode 1: always quality (slowest, cleanest)
    return epd_mode_t::epd_quality;
  }
  // Mode 0 and 2: fast with periodic quality refresh every 10 pages for cleanup
  if (pagesSinceFullRefresh >= 10) {
    pagesSinceFullRefresh = 0;
  }
  if (pagesSinceFullRefresh == 0) {
    pagesSinceFullRefresh++;
    return epd_mode_t::epd_quality;
  }
  pagesSinceFullRefresh++;
  return epd_mode_t::epd_fast;
}

void drawReading() {
  Serial.printf("\n=== drawReading() === heap=%u, psram=%u, ofrLoaded=%d, ofrFiles=%d\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), (int)ofrFontLoaded, (int)ofr_file_list.size());
  Serial.println("Drawing reading mode...");
  
  // Common screen setup for all reading paths
  M5.Display.setEpdMode(getReadingEpdMode());
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  drawStatusBar();
  
  // Comic zoom mode: draw zoomed quadrant fullscreen
  if (currentBookIsEpub && epubIsImageBased && comicZoomQuadrant >= 0) {
    String displayText = currentPageContent;
    // Find the image marker in the page content
    int markerPos = displayText.indexOf(EPUB_IMG_MARKER);
    if (markerPos >= 0) {
      int pathStart = markerPos + 1;
      int pathEnd = displayText.indexOf(EPUB_IMG_MARKER, pathStart);
      if (pathEnd > pathStart) {
        String imgPath = displayText.substring(pathStart, pathEnd);
        // Draw the zoomed view using full screen
        epubExtractAndDrawImage(imgPath, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                comicZoomQuadrant, comicZoomCX, comicZoomCY);
      }
    }
    
    // Redraw status bar on top of fullscreen image
    drawStatusBar();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // Image-based EPUB full view: render image directly, skip text loop
  // This avoids showing stray text (e.g. filenames) that may precede image markers
  if (currentBookIsEpub && epubIsImageBased) {
    // Only draw arrows and return button (no progress bar / font buttons for comic)
    drawReturnButton();
    {
      bool hasPrev = (currentPage > 0);
      bool hasNext = (currentPage < totalPages - 1);
      // Draw arrows only, nav bar already includes return
      if (hasNext) drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
      if (hasPrev) drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
    }
    // Page number centered, vertically aligned with arrows
    {
      char pageStr[16];
      snprintf(pageStr, sizeof(pageStr), "%d / %d", currentPage + 1, totalPages);
      drawSystemText(pageStr, 180, NAV_Y + (NAV_ICON_SIZE - 24) / 2, 24);
    }
    // Comic zoom mode toggle button
    {
      int btnX = 350;
      int btnY = NAV_Y;
      int btnW = 80, btnH = 64;
      M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_BLACK);
      const char* zoomLabel = (comicZoomMode == 1) ? "自由" : "四分";
      // Manually center: measure text width then draw at calculated x
      loadSystemFont();
      int tx = btnX + (btnW - (int)getSystemTextWidth(zoomLabel, 24)) / 2;
      int ty = btnY + btnH / 2 - 12;
      drawSystemText(zoomLabel, tx, ty, 24);
    }
    
    String displayText = currentPageContent;
    int markerPos = displayText.indexOf(EPUB_IMG_MARKER);
    if (markerPos >= 0) {
      int pathStart = markerPos + 1;
      int pathEnd = displayText.indexOf(EPUB_IMG_MARKER, pathStart);
      if (pathEnd > pathStart) {
        String imgPath = displayText.substring(pathStart, pathEnd);
        Serial.printf("EPUB IMG full view: page %d/%d, image='%s'\n",
                      currentPage + 1, totalPages, imgPath.c_str());
        int imgX = READING_AREA_LEFT;
        int imgY = READING_AREA_TOP;
        int imgW = READING_AREA_RIGHT - READING_AREA_LEFT;
        int imgH = READING_AREA_BOTTOM - READING_AREA_TOP;
        if (!epubExtractAndDrawImage(imgPath, imgX, imgY, imgW, imgH)) {
          Serial.printf("EPUB IMG: Failed to draw image for page %d\n", currentPage + 1);
          drawSystemText("圖片載入失敗", 60, 400, 24);
        }
      }
    } else {
      Serial.printf("EPUB IMG: No image marker found in page %d content (%d bytes)\n",
                    currentPage + 1, displayText.length());
      drawSystemText("此頁無圖片內容", 60, 400, 24);
    }
    
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // Load the user-selected reading font (may differ from system font)
  Serial.printf("drawReading: before loadReadingFont, ofrFontLoaded=%d, currentFontFile=%s, heap=%u, psram=%u, ofrFiles=%d\n",
               ofrFontLoaded, currentFontFile.c_str(), ESP.getFreeHeap(), ESP.getFreePsram(), (int)ofr_file_list.size());
  yield();
  esp_task_wdt_reset();
  bool fontOk = loadReadingFont();
  Serial.printf("drawReading: after loadReadingFont, ok=%d, ofrFontLoaded=%d, currentFontFile=%s, ofrFiles=%d\n",
               fontOk, ofrFontLoaded, currentFontFile.c_str(), (int)ofr_file_list.size());
  yield();
  esp_task_wdt_reset();
  
  // Nav bar
  {
    bool hasPrev = (currentPage > 0);
    bool hasNext = (currentPage < totalPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);
  }
  
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
    Serial.println("No custom font loaded - built-in fallback");
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
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
  bool silverReading = isReadingFontSilver();
  if (renderer == FONT_OFR) {
    fontSizePt = silverReading ? silverScaledSize(readingFontSize) : readingFontSize;
    ofr.setFontSize(fontSizePt);
    ofr.setFontColor(TFT_BLACK, TFT_WHITE);
  } else if (renderer == FONT_BINFONT) {
    fontSizePt = readingFontSize;
  } else {
    fontSizePt = DEFAULT_READING_FONT_SIZE;
  }
  
  int charHeight, columnSpacing;
  // Silver font uses tighter layout with reduced margins
  int rdLeft, rdRight, rdTop, rdMaxY;
  if (silverReading) {
    charHeight = fontSizePt;      // Silver: no extra gap
    columnSpacing = fontSizePt;
    rdLeft = SILVER_AREA_LEFT; rdRight = SILVER_AREA_RIGHT;
    rdTop = SILVER_AREA_TOP;  rdMaxY = SILVER_MAX_Y;
  } else {
    charHeight = fontSizePt + (fontSizePt / 5);  // ~1.2x font size for spacing
    columnSpacing = fontSizePt + (fontSizePt / 5);
    rdLeft = READING_AREA_LEFT; rdRight = READING_AREA_RIGHT;
    rdTop = READING_AREA_TOP;   rdMaxY = VERTICAL_TEXT_MAX_Y;
  }
  // Safety: guard against zero char dimensions (would cause div-by-zero or infinite loop)
  if (charHeight < 1) charHeight = 1;
  if (columnSpacing < 1) columnSpacing = 1;
  int charsPerColumn = (rdMaxY - rdTop) / charHeight - 2;
  if (charsPerColumn < 1) charsPerColumn = 1;
  int columnX = rdRight - columnSpacing / 2;
  int startY = rdTop;
  
  int charIndex = 0;
  int currentY = startY;
  
  Serial.printf("Font renderer: %d, fontSize: %d, charHeight: %d, silverReading: %d\n", renderer, fontSizePt, charHeight, silverReading);
  Serial.printf("Layout: rdLeft=%d rdRight=%d rdTop=%d rdMaxY=%d charsPerCol=%d columnX=%d\n",
                rdLeft, rdRight, rdTop, rdMaxY, charsPerColumn, columnX);
  int charsDrawn = 0;
  yield();
  esp_task_wdt_reset();
  
  // BinFont scale factor: ratio of desired size to native bitmap size
  float binScale = (renderer == FONT_BINFONT && g_binFont.fontSize > 0) ?
    (float)fontSizePt / (float)g_binFont.fontSize : 1.0f;
  
  int renderStopByte = sampleText.length();  // Track actual bytes consumed by rendering
  
  for (int i = 0; i < (int)sampleText.length(); ) {
    // Check for nav touch every 50 characters
    if (charsDrawn > 0 && charsDrawn % 50 == 0) {
      yield();
      esp_task_wdt_reset();
      if (checkNavTouch()) {
        Serial.println("Nav touch during reading render - aborting");
        return;
      }
    }
    
    // Get one UTF-8 character and its Unicode codepoint
    int charStart = i;
    uint32_t unicode = utf8Decode(sampleText, i);
    // Apply vertical punctuation mapping (unicode only; String built lazily when needed)
    unicode = toVerticalPunct(unicode);
    
    // Skip carriage returns and control characters
    if (unicode == '\r') continue;
    if (unicode < 0x20 && unicode != '\n' && unicode != EPUB_IMG_MARKER) continue;
    if (unicode == 0x3000) continue;  // Ideographic space
    // Skip ASCII spaces (paragraph indentation is handled by column breaks)
    if (unicode == ' ') continue;
    
    // Latin text run: collect consecutive printable ASCII chars and render rotated 90° CW
    // Only trigger for runs containing at least one letter (skip lone digits/punctuation)
    if (unicode >= 0x21 && unicode <= 0x7E && renderer == FONT_OFR) {
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
      while (i < (int)sampleText.length()) {
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
      ofr.setFontSize(fontSizePt);
      uint32_t textW = ofr.getTextWidth(latinRun.c_str());
      int spriteW = (int)textW + 4;
      int spriteH = fontSizePt + 4;
      int rotatedH = spriteW;  // After 90° rotation, width becomes height

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
      LGFX_Sprite sprite(&M5.Display);
      if (sprite.createSprite(spriteW, spriteH)) {
        sprite.fillSprite(TFT_WHITE);
        ofr.setDrawer(sprite);
        ofr.setFontSize(fontSizePt);
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
        ofr.cdrawString(latinRun.c_str(), spriteW / 2, 2, TFT_BLACK, TFT_WHITE);
        // pushRotateZoom(dest, pivotX, pivotY, angle, scaleX, scaleY)
        // 90° CW rotation: pivot at center of where we want it on screen
        int destX = columnX;
        int destY = currentY + rotatedH / 2;
        sprite.pushRotateZoom(&M5.Display, destX, destY, 90, 1.0, 1.0);
        sprite.deleteSprite();
        ofr.setDrawer(M5.Display);  // Restore OFR drawer to main display
      }

      currentY += rotatedH;
      charIndex += latinRun.length();
      charsDrawn += latinRun.length();

      // Check column overflow
      if (charIndex >= charsPerColumn || currentY > rdMaxY) {
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
        if (columnX - columnSpacing / 2 < rdLeft) {
          renderStopByte = i;
          break;
        }
      }
      continue;
    }
    
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
        // Position glyph within em-square using bearing offsets (v2) or center (v1)
        int emX = columnX - fontSizePt / 2;  // em-square left edge
        int emY = currentY + (charHeight - fontSizePt) / 2;  // em-square top edge
        int drawX, drawY;
        if (glyph->bearingX != 0 || glyph->bearingY != 0) {
          // v2: use font bearing offsets, scaled to target size
          drawX = emX + (int)(glyph->bearingX * binScale);
          drawY = emY + (int)(glyph->bearingY * binScale);
        } else {
          // v1 fallback: center scaled glyph within em-square
          int scaledW = (int)(glyph->width * binScale + 0.5f);
          int scaledH = (int)(glyph->height * binScale + 0.5f);
          drawX = columnX - scaledW / 2;
          drawY = emY + (fontSizePt - scaledH) / 2;
        }
        if (charsDrawn < 5) {
          Serial.printf("Drawing char %d: U+%04X at x=%d y=%d scale=%.2f\n", charsDrawn, unicode, drawX, drawY, binScale);
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
              int emX = columnX - fontSizePt / 2;
              int emY = currentY + (charHeight - fontSizePt) / 2;
              int kx = (glyph->bearingX != 0 || glyph->bearingY != 0) ? emX + (int)(glyph->bearingX * binScale) : columnX - (int)(glyph->width * binScale + 0.5f)/2;
              int ky = (glyph->bearingX != 0 || glyph->bearingY != 0) ? emY + (int)(glyph->bearingY * binScale) : emY + (fontSizePt - (int)(glyph->height * binScale + 0.5f)) / 2;
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

// ==================== Table of Contents ====================

void drawTocList() {
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);

  drawStatusBar();
  drawReturnButton();

  // Title
  drawSystemText("目錄", 20, 30, 36);
  M5.Display.drawLine(20, 80, 520, 80, TFT_BLACK);

  if (epubTocEntries && epubTocCount > 0) {
    int totalTocPages = (epubTocCount + TOC_PER_PAGE - 1) / TOC_PER_PAGE;
    if (tocListPage >= totalTocPages) tocListPage = totalTocPages - 1;
    if (tocListPage < 0) tocListPage = 0;
    int startIdx = tocListPage * TOC_PER_PAGE;
    int endIdx = min(startIdx + TOC_PER_PAGE, epubTocCount);

    for (int i = startIdx; i < endIdx; i++) {
      int row = i - startIdx;
      int rowY = 120 + row * BOOK_ROW_HEIGHT;

      // Truncate long titles to fit
      String displayName = epubTocEntries[i].label;
      int len = displayName.length();
      if (len > 30) {
        int charCount = 0, bytePos = 0;
        while (bytePos < len) {
          uint8_t c = (uint8_t)displayName[bytePos];
          if (c < 0x80) bytePos += 1;
          else if (c < 0xE0) bytePos += 2;
          else if (c < 0xF0) bytePos += 3;
          else bytePos += 4;
          charCount++;
        }
        if (charCount > 17) {
          int targetChars = 16;
          bytePos = 0;
          int count = 0;
          while (bytePos < len && count < targetChars) {
            uint8_t c = (uint8_t)displayName[bytePos];
            if (c < 0x80) bytePos += 1;
            else if (c < 0xE0) bytePos += 2;
            else if (c < 0xF0) bytePos += 3;
            else bytePos += 4;
            count++;
          }
          displayName = displayName.substring(0, bytePos) + "\xe2\x80\xa6";  // "…"
        }
      }

      drawSystemText(displayName.c_str(), 40, rowY, 28);
      yield();  // prevent watchdog timeout during CJK font rendering
    }

    // Pagination
    bool hasPrev = (tocListPage > 0);
    bool hasNext = (tocListPage < totalTocPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);

    // Status
    String info = String(epubTocCount) + " 章";
    if (totalTocPages > 1) {
      info += " | 第 " + String(tocListPage + 1) + "/" + String(totalTocPages) + " 頁";
    }
    drawSystemText(info.c_str(), 20, 840, 28);
  } else {
    drawSystemText("無目錄資料", 20, 120, 28);
    drawReturnButton();
  }

  M5.Display.endWrite();
  M5.Display.display();
}

// ==================== Page Jump Popup ====================

// Draw a numeric keyboard popup overlay for jumping to a specific page
void drawPageJumpPopup() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);

  // Popup card dimensions
  const int cardW = 460, cardH = 560;
  const int cardX = (DISPLAY_WIDTH - cardW) / 2;   // 40
  const int cardY = (DISPLAY_HEIGHT - cardH) / 2;  // 200

  // Draw card frame
  M5.Display.fillRoundRect(cardX, cardY, cardW, cardH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(cardX, cardY, cardW, cardH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 11, TFT_BLACK);

  // Title: "跳到頁面" (Jump to page)
  drawSystemTextCentered("跳到頁面", DISPLAY_WIDTH / 2, cardY + 20, 32);

  // Show total pages info
  char totalStr[32];
  snprintf(totalStr, sizeof(totalStr), "共 %d 頁", totalPages);
  drawSystemTextCentered(totalStr, DISPLAY_WIDTH / 2, cardY + 60, 22, EPD_DARK_GRAY);

  // Input display area
  const int inputX = cardX + 30, inputY = cardY + 95;
  const int inputW = cardW - 60, inputH = 50;
  M5.Display.drawRect(inputX, inputY, inputW, inputH, TFT_BLACK);
  // Show current input
  if (pageJumpInput.length() > 0) {
    drawSystemText(pageJumpInput.c_str(), inputX + 15, inputY + 10, 28);
  } else {
    drawSystemText("_", inputX + 15, inputY + 10, 28, EPD_LIGHT_GRAY);
  }

  // Numpad layout: 3x4 grid
  // [1] [2] [3]
  // [4] [5] [6]
  // [7] [8] [9]
  // [清除] [0] [確定]
  const int btnW = 120, btnH = 70;
  const int padX = cardX + (cardW - 3 * btnW - 2 * 15) / 2;  // center the 3 columns
  const int padY = cardY + 160;
  const int gapX = 15, gapY = 12;

  const char* labels[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"清除", "0", "確定"}
  };

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int bx = padX + col * (btnW + gapX);
      int by = padY + row * (btnH + gapY);
      M5.Display.drawRoundRect(bx, by, btnW, btnH, 8, TFT_BLACK);
      // Use smaller font for Chinese labels, larger for digits
      int fontSize = (row == 3 && col != 1) ? 24 : 32;
      drawSystemTextCentered(labels[row][col], bx + btnW / 2, by + (btnH - fontSize) / 2, fontSize);
    }
  }

  // Cancel button at bottom-left corner
  drawSystemText("返回", cardX + 20, cardY + cardH - 45, 24, EPD_DARK_GRAY);

  M5.Display.endWrite();
  M5.Display.display();
}

// Lightweight update: only redraw the input field area (no full e-ink refresh)
static void updatePageJumpInput() {
  const int cardW = 460, cardH = 560;
  const int cardX = (DISPLAY_WIDTH - cardW) / 2;
  const int cardY = (DISPLAY_HEIGHT - cardH) / 2;
  const int inputX = cardX + 30, inputY = cardY + 95;
  const int inputW = cardW - 60, inputH = 50;

  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  M5.Display.startWrite();
  M5.Display.fillRect(inputX + 1, inputY + 1, inputW - 2, inputH - 2, TFT_WHITE);
  if (pageJumpInput.length() > 0) {
    drawSystemText(pageJumpInput.c_str(), inputX + 15, inputY + 10, 28);
  } else {
    drawSystemText("_", inputX + 15, inputY + 10, 28, EPD_LIGHT_GRAY);
  }
  M5.Display.endWrite();
  M5.Display.display();
}

// Handle touch input on the page jump popup
// Returns true if touch was handled (consumed)
bool handlePageJumpTouch(int x, int y) {
  const int cardW = 460, cardH = 560;
  const int cardX = (DISPLAY_WIDTH - cardW) / 2;
  const int cardY = (DISPLAY_HEIGHT - cardH) / 2;

  const int btnW = 120, btnH = 70;
  const int padX = cardX + (cardW - 3 * btnW - 2 * 15) / 2;
  const int padY = cardY + 160;
  const int gapX = 15, gapY = 12;

  // Check cancel/return button (bottom-left of card)
  if (x >= cardX && x <= cardX + 100 && y >= cardY + cardH - 55 && y <= cardY + cardH) {
    Serial.println("Page jump: cancelled");
    pageJumpInput = "";
    currentMode = MODE_READING;
    drawReading();
    return true;
  }

  // Check numpad buttons
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int bx = padX + col * (btnW + gapX);
      int by = padY + row * (btnH + gapY);
      if (x >= bx && x <= bx + btnW && y >= by && y <= by + btnH) {
        if (row == 3 && col == 0) {
          // Clear button
          pageJumpInput = "";
          Serial.println("Page jump: cleared");
          updatePageJumpInput();
        } else if (row == 3 && col == 2) {
          // Confirm button
          if (pageJumpInput.length() > 0) {
            int targetPage = pageJumpInput.toInt();
            if (targetPage >= 1 && targetPage <= totalPages) {
              currentPage = targetPage - 1;  // 0-based internally
              pageJumpInput = "";
              currentMode = MODE_READING;
              if (loadCurrentPage()) {
                saveReadingPosition();
                drawReading();
              }
              Serial.printf("Page jump: jumped to page %d\n", targetPage);
            } else {
              // Invalid page — flash the input (redraw to show it's wrong)
              Serial.printf("Page jump: invalid page %d (max %d)\n", targetPage, totalPages);
              pageJumpInput = "";
              updatePageJumpInput();
            }
          }
        } else {
          // Digit button
          int digit = row * 3 + col + 1;
          if (row == 3 && col == 1) digit = 0;
          // Prevent leading zeros
          if (digit == 0 && pageJumpInput.length() == 0) {
            return true;
          }
          String candidate = pageJumpInput + String(digit);
          int candidateVal = candidate.toInt();
          // Only allow input if the number doesn't exceed totalPages
          // Also limit input length to prevent overflow
          if (candidateVal <= totalPages && candidate.length() <= 6) {
            pageJumpInput = candidate;
            Serial.printf("Page jump: input = %s\n", pageJumpInput.c_str());
            updatePageJumpInput();
          } else {
            Serial.printf("Page jump: %d exceeds totalPages %d, ignored\n", candidateVal, totalPages);
          }
        }
        return true;
      }
    }
  }

  return false;
}
