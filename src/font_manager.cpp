#include "globals.h"

// ==================== OpenFontRender SD File I/O Overrides ====================

FT_FILE *OFR_fopen(const char *filename, const char *mode) {
  File f = SD.open(filename, mode);
  if (!f) return NULL;
  ofr_file_list.push_back(f);
  return &ofr_file_list.back();
}

void OFR_fclose(FT_FILE *stream) {
  ((File *)stream)->close();
  for (auto it = ofr_file_list.begin(); it != ofr_file_list.end(); ++it) {
    if (&(*it) == (File *)stream) {
      ofr_file_list.erase(it);
      break;
    }
  }
}

size_t OFR_fread(void *ptr, size_t size, size_t nmemb, FT_FILE *stream) {
  return ((File *)stream)->read((uint8_t *)ptr, size * nmemb);
}

int OFR_fseek(FT_FILE *stream, long int offset, int whence) {
  return ((File *)stream)->seek(offset, (SeekMode)whence);
}

long int OFR_ftell(FT_FILE *stream) {
  return ((File *)stream)->position();
}

// ==================== Font Scanning ====================

// Extract font family name from a TTF/TTC file's 'name' table
// Prefers: platformID=3 (Windows), nameID=1 (Family), Chinese or English
// Returns empty string if unable to parse
static String extractTTFName(File &f) {
  uint32_t startPos = 0;
  
  // Check for TTC (collection) header
  uint8_t tag[4];
  f.seek(0);
  if (f.read(tag, 4) != 4) return "";
  
  if (tag[0] == 't' && tag[1] == 't' && tag[2] == 'c' && tag[3] == 'f') {
    // TTC file: read offset to first font
    f.seek(8); // skip version
    uint8_t buf4[4];
    if (f.read(buf4, 4) != 4) return "";
    uint32_t numFonts = ((uint32_t)buf4[0] << 24) | (buf4[1] << 16) | (buf4[2] << 8) | buf4[3];
    if (numFonts == 0) return "";
    if (f.read(buf4, 4) != 4) return "";
    startPos = ((uint32_t)buf4[0] << 24) | (buf4[1] << 16) | (buf4[2] << 8) | buf4[3];
  }
  
  // Read offset table
  f.seek(startPos + 4); // skip sfVersion
  uint8_t hdr[8];
  if (f.read(hdr, 8) != 8) return "";
  uint16_t numTables = (hdr[0] << 8) | hdr[1];
  
  // Find 'name' table
  uint32_t nameOffset = 0, nameLength = 0;
  for (int i = 0; i < numTables && i < 100; i++) {
    uint8_t rec[16];
    f.seek(startPos + 12 + i * 16);
    if (f.read(rec, 16) != 16) return "";
    if (rec[0] == 'n' && rec[1] == 'a' && rec[2] == 'm' && rec[3] == 'e') {
      nameOffset = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
      nameLength = ((uint32_t)rec[12] << 24) | (rec[13] << 16) | (rec[14] << 8) | rec[15];
      break;
    }
  }
  if (nameOffset == 0) return "";
  
  // Read name table header
  f.seek(nameOffset);
  uint8_t nameHdr[6];
  if (f.read(nameHdr, 6) != 6) return "";
  uint16_t nameCount = (nameHdr[2] << 8) | nameHdr[3];
  uint16_t stringOffset = (nameHdr[4] << 8) | nameHdr[5];
  uint32_t stringsBase = nameOffset + stringOffset;
  
  // Scan name records: look for nameID=1 (Font Family)
  // Priority: platform 3 (Windows) languageID 0x0404 (zh-TW) > 0x0804 (zh-CN) > 0x0409 (en-US)
  String bestName = "";
  int bestPriority = 0; // higher = better
  
  for (int i = 0; i < nameCount && i < 200; i++) {
    uint8_t nrec[12];
    f.seek(nameOffset + 6 + i * 12);
    if (f.read(nrec, 12) != 12) break;
    
    uint16_t platformID = (nrec[0] << 8) | nrec[1];
    uint16_t encodingID = (nrec[2] << 8) | nrec[3];
    uint16_t languageID = (nrec[4] << 8) | nrec[5];
    uint16_t nameID = (nrec[6] << 8) | nrec[7];
    uint16_t length = (nrec[8] << 8) | nrec[9];
    uint16_t offset = (nrec[10] << 8) | nrec[11];
    
    if (nameID != 1) continue; // Only want Font Family name
    if (length == 0 || length > 256) continue;
    
    int priority = 0;
    bool isUTF16 = false;
    
    if (platformID == 3) { // Windows
      isUTF16 = true;
      if (languageID == 0x0404) priority = 10;      // zh-TW
      else if (languageID == 0x0804) priority = 9;   // zh-CN
      else if (languageID == 0x0411) priority = 5;   // Japanese
      else if (languageID == 0x0409) priority = 4;   // en-US
      else priority = 2;
    } else if (platformID == 1) { // Macintosh
      if (encodingID == 2 || encodingID == 25) priority = 8; // Big5 / zh-TW
      else if (encodingID == 0) priority = 3; // Roman / English
      else priority = 1;
    }
    
    if (priority <= bestPriority) continue;
    
    // Read the string
    uint8_t* buf = (uint8_t*)malloc(length);
    if (!buf) continue;
    f.seek(stringsBase + offset);
    if (f.read(buf, length) != length) { free(buf); continue; }
    
    String result = "";
    if (isUTF16) {
      // UTF-16BE to UTF-8
      for (int j = 0; j + 1 < length; j += 2) {
        uint16_t ch = (buf[j] << 8) | buf[j + 1];
        if (ch < 0x80) {
          result += (char)ch;
        } else if (ch < 0x800) {
          result += (char)(0xC0 | (ch >> 6));
          result += (char)(0x80 | (ch & 0x3F));
        } else {
          result += (char)(0xE0 | (ch >> 12));
          result += (char)(0x80 | ((ch >> 6) & 0x3F));
          result += (char)(0x80 | (ch & 0x3F));
        }
      }
    } else {
      // Platform 1 (Mac) - assume ASCII-compatible
      for (int j = 0; j < length; j++) {
        if (buf[j] >= 32 && buf[j] < 127) result += (char)buf[j];
      }
    }
    free(buf);
    
    if (result.length() > 0) {
      bestName = result;
      bestPriority = priority;
    }
  }
  
  return bestName;
}

// Check if a TTF/TTC/OTF font supports CJK Unified Ideographs
// by reading the OS/2 table's ulUnicodeRange2 field (bit 59 = CJK Unified Ideographs)
static bool ttfHasCJK(File &f) {
  uint32_t startPos = 0;

  uint8_t tag[4];
  f.seek(0);
  if (f.read(tag, 4) != 4) return false;

  if (tag[0] == 't' && tag[1] == 't' && tag[2] == 'c' && tag[3] == 'f') {
    f.seek(8);
    uint8_t buf4[4];
    if (f.read(buf4, 4) != 4) return false;
    uint32_t numFonts = ((uint32_t)buf4[0] << 24) | (buf4[1] << 16) | (buf4[2] << 8) | buf4[3];
    if (numFonts == 0) return false;
    if (f.read(buf4, 4) != 4) return false;
    startPos = ((uint32_t)buf4[0] << 24) | (buf4[1] << 16) | (buf4[2] << 8) | buf4[3];
  }

  f.seek(startPos + 4);
  uint8_t hdr[8];
  if (f.read(hdr, 8) != 8) return false;
  uint16_t numTables = (hdr[0] << 8) | hdr[1];

  uint32_t os2Offset = 0;
  for (int i = 0; i < numTables && i < 100; i++) {
    uint8_t rec[16];
    f.seek(startPos + 12 + i * 16);
    if (f.read(rec, 16) != 16) return false;
    if (rec[0] == 'O' && rec[1] == 'S' && rec[2] == '/' && rec[3] == '2') {
      os2Offset = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
      break;
    }
  }
  if (os2Offset == 0) return false;

  // ulUnicodeRange2 is at offset 46 in the OS/2 table (big-endian)
  f.seek(os2Offset + 46);
  uint8_t range2[4];
  if (f.read(range2, 4) != 4) return false;
  uint32_t ulRange2 = ((uint32_t)range2[0] << 24) | (range2[1] << 16) | (range2[2] << 8) | range2[3];
  // Bit 59 overall = bit 27 in ulUnicodeRange2 = CJK Unified Ideographs
  return (ulRange2 & (1u << 27)) != 0;
}

// Check if a .bin font contains CJK characters by sampling the glyph index
static bool binHasCJK(File &f) {
  f.seek(0);
  uint8_t header[5];
  if (f.read(header, 5) != 5) return false;
  uint32_t charCount = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
  if (charCount == 0) return false;

  // Sample a few entries from the glyph index (starts at offset 137, 20 bytes each)
  // Check ~8 evenly spaced entries for CJK codepoints (U+4E00-U+9FFF)
  int samples = (charCount < 8) ? charCount : 8;
  for (int s = 0; s < samples; s++) {
    uint32_t idx = (uint32_t)s * charCount / samples;
    f.seek(137 + idx * 20);
    uint8_t entry[4];
    if (f.read(entry, 4) != 4) continue;
    uint32_t unicode = entry[0] | (entry[1] << 8) | (entry[2] << 16) | (entry[3] << 24);
    if (unicode >= 0x4E00 && unicode <= 0x9FFF) return true;
  }
  return false;
}

// Extract family name from a .bin font file header
static String extractBinFontName(File &f) {
  f.seek(0);
  uint8_t header[137];
  if (f.read(header, 137) != 137) return "";
  
  char familyName[65];
  memcpy(familyName, &header[9], 64);
  familyName[64] = '\0';
  
  String name = String(familyName);
  name.trim();
  return name;
}

void scanFontFiles() {
  fontFileCount = 0;
  
  if (!sdCardAvailable) return;
  
  File fontsDir;
  if (sdMutex != NULL) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    fontsDir = SD.open("/fonts");
    xSemaphoreGive(sdMutex);
  } else {
    fontsDir = SD.open("/fonts");
  }
  
  if (!fontsDir || !fontsDir.isDirectory()) return;
  
  File entry = fontsDir.openNextFile();
  while (entry && fontFileCount < MAX_FONT_FILES) {
    String name = String(entry.name());
    if (!name.startsWith("._") && !name.startsWith(".")) {
      if (name.endsWith(".ttf") || name.endsWith(".TTF") ||
          name.endsWith(".ttc") || name.endsWith(".TTC") ||
          name.endsWith(".otf") || name.endsWith(".OTF")) {
        if (!ttfHasCJK(entry)) {
          Serial.printf("  Skipped (no CJK): %s\n", name.c_str());
        } else {
          String displayName = extractTTFName(entry);
          fontFileList[fontFileCount] = name;
          fontDisplayNames[fontFileCount] = (displayName.length() > 0) ? displayName : name;
          Serial.printf("  Font found: %s → %s\n", name.c_str(), fontDisplayNames[fontFileCount].c_str());
          fontFileCount++;
        }
      } else if (name.endsWith(".bin") || name.endsWith(".BIN")) {
        if (!binHasCJK(entry)) {
          Serial.printf("  Skipped (no CJK): %s\n", name.c_str());
        } else {
          String displayName = extractBinFontName(entry);
          fontFileList[fontFileCount] = name;
          fontDisplayNames[fontFileCount] = (displayName.length() > 0) ? displayName : name;
          Serial.printf("  Font found: %s → %s\n", name.c_str(), fontDisplayNames[fontFileCount].c_str());
          fontFileCount++;
        }
      }
    }
    entry.close();
    entry = fontsDir.openNextFile();
  }
  fontsDir.close();
  
  // Sort fonts by display name
  for (int i = 0; i < fontFileCount - 1; i++) {
    for (int j = i + 1; j < fontFileCount; j++) {
      String a = fontDisplayNames[i];
      String b = fontDisplayNames[j];
      a.toLowerCase();
      b.toLowerCase();
      if (a > b) {
        // Swap both arrays
        String tmp = fontFileList[i];
        fontFileList[i] = fontFileList[j];
        fontFileList[j] = tmp;
        tmp = fontDisplayNames[i];
        fontDisplayNames[i] = fontDisplayNames[j];
        fontDisplayNames[j] = tmp;
      }
    }
  }
  
  Serial.printf("Total fonts found: %d\n", fontFileCount);
}

// ==================== Binary Font Loading ====================

bool loadBinaryFont(const char* fontPath) {
  Serial.printf("\n=== loadBinaryFont() called ===\n");
  Serial.printf("Input path: '%s'\n", fontPath);
  
  // Clear glyph cache from previous font
  clearGlyphCache();
  
  if (!sdCardAvailable) {
    Serial.println("✗ SD card not available");
    return false;
  }
  
  String fullPath;
  if (fontPath[0] == '/') {
    fullPath = String(fontPath);
  } else {
    fullPath = String("/fonts/") + fontPath;
  }
  
  Serial.printf("Full path: '%s'\n", fullPath.c_str());
  Serial.println("Attempting to open font file...");
  
  if (sdMutex != NULL) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
  }
  
  g_binFont.fontFile = SD.open(fullPath.c_str());
  
  if (sdMutex != NULL) {
    xSemaphoreGive(sdMutex);
  }
  
  if (!g_binFont.fontFile) {
    Serial.printf("✗ Failed to open font file: %s\n", fullPath.c_str());
    Serial.println("File does not exist or is not accessible");
    return false;
  }
  
  Serial.printf("✓ Font file opened successfully (size: %d bytes)\n", g_binFont.fontFile.size());
  
  uint8_t header[137];
  size_t bytesRead = g_binFont.fontFile.read(header, 137);
  if (bytesRead != 137) {
    Serial.printf("✗ Header read error: got %d bytes\n", bytesRead);
    g_binFont.fontFile.close();
    return false;
  }
  
  g_binFont.charCount = *((uint32_t*)&header[0]);
  g_binFont.fontSize = header[4];
  g_binFont.version = *((uint32_t*)&header[5]);
  memcpy(g_binFont.familyName, &header[9], 64);
  memcpy(g_binFont.styleName, &header[73], 64);
  
  Serial.println("=== Binary Font Header ===");
  Serial.printf("Characters: %d\n", g_binFont.charCount);
  Serial.printf("Font Size: %d pt\n", g_binFont.fontSize);
  Serial.printf("Version: %d\n", g_binFont.version);
  Serial.printf("Family: %s\n", g_binFont.familyName);
  Serial.printf("Style: %s\n", g_binFont.styleName);
  
  size_t indexSize = g_binFont.charCount * 20;
  Serial.printf("Header size: 137 bytes\n");
  Serial.printf("Allocating index: %d bytes\n", indexSize);
  
  g_binFont.index = (GlyphIndex*)ps_malloc(g_binFont.charCount * sizeof(GlyphIndex));
  if (!g_binFont.index) {
    Serial.println("✗ Failed to allocate glyph index");
    g_binFont.fontFile.close();
    return false;
  }
  
  Serial.println("Reading glyph index (bulk)...");
  {
    // Bulk-read entire index from SD in one operation (vs N individual reads)
    size_t rawSize = g_binFont.charCount * 20;
    uint8_t* rawIndex = (uint8_t*)ps_malloc(rawSize);
    if (!rawIndex) {
      Serial.println("✗ Failed to allocate raw index buffer");
      free(g_binFont.index);
      g_binFont.fontFile.close();
      return false;
    }
    bytesRead = g_binFont.fontFile.read(rawIndex, rawSize);
    if (bytesRead != rawSize) {
      Serial.printf("✗ Index bulk read error: got %d of %d bytes\n", bytesRead, rawSize);
      free(rawIndex);
      free(g_binFont.index);
      g_binFont.fontFile.close();
      return false;
    }
    // Parse raw bytes into GlyphIndex structs
    bool hasV2Bearings = (g_binFont.version >= 2);
    for (uint32_t i = 0; i < g_binFont.charCount; i++) {
      uint8_t* e = rawIndex + i * 20;
      g_binFont.index[i].unicode = e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24);
      g_binFont.index[i].width = e[4] | (e[5] << 8);
      g_binFont.index[i].height = e[6] | (e[7] << 8);
      g_binFont.index[i].bitmapOffset = e[8] | (e[9] << 8) | (e[10] << 16) | (e[11] << 24);
      g_binFont.index[i].bitmapSize = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
      if (hasV2Bearings) {
        g_binFont.index[i].bearingX = (int16_t)(e[16] | (e[17] << 8));
        g_binFont.index[i].bearingY = (int16_t)(e[18] | (e[19] << 8));
      } else {
        g_binFont.index[i].bearingX = 0;
        g_binFont.index[i].bearingY = 0;
      }
      if (i < 3) {
        Serial.printf("  [%d] U+%04X %dx%d offset=%d size=%d bearing=(%d,%d)\n",
          i, g_binFont.index[i].unicode,
          g_binFont.index[i].width, g_binFont.index[i].height,
          g_binFont.index[i].bitmapOffset, g_binFont.index[i].bitmapSize,
          g_binFont.index[i].bearingX, g_binFont.index[i].bearingY);
      }
    }
    free(rawIndex);
    Serial.printf("Parsed %d glyph entries\n", g_binFont.charCount);
  }
  
  std::sort(g_binFont.index, g_binFont.index + g_binFont.charCount,
    [](const GlyphIndex& a, const GlyphIndex& b) { return a.unicode < b.unicode; });
  Serial.println("Glyph index sorted by unicode");
  
  g_binFont.loaded = true;
  Serial.printf("✓ Binary font loaded: %d glyphs in index\n", g_binFont.charCount);
  Serial.printf("Free PSRAM after index: %d bytes\n", ESP.getFreePsram());
  
  return true;
}

// ==================== Glyph Lookup & Drawing ====================

// Glyph bitmap cache — avoids repeated SD reads for the same character
// Uses a simple hash table with open addressing for O(1) lookup
static const int GLYPH_CACHE_SIZE = 512;  // Must be power of 2
struct CachedGlyph {
  uint32_t unicode;
  uint8_t* bitmap;
  uint16_t bitmapSize;
  uint16_t width;
  uint16_t height;
  bool occupied;
};
static CachedGlyph* glyphCache = nullptr;
static uint8_t* glyphBitmapPool = nullptr;  // Contiguous PSRAM pool for bitmap data
static size_t glyphPoolUsed = 0;
static const size_t GLYPH_POOL_SIZE = 192 * 1024;  // 192KB for cached bitmaps

static void initGlyphCache() {
  if (glyphCache) return;  // Already initialized
  glyphCache = (CachedGlyph*)ps_calloc(GLYPH_CACHE_SIZE, sizeof(CachedGlyph));
  glyphBitmapPool = (uint8_t*)ps_malloc(GLYPH_POOL_SIZE);
  if (!glyphCache || !glyphBitmapPool) {
    Serial.println("Glyph cache alloc failed");
    if (glyphCache) { free(glyphCache); glyphCache = nullptr; }
    if (glyphBitmapPool) { free(glyphBitmapPool); glyphBitmapPool = nullptr; }
    return;
  }
  glyphPoolUsed = 0;
  Serial.printf("Glyph cache initialized: %d slots, %dKB pool\n", GLYPH_CACHE_SIZE, GLYPH_POOL_SIZE / 1024);
}

void clearGlyphCache() {
  if (glyphCache) {
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) glyphCache[i].occupied = false;
    glyphPoolUsed = 0;
  }
}

static CachedGlyph* cacheFind(uint32_t unicode) {
  if (!glyphCache) return nullptr;
  uint32_t idx = (unicode * 2654435761u) & (GLYPH_CACHE_SIZE - 1);  // Knuth hash
  for (int probe = 0; probe < 8; probe++) {
    uint32_t slot = (idx + probe) & (GLYPH_CACHE_SIZE - 1);
    if (!glyphCache[slot].occupied) return nullptr;
    if (glyphCache[slot].unicode == unicode) return &glyphCache[slot];
  }
  return nullptr;
}

static void cacheInsert(uint32_t unicode, const uint8_t* bitmap, uint16_t bitmapSize, uint16_t w, uint16_t h) {
  if (!glyphCache || !glyphBitmapPool) return;
  if (glyphPoolUsed + bitmapSize > GLYPH_POOL_SIZE) return;  // Pool full

  uint32_t idx = (unicode * 2654435761u) & (GLYPH_CACHE_SIZE - 1);
  for (int probe = 0; probe < 8; probe++) {
    uint32_t slot = (idx + probe) & (GLYPH_CACHE_SIZE - 1);
    if (!glyphCache[slot].occupied) {
      glyphCache[slot].unicode = unicode;
      glyphCache[slot].bitmap = glyphBitmapPool + glyphPoolUsed;
      memcpy(glyphCache[slot].bitmap, bitmap, bitmapSize);
      glyphCache[slot].bitmapSize = bitmapSize;
      glyphCache[slot].width = w;
      glyphCache[slot].height = h;
      glyphCache[slot].occupied = true;
      glyphPoolUsed += bitmapSize;
      return;
    }
  }
}

GlyphIndex* findGlyph(uint32_t unicode) {
  if (!g_binFont.loaded || !g_binFont.index || g_binFont.charCount == 0) return nullptr;
  
  int32_t low = 0;
  int32_t high = (int32_t)g_binFont.charCount - 1;
  while (low <= high) {
    int32_t mid = low + (high - low) / 2;
    uint32_t midVal = g_binFont.index[mid].unicode;
    if (midVal == unicode) {
      return &g_binFont.index[mid];
    } else if (midVal < unicode) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  
  return nullptr;
}

bool drawBinFontChar(uint32_t unicode, int x, int y, uint16_t color, float scale) {
  if (!g_binFont.loaded) return false;
  
  GlyphIndex* glyph = findGlyph(unicode);
  if (!glyph) {
    return false;
  }
  if (glyph->bitmapSize == 0 || glyph->width == 0 || glyph->height == 0) {
    return false;
  }
  
  // Initialize cache on first use
  if (!glyphCache) initGlyphCache();
  
  const uint8_t* bitmap = nullptr;
  // Static reusable buffer to avoid per-char malloc (max glyph at 30pt ~112 bytes)
  static uint8_t* reuseBuf = nullptr;
  static size_t reuseBufSize = 0;
  
  // Check glyph cache first (avoids SD read)
  CachedGlyph* cached = cacheFind(unicode);
  if (cached) {
    bitmap = cached->bitmap;
  } else {
    // Read from SD card
    if (glyph->bitmapSize > reuseBufSize) {
      if (reuseBuf) free(reuseBuf);
      reuseBufSize = glyph->bitmapSize + 64;  // Slight overalloc to avoid frequent reallocs
      reuseBuf = (uint8_t*)malloc(reuseBufSize);
      if (!reuseBuf) { reuseBufSize = 0; return false; }
    }
    
    if (sdMutex != NULL) xSemaphoreTake(sdMutex, portMAX_DELAY);
    g_binFont.fontFile.seek(glyph->bitmapOffset);
    size_t bytesRead = g_binFont.fontFile.read(reuseBuf, glyph->bitmapSize);
    if (sdMutex != NULL) xSemaphoreGive(sdMutex);
    
    if (bytesRead != glyph->bitmapSize) return false;
    
    // Cache the bitmap for future use
    cacheInsert(unicode, reuseBuf, glyph->bitmapSize, glyph->width, glyph->height);
    bitmap = reuseBuf;
  }
  
  // Draw glyph — scaled nearest-neighbor if scale != 1.0
  int srcW = glyph->width;
  int srcH = glyph->height;
  
  if (scale <= 1.01f && scale >= 0.99f) {
    // 1:1 — fast path with run-length hline optimization
    for (int py = 0; py < srcH; py++) {
      int lineStart = -1;
      for (int px = 0; px < srcW; px++) {
        int bitPos = py * srcW + px;
        bool isBlack = bitmap[bitPos / 8] & (1 << (7 - (bitPos % 8)));
        if (isBlack) {
          if (lineStart == -1) lineStart = px;
        } else {
          if (lineStart != -1) {
            M5.Display.drawFastHLine(x + lineStart, y + py, px - lineStart, color);
            lineStart = -1;
          }
        }
      }
      if (lineStart != -1) {
        M5.Display.drawFastHLine(x + lineStart, y + py, srcW - lineStart, color);
      }
    }
  } else {
    // Scaled rendering — nearest-neighbor with hline optimization
    int dstW = (int)(srcW * scale + 0.5f);
    int dstH = (int)(srcH * scale + 0.5f);
    for (int ty = 0; ty < dstH; ty++) {
      int sy = ty * srcH / dstH;
      int lineStart = -1;
      for (int tx = 0; tx < dstW; tx++) {
        int sx = tx * srcW / dstW;
        int bitPos = sy * srcW + sx;
        bool isBlack = bitmap[bitPos / 8] & (1 << (7 - (bitPos % 8)));
        if (isBlack) {
          if (lineStart == -1) lineStart = tx;
        } else {
          if (lineStart != -1) {
            M5.Display.drawFastHLine(x + lineStart, y + ty, tx - lineStart, color);
            lineStart = -1;
          }
        }
      }
      if (lineStart != -1) {
        M5.Display.drawFastHLine(x + lineStart, y + ty, dstW - lineStart, color);
      }
    }
  }
  
  return true;
}

int drawBinFontString(const String &text, int x, int y, int charSpacing) {
  if (!g_binFont.loaded) return x;
  
  int fontSize = g_binFont.fontSize;  // Native em-square height
  int currentX = x;
  for (int i = 0; i < text.length(); ) {
    int charStart = i;
    uint32_t unicode = utf8Decode(text, i);
    
    GlyphIndex* glyph = findGlyph(unicode);
    if (glyph && glyph->width > 0) {
      // Bottom-align: offset shorter glyphs (e.g. Latin) so baselines match
      int yOffset = fontSize - glyph->height;
      if (glyph->bearingY != 0) {
        // v2 font: use bearing for precise baseline positioning
        yOffset = fontSize - glyph->bearingY;
      }
      if (yOffset < 0) yOffset = 0;
      drawBinFontChar(unicode, currentX, y + yOffset);
      currentX += glyph->width + 2;
    } else {
      M5.Display.setCursor(currentX, y);
      String ch = text.substring(charStart, i);
      M5.Display.print(ch);
      currentX += charSpacing;
    }
  }
  
  return currentX;
}

// ==================== OFR Cached Glyph Drawing ====================
// Renders a TTF character via OpenFontRender into a small sprite,
// converts to 1-bit bitmap, and caches in PSRAM.
// Subsequent draws of the same character skip FreeType entirely.

bool drawOFRCharCached(uint32_t unicode, int x, int y, uint16_t color, int fontSize) {
  if (!ofrFontLoaded) return false;

  // Ensure glyph cache is initialized
  if (!glyphCache) initGlyphCache();

  // Check cache first — O(1) lookup
  CachedGlyph* cached = cacheFind(unicode);
  if (cached) {
    const uint8_t* bitmap = cached->bitmap;
    int w = cached->width;
    int h = cached->height;
    for (int py = 0; py < h; py++) {
      int lineStart = -1;
      for (int px = 0; px < w; px++) {
        int bitPos = py * w + px;
        bool isSet = bitmap[bitPos / 8] & (1 << (7 - (bitPos % 8)));
        if (isSet) {
          if (lineStart == -1) lineStart = px;
        } else {
          if (lineStart != -1) {
            M5.Display.drawFastHLine(x + lineStart, y + py, px - lineStart, color);
            lineStart = -1;
          }
        }
      }
      if (lineStart != -1) {
        M5.Display.drawFastHLine(x + lineStart, y + py, w - lineStart, color);
      }
    }
    return true;
  }

  // Cache miss — render via FreeType into a sprite, then capture bitmap
  int sprW = fontSize;
  int sprH = fontSize;
  LGFX_Sprite sprite(&M5.Display);
  sprite.setColorDepth(8);
  if (!sprite.createSprite(sprW, sprH)) return false;
  sprite.fillSprite(TFT_WHITE);

  ofr.setDrawer(sprite);
  char chBuf[5];
  int chLen = utf8Encode(unicode, chBuf);
  chBuf[chLen] = '\0';
  ofr.cdrawString(chBuf, sprW / 2, 0, TFT_BLACK, TFT_WHITE);
  ofr.setDrawer(M5.Display);

  // Convert 8-bit sprite buffer to 1-bit packed bitmap
  int bitmapSize = (sprW * sprH + 7) / 8;
  static uint8_t* convBuf = nullptr;
  static size_t convBufSize = 0;
  if ((size_t)bitmapSize > convBufSize) {
    if (convBuf) free(convBuf);
    convBufSize = bitmapSize + 64;
    convBuf = (uint8_t*)malloc(convBufSize);
    if (!convBuf) { convBufSize = 0; sprite.deleteSprite(); return false; }
  }
  memset(convBuf, 0, bitmapSize);

  uint8_t* spriteBuf = (uint8_t*)sprite.getBuffer();
  if (spriteBuf) {
    for (int py = 0; py < sprH; py++) {
      for (int px = 0; px < sprW; px++) {
        uint8_t pixel = spriteBuf[py * sprW + px];
        if (pixel < 128) {  // Dark pixel (0=black, 255=white in 8-bit)
          int bitPos = py * sprW + px;
          convBuf[bitPos / 8] |= (1 << (7 - (bitPos % 8)));
        }
      }
    }
  }
  sprite.deleteSprite();

  // Cache the 1-bit bitmap
  cacheInsert(unicode, convBuf, bitmapSize, sprW, sprH);

  // Draw to display using hline optimization
  for (int py = 0; py < sprH; py++) {
    int lineStart = -1;
    for (int px = 0; px < sprW; px++) {
      int bitPos = py * sprW + px;
      bool isSet = convBuf[bitPos / 8] & (1 << (7 - (bitPos % 8)));
      if (isSet) {
        if (lineStart == -1) lineStart = px;
      } else {
        if (lineStart != -1) {
          M5.Display.drawFastHLine(x + lineStart, y + py, px - lineStart, color);
          lineStart = -1;
        }
      }
    }
    if (lineStart != -1) {
      M5.Display.drawFastHLine(x + lineStart, y + py, sprW - lineStart, color);
    }
  }

  return true;
}

// ==================== TTF Font Loading ====================

bool loadTTFFont(const char* fontPath, int size) {
  // Clear glyph cache from previous font/size
  clearGlyphCache();

  if (!sdCardAvailable) {
    Serial.println("SD card not available");
    return false;
  }
  
  String fullPath;
  if (fontPath[0] == '/') {
    fullPath = String(fontPath);
  } else {
    fullPath = String("/fonts/") + fontPath;
  }
  
  Serial.printf("Loading TTF font via OpenFontRender: %s\n", fullPath.c_str());
  
  if (ofrFontLoaded) {
    ofr.unloadFont();
    ofrFontLoaded = false;
    Serial.println("Unloaded previous OFR font");
  }
  
  FT_Error error = ofr.loadFont(fullPath.c_str());
  if (error) {
    Serial.printf("✗ OpenFontRender loadFont error: 0x%02X\n", error);
    return false;
  }
  
  ofr.setDrawer(M5.Display);
  ofr.setFontSize(size);
  ofr.setFontColor(TFT_BLACK, TFT_WHITE);
  ofr.setBackgroundFillMethod(BgFillMethod::None);
  
  ofrFontLoaded = true;
  useTTFFont = true;
  currentFontFile = fontPath;
  
  Serial.printf("✓ TTF font loaded via OpenFontRender: %s (size=%d)\n", fontPath, size);
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  return true;
}

bool loadSystemFont() {
  if (systemFontFile.length() == 0) return false;
  if (ofrFontLoaded && currentFontFile == systemFontFile) return true;
  return loadTTFFont(systemFontFile.c_str(), 30);
}

bool loadReadingFont() {
  if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) {
    return loadSystemFont();
  }
  String fname = fontFileList[readingFontIndex];
  if (ofrFontLoaded && currentFontFile == fname) return true;
  if (g_binFont.loaded && currentFontFile == fname) return true;
  if (fname.endsWith(".ttf") || fname.endsWith(".TTF") ||
      fname.endsWith(".ttc") || fname.endsWith(".TTC") ||
      fname.endsWith(".otf") || fname.endsWith(".OTF")) {
    return loadTTFFont(fname.c_str(), readingFontSize);
  } else if (fname.endsWith(".bin") || fname.endsWith(".BIN")) {
    if (ofrFontLoaded) { ofr.unloadFont(); ofrFontLoaded = false; }
    bool ok = loadBinaryFont(fname.c_str());
    if (ok) currentFontFile = fname;
    return ok;
  }
  return false;
}

// ==================== SD-card Label Bitmaps ==================================

bool loadSDLabels() {
  File f = SD.open("/labels.bin", FILE_READ);
  if (!f) {
    Serial.println("SD labels: /labels.bin not found");
    return false;
  }

  size_t fileSize = f.size();
  if (fileSize < 8) {
    f.close();
    return false;
  }

  sdLabelData = (uint8_t*)ps_malloc(fileSize);
  if (!sdLabelData) {
    Serial.println("SD labels: out of PSRAM");
    f.close();
    return false;
  }

  f.read(sdLabelData, fileSize);
  f.close();

  // Validate magic
  if (memcmp(sdLabelData, "SLBL", 4) != 0) {
    Serial.println("SD labels: invalid magic");
    free(sdLabelData);
    sdLabelData = nullptr;
    return false;
  }

  uint32_t count;
  memcpy(&count, sdLabelData + 4, 4);
  sdLabelCount = (int)count;

  sdLabelEntries = (SDLabelEntry*)ps_malloc(count * sizeof(SDLabelEntry));
  if (!sdLabelEntries) {
    Serial.println("SD labels: out of PSRAM for entries");
    free(sdLabelData);
    sdLabelData = nullptr;
    sdLabelCount = 0;
    return false;
  }

  // Parse entries — pointers directly into sdLabelData buffer
  uint8_t* ptr = sdLabelData + 8;
  uint8_t* end = sdLabelData + fileSize;
  for (int i = 0; i < sdLabelCount; i++) {
    if (ptr + 2 > end) break;
    uint16_t textLen;
    memcpy(&textLen, ptr, 2);
    ptr += 2;

    if (ptr + textLen + 1 + 6 > end) break;
    sdLabelEntries[i].text = (const char*)ptr;
    ptr += textLen + 1;  // text + null terminator

    memcpy(&sdLabelEntries[i].fontSize, ptr, 2); ptr += 2;
    memcpy(&sdLabelEntries[i].w, ptr, 2); ptr += 2;
    memcpy(&sdLabelEntries[i].h, ptr, 2); ptr += 2;

    sdLabelEntries[i].bitmap = ptr;
    int bitmapSize = ((sdLabelEntries[i].w + 1) / 2) * sdLabelEntries[i].h;
    ptr += bitmapSize;
  }

  Serial.printf("SD labels: loaded %d entries (%d KB)\n", sdLabelCount, (int)(fileSize / 1024));
  return true;
}

void freeSDLabels() {
  if (sdLabelEntries) {
    free(sdLabelEntries);
    sdLabelEntries = nullptr;
  }
  if (sdLabelData) {
    free(sdLabelData);
    sdLabelData = nullptr;
  }
  sdLabelCount = 0;
}
