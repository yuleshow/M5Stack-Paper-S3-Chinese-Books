#include "globals.h"
#include "esp_task_wdt.h"

// Save current reading position to SD card (.pos file)
void saveReadingPosition() {
  if (!sdCardAvailable || currentBookPath.isEmpty()) return;
  String posPath = currentBookPath + ".pos";
  ScopedSDLock lock;
  File f = SD.open(posPath, FILE_WRITE);
  if (!f) return;
  // Save page number and byte offset for accurate restoration
  size_t byteOff = (pageByteOffsets && currentPage < pageOffsetsCount) ?
                   pageByteOffsets[currentPage] : (size_t)currentPage * bytesPerPage;
  f.printf("%d\n%lu\n", currentPage, (unsigned long)byteOff);
  f.close();
  Serial.printf("Saved reading position: page %d offset %lu to %s\n", currentPage, (unsigned long)byteOff, posPath.c_str());
}

// Load saved reading position from SD card (.pos file)
// Also restores the byte offset into pageByteOffsets for accurate backward navigation
int loadReadingPosition() {
  if (!sdCardAvailable || currentBookPath.isEmpty()) return 0;
  String posPath = currentBookPath + ".pos";
  ScopedSDLock lock;
  if (!SD.exists(posPath)) return 0;
  File f = SD.open(posPath, FILE_READ);
  if (!f) return 0;
  String line1 = f.readStringUntil('\n');
  String line2 = f.readStringUntil('\n');
  f.close();
  line1.trim();
  line2.trim();
  int page = line1.toInt();
  unsigned long savedOffset = line2.length() > 0 ? strtoul(line2.c_str(), NULL, 10) : 0;
  Serial.printf("Loaded reading position: page %d offset %lu from %s\n", page, savedOffset, posPath.c_str());
  // Recalculate page number from byte offset using current bytesPerPage
  // (layout changes may have changed chars-per-page since position was saved)
  if (savedOffset > 0 && bytesPerPage > 0) {
    int recalcPage = (int)(savedOffset / bytesPerPage);
    if (recalcPage != page) {
      Serial.printf("Recalculated page from offset: %d -> %d\n", page, recalcPage);
      page = recalcPage;
    }
  }
  // Restore the byte offset for this page so backward navigation works correctly
  if (page > 0 && savedOffset > 0 && pageByteOffsets) {
    // Extend pageByteOffsets up to this page, estimating backward from the known offset
    while (pageOffsetsCount <= page && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      int gap = page - pageOffsetsCount;
      size_t est = (savedOffset > (size_t)(gap + 1) * bytesPerPage) ?
                   savedOffset - (size_t)(gap + 1) * bytesPerPage : 0;
      pageByteOffsets[pageOffsetsCount] = est;
      pageOffsetsCount++;
    }
    if (page < pageOffsetsCount) {
      pageByteOffsets[page] = (size_t)savedOffset;
    }
    Serial.printf("Restored %d page offsets from saved position\n", pageOffsetsCount);
  }
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

void removeBookmark(int page) {
  for (int i = 0; i < bookmarkCount; i++) {
    if (bookmarks[i].page == page) {
      for (int j = i; j < bookmarkCount - 1; j++) bookmarks[j] = bookmarks[j + 1];
      bookmarkCount--;
      saveBookmarks();
      return;
    }
  }
}

void toggleBookmark() {
  for (int i = 0; i < bookmarkCount; i++) {
    if (bookmarks[i].page == currentPage) {
      removeBookmark(currentPage);
      return;
    }
  }
  addBookmark();
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
  
  // Sort books by display name (case-insensitive)
  for (int i = 0; i < bookCount - 1; i++) {
    for (int j = i + 1; j < bookCount; j++) {
      String a = bookDisplayName[i]; a.toLowerCase();
      String b = bookDisplayName[j]; b.toLowerCase();
      if (a > b) {
        String tmp = bookList[i]; bookList[i] = bookList[j]; bookList[j] = tmp;
        tmp = bookDisplayName[i]; bookDisplayName[i] = bookDisplayName[j]; bookDisplayName[j] = tmp;
      }
    }
  }
  
  Serial.printf("Found %d books (sorted)\n", bookCount);
}

// Check if a filename contains Chinese/CJK characters (any byte >= 0x80 in UTF-8)
bool isChineseBookName(const String& filename) {
  for (int i = 0; i < (int)filename.length(); i++) {
    if ((uint8_t)filename[i] >= 0x80) return true;
  }
  return false;
}

// Check if a book should be opened in English (horizontal) mode
// EPUB: check the display title (embedded metadata title)
// TXT: check the filename
bool bookIsEnglishMode(int bookIndex) {
  if (bookIndex < 0 || bookIndex >= bookCount) return false;
  String fname = bookList[bookIndex];
  fname.toLowerCase();
  if (fname.endsWith(".epub")) {
    return !isChineseBookName(bookDisplayName[bookIndex]);
  }
  return !isChineseBookName(bookList[bookIndex]);
}

// Check if the current reading font is Silver
bool isReadingFontSilver() {
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
  // Use nominal readingFontSize for layout so all fonts at same size have same grid
  int charH, colSp;
  int areaTop, areaLeft, areaRight, maxY;
  charH = readingFontSize + readingFontSize / 5;  // 1.2x nominal size
  colSp = readingFontSize + readingFontSize / 5;
  areaTop = READING_AREA_TOP; areaLeft = READING_AREA_LEFT;
  areaRight = READING_AREA_RIGHT; maxY = VERTICAL_TEXT_MAX_Y;
  if (charH < 1) charH = 1;
  if (colSp < 1) colSp = 1;

  // Horizontal layout for English/Latin books
  if (epubIsHorizontal) {
    int lineHeight = readingFontSize + readingFontSize / 4;  // 1.25x — matches renderer
    int availH = 830 - areaTop;                               // rdBottom=830 in renderer
    int linesPerPage = availH / lineHeight;
    if (linesPerPage < 1) linesPerPage = 1;
    int availW = areaRight - areaLeft;
    // Estimate ~1.8 bytes per pixel width for English text at this font size
    int charsPerLine = availW / max(1, readingFontSize * 3 / 5);  // ~0.6em per char
    int totalChars = linesPerPage * charsPerLine;
    bytesPerPage = max(200, totalChars * 2 + 50);  // *2 for UTF-8 multi-byte (smart quotes etc.)
    Serial.printf("Horizontal: fontSize %d -> bytesPerPage %d (lines %d × chars %d)\n",
                  readingFontSize, bytesPerPage, linesPerPage, charsPerLine);
    return;
  }

  // Optimize: squeeze one more column if leftover space >= 40% of column width
  {
    int availW = areaRight - areaLeft;
    int nc = availW / colSp;
    int leftover = availW - nc * colSp;
    if (nc > 0 && leftover * 5 >= colSp * 2) {
      nc++;
      colSp = availW / nc;
    }
  }
  int charsPerCol = (maxY - areaTop) / charH - 1;
  if (areaTop + (charsPerCol + 1) * charH > READING_AREA_BOTTOM)
    charsPerCol--;
  if (charsPerCol < 1) charsPerCol = 1;
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
      // Keep the current page's known offset; estimate earlier pages backward
      if (pageByteOffsets) {
        pageByteOffsets[0] = 0;  // Page 0 always starts at byte 0
        pageOffsetsCount = 1;
        // Fill up to current page with backward estimates from pageOffset
        while (pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
          int gap = currentPage - pageOffsetsCount;
          size_t est = (pageOffset > (size_t)(gap + 1) * bytesPerPage) ?
                       pageOffset - (size_t)(gap + 1) * bytesPerPage : 0;
          pageByteOffsets[pageOffsetsCount] = est;
          pageOffsetsCount++;
        }
        if (currentPage < pageOffsetsCount) {
          pageByteOffsets[currentPage] = pageOffset;
        }
        // Recalculate current page number based on offset
        if (bytesPerPage > 0) {
          totalPages = (totalBookBytes / bytesPerPage) + 1;
        }
        Serial.printf("EPUB reload: rebuilt %d page offsets, page %d = offset %u\n",
                      pageOffsetsCount, currentPage, (unsigned)pageOffset);
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
      // For horizontal (English) mode, snap back to a word boundary so the
      // page buffer never ends mid-word. The renderer sees complete words and
      // produces clean page breaks.
      if (epubIsHorizontal && safeEnd > 0 && safeEnd < remaining) {
        unsigned char endChar = (unsigned char)epubFullText[localOffset + safeEnd];
        // If we'd cut inside a word, back up to last whitespace
        if (endChar != ' ' && endChar != '\n' && endChar != '\r' && endChar != '\t') {
          size_t wordBound = safeEnd;
          while (wordBound > 0) {
            unsigned char c = (unsigned char)epubFullText[localOffset + wordBound - 1];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
            wordBound--;
          }
          if (wordBound > safeEnd / 2) {  // Don't snap too far back
            safeEnd = wordBound;
          }
        }
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

bool loadBook(int bookIndex, bool forceChinese) {
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
  // Unload all font resources to free SD file handles — prevents SD bus contention
  // during heavy EPUB chapter loading. Will be reloaded after loading completes.
  unloadBinaryFont();
  if (ofrFontLoaded) {
    ofr.unloadFont();
    ofrFontLoaded = false;
  }
  // Force-close any stale OFR file handles
  if (!ofr_file_list.empty()) {
    Serial.printf("WARNING: %d stale OFR file handles in loadBook\n", (int)ofr_file_list.size());
    for (auto &f : ofr_file_list) f.close();
    ofr_file_list.clear();
  }
  currentFontFile = "";
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
    bool isDir = checkFile.isDirectory();
    checkFile.close();
    Serial.printf("loadBook: file verified, size=%u bytes, isDir=%d, path=%s\n",
                  fsize, isDir, currentBookPath.c_str());
    if (isDir) {
      lastLoadError = "is directory";
      return false;
    }
    if (fsize < 100) {
      char detail[60];
      snprintf(detail, sizeof(detail), "太小: %u bytes", (unsigned)fsize);
      lastLoadError = String(detail);
      return false;
    }
  }
  
  // Check if EPUB
  String lowerName = filename;
  lowerName.toLowerCase();
  if (lowerName.endsWith(".epub")) {
    currentBookIsEpub = true;
    // Detect English mode BEFORE epubLoad (which calls epubCleanup and resets the flag)
    bool useHorizontal = (!forceChinese && bookIsEnglishMode(bookIndex));
    if (!epubLoad(currentBookPath)) {
      Serial.println("EPUB: Failed to load");
      if (lastLoadError.isEmpty()) lastLoadError = "epubLoad fail";
      currentBookIsEpub = false;
      return false;
    }
    // Set horizontal layout AFTER epubLoad (epubCleanup resets epubIsHorizontal)
    if (useHorizontal) {
      epubIsHorizontal = true;
      Serial.println("EPUB: English mode (horizontal layout)");
      readingFontSize = loadPrefInt("ereader", "rdFontSzEn", DEFAULT_ENGLISH_READING_FONT_SIZE);
      readingFontSize = constrain(readingFontSize, MIN_READING_FONT_SIZE, MAX_READING_FONT_SIZE);
      // Restore English font selection (default to embedded ET Book)
      readingFontIndex = loadPrefInt("ereader", "fontIdxEn", -1);
      if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) {
        // Find ET Book index (last entry added by scanFontFiles)
        int etIdx = -1;
        for (int fi = 0; fi < fontFileCount; fi++) {
          if (fontFileList[fi] == "ETBook-embedded") { etIdx = fi; break; }
        }
        readingFontIndex = (etIdx >= 0) ? etIdx : max(0, systemFontIndex);
      }
      if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = 0;
      selectedFontIndex = readingFontIndex;
      readingFontFile = loadPrefStr("ereader", "fontFileEn", "ETBook-embedded");
    } else {
      readingFontSize = loadPrefInt("ereader", "rdFontSz", DEFAULT_READING_FONT_SIZE);
      readingFontSize = constrain(readingFontSize, MIN_READING_FONT_SIZE, MAX_READING_FONT_SIZE);
      // Restore Chinese font selection (default to system font, not previous readingFontIndex
      // which may point to an English font from a previous session)
      readingFontIndex = loadPrefInt("ereader", "fontIdx", max(0, systemFontIndex));
      if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = max(0, systemFontIndex);
      if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = 0;
      selectedFontIndex = readingFontIndex;
      readingFontFile = loadPrefStr("ereader", "fontFile",
                         (readingFontIndex >= 0 && readingFontIndex < fontFileCount) ? fontFileList[readingFontIndex] : String(""));
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
          esp_task_wdt_reset();
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
    if (!epubIsImageBased) updateLoadProgress(75);
    yield();
    esp_task_wdt_reset();
    if (!loadCurrentPage()) { lastLoadError = "epub loadPage"; return false; }
    Serial.println("EPUB: loadCurrentPage OK, ready to draw");
    if (epubIsImageBased) {
      // Comics render images, not text — skip font loading entirely.
      // drawReading() will unload any leftover OFR font to free heap for image decode.
      Serial.println("EPUB: Comic mode — skipping loadReadingFont");
    } else {
      updateLoadProgress(85);
      // Pre-load reading font here (not in drawReading) to avoid e-ink display bus conflicts
      loadReadingFont();
      updateLoadProgress(95);
    }
    yield();
    esp_task_wdt_reset();
    return true;
  }
  
  // Plain text file
  // Set horizontal layout: auto-detect from filename unless forced Chinese
  if (!forceChinese && bookIsEnglishMode(bookIndex)) {
    epubIsHorizontal = true;
    Serial.println("TXT: English mode (horizontal layout)");
    readingFontSize = loadPrefInt("ereader", "rdFontSzEn", DEFAULT_ENGLISH_READING_FONT_SIZE);
    readingFontSize = constrain(readingFontSize, MIN_READING_FONT_SIZE, MAX_READING_FONT_SIZE);
    // Restore English font selection (default to embedded ET Book)
    readingFontIndex = loadPrefInt("ereader", "fontIdxEn", -1);
    if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) {
      // Find ET Book index
      int etIdx = -1;
      for (int fi = 0; fi < fontFileCount; fi++) {
        if (fontFileList[fi] == "ETBook-embedded") { etIdx = fi; break; }
      }
      readingFontIndex = (etIdx >= 0) ? etIdx : max(0, systemFontIndex);
    }
    if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = 0;
    selectedFontIndex = readingFontIndex;
    readingFontFile = loadPrefStr("ereader", "fontFileEn", "ETBook-embedded");
  } else {
    readingFontSize = loadPrefInt("ereader", "rdFontSz", DEFAULT_READING_FONT_SIZE);
    readingFontSize = constrain(readingFontSize, MIN_READING_FONT_SIZE, MAX_READING_FONT_SIZE);
    readingFontIndex = loadPrefInt("ereader", "fontIdx", max(0, systemFontIndex));
    if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = max(0, systemFontIndex);
    if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = 0;
    selectedFontIndex = readingFontIndex;
    readingFontFile = loadPrefStr("ereader", "fontFile",
                       (readingFontIndex >= 0 && readingFontIndex < fontFileCount) ? fontFileList[readingFontIndex] : String(""));
  }
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
  updateLoadProgress(80);
  
  // Load first page
  if (!loadCurrentPage()) { lastLoadError = "txt loadPage"; return false; }
  updateLoadProgress(95);
  return true;
}

bool openBookFromList(int bookIndex, bool openAsChinese) {
  currentBook = bookDisplayName[bookIndex];
  Serial.printf("Opening book: %s (%s) chinese=%d\n", currentBook.c_str(), bookList[bookIndex].c_str(), openAsChinese);
  
  esp_task_wdt_reset();
  pagesSinceFullRefresh = 1;

  // Show loading indicator
  {
    unsigned long busyStart = millis();
    while (M5.Display.displayBusy()) {
      delay(10);
      esp_task_wdt_reset();
      if (millis() - busyStart > 3000) break;
    }
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
    drawStatusBar();
    drawSystemTextCentered(currentBook.c_str(), M5.Display.width() / 2,
                           M5.Display.height() / 2 - 100, 32);
    drawSystemTextCentered("AI智能排版中...", M5.Display.width() / 2,
                           M5.Display.height() / 2 - 20, 36);
    int barX = 70, barY = M5.Display.height() / 2 + 50;
    int barW = 400, barH = 30;
    M5.Display.drawRect(barX, barY, barW, barH, TFT_BLACK);
    M5.Display.drawRect(barX + 1, barY + 1, barW - 2, barH - 2, TFT_BLACK);
    M5.Display.endWrite();
    M5.Display.display();
  }

  Serial.printf("\n=== Attempting to load book index %d ===\n", bookIndex);
  Serial.printf("Pre-loadBook: Free heap: %u, Free PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  unsigned long loadStart = millis();
  loadBookCrashed = true;
  if (loadBook(bookIndex, openAsChinese)) {
    loadBookCrashed = false;
    unsigned long loadMs = millis() - loadStart;
    Serial.printf("Book loaded OK in %lu ms - Free heap: %u, Free PSRAM: %u\n",
                  loadMs, ESP.getFreeHeap(), ESP.getFreePsram());
    savePrefStr("ereader", "lastBook", bookList[bookIndex]);
    yield();
    esp_task_wdt_reset();
    currentMode = MODE_READING;
    comicZoomQuadrant = -1;
    drawReading();
    return true;
  } else {
    Serial.printf("Failed to load book! Free heap: %u, Free PSRAM: %u, error: %s\n",
                  ESP.getFreeHeap(), ESP.getFreePsram(), lastLoadError.c_str());
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
    drawSystemText("載入失敗", 120, 200, 48);
    char errMsg1[80];
    snprintf(errMsg1, sizeof(errMsg1), "錯誤: %s", lastLoadError.c_str());
    drawSystemText(errMsg1, 40, 320, 32);
    char errMsg2[80];
    snprintf(errMsg2, sizeof(errMsg2), "%.50s", bookList[bookIndex].c_str());
    drawSystemText(errMsg2, 40, 400, 24);
    char errMsg3[80];
    snprintf(errMsg3, sizeof(errMsg3), "Heap:%uK PSRAM:%uK",
             ESP.getFreeHeap()/1024, ESP.getFreePsram()/1024);
    drawSystemText(errMsg3, 40, 470, 24);
    drawSystemText("點擊螢幕繼續", 140, 600, 28);
    M5.Display.endWrite();
    M5.Display.display();
    while (true) {
      M5.update();
      auto t = M5.Touch.getDetail();
      if (t.wasPressed()) break;
      delay(50);
    }
    loadSystemFont();
    drawBookList();
    return false;
  }
}

void drawBookList() {
  Serial.println("Drawing book list...");
  
  {
    unsigned long busyStart = millis();
    while (M5.Display.displayBusy()) {
      delay(10);
      esp_task_wdt_reset();
      if (millis() - busyStart > 5000) {
        Serial.println("WARNING: displayBusy timeout (5s) in drawBookList");
        break;
      }
    }
  }
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();
  
  M5.Display.setTextColor(TFT_BLACK);
  
  // Draw title
  drawSystemText("電子書列表", 20, 42, 36);
  
  // Draw "最後閱讀" button if there's a saved last book
  {
    String lastBook = loadPrefStr("ereader", "lastBook", "");
    if (lastBook.length() > 0) {
      const int btnX = 370, btnY = 44, btnW = 140, btnH = 36;
      M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, 6, TFT_BLACK);
      drawSystemTextCentered("最後閱讀", btnX + btnW / 2, btnY + (btnH - 24) / 2, 24);
    }
  }
  
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
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
      // English-mode books: show "中" button to allow opening in Chinese mode
      if (bookIsEnglishMode(i)) {
        int btnX = DISPLAY_WIDTH - 60;
        int btnY = 120 + (row * BOOK_ROW_HEIGHT) - 2;
        int btnW = 45, btnH = 34;
        M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, 4, TFT_BLACK);
        drawSystemTextCentered("中", btnX + btnW / 2, btnY + (btnH - 24) / 2, 24);
      }
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
epd_mode_t getReadingEpdMode() {
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
  
  // Ensure reading font is loaded (first page or after mode change).
  // loadReadingFont() short-circuits if BIN is already in memory,
  // keeping OFR (ET Book embedded) loaded across pages for fast Latin rendering.
  // Skip for image-based EPUBs — they render images, not text, so no font needed.
  if (!(currentBookIsEpub && epubIsImageBased)) {
    loadReadingFont();
  } else {
    // Comics don't need FreeType — they render images, not text.
    // Unload OFR to free ~200KB of regular heap that JPEG/PNG decoders need.
    // Comic UI (page numbers, zoom label) uses built-in fonts and PROGMEM labels.
    if (ofrFontLoaded) {
      Serial.printf("Comic mode: unloading OFR to free heap for image decode (heap=%u)\n", ESP.getFreeHeap());
      ofr.unloadFont();
      ofrFontLoaded = false;
      currentFontFile = "";
      Serial.printf("Comic mode: OFR unloaded (heap=%u)\n", ESP.getFreeHeap());
    }
  }
  yield();
  esp_task_wdt_reset();

  // Wait for any pending e-ink refresh before starting new frame
  // M5GFX startWrite() does not wait — calling it during an active refresh deadlocks
  {
    unsigned long busyStart = millis();
    while (M5.Display.displayBusy()) {
      delay(10);
      esp_task_wdt_reset();
      if (millis() - busyStart > 5000) {
        Serial.println("WARNING: displayBusy timeout (5s) in drawReading");
        break;
      }
    }
    Serial.printf("drawReading: displayBusy wait %lu ms\n", millis() - busyStart);
  }

  // Common screen setup for all reading paths
  Serial.println("drawReading: startWrite...");
  M5.Display.setEpdMode(getReadingEpdMode());
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  drawStatusBar();
  Serial.println("drawReading: screen cleared, status bar drawn");
  
  // Dispatch to specialized renderer based on book type
  if (currentBookIsEpub && epubIsImageBased) {
    drawComicReading();
    return;
  }
  
  // Dispatch to text renderer based on book language/orientation
  if (epubIsHorizontal) {
    drawEnglishReading();
    return;
  }

  drawChineseReading();
}

// ==================== Table of Contents ====================

void drawTocList() {
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);

  drawStatusBar();
  drawReturnButton();

  // Tab bar
  int tabW = 260;
  int tabH = 50;
  int tabY = 35;

  // Tab 0: 目錄
  if (tocTab == 0) {
    M5.Display.fillRect(10, tabY, tabW, tabH, TFT_BLACK);
    drawSystemText("目錄", 10 + tabW / 2 - 32, tabY + 8, 32, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Display.drawRect(10, tabY, tabW, tabH, TFT_BLACK);
    drawSystemText("目錄", 10 + tabW / 2 - 32, tabY + 8, 32);
  }

  // Tab 1: 書籤
  if (tocTab == 1) {
    M5.Display.fillRect(270, tabY, tabW, tabH, TFT_BLACK);
    drawSystemText("書籤", 270 + tabW / 2 - 32, tabY + 8, 32, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Display.drawRect(270, tabY, tabW, tabH, TFT_BLACK);
    drawSystemText("書籤", 270 + tabW / 2 - 32, tabY + 8, 32);
  }

  M5.Display.drawLine(10, tabY + tabH, 530, tabY + tabH, TFT_BLACK);

  int listStartY = 100;

  if (tocTab == 0) {
    // === Chapter list ===
    if (epubTocEntries && epubTocCount > 0) {
      int totalTocPages = (epubTocCount + TOC_PER_PAGE - 1) / TOC_PER_PAGE;
      if (tocListPage >= totalTocPages) tocListPage = totalTocPages - 1;
      if (tocListPage < 0) tocListPage = 0;
      int startIdx = tocListPage * TOC_PER_PAGE;
      int endIdx = min(startIdx + TOC_PER_PAGE, epubTocCount);

      // Load reading font for chapter titles
      loadReadingFont();
      if (ofrFontLoaded) {
        ofr.setFontSize(28);
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
        ofr.setDrawer(M5.Display);
      }

      for (int i = startIdx; i < endIdx; i++) {
        int row = i - startIdx;
        int rowY = listStartY + row * BOOK_ROW_HEIGHT;

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
            displayName = displayName.substring(0, bytePos) + "\xe2\x80\xa6";
          }
        }

        if (ofrFontLoaded) {
          ofr.drawString(displayName.c_str(), 40, rowY);
        } else {
          drawSystemText(displayName.c_str(), 40, rowY, 28);
        }
        yield();
      }
      // Restore system font for UI elements below
      loadSystemFont();

      bool hasPrev = (tocListPage > 0);
      bool hasNext = (tocListPage < totalTocPages - 1);
      drawVerticalNavBar(hasPrev, hasNext);

      String info = String(epubTocCount) + " 章";
      if (totalTocPages > 1) {
        info += " | 第 " + String(tocListPage + 1) + "/" + String(totalTocPages) + " 頁";
      }
      drawSystemText(info.c_str(), 20, 840, 28);
    } else {
      drawSystemText("此書無目錄", 40, listStartY, 28);
    }
  } else {
    // === Bookmark list ===
    if (bookmarkCount > 0) {
      for (int i = 0; i < bookmarkCount; i++) {
        int rowY = listStartY + i * BOOK_ROW_HEIGHT;

        // Page label
        String bmLabel = "第 " + String(bookmarks[i].page + 1) + " 頁";
        drawSystemText(bmLabel.c_str(), 40, rowY, 28);

        // Delete button ✕
        int delX = 460, delW = 50, delH = 40;
        M5.Display.drawRect(delX, rowY - 2, delW, delH, TFT_DARKGRAY);
        drawSystemText("✕", delX + 14, rowY + 2, 28, TFT_DARKGRAY);
      }

      String info = String(bookmarkCount) + " / 5 個書籤";
      drawSystemText(info.c_str(), 20, 840, 28);
    } else {
      drawSystemText("尚無書籤", 40, listStartY, 28);
      drawSystemText("閱讀時點擊 ★ 可加入書籤", 40, listStartY + 50, 22, EPD_DARK_GRAY);
    }
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
