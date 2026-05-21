#include "globals.h"
#include "esp_task_wdt.h"

// ==================== ZIP / EPUB Reader ====================

// Little-endian binary readers
static uint16_t zipU16(const uint8_t* b) { return b[0] | (b[1] << 8); }
static uint32_t zipU32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

// Helper: log loading step (serial + watchdog only, no e-ink display to avoid bus conflicts)
static int _lastStepNum = -1;
static unsigned long _lastStepMillis = 0;
// epubLoad progress occupies 0-70% of the bar; post-load steps cover 70-100%
static int _totalChaptersForProgress = 0;
// Only update the on-screen progress bar during initial book load (epubLoad),
// not during chapter reloads triggered by page turns.
static bool _progressDisplayEnabled = false;

static void showLoadStep(const char* step) {
  Serial.printf("EPUB step: %s (heap=%u, psram=%u)\n", step, ESP.getFreeHeap(), ESP.getFreePsram());
  _lastStepNum++;
  _lastStepMillis = millis();
  esp_task_wdt_reset();
  if (!_progressDisplayEnabled) return;  // Skip screen updates during page-turn reloads
  // Map step number to 0-70%: first 8 fixed steps are ~5% each = 40%,
  // then per-chapter steps fill up to 70%
  int pct;
  if (_lastStepNum <= 8) {
    pct = _lastStepNum * 5;  // 0..40%
  } else if (_totalChaptersForProgress > 0) {
    // Scale chapter progress from 40% to 70% based on actual chapter count
    int chapterStep = _lastStepNum - 8;
    pct = 40 + min((30 * chapterStep * 5) / _totalChaptersForProgress, 30);
  } else {
    pct = 40 + min((_lastStepNum - 8) * 3, 30);  // 40..70%
  }
  updateLoadProgress(pct);
}

static bool xmlIsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool xmlNameChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
}

static String xmlAttrValue(const String& text, const char* attrName, int searchFrom = 0) {
  int nameLen = strlen(attrName);
  int pos = searchFrom;
  while (pos < (int)text.length()) {
    pos = text.indexOf(attrName, pos);
    if (pos < 0) break;
    if (pos > 0 && xmlNameChar(text.charAt(pos - 1))) { pos += nameLen; continue; }

    int cursor = pos + nameLen;
    while (cursor < (int)text.length() && xmlIsSpace(text.charAt(cursor))) cursor++;
    if (cursor >= (int)text.length() || text.charAt(cursor) != '=') { pos += nameLen; continue; }
    cursor++;
    while (cursor < (int)text.length() && xmlIsSpace(text.charAt(cursor))) cursor++;
    if (cursor >= (int)text.length()) break;

    char quote = text.charAt(cursor);
    if (quote != '"' && quote != '\'') { pos += nameLen; continue; }
    int valueStart = cursor + 1;
    int valueEnd = text.indexOf(quote, valueStart);
    if (valueEnd < 0) break;
    return text.substring(valueStart, valueEnd);
  }
  return "";
}

static String xmlDecodeEntities(const String& text) {
  if (text.indexOf('&') < 0) return text;

  String out;
  out.reserve(text.length());
  for (int i = 0; i < (int)text.length(); i++) {
    if (text.charAt(i) != '&') { out += text.charAt(i); continue; }

    int semi = text.indexOf(';', i + 1);
    if (semi < 0 || semi - i > 12) { out += text.charAt(i); continue; }

    String entity = text.substring(i, semi + 1);
    if (entity == "&amp;") out += '&';
    else if (entity == "&lt;") out += '<';
    else if (entity == "&gt;") out += '>';
    else if (entity == "&quot;") out += '"';
    else if (entity == "&apos;") out += '\'';
    else if (entity == "&nbsp;") out += ' ';
    else if (entity.length() > 3 && entity.charAt(1) == '#') {
      long code = 0;
      if (entity.charAt(2) == 'x' || entity.charAt(2) == 'X') code = strtol(entity.c_str() + 3, nullptr, 16);
      else code = strtol(entity.c_str() + 2, nullptr, 10);
      if (code > 0) utf8Encode((uint32_t)code, out);
      else out += entity;
    } else {
      out += entity;
    }
    i = semi;
  }
  return out;
}

#define MAX_ZIP_ENTRIES 3000

// Parse ZIP central directory and return entries
int zipReadDirectory(File& f, ZipEntry* entries, int maxEntries) {
  uint32_t fileSize = f.size();
  if (fileSize < 22) return 0;

  // Search backwards for End of Central Directory record (signature 0x06054b50)
  // Read in chunks for efficiency (avoids thousands of individual SD card reads)
  uint32_t searchLen = (fileSize > 65557) ? 65557 : fileSize;
  uint32_t searchFrom = fileSize - searchLen;
  uint32_t eocdPos = 0;
  bool found = false;

  const int CHUNK = 4096;
  uint8_t buf[CHUNK];

  for (uint32_t offset = fileSize; offset > searchFrom && !found; ) {
    uint32_t readStart = (offset > (uint32_t)CHUNK) ? offset - CHUNK : searchFrom;
    uint32_t readLen = offset - readStart;

    f.seek(readStart);
    if (f.read(buf, readLen) != readLen) break;
    yield();

    // Search backwards within this chunk
    for (int i = (int)readLen - 4; i >= 0 && !found; i--) {
      if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x05 && buf[i+3] == 0x06) {
        eocdPos = readStart + i;
        found = true;
      }
    }
    if (readStart == searchFrom) break;
    offset = readStart + 3;  // overlap chunks so signatures crossing a boundary are found
  }
  if (!found) {
    Serial.println("ZIP: EOCD not found");
    return 0;
  }

  // Read EOCD fields
  uint8_t eocd[18];
  f.seek(eocdPos + 4);
  f.read(eocd, 18);
  uint16_t numEntries = zipU16(eocd + 4);  // Total entries
  uint32_t cdSize = zipU32(eocd + 8);
  uint32_t cdOffset = zipU32(eocd + 12);

  Serial.printf("ZIP: %d entries, CD at offset %u, size %u\n", numEntries, cdOffset, cdSize);

  // Read central directory
  f.seek(cdOffset);
  int count = 0;
  uint8_t hdr[46];

  for (int i = 0; i < numEntries && count < maxEntries; i++) {
    if (f.read(hdr, 46) != 46) break;
    // Verify central directory signature 0x02014b50
    if (hdr[0] != 0x50 || hdr[1] != 0x4B || hdr[2] != 0x01 || hdr[3] != 0x02) break;

    uint16_t method = zipU16(hdr + 10);
    uint32_t compSize = zipU32(hdr + 20);
    uint32_t uncompSize = zipU32(hdr + 24);
    uint16_t nameLen = zipU16(hdr + 28);
    uint16_t extraLen = zipU16(hdr + 30);
    uint16_t commentLen = zipU16(hdr + 32);
    uint32_t localOff = zipU32(hdr + 42);

    // Read filename
    char nameBuf[256];
    int toRead = (nameLen < 255) ? nameLen : 255;
    f.read((uint8_t*)nameBuf, toRead);
    nameBuf[toRead] = '\0';
    if (nameLen > toRead) f.seek(f.position() + (nameLen - toRead));

    // Skip extra field and comment
    f.seek(f.position() + extraLen + commentLen);

    entries[count].filename = String(nameBuf);
    entries[count].method = method;
    entries[count].compSize = compSize;
    entries[count].uncompSize = uncompSize;
    entries[count].localOffset = localOff;
    count++;
    if ((count & 63) == 0) { yield(); esp_task_wdt_reset(); }  // prevent watchdog during large ZIP parsing
  }

  return count;
}

// Extract a file from ZIP to a newly allocated buffer (caller must free)
// Returns nullptr on failure, sets outLen on success
uint8_t* zipExtractFile(File& f, ZipEntry& entry, size_t& outLen) {
  // Read local file header to find actual data offset
  f.seek(entry.localOffset);
  uint8_t lhdr[30];
  if (f.read(lhdr, 30) != 30) return nullptr;
  // Verify local header signature 0x04034b50
  if (lhdr[0] != 0x50 || lhdr[1] != 0x4B || lhdr[2] != 0x03 || lhdr[3] != 0x04) return nullptr;

  uint16_t nameLen = zipU16(lhdr + 26);
  uint16_t extraLen = zipU16(lhdr + 28);
  uint32_t dataOffset = entry.localOffset + 30 + nameLen + extraLen;

  f.seek(dataOffset);

  if (entry.method == 0) {
    // Stored (no compression)
    if (entry.uncompSize > 4 * 1024 * 1024) {
      Serial.printf("ZIP: entry too large (%u bytes), skipping\n", entry.uncompSize);
      return nullptr;
    }
    uint8_t* buf = (uint8_t*)ps_malloc(entry.uncompSize + 1);
    if (!buf) {
      Serial.printf("ZIP: ps_malloc(%u) failed for stored, free PSRAM: %u\n",
                     entry.uncompSize + 1, ESP.getFreePsram());
      return nullptr;
    }
    f.read(buf, entry.uncompSize);
    yield();
    esp_task_wdt_reset();
    buf[entry.uncompSize] = '\0';
    outLen = entry.uncompSize;
    return buf;
  }
  else if (entry.method == 8) {
    // Deflated — read compressed data then inflate using ROM
    if (entry.compSize > 4 * 1024 * 1024 || entry.uncompSize > 8 * 1024 * 1024) {
      Serial.printf("ZIP: entry too large (comp=%u, uncomp=%u), skipping\n", entry.compSize, entry.uncompSize);
      return nullptr;
    }
    uint8_t* compBuf = (uint8_t*)ps_malloc(entry.compSize);
    if (!compBuf) {
      Serial.printf("ZIP: ps_malloc(%u) failed for compBuf, free PSRAM: %u\n",
                     entry.compSize, ESP.getFreePsram());
      return nullptr;
    }
    f.read(compBuf, entry.compSize);
    yield();
    esp_task_wdt_reset();  // feed watchdog after large SD read

    uint8_t* outBuf = (uint8_t*)ps_malloc(entry.uncompSize + 1);
    if (!outBuf) {
      Serial.printf("ZIP: ps_malloc(%u) failed for outBuf, free PSRAM: %u\n",
                     entry.uncompSize + 1, ESP.getFreePsram());
      free(compBuf);
      return nullptr;
    }

    size_t result = tinfl_decompress_mem_to_mem(outBuf, entry.uncompSize,
                                                 compBuf, entry.compSize, 0);
    free(compBuf);
    yield();
    esp_task_wdt_reset();  // feed watchdog after decompression

    if (result == (size_t)(-1)) {
      Serial.printf("ZIP: inflate failed for %s\n", entry.filename.c_str());
      free(outBuf);
      return nullptr;
    }
    outBuf[result] = '\0';
    outLen = result;
    return outBuf;
  }

  Serial.printf("ZIP: unsupported method %d for %s\n", entry.method, entry.filename.c_str());
  return nullptr;
}

// Extract a file from ZIP as a String (for XML/HTML text)
String zipExtractString(File& f, ZipEntry& entry) {
  size_t len = 0;
  uint8_t* buf = zipExtractFile(f, entry, len);
  if (!buf) return "";
  String result = String((char*)buf);
  free(buf);
  return result;
}

// ==================== Direct HTML-to-text stripping ====================
// Processes raw HTML buffer and writes stripped text directly to output buffer.
// Avoids intermediate String allocations for reduced peak memory.
// If basePath is non-empty, inserts image markers for <img> tags.
// Returns number of bytes written to outBuf.
size_t htmlStripDirect(const char* htmlBuf, size_t htmlLen,
                       char* outBuf, size_t outBufSize,
                       const String& basePath) {
  size_t outPos = 0;
  bool inScript = false;
  bool inHead = false;
  bool lastWasNewline = false;
  bool lastWasSpace = false;
  size_t yieldCounter = 0;

  for (size_t i = 0; i < htmlLen && outPos < outBufSize - 200; i++) {
    // Yield periodically to prevent watchdog timeout on large chapters
    if (++yieldCounter >= 8192) { yieldCounter = 0; yield(); esp_task_wdt_reset(); }
    char c = htmlBuf[i];

    if (c == '<') {
      // Scan ahead to find '>'
      size_t tagEnd = i + 1;
      while (tagEnd < htmlLen && htmlBuf[tagEnd] != '>') tagEnd++;
      if (tagEnd >= htmlLen) break;

      // Extract tag name (lowercase, up to first space/slash)
      char tagName[32];
      int tnLen = 0;
      for (size_t j = i + 1; j < tagEnd && tnLen < 30; j++) {
        char tc = htmlBuf[j];
        if (tc == ' ' || tc == '\t' || tc == '\n' || tc == '\r') {
          if (tnLen > 0) break;
          continue;
        }
        if (tc == '/' && tnLen == 0) { tagName[tnLen++] = '/'; continue; }
        tagName[tnLen++] = tolower(tc);
      }
      tagName[tnLen] = '\0';

      // Handle image tags: <img src="..."> and SVG <image href="..."> / <image xlink:href="...">
      if (basePath.length() > 0 &&
          (strcmp(tagName, "img") == 0 || strcmp(tagName, "image") == 0)) {
        // Try to find src/href/xlink:href attribute
        const char* attrPatterns[] = {"src=", "href=", "xlink:href="};
        int attrLens[] = {4, 5, 11};
        int numPatterns = 3;
        bool foundImg = false;

        for (int ap = 0; ap < numPatterns && !foundImg; ap++) {
          for (size_t j = i + 1; j < tagEnd; j++) {
            bool match = true;
            for (int al = 0; al < attrLens[ap] && j + al < tagEnd; al++) {
              if (tolower(htmlBuf[j + al]) != attrPatterns[ap][al]) { match = false; break; }
            }
            if (!match) continue;

            size_t valStart = j + attrLens[ap];
            if (valStart >= tagEnd) break;
            char quote = htmlBuf[valStart];
            if (quote == '"' || quote == '\'') {
              valStart++;
              size_t valEnd = valStart;
              while (valEnd < tagEnd && htmlBuf[valEnd] != quote) valEnd++;
              if (valEnd < tagEnd && valEnd > valStart) {
                // Write image marker: \x01<basePath><imgSrc>\x01
                outBuf[outPos++] = EPUB_IMG_MARKER;
                for (size_t k = 0; k < basePath.length() && outPos < outBufSize - 100; k++)
                  outBuf[outPos++] = basePath.charAt(k);
                // Write src value with URL decoding
                for (size_t k = valStart; k < valEnd && outPos < outBufSize - 10; k++) {
                  if (htmlBuf[k] == '%' && k + 2 < valEnd) {
                    char hex[3] = {htmlBuf[k+1], htmlBuf[k+2], 0};
                    unsigned char decoded = (unsigned char)strtol(hex, nullptr, 16);
                    if (decoded != 0) { outBuf[outPos++] = (char)decoded; k += 2; continue; }
                  }
                  outBuf[outPos++] = htmlBuf[k];
                }
                outBuf[outPos++] = EPUB_IMG_MARKER;
                outBuf[outPos++] = '\n';
                lastWasNewline = true;
                foundImg = true;
              }
            }
            break;
          }
        }
      }

      // Track <head> section to suppress non-body text
      if (strcmp(tagName, "head") == 0) inHead = true;
      else if (strcmp(tagName, "/head") == 0) inHead = false;

      // Handle script/style
      if (strcmp(tagName, "script") == 0 || strcmp(tagName, "style") == 0) inScript = true;
      else if (strcmp(tagName, "/script") == 0 || strcmp(tagName, "/style") == 0) inScript = false;

      // Handle block elements → insert newline
      if (!inScript) {
        // <br> / <br/> → space (not paragraph break).
        // In many EPUBs, <br> is used for line breaks in poems/verse within a <p>.
        // Treating it as a full paragraph break (column change) splits words and
        // orphans punctuation (e.g. 設<br/>計 → 設 | 計 across columns).
        if (strcmp(tagName, "br") == 0 || strcmp(tagName, "br/") == 0) {
          if (!lastWasSpace && outPos > 0) {
            outBuf[outPos++] = ' ';
            lastWasSpace = true;
          }
        }
        else if (strcmp(tagName, "/p") == 0 || strcmp(tagName, "/div") == 0 ||
            (tagName[0] == '/' && tagName[1] == 'h') ||
            strcmp(tagName, "/li") == 0 || strcmp(tagName, "/tr") == 0) {
          if (!lastWasNewline && outPos > 0) {
            outBuf[outPos++] = '\n';
            lastWasNewline = true;
            lastWasSpace = true;  // Prevent space after block-tag newline
          }
        }
        // Inline style tags → insert style markers
        if (outPos < outBufSize - 10) {
          if (strcmp(tagName, "em") == 0 || strcmp(tagName, "i") == 0)
            outBuf[outPos++] = STYLE_ITALIC_ON;
          else if (strcmp(tagName, "/em") == 0 || strcmp(tagName, "/i") == 0)
            outBuf[outPos++] = STYLE_ITALIC_OFF;
          else if (strcmp(tagName, "strong") == 0 || strcmp(tagName, "b") == 0)
            outBuf[outPos++] = STYLE_BOLD_ON;
          else if (strcmp(tagName, "/strong") == 0 || strcmp(tagName, "/b") == 0)
            outBuf[outPos++] = STYLE_BOLD_OFF;
          else if (strcmp(tagName, "/a") == 0)
            outBuf[outPos++] = STYLE_UNDERLINE_OFF;
        }

        // Handle <a href="..."> → emit link marker + underline
        if (strcmp(tagName, "a") == 0 && basePath.length() > 0 && outPos < outBufSize - 200) {
          // Extract href attribute
          for (size_t j = i + 1; j < tagEnd; j++) {
            bool match = true;
            const char* pat = "href=";
            for (int al = 0; al < 5 && j + al < tagEnd; al++) {
              if (tolower(htmlBuf[j + al]) != pat[al]) { match = false; break; }
            }
            if (!match) continue;
            size_t valStart = j + 5;
            if (valStart >= tagEnd) break;
            char quote = htmlBuf[valStart];
            if (quote == '"' || quote == '\'') {
              valStart++;
              size_t valEnd = valStart;
              while (valEnd < tagEnd && htmlBuf[valEnd] != quote) valEnd++;
              if (valEnd > valStart && valEnd < tagEnd) {
                // Emit \x08<basePath><href>\x08\x06
                outBuf[outPos++] = EPUB_LINK_MARKER;
                for (size_t k = 0; k < basePath.length() && outPos < outBufSize - 100; k++)
                  outBuf[outPos++] = basePath.charAt(k);
                for (size_t k = valStart; k < valEnd && outPos < outBufSize - 10; k++) {
                  if (htmlBuf[k] == '%' && k + 2 < valEnd) {
                    char hex[3] = {htmlBuf[k+1], htmlBuf[k+2], 0};
                    unsigned char decoded = (unsigned char)strtol(hex, nullptr, 16);
                    if (decoded != 0) { outBuf[outPos++] = (char)decoded; k += 2; continue; }
                  }
                  outBuf[outPos++] = htmlBuf[k];
                }
                outBuf[outPos++] = EPUB_LINK_MARKER;
                outBuf[outPos++] = STYLE_UNDERLINE_ON;
              }
            }
            break;
          }
        }
      }

      i = tagEnd;
      continue;
    }

    if (inScript || inHead) continue;

    // Handle HTML entities
    if (c == '&') {
      char entity[16];
      int eLen = 0;
      entity[eLen++] = '&';
      size_t j = i + 1;
      while (j < htmlLen && j - i < 12 && htmlBuf[j] != ';' && htmlBuf[j] != '<') {
        entity[eLen++] = htmlBuf[j++];
      }
      if (j < htmlLen && htmlBuf[j] == ';') {
        entity[eLen++] = ';';
        entity[eLen] = '\0';
        i = j;

        if (strcmp(entity, "&amp;") == 0) { outBuf[outPos++] = '&'; lastWasNewline = false; }
        else if (strcmp(entity, "&lt;") == 0) { outBuf[outPos++] = '<'; lastWasNewline = false; }
        else if (strcmp(entity, "&gt;") == 0) { outBuf[outPos++] = '>'; lastWasNewline = false; }
        else if (strcmp(entity, "&quot;") == 0) { outBuf[outPos++] = '"'; lastWasNewline = false; }
        else if (strcmp(entity, "&apos;") == 0) { outBuf[outPos++] = '\''; lastWasNewline = false; }
        else if (strcmp(entity, "&nbsp;") == 0) {
          if (!lastWasSpace && !lastWasNewline && outPos < outBufSize - 10) {
            outBuf[outPos++] = ' ';
            lastWasSpace = true;
          }
        }
        else if (entity[1] == '#') {
          long code = 0;
          if (entity[2] == 'x' || entity[2] == 'X')
            code = strtol(entity + 3, nullptr, 16);
          else
            code = strtol(entity + 2, nullptr, 10);
          if (code > 0) {
            char utf8[5];
            int len = utf8Encode((uint32_t)code, utf8);
            for (int k = 0; k < len && outPos < outBufSize - 10; k++)
              outBuf[outPos++] = utf8[k];
            lastWasNewline = false;
          }
        }
        continue;
      }
    }

    // Normal character — pass through unchanged.
    // In HTML, raw newlines/carriage returns are whitespace, equivalent to spaces.
    // Only block-level tags (</p>, <br>, etc.) produce structural line breaks.
    // All whitespace (space, tab, \n, \r, U+00A0) → collapse to one regular space
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t' ||
        ((unsigned char)c == 0xC2 && i + 1 < htmlLen && (unsigned char)htmlBuf[i + 1] == 0xA0)) {
      if ((unsigned char)c == 0xC2) i++;  // Skip second byte of U+00A0
      if (!lastWasSpace && !lastWasNewline && outPos < outBufSize - 10) {
        outBuf[outPos++] = ' ';
        lastWasSpace = true;
      }
    } else {
      outBuf[outPos++] = c;
      lastWasNewline = false;
      lastWasSpace = false;
    }
  }
  return outPos;
}

// Get directory part of a path (e.g., "OEBPS/content.opf" → "OEBPS/")
static String pathDir(const String& path) {
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash >= 0) return path.substring(0, lastSlash + 1);
  return "";
}

// Normalize a path by resolving ".." and "." segments
// e.g., "OEBPS/text/../images/cover.jpg" → "OEBPS/images/cover.jpg"
String pathNormalize(const String& path) {
  // Split into segments
  String segments[32];
  int segCount = 0;
  int start = 0;
  for (int i = 0; i <= (int)path.length(); i++) {
    if (i == (int)path.length() || path.charAt(i) == '/') {
      String seg = path.substring(start, i);
      if (seg == "..") {
        if (segCount > 0) segCount--;  // Go up one level
      } else if (seg.length() > 0 && seg != ".") {
        if (segCount < 32) segments[segCount++] = seg;
      }
      start = i + 1;
    }
  }
  String result;
  for (int i = 0; i < segCount; i++) {
    if (i > 0) result += '/';
    result += segments[i];
  }
  return result;
}

// Extract title from EPUB's OPF metadata (lightweight, for book list display)
String epubGetTitle(const String& epubPath) {
  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubPath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubPath.c_str()); }
  if (!f) return "";

  // Only need container.xml and OPF — use small allocation, scan early entries
  const int TITLE_MAX_ENTRIES = 500;
  ZipEntry* entries = (ZipEntry*)ps_malloc(sizeof(ZipEntry) * TITLE_MAX_ENTRIES);
  if (!entries) { f.close(); return ""; }
  for (int i = 0; i < TITLE_MAX_ENTRIES; i++) new (&entries[i]) ZipEntry();
  int entryCount = zipReadDirectory(f, entries, TITLE_MAX_ENTRIES);
  if (entryCount == 0) {
    for (int i = 0; i < entryCount; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return "";
  }

  // Find OPF path from container.xml
  String opfPath = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == "META-INF/container.xml") {
      String containerXml = zipExtractString(f, entries[i]);
      opfPath = xmlDecodeEntities(xmlAttrValue(containerXml, "full-path"));
      break;
    }
  }
  if (opfPath.length() == 0) {
    for (int i = 0; i < entryCount; i++) {
      if (entries[i].filename.endsWith(".opf")) { opfPath = entries[i].filename; break; }
    }
  }
  if (opfPath.length() == 0) {
    for (int i = 0; i < entryCount; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return "";
  }

  // Read OPF and extract <dc:title>
  String opfContent = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == opfPath) {
      // Guard: skip very large OPF files (e.g. manga with thousands of entries)
      if (entries[i].uncompSize > 200 * 1024) {
        Serial.printf("EPUB title: OPF too large (%u bytes), skipping\n", entries[i].uncompSize);
        break;
      }
      opfContent = zipExtractString(f, entries[i]);
      break;
    }
  }
  for (int i = 0; i < entryCount; i++) entries[i].~ZipEntry();
  free(entries); f.close();

  if (opfContent.length() == 0) return "";

  // Try <dc:title>...</dc:title>
  String title = "";
  int tStart = opfContent.indexOf("<dc:title");
  if (tStart >= 0) {
    int tContentStart = opfContent.indexOf('>', tStart);
    if (tContentStart >= 0) {
      tContentStart++;
      int tEnd = opfContent.indexOf("</dc:title>", tContentStart);
      if (tEnd > tContentStart) title = opfContent.substring(tContentStart, tEnd);
    }
  }
  title.trim();
  Serial.printf("EPUB title: '%s' from %s\n", title.c_str(), epubPath.c_str());
  return title;
}

// Open EPUB: parse metadata and build chapter index, then load initial chapters.
// Does NOT load all text upfront — uses chapter windowing for big files.
bool epubLoad(const String& epubPath, bool isComic) {
  Serial.printf("\n=== EPUB: Loading %s ===\n", epubPath.c_str());

  // Clean up previous EPUB data
  _lastStepNum = -1;  // Reset so all steps display fresh
  _lastStepMillis = 0;
  _totalChaptersForProgress = 0;
  _progressDisplayEnabled = true;  // Enable on-screen progress for initial load
  resetLoadProgress();
  showLoadStep("cleanup");
  epubCleanup();

  // Comic subfolder: set image-based mode right after cleanup
  if (isComic) {
    epubIsImageBased = true;
    _progressDisplayEnabled = false;
    Serial.println("EPUB: Image-based mode (comic subfolder)");
  }

  epubFilePath = epubPath;

  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubPath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubPath.c_str()); }
  if (!f) {
    Serial.printf("EPUB: Cannot open file: %s\n", epubPath.c_str());
    char detail[60];
    snprintf(detail, sizeof(detail), "SD.open: %.40s", epubPath.c_str() + epubPath.lastIndexOf('/') + 1);
    lastLoadError = String(detail);
    return false;
  }

  Serial.printf("EPUB: File size = %u bytes, Free PSRAM = %u bytes\n", f.size(), ESP.getFreePsram());

  // Parse ZIP central directory — allocate based on actual entry count
  showLoadStep("parse ZIP");
  // Pre-scan EOCD to get actual entry count before allocating
  int actualZipCount = MAX_ZIP_ENTRIES;
  {
    uint32_t fileSize = f.size();
    uint32_t searchLen = (fileSize > 65557) ? 65557 : fileSize;
    uint32_t searchFrom = fileSize - searchLen;
    const int CHUNK = 4096;
    uint8_t sbuf[CHUNK];
    bool found = false;
    for (uint32_t offset = fileSize; offset > searchFrom && !found; ) {
      uint32_t readStart = (offset > (uint32_t)CHUNK) ? offset - CHUNK : searchFrom;
      uint32_t readLen = offset - readStart;
      f.seek(readStart);
      if (f.read(sbuf, readLen) != readLen) break;
      for (int i = (int)readLen - 4; i >= 0 && !found; i--) {
        if (sbuf[i]==0x50 && sbuf[i+1]==0x4B && sbuf[i+2]==0x05 && sbuf[i+3]==0x06) {
          f.seek(readStart + i + 4 + 4); // skip sig + disk fields
          uint8_t nb[2];
          if (f.read(nb, 2) == 2) {
            actualZipCount = nb[0] | (nb[1] << 8);
            found = true;
          }
        }
      }
      if (readStart == searchFrom) break;
      offset = readStart + 3;
      yield();
      esp_task_wdt_reset();
    }
    f.seek(0); // reset for full parse
  }
  if (actualZipCount > MAX_ZIP_ENTRIES) actualZipCount = MAX_ZIP_ENTRIES;
  Serial.printf("EPUB: Allocating %d ZipEntries (%u bytes)\n", actualZipCount,
                (unsigned)(sizeof(ZipEntry) * actualZipCount));
  epubZipEntries = (ZipEntry*)ps_malloc(sizeof(ZipEntry) * actualZipCount);
  if (!epubZipEntries) {
    Serial.printf("EPUB: Failed to allocate ZipEntries! Free PSRAM: %u\n", ESP.getFreePsram());
    lastLoadError = "alloc ZipEnt";
    f.close(); return false;
  }
  for (int i = 0; i < actualZipCount; i++) new (&epubZipEntries[i]) ZipEntry();
  epubZipEntryCount = zipReadDirectory(f, epubZipEntries, actualZipCount);
  Serial.printf("EPUB: ZIP has %d entries\n", epubZipEntryCount);
  if (epubZipEntryCount == 0) {
    lastLoadError = "ZIP 0 entries";
    epubCleanup(); f.close(); return false;
  }

  // Step 1: Find container.xml → get OPF path
  showLoadStep("find OPF");
  String opfPath = "";
  for (int i = 0; i < epubZipEntryCount; i++) {
    if (epubZipEntries[i].filename == "META-INF/container.xml") {
      String containerXml = zipExtractString(f, epubZipEntries[i]);
      opfPath = xmlDecodeEntities(xmlAttrValue(containerXml, "full-path"));
      break;
    }
  }
  if (opfPath.length() == 0) {
    for (int i = 0; i < epubZipEntryCount; i++) {
      if (epubZipEntries[i].filename.endsWith(".opf")) { opfPath = epubZipEntries[i].filename; break; }
    }
  }
  Serial.printf("EPUB: OPF path = %s\n", opfPath.c_str());
  if (opfPath.length() == 0) {
    lastLoadError = "no OPF";
    epubCleanup(); f.close(); return false;
  }

  epubBasePath = pathDir(opfPath);

  // Step 2: Read OPF → build manifest (id→href) and read spine order
  showLoadStep("manifest");
  String opfContent = "";
  for (int i = 0; i < epubZipEntryCount; i++) {
    if (epubZipEntries[i].filename == opfPath) {
      opfContent = zipExtractString(f, epubZipEntries[i]);
      break;
    }
  }
  if (opfContent.length() == 0) {
    lastLoadError = "empty OPF";
    epubCleanup(); f.close(); return false;
  }

  // Log language from OPF metadata (layout controlled by user's English/Chinese toggle)
  {
    int langStart = opfContent.indexOf("<dc:language");
    if (langStart >= 0) {
      int langContentStart = opfContent.indexOf('>', langStart);
      if (langContentStart >= 0) {
        langContentStart++;
        int langEnd = opfContent.indexOf("</dc:language>", langContentStart);
        if (langEnd > langContentStart) {
          String lang = opfContent.substring(langContentStart, langEnd);
          lang.trim();
          Serial.printf("EPUB language: '%s', horizontal=%d\n", lang.c_str(), epubIsHorizontal);
        }
      }
    }
  }

  // Parse manifest: <item id="..." href="..." media-type="..."/>
  // First pass: count items to allocate exact size
  struct ManifestItem { String id; String href; bool isContent; };
  int manifestCount = 0;
  {
    int searchFrom = 0;
    while (searchFrom < (int)opfContent.length()) {
      int itemPos = opfContent.indexOf("<item ", searchFrom);
      if (itemPos < 0) break;
      int itemEnd = opfContent.indexOf("/>", itemPos);
      if (itemEnd < 0) itemEnd = opfContent.indexOf(">", itemPos);
      if (itemEnd < 0) break;
      manifestCount++;
      searchFrom = itemEnd + 1;
    }
  }
  if (manifestCount == 0) manifestCount = 1; // safety
  Serial.printf("EPUB: Manifest count = %d\n", manifestCount);
  ManifestItem* manifest = (ManifestItem*)ps_malloc(sizeof(ManifestItem) * manifestCount);
  if (!manifest) { lastLoadError = "alloc manifest"; epubCleanup(); f.close(); return false; }
  for (int i = 0; i < manifestCount; i++) new (&manifest[i]) ManifestItem();
  int manifestParsed = 0;

  {
    int searchFrom = 0;
      while (searchFrom < (int)opfContent.length() && manifestParsed < manifestCount) {
      int itemPos = opfContent.indexOf("<item ", searchFrom);
      if (itemPos < 0) break;
      int itemEnd = opfContent.indexOf("/>", itemPos);
      if (itemEnd < 0) itemEnd = opfContent.indexOf(">", itemPos);
      if (itemEnd < 0) break;
      String itemTag = opfContent.substring(itemPos, itemEnd + 2);

      String id = xmlDecodeEntities(xmlAttrValue(itemTag, "id"));
      String href = xmlDecodeEntities(xmlAttrValue(itemTag, "href"));
      String mediaType = xmlDecodeEntities(xmlAttrValue(itemTag, "media-type"));

      manifest[manifestParsed].id = id;
      manifest[manifestParsed].href = href;
      manifest[manifestParsed].isContent = (mediaType.indexOf("html") >= 0);
      manifestParsed++;

      searchFrom = itemEnd + 1;
      if ((manifestParsed & 31) == 0) { yield(); esp_task_wdt_reset(); }
    }
  }
  manifestCount = manifestParsed;
  Serial.printf("EPUB: Manifest has %d items\n", manifestCount);

  // Parse spine: <itemref idref="..."/>
  showLoadStep("spine");
  // First pass: count spine items
  int spineCount = 0;
  {
    int spineStart = opfContent.indexOf("<spine");
    int spineEnd = opfContent.indexOf("</spine>");
    if (spineStart >= 0 && spineEnd > spineStart) {
      String spineSection = opfContent.substring(spineStart, spineEnd);
      int searchFrom = 0;
      while (searchFrom < (int)spineSection.length()) {
        int refPos = spineSection.indexOf("<itemref", searchFrom);
        if (refPos < 0) break;
        int refEnd = spineSection.indexOf('>', refPos);
        if (refEnd < 0) break;
        String refTag = spineSection.substring(refPos, refEnd + 1);
        if (xmlAttrValue(refTag, "idref").length() > 0) spineCount++;
        searchFrom = refEnd + 1;
      }
    }
  }
  if (spineCount == 0) spineCount = 1; // safety
  String* spineRefs = (String*)ps_malloc(sizeof(String) * spineCount);
  if (!spineRefs) {
    for (int i = 0; i < manifestCount; i++) manifest[i].~ManifestItem();
    free(manifest); lastLoadError = "alloc spine"; epubCleanup(); f.close(); return false;
  }
  for (int i = 0; i < spineCount; i++) new (&spineRefs[i]) String();
  int spineParsed = 0;
  {
    int spineStart = opfContent.indexOf("<spine");
    int spineEnd = opfContent.indexOf("</spine>");
    if (spineStart >= 0 && spineEnd > spineStart) {
      String spineSection = opfContent.substring(spineStart, spineEnd);
      int searchFrom = 0;
      while (searchFrom < (int)spineSection.length() && spineParsed < spineCount) {
        int refPos = spineSection.indexOf("<itemref", searchFrom);
        if (refPos < 0) break;
        int refEnd = spineSection.indexOf('>', refPos);
        if (refEnd < 0) break;
        String refTag = spineSection.substring(refPos, refEnd + 1);
        String idref = xmlDecodeEntities(xmlAttrValue(refTag, "idref"));
        if (idref.length() > 0) spineRefs[spineParsed++] = idref;
        searchFrom = refEnd + 1;
      }
    }
  }
  spineCount = spineParsed;
  Serial.printf("EPUB: Spine has %d items\n", spineCount);
  opfContent = "";  // Free OPF content early

  // Step 3: Build chapter index with estimated text sizes
  // Optimization: resolve spine→manifest→zip in O(spine × manifest) instead of O(spine × manifest × zip)
  // by pre-building a lookup from filename→zipIndex
  showLoadStep("chapters");
  epubChapters = (EpubChapterInfo*)ps_malloc(sizeof(EpubChapterInfo) * spineCount);
  if (!epubChapters) {
    for (int i = 0; i < spineCount; i++) spineRefs[i].~String();
    free(spineRefs);
    for (int i = 0; i < manifestCount; i++) manifest[i].~ManifestItem();
    free(manifest); lastLoadError = "alloc chapters"; epubCleanup(); f.close(); return false;
  }
  epubChapterCount = 0;
  size_t cumOffset = 0;

  for (int s = 0; s < spineCount; s++) {
    for (int m = 0; m < manifestCount; m++) {
      if (manifest[m].id == spineRefs[s] && manifest[m].isContent) {
        // URL-decode the manifest href (e.g. %20 → space, %C3%A9 → é)
        // so it matches the raw UTF-8 filenames stored in the ZIP directory.
        String href = manifest[m].href;
        bool needsDecode = false;
        for (int k = 0; k < (int)href.length(); k++) {
          if (href.charAt(k) == '%') { needsDecode = true; break; }
        }
        if (needsDecode) {
          String decoded;
          decoded.reserve(href.length());
          for (int k = 0; k < (int)href.length(); k++) {
            if (href.charAt(k) == '%' && k + 2 < (int)href.length()) {
              char hex[3] = { href.charAt(k+1), href.charAt(k+2), 0 };
              unsigned char val = (unsigned char)strtol(hex, nullptr, 16);
              if (val) { decoded += (char)val; k += 2; continue; }
            }
            decoded += href.charAt(k);
          }
          href = decoded;
        }
        String fullPath = epubBasePath + href;
        for (int z = 0; z < epubZipEntryCount; z++) {
          if (epubZipEntries[z].filename == fullPath) {
            EpubChapterInfo& ch = epubChapters[epubChapterCount];
            ch.zipEntryIndex = z;
            // Estimate: stripped text is ~50% of uncompressed HTML
            ch.estimatedTextSize = epubZipEntries[z].uncompSize / 2;
            if (ch.estimatedTextSize < 100) ch.estimatedTextSize = 100;
            ch.actualTextSize = 0;  // Not yet extracted
            ch.cumulativeOffset = cumOffset;
            cumOffset += ch.estimatedTextSize;
            epubChapterCount++;
            break;
          }
        }
        break;
      }
    }
    if ((s & 7) == 0) esp_task_wdt_reset();  // feed watchdog during spine resolution
    yield();
  }

  // Free spine and manifest — no longer needed
  for (int i = 0; i < spineCount; i++) spineRefs[i].~String();
  free(spineRefs);
  for (int i = 0; i < manifestCount; i++) manifest[i].~ManifestItem();
  free(manifest);

  epubEstimatedTotalBytes = cumOffset;
  f.close();

  Serial.printf("EPUB: %d chapters, estimated total text: %u bytes\n",
                epubChapterCount, epubEstimatedTotalBytes);
  Serial.printf("EPUB: Free PSRAM after metadata: %u bytes\n", ESP.getFreePsram());

  if (epubChapterCount == 0) {
    lastLoadError = "0 chapters";
    epubCleanup();
    return false;
  }

  // Step 4: Load initial chapters into buffer
  _totalChaptersForProgress = epubChapterCount;  // Set for progress calculation
  showLoadStep("AI智能排版中...");

  // For image-based EPUBs (comic subfolder), skip bulk chapter loading
  // (chapters will be loaded individually when pages are displayed)
  if (epubIsImageBased) {
    Serial.printf("EPUB: Skipping bulk load for image-based EPUB (%d chapters)\n", epubChapterCount);
  } else {
    if (!epubLoadChapterRange(0)) {
      if (lastLoadError.isEmpty()) lastLoadError = "chapterRange";
      epubCleanup();
      return false;
    }
  }

  // Loading complete — disable on-screen progress for subsequent chapter reloads
  _progressDisplayEnabled = false;
  Serial.printf("EPUB: load complete (heap=%u, psram=%u, imgBased=%d)\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), epubIsImageBased);
  return true;
}

// Load a range of chapters starting from startChapter into epubFullText.
// Allocates buffer using available PSRAM, loads as many chapters as fit.
// Returns true if at least one chapter was loaded.
bool epubLoadChapterRange(int startChapter) {
  if (!epubChapters || startChapter < 0 || startChapter >= epubChapterCount) {
    lastLoadError = "chRange args";
    return false;
  }
  if (epubFilePath.isEmpty() || !epubZipEntries) {
    lastLoadError = "chRange state";
    return false;
  }

  Serial.printf("EPUB: Loading chapters from %d/%d\n", startChapter, epubChapterCount);

  // Free existing text buffer
  if (epubFullText) {
    free(epubFullText);
    epubFullText = nullptr;
    epubFullTextLen = 0;
  }

  // Calculate decompression headroom: zipExtractFile needs compBuf + outBuf simultaneously
  size_t maxDecompSize = 0;
  for (int c = startChapter; c < epubChapterCount; c++) {
    ZipEntry& entry = epubZipEntries[epubChapters[c].zipEntryIndex];
    size_t needed = (size_t)entry.compSize + (size_t)entry.uncompSize + 256;
    if (needed > maxDecompSize) maxDecompSize = needed;
  }
  // Add safety margin for heap overhead + other allocations
  size_t headroom = maxDecompSize + (256 * 1024);

  size_t freePsram = ESP.getFreePsram();
  Serial.printf("EPUB: Free PSRAM: %u, max decomp needed: %u, headroom: %u\n",
                freePsram, maxDecompSize, headroom);

  if (freePsram <= headroom + 65536) {
    Serial.printf("EPUB: Not enough PSRAM! Free: %u, headroom: %u\n", freePsram, headroom);
    lastLoadError = "PSRAM low";
    return false;
  }

  // Allocate text buffer: free PSRAM minus decompression headroom
  size_t bufferSize = freePsram - headroom;
  // Cap at 4MB to be conservative
  if (bufferSize > 4 * 1024 * 1024) bufferSize = 4 * 1024 * 1024;

  Serial.printf("EPUB: Trying to allocate text buffer: %u bytes (free PSRAM: %u, largest block: %u)\n",
                bufferSize, freePsram, heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  epubFullText = (char*)ps_malloc(bufferSize);
  // If allocation fails, retry with progressively smaller sizes (PSRAM fragmentation)
  if (!epubFullText && bufferSize > 512 * 1024) {
    for (size_t trySize = bufferSize / 2; trySize >= 256 * 1024; trySize /= 2) {
      Serial.printf("EPUB: Retrying with %u bytes\n", trySize);
      esp_task_wdt_reset();
      epubFullText = (char*)ps_malloc(trySize);
      if (epubFullText) {
        bufferSize = trySize;
        Serial.printf("EPUB: Allocated %u bytes on retry\n", trySize);
        break;
      }
    }
  }
  if (!epubFullText) {
    Serial.printf("EPUB: Failed to allocate text buffer! Tried %u, free PSRAM: %u, largest free: %u\n",
                  bufferSize, ESP.getFreePsram(), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    char errDetail[40];
    snprintf(errDetail, sizeof(errDetail), "textBuf %uK/%uK", bufferSize/1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)/1024);
    lastLoadError = String(errDetail);
    return false;
  }
  epubFullTextLen = 0;
  epubLoadedStartChapter = startChapter;
  epubLoadedBaseOffset = epubChapters[startChapter].cumulativeOffset;

  Serial.printf("EPUB: Allocated %u bytes, base offset %u\n", bufferSize, epubLoadedBaseOffset);

  // Open EPUB file
  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubFilePath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubFilePath.c_str()); }
  if (!f) {
    free(epubFullText); epubFullText = nullptr;
    Serial.println("EPUB: Cannot open file for chapter loading");
    lastLoadError = "chRange open";
    return false;
  }

  // Extract chapters into buffer
  int chaptersLoaded = 0;
  size_t runningOffset = epubLoadedBaseOffset;

  for (int c = startChapter; c < epubChapterCount; c++) {
    EpubChapterInfo& ch = epubChapters[c];
    ZipEntry& entry = epubZipEntries[ch.zipEntryIndex];

    // Show progress periodically (not every chapter — reduces display bus contention)
    if (c == startChapter || (c - startChapter) % 5 == 0 || c == epubChapterCount - 1) {
      char stepMsg[32];
      snprintf(stepMsg, sizeof(stepMsg), "ch %d/%d", c + 1, epubChapterCount);
      showLoadStep(stepMsg);
    } else {
      // Still feed watchdog and yield on non-display iterations
      esp_task_wdt_reset();
      yield();
    }

    Serial.printf("  Chapter %d/%d: %s (%u bytes comp=%u, free PSRAM: %u)\n",
                  c + 1, epubChapterCount, entry.filename.c_str(),
                  entry.uncompSize, entry.compSize, ESP.getFreePsram());

    // Check if we have enough buffer space (rough check)
    if (epubFullTextLen + 1024 >= bufferSize) {
      Serial.printf("  Buffer full at chapter %d\n", c + 1);
      break;
    }

    // Extract raw HTML
    unsigned long extractStart = millis();
    size_t rawLen = 0;
    uint8_t* rawBuf = zipExtractFile(f, entry, rawLen);
    Serial.printf("  Extract took %lu ms\n", millis() - extractStart);
    if (!rawBuf || rawLen == 0) {
      if (rawBuf) free(rawBuf);
      Serial.printf("  Skipping chapter %d (extraction failed)\n", c + 1);
      // Mark as empty — don't inflate runningOffset for failed extractions
      // (inflating causes subsequent chapters' offsets to drift from actual buffer positions)
      ch.actualTextSize = 0;
      ch.cumulativeOffset = runningOffset;
      continue;
    }

    // Add chapter break marker (forces new page in renderer)
    // Capture beforeLen BEFORE marker so actualTextSize includes the marker byte.
    // This keeps cumulativeOffset in sync with actual buffer positions.
    size_t beforeLen = epubFullTextLen;
    if (epubFullTextLen > 0 && epubFullTextLen < bufferSize - 2) {
      epubFullText[epubFullTextLen++] = EPUB_CHAPTER_BREAK;
    }

    // Strip HTML directly into buffer
    // Use the HTML file's own directory as basePath (not OPF directory)
    // so that relative image paths like "../images/cover.jpg" resolve correctly
    esp_task_wdt_reset();
    String chapterDir = pathDir(entry.filename);
    size_t remaining = bufferSize - epubFullTextLen - 1;
    unsigned long stripStart = millis();
    size_t written = htmlStripDirect((const char*)rawBuf, rawLen,
                                     epubFullText + epubFullTextLen, remaining,
                                     chapterDir);
    Serial.printf("  Strip took %lu ms, wrote %u bytes\n", millis() - stripStart, written);
    free(rawBuf);

    if (written > 0) {
      epubFullTextLen += written;
      ch.actualTextSize = epubFullTextLen - beforeLen;
      chaptersLoaded++;
    } else {
      // Strip failed — roll back marker byte if it was written
      epubFullTextLen = beforeLen;
      ch.actualTextSize = 0;
    }

    // Update cumulative offset with actual size
    ch.cumulativeOffset = runningOffset;
    runningOffset += ch.actualTextSize;

    epubLoadedEndChapter = c + 1;

    // Check buffer nearly full
    if (epubFullTextLen >= bufferSize - 4096) {
      Serial.printf("  Buffer nearly full at chapter %d\n", c + 1);
      break;
    }

    yield();
    esp_task_wdt_reset();
  }

  {
    size_t nextOffset = epubLoadedBaseOffset;
    for (int j = startChapter; j < epubChapterCount; j++) {
      epubChapters[j].cumulativeOffset = nextOffset;
      size_t chapterSize = (j < epubLoadedEndChapter) ?
                           epubChapters[j].actualTextSize :
                           epubChapters[j].estimatedTextSize;
      nextOffset += chapterSize;
    }
    epubEstimatedTotalBytes = nextOffset;
  }

  f.close();
  epubFullText[epubFullTextLen] = '\0';

  // Remove whitespace (space, tab, newline, U+3000) within paired CJK punctuation.
  // Fixes artifacts from HTML line-wrapping inside （）「」『』《》【】"" etc.
  // Safety: resets tracking if outermost pair spans > 300 bytes or at chapter breaks.
  {
    const size_t MAX_SPAN = 300;
    const int MAX_DEPTH = 8;
    struct { uint32_t close; size_t pos; } stack[MAX_DEPTH];
    int depth = 0;
    size_t w = 0;
    int r = 0;
    int textLen = (int)epubFullTextLen;

    while (r < textLen) {
      int byteStart = r;
      uint32_t cp = utf8Decode(epubFullText, r);  // advances r
      int cpLen = r - byteStart;

      // Reset at chapter breaks
      if (cp == (uint32_t)EPUB_CHAPTER_BREAK) {
        depth = 0;
      }

      // Check for opening paired punctuation
      uint32_t closeCp = 0;
      switch (cp) {
        case 0xFF08: closeCp = 0xFF09; break;  // （）
        case 0x300C: closeCp = 0x300D; break;  // 「」
        case 0x300E: closeCp = 0x300F; break;  // 『』
        case 0x300A: closeCp = 0x300B; break;  // 《》
        case 0x3010: closeCp = 0x3011; break;  // 【】
        case 0x201C: closeCp = 0x201D; break;  // ""
        case 0x2018: closeCp = 0x2019; break;  // ''
      }
      if (closeCp && depth < MAX_DEPTH) {
        stack[depth++] = { closeCp, w };
      }

      // Check for closing paired punctuation (match innermost)
      if (!closeCp && depth > 0 && cp == stack[depth - 1].close) {
        depth--;
      }

      // Safety: expire if outermost pair spans too far
      if (depth > 0 && (w - stack[0].pos) > MAX_SPAN) {
        depth = 0;
      }

      // Skip whitespace when inside paired punctuation (but not the opening mark itself)
      if (depth > 0 && !closeCp) {
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x3000) {
          continue;
        }
      }

      // Copy character
      if ((size_t)byteStart != w) {
        memmove(epubFullText + w, epubFullText + byteStart, cpLen);
      }
      w += cpLen;
    }

    if (w < epubFullTextLen) {
      Serial.printf("EPUB: Removed %u bytes of whitespace in paired punctuation\n",
                    (unsigned)(epubFullTextLen - w));
      epubFullTextLen = w;
      epubFullText[w] = '\0';
    }
  }

  // Shrink text buffer to actual size to free wasted PSRAM.
  // epubLoadChapterRange allocates up to 4MB but may only use a fraction
  // (e.g., 470KB for The Economist). Freeing the excess allows image
  // decoding, font loading, and other PSRAM operations to succeed.
  if (epubFullTextLen + 1 < bufferSize) {
    char* shrunk = (char*)ps_realloc(epubFullText, epubFullTextLen + 1);
    if (shrunk) {
      epubFullText = shrunk;
      Serial.printf("EPUB: Shrunk text buffer from %u to %u bytes, freed %u\n",
                    bufferSize, epubFullTextLen + 1, bufferSize - epubFullTextLen - 1);
    }
  }

  Serial.printf("EPUB: Loaded chapters %d-%d, %u bytes of text\n",
                epubLoadedStartChapter + 1, epubLoadedEndChapter,
                epubFullTextLen);
  Serial.printf("  Total estimated: %u bytes across %d chapters\n",
                epubEstimatedTotalBytes, epubChapterCount);
  Serial.printf("  Free PSRAM: %u bytes\n", ESP.getFreePsram());

  return (chaptersLoaded > 0 && epubFullTextLen > 0);
}

// Load a single chapter for image-based (manga) EPUBs.
// Extracts the chapter HTML, strips to text/image markers, stores in epubFullText.
// Returns true on success.
bool epubLoadSingleChapter(int chapterIndex) {
  if (!epubChapters || chapterIndex < 0 || chapterIndex >= epubChapterCount) return false;
  if (epubFilePath.isEmpty() || !epubZipEntries) return false;

  // Free existing buffer
  if (epubFullText) {
    free(epubFullText);
    epubFullText = nullptr;
    epubFullTextLen = 0;
  }

  EpubChapterInfo& ch = epubChapters[chapterIndex];
  ZipEntry& entry = epubZipEntries[ch.zipEntryIndex];

  Serial.printf("EPUB IMG: Loading chapter %d/%d: %s\n",
                chapterIndex + 1, epubChapterCount, entry.filename.c_str());

  // Allocate a small buffer for the stripped text (just image marker + minimal text)
  size_t bufferSize = max((size_t)4096, entry.uncompSize + 256);
  epubFullText = (char*)ps_malloc(bufferSize);
  if (!epubFullText) {
    Serial.println("EPUB IMG: Failed to allocate chapter buffer");
    return false;
  }

  // Open file and extract HTML
  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubFilePath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubFilePath.c_str()); }
  if (!f) {
    free(epubFullText); epubFullText = nullptr;
    return false;
  }

  size_t rawLen = 0;
  uint8_t* rawBuf = zipExtractFile(f, entry, rawLen);
  f.close();

  if (!rawBuf || rawLen == 0) {
    if (rawBuf) free(rawBuf);
    free(epubFullText); epubFullText = nullptr;
    return false;
  }

  // Strip HTML to text/image markers
  String chapterDir = pathDir(entry.filename);
  epubFullTextLen = htmlStripDirect((const char*)rawBuf, rawLen,
                                    epubFullText, bufferSize - 1, chapterDir);
  free(rawBuf);

  epubFullText[epubFullTextLen] = '\0';
  epubLoadedStartChapter = chapterIndex;
  epubLoadedEndChapter = chapterIndex + 1;

  Serial.printf("EPUB IMG: Chapter %d loaded, %u bytes of content\n",
                chapterIndex + 1, epubFullTextLen);
  return (epubFullTextLen > 0);
}

// Find the chapter index that contains a given virtual text offset
int epubChapterForOffset(size_t offset) {
  if (!epubChapters || epubChapterCount == 0) return 0;
  for (int i = epubChapterCount - 1; i >= 0; i--) {
    if (offset >= epubChapters[i].cumulativeOffset) return i;
  }
  return 0;
}

// Free TOC entries
void epubFreeToc() {
  if (epubTocEntries) {
    for (int i = 0; i < epubTocCount; i++) epubTocEntries[i].~TocEntry();
    free(epubTocEntries);
    epubTocEntries = nullptr;
  }
  epubTocCount = 0;
  tocListPage = 0;
}

// Extract chapter heading label from text at the given position.
// headingStart: byte offset of 第 in text
// numPos: byte offset just past the terminator (回/章/節/篇/卷)
// Returns the cleaned label string (e.g., "第三回 託孤寄命").
static String extractChapterLabel(const char* text, int textLen, int headingStart, int numPos) {
    int labelEnd = numPos;

    // If rest of line after terminator is only whitespace, merge next line
    {
      int peekPos = labelEnd;
      bool restIsEmpty = true;
      while (peekPos < textLen) {
        unsigned char pc = (unsigned char)text[peekPos];
        if (pc == '\n' || pc == '\r' || pc == EPUB_CHAPTER_BREAK) break;
        int tmpPos = peekPos;
        uint32_t peekCp = utf8Decode(text, tmpPos);
        if (peekCp != ' ' && peekCp != '\t' && peekCp != 0x3000) { restIsEmpty = false; break; }
        peekPos = tmpPos;
      }
      if (restIsEmpty && peekPos < textLen) {
        unsigned char nc = (unsigned char)text[peekPos];
        if (nc == '\r') peekPos++;
        if (peekPos < textLen && (unsigned char)text[peekPos] == '\n') peekPos++;
        if (peekPos < textLen && (unsigned char)text[peekPos] == EPUB_CHAPTER_BREAK) peekPos++;
        if (peekPos < textLen) labelEnd = peekPos;
      }
    }

    // Skip colon right after terminator (e.g., 第一回：title → 第一回 title)
    if (labelEnd < textLen) {
      int peekColon = labelEnd;
      uint32_t colonCp = utf8Decode(text, peekColon);
      if (colonCp == ':' || colonCp == 0xFF1A) labelEnd = peekColon;  // skip ： or :
    }

    // Scan line: title ends at line ending, or at last space before
    // first punctuation (or at punctuation itself if no space precedes it)
    int charCount = 0;
    int firstPunctByte = -1, lastSpaceBeforePunct = -1;
    while (labelEnd < textLen && charCount < 60) {
      unsigned char c = (unsigned char)text[labelEnd];
      if (c == '\n' || c == '\r' || c == EPUB_CHAPTER_BREAK) break;
      int cpStart = labelEnd;
      uint32_t lineCp = utf8Decode(text, labelEnd);
      if (isPunctuation(lineCp)) {
        if (firstPunctByte < 0) firstPunctByte = cpStart;
      }
      if (firstPunctByte < 0 && (lineCp == ' ' || lineCp == 0x3000))
        lastSpaceBeforePunct = cpStart;
      charCount++;
    }

    int titleEnd;
    if (firstPunctByte < 0) titleEnd = labelEnd;
    else if (lastSpaceBeforePunct > numPos) titleEnd = lastSpaceBeforePunct;
    else titleEnd = firstPunctByte;

    String label = String(text + headingStart, titleEnd - headingStart);
    label.replace("\r\n", " ");
    label.replace("\r", " ");
    label.replace("\n", " ");
    { char chBreak[2] = {EPUB_CHAPTER_BREAK, 0}; label.replace(chBreak, " "); }
    // Replace fullwidth spaces with ASCII space, then collapse runs of 2+ spaces
    label.replace("\xe3\x80\x80", " ");  // U+3000 → space
    label.replace("\xef\xbc\x9a", " ");  // U+FF1A fullwidth colon → space
    label.replace(":", " ");             // ASCII colon → space
    while (label.indexOf("  ") >= 0) label.replace("  ", " ");
    label.trim();

    // Insert space after terminator (or after number for 卷X pattern) if none exists
    {
      int lp = 0, lLen = label.length();
      while (lp < lLen) {
        uint32_t lCp = utf8Decode(label, lp);
        if (lCp == 0x56DE || lCp == 0x7AE0 || lCp == 0x7BC0 || lCp == 0x7BC7 || lCp == 0x5377) {
          // For 卷X pattern (卷 followed by number): skip past the number
          if (lCp == 0x5377 && lp < lLen) {
            int peekNum = lp;
            uint32_t nextCp = utf8Decode(label, peekNum);
            bool isNum = (nextCp==0x4E00||nextCp==0x4E8C||nextCp==0x4E09||nextCp==0x56DB||nextCp==0x4E94||
                          nextCp==0x516D||nextCp==0x4E03||nextCp==0x516B||nextCp==0x4E5D||nextCp==0x5341||
                          nextCp==0x767E||nextCp==0x5343||nextCp==0x96F6||nextCp==0x3007||nextCp==0x25CB||
                          (nextCp>='0'&&nextCp<='9')||(nextCp>=0xFF10&&nextCp<=0xFF19));
            if (isNum) {
              // Skip all number characters
              while (lp < lLen) {
                int bk = lp;
                uint32_t nc = utf8Decode(label, lp);
                bool isCJKNum = (nc==0x4E00||nc==0x4E8C||nc==0x4E09||nc==0x56DB||nc==0x4E94||
                                 nc==0x516D||nc==0x4E03||nc==0x516B||nc==0x4E5D||nc==0x5341||
                                 nc==0x767E||nc==0x5343||nc==0x96F6||nc==0x3007||nc==0x25CB);
                bool isDig = (nc>='0'&&nc<='9')||(nc>=0xFF10&&nc<=0xFF19);
                if (!isCJKNum && !isDig) { lp = bk; break; }
              }
            }
          }
          if (lp < lLen) {
            int pp = lp;
            uint32_t nc = utf8Decode(label, pp);
            if (nc != ' ' && nc != 0x3000) label = label.substring(0, lp) + " " + label.substring(lp);
          }
          break;
        }
      }
    }
    return label;
}

// Parse the CJK/Arabic number from a label like "第二十八篇..." → 28
// Also handles reverse pattern "卷二十八..." → 28
static int parseTocCjkNumber(const String& label) {
    int pos = label.indexOf("\xe7\xac\xac");  // 第
    if (pos >= 0) {
      pos += 3;
    } else {
      // Check for 卷X pattern: 卷 at start followed by number
      if (label.startsWith("\xe5\x8d\xb7")) {  // 卷
        pos = 3;
      } else if (label == "\xe5\xba\x8f") {  // 序 → sort before all chapters
        return 0;
      } else {
        return 99999;
      }
    }
    int section = 0;
    int currentDigit = 0;
    int positionalValue = 0;
    int digitCount = 0;
    bool hasUnit = false;
    bool found = false;
    bool positional = false;  // set once we see a positional zero (〇/○)
    while (pos < (int)label.length()) {
      uint32_t cp = utf8Decode(label, pos);
      int digit = -1;
      int unit = 0;
      switch (cp) {
        case 0x96F6: case 0x3007: case 0x25CB: digit = 0; positional = true; break;
        case 0x4E00: digit = 1; break;
        case 0x4E8C: digit = 2; break;
        case 0x4E09: digit = 3; break;
        case 0x56DB: digit = 4; break;
        case 0x4E94: digit = 5; break;
        case 0x516D: digit = 6; break;
        case 0x4E03: digit = 7; break;
        case 0x516B: digit = 8; break;
        case 0x4E5D: digit = 9; break;
        case 0x5341: unit = 10; break;
        case 0x767E: unit = 100; break;
        case 0x5343: unit = 1000; break;
        default:
          if (cp >= '0' && cp <= '9') { digit = cp - '0'; break; }
          if (cp >= 0xFF10 && cp <= 0xFF19) { digit = cp - 0xFF10; break; }
          goto done_num;
      }
      if (digit >= 0) {
        found = true;
        currentDigit = digit;
        positionalValue = positionalValue * 10 + digit;
        digitCount++;
      } else if (unit > 0) {
        found = true;
        hasUnit = true;
        section += (currentDigit > 0 ? currentDigit : 1) * unit;
        currentDigit = 0;
      }
    }
    done_num:
    if (!found) return 99999;
    if (positional || (!hasUnit && digitCount > 1)) return positionalValue;
    return section + currentDigit;
}

// Generate virtual TOC by scanning epubFullText for 第X回/章/節/篇/卷 patterns.
// Called as fallback when no .ncx TOC exists. Requires epubFullText to be loaded.
// Also collects heading text after the keyword (e.g., "第三回 託孤寄命").
bool epubGenerateVirtualToc() {
  if (!epubFullText || epubFullTextLen == 0) return false;
  if (!epubChapters || epubChapterCount == 0) return false;

  epubFreeToc();

  // Allocate TOC entries
  epubTocEntries = (TocEntry*)ps_malloc(sizeof(TocEntry) * MAX_TOC_ENTRIES);
  if (!epubTocEntries) return false;
  for (int e = 0; e < MAX_TOC_ENTRIES; e++) new (&epubTocEntries[e]) TocEntry();

  // Scan epubFullText for 第X回/章/節/篇/卷 patterns
  int pos = 0;
  while (pos < (int)epubFullTextLen && epubTocCount < MAX_TOC_ENTRIES) {
    uint32_t cp = utf8Decode(epubFullText, pos);
    if (cp != 0x7B2C) continue;  // Not 第

    int headingStart = pos - 3;  // Back up to include 第 (3 bytes in UTF-8)
    if (headingStart < 0) headingStart = 0;

    // 第 must be at the start of a line to be a chapter heading.
    // Skip inline references like "參看本書第三篇" where 第 appears mid-line.
    // Also skip 第 inside paired punctuation like （第一回）.
    bool atTrueLineStart = (headingStart == 0);  // start of text = true line start
    if (headingStart > 0) {
      bool atLineStart = false;
      int scanBack = headingStart - 1;
      while (scanBack >= 0) {
        unsigned char sb = (unsigned char)epubFullText[scanBack];
        if (sb == '\n' || sb == '\r' || sb == EPUB_CHAPTER_BREAK) {
          atLineStart = true;
          atTrueLineStart = true;
          break;
        }
        // U+3000 (E3 80 80) and ASCII space/tab are whitespace — keep scanning
        if (sb == ' ' || sb == '\t') { scanBack--; continue; }
        // Check for trailing byte of U+3000 (E3 80 80): last byte is 0x80
        if (sb == 0x80 && scanBack >= 2 &&
            (unsigned char)epubFullText[scanBack-1] == 0x80 &&
            (unsigned char)epubFullText[scanBack-2] == 0xE3) {
          scanBack -= 3;
          continue;
        }
        break;  // Non-whitespace character found → 第 is mid-line
      }
      if (scanBack < 0) { atLineStart = true; atTrueLineStart = true; }

      // If not at line start, check if preceded by sentence-ending punctuation
      // (。！？) + optional whitespace.  Many older Chinese EPUBs place chapter
      // headings inside the same <p> as the previous chapter's last sentence,
      // so the HTML newline is collapsed to a space.  The pattern
      // "…且聽下回分解。 第四回 盼鄉榜…" is a valid heading.
      if (!atLineStart && headingStart > 0) {
        // scanBack stopped at a non-whitespace byte — decode that character
        int charStart = scanBack;
        while (charStart > 0 && ((unsigned char)epubFullText[charStart] & 0xC0) == 0x80) charStart--;
        int tmp = charStart;
        uint32_t prevCp = utf8Decode(epubFullText, tmp);
        if (prevCp == 0x3002 ||   // 。
            prevCp == 0xFF01 ||   // ！
            prevCp == 0xFF1F ||   // ？
            prevCp == 0x2014) {   // — (em-dash separator line)
          atLineStart = true;  // treat as valid heading position
        }
      }

      // If not at line start, check if the complete 第X[terminator] pattern
      // ends the line (only whitespace until newline/EOF/chapter-break).
      // This catches headings embedded at the end of a paragraph, e.g.:
      // "...撰於萬卷樓 第一卷\n" where 第一卷 is in the same <p> as preface text.
      if (!atLineStart && headingStart > 0) {
        // Peek forward: skip 第 + number + terminator to see if line ends
        int peekEnd = pos;  // pos is after 第
        while (peekEnd < (int)epubFullTextLen) {
          int bk = peekEnd;
          uint32_t fc = utf8Decode(epubFullText, peekEnd);
          bool isCJKNum = (fc==0x4E00||fc==0x4E8C||fc==0x4E09||fc==0x56DB||fc==0x4E94||
                           fc==0x516D||fc==0x4E03||fc==0x516B||fc==0x4E5D||fc==0x5341||
                           fc==0x767E||fc==0x5343||fc==0x96F6||fc==0x3007||fc==0x25CB);
          bool isDigit = (fc>='0'&&fc<='9')||(fc>=0xFF10&&fc<=0xFF19);
          if (!isCJKNum && !isDigit) { peekEnd = bk; break; }
        }
        // Check for terminator
        if (peekEnd < (int)epubFullTextLen) {
          int bk = peekEnd;
          uint32_t tc = utf8Decode(epubFullText, peekEnd);
          if (tc==0x56DE||tc==0x7AE0||tc==0x7BC0||tc==0x7BC7||tc==0x5377) {
            // Now check if only whitespace follows until newline/EOF/chapter-break
            bool lineEnds = true;
            while (peekEnd < (int)epubFullTextLen) {
              unsigned char pe = (unsigned char)epubFullText[peekEnd];
              if (pe == '\n' || pe == '\r' || pe == EPUB_CHAPTER_BREAK) break;
              if (pe == ' ' || pe == '\t') { peekEnd++; continue; }
              // Check for U+3000 (E3 80 80)
              if (pe == 0xE3 && peekEnd + 2 < (int)epubFullTextLen &&
                  (unsigned char)epubFullText[peekEnd+1] == 0x80 &&
                  (unsigned char)epubFullText[peekEnd+2] == 0x80) {
                peekEnd += 3; continue;
              }
              lineEnds = false; break;
            }
            if (lineEnds) {
              atLineStart = true;
              atTrueLineStart = true;
            }
          } else {
            peekEnd = bk;  // rewind — no terminator
          }
        }
      }

      if (!atLineStart) continue;

      // If a newline was found, check the last character on the previous line.
      // If it's mid-sentence punctuation (，,；;：:), this is just a wrapped line,
      // not a real line start — skip it.
      if (atLineStart && scanBack > 0) {
        int prevCharPos = scanBack - 1;
        // Skip whitespace and additional newlines to find last char of previous line
        while (prevCharPos >= 0) {
          unsigned char pb = (unsigned char)epubFullText[prevCharPos];
          if (pb == '\n' || pb == '\r' || pb == ' ' || pb == '\t') { prevCharPos--; continue; }
          break;
        }
        if (prevCharPos >= 0) {
          // Decode the character at prevCharPos (find start of UTF-8 sequence)
          int charStart = prevCharPos;
          while (charStart > 0 && ((unsigned char)epubFullText[charStart] & 0xC0) == 0x80) charStart--;
          int tmp = charStart;
          uint32_t prevCp = utf8Decode(epubFullText, tmp);
          // Mid-sentence punctuation: ， , ； ;
          if (prevCp == 0xFF0C || prevCp == ',' ||   // ，,
              prevCp == 0xFF1B || prevCp == ';') {    // ；;
            continue;  // Previous line ends with mid-sentence punct → not a chapter start
          }
        }
      }

      // Check if 第 is inside paired punctuation (e.g., "（戚蓼生所序八十回本之\n第一回）").
      // Scan forward from headingStart after the terminator to see if there's a
      // closing paired punctuation before any CJK content. Then scan backwards
      // from headingStart across at most one newline to find the matching opening.
      // This catches cases like （第一回） split across lines.
      {
        // First, peek forward past the number+terminator to see if next non-space is closing punct
        int peekFwd = pos;  // pos is already past 第
        // Skip number
        while (peekFwd < (int)epubFullTextLen) {
          int bk = peekFwd;
          uint32_t fc = utf8Decode(epubFullText, peekFwd);
          bool isCJKNum = (fc==0x4E00||fc==0x4E8C||fc==0x4E09||fc==0x56DB||fc==0x4E94||
                           fc==0x516D||fc==0x4E03||fc==0x516B||fc==0x4E5D||fc==0x5341||
                           fc==0x767E||fc==0x5343||fc==0x96F6||fc==0x3007||fc==0x25CB);
          bool isDigit = (fc>='0'&&fc<='9')||(fc>=0xFF10&&fc<=0xFF19);
          if (!isCJKNum && !isDigit) { peekFwd = bk; break; }
        }
        // Skip terminator
        if (peekFwd < (int)epubFullTextLen) {
          int bk = peekFwd;
          uint32_t tc = utf8Decode(epubFullText, peekFwd);
          if (tc!=0x56DE&&tc!=0x7AE0&&tc!=0x7BC0&&tc!=0x7BC7&&tc!=0x5377) peekFwd = bk;
        }
        // Check if next char is closing punctuation
        bool hasClosingAfter = false;
        if (peekFwd < (int)epubFullTextLen) {
          int bk = peekFwd;
          uint32_t nc = utf8Decode(epubFullText, peekFwd);
          hasClosingAfter = isClosingPunctuation(nc);
          peekFwd = bk;
        }

        if (hasClosingAfter) {
          // Scan backwards from headingStart across at most one newline to find matching opening
          int pScan = headingStart;
          int newlinesSeen = 0;
          int pLimit = (headingStart > 200) ? headingStart - 200 : 0;
          bool foundOpening = false;
          while (pScan > pLimit) {
            int prev = pScan - 1;
            while (prev > pLimit && ((unsigned char)epubFullText[prev] & 0xC0) == 0x80) prev--;
            int tmp = prev;
            uint32_t pc = utf8Decode(epubFullText, tmp);
            if (pc == EPUB_CHAPTER_BREAK) break;
            if (pc == '\n' || pc == '\r') {
              newlinesSeen++;
              if (newlinesSeen > 1) break;  // Only check one line back
            }
            if (isOpeningPunctuation(pc)) { foundOpening = true; break; }
            pScan = prev;
          }
          if (foundOpening) continue;  // 第 is inside paired punctuation, skip
        }
      }
    }

    // Scan number part: CJK numerals or Arabic digits
    int numPos = pos;
    bool hasNumber = false;
    uint32_t terminator = 0;
    while (numPos < (int)epubFullTextLen) {
      int beforeDecode = numPos;
      uint32_t numCp = utf8Decode(epubFullText, numPos);
      bool isCJKNum = (numCp == 0x4E00 || numCp == 0x4E8C || numCp == 0x4E09 || numCp == 0x56DB ||
                       numCp == 0x4E94 || numCp == 0x516D || numCp == 0x4E03 || numCp == 0x516B ||
                       numCp == 0x4E5D || numCp == 0x5341 || numCp == 0x767E || numCp == 0x5343 ||
                       numCp == 0x96F6 || numCp == 0x3007 || numCp == 0x25CB);
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
        terminator = numCp;
        break;
      } else if (hasNumber && (numCp == ' ' || numCp == '\t' || numCp == 0x3000)) {
        // Allow "第X<space>回" (e.g. 第六十七 回) — peek past any whitespace
        // to see if a terminator follows.
        int peekAfter = numPos;
        while (peekAfter < (int)epubFullTextLen) {
          int bk2 = peekAfter;
          uint32_t wc = utf8Decode(epubFullText, peekAfter);
          if (wc != ' ' && wc != '\t' && wc != 0x3000) {
            if (wc == 0x56DE || wc == 0x7AE0 || wc == 0x7BC0 || wc == 0x7BC7 || wc == 0x5377) {
              terminator = wc;
              numPos = peekAfter;  // advance past the terminator
            } else {
              peekAfter = bk2;  // rewind to non-space
            }
            break;
          }
        }
        if (terminator != 0) break;
        numPos = beforeDecode;  // no terminator after spaces → not a heading
        break;
      } else {
        numPos = beforeDecode;  // Not a match, rewind
        break;
      }
    }

    if (!hasNumber || terminator == 0) continue;  // Not a valid chapter heading

    // After the terminator, require whitespace, newline, or end-of-text.
    // Skip cases like 第九卷《... where text continues immediately.
    // Exception: if 第 was at a true line start, allow CJK text to follow
    // directly (e.g., "第一卷轉運漢遇巧洞庭紅" — common in classical novels).
    if (!atTrueLineStart && numPos < (int)epubFullTextLen) {
      int peekAfter = numPos;
      uint32_t afterCp = utf8Decode(epubFullText, peekAfter);
      if (afterCp != ' ' && afterCp != '\t' && afterCp != 0x3000 &&
          afterCp != '\n' && afterCp != '\r' && afterCp != EPUB_CHAPTER_BREAK) {
        continue;  // Not a heading — text continues immediately after terminator
      }
    }

    // Extract heading label
    String label = extractChapterLabel(epubFullText, (int)epubFullTextLen, headingStart, numPos);

    // Map byte position (in epubFullText buffer) to absolute offset
    size_t absOffset = epubLoadedBaseOffset + headingStart;

    // Find which chapter this belongs to
    int chapterIdx = -1;
    for (int c = epubChapterCount - 1; c >= 0; c--) {
      if (absOffset >= epubChapters[c].cumulativeOffset) {
        chapterIdx = c;
        break;
      }
    }

    if (chapterIdx >= 0 && label.length() > 0) {
      // Avoid duplicate entries for the same chapter
      bool duplicate = false;
      for (int d = 0; d < epubTocCount; d++) {
        if (epubTocEntries[d].label == label) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        epubTocEntries[epubTocCount].label = label;
        epubTocEntries[epubTocCount].chapterIndex = chapterIdx;
        epubTocEntries[epubTocCount].byteOffset = absOffset;
        epubTocCount++;
        Serial.printf("VirtualTOC[%d]: \"%s\" → ch %d\n", epubTocCount - 1, label.c_str(), chapterIdx);
      }
    }

    yield();
  }

  // === Second scan: 卷X pattern (terminator before number) ===
  // Some books use 卷一, 卷二, ... instead of 第一卷, 第二卷, ...
  // Only for 卷 — other terminators (回/章/節/篇) don't use this reverse pattern.
  pos = 0;
  while (pos < (int)epubFullTextLen && epubTocCount < MAX_TOC_ENTRIES) {
    uint32_t cp = utf8Decode(epubFullText, pos);
    if (cp != 0x5377) continue;  // Not 卷

    int headingStart = pos - 3;  // Back up to include 卷 (3 bytes in UTF-8)
    if (headingStart < 0) headingStart = 0;

    // Check that 卷 is followed by a CJK/Arabic number
    int numPos = pos;
    bool hasNumber = false;
    while (numPos < (int)epubFullTextLen) {
      int bk = numPos;
      uint32_t nc = utf8Decode(epubFullText, numPos);
      bool isCJKNum = (nc==0x4E00||nc==0x4E8C||nc==0x4E09||nc==0x56DB||nc==0x4E94||
                       nc==0x516D||nc==0x4E03||nc==0x516B||nc==0x4E5D||nc==0x5341||
                       nc==0x767E||nc==0x5343||nc==0x96F6||nc==0x3007||nc==0x25CB);
      bool isDigit = (nc>='0'&&nc<='9')||(nc>=0xFF10&&nc<=0xFF19);
      if (isCJKNum || isDigit) { hasNumber = true; }
      else { numPos = bk; break; }
    }
    if (!hasNumber) continue;

    // After the number, require whitespace, newline, or end-of-text
    if (numPos < (int)epubFullTextLen) {
      int peekAfter = numPos;
      uint32_t afterCp = utf8Decode(epubFullText, peekAfter);
      if (afterCp != ' ' && afterCp != '\t' && afterCp != 0x3000 &&
          afterCp != '\n' && afterCp != '\r' && afterCp != EPUB_CHAPTER_BREAK) {
        continue;  // Text continues immediately (e.g. 卷樓) — not a heading
      }
    }

    // Line-start check (same rules as 第 pattern)
    bool atLineStart = (headingStart == 0);
    if (headingStart > 0) {
      int scanBack = headingStart - 1;
      while (scanBack >= 0) {
        unsigned char sb = (unsigned char)epubFullText[scanBack];
        if (sb == '\n' || sb == '\r' || sb == EPUB_CHAPTER_BREAK) { atLineStart = true; break; }
        if (sb == ' ' || sb == '\t') { scanBack--; continue; }
        if (sb == 0x80 && scanBack >= 2 &&
            (unsigned char)epubFullText[scanBack-1] == 0x80 &&
            (unsigned char)epubFullText[scanBack-2] == 0xE3) { scanBack -= 3; continue; }
        break;
      }
      if (scanBack < 0) atLineStart = true;

      // Check if preceded by sentence-ending punctuation
      if (!atLineStart) {
        int charStart = scanBack;
        while (charStart > 0 && ((unsigned char)epubFullText[charStart] & 0xC0) == 0x80) charStart--;
        int tmp = charStart;
        uint32_t prevCp = utf8Decode(epubFullText, tmp);
        if (prevCp == 0x3002 || prevCp == 0xFF01 || prevCp == 0xFF1F || prevCp == 0x2014)
          atLineStart = true;
      }

      if (!atLineStart) continue;

      // Mid-sentence punctuation rejection on previous line
      if (scanBack > 0) {
        int prevCharPos = scanBack - 1;
        while (prevCharPos >= 0) {
          unsigned char pb = (unsigned char)epubFullText[prevCharPos];
          if (pb == '\n' || pb == '\r' || pb == ' ' || pb == '\t') { prevCharPos--; continue; }
          break;
        }
        if (prevCharPos >= 0) {
          int charStart = prevCharPos;
          while (charStart > 0 && ((unsigned char)epubFullText[charStart] & 0xC0) == 0x80) charStart--;
          int tmp = charStart;
          uint32_t prevCp = utf8Decode(epubFullText, tmp);
          if (prevCp == 0xFF0C || prevCp == ',' || prevCp == 0xFF1B || prevCp == ';')
            continue;
        }
      }
    }

    // Extract heading label
    String label = extractChapterLabel(epubFullText, (int)epubFullTextLen, headingStart, numPos);

    size_t absOffset = epubLoadedBaseOffset + headingStart;
    int chapterIdx = -1;
    for (int c = epubChapterCount - 1; c >= 0; c--) {
      if (absOffset >= epubChapters[c].cumulativeOffset) { chapterIdx = c; break; }
    }

    if (chapterIdx >= 0 && label.length() > 0) {
      bool duplicate = false;
      for (int d = 0; d < epubTocCount; d++) {
        if (epubTocEntries[d].label == label) { duplicate = true; break; }
      }
      if (!duplicate) {
        epubTocEntries[epubTocCount].label = label;
        epubTocEntries[epubTocCount].chapterIndex = chapterIdx;
        epubTocEntries[epubTocCount].byteOffset = absOffset;
        epubTocCount++;
        Serial.printf("VirtualTOC-卷[%d]: \"%s\" → ch %d\n", epubTocCount - 1, label.c_str(), chapterIdx);
      }
    }
    yield();
  }

  // === Third scan: standalone 序 (preface) heading ===
  // Matches 序 on its own line (preceded and followed by newline/start/end).
  // Only add if numbered chapters (第X回 etc.) were found, so we know this is a structured book.
  if (epubTocCount > 0) {
    pos = 0;
    while (pos < (int)epubFullTextLen && epubTocCount < MAX_TOC_ENTRIES) {
      uint32_t cp = utf8Decode(epubFullText, pos);
      if (cp != 0x5E8F) continue;  // Not 序

      int headingStart = pos - 3;  // 序 is 3 bytes in UTF-8 (E5 BA 8F)
      if (headingStart < 0) headingStart = 0;

      // 序 must be preceded by newline/start-of-text (after optional whitespace)
      bool atLineStart = (headingStart == 0);
      if (headingStart > 0) {
        int scanBack = headingStart - 1;
        while (scanBack >= 0) {
          unsigned char sb = (unsigned char)epubFullText[scanBack];
          if (sb == '\n' || sb == '\r' || sb == EPUB_CHAPTER_BREAK) { atLineStart = true; break; }
          if (sb == ' ' || sb == '\t') { scanBack--; continue; }
          if (sb == 0x80 && scanBack >= 2 &&
              (unsigned char)epubFullText[scanBack-1] == 0x80 &&
              (unsigned char)epubFullText[scanBack-2] == 0xE3) { scanBack -= 3; continue; }
          break;
        }
        if (scanBack < 0) atLineStart = true;
      }
      if (!atLineStart) continue;

      // 序 must be followed by newline/end-of-text (after optional whitespace)
      int afterPos = pos;
      bool lineEnds = false;
      while (afterPos < (int)epubFullTextLen) {
        unsigned char ab = (unsigned char)epubFullText[afterPos];
        if (ab == '\n' || ab == '\r' || ab == EPUB_CHAPTER_BREAK) { lineEnds = true; break; }
        if (ab == ' ' || ab == '\t') { afterPos++; continue; }
        if (ab == 0xE3 && afterPos + 2 < (int)epubFullTextLen &&
            (unsigned char)epubFullText[afterPos+1] == 0x80 &&
            (unsigned char)epubFullText[afterPos+2] == 0x80) { afterPos += 3; continue; }
        break;
      }
      if (afterPos >= (int)epubFullTextLen) lineEnds = true;
      if (!lineEnds) continue;

      // Valid standalone 序 heading
      size_t absOffset = epubLoadedBaseOffset + headingStart;
      int chapterIdx = -1;
      for (int c = epubChapterCount - 1; c >= 0; c--) {
        if (absOffset >= epubChapters[c].cumulativeOffset) { chapterIdx = c; break; }
      }
      if (chapterIdx >= 0) {
        bool duplicate = false;
        for (int d = 0; d < epubTocCount; d++) {
          if (epubTocEntries[d].label == "\xe5\xba\x8f") { duplicate = true; break; }  // 序 UTF-8
        }
        if (!duplicate) {
          epubTocEntries[epubTocCount].label = "\xe5\xba\x8f";  // 序
          epubTocEntries[epubTocCount].chapterIndex = chapterIdx;
          epubTocEntries[epubTocCount].byteOffset = absOffset;
          epubTocCount++;
          Serial.printf("VirtualTOC-序[%d]: \"序\" → ch %d\n", epubTocCount - 1, chapterIdx);
          break;  // Only add first standalone 序 (one preface per book)
        }
      }
    }
  }

  Serial.printf("EPUB Virtual TOC: %d entries generated\n", epubTocCount);

  if (epubTocCount == 0) {
    epubFreeToc();
    return false;
  }

  // Sort TOC entries by the Chinese/Arabic numeral after 第
  // Bubble sort (small array, on ESP32, simple is fine)
  for (int i = 0; i < epubTocCount - 1; i++) {
    for (int j = 0; j < epubTocCount - 1 - i; j++) {
      int a = parseTocCjkNumber(epubTocEntries[j].label);
      int b = parseTocCjkNumber(epubTocEntries[j + 1].label);
      if (a > b) {
        String tmpLabel = epubTocEntries[j].label;
        int tmpIdx = epubTocEntries[j].chapterIndex;
        size_t tmpOff = epubTocEntries[j].byteOffset;
        epubTocEntries[j].label = epubTocEntries[j + 1].label;
        epubTocEntries[j].chapterIndex = epubTocEntries[j + 1].chapterIndex;
        epubTocEntries[j].byteOffset = epubTocEntries[j + 1].byteOffset;
        epubTocEntries[j + 1].label = tmpLabel;
        epubTocEntries[j + 1].chapterIndex = tmpIdx;
        epubTocEntries[j + 1].byteOffset = tmpOff;
      }
    }
    yield();
  }

  Serial.printf("EPUB Virtual TOC: sorted %d entries by chapter number\n", epubTocCount);

  // === Gap-filling second pass ===
  // If more than half of the expected sequential entries were found,
  // search for missing numbers with relaxed position rules.
  // Keeps: post-terminator whitespace check (filters inline refs like "本書第三篇。")
  //        paired punctuation check (filters "(第三篇)" references)
  // Skips: line-start check, sentence-ending punct check, ends-line check
  if (epubTocCount >= 3) {
    // Find dominant terminator
    uint32_t termCodes[5] = {0x56DE, 0x7AE0, 0x7BC0, 0x7BC7, 0x5377};
    int termCounts[5] = {0};
    for (int e = 0; e < epubTocCount; e++) {
      int lp = 0;
      while (lp < (int)epubTocEntries[e].label.length()) {
        uint32_t lc = utf8Decode(epubTocEntries[e].label, lp);
        for (int t = 0; t < 5; t++) {
          if (lc == termCodes[t]) { termCounts[t]++; goto gf_next_entry; }
        }
      }
      gf_next_entry:;
    }
    int bestT = 0;
    for (int t = 1; t < 5; t++) if (termCounts[t] > termCounts[bestT]) bestT = t;
    uint32_t domTerm = termCodes[bestT];

    // Parse all found numbers, build gap map.
    // Cap at 1024 chapters — long-form classical novels (e.g. 彭公案 has 341) exceed the
    // previous 200 limit and ended up unable to gap-fill anything past chapter 200.
    const int GAP_FILL_MAX = 1024;
    int maxNum = 0;
    bool foundNums[GAP_FILL_MAX + 1] = {};
    for (int e = 0; e < epubTocCount; e++) {
      int n = parseTocCjkNumber(epubTocEntries[e].label);
      if (n > 0 && n <= GAP_FILL_MAX) { foundNums[n] = true; if (n > maxNum) maxNum = n; }
    }
    int foundCount = 0;
    for (int n = 1; n <= maxNum; n++) if (foundNums[n]) foundCount++;

    if (maxNum > foundCount && foundCount > maxNum / 2) {
      Serial.printf("Gap-fill: %d/%d entries found, searching for %d missing\n",
                     foundCount, maxNum, maxNum - foundCount);

      int pos = 0;
      while (pos < (int)epubFullTextLen && epubTocCount < MAX_TOC_ENTRIES) {
        uint32_t cp = utf8Decode(epubFullText, pos);
        if (cp != 0x7B2C) continue;  // Not 第
        int headingStart = pos - 3;
        if (headingStart < 0) headingStart = 0;

        // Parse number + terminator (must match dominant terminator)
        int numPos = pos;
        bool hasNumber = false;
        uint32_t terminator = 0;
        while (numPos < (int)epubFullTextLen) {
          int bk = numPos;
          uint32_t nc = utf8Decode(epubFullText, numPos);
          bool isCJKNum = (nc==0x4E00||nc==0x4E8C||nc==0x4E09||nc==0x56DB||nc==0x4E94||
                           nc==0x516D||nc==0x4E03||nc==0x516B||nc==0x4E5D||nc==0x5341||
                           nc==0x767E||nc==0x5343||nc==0x96F6||nc==0x3007||nc==0x25CB);
          bool isDigit = (nc>='0'&&nc<='9')||(nc>=0xFF10&&nc<=0xFF19);
          if (isCJKNum || isDigit) { hasNumber = true; }
          else if (hasNumber && (nc==0x56DE||nc==0x7AE0||nc==0x7BC0||nc==0x7BC7||nc==0x5377)) {
            terminator = nc; break;
          } else if (hasNumber && (nc == ' ' || nc == '\t' || nc == 0x3000)) {
            // Allow "第X<space>回" in gap-fill pass too
            int pa = numPos;
            while (pa < (int)epubFullTextLen) {
              int bk2 = pa;
              uint32_t wc = utf8Decode(epubFullText, pa);
              if (wc != ' ' && wc != '\t' && wc != 0x3000) {
                if (wc==0x56DE||wc==0x7AE0||wc==0x7BC0||wc==0x7BC7||wc==0x5377) {
                  terminator = wc; numPos = pa;
                } else { pa = bk2; }
                break;
              }
            }
            if (terminator != 0) break;
            numPos = bk; break;
          } else { numPos = bk; break; }
        }
        if (!hasNumber || terminator != domTerm) continue;

        // Check if this number fills a gap
        String tmpLabel = String(epubFullText + headingStart, numPos - headingStart);
        int numValue = parseTocCjkNumber(tmpLabel);
        if (numValue <= 0 || numValue > GAP_FILL_MAX || foundNums[numValue]) continue;
        if (numValue > maxNum) continue;  // don't add chapters past the observed max

        // Paired punctuation check — reject （第三篇）
        {
          int peekFwd = numPos;
          while (peekFwd < (int)epubFullTextLen) {
            unsigned char pb = (unsigned char)epubFullText[peekFwd];
            if (pb == ' ' || pb == '\t') { peekFwd++; continue; }
            if (pb == 0xE3 && peekFwd+2 < (int)epubFullTextLen &&
                (unsigned char)epubFullText[peekFwd+1] == 0x80 &&
                (unsigned char)epubFullText[peekFwd+2] == 0x80) { peekFwd += 3; continue; }
            break;
          }
          if (peekFwd < (int)epubFullTextLen) {
            int bk = peekFwd;
            uint32_t nc = utf8Decode(epubFullText, peekFwd);
            if (isClosingPunctuation(nc)) continue;
          }
        }

        // Post-terminator whitespace check (always required for gap-filling)
        if (numPos < (int)epubFullTextLen) {
          int peekAfter = numPos;
          uint32_t afterCp = utf8Decode(epubFullText, peekAfter);
          if (afterCp != ' ' && afterCp != '\t' && afterCp != 0x3000 &&
              afterCp != '\n' && afterCp != '\r' && afterCp != EPUB_CHAPTER_BREAK) {
            continue;
          }
        }

        // Extract label and insert
        String label = extractChapterLabel(epubFullText, (int)epubFullTextLen, headingStart, numPos);
        size_t absOffset = epubLoadedBaseOffset + headingStart;
        int chapterIdx = -1;
        for (int c = epubChapterCount - 1; c >= 0; c--) {
          if (absOffset >= epubChapters[c].cumulativeOffset) { chapterIdx = c; break; }
        }
        if (chapterIdx >= 0 && label.length() > 0) {
          bool duplicate = false;
          for (int d = 0; d < epubTocCount; d++) {
            if (epubTocEntries[d].label == label) { duplicate = true; break; }
          }
          if (!duplicate) {
            epubTocEntries[epubTocCount].label = label;
            epubTocEntries[epubTocCount].chapterIndex = chapterIdx;
            epubTocEntries[epubTocCount].byteOffset = absOffset;
            epubTocCount++;
            foundNums[numValue] = true;
            Serial.printf("GapFill[%d]: \"%s\" → ch %d\n", epubTocCount - 1, label.c_str(), chapterIdx);
          }
        }
        yield();
      }

      // Re-sort if new entries were added
      if (epubTocCount > foundCount) {
        for (int i = 0; i < epubTocCount - 1; i++) {
          for (int j = 0; j < epubTocCount - 1 - i; j++) {
            int a = parseTocCjkNumber(epubTocEntries[j].label);
            int b = parseTocCjkNumber(epubTocEntries[j + 1].label);
            if (a > b) {
              String tmpLabel = epubTocEntries[j].label;
              int tmpIdx = epubTocEntries[j].chapterIndex;
              size_t tmpOff = epubTocEntries[j].byteOffset;
              epubTocEntries[j].label = epubTocEntries[j + 1].label;
              epubTocEntries[j].chapterIndex = epubTocEntries[j + 1].chapterIndex;
              epubTocEntries[j].byteOffset = epubTocEntries[j + 1].byteOffset;
              epubTocEntries[j + 1].label = tmpLabel;
              epubTocEntries[j + 1].chapterIndex = tmpIdx;
              epubTocEntries[j + 1].byteOffset = tmpOff;
            }
          }
          yield();
        }
        Serial.printf("Gap-fill: re-sorted %d entries\n", epubTocCount);
      }
    }
  }

  tocPolishLabels();

  return true;
}

// === Label polish (standalone function) ===
// Called after any TOC generation (embedded, virtual EPUB, or virtual TXT).
// 1. If more than half of entries share the same punctuation at the body
//    start (e.g. every label reads "第X回 ：title"), strip that character —
//    it's almost always a typesetting artifact rather than a real delimiter.
// 2. If a body is longer than 10 CJK characters with no internal spaces,
//    insert a space at the midpoint (classical couplet titles are two halves
//    of equal length; this restores the visual break).
void tocPolishLabels() {
  if (!epubTocEntries || epubTocCount < 1) return;

  // Phase 0: Ensure a space exists after the chapter terminator / number.
  // Embedded TOC labels (NCX/nav) don't have this; virtual TOC builders
  // already insert it, but it's harmless to re-check.
  for (int e = 0; e < epubTocCount; e++) {
    String& s = epubTocEntries[e].label;
    int sp = 0;
    uint32_t c0 = s.length() ? utf8Decode(s, sp) : 0;
    if (c0 == 0x7B2C) {  // 第
      // Skip number chars
      bool hasNum = false;
      while (sp < (int)s.length()) {
        int bk = sp;
        uint32_t c = utf8Decode(s, sp);
        bool isCJKNum = (c==0x4E00||c==0x4E8C||c==0x4E09||c==0x56DB||c==0x4E94||
                         c==0x516D||c==0x4E03||c==0x516B||c==0x4E5D||c==0x5341||
                         c==0x767E||c==0x5343||c==0x96F6||c==0x3007||c==0x25CB);
        bool isDig = (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
        if (isCJKNum || isDig) { hasNum = true; continue; }
        sp = bk; break;
      }
      // Skip optional terminator (回/章/節/篇/卷)
      if (hasNum && sp < (int)s.length()) {
        int bk = sp;
        uint32_t c = utf8Decode(s, sp);
        if (c!=0x56DE&&c!=0x7AE0&&c!=0x7BC0&&c!=0x7BC7&&c!=0x5377) sp = bk;
      }
      // Insert space if next char is not space/whitespace
      if (sp < (int)s.length()) {
        int bk = sp;
        uint32_t nc = utf8Decode(s, bk);
        if (nc != ' ' && nc != 0x3000 && nc != '\t') {
          s = s.substring(0, sp) + " " + s.substring(sp);
        }
      }
    } else if (c0 == 0x5377) {  // 卷
      while (sp < (int)s.length()) {
        int bk = sp;
        uint32_t c = utf8Decode(s, sp);
        bool isCJKNum = (c==0x4E00||c==0x4E8C||c==0x4E09||c==0x56DB||c==0x4E94||
                         c==0x516D||c==0x4E03||c==0x516B||c==0x4E5D||c==0x5341||
                         c==0x767E||c==0x5343||c==0x96F6||c==0x3007||c==0x25CB);
        bool isDig = (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
        if (!isCJKNum && !isDig) { sp = bk; break; }
      }
      if (sp < (int)s.length()) {
        int bk = sp;
        uint32_t nc = utf8Decode(s, bk);
        if (nc != ' ' && nc != 0x3000 && nc != '\t') {
          s = s.substring(0, sp) + " " + s.substring(sp);
        }
      }
    }
  }

  // Phase 1-2: punct stripping + couplet midpoint split (requires ≥3 entries)
  if (epubTocCount < 3) return;
  {
    // Find body start for each label: skip 第 + number + terminator + whitespace,
    // or (for 卷X pattern) 卷 + number + whitespace.
    // Heap-allocate these so we don't blow the FreeRTOS task stack at large TOC sizes.
    int* bodyStarts = (int*)heap_caps_malloc(sizeof(int) * epubTocCount, MALLOC_CAP_8BIT);
    uint32_t* firstBodyCp = (uint32_t*)heap_caps_malloc(sizeof(uint32_t) * epubTocCount, MALLOC_CAP_8BIT);
    int* firstBodyCpByteLen = (int*)heap_caps_malloc(sizeof(int) * epubTocCount, MALLOC_CAP_8BIT);
    if (!bodyStarts || !firstBodyCp || !firstBodyCpByteLen) {
      if (bodyStarts) free(bodyStarts);
      if (firstBodyCp) free(firstBodyCp);
      if (firstBodyCpByteLen) free(firstBodyCpByteLen);
      return;  // skip polish if we can't allocate; TOC is still usable
    }
    for (int e = 0; e < epubTocCount; e++) {
      const String& s = epubTocEntries[e].label;
      int sp = 0;
      uint32_t c0 = s.length() ? utf8Decode(s, sp) : 0;
      if (c0 == 0x7B2C) {  // 第
        // Skip number chars
        while (sp < (int)s.length()) {
          int bk = sp;
          uint32_t c = utf8Decode(s, sp);
          bool isCJKNum = (c==0x4E00||c==0x4E8C||c==0x4E09||c==0x56DB||c==0x4E94||
                           c==0x516D||c==0x4E03||c==0x516B||c==0x4E5D||c==0x5341||
                           c==0x767E||c==0x5343||c==0x96F6||c==0x3007||c==0x25CB);
          bool isDig = (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
          if (!isCJKNum && !isDig) { sp = bk; break; }
        }
        // Skip optional whitespace then terminator
        {
          int bk = sp;
          while (sp < (int)s.length()) {
            int bk2 = sp;
            uint32_t c = utf8Decode(s, sp);
            if (c != ' ' && c != '\t' && c != 0x3000) { sp = bk2; break; }
          }
          if (sp < (int)s.length()) {
            int bk2 = sp;
            uint32_t c = utf8Decode(s, sp);
            if (c!=0x56DE&&c!=0x7AE0&&c!=0x7BC0&&c!=0x7BC7&&c!=0x5377) { sp = bk; }
          }
        }
      } else if (c0 == 0x5377) {  // 卷
        while (sp < (int)s.length()) {
          int bk = sp;
          uint32_t c = utf8Decode(s, sp);
          bool isCJKNum = (c==0x4E00||c==0x4E8C||c==0x4E09||c==0x56DB||c==0x4E94||
                           c==0x516D||c==0x4E03||c==0x516B||c==0x4E5D||c==0x5341||
                           c==0x767E||c==0x5343||c==0x96F6||c==0x3007||c==0x25CB);
          bool isDig = (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
          if (!isCJKNum && !isDig) { sp = bk; break; }
        }
      }
      // Skip whitespace after terminator
      while (sp < (int)s.length()) {
        int bk = sp;
        uint32_t c = utf8Decode(s, sp);
        if (c != ' ' && c != '\t' && c != 0x3000) { sp = bk; break; }
      }
      bodyStarts[e] = sp;
      if (sp < (int)s.length()) {
        int tmp = sp;
        firstBodyCp[e] = utf8Decode(s, tmp);
        firstBodyCpByteLen[e] = tmp - sp;
      } else {
        firstBodyCp[e] = 0;
        firstBodyCpByteLen[e] = 0;
      }
    }

    // Build histogram of first body char, count only punctuation candidates
    struct { uint32_t cp; int count; int byteLen; } hist[8];
    int histN = 0;
    int total = 0;
    for (int e = 0; e < epubTocCount; e++) {
      uint32_t c = firstBodyCp[e];
      if (c == 0) continue;
      if (!isPunctuation(c)) continue;
      total++;
      bool merged = false;
      for (int h = 0; h < histN; h++) {
        if (hist[h].cp == c) { hist[h].count++; merged = true; break; }
      }
      if (!merged && histN < 8) {
        hist[histN].cp = c;
        hist[histN].count = 1;
        hist[histN].byteLen = firstBodyCpByteLen[e];
        histN++;
      }
    }
    uint32_t stripCp = 0;
    int stripByteLen = 0;
    for (int h = 0; h < histN; h++) {
      if (hist[h].count * 2 > epubTocCount) {
        stripCp = hist[h].cp;
        stripByteLen = hist[h].byteLen;
        break;
      }
    }
    if (stripCp) {
      Serial.printf("TOC polish: stripping repeated prefix U+%04X from %d labels\n",
                    stripCp, (int)stripCp);
    }

    // Apply strip + midpoint split
    for (int e = 0; e < epubTocCount; e++) {
      String& s = epubTocEntries[e].label;
      if (stripCp && firstBodyCp[e] == stripCp) {
        int cut = bodyStarts[e] + stripByteLen;
        // Also consume any whitespace that follows
        while (cut < (int)s.length()) {
          int bk = cut;
          uint32_t c = utf8Decode(s, cut);
          if (c != ' ' && c != '\t' && c != 0x3000) { cut = bk; break; }
        }
        s = s.substring(0, bodyStarts[e]) + s.substring(cut);
      }

      // Midpoint split: if body has >=10 CJK chars with no internal separator
      int bs = 0;
      // recompute body start (label may have changed)
      {
        int sp = 0;
        uint32_t c0 = s.length() ? utf8Decode(s, sp) : 0;
        if (c0 == 0x7B2C) {
          while (sp < (int)s.length()) {
            int bk = sp;
            uint32_t c = utf8Decode(s, sp);
            bool isCJKNum = (c==0x4E00||c==0x4E8C||c==0x4E09||c==0x56DB||c==0x4E94||
                             c==0x516D||c==0x4E03||c==0x516B||c==0x4E5D||c==0x5341||
                             c==0x767E||c==0x5343||c==0x96F6||c==0x3007||c==0x25CB);
            bool isDig = (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
            if (!isCJKNum && !isDig) { sp = bk; break; }
          }
          int bk = sp;
          while (sp < (int)s.length()) {
            int bk2 = sp;
            uint32_t c = utf8Decode(s, sp);
            if (c != ' ' && c != '\t' && c != 0x3000) { sp = bk2; break; }
          }
          if (sp < (int)s.length()) {
            int bk2 = sp;
            uint32_t c = utf8Decode(s, sp);
            if (c!=0x56DE&&c!=0x7AE0&&c!=0x7BC0&&c!=0x7BC7&&c!=0x5377) sp = bk;
          }
        } else if (c0 == 0x5377) {
          while (sp < (int)s.length()) {
            int bk = sp;
            uint32_t c = utf8Decode(s, sp);
            bool isCJKNum = (c==0x4E00||c==0x4E8C||c==0x4E09||c==0x56DB||c==0x4E94||
                             c==0x516D||c==0x4E03||c==0x516B||c==0x4E5D||c==0x5341||
                             c==0x767E||c==0x5343||c==0x96F6||c==0x3007||c==0x25CB);
            bool isDig = (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
            if (!isCJKNum && !isDig) { sp = bk; break; }
          }
        }
        while (sp < (int)s.length()) {
          int bk = sp;
          uint32_t c = utf8Decode(s, sp);
          if (c != ' ' && c != '\t' && c != 0x3000) { sp = bk; break; }
        }
        bs = sp;
      }

      // Count CJK chars in the body and record byte positions
      int cjkPositions[128];
      int cjkCount = 0;
      bool hasInternalSpace = false;
      int sp = bs;
      while (sp < (int)s.length() && cjkCount < 128) {
        int bk = sp;
        uint32_t c = utf8Decode(s, sp);
        if (c == ' ' || c == 0x3000 || c == '\t') { hasInternalSpace = true; break; }
        if (c >= 0x4E00 && c <= 0x9FFF) {
          cjkPositions[cjkCount++] = bk;
        }
      }
      if (!hasInternalSpace && cjkCount >= 10) {
        // Round the midpoint up so odd-length couplets split with the longer
        // half first (e.g. 17 chars → 9 + 8, keeping 良朋刮目 intact on line 1
        // instead of breaking "刮目" across lines).
        int midByte = cjkPositions[(cjkCount + 1) / 2];
        s = s.substring(0, midByte) + " " + s.substring(midByte);
      }
    }
    free(bodyStarts);
    free(firstBodyCp);
    free(firstBodyCpByteLen);
  }
}

// Parse TOC from EPUB's toc.ncx file
// Extracts <navPoint> entries: label text and content src → mapped to chapter index
bool epubParseToc() {
  epubFreeToc();

  if (epubFilePath.isEmpty() || !epubZipEntries || epubZipEntryCount == 0) return false;
  if (!epubChapters || epubChapterCount == 0) return false;

  // Open EPUB file
  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubFilePath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubFilePath.c_str()); }
  if (!f) return false;

  // Find .ncx file in ZIP entries
  int ncxIdx = -1;
  for (int i = 0; i < epubZipEntryCount; i++) {
    if (epubZipEntries[i].filename.endsWith(".ncx")) {
      ncxIdx = i;
      break;
    }
  }

  // Find EPUB3 nav document (nav.xhtml or similar with epub:type="toc")
  int navIdx = -1;
  if (ncxIdx < 0) {
    for (int i = 0; i < epubZipEntryCount; i++) {
      String fn = epubZipEntries[i].filename;
      fn.toLowerCase();
      if (fn.endsWith("nav.xhtml") || fn.endsWith("nav.html") || fn.endsWith("toc.xhtml")) {
        navIdx = i;
        break;
      }
    }
  }

  if (ncxIdx < 0 && navIdx < 0) {
    Serial.println("EPUB TOC: no .ncx or nav.xhtml found");
    f.close();
    return false;
  }

  // Allocate TOC entries
  epubTocEntries = (TocEntry*)ps_malloc(sizeof(TocEntry) * MAX_TOC_ENTRIES);
  if (!epubTocEntries) { f.close(); return false; }
  for (int i = 0; i < MAX_TOC_ENTRIES; i++) new (&epubTocEntries[i]) TocEntry();

  // Helper lambda: resolve href to chapter index
  auto resolveHrefToChapter = [&](const String& baseDir, const String& href) -> int {
    String src = href;
    int hashPos = src.indexOf('#');
    if (hashPos >= 0) src = src.substring(0, hashPos);
    String fullPath = pathNormalize(baseDir + src);
    for (int c = 0; c < epubChapterCount; c++) {
      int zipIdx = epubChapters[c].zipEntryIndex;
      if (zipIdx >= 0 && zipIdx < epubZipEntryCount) {
        if (epubZipEntries[zipIdx].filename == fullPath) return c;
      }
    }
    return -1;
  };

  if (ncxIdx >= 0) {
    // Parse EPUB2 .ncx TOC
    String ncxContent = zipExtractString(f, epubZipEntries[ncxIdx]);
    f.close();
    if (ncxContent.length() == 0) { epubFreeToc(); return false; }

    String ncxDir = pathDir(epubZipEntries[ncxIdx].filename);

    int searchFrom = 0;
    while (searchFrom < (int)ncxContent.length() && epubTocCount < MAX_TOC_ENTRIES) {
      int npStart = ncxContent.indexOf("<navPoint", searchFrom);
      if (npStart < 0) break;

      int textStart = ncxContent.indexOf("<text>", npStart);
      if (textStart < 0) { searchFrom = npStart + 9; continue; }
      textStart += 6;
      int textEnd = ncxContent.indexOf("</text>", textStart);
      if (textEnd < 0) { searchFrom = npStart + 9; continue; }
      String label = ncxContent.substring(textStart, textEnd);
      label = xmlDecodeEntities(label);
      label.trim();

      int contentStart = ncxContent.indexOf("<content", npStart);
      if (contentStart < 0 || contentStart > textEnd + 200) { searchFrom = textEnd; continue; }
      int contentEnd = ncxContent.indexOf('>', contentStart);
      if (contentEnd < 0) { searchFrom = textEnd; continue; }
      String contentTag = ncxContent.substring(contentStart, contentEnd + 1);
      String src = xmlDecodeEntities(xmlAttrValue(contentTag, "src"));
      if (src.length() == 0) { searchFrom = textEnd; continue; }

      int chapterIdx = resolveHrefToChapter(ncxDir, src);

      if (chapterIdx >= 0 && label.length() > 0) {
        epubTocEntries[epubTocCount].label = label;
        epubTocEntries[epubTocCount].chapterIndex = chapterIdx;
        epubTocCount++;
      }

      searchFrom = textEnd;
      yield();
    }

    Serial.printf("EPUB TOC (ncx): %d entries parsed\n", epubTocCount);
  } else {
    // Parse EPUB3 nav.xhtml TOC
    // Looks for <nav epub:type="toc"> then parses <a href="...">label</a> entries
    String navContent = zipExtractString(f, epubZipEntries[navIdx]);
    f.close();
    if (navContent.length() == 0) { epubFreeToc(); return false; }

    String navDir = pathDir(epubZipEntries[navIdx].filename);
    Serial.printf("EPUB TOC: parsing nav.xhtml (%d bytes) from %s\n",
                  navContent.length(), epubZipEntries[navIdx].filename.c_str());

    // Find <nav ... epub:type="toc" ...> section
    int navStart = -1;
    int searchFrom = 0;
    while (searchFrom < (int)navContent.length()) {
      int tagStart = navContent.indexOf("<nav", searchFrom);
      if (tagStart < 0) break;
      int tagEnd = navContent.indexOf('>', tagStart);
      if (tagEnd < 0) break;
      String tagContent = navContent.substring(tagStart, tagEnd + 1);
      if (tagContent.indexOf("\"toc\"") >= 0 || tagContent.indexOf("'toc'") >= 0) {
        navStart = tagEnd + 1;
        break;
      }
      searchFrom = tagEnd + 1;
    }

    if (navStart < 0) {
      // Fallback: look for first <ol> after <nav
      navStart = navContent.indexOf("<nav");
      if (navStart >= 0) {
        int olPos = navContent.indexOf("<ol", navStart);
        if (olPos >= 0) navStart = olPos;
        else navStart = -1;
      }
    }

    if (navStart < 0) {
      Serial.println("EPUB TOC: no <nav epub:type=\"toc\"> found in nav.xhtml");
      epubFreeToc();
      return false;
    }

    // Find closing </nav>
    int navEnd = navContent.indexOf("</nav>", navStart);
    if (navEnd < 0) navEnd = navContent.length();

    // Parse <a href="...">text</a> entries within the nav section
    int pos = navStart;
    while (pos < navEnd && epubTocCount < MAX_TOC_ENTRIES) {
      int aStart = navContent.indexOf("<a", pos);
      if (aStart < 0 || aStart >= navEnd) break;

      // Extract href
      int aTagEnd = navContent.indexOf('>', aStart);
      if (aTagEnd < 0 || aTagEnd >= navEnd) { pos = aStart + 2; continue; }
      String aTag = navContent.substring(aStart, aTagEnd + 1);
      String href = xmlDecodeEntities(xmlAttrValue(aTag, "href"));
      if (href.length() == 0) { pos = aTagEnd + 1; continue; }

      // Extract link text (between > and </a>)
      int textStart = aTagEnd + 1;
      int textEnd = navContent.indexOf("</a>", textStart);
      if (textEnd < 0) { pos = aStart + 2; continue; }
      String label = navContent.substring(textStart, textEnd);
      // Strip any nested HTML tags from label
      while (true) {
        int lt = label.indexOf('<');
        if (lt < 0) break;
        int gt = label.indexOf('>', lt);
        if (gt < 0) break;
        label = label.substring(0, lt) + label.substring(gt + 1);
      }
      label = xmlDecodeEntities(label);
      label.trim();

      int chapterIdx = resolveHrefToChapter(navDir, href);

      if (chapterIdx >= 0 && label.length() > 0) {
        epubTocEntries[epubTocCount].label = label;
        epubTocEntries[epubTocCount].chapterIndex = chapterIdx;
        epubTocCount++;
      }

      pos = textEnd + 4;
      yield();
    }

    Serial.printf("EPUB TOC (nav.xhtml): %d entries parsed\n", epubTocCount);
  }

  if (epubTocCount == 0) {
    epubFreeToc();
    return false;
  }
  return true;
}

// Clean up all persistent EPUB data
void epubCleanup() {
  epubFreeToc();
  if (epubFullText) {
    free(epubFullText);
    epubFullText = nullptr;
    epubFullTextLen = 0;
  }
  if (epubZipEntries) {
    for (int i = 0; i < epubZipEntryCount; i++) epubZipEntries[i].~ZipEntry();
    free(epubZipEntries);
    epubZipEntries = nullptr;
    epubZipEntryCount = 0;
  }
  if (epubChapters) {
    free(epubChapters);
    epubChapters = nullptr;
    epubChapterCount = 0;
  }
  epubFilePath = "";
  epubBasePath = "";
  epubLoadedStartChapter = 0;
  epubLoadedEndChapter = 0;
  epubLoadedBaseOffset = 0;
  epubEstimatedTotalBytes = 0;
  epubIsImageBased = false;
  epubHasMultiImageChapters = false;
  epubIsHorizontal = false;
}

// Parse JPEG dimensions from raw data (returns true if found)
bool getJpegDimensions(const uint8_t* data, size_t len, int& width, int& height) {
  size_t i = 0;
  while (i + 1 < len) {
    if (data[i] != 0xFF) return false;
    uint8_t marker = data[i + 1];
    if (marker == 0xD8) { i += 2; continue; }  // SOI
    if (marker == 0xD9) return false;  // EOI
    if (i + 3 >= len) return false;
    uint16_t segLen = (data[i + 2] << 8) | data[i + 3];
    // SOF markers (0xC0-0xCF except 0xC4, 0xC8, 0xCC)
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      if (i + 8 < len) {
        height = (data[i + 5] << 8) | data[i + 6];
        width  = (data[i + 7] << 8) | data[i + 8];
        return true;
      }
      return false;
    }
    i += 2 + segLen;
  }
  return false;
}

// Parse PNG dimensions from raw data (returns true if found)
bool getPngDimensions(const uint8_t* data, size_t len, int& width, int& height) {
  // PNG signature: 89 50 4E 47 0D 0A 1A 0A, then IHDR chunk
  if (len < 24) return false;
  if (data[0] != 0x89 || data[1] != 0x50 || data[2] != 0x4E || data[3] != 0x47) return false;
  // IHDR starts at offset 8 (4 bytes length + 4 bytes "IHDR" + 4 bytes width + 4 bytes height)
  width  = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
  height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
  return (width > 0 && height > 0);
}

// Extract an image from the EPUB ZIP and draw it on the display
// Returns true if image was drawn successfully
bool epubExtractAndDrawImage(const String& imagePath, int x, int y, int maxW, int maxH,
                             int quadrant, float zoomCenterX, float zoomCenterY,
                             int* outRenderedH) {
  if (epubFilePath.isEmpty() || !epubZipEntries || epubZipEntryCount == 0) {
    Serial.println("EPUB IMG: No persistent ZIP data");
    return false;
  }

  // Normalize the image path to resolve any ".." segments
  String normalizedPath = pathNormalize(imagePath);
  Serial.printf("EPUB IMG: Looking for '%s' (normalized: '%s')\n",
                imagePath.c_str(), normalizedPath.c_str());

  // Find the ZIP entry for this image
  int entryIdx = -1;
  for (int i = 0; i < epubZipEntryCount; i++) {
    if (epubZipEntries[i].filename == normalizedPath ||
        epubZipEntries[i].filename == imagePath) {
      entryIdx = i;
      break;
    }
  }
  if (entryIdx < 0) {
    // Try matching just the filename part as fallback
    int lastSlash = normalizedPath.lastIndexOf('/');
    String justName = (lastSlash >= 0) ? normalizedPath.substring(lastSlash + 1) : normalizedPath;
    for (int i = 0; i < epubZipEntryCount; i++) {
      if (epubZipEntries[i].filename.endsWith("/" + justName) ||
          epubZipEntries[i].filename == justName) {
        entryIdx = i;
        Serial.printf("EPUB IMG: Matched by filename: %s\n", epubZipEntries[i].filename.c_str());
        break;
      }
    }
  }
  if (entryIdx < 0) {
    Serial.printf("EPUB IMG: Not found in ZIP: %s\n", normalizedPath.c_str());
    return false;
  }

  ZipEntry& entry = epubZipEntries[entryIdx];

  // Safety check - skip very large images to avoid PSRAM exhaustion
  if (entry.uncompSize > 2 * 1024 * 1024) {
    Serial.printf("EPUB IMG: Too large (%u bytes): %s\n", entry.uncompSize, imagePath.c_str());
    return false;
  }

  // Open EPUB file
  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubFilePath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubFilePath.c_str()); }
  if (!f) { Serial.println("EPUB IMG: Cannot open file"); return false; }

  // Extract image data
  size_t imgLen = 0;
  uint8_t* imgBuf = zipExtractFile(f, entry, imgLen);
  f.close();

  if (!imgBuf || imgLen == 0) {
    if (imgBuf) free(imgBuf);
    Serial.println("EPUB IMG: Extraction failed");
    return false;
  }

  Serial.printf("EPUB IMG: Extracted %u bytes, free PSRAM: %u, drawing at (%d,%d) max %dx%d\n",
                imgLen, ESP.getFreePsram(), x, y, maxW, maxH);

  // Determine image format and draw
  bool drawn = false;
  String lowerPath = imagePath;
  lowerPath.toLowerCase();
  bool isJpeg = lowerPath.endsWith(".jpg") || lowerPath.endsWith(".jpeg");
  bool isPng = lowerPath.endsWith(".png");
  bool isBmp = lowerPath.endsWith(".bmp");

  // Parse image dimensions to calculate proper scale
  int imgW = 0, imgH = 0;
  bool gotDims = false;
  if (isJpeg || (!isPng && !isBmp)) {
    gotDims = getJpegDimensions(imgBuf, imgLen, imgW, imgH);
  }
  if (!gotDims && (isPng || (!isJpeg && !isBmp))) {
    gotDims = getPngDimensions(imgBuf, imgLen, imgW, imgH);
  }

  // Calculate scale to fit image within available area
  float scale_x = 1.0f;
  float scale_y = 0.0f;  // 0 means same as scale_x (maintain aspect ratio)
  int offX = 0, offY = 0;
  if (gotDims && imgW > 0 && imgH > 0) {
    if (quadrant >= 0) {
      // Zoom: scale so half the image fills the display area
      float sx = (float)maxW / ((float)imgW / 2.0f);
      float sy = (float)maxH / ((float)imgH / 2.0f);
      float scale = (sx < sy) ? sx : sy;
      scale_x = scale;
      scale_y = scale;
      int scaledW = (int)(imgW * scale);
      int scaledH = (int)(imgH * scale);

      if (quadrant == 100) {
        // Free-point zoom: center on the tap point
        int centerPixX = (int)(zoomCenterX * scaledW);
        int centerPixY = (int)(zoomCenterY * scaledH);
        offX = centerPixX - maxW / 2;
        offY = centerPixY - maxH / 2;
        // Clamp to image bounds
        if (offX < 0) offX = 0;
        if (offY < 0) offY = 0;
        if (offX + maxW > scaledW) offX = scaledW - maxW;
        if (offY + maxH > scaledH) offY = scaledH - maxH;
        if (offX < 0) offX = 0;
        if (offY < 0) offY = 0;
        Serial.printf("EPUB IMG: Free zoom center=(%.2f,%.2f), off=(%d,%d), scale=%.3f\n",
                      zoomCenterX, zoomCenterY, offX, offY, scale);
      } else {
        // Quadrant zoom
        int halfW = scaledW / 2;
        int halfH = scaledH / 2;
        int visW = (halfW < maxW) ? halfW : maxW;
        int visH = (halfH < maxH) ? halfH : maxH;
        int padX = (maxW - visW) / 2;
        int padY = (maxH - visH) / 2;
        x += padX;
        y += padY;
        switch (quadrant) {
          case 0: offX = 0;     offY = 0;     break;  // Top-left
          case 1: offX = halfW; offY = 0;     break;  // Top-right
          case 2: offX = 0;     offY = halfH; break;  // Bottom-left
          case 3: offX = halfW; offY = halfH; break;  // Bottom-right
        }
        Serial.printf("EPUB IMG: Zoom Q%d, scale=%.3f, off=(%d,%d), vis=%dx%d\n",
                      quadrant, scale, offX, offY, visW, visH);
      }
    } else {
      float sx = (float)maxW / (float)imgW;
      float sy = (float)maxH / (float)imgH;
      float scale = (sx < sy) ? sx : sy;  // Fit within bounds
      if (scale > 1.0f) scale = 1.0f;     // Don't enlarge small images
      scale_x = scale;
      scale_y = scale;
      Serial.printf("EPUB IMG: Original %dx%d, scale=%.3f, output ~%dx%d\n",
                    imgW, imgH, scale_x, (int)(imgW * scale_x), (int)(imgH * scale_x));
    }
  } else {
    Serial.printf("EPUB IMG: Could not parse dimensions, using default scale\n");
  }

  // Center the scaled image horizontally, keep top-aligned vertically
  // so text can flow below the image and outRenderedH is accurate.
  int drawX = x;
  int drawY = y;
  int clipW = maxW;
  int clipH = maxH;
  if (quadrant < 0 && gotDims && imgW > 0 && imgH > 0) {
    int outW = (int)(imgW * scale_x);
    int outH = (int)(imgH * (scale_y > 0 ? scale_y : scale_x));
    if (outW < maxW) drawX = x + (maxW - outW) / 2;
    // Keep drawY = y (top-aligned) so text can continue below
    clipW = outW < maxW ? outW : maxW;
    clipH = outH < maxH ? outH : maxH;
  } else {
    drawX = x;
    drawY = y;
  }

  if (isJpeg) {
    Serial.printf("EPUB IMG: drawing JPEG %dx%d at (%d,%d)...\n", imgW, imgH, drawX, drawY);
    drawn = M5.Display.drawJpg(imgBuf, imgLen, drawX, drawY, clipW, clipH, offX, offY, scale_x, scale_y);
  } else if (isPng) {
    Serial.printf("EPUB IMG: drawing PNG %dx%d at (%d,%d)...\n", imgW, imgH, drawX, drawY);
    drawn = M5.Display.drawPng(imgBuf, imgLen, drawX, drawY, clipW, clipH, offX, offY, scale_x, scale_y);
  } else if (isBmp) {
    Serial.printf("EPUB IMG: drawing BMP %dx%d at (%d,%d)...\n", imgW, imgH, drawX, drawY);
    drawn = M5.Display.drawBmp(imgBuf, imgLen, drawX, drawY, clipW, clipH, offX, offY, scale_x, scale_y);
  } else {
    // Try JPEG first (most common in EPUBs), then PNG
    Serial.printf("EPUB IMG: drawing unknown format %dx%d at (%d,%d)...\n", imgW, imgH, drawX, drawY);
    drawn = M5.Display.drawJpg(imgBuf, imgLen, drawX, drawY, clipW, clipH, offX, offY, scale_x, scale_y);
    if (!drawn) drawn = M5.Display.drawPng(imgBuf, imgLen, drawX, drawY, clipW, clipH, offX, offY, scale_x, scale_y);
  }
  esp_task_wdt_reset();

  free(imgBuf);

  if (drawn) {
    Serial.printf("EPUB IMG: Drew %s successfully\n", imagePath.c_str());
    if (outRenderedH) {
      if (gotDims && imgW > 0 && imgH > 0) {
        int outH = (int)(imgH * (scale_y > 0 ? scale_y : scale_x));
        *outRenderedH = (outH < maxH) ? outH : maxH;
      } else {
        *outRenderedH = maxH;
      }
    }
  } else {
    Serial.printf("EPUB IMG: Failed to decode %s\n", imagePath.c_str());
    if (outRenderedH) *outRenderedH = 0;
  }
  return drawn;
}

// ==================== End ZIP / EPUB Reader ====================
