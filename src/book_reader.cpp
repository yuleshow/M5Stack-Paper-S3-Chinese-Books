#include "globals.h"
#include "conv_table.h"
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
      size_t est = (savedOffset > (size_t)gap * bytesPerPage) ?
                   savedOffset - (size_t)gap * bytesPerPage : 0;
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
  
  Serial.println("Scanning /books directory and subfolders...");
  
  yield();

  // Subfolder → category mapping
  struct SubfolderEntry { const char* name; BookCategory cat; };
  static const SubfolderEntry subfolders[] = {
    {"en",    CAT_EN},
    {"cn",    CAT_CN},
    {"comic", CAT_COMIC},
  };
  static const int numSubfolders = sizeof(subfolders) / sizeof(subfolders[0]);

  // Helper lambda: scan one directory and add matching files
  auto scanDir = [&](const char* dirPath, const char* prefix, BookCategory cat) {
    File dir;
    {
      ScopedSDLock lock;
      dir = SD.open(dirPath);
    }
    if (!dir || !dir.isDirectory()) return;

    File entry = dir.openNextFile();
    int count = 0;
    while (entry && bookCount < MAX_BOOKS) {
      const char* name = entry.name();
      if (name) {
        String filename = String(name);
        // ESP32 SD returns full path — extract just the filename
        int lastSlash = filename.lastIndexOf('/');
        if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);
        // Skip dot files (macOS AppleDouble)
        if (!filename.startsWith(".") && !filename.startsWith("._")) {
          String lowerName = filename;
          lowerName.toLowerCase();
          if (lowerName.endsWith(".txt") || lowerName.endsWith(".epub")) {
            // Build relative path from /books/: e.g. "en/mybook.epub" or just "mybook.epub"
            String relPath = (prefix[0] != '\0') ? (String(prefix) + "/" + filename) : filename;
            bookList[bookCount] = relPath;
            bookCategory[bookCount] = cat;

            // Set display name
            if (lowerName.endsWith(".epub")) {
              String fullPath = String("/books/") + relPath;
              // Skip title extraction for very large/small EPUBs
              File epubCheck;
              { ScopedSDLock lock; epubCheck = SD.open(fullPath.c_str()); }
              bool skipTitle = false;
              if (epubCheck) {
                size_t epubSize = epubCheck.size();
                epubCheck.close();
                if (epubSize < 100 || epubSize > 100UL * 1024 * 1024) skipTitle = true;
              } else {
                skipTitle = true;
              }
              String epubTitle = "";
              if (!skipTitle) epubTitle = epubGetTitle(fullPath);
              if (epubTitle.length() > 0) {
                bookDisplayName[bookCount] = epubTitle;
              } else {
                bookDisplayName[bookCount] = filename.substring(0, filename.length() - 5);
              }
            } else {
              int dotPos = filename.lastIndexOf('.');
              bookDisplayName[bookCount] = (dotPos > 0) ? filename.substring(0, dotPos) : filename;
            }
            Serial.printf("  [%d] %s -> \"%s\" (cat=%d)\n", bookCount, relPath.c_str(),
                          bookDisplayName[bookCount].c_str(), (int)cat);
            bookCount++;
          }
        }
      }
      entry.close();
      entry = dir.openNextFile();
      if (++count % 10 == 0) yield();
    }
    dir.close();
  };

  // 1. Scan recognized subfolders first
  for (int s = 0; s < numSubfolders; s++) {
    String subPath = String("/books/") + subfolders[s].name;
    scanDir(subPath.c_str(), subfolders[s].name, subfolders[s].cat);
    yield();
    esp_task_wdt_reset();
  }

  // 2. Scan /books/ root for backward compatibility (files not in subfolders)
  scanDir("/books", "", CAT_AUTO);

  // Sort books: Chinese first, then Comics, then English, then Auto.
  // Within each group, sort by display name (case-insensitive).
  auto catOrder = [](BookCategory c) -> int {
    switch (c) {
      case CAT_CN:    return 0;
      case CAT_COMIC: return 1;
      case CAT_EN:    return 2;
      default:        return 3; // CAT_AUTO
    }
  };
  for (int i = 0; i < bookCount - 1; i++) {
    for (int j = i + 1; j < bookCount; j++) {
      int oi = catOrder(bookCategory[i]);
      int oj = catOrder(bookCategory[j]);
      bool shouldSwap = false;
      if (oi > oj) {
        shouldSwap = true;
      } else if (oi == oj) {
        String a = bookDisplayName[i]; a.toLowerCase();
        String b = bookDisplayName[j]; b.toLowerCase();
        if (a > b) shouldSwap = true;
      }
      if (shouldSwap) {
        String tmp = bookList[i]; bookList[i] = bookList[j]; bookList[j] = tmp;
        tmp = bookDisplayName[i]; bookDisplayName[i] = bookDisplayName[j]; bookDisplayName[j] = tmp;
        BookCategory tc = bookCategory[i]; bookCategory[i] = bookCategory[j]; bookCategory[j] = tc;
      }
    }
  }
  
  Serial.printf("Found %d books (sorted)\n", bookCount);
}

// Check if a book should be opened in English (horizontal) mode.
// Determined by subfolder: en/ → English (horizontal), everything else → Chinese (vertical).
bool bookIsEnglishMode(int bookIndex) {
  if (bookIndex < 0 || bookIndex >= bookCount) return false;
  return bookCategory[bookIndex] == CAT_EN;
}

// Check if a book is a comic (image-based EPUB).
// Determined entirely by subfolder: comic/ → true.
bool bookIsComic(int bookIndex) {
  if (bookIndex < 0 || bookIndex >= bookCount) return false;
  return bookCategory[bookIndex] == CAT_COMIC;
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

static bool isReaderTocHeadingNumber(uint32_t cp) {
  return cp == 0x4E00 || cp == 0x4E8C || cp == 0x4E09 || cp == 0x56DB ||
         cp == 0x4E94 || cp == 0x516D || cp == 0x4E03 || cp == 0x516B ||
         cp == 0x4E5D || cp == 0x5341 || cp == 0x767E || cp == 0x5343 ||
         cp == 0x96F6 || cp == 0x3007;
}

static bool isReaderTocHeadingDigit(uint32_t cp) {
  return (cp >= '0' && cp <= '9') || (cp >= 0xFF10 && cp <= 0xFF19);
}

static bool isReaderTocChapterTerminator(uint32_t cp) {
  return cp == 0x56DE || cp == 0x7AE0 || cp == 0x7BC0 || cp == 0x7BC7 || cp == 0x5377;
}

static int skipReaderTocInlineSpaces(const String& text, int pos) {
  while (pos < (int)text.length()) {
    unsigned char byte = (unsigned char)text[pos];
    if (byte == STYLE_ITALIC_ON || byte == STYLE_ITALIC_OFF ||
        byte == STYLE_BOLD_ON || byte == STYLE_BOLD_OFF ||
        byte == STYLE_UNDERLINE_ON || byte == STYLE_UNDERLINE_OFF) {
      pos++;
      continue;
    }
    if (byte == EPUB_LINK_MARKER) {
      pos++;
      while (pos < (int)text.length() && (unsigned char)text[pos] != EPUB_LINK_MARKER) pos++;
      if (pos < (int)text.length()) pos++;
      continue;
    }
    int nextPos = pos;
    uint32_t cp = utf8Decode(text, nextPos);
    if (cp != ' ' && cp != '\t' && cp != 0x3000) break;
    pos = nextPos;
  }
  return pos;
}

static bool scanReaderTocHeadingLine(const String& text, int lineStart, int* lineEnd = nullptr) {
  int len = text.length();
  int pos = skipReaderTocInlineSpaces(text, lineStart);
  int scanPos = pos;
  uint32_t firstCp = (scanPos < len) ? utf8Decode(text, scanPos) : 0;
  bool hasNumber = false;
  bool valid = false;

  if (firstCp == 0x7B2C) {
    while (scanPos < len) {
      int before = scanPos;
      uint32_t cp = utf8Decode(text, scanPos);
      if (isReaderTocHeadingNumber(cp) || isReaderTocHeadingDigit(cp)) {
        hasNumber = true;
      } else if (hasNumber && isReaderTocChapterTerminator(cp)) {
        valid = true;
        break;
      } else {
        scanPos = before;
        break;
      }
    }
  } else if (firstCp == 0x5377) {
    while (scanPos < len) {
      int before = scanPos;
      uint32_t cp = utf8Decode(text, scanPos);
      if (isReaderTocHeadingNumber(cp) || isReaderTocHeadingDigit(cp)) {
        hasNumber = true;
      } else {
        scanPos = before;
        break;
      }
    }
    valid = hasNumber;
  }

  if (lineEnd) {
    int end = lineStart;
    while (end < len) {
      unsigned char byte = (unsigned char)text[end];
      if (byte == '\n' || byte == '\r' || byte == EPUB_CHAPTER_BREAK) break;
      end++;
    }
    *lineEnd = end;
  }
  return valid;
}

static bool isReaderTocHeadingClusterAt(const String& text, int bytePos) {
  int len = text.length();
  int pos = bytePos;
  int headingLines = 0;
  int nonEmptyLines = 0;
  const int maxLinesToScan = 8;
  const int maxBytesToScan = 1400;

  while (pos < len && nonEmptyLines < maxLinesToScan && pos - bytePos < maxBytesToScan) {
    while (pos < len) {
      unsigned char byte = (unsigned char)text[pos];
      if (byte == '\n' || byte == '\r' || byte == EPUB_CHAPTER_BREAK) { pos++; continue; }
      int nextPos = pos;
      uint32_t cp = utf8Decode(text, nextPos);
      if (cp == ' ' || cp == '\t' || cp == 0x3000) { pos = nextPos; continue; }
      break;
    }
    if (pos >= len) break;

    int lineEnd = pos;
    bool heading = scanReaderTocHeadingLine(text, pos, &lineEnd);
    if (!heading) break;

    headingLines++;
    nonEmptyLines++;
    if (headingLines >= 2) return true;

    pos = lineEnd;
    while (pos < len && ((unsigned char)text[pos] == '\n' ||
                         (unsigned char)text[pos] == '\r' ||
                         (unsigned char)text[pos] == EPUB_CHAPTER_BREAK)) pos++;
  }
  return false;
}

static bool epubReaderPageLooksLikeTocCluster(const char* text, size_t len) {
  if (!text || len == 0) return false;
  String preview;
  preview.reserve(min((size_t)1400, len));
  for (size_t i = 0; i < len && i < 1400; i++) preview += text[i];
  return isReaderTocHeadingClusterAt(preview, 0);
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
  lastRenderedForPage = -1;  // Reset precise offset chain
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
  } else if (currentPage == lastRenderedForPage + 1 && lastRenderedForPage >= 0) {
    // Precise offset from previous page's render (works beyond MAX_PAGE_OFFSETS)
    pageOffset = lastRenderedNextOffset;
    Serial.printf("Using precise rendered offset for page %d: %u\n", currentPage, (unsigned)pageOffset);
  } else if (currentPage == lastRenderedForPage && lastRenderedForPage >= 0) {
    // Re-loading the same page — use currentPageByteOffset
    pageOffset = currentPageByteOffset;
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

      // Save old chapter start before reloading (cumulative offsets may shift)
      size_t oldChapterStart = (targetChapter < epubChapterCount) ?
                                epubChapters[targetChapter].cumulativeOffset : 0;

      // Load chapters starting from one before the target (for backward navigation)
      int loadFrom = max(0, targetChapter - 1);
      if (!epubLoadChapterRange(loadFrom)) {
        Serial.println("EPUB: Failed to reload chapters");
        return false;
      }

      // After reloading, cumulativeOffset values are updated with actual sizes.
      // Preserve relative position within the chapter instead of snapping to
      // chapter start (which would lose reading progress after font changes).
      if (targetChapter < epubChapterCount) {
        size_t newChapterStart = epubChapters[targetChapter].cumulativeOffset;
        if (newChapterStart != oldChapterStart && pageOffset >= oldChapterStart) {
          size_t inChapterOffset = pageOffset - oldChapterStart;
          size_t adjusted = newChapterStart + inChapterOffset;
          Serial.printf("EPUB: Adjusted page offset %u -> %u (chapter %d shifted %d->%d, +%u within)\n",
                        (unsigned)pageOffset, (unsigned)adjusted, targetChapter + 1,
                        (unsigned)oldChapterStart, (unsigned)newChapterStart,
                        (unsigned)inChapterOffset);
          pageOffset = adjusted;
          currentPageByteOffset = pageOffset;
        } else if (pageOffset < oldChapterStart) {
          // Offset was before the chapter start — snap to chapter start
          pageOffset = newChapterStart;
          currentPageByteOffset = pageOffset;
          Serial.printf("EPUB: Corrected page offset -> %u (chapter %d start)\n",
                        (unsigned)pageOffset, targetChapter + 1);
        }
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
          size_t est = (pageOffset > (size_t)gap * bytesPerPage) ?
                       pageOffset - (size_t)gap * bytesPerPage : 0;
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

    // Safety: check if localOffset lands inside a short \x01...\x01 image marker pair.
    // Scan only near the page boundary; counting every prior marker made later EPUB pages slower.
    if (localOffset > 0 && localOffset < epubFullTextLen) {
      const size_t MAX_MARKER_PATH = 512;
      size_t scanStart = (localOffset > MAX_MARKER_PATH) ? localOffset - MAX_MARKER_PATH : 0;
      int prevMarker = -1;
      for (size_t k = localOffset; k > scanStart; k--) {
        if (epubFullText[k - 1] == EPUB_IMG_MARKER) {
          prevMarker = (int)(k - 1);
          break;
        }
      }
      if (prevMarker >= 0) {
        bool pathLikePrefix = true;
        for (size_t k = (size_t)prevMarker + 1; k < localOffset; k++) {
          char c = epubFullText[k];
          if (c == '\n' || c == '\r' || c == ' ' || c == EPUB_CHAPTER_BREAK) {
            pathLikePrefix = false;
            break;
          }
        }
        if (pathLikePrefix) {
          size_t nextMarker = localOffset;
          while (nextMarker < epubFullTextLen &&
                 nextMarker - (size_t)prevMarker <= MAX_MARKER_PATH &&
                 epubFullText[nextMarker] != EPUB_IMG_MARKER &&
                 epubFullText[nextMarker] != '\n' &&
                 epubFullText[nextMarker] != '\r' &&
                 epubFullText[nextMarker] != EPUB_CHAPTER_BREAK) {
            nextMarker++;
          }
          if (nextMarker < epubFullTextLen && epubFullText[nextMarker] == EPUB_IMG_MARKER) {
            localOffset = nextMarker + 1;
            if (localOffset < epubFullTextLen && epubFullText[localOffset] == '\n') localOffset++;
            Serial.printf("EPUB: Adjusted offset past image marker boundary (was mid-path)\n");
          }
        }
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

    if (!epubIsHorizontal && safeEnd < remaining) {
      size_t previewLen = min(remaining, safeEnd + (size_t)900);
      if (epubReaderPageLooksLikeTocCluster(epubFullText + localOffset, previewLen)) {
        size_t expandedEnd = min(remaining, max((size_t)bytesPerPage * 3 + UTF8_READ_PADDING, (size_t)1600));
        while (expandedEnd > safeEnd && expandedEnd < remaining &&
               (epubFullText[localOffset + expandedEnd] & 0xC0) == 0x80) {
          expandedEnd--;
        }
        if (expandedEnd > safeEnd) {
          Serial.printf("EPUB TOC cluster: expanded page buffer %u -> %u bytes\n",
                        (unsigned)safeEnd, (unsigned)expandedEnd);
          safeEnd = expandedEnd;
        }
      }
    }
    
    char saved = epubFullText[localOffset + safeEnd];
    epubFullText[localOffset + safeEnd] = '\0';
    currentPageContent = String(epubFullText + localOffset);
    epubFullText[localOffset + safeEnd] = saved;
    if (bookConvMode != CONV_ORIGINAL) applyConversion(currentPageContent, (ConvMode)bookConvMode);
    
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
  char* buffer = (char*)ps_malloc(bytesToRead + 1);
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
  if (bookConvMode != CONV_ORIGINAL) applyConversion(currentPageContent, (ConvMode)bookConvMode);
  
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
  sdLog("loadBook: START idx=%d '%s' WiFi=%d heap=%u psram=%u",
        bookIndex, bookList[bookIndex].c_str(), (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
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
  // Free glyph cache to defragment PSRAM before new EPUB allocations.
  // The previous book's 192KB bitmap pool would fragment the PSRAM heap,
  // potentially preventing the text buffer from being allocated contiguously.
  freeGlyphCache();
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
  sdLog("loadBook: post-cleanup WiFi=%d heap=%u psram=%u",
        (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

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
    sdLog("loadBook: file='%s' size=%u isDir=%d",
          currentBookPath.c_str(), (unsigned)fsize, isDir);
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
    // Determine layout from subfolder category
    bool useHorizontal = bookIsEnglishMode(bookIndex);
    bool isComic = bookIsComic(bookIndex);
    sdLog("loadBook: pre-epubLoad WiFi=%d heap=%u",
          (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
    if (!epubLoad(currentBookPath, isComic)) {
      Serial.println("EPUB: Failed to load");
      if (lastLoadError.isEmpty()) lastLoadError = "epubLoad fail";
      sdLog("loadBook: epubLoad FAILED WiFi=%d", (int)WiFi.status());
      currentBookIsEpub = false;
      return false;
    }
    sdLog("loadBook: post-epubLoad WiFi=%d heap=%u psram=%u chapters=%d pages=%d imgBased=%d",
          (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
          epubChapterCount, totalPages, epubIsImageBased);
    // Set horizontal layout AFTER epubLoad (epubCleanup resets epubIsHorizontal)
    // Comic books: no font needed — skip font selection entirely
    if (!isComic) {
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
    sdLog("loadBook: pre-loadCurrentPage WiFi=%d heap=%u", (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
    if (!loadCurrentPage()) { lastLoadError = "epub loadPage"; return false; }
    Serial.println("EPUB: loadCurrentPage OK, ready to draw");
    sdLog("loadBook: post-loadCurrentPage WiFi=%d heap=%u", (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
    if (epubIsImageBased) {
      // Comics render images, not text — skip font loading entirely.
      // drawReading() will unload any leftover OFR font to free heap for image decode.
      Serial.println("EPUB: Comic mode — skipping loadReadingFont");
    } else {
      updateLoadProgress(85);
      // Pre-load reading font here (not in drawReading) to avoid e-ink display bus conflicts
      sdLog("loadBook: pre-loadReadingFont WiFi=%d heap=%u", (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
      loadReadingFont();
      sdLog("loadBook: post-loadReadingFont WiFi=%d heap=%u", (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
      // Pre-initialize glyph cache now (after EPUB text buffer is shrunk and font is loaded).
      // Without this, the 192KB cache is lazily allocated during first text page render,
      // which could fragment PSRAM or cause a spike in allocation+rendering latency.
      initGlyphCache();
      sdLog("loadBook: post-initGlyphCache WiFi=%d heap=%u", (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
      updateLoadProgress(95);
    }
    yield();
    esp_task_wdt_reset();
    sdLog("loadBook: DONE WiFi=%d heap=%u psram=%u", (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    return true;
  }
  
  // Plain text file
  // Set horizontal layout from subfolder category
  if (bookIsEnglishMode(bookIndex)) {
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

bool openBookFromList(int bookIndex) {
  currentBook = bookDisplayName[bookIndex];
  Serial.printf("Opening book: %s (%s)\n", currentBook.c_str(), bookList[bookIndex].c_str());
  sdLog("openBook: START idx=%d '%s' path='%s' WiFi=%d heap=%u psram=%u",
        bookIndex, currentBook.c_str(), bookList[bookIndex].c_str(),
        (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  
  esp_task_wdt_reset();
  pagesSinceFullRefresh = 1;

  // Show loading indicator
  {
    unsigned long busyStart = millis();
    while (M5.Display.displayBusy()) {
      if (webServerRunning) handleWebClients();
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
  sdLog("openBook: pre-loadBook WiFi=%d heap=%u psram=%u",
        (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (loadBook(bookIndex)) {
    loadBookCrashed = false;
    unsigned long loadMs = millis() - loadStart;
    Serial.printf("Book loaded OK in %lu ms - Free heap: %u, Free PSRAM: %u\n",
                  loadMs, ESP.getFreeHeap(), ESP.getFreePsram());
    sdLog("openBook: loadBook OK %lums WiFi=%d heap=%u psram=%u",
          loadMs, (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    savePrefStr("ereader", "lastBook", bookList[bookIndex]);
    yield();
    esp_task_wdt_reset();
    // WiFi may have crashed during heavy EPUB loading (heap drops below ~10KB).
    // Force reconnect if WiFi is no longer connected.
    sdLog("openBook: post-load WiFi=%d webRunning=%d heap=%u",
          (int)WiFi.status(), (int)webServerRunning, (unsigned)ESP.getFreeHeap());
    if (webServerEnabled && WiFi.status() != WL_CONNECTED && wifiConfig.ssid.length() > 0) {
      sdLog("openBook: WiFi lost during load, reconnecting...");
      WiFi.disconnect(true);
      delay(100);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
      // Wait up to 5s for reconnect
      unsigned long wStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - wStart < 5000) {
        delay(100);
        esp_task_wdt_reset();
      }
      sdLog("openBook: WiFi reconnect result=%d", (int)WiFi.status());
      if (WiFi.status() == WL_CONNECTED && !webServerRunning) {
        startWebServer();
      }
    }
    currentMode = MODE_READING;
    comicZoomQuadrant = -1;
    sdLog("openBook: pre-drawReading WiFi=%d", (int)WiFi.status());
    drawReading();
    sdLog("openBook: post-drawReading WiFi=%d", (int)WiFi.status());
    return true;
  } else {
    Serial.printf("Failed to load book! Free heap: %u, Free PSRAM: %u, error: %s\n",
                  ESP.getFreeHeap(), ESP.getFreePsram(), lastLoadError.c_str());
    sdLog("openBook: FAILED WiFi=%d heap=%u err=%s",
          (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), lastLoadError.c_str());
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
      if (webServerRunning) handleWebClients();
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
  drawSystemText("電子書列表", 20, 42, 40);
  
  // Draw "最後閱讀" button (top-right) if there's a saved last book
  {
    String lastBook = loadPrefStr("ereader", "lastBook", "");
    if (lastBook.length() > 0) {
      const int btnX = 380, btnY = 42, btnW = 140, btnH = 40;
      M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_BLACK);
      drawSystemTextCentered("最後閱讀", btnX + btnW / 2, btnY + btnH / 2 - 14, 28, TFT_WHITE, TFT_BLACK);
    }
  }
  
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);
  
  if (sdCardAvailable && bookCount > 0) {
    int totalBookPages;
    if (bookViewMode == 0) {
      // List view
      totalBookPages = (bookCount + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
    } else {
      // Grid view: 3 columns × 4 rows = 12 per page
      totalBookPages = (bookCount + 11) / 12;
    }
    if (bookListPage >= totalBookPages) bookListPage = totalBookPages - 1;
    if (bookListPage < 0) bookListPage = 0;

    if (bookViewMode == 0) {
      // ===== LIST VIEW =====
      int startIdx = bookListPage * BOOKS_PER_PAGE;
      int endIdx = min(startIdx + BOOKS_PER_PAGE, bookCount);
    
    // Show books for current page
    for (int i = startIdx; i < endIdx; i++) {
      int row = i - startIdx;
      bool isCN = (bookCategory[i] == CAT_CN);
      // Truncate long names to fit display width
      // Chinese books need room for 简/正 buttons at right (~100px)
      String displayName = bookDisplayName[i];
      int maxChars = isCN ? 10 : 15;  // fewer chars when buttons present (enlarged font)
      int len = displayName.length();
      if (len > 20) {
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
        if (charCount > maxChars) {
          int targetChars = maxChars - 1;
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
      // Draw category tag for non-auto books
      int textX = 15;
      BookCategory cat = bookCategory[i];
      if (cat != CAT_AUTO) {
        const char* tag = nullptr;
        switch (cat) {
          case CAT_EN:    tag = "EN"; break;
          case CAT_CN:    tag = "中"; break;
          case CAT_COMIC: tag = "漫"; break;
          default: break;
        }
        if (tag) {
          int tagY = 120 + (row * BOOK_ROW_HEIGHT) - 2;
          int tagW = 56, tagH = 42;
          M5.Display.drawRoundRect(textX, tagY, tagW, tagH, 4, TFT_BLACK);
          drawSystemTextCentered(tag, textX + tagW / 2, tagY + (tagH - 30) / 2, 30);
          textX += tagW + 6;
        }
      }
      drawSystemText(displayName.c_str(), textX, 120 + (row * BOOK_ROW_HEIGHT), 34);

      // Draw 简/正 buttons for Chinese books
      if (isCN) {
        int btnY = 120 + (row * BOOK_ROW_HEIGHT) - 2;
        int btnW = 56, btnH = 42;
        // 简 button
        int jianX = 405;
        M5.Display.drawRoundRect(jianX, btnY, btnW, btnH, 4, TFT_BLACK);
        drawSystemTextCentered("\xe7\xae\x80", jianX + btnW / 2, btnY + (btnH - 30) / 2, 30);  // 简
        // 正 button
        int zhengX = 467;
        M5.Display.drawRoundRect(zhengX, btnY, btnW, btnH, 4, TFT_BLACK);
        drawSystemTextCentered("\xe6\xad\xa3", zhengX + btnW / 2, btnY + (btnH - 30) / 2, 30);  // 正
      }
    }
    
    // Draw pagination nav arrows if needed
    bool hasPrev = (bookListPage > 0);
    bool hasNext = (bookListPage < totalBookPages - 1);
    if (totalBookPages > 1) {
      drawVerticalNavBar(hasPrev, hasNext);
    }
    
    // Status info + page indicator
    char bookInfoBuf[32];
    snprintf(bookInfoBuf, sizeof(bookInfoBuf), "共 %d 本書", bookCount);
    drawPageIndicator(bookListPage + 1, totalBookPages, bookInfoBuf);
    } else {
      // ===== GRID VIEW =====
      int cols = 3, gridRows = 4, gridPad = 8;
      int contentTop = 92;
      int contentBot = 835;
      int cellW = (DISPLAY_WIDTH - gridPad * (cols + 1)) / cols;
      int cellH = (contentBot - contentTop - gridPad * (gridRows + 1)) / gridRows;
      int perPage = cols * gridRows;  // 12
      int startIdx = bookListPage * perPage;
      int endIdx = min(startIdx + perPage, bookCount);

      for (int i = startIdx; i < endIdx; i++) {
        int slot = i - startIdx;
        int col = slot % cols;
        int row = slot / cols;
        int cx = gridPad + col * (cellW + gridPad);
        int cy = contentTop + gridPad + row * (cellH + gridPad);

        // Draw card
        M5.Display.drawRoundRect(cx, cy, cellW, cellH, 6, TFT_BLACK);

        // Category tag
        BookCategory cat = bookCategory[i];
        if (cat != CAT_AUTO) {
          const char* tag = nullptr;
          switch (cat) {
            case CAT_EN:    tag = "EN"; break;
            case CAT_CN:    tag = "中"; break;
            case CAT_COMIC: tag = "漫"; break;
            default: break;
          }
          if (tag) {
            int tagW = 40, tagH = 28;
            M5.Display.drawRoundRect(cx + 4, cy + 4, tagW, tagH, 3, TFT_BLACK);
            drawSystemTextCentered(tag, cx + 4 + tagW / 2, cy + 4 + (tagH - 20) / 2, 20);
          }
        }

        // Book title — wrap within cell
        String name = bookDisplayName[i];
        int textX = cx + 6;
        int textY = cy + 30;
        int maxW = cellW - 12;
        // Draw up to 3 lines of text
        int bytePos = 0;
        int nameLen = name.length();
        for (int line = 0; line < 3 && bytePos < nameLen; line++) {
          // Estimate chars per line: CJK ~26px at size 26, ASCII ~13px
          int lineW = 0;
          int lineStart = bytePos;
          while (bytePos < nameLen && lineW < maxW) {
            uint8_t c = (uint8_t)name[bytePos];
            int charW = (c < 0x80) ? 13 : 26;
            if (lineW + charW > maxW) break;
            lineW += charW;
            if (c < 0x80) bytePos += 1;
            else if (c < 0xE0) bytePos += 2;
            else if (c < 0xF0) bytePos += 3;
            else bytePos += 4;
          }
          String lineStr = name.substring(lineStart, bytePos);
          if (line == 2 && bytePos < nameLen) lineStr += "\xe2\x80\xa6";  // "…"
          drawSystemText(lineStr.c_str(), textX, textY + line * 30, 26);
        }

        // 简/正 buttons at bottom of card for Chinese books
        if (bookCategory[i] == CAT_CN) {
          int btnW = 44, btnH = 30;
          int btnY2 = cy + cellH - btnH - 4;
          // 简 button (left)
          int jX = cx + cellW / 2 - btnW - 4;
          M5.Display.drawRoundRect(jX, btnY2, btnW, btnH, 3, TFT_BLACK);
          drawSystemTextCentered("\xe7\xae\x80", jX + btnW / 2, btnY2 + (btnH - 20) / 2, 20);  // 简
          // 正 button (right)
          int zX = cx + cellW / 2 + 4;
          M5.Display.drawRoundRect(zX, btnY2, btnW, btnH, 3, TFT_BLACK);
          drawSystemTextCentered("\xe6\xad\xa3", zX + btnW / 2, btnY2 + (btnH - 20) / 2, 20);  // 正
        }
      }

      // Draw pagination nav arrows if needed
      bool hasPrev = (bookListPage > 0);
      bool hasNext = (bookListPage < totalBookPages - 1);
      if (totalBookPages > 1) {
        drawVerticalNavBar(hasPrev, hasNext);
      }

      // Status info + page indicator
      char bookInfoBuf2[32];
      snprintf(bookInfoBuf2, sizeof(bookInfoBuf2), "共 %d 本書", bookCount);
      drawPageIndicator(bookListPage + 1, totalBookPages, bookInfoBuf2);
    }
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
  if (pageRefreshMode == 2) {
    // Mode 2: fastest only — never do quality refresh (fastest page turns, may show ghosting)
    return epd_mode_t::epd_fastest;
  }
  // Mode 0: fastest with periodic quality refresh every 20 pages for cleanup
  if (pagesSinceFullRefresh >= 20) {
    pagesSinceFullRefresh = 0;
  }
  if (pagesSinceFullRefresh == 0) {
    pagesSinceFullRefresh++;
    return epd_mode_t::epd_quality;
  }
  pagesSinceFullRefresh++;
  return epd_mode_t::epd_fastest;
}

void drawReading() {
  Serial.printf("\n=== drawReading() === heap=%u, psram=%u, ofrLoaded=%d, ofrFiles=%d, stack=%u\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), (int)ofrFontLoaded, (int)ofr_file_list.size(),
                uxTaskGetStackHighWaterMark(NULL));
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
      if (webServerRunning) handleWebClients();
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

// Generate virtual TOC for plain text files by scanning for 第X回/章/節/篇/卷 patterns.
// Reads file in chunks from SD card to avoid loading entire file into memory.
bool txtGenerateVirtualToc() {
  if (currentBookPath.length() == 0 || totalBookBytes == 0) return false;

  epubFreeToc();

  epubTocEntries = (TocEntry*)ps_malloc(sizeof(TocEntry) * MAX_TOC_ENTRIES);
  if (!epubTocEntries) return false;
  for (int e = 0; e < MAX_TOC_ENTRIES; e++) new (&epubTocEntries[e]) TocEntry();

  const size_t CHUNK_SIZE = 8192;
  const size_t OVERLAP = 256;  // Overlap between chunks to catch patterns at boundaries
  char* buf = (char*)ps_malloc(CHUNK_SIZE + OVERLAP + 1);
  if (!buf) { epubFreeToc(); return false; }

  ScopedSDLock lock;
  File file = SD.open(currentBookPath.c_str());
  if (!file) { free(buf); epubFreeToc(); return false; }

  size_t fileSize = file.size();
  size_t filePos = 0;
  size_t carryOver = 0;  // Bytes carried over from previous chunk

  while (filePos < fileSize && epubTocCount < MAX_TOC_ENTRIES) {
    size_t toRead = min(CHUNK_SIZE, fileSize - filePos);
    // Read into buffer after any carry-over bytes
    size_t bytesRead = file.read((uint8_t*)(buf + carryOver), toRead);
    if (bytesRead == 0) break;
    size_t totalBytes = carryOver + bytesRead;
    buf[totalBytes] = '\0';

    // The base offset for byte 0 of this buffer in the file
    size_t bufBaseOffset = filePos - carryOver;

    // Scan for 第 (E7 AC AC in UTF-8)
    int pos = 0;
    while (pos < (int)totalBytes && epubTocCount < MAX_TOC_ENTRIES) {
      // Quick byte scan for 第 (0xE7 0xAC 0xAC) or 卷 (0xE5 0x8D 0xB7) at line start
      unsigned char b0 = (unsigned char)buf[pos];
      bool isDi = (b0 == 0xE7 && pos + 2 < (int)totalBytes &&
                   (unsigned char)buf[pos+1] == 0xAC && (unsigned char)buf[pos+2] == 0xAC);
      bool isJuanAtLineStart = false;
      if (!isDi && b0 == 0xE5 && pos + 2 < (int)totalBytes &&
          (unsigned char)buf[pos+1] == 0x8D && (unsigned char)buf[pos+2] == 0xB7) {
        isJuanAtLineStart = (bufBaseOffset + pos == 0) ||
                            (pos > 0 && ((unsigned char)buf[pos-1] == '\n' || (unsigned char)buf[pos-1] == '\r'));
      }
      if (!isDi && !isJuanAtLineStart) { pos++; continue; }

      int headingStart = pos;
      int scanPos = pos + 3;  // Skip past 第

      // Scan number part
      bool hasNumber = false;
      uint32_t terminator = 0;
      while (scanPos < (int)totalBytes) {
        int beforeDecode = scanPos;
        uint32_t numCp = utf8Decode(buf, scanPos);
        bool isCJKNum = (numCp == 0x4E00 || numCp == 0x4E8C || numCp == 0x4E09 || numCp == 0x56DB ||
                         numCp == 0x4E94 || numCp == 0x516D || numCp == 0x4E03 || numCp == 0x516B ||
                         numCp == 0x4E5D || numCp == 0x5341 || numCp == 0x767E || numCp == 0x5343 ||
                         numCp == 0x96F6 || numCp == 0x3007);
        bool isDigit = (numCp >= '0' && numCp <= '9') ||
                       (numCp >= 0xFF10 && numCp <= 0xFF19);
        if (isCJKNum || isDigit) {
          hasNumber = true;
        } else if (isDi && hasNumber &&
                   (numCp == 0x56DE || numCp == 0x7AE0 || numCp == 0x7BC0 ||
                    numCp == 0x7BC7 || numCp == 0x5377)) {
          terminator = numCp;
          break;
        } else {
          scanPos = beforeDecode;
          break;
        }
      }

      // For 第+number+terminator, both must be present
      if (isDi && (!hasNumber || terminator == 0)) { pos++; continue; }
      // For 卷+number, just need a number after 卷
      if (isJuanAtLineStart && !hasNumber) { pos++; continue; }

      // Extract heading label (up to 60 chars from heading start)
      int labelEnd = scanPos;

      // If the rest of this line (after terminator) is only whitespace,
      // merge the next line into the heading (e.g. "第一回\n靈根育孕源流出...")
      {
        int peekPos = labelEnd;
        bool restIsEmpty = true;
        while (peekPos < (int)totalBytes) {
          unsigned char pc = (unsigned char)buf[peekPos];
          if (pc == '\n' || pc == '\r') break;
          int tmpPos = peekPos;
          uint32_t peekCp = utf8Decode(buf, tmpPos);
          if (peekCp != ' ' && peekCp != '\t' && peekCp != 0x3000) {
            restIsEmpty = false;
            break;
          }
          peekPos = tmpPos;
        }
        if (restIsEmpty && peekPos < (int)totalBytes) {
          unsigned char nc = (unsigned char)buf[peekPos];
          if (nc == '\r') peekPos++;
          if (peekPos < (int)totalBytes && (unsigned char)buf[peekPos] == '\n') peekPos++;
          if (peekPos < (int)totalBytes) {
            labelEnd = peekPos;  // Start scanning from the next line
          }
        }
      }

      // Skip colon right after terminator (e.g., 第一回：title → 第一回 title)
      if (labelEnd < (int)totalBytes) {
        int peekColon = labelEnd;
        uint32_t colonCp = utf8Decode(buf, peekColon);
        if (colonCp == ':' || colonCp == 0xFF1A) labelEnd = peekColon;
      }

      // Scan line: title ends at line ending, or at last space before
      // first punctuation (or at punctuation itself if no space precedes it)
      int charCount = 0;
      int firstPunctByte = -1;
      int lastSpaceBeforePunct = -1;
      while (labelEnd < (int)totalBytes && charCount < 60) {
        unsigned char c = (unsigned char)buf[labelEnd];
        if (c == '\n' || c == '\r') break;
        int cpStart = labelEnd;
        uint32_t lineCp = utf8Decode(buf, labelEnd);
        if (isPunctuation(lineCp)) {
          if (firstPunctByte < 0) firstPunctByte = cpStart;
        }
        if (firstPunctByte < 0 && (lineCp == ' ' || lineCp == 0x3000))
          lastSpaceBeforePunct = cpStart;
        charCount++;
      }

      int titleEnd;
      if (firstPunctByte < 0) {
        titleEnd = labelEnd;
      } else if (lastSpaceBeforePunct > scanPos) {
        titleEnd = lastSpaceBeforePunct;
      } else {
        titleEnd = firstPunctByte;
      }

      String label = String(buf + headingStart, titleEnd - headingStart);
      label.replace("\r\n", " ");
      label.replace("\r", " ");
      label.replace("\n", " ");
      label.replace("\xef\xbc\x9a", " ");  // U+FF1A fullwidth colon → space
      label.replace(":", " ");             // ASCII colon → space
      while (label.indexOf("  ") >= 0) label.replace("  ", " ");
      label.trim();

      // Insert a space after the chapter marker if none exists
      if (isJuanAtLineStart) {
        // For 卷+number, insert space after the number part
        int spacePos = scanPos - headingStart;
        if (spacePos > 0 && spacePos < (int)label.length()) {
          int peekPos = spacePos;
          uint32_t nextCp = utf8Decode(label, peekPos);
          if (nextCp != ' ' && nextCp != 0x3000) {
            label = label.substring(0, spacePos) + " " + label.substring(spacePos);
          }
        }
      } else {
        // For 第+number+terminator, insert space after the terminator character
        int lp = 0, lLen = label.length();
        while (lp < lLen) {
          uint32_t lCp = utf8Decode(label, lp);
          if (lCp == 0x56DE || lCp == 0x7AE0 || lCp == 0x7BC0 || lCp == 0x7BC7 || lCp == 0x5377) {
            if (lp < lLen) {
              int peekPos = lp;
              uint32_t nextCp = utf8Decode(label, peekPos);
              if (nextCp != ' ' && nextCp != 0x3000) {
                label = label.substring(0, lp) + " " + label.substring(lp);
              }
            }
            break;
          }
        }
      }

      if (label.length() > 0) {
        size_t absOffset = bufBaseOffset + headingStart;
        // Avoid duplicates
        bool duplicate = false;
        for (int d = 0; d < epubTocCount; d++) {
          if (epubTocEntries[d].byteOffset == absOffset) { duplicate = true; break; }
        }
        if (!duplicate) {
          epubTocEntries[epubTocCount].label = label;
          epubTocEntries[epubTocCount].chapterIndex = 0;
          epubTocEntries[epubTocCount].byteOffset = absOffset;
          epubTocCount++;
          Serial.printf("TXT TOC[%d]: \"%s\" @ %u\n", epubTocCount - 1, label.c_str(), (unsigned)absOffset);
        }
      }

      pos = scanPos;  // Continue scanning after this match
    }

    // Advance file position, keeping overlap for boundary patterns
    filePos += bytesRead;
    if (filePos < fileSize && totalBytes > OVERLAP) {
      carryOver = OVERLAP;
      memmove(buf, buf + totalBytes - OVERLAP, OVERLAP);
    } else {
      carryOver = 0;
    }
    yield();
  }

  file.close();
  free(buf);

  Serial.printf("TXT Virtual TOC: %d entries generated\n", epubTocCount);

  if (epubTocCount == 0) {
    epubFreeToc();
    return false;
  }
  return true;
}

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

  // TOC layout depends on language
  int tocFontSize = isReadingFontSilver() ? silverScaledSize(32) : 32;
  int tocRowH = epubIsHorizontal ? 46 : 42;
  int maxVisualRows = (914 - listStartY) / tocRowH - 1;  // Reserve last row for page indicator
  // Max chars per line (CJK chars at tocFontSize ~32px, display width 540, left margin 40)
  int maxCharsPerLine = 15;

  auto displayTocLabel = [](const String& label) -> String {
    String converted = label;
    if (bookConvMode != CONV_ORIGINAL) applyConversion(converted, (ConvMode)bookConvMode);
    return converted;
  };

  if (tocTab == 0) {
    // === Chapter list ===
    if (epubTocEntries && epubTocCount > 0) {

      // Load reading font TTF for TOC chapter titles.
      // When reading font is BIN, OFR may hold ETBook (Latin-only) → squares.
      // Load the parent TTF (fontFileList[readingFontIndex]) which has CJK.
      {
        String tocFont;
        if (readingFontIndex >= 0 && readingFontIndex < fontFileCount) {
          tocFont = fontFileList[readingFontIndex];
        }
        if (tocFont.length() > 0 && tocFont != "ETBook-embedded") {
          loadTTFFont(tocFont.c_str(), tocFontSize);
        } else {
          loadSystemFont();
        }
      }
      if (ofrFontLoaded) {
        ofr.setFontSize(tocFontSize);
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
        ofr.setDrawer(M5.Display);
      }

      auto lineCapacity = [&](int indentChars) -> int {
        int capacity = maxCharsPerLine - indentChars;
        return capacity < 4 ? 4 : capacity;
      };

      // Helper: count unicode chars in prefix up to the chapter terminator (not including the space)
      // Returns the number of CJK chars to use as indent (using full-width spaces) for continuation lines
      // Also returns whether a space follows the terminator (to replicate it in the indent)
      auto countIndentChars = [](const String& label, bool& hasSpace) -> int {
        int labelLen = label.length();
        int bp = 0;
        int charCount = 0;
        bool foundTerminator = false;
        hasSpace = false;
        while (bp < labelLen) {
          uint32_t cp = utf8Decode(label.c_str(), bp);
          charCount++;
          if (!foundTerminator) {
            if (cp == 0x56DE || cp == 0x7AE0 || cp == 0x7BC0 || cp == 0x7BC7 || cp == 0x5377)
              foundTerminator = true;
          } else {
            // First char after terminator — check if it's a space
            if (cp == ' ' || cp == 0x3000) { hasSpace = true; return charCount - 1; }
            return charCount - 1;
          }
        }
        return 0;
      };

      // Helper: count lines needed for a TOC entry with space-based indent
      auto countEntryLines = [&](const String& label, int indentChars) -> int {
        int labelLen = label.length();
        int bp = 0;
        int charCount = 0;
        int lineChars = 0;
        int lines = 1;
        bool firstLine = true;
        int maxOnLine = firstLine ? maxCharsPerLine : lineCapacity(indentChars);
        int lastSpaceBp = -1;
        int lastSpaceCharIdx = -1;
        int lastSpaceLineChars = 0;
        int lineStartBp = 0;
        int lineStartChar = 0;

        while (bp < labelLen) {
          int prevBp = bp;
          uint32_t cp = utf8Decode(label.c_str(), bp);
          lineChars++;
          charCount++;
          if (cp == ' ' || cp == 0x3000) {
            lastSpaceBp = prevBp;
            lastSpaceCharIdx = charCount;
            lastSpaceLineChars = lineChars;
          }
          if (lineChars > maxOnLine) {
            // Only break at space if it gives a reasonably long line
            // (avoid breaking right after 第X回 which is too short)
            if (lastSpaceBp > lineStartBp && lastSpaceLineChars >= maxOnLine / 2) {
              bp = lastSpaceBp;
              // Skip the space
              utf8Decode(label.c_str(), bp);
            }
            lines++;
            firstLine = false;
            maxOnLine = lineCapacity(indentChars);
            lineStartBp = bp;
            lineStartChar = charCount;
            lineChars = 0;
            lastSpaceBp = -1;
            lastSpaceLineChars = 0;
          }
        }
        return lines;
      };

      auto tocEntryRows = [&](const String& label) -> int {
        bool hasSpace = false;
        int indentChars = countIndentChars(label, hasSpace);
        int totalIndent = indentChars + (hasSpace ? 1 : 0);
        int rows = countEntryLines(label, totalIndent);
        if (rows < 1) rows = 1;
        if (rows > maxVisualRows) rows = maxVisualRows;
        return rows;
      };

      int totalTocPages = 1;
      {
        int tmpEntry = 0, tmpRow = 0, tmpPage = 0;
        while (tmpEntry < epubTocCount) {
          String label = displayTocLabel(epubTocEntries[tmpEntry].label);
          int lines = tocEntryRows(label);
          if (tmpRow + lines > maxVisualRows) {
            tmpPage++;
            tmpRow = 0;
          }
          tmpRow += lines;
          tmpEntry++;
        }
        totalTocPages = tmpPage + 1;
      }

      if (tocListPage >= totalTocPages) tocListPage = totalTocPages - 1;
      if (tocListPage < 0) tocListPage = 0;

      // Skip entries for previous pages
      int entryIdx = 0;
      for (int p = 0; p < tocListPage && entryIdx < epubTocCount; p++) {
        int rowsUsed = 0;
        while (entryIdx < epubTocCount && rowsUsed < maxVisualRows) {
          String label = displayTocLabel(epubTocEntries[entryIdx].label);
          int lines = tocEntryRows(label);
          if (rowsUsed + lines > maxVisualRows) break;
          rowsUsed += lines;
          entryIdx++;
        }
      }

      // Build indent string: full-width spaces for CJK chars, plus the space after terminator
      auto makeIndent = [](int n, bool addSpace) -> String {
        String s;
        for (int i = 0; i < n; i++) s += "\xe3\x80\x80";  // U+3000
        if (addSpace) s += " ";
        return s;
      };

      // Now render entries for the current page
      int visualRow = 0;
      tocVisualRowCount = 0;

      for (int i = entryIdx; i < epubTocCount && visualRow < maxVisualRows; i++) {
        String label = displayTocLabel(epubTocEntries[i].label);
        int labelLen = label.length();
        bool hasSpace = false;
        int indentChars = countIndentChars(label, hasSpace);
        int totalIndent = indentChars + (hasSpace ? 1 : 0);
        String indentStr = makeIndent(indentChars, hasSpace);

        // Break title into lines by character count, breaking at spaces
        String displayLines[MAX_TOC_VISUAL_ROWS];
        int lineCount = 0;
        int bp = 0;
        int lineChars = 0;
        int lineStartBp = 0;
        int lastSpaceBp = -1;
        int lastSpaceBpAfter = -1;
        int lastSpaceLineChars = 0;
        bool firstLine = true;
        int maxOnLine = maxCharsPerLine;

        while (bp < labelLen && lineCount < MAX_TOC_VISUAL_ROWS) {
          int prevBp = bp;
          uint32_t cp = utf8Decode(label.c_str(), bp);
          lineChars++;
          if (cp == ' ' || cp == 0x3000) {
            lastSpaceBp = prevBp;
            lastSpaceBpAfter = bp;
            lastSpaceLineChars = lineChars;
          }
          if (lineChars > maxOnLine) {
            // Line overflow — break at last space if it gives a reasonably long line
            // (avoid breaking right after 第X回 which looks ugly)
            if (lastSpaceBp > lineStartBp && lastSpaceLineChars >= maxOnLine / 2) {
              displayLines[lineCount++] = label.substring(lineStartBp, lastSpaceBp);
              bp = lastSpaceBpAfter;
            } else {
              displayLines[lineCount++] = label.substring(lineStartBp, prevBp);
              bp = prevBp;
            }
            firstLine = false;
            maxOnLine = lineCapacity(totalIndent);
            lineStartBp = bp;
            lineChars = 0;
            lastSpaceBp = -1;
            lastSpaceLineChars = 0;
          }
        }
        // Remaining text
        if (lineStartBp < labelLen && lineCount < MAX_TOC_VISUAL_ROWS) {
          displayLines[lineCount++] = label.substring(lineStartBp, labelLen);
        }

        // Check if all lines fit on this page
        if (visualRow + lineCount > maxVisualRows) break;

        // Render each line
        for (int ln = 0; ln < lineCount; ln++) {
          int rowY = listStartY + visualRow * tocRowH;
          String lineTxt = (ln == 0) ? displayLines[ln] : indentStr + displayLines[ln];

          if (ofrFontLoaded) {
            ofr.drawString(lineTxt.c_str(), 40, rowY);
          } else {
            drawSystemText(lineTxt.c_str(), 40, rowY, tocFontSize);
          }

          if (visualRow < MAX_TOC_VISUAL_ROWS) {
            tocRowToEntry[visualRow] = i;
          }
          visualRow++;
        }
        tocVisualRowCount = visualRow;
        yield();
      }

      bool hasPrev = (tocListPage > 0);
      bool hasNext = (tocListPage < totalTocPages - 1);
      drawVerticalNavBar(hasPrev, hasNext);

      char tocInfoBuf[32];
      snprintf(tocInfoBuf, sizeof(tocInfoBuf), "%d 章", epubTocCount);
      drawPageIndicator(tocListPage + 1, totalTocPages, tocInfoBuf);
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
      drawSystemText(info.c_str(), 20, 830, 28);
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
