#include "globals.h"
#include "esp_task_wdt.h"
#include "etbook_ttf.h"

// ==================== OpenFontRender SD File I/O Overrides ====================

static int ofrReadCounter = 0;  // Rate-limit yield() inside OFR_fread

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
  // Feed watchdog every 64 reads to prevent timeout during font parsing
  if (++ofrReadCounter >= 64) {
    ofrReadCounter = 0;
    yield();
    esp_task_wdt_reset();
  }
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
    uint8_t* buf = (uint8_t*)ps_malloc(length);
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

// Extract font size from a .bin font file header (offset 4, 1 byte)
static uint8_t extractBinFontSize(File &f) {
  f.seek(4);
  uint8_t sz = 0;
  if (f.read(&sz, 1) != 1) return 0;
  return sz;
}

void scanFontFiles() {
  fontFileCount = 0;
  
  if (!sdCardAvailable) return;
  
  // First pass: collect all font files into temporary arrays
  // Use static to avoid ~3KB+ stack allocation (function called only once)
  // Chinese fonts live in /fonts/, English fonts in /fonts/en/
  static String tmpFiles[MAX_FONT_FILES];
  static String tmpNames[MAX_FONT_FILES];
  static bool   tmpIsBin[MAX_FONT_FILES];
  static bool   tmpIsCJK[MAX_FONT_FILES];
  static uint8_t tmpBinSize[MAX_FONT_FILES];
  int tmpCount = 0;
  
  // Scan both /fonts/ (CJK) and /fonts/en/ (English)
  const char* scanDirs[] = { "/fonts", "/fonts/en" };
  const char* scanPrefixes[] = { "", "en/" };
  const bool  scanIsCJK[] = { true, false };
  
  for (int d = 0; d < 2; d++) {
    File fontsDir;
    if (sdMutex != NULL) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      fontsDir = SD.open(scanDirs[d]);
      xSemaphoreGive(sdMutex);
    } else {
      fontsDir = SD.open(scanDirs[d]);
    }
    
    if (!fontsDir || !fontsDir.isDirectory()) {
      if (d == 0) Serial.println("WARNING: /fonts directory not found");
      continue;
    }
    
    bool isCJK = scanIsCJK[d];
    String prefix = scanPrefixes[d];
    Serial.printf("Scanning %s (%s fonts)...\n", scanDirs[d], isCJK ? "CJK" : "English");
    
    File entry = fontsDir.openNextFile();
    while (entry && tmpCount < MAX_FONT_FILES) {
      yield(); esp_task_wdt_reset();
      String name = String(entry.name());
      if (!name.startsWith("._") && !name.startsWith(".") && !entry.isDirectory()) {
        if (name.endsWith(".ttf") || name.endsWith(".TTF") ||
            name.endsWith(".ttc") || name.endsWith(".TTC") ||
            name.endsWith(".otf") || name.endsWith(".OTF")) {
          String displayName = extractTTFName(entry);
          if (isCJK) {
            // Map well-known English names to Traditional Chinese
            if (displayName == "Noto Sans TC") displayName = "\xE6\x80\x9D\xE6\xBA\x90\xE9\xBB\x91\xE9\xAB\x94";  // 思源黑體
            else if (displayName == "Noto Serif TC") displayName = "\xE6\x80\x9D\xE6\xBA\x90\xE5\xAE\x8B\xE9\xAB\x94";  // 思源宋體
          }
          tmpFiles[tmpCount] = prefix + name;
          tmpNames[tmpCount] = (displayName.length() > 0) ? displayName : name;
          tmpIsBin[tmpCount] = false;
          tmpIsCJK[tmpCount] = isCJK;
          tmpBinSize[tmpCount] = 0;
          Serial.printf("  %s font: %s → %s\n", isCJK ? "CJK" : "English", (prefix + name).c_str(), tmpNames[tmpCount].c_str());
          tmpCount++;
        } else if (name.endsWith(".bin") || name.endsWith(".BIN")) {
          String displayName = extractBinFontName(entry);
          if (isCJK) {
            if (displayName == "Noto Sans TC") displayName = "\xE6\x80\x9D\xE6\xBA\x90\xE9\xBB\x91\xE9\xAB\x94";  // 思源黑體
            else if (displayName == "Noto Serif TC") displayName = "\xE6\x80\x9D\xE6\xBA\x90\xE5\xAE\x8B\xE9\xAB\x94";  // 思源宋體
          }
          tmpFiles[tmpCount] = prefix + name;
          tmpNames[tmpCount] = (displayName.length() > 0) ? displayName : name;
          tmpIsBin[tmpCount] = true;
          tmpIsCJK[tmpCount] = isCJK;
          tmpBinSize[tmpCount] = extractBinFontSize(entry);
          Serial.printf("  %s font: %s → %s (%dpt)\n", isCJK ? "CJK" : "English", (prefix + name).c_str(), tmpNames[tmpCount].c_str(), tmpBinSize[tmpCount]);
          tmpCount++;
        }
      }
      entry.close();
      entry = fontsDir.openNextFile();
    }
    fontsDir.close();
  }
  
  // Second pass: pair TTF+BIN fonts with matching display names
  // TTF entries take priority; matching BIN files become the paired counterparts
  // A TTF can have multiple BIN files with different font sizes
  static bool binPaired[MAX_FONT_FILES];
  static bool ttfPaired[MAX_FONT_FILES];
  for (int i = 0; i < tmpCount; i++) { binPaired[i] = false; ttfPaired[i] = false; }
  
  // Helper: detect font style from filename
  auto detectStyle = [](const String& filename) -> int {
    String lower = filename;
    lower.toLowerCase();
    int dot = lower.lastIndexOf('.');
    if (dot > 0) lower = lower.substring(0, dot);
    bool hasBold = lower.indexOf("bold") >= 0 || lower.endsWith("bd");
    bool hasItalic = lower.indexOf("italic") >= 0 || lower.endsWith("bi") || lower.endsWith("it");
    if (hasBold && hasItalic) return 3;  // Bold Italic
    if (hasBold) return 2;               // Bold
    if (hasItalic) return 1;             // Italic
    return 0;                            // Regular
  };
  
  // Add CJK TTF entries (one entry per font, no grouping needed)
  for (int i = 0; i < tmpCount && fontFileCount < MAX_FONT_FILES; i++) {
    if (tmpIsBin[i] || !tmpIsCJK[i]) continue;
    fontFileList[fontFileCount] = tmpFiles[i];
    fontDisplayNames[fontFileCount] = tmpNames[i];
    fontBinCount[fontFileCount] = 0;
    fontIsCJK[fontFileCount] = true;
    for (int s = 0; s < 4; s++) fontStyleFiles[fontFileCount][s] = "";
    ttfPaired[i] = true;
    
    // Find all matching BINs by display name
    String ttfNameLow = tmpNames[i];
    ttfNameLow.toLowerCase();
    for (int j = 0; j < tmpCount; j++) {
      if (!tmpIsBin[j] || binPaired[j]) continue;
      String binNameLow = tmpNames[j];
      binNameLow.toLowerCase();
      if (ttfNameLow == binNameLow && fontBinCount[fontFileCount] < MAX_BIN_PER_FONT) {
        int idx = fontBinCount[fontFileCount];
        fontBinFiles[fontFileCount][idx] = tmpFiles[j];
        fontBinSizes[fontFileCount][idx] = tmpBinSize[j];
        fontBinCount[fontFileCount]++;
        binPaired[j] = true;
      }
    }
    fontFileCount++;
  }
  
  // Add non-CJK TTF entries, grouped by family name
  for (int i = 0; i < tmpCount && fontFileCount < MAX_FONT_FILES; i++) {
    if (tmpIsBin[i] || tmpIsCJK[i] || ttfPaired[i]) continue;
    
    // Check if we already created an entry for this family
    String familyLow = tmpNames[i];
    familyLow.toLowerCase();
    int existingIdx = -1;
    for (int f = 0; f < fontFileCount; f++) {
      if (fontIsCJK[f]) continue;
      String existLow = fontDisplayNames[f];
      existLow.toLowerCase();
      if (existLow == familyLow) { existingIdx = f; break; }
    }
    
    int style = detectStyle(tmpFiles[i]);
    
    if (existingIdx >= 0) {
      // Add as style variant to existing group
      if (fontStyleFiles[existingIdx][style].length() == 0) {
        fontStyleFiles[existingIdx][style] = tmpFiles[i];
        // If this is Regular and the current primary isn't, update primary
        if (style == 0) fontFileList[existingIdx] = tmpFiles[i];
        Serial.printf("  Grouped style: %s into %s (style %d)\n", tmpFiles[i].c_str(), fontDisplayNames[existingIdx].c_str(), style);
      }
    } else {
      // Create new family entry
      fontFileList[fontFileCount] = tmpFiles[i];
      fontDisplayNames[fontFileCount] = tmpNames[i];
      fontBinCount[fontFileCount] = 0;
      fontIsCJK[fontFileCount] = false;
      for (int s = 0; s < 4; s++) fontStyleFiles[fontFileCount][s] = "";
      fontStyleFiles[fontFileCount][style] = tmpFiles[i];
      
      // Look ahead for other variants with the same family name
      for (int j = i + 1; j < tmpCount; j++) {
        if (tmpIsBin[j] || tmpIsCJK[j] || ttfPaired[j]) continue;
        String otherLow = tmpNames[j];
        otherLow.toLowerCase();
        if (otherLow == familyLow) {
          int otherStyle = detectStyle(tmpFiles[j]);
          if (fontStyleFiles[fontFileCount][otherStyle].length() == 0) {
            fontStyleFiles[fontFileCount][otherStyle] = tmpFiles[j];
            // Prefer a Regular file as the primary
            if (otherStyle == 0) fontFileList[fontFileCount] = tmpFiles[j];
          }
          ttfPaired[j] = true;
          Serial.printf("  Grouped style: %s (style %d)\n", tmpFiles[j].c_str(), otherStyle);
        }
      }
      ttfPaired[i] = true;
      fontFileCount++;
    }
  }
  
  // Add unpaired BIN entries (no matching TTF found)
  // Group by display name so multiple sizes appear as size buttons
  for (int i = 0; i < tmpCount && fontFileCount < MAX_FONT_FILES; i++) {
    if (!tmpIsBin[i] || binPaired[i]) continue;
    
    // Check if we already created a group for this display name
    String thisNameLow = tmpNames[i];
    thisNameLow.toLowerCase();
    int existingIdx = -1;
    for (int f = 0; f < fontFileCount; f++) {
      String existNameLow = fontDisplayNames[f];
      existNameLow.toLowerCase();
      if (existNameLow == thisNameLow) {
        existingIdx = f;
        break;
      }
    }
    
    if (existingIdx >= 0 && fontBinCount[existingIdx] < MAX_BIN_PER_FONT) {
      // Add to existing group as another size variant
      int idx = fontBinCount[existingIdx];
      fontBinFiles[existingIdx][idx] = tmpFiles[i];
      fontBinSizes[existingIdx][idx] = tmpBinSize[i];
      fontBinCount[existingIdx]++;
      binPaired[i] = true;
      Serial.printf("  Grouped BIN: %s into %s (%dpt)\n", tmpFiles[i].c_str(), fontDisplayNames[existingIdx].c_str(), tmpBinSize[i]);
    } else if (existingIdx < 0) {
      // Create new group — first BIN becomes the primary entry
      // Also add it as the first bin button so all sizes show as buttons
      fontFileList[fontFileCount] = tmpFiles[i];
      fontDisplayNames[fontFileCount] = tmpNames[i];
      fontBinFiles[fontFileCount][0] = tmpFiles[i];
      fontBinSizes[fontFileCount][0] = tmpBinSize[i];
      fontBinCount[fontFileCount] = 1;
      fontIsCJK[fontFileCount] = true;
      binPaired[i] = true;
      
      // Look ahead for other BINs with the same display name to group them
      for (int j = i + 1; j < tmpCount; j++) {
        if (!tmpIsBin[j] || binPaired[j]) continue;
        String otherNameLow = tmpNames[j];
        otherNameLow.toLowerCase();
        if (otherNameLow == thisNameLow && fontBinCount[fontFileCount] < MAX_BIN_PER_FONT) {
          int idx = fontBinCount[fontFileCount];
          fontBinFiles[fontFileCount][idx] = tmpFiles[j];
          fontBinSizes[fontFileCount][idx] = tmpBinSize[j];
          fontBinCount[fontFileCount]++;
          binPaired[j] = true;
          Serial.printf("  Grouped BIN: %s (%dpt)\n", tmpFiles[j].c_str(), tmpBinSize[j]);
        }
      }
      fontFileCount++;
    }
  }
  
  // Sort fonts by display name
  for (int i = 0; i < fontFileCount - 1; i++) {
    for (int j = i + 1; j < fontFileCount; j++) {
      String a = fontDisplayNames[i];
      String b = fontDisplayNames[j];
      a.toLowerCase();
      b.toLowerCase();
      if (a > b) {
        // Swap all arrays
        String tmp = fontFileList[i];
        fontFileList[i] = fontFileList[j];
        fontFileList[j] = tmp;
        tmp = fontDisplayNames[i];
        fontDisplayNames[i] = fontDisplayNames[j];
        fontDisplayNames[j] = tmp;
        bool tmpCJK = fontIsCJK[i];
        fontIsCJK[i] = fontIsCJK[j];
        fontIsCJK[j] = tmpCJK;
        // Swap style files
        for (int k = 0; k < 4; k++) {
          tmp = fontStyleFiles[i][k];
          fontStyleFiles[i][k] = fontStyleFiles[j][k];
          fontStyleFiles[j][k] = tmp;
        }
        // Swap bin arrays
        int tmpCnt = fontBinCount[i];
        fontBinCount[i] = fontBinCount[j];
        fontBinCount[j] = tmpCnt;
        for (int k = 0; k < MAX_BIN_PER_FONT; k++) {
          tmp = fontBinFiles[i][k];
          fontBinFiles[i][k] = fontBinFiles[j][k];
          fontBinFiles[j][k] = tmp;
          uint8_t tmpSz = fontBinSizes[i][k];
          fontBinSizes[i][k] = fontBinSizes[j][k];
          fontBinSizes[j][k] = tmpSz;
        }
      }
    }
  }
  
  // Add embedded ET Book as virtual entry (from firmware, no SD file)
  if (fontFileCount < MAX_FONT_FILES) {
    fontFileList[fontFileCount] = "ETBook-embedded";
    fontDisplayNames[fontFileCount] = "ET Book (Built-in)";
    fontBinCount[fontFileCount] = 0;
    fontIsCJK[fontFileCount] = false;
    for (int s = 0; s < 4; s++) fontStyleFiles[fontFileCount][s] = "";
    fontStyleFiles[fontFileCount][0] = "ETBook-embedded";
    fontFileCount++;
  }
  
  Serial.printf("Total fonts found: %d\n", fontFileCount);
  for (int i = 0; i < fontFileCount; i++) {
    Serial.printf("  [%d] %s (CJK=%d, file=%s)\n", i, fontDisplayNames[i].c_str(), fontIsCJK[i], fontFileList[i].c_str());
  }
}

// ==================== Binary Font Loading ====================

bool loadBinaryFont(const char* fontPath) {
  Serial.printf("\n=== loadBinaryFont() called ===\n");
  Serial.printf("Input path: '%s'\n", fontPath);
  
  // Clear glyph cache from previous font
  clearGlyphCache();
  clearAdvanceCache();

  // Free old glyph index if reloading (prevents PSRAM leak)
  if (g_binFont.index) {
    free(g_binFont.index);
    g_binFont.index = nullptr;
  }
  // Close previous BIN font file handle
  if (g_binFont.loaded) {
    g_binFont.fontFile.close();
    g_binFont.loaded = false;
  }
  
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
  
  memcpy(&g_binFont.charCount, &header[0], 4);
  g_binFont.fontSize = header[4];
  memcpy(&g_binFont.version, &header[5], 4);
  if (g_binFont.charCount > 200000) {
    Serial.printf("✗ Unreasonable charCount: %u\n", g_binFont.charCount);
    g_binFont.fontFile.close();
    return false;
  }
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
  g_binFont.filePath = fullPath;
  Serial.printf("✓ Binary font loaded: %d glyphs in index\n", g_binFont.charCount);
  Serial.printf("Free PSRAM after index: %d bytes\n", ESP.getFreePsram());
  
  return true;
}

void unloadBinaryFont() {
  if (g_binFont.loaded) {
    g_binFont.fontFile.close();
    g_binFont.loaded = false;
    Serial.println("BIN font unloaded");
  }
  if (g_binFont.index) {
    free(g_binFont.index);
    g_binFont.index = nullptr;
  }
}

// Thorough cleanup of ALL font resources — call when leaving reading mode.
// Unloads BIN font, OFR/FreeType font, closes stale file handles, then
// reloads the system font so the UI is ready to draw.
void resetToSystemFont() {
  Serial.printf("resetToSystemFont: heap=%u psram=%u ofrFiles=%d\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), (int)ofr_file_list.size());
  unloadBinaryFont();
  if (ofrFontLoaded) {
    ofr.unloadFont();
    ofrFontLoaded = false;
  }
  // Force-close any stale SD file handles left by incomplete FreeType cleanup
  if (!ofr_file_list.empty()) {
    Serial.printf("WARNING: %d stale OFR file handles in resetToSystemFont\n", (int)ofr_file_list.size());
    for (auto &f : ofr_file_list) f.close();
    ofr_file_list.clear();
  }
  currentFontFile = "";
  loadSystemFont();
  Serial.printf("resetToSystemFont done: heap=%u psram=%u\n",
                ESP.getFreeHeap(), ESP.getFreePsram());
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
  int8_t offsetX;  // draw offset from cell origin
  int8_t offsetY;
  bool occupied;
};
static CachedGlyph* glyphCache = nullptr;
static uint8_t* glyphBitmapPool = nullptr;  // Contiguous PSRAM pool for bitmap data
static size_t glyphPoolUsed = 0;
static const size_t GLYPH_POOL_SIZE = 192 * 1024;  // 192KB for cached bitmaps

void initGlyphCache() {
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

void freeGlyphCache() {
  if (glyphBitmapPool) {
    free(glyphBitmapPool);
    glyphBitmapPool = nullptr;
  }
  if (glyphCache) {
    free(glyphCache);
    glyphCache = nullptr;
  }
  glyphPoolUsed = 0;
  Serial.printf("Glyph cache freed: psram=%u\n", ESP.getFreePsram());
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

static void cacheInsert(uint32_t unicode, const uint8_t* bitmap, uint16_t bitmapSize, uint16_t w, uint16_t h, int8_t ox = 0, int8_t oy = 0) {
  if (!glyphCache || !glyphBitmapPool) return;
  if (glyphPoolUsed + bitmapSize > GLYPH_POOL_SIZE) {
    // Pool full — evict entire cache (ring-buffer reset) so new glyphs can always be cached.
    // Better than silently dropping: avoids repeated SD reads for uncached characters.
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) glyphCache[i].occupied = false;
    glyphPoolUsed = 0;
  }

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
      glyphCache[slot].offsetX = ox;
      glyphCache[slot].offsetY = oy;
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
    if (!g_binFont.fontFile) {
      Serial.printf("drawBinFontChar: fontFile not open for U+%04X\n", unicode);
      return false;
    }
    if (glyph->bitmapSize > reuseBufSize) {
      if (reuseBuf) free(reuseBuf);
      reuseBufSize = glyph->bitmapSize + 64;  // Slight overalloc to avoid frequent reallocs
      reuseBuf = (uint8_t*)ps_malloc(reuseBufSize);
      if (!reuseBuf) { reuseBufSize = 0; return false; }
    }
    
    if (sdMutex != NULL) xSemaphoreTake(sdMutex, portMAX_DELAY);
    g_binFont.fontFile.seek(glyph->bitmapOffset);
    size_t bytesRead = g_binFont.fontFile.read(reuseBuf, glyph->bitmapSize);
    if (sdMutex != NULL) xSemaphoreGive(sdMutex);
    
    if (bytesRead != glyph->bitmapSize) {
      Serial.printf("drawBinFontChar: short read for U+%04X (got %u of %u)\n",
                    unicode, (unsigned)bytesRead, (unsigned)glyph->bitmapSize);
      return false;
    }
    
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

int drawBinFontStringScaled(const String &text, int x, int y, float scale, bool noYOffset) {
  if (!g_binFont.loaded) return x;

  int fontSize = g_binFont.fontSize;
  int currentX = x;
  for (int i = 0; i < (int)text.length(); ) {
    int charStart = i;
    uint32_t unicode = utf8Decode(text, i);

    GlyphIndex* glyph = findGlyph(unicode);
    if (glyph && glyph->width > 0) {
      int yOffset = 0;
      if (noYOffset) {
        // Preview mode: bearingY is the offset from em-square top (normalized)
        yOffset = (glyph->bearingY > 0) ? glyph->bearingY : 0;
      } else {
        yOffset = fontSize - glyph->height;
        if (glyph->bearingY != 0) {
          yOffset = fontSize - glyph->bearingY;
        }
        if (yOffset < 0) yOffset = 0;
      }
      drawBinFontChar(unicode, currentX, y + (int)(yOffset * scale), TFT_BLACK, scale);
      currentX += (int)((glyph->width + 2) * scale);
    } else {
      M5.Display.setCursor(currentX, y);
      String ch = text.substring(charStart, i);
      M5.Display.print(ch);
      currentX += (int)(fontSize * scale);
    }
  }

  return currentX;
}

// ==================== OFR Cached Glyph Drawing ======================================
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
    int drawX = x + cached->offsetX;
    int drawY = y + cached->offsetY;
    for (int py = 0; py < h; py++) {
      int lineStart = -1;
      for (int px = 0; px < w; px++) {
        int bitPos = py * w + px;
        bool isSet = bitmap[bitPos / 8] & (1 << (7 - (bitPos % 8)));
        if (isSet) {
          if (lineStart == -1) lineStart = px;
        } else {
          if (lineStart != -1) {
            M5.Display.drawFastHLine(drawX + lineStart, drawY + py, px - lineStart, color);
            lineStart = -1;
          }
        }
      }
      if (lineStart != -1) {
        M5.Display.drawFastHLine(drawX + lineStart, drawY + py, w - lineStart, color);
      }
    }
    return true;
  }

  // Cache miss — render via FreeType into an oversized sprite to avoid clipping.
  // Use 2x fontSize to accommodate fonts whose ascender+descender exceeds fontSize.
  // Render at y=0 (no top margin) — FreeType places text from y=0 downward.
  int sprW = fontSize * 2;
  int sprH = fontSize * 2;
  LGFX_Sprite sprite(&M5.Display);
  sprite.setColorDepth(8);
  sprite.setPsram(true);  // Use PSRAM — internal DMA heap is too tight with SD TTF fonts
  if (!sprite.createSprite(sprW, sprH)) {
    static int sprFailCount = 0;
    if (++sprFailCount <= 3)
      Serial.printf("createSprite FAILED: %dx%d (%d bytes), heap=%u psram=%u\n",
                     sprW, sprH, sprW * sprH, ESP.getFreeHeap(), ESP.getFreePsram());
    return false;
  }
  sprite.fillSprite(TFT_WHITE);

  // Check if the character exists in the loaded font (glyph_index != 0)
  if (!ofr.isCharAvailable(unicode)) {
    sprite.deleteSprite();
    return false;
  }

  char chBuf[5];
  int chLen = utf8Encode(unicode, chBuf);
  chBuf[chLen] = '\0';

  // Render centered horizontally, starting from top of sprite.
  // Cell origin in sprite coords: x = (sprW - fontSize) / 2, y = 0
  int cellX = (sprW - fontSize) / 2;  // left edge of virtual fontSize×fontSize cell
  ofr.setFontSize(fontSize);
  ofr.setDrawer(sprite);
  ofr.cdrawString(chBuf, sprW / 2, 0, TFT_BLACK, TFT_WHITE);
  ofr.setDrawer(M5.Display);

  // Find tight bounding box of rendered pixels
  uint8_t* spriteBuf = (uint8_t*)sprite.getBuffer();
  if (!spriteBuf) {
    sprite.deleteSprite();
    return false;
  }
  int minX = sprW, maxX = -1, minY = sprH, maxY = -1;
  for (int py = 0; py < sprH; py++) {
    for (int px = 0; px < sprW; px++) {
      if (spriteBuf[py * sprW + px] < 128) {
        if (px < minX) minX = px;
        if (px > maxX) maxX = px;
        if (py < minY) minY = py;
        if (py > maxY) maxY = py;
      }
    }
  }
  if (maxX < 0) {
    sprite.deleteSprite();
    return false;
  }

  // Crop to tight bounds
  int cropW = maxX - minX + 1;
  int cropH = maxY - minY + 1;
  int8_t offsetX = (int8_t)(minX - cellX);   // offset relative to cell left edge
  int8_t offsetY = (int8_t)(minY);            // offset relative to cell top (y=0)

  // Convert cropped region to 1-bit packed bitmap
  int bitmapSize = (cropW * cropH + 7) / 8;
  static uint8_t* convBuf = nullptr;
  static size_t convBufSize = 0;
  if ((size_t)bitmapSize > convBufSize) {
    if (convBuf) free(convBuf);
    convBufSize = bitmapSize + 64;
    convBuf = (uint8_t*)ps_malloc(convBufSize);
    if (!convBuf) { convBufSize = 0; sprite.deleteSprite(); return false; }
  }
  memset(convBuf, 0, bitmapSize);

  for (int py = 0; py < cropH; py++) {
    for (int px = 0; px < cropW; px++) {
      uint8_t pixel = spriteBuf[(minY + py) * sprW + (minX + px)];
      if (pixel < 128) {
        int bitPos = py * cropW + px;
        convBuf[bitPos / 8] |= (1 << (7 - (bitPos % 8)));
      }
    }
  }
  sprite.deleteSprite();

  // Cache the cropped 1-bit bitmap with offsets
  cacheInsert(unicode, convBuf, bitmapSize, cropW, cropH, offsetX, offsetY);

  // Draw to display using hline optimization
  int drawX = x + offsetX;
  int drawY = y + offsetY;
  for (int py = 0; py < cropH; py++) {
    int lineStart = -1;
    for (int px = 0; px < cropW; px++) {
      int bitPos = py * cropW + px;
      bool isSet = convBuf[bitPos / 8] & (1 << (7 - (bitPos % 8)));
      if (isSet) {
        if (lineStart == -1) lineStart = px;
      } else {
        if (lineStart != -1) {
          M5.Display.drawFastHLine(drawX + lineStart, drawY + py, px - lineStart, color);
          lineStart = -1;
        }
      }
    }
    if (lineStart != -1) {
      M5.Display.drawFastHLine(drawX + lineStart, drawY + py, cropW - lineStart, color);
    }
  }

  return true;
}

// ==================== Proportional Advance Width Cache (Horizontal Mode) =========
// Caches per-character advance widths so word measurement is O(1) per char
// instead of calling FreeType for every word on every page turn.
static int16_t s_advanceCache[128];  // ASCII advance widths
static int s_advanceCacheFontSize = 0;

void clearAdvanceCache() {
  s_advanceCacheFontSize = 0;
}

int getCharAdvanceW(uint32_t unicode, int fontSize) {
  if (!ofrFontLoaded) return fontSize / 2;  // fallback
  // Rebuild ASCII cache on font size change
  if (fontSize != s_advanceCacheFontSize) {
    ofr.setFontSize(fontSize);
    char buf[2] = {0, 0};
    for (int c = 33; c < 127; c++) {
      buf[0] = (char)c;
      s_advanceCache[c] = (int16_t)ofr.getTextWidth(buf);
      if ((c & 0x1F) == 0) { yield(); esp_task_wdt_reset(); } // WDT every 32 chars
    }
    // Space: derive from advance (getTextWidth(" ") returns 0 for OFR)
    int spW = (int)ofr.getTextWidth("i i") - (int)ofr.getTextWidth("ii");
    if (spW <= 0) spW = fontSize / 3;
    s_advanceCache[32] = (int16_t)spW;
    s_advanceCacheFontSize = fontSize;
    esp_task_wdt_reset();
    Serial.printf("Advance cache built for size %d, space=%d\n", fontSize, spW);
  }
  if (unicode < 128) return s_advanceCache[unicode];
  // Non-ASCII: measure single char on demand
  char chBuf[5];
  int len = utf8Encode(unicode, chBuf);
  chBuf[len] = '\0';
  ofr.setFontSize(fontSize);
  return (int)ofr.getTextWidth(chBuf);
}

int drawOFRCharHoriz(uint32_t unicode, int cursorX, int y, uint16_t color, int fontSize) {
  int advW = getCharAdvanceW(unicode, fontSize);
  if (unicode == 32 || unicode < 33) return advW;  // Space/control: just advance
  // Adjust position: drawOFRCharCached uses centered rendering (cdrawString)
  // so the advance origin sits at fontSize/2 from cell left. Shift x so
  // the advance origin aligns with cursorX.
  int adjustedX = cursorX - fontSize / 2 + advW / 2;
  drawOFRCharCached(unicode, adjustedX, y, color, fontSize);
  return advW;
}

// ==================== Embedded ET Book Font ====================

bool loadEmbeddedETBook(int size) {
  // Short-circuit if already loaded
  if (ofrFontLoaded && currentFontFile == "ETBook-embedded") {
    ofr.setFontSize(size);
    ofr.setFontColor(TFT_BLACK, TFT_WHITE);
    return true;
  }
  clearGlyphCache();
  clearAdvanceCache();
  if (ofrFontLoaded) { ofr.unloadFont(); ofrFontLoaded = false; }
  Serial.printf("Loading embedded ET Book font (%u bytes), size=%d\n", (unsigned)etbook_ttf_len, size);
  FT_Error error = ofr.loadFont(etbook_ttf, etbook_ttf_len);
  if (error) {
    Serial.printf("ET Book loadFont error: 0x%02X\n", error);
    return false;
  }
  ofr.setDrawer(M5.Display);
  ofr.setFontSize(size);
  ofr.setFontColor(TFT_BLACK, TFT_WHITE);
  ofr.setBackgroundFillMethod(BgFillMethod::None);
  ofrFontLoaded = true;
  useTTFFont = true;
  currentFontFile = "ETBook-embedded";
  Serial.printf("ET Book loaded from flash (size=%d)\n", size);
  return true;
}

// ==================== TTF Font Loading ====================

bool loadTTFFont(const char* fontPath, int size) {
  // Clear glyph cache from previous font/size
  clearGlyphCache();
  clearAdvanceCache();

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
  
  // Safety: close any stale OFR file handles left by incomplete cleanup
  if (!ofr_file_list.empty()) {
    Serial.printf("WARNING: %d stale OFR file handles, force-closing\n", (int)ofr_file_list.size());
    for (auto &f : ofr_file_list) f.close();
    ofr_file_list.clear();
  }
  
  yield();
  esp_task_wdt_reset();
  ofrReadCounter = 0;  // Reset rate-limiter before font parse
  Serial.printf("loadTTFFont: calling ofr.loadFont, heap=%u, psram=%u, ofrFiles=%d\n",
               ESP.getFreeHeap(), ESP.getFreePsram(), (int)ofr_file_list.size());
  unsigned long fontLoadStart = millis();
  FT_Error error = ofr.loadFont(fullPath.c_str());
  unsigned long fontLoadMs = millis() - fontLoadStart;
  Serial.printf("loadTTFFont: ofr.loadFont returned 0x%02X in %lu ms, ofrFiles=%d\n",
               error, fontLoadMs, (int)ofr_file_list.size());
  yield();
  esp_task_wdt_reset();
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

// Select the best BIN file for a given target size from the current font's paired BINs.
// Returns true if readingFontFile was changed (caller should reload). 
// Strategy: pick the BIN whose native size is closest. Prefer larger (scale down) over
// smaller (scale up) when distance is equal, for better rendering quality.
bool selectBestBinForSize(int targetSize) {
  if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) return false;
  int count = fontBinCount[readingFontIndex];
  if (count <= 0) return false;

  // Check if current font is actually a BIN
  String curFont = (readingFontFile.length() > 0) ? readingFontFile :
      fontFileList[readingFontIndex];
  if (!curFont.endsWith(".bin") && !curFont.endsWith(".BIN")) return false;

  bool isSilver = (curFont.indexOf("Silver") >= 0 || curFont.indexOf("silver") >= 0);

  int bestIdx = 0;
  int bestDist = 9999;
  for (int i = 0; i < count; i++) {
    int nativeSize = isSilver ? silverNominalSize(fontBinSizes[readingFontIndex][i])
                              : (int)fontBinSizes[readingFontIndex][i];
    int dist = abs(nativeSize - targetSize);
    // Prefer larger (or equal) BIN when distances are equal
    if (dist < bestDist || (dist == bestDist && nativeSize >= targetSize)) {
      bestDist = dist;
      bestIdx = i;
    }
  }

  String bestFile = fontBinFiles[readingFontIndex][bestIdx];
  if (bestFile == readingFontFile) return false;  // Already using the best one

  Serial.printf("BIN auto-select: target=%dpt → %s (native=%dpt)\n",
                targetSize, bestFile.c_str(), fontBinSizes[readingFontIndex][bestIdx]);
  readingFontFile = bestFile;
  savePrefStr("ereader", "fontFile", readingFontFile);
  return true;  // Caller should reload
}

bool loadReadingFont() {
  if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) {
    return loadSystemFont();
  }
  // Use readingFontFile if set (may be paired BIN), otherwise use primary font file
  String fname = (readingFontFile.length() > 0) ? readingFontFile : fontFileList[readingFontIndex];
  // Embedded ET Book: load from flash, not SD
  if (fname == "ETBook-embedded") {
    return loadEmbeddedETBook(readingFontSize);
  }
  if (ofrFontLoaded && currentFontFile == fname) return true;
  if (g_binFont.loaded && currentFontFile == fname) return true;
  // BIN font is already in memory but currentFontFile was changed by loadSystemFont().
  // Keep OFR loaded — drawReading() reuses it for Latin text (ET Book) across pages.
  // Don't overwrite currentFontFile — it tracks the OFR font (e.g. ETBook-embedded)
  // so the ET Book short-circuit in loadEmbeddedETBook() keeps working.
  // Note: must check g_binFont.filePath matches — selectBestBinForSize() may have
  // changed readingFontFile to a different BIN.
  if (g_binFont.loaded && (fname.endsWith(".bin") || fname.endsWith(".BIN"))) {
    String expectedPath = fname.startsWith("/") ? fname : (String("/fonts/") + fname);
    if (g_binFont.filePath == expectedPath) return true;
  }
  if (fname.endsWith(".ttf") || fname.endsWith(".TTF") ||
      fname.endsWith(".ttc") || fname.endsWith(".TTC") ||
      fname.endsWith(".otf") || fname.endsWith(".OTF")) {
    return loadTTFFont(fname.c_str(), readingFontSize);
  } else if (fname.endsWith(".bin") || fname.endsWith(".BIN")) {
    // Keep ETBook loaded — drawChineseReading() needs it for Latin fallback.
    // Unloading then immediately reloading FreeType wastes time and can fail
    // due to PSRAM fragmentation after font menu preview cycles.
    if (ofrFontLoaded && currentFontFile != "ETBook-embedded") {
      ofr.unloadFont();
      ofrFontLoaded = false;
    }
    bool ok = loadBinaryFont(fname.c_str());
    if (ok) {
      // If ETBook is still loaded, preserve currentFontFile so the
      // loadEmbeddedETBook() short-circuit keeps working on the next render.
      if (!(ofrFontLoaded && currentFontFile == "ETBook-embedded")) {
        currentFontFile = fname;
      }
    }
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
    if (ptr + bitmapSize > end) break;
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
