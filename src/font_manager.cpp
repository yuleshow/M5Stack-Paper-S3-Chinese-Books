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
          name.endsWith(".ttc") || name.endsWith(".TTC")) {
        String displayName = extractTTFName(entry);
        fontFileList[fontFileCount] = name;
        fontDisplayNames[fontFileCount] = (displayName.length() > 0) ? displayName : name;
        Serial.printf("  Font found: %s → %s\n", name.c_str(), fontDisplayNames[fontFileCount].c_str());
        fontFileCount++;
      } else if (name.endsWith(".bin") || name.endsWith(".BIN")) {
        String displayName = extractBinFontName(entry);
        fontFileList[fontFileCount] = name;
        fontDisplayNames[fontFileCount] = (displayName.length() > 0) ? displayName : name;
        Serial.printf("  Font found: %s → %s\n", name.c_str(), fontDisplayNames[fontFileCount].c_str());
        fontFileCount++;
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
  
  Serial.println("Reading glyph index...");
  for (uint32_t i = 0; i < g_binFont.charCount; i++) {
    uint8_t indexEntry[20];
    bytesRead = g_binFont.fontFile.read(indexEntry, 20);
    if (bytesRead != 20) {
      Serial.printf("✗ Index read error at entry %d\n", i);
      free(g_binFont.index);
      g_binFont.fontFile.close();
      return false;
    }
    
    g_binFont.index[i].unicode = indexEntry[0] | (indexEntry[1] << 8) | (indexEntry[2] << 16) | (indexEntry[3] << 24);
    g_binFont.index[i].width = indexEntry[4] | (indexEntry[5] << 8);
    g_binFont.index[i].height = indexEntry[6] | (indexEntry[7] << 8);
    g_binFont.index[i].bitmapOffset = indexEntry[8] | (indexEntry[9] << 8) | (indexEntry[10] << 16) | (indexEntry[11] << 24);
    g_binFont.index[i].bitmapSize = indexEntry[12] | (indexEntry[13] << 8) | (indexEntry[14] << 16) | (indexEntry[15] << 24);
    
    if (i == 0) {
      Serial.print("Raw bytes: ");
      for (int j = 0; j < 20; j++) {
        Serial.printf("%02X ", indexEntry[j]);
      }
      Serial.println();
    }
    
    if (i < 5) {
      char ch = (g_binFont.index[i].unicode < 128) ? (char)g_binFont.index[i].unicode : '?';
      Serial.printf("  [%d] U+%04X '%c' %dx%d offset=%d size=%d\n",
        i, g_binFont.index[i].unicode, ch,
        g_binFont.index[i].width, g_binFont.index[i].height,
        g_binFont.index[i].bitmapOffset, g_binFont.index[i].bitmapSize);
    }
    
    if (i % 100 == 0) yield();
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

bool drawBinFontChar(uint32_t unicode, int x, int y, uint16_t color) {
  if (!g_binFont.loaded) return false;
  
  GlyphIndex* glyph = findGlyph(unicode);
  if (!glyph) {
    char ch[5] = {0};
    utf8Encode(unicode, ch);
    Serial.printf("Char not found: U+%04X '%s'\n", unicode, ch);
    return false;
  }
  if (glyph->bitmapSize == 0 || glyph->width == 0 || glyph->height == 0) {
    Serial.printf("Invalid glyph: U+%04X size=%d %dx%d\n", unicode, glyph->bitmapSize, glyph->width, glyph->height);
    return false;
  }
  
  uint8_t* bitmap = (uint8_t*)malloc(glyph->bitmapSize);
  if (!bitmap) {
    return false;
  }
  
  if (sdMutex != NULL) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
  }
  
  g_binFont.fontFile.seek(glyph->bitmapOffset);
  size_t bytesRead = g_binFont.fontFile.read(bitmap, glyph->bitmapSize);
  
  if (sdMutex != NULL) {
    xSemaphoreGive(sdMutex);
  }
  
  if (bytesRead != glyph->bitmapSize) {
    free(bitmap);
    return false;
  }
  
  for (int py = 0; py < glyph->height; py++) {
    int lineStart = -1;
    for (int px = 0; px < glyph->width; px++) {
      int byteIdx = (py * glyph->width + px) / 8;
      int bitIdx = (py * glyph->width + px) % 8;
      
      bool isBlack = bitmap[byteIdx] & (1 << (7 - bitIdx));
      
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
      M5.Display.drawFastHLine(x + lineStart, y + py, glyph->width - lineStart, color);
    }
  }
  
  free(bitmap);
  return true;
}

int drawBinFontString(const String &text, int x, int y, int charSpacing) {
  if (!g_binFont.loaded) return x;
  
  int currentX = x;
  for (int i = 0; i < text.length(); ) {
    int charStart = i;
    uint32_t unicode = utf8Decode(text, i);
    
    if (drawBinFontChar(unicode, currentX, y)) {
      GlyphIndex* glyph = findGlyph(unicode);
      if (glyph) {
        currentX += glyph->width + 2;
      } else {
        currentX += charSpacing;
      }
    } else {
      M5.Display.setCursor(currentX, y);
      String ch = text.substring(charStart, i);
      M5.Display.print(ch);
      currentX += charSpacing;
    }
  }
  
  return currentX;
}

// ==================== TTF Font Loading ====================

bool loadTTFFont(const char* fontPath, int size) {
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
  if (fname.endsWith(".ttf") || fname.endsWith(".TTF") ||
      fname.endsWith(".ttc") || fname.endsWith(".TTC")) {
    return loadTTFFont(fname.c_str(), readingFontSize);
  } else if (fname.endsWith(".bin") || fname.endsWith(".BIN")) {
    if (ofrFontLoaded) { ofr.unloadFont(); ofrFontLoaded = false; }
    return loadBinaryFont(fname.c_str());
  }
  return false;
}
