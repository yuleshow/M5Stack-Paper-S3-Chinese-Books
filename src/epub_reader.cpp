#include "globals.h"

// ==================== ZIP / EPUB Reader ====================

// Little-endian binary readers
static uint16_t zipU16(const uint8_t* b) { return b[0] | (b[1] << 8); }
static uint32_t zipU32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24); }

#define MAX_ZIP_ENTRIES 3000

// Parse ZIP central directory and return entries
int zipReadDirectory(File& f, ZipEntry* entries, int maxEntries) {
  uint32_t fileSize = f.size();
  if (fileSize < 22) return 0;

  // Search backwards for End of Central Directory record (signature 0x06054b50)
  uint32_t searchFrom = (fileSize > 65557) ? fileSize - 65557 : 0;
  uint8_t sig[4];
  uint32_t eocdPos = 0;
  bool found = false;

  for (uint32_t pos = fileSize - 22; pos >= searchFrom; pos--) {
    f.seek(pos);
    f.read(sig, 4);
    if (sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x05 && sig[3] == 0x06) {
      eocdPos = pos;
      found = true;
      break;
    }
    if (pos == 0) break;
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
    uint8_t* buf = (uint8_t*)ps_malloc(entry.uncompSize + 1);
    if (!buf) return nullptr;
    f.read(buf, entry.uncompSize);
    buf[entry.uncompSize] = '\0';
    outLen = entry.uncompSize;
    return buf;
  }
  else if (entry.method == 8) {
    // Deflated — read compressed data then inflate using ROM
    uint8_t* compBuf = (uint8_t*)ps_malloc(entry.compSize);
    if (!compBuf) {
      Serial.printf("ZIP: ps_malloc(%u) failed for compBuf, free PSRAM: %u\n",
                     entry.compSize, ESP.getFreePsram());
      return nullptr;
    }
    f.read(compBuf, entry.compSize);

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

  for (size_t i = 0; i < htmlLen && outPos < outBufSize - 200; i++) {
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
                    char decoded = (char)strtol(hex, nullptr, 16);
                    if (decoded > 0) { outBuf[outPos++] = decoded; k += 2; continue; }
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
        if (strcmp(tagName, "/p") == 0 || strcmp(tagName, "/div") == 0 ||
            (tagName[0] == '/' && tagName[1] == 'h') ||
            strcmp(tagName, "br") == 0 || strcmp(tagName, "br/") == 0 ||
            strcmp(tagName, "/li") == 0 || strcmp(tagName, "/tr") == 0) {
          if (!lastWasNewline && outPos > 0) {
            outBuf[outPos++] = '\n';
            lastWasNewline = true;
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
        else if (strcmp(entity, "&nbsp;") == 0) { outBuf[outPos++] = ' '; lastWasNewline = false; }
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

    // Normal character
    if (c == '\n' || c == '\r') {
      if (!lastWasNewline) { outBuf[outPos++] = '\n'; lastWasNewline = true; }
    } else if (c == ' ' || c == '\t') {
      if (outPos > 0 && outBuf[outPos-1] != ' ' && !lastWasNewline) outBuf[outPos++] = ' ';
    } else {
      outBuf[outPos++] = c;
      lastWasNewline = false;
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

  const int TITLE_MAX_ENTRIES = 200;  // Only need a few entries for title lookup
  ZipEntry* entries = (ZipEntry*)ps_malloc(sizeof(ZipEntry) * TITLE_MAX_ENTRIES);
  if (!entries) { f.close(); return ""; }
  for (int i = 0; i < TITLE_MAX_ENTRIES; i++) new (&entries[i]) ZipEntry();
  int entryCount = zipReadDirectory(f, entries, TITLE_MAX_ENTRIES);
  if (entryCount == 0) {
    for (int i = 0; i < TITLE_MAX_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return "";
  }

  // Find OPF path from container.xml
  String opfPath = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == "META-INF/container.xml") {
      String containerXml = zipExtractString(f, entries[i]);
      int idx = containerXml.indexOf("full-path=\"");
      if (idx >= 0) { idx += 11; int end = containerXml.indexOf('"', idx); if (end > idx) opfPath = containerXml.substring(idx, end); }
      break;
    }
  }
  if (opfPath.length() == 0) {
    for (int i = 0; i < entryCount; i++) {
      if (entries[i].filename.endsWith(".opf")) { opfPath = entries[i].filename; break; }
    }
  }
  if (opfPath.length() == 0) {
    for (int i = 0; i < TITLE_MAX_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return "";
  }

  // Read OPF and extract <dc:title>
  String opfContent = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == opfPath) { opfContent = zipExtractString(f, entries[i]); break; }
  }
  for (int i = 0; i < TITLE_MAX_ENTRIES; i++) entries[i].~ZipEntry();
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
bool epubLoad(const String& epubPath) {
  Serial.printf("\n=== EPUB: Loading %s ===\n", epubPath.c_str());

  // Clean up previous EPUB data
  epubCleanup();

  epubFilePath = epubPath;

  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubPath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubPath.c_str()); }
  if (!f) { Serial.println("EPUB: Cannot open file"); return false; }

  Serial.printf("EPUB: File size = %u bytes, Free PSRAM = %u bytes\n", f.size(), ESP.getFreePsram());

  // Parse ZIP central directory — allocate persistent entries
  epubZipEntries = (ZipEntry*)ps_malloc(sizeof(ZipEntry) * MAX_ZIP_ENTRIES);
  if (!epubZipEntries) { f.close(); return false; }
  for (int i = 0; i < MAX_ZIP_ENTRIES; i++) new (&epubZipEntries[i]) ZipEntry();
  epubZipEntryCount = zipReadDirectory(f, epubZipEntries, MAX_ZIP_ENTRIES);
  Serial.printf("EPUB: ZIP has %d entries\n", epubZipEntryCount);
  if (epubZipEntryCount == 0) {
    epubCleanup(); f.close(); return false;
  }

  // Step 1: Find container.xml → get OPF path
  String opfPath = "";
  for (int i = 0; i < epubZipEntryCount; i++) {
    if (epubZipEntries[i].filename == "META-INF/container.xml") {
      String containerXml = zipExtractString(f, epubZipEntries[i]);
      int idx = containerXml.indexOf("full-path=\"");
      if (idx >= 0) {
        idx += 11;
        int end = containerXml.indexOf('"', idx);
        if (end > idx) opfPath = containerXml.substring(idx, end);
      }
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
    epubCleanup(); f.close(); return false;
  }

  epubBasePath = pathDir(opfPath);

  // Step 2: Read OPF → build manifest (id→href) and read spine order
  String opfContent = "";
  for (int i = 0; i < epubZipEntryCount; i++) {
    if (epubZipEntries[i].filename == opfPath) {
      opfContent = zipExtractString(f, epubZipEntries[i]);
      break;
    }
  }
  if (opfContent.length() == 0) {
    epubCleanup(); f.close(); return false;
  }

  // Parse manifest: <item id="..." href="..." media-type="..."/>
  struct ManifestItem { String id; String href; bool isContent; };
  const int MAX_MANIFEST = 3000;
  ManifestItem* manifest = (ManifestItem*)ps_malloc(sizeof(ManifestItem) * MAX_MANIFEST);
  if (!manifest) { epubCleanup(); f.close(); return false; }
  for (int i = 0; i < MAX_MANIFEST; i++) new (&manifest[i]) ManifestItem();
  int manifestCount = 0;

  {
    int searchFrom = 0;
      while (searchFrom < (int)opfContent.length() && manifestCount < MAX_MANIFEST) {
      int itemPos = opfContent.indexOf("<item ", searchFrom);
      if (itemPos < 0) break;
      int itemEnd = opfContent.indexOf("/>", itemPos);
      if (itemEnd < 0) itemEnd = opfContent.indexOf(">", itemPos);
      if (itemEnd < 0) break;
      String itemTag = opfContent.substring(itemPos, itemEnd + 2);

      String id = "", href = "", mediaType = "";
      int p;
      p = itemTag.indexOf("id=\"");
      if (p >= 0) { p += 4; int e = itemTag.indexOf('"', p); if (e > p) id = itemTag.substring(p, e); }
      p = itemTag.indexOf("href=\"");
      if (p >= 0) { p += 6; int e = itemTag.indexOf('"', p); if (e > p) href = itemTag.substring(p, e); }
      p = itemTag.indexOf("media-type=\"");
      if (p >= 0) { p += 12; int e = itemTag.indexOf('"', p); if (e > p) mediaType = itemTag.substring(p, e); }

      manifest[manifestCount].id = id;
      manifest[manifestCount].href = href;
      manifest[manifestCount].isContent = (mediaType.indexOf("html") >= 0);
      manifestCount++;

      searchFrom = itemEnd + 1;
    }
  }
  Serial.printf("EPUB: Manifest has %d items\n", manifestCount);

  // Parse spine: <itemref idref="..."/>
  const int MAX_SPINE = 1500;
  String* spineRefs = (String*)ps_malloc(sizeof(String) * MAX_SPINE);
  if (!spineRefs) {
    for (int i = 0; i < MAX_MANIFEST; i++) manifest[i].~ManifestItem();
    free(manifest); epubCleanup(); f.close(); return false;
  }
  for (int i = 0; i < MAX_SPINE; i++) new (&spineRefs[i]) String();
  int spineCount = 0;
  {
    int spineStart = opfContent.indexOf("<spine");
    int spineEnd = opfContent.indexOf("</spine>");
    if (spineStart >= 0 && spineEnd > spineStart) {
      String spineSection = opfContent.substring(spineStart, spineEnd);
      int searchFrom = 0;
      while (searchFrom < (int)spineSection.length() && spineCount < MAX_SPINE) {
        int refPos = spineSection.indexOf("idref=\"", searchFrom);
        if (refPos < 0) break;
        refPos += 7;
        int refEnd = spineSection.indexOf('"', refPos);
        if (refEnd > refPos) {
          spineRefs[spineCount++] = spineSection.substring(refPos, refEnd);
        }
        searchFrom = refEnd + 1;
      }
    }
  }
  Serial.printf("EPUB: Spine has %d items\n", spineCount);
  opfContent = "";  // Free OPF content early

  // Step 3: Build chapter index with estimated text sizes
  epubChapters = (EpubChapterInfo*)ps_malloc(sizeof(EpubChapterInfo) * spineCount);
  if (!epubChapters) {
    for (int i = 0; i < MAX_SPINE; i++) spineRefs[i].~String();
    free(spineRefs);
    for (int i = 0; i < MAX_MANIFEST; i++) manifest[i].~ManifestItem();
    free(manifest); epubCleanup(); f.close(); return false;
  }
  epubChapterCount = 0;
  size_t cumOffset = 0;

  for (int s = 0; s < spineCount; s++) {
    for (int m = 0; m < manifestCount; m++) {
      if (manifest[m].id == spineRefs[s] && manifest[m].isContent) {
        String fullPath = epubBasePath + manifest[m].href;
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
  }

  // Free spine and manifest — no longer needed
  for (int i = 0; i < MAX_SPINE; i++) spineRefs[i].~String();
  free(spineRefs);
  for (int i = 0; i < MAX_MANIFEST; i++) manifest[i].~ManifestItem();
  free(manifest);

  epubEstimatedTotalBytes = cumOffset;
  f.close();

  Serial.printf("EPUB: %d chapters, estimated total text: %u bytes\n",
                epubChapterCount, epubEstimatedTotalBytes);
  Serial.printf("EPUB: Free PSRAM after metadata: %u bytes\n", ESP.getFreePsram());

  if (epubChapterCount == 0) {
    epubCleanup();
    return false;
  }

  // Step 4: Load initial chapters into buffer
  if (!epubLoadChapterRange(0)) {
    epubCleanup();
    return false;
  }

  // Step 5: Detect image-based EPUB (manga/comics)
  // If most chapters contain mainly image markers (little real text), treat as image-based
  if (epubChapterCount >= 2) {
    int loadedChapters = 0;
    int imageOnlyChapters = 0;
    int chaptersWithImages = 0;
    for (int c = 0; c < epubChapterCount; c++) {
      if (epubChapters[c].actualTextSize > 0) {
        loadedChapters++;
        // Scan the chapter content: count real text chars vs image markers
        // Chapter content is in epubFullText at its cumulative offset
        size_t chStart = epubChapters[c].cumulativeOffset - epubLoadedBaseOffset;
        size_t chEnd = chStart + epubChapters[c].actualTextSize;
        if (chEnd > epubFullTextLen) chEnd = epubFullTextLen;
        int realTextChars = 0;
        bool hasImage = false;
        bool inMarker = false;
        for (size_t j = chStart; j < chEnd; j++) {
          char ch = epubFullText[j];
          if (ch == EPUB_IMG_MARKER) { hasImage = true; inMarker = !inMarker; continue; }
          if (inMarker) continue;  // skip image path chars
          if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t') realTextChars++;
        }
        if (hasImage) chaptersWithImages++;
        if (hasImage && realTextChars < 50) imageOnlyChapters++;
        if (c < 5 || realTextChars > 0)
          Serial.printf("  Ch %d: %d real chars, hasImage=%d\n", c+1, realTextChars, hasImage);
      }
    }
    float ratio = (loadedChapters > 0) ? (float)imageOnlyChapters / loadedChapters : 0.0f;
    float imgRatio = (loadedChapters > 0) ? (float)chaptersWithImages / loadedChapters : 0.0f;
    Serial.printf("EPUB detect: %d loaded, %d imgOnly, %d withImg, ratio=%.2f, imgRatio=%.2f\n",
                  loadedChapters, imageOnlyChapters, chaptersWithImages, ratio, imgRatio);
    if (ratio > 0.7f) {
      epubIsImageBased = true;
      Serial.printf("EPUB: Detected as IMAGE-BASED (manga) — %d/%d loaded chapters are image-only (total: %d)\n",
                    imageOnlyChapters, loadedChapters, epubChapterCount);
      
      // Check if any chapters have multiple images (non-standard layout)
      epubHasMultiImageChapters = false;
      for (int c = 0; c < epubChapterCount && !epubHasMultiImageChapters; c++) {
        if (epubChapters[c].actualTextSize > 0 && epubFullText) {
          size_t chStart = epubChapters[c].cumulativeOffset - epubLoadedBaseOffset;
          size_t chEnd = chStart + epubChapters[c].actualTextSize;
          if (chEnd > epubFullTextLen) chEnd = epubFullTextLen;
          int imgCount = 0;
          bool inM = false;
          for (size_t j = chStart; j < chEnd; j++) {
            if (epubFullText[j] == EPUB_IMG_MARKER) {
              if (!inM) imgCount++;
              inM = !inM;
            }
          }
          if (imgCount > 1) epubHasMultiImageChapters = true;
        }
      }
      if (epubHasMultiImageChapters)
        Serial.println("EPUB: WARNING — multi-image chapters detected, only first image per chapter shown");
      
      // Free the large text buffer — we'll load chapters one-at-a-time
      if (epubFullText) {
        free(epubFullText);
        epubFullText = nullptr;
        epubFullTextLen = 0;
      }
    }
  }

  return true;
}

// Load a range of chapters starting from startChapter into epubFullText.
// Allocates buffer using available PSRAM, loads as many chapters as fit.
// Returns true if at least one chapter was loaded.
bool epubLoadChapterRange(int startChapter) {
  if (!epubChapters || startChapter < 0 || startChapter >= epubChapterCount) return false;
  if (epubFilePath.isEmpty() || !epubZipEntries) return false;

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
    return false;
  }

  // Allocate text buffer: free PSRAM minus decompression headroom
  size_t bufferSize = freePsram - headroom;
  // Cap at 4MB to be conservative
  if (bufferSize > 4 * 1024 * 1024) bufferSize = 4 * 1024 * 1024;

  epubFullText = (char*)ps_malloc(bufferSize);
  if (!epubFullText) {
    Serial.println("EPUB: Failed to allocate text buffer");
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
    return false;
  }

  // Extract chapters into buffer
  int chaptersLoaded = 0;
  size_t runningOffset = epubLoadedBaseOffset;

  for (int c = startChapter; c < epubChapterCount; c++) {
    EpubChapterInfo& ch = epubChapters[c];
    ZipEntry& entry = epubZipEntries[ch.zipEntryIndex];

    Serial.printf("  Chapter %d/%d: %s (%u bytes)\n",
                  c + 1, epubChapterCount, entry.filename.c_str(), entry.uncompSize);

    // Check if we have enough buffer space (rough check)
    if (epubFullTextLen + 1024 >= bufferSize) {
      Serial.printf("  Buffer full at chapter %d\n", c + 1);
      break;
    }

    // Extract raw HTML
    size_t rawLen = 0;
    uint8_t* rawBuf = zipExtractFile(f, entry, rawLen);
    if (!rawBuf || rawLen == 0) {
      if (rawBuf) free(rawBuf);
      Serial.printf("  Skipping chapter %d (extraction failed)\n", c + 1);
      // Still update cumulative offset even if extraction fails
      if (ch.actualTextSize == 0) ch.actualTextSize = ch.estimatedTextSize;
      ch.cumulativeOffset = runningOffset;
      runningOffset += ch.actualTextSize;
      continue;
    }

    // Add chapter separator
    if (epubFullTextLen > 0 && epubFullTextLen < bufferSize - 2) {
      epubFullText[epubFullTextLen++] = '\n';
      epubFullText[epubFullTextLen++] = '\n';
    }

    size_t beforeLen = epubFullTextLen;

    // Strip HTML directly into buffer
    // Use the HTML file's own directory as basePath (not OPF directory)
    // so that relative image paths like "../images/cover.jpg" resolve correctly
    String chapterDir = pathDir(entry.filename);
    size_t remaining = bufferSize - epubFullTextLen - 1;
    size_t written = htmlStripDirect((const char*)rawBuf, rawLen,
                                     epubFullText + epubFullTextLen, remaining,
                                     chapterDir);
    free(rawBuf);

    if (written > 0) {
      epubFullTextLen += written;
      ch.actualTextSize = epubFullTextLen - beforeLen;
      chaptersLoaded++;
    } else {
      ch.actualTextSize = 0;
    }

    // Update cumulative offset with actual size
    ch.cumulativeOffset = runningOffset;
    runningOffset += ch.actualTextSize;

    // Update subsequent chapter offsets based on actual size vs estimate
    // (shift all following chapters' cumulative offsets)
    if (ch.actualTextSize != ch.estimatedTextSize && c + 1 < epubChapterCount) {
      size_t nextOffset = runningOffset;
      for (int j = c + 1; j < epubChapterCount; j++) {
        epubChapters[j].cumulativeOffset = nextOffset;
        nextOffset += (epubChapters[j].actualTextSize > 0) ?
                       epubChapters[j].actualTextSize :
                       epubChapters[j].estimatedTextSize;
      }
      epubEstimatedTotalBytes = nextOffset;
    }

    epubLoadedEndChapter = c + 1;

    // Check buffer nearly full
    if (epubFullTextLen >= bufferSize - 4096) {
      Serial.printf("  Buffer nearly full at chapter %d\n", c + 1);
      break;
    }

    yield();
  }

  f.close();
  epubFullText[epubFullTextLen] = '\0';

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

// Clean up all persistent EPUB data
void epubCleanup() {
  if (epubFullText) {
    free(epubFullText);
    epubFullText = nullptr;
    epubFullTextLen = 0;
  }
  if (epubZipEntries) {
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) epubZipEntries[i].~ZipEntry();
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
                             int quadrant, float zoomCenterX, float zoomCenterY) {
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
      if (scale < 1.0f) {
        scale_x = scale;
        scale_y = scale;
      }
      Serial.printf("EPUB IMG: Original %dx%d, scale=%.3f, output ~%dx%d\n",
                    imgW, imgH, scale_x, (int)(imgW * scale_x), (int)(imgH * scale_x));
    }
  } else {
    Serial.printf("EPUB IMG: Could not parse dimensions, using default scale\n");
  }

  // Center the scaled image in the available area (full view only)
  int drawX = x;
  int drawY = y;
  if (quadrant < 0 && gotDims && imgW > 0 && imgH > 0) {
    int outW = (int)(imgW * scale_x);
    int outH = (int)(imgH * (scale_y > 0 ? scale_y : scale_x));
    if (outW < maxW) drawX = x + (maxW - outW) / 2;
    if (outH < maxH) drawY = y + (maxH - outH) / 2;
  } else {
    drawX = x;
    drawY = y;
  }

  if (isJpeg) {
    drawn = M5.Display.drawJpg(imgBuf, imgLen, drawX, drawY, maxW, maxH, offX, offY, scale_x, scale_y);
  } else if (isPng) {
    drawn = M5.Display.drawPng(imgBuf, imgLen, drawX, drawY, maxW, maxH, offX, offY, scale_x, scale_y);
  } else if (isBmp) {
    drawn = M5.Display.drawBmp(imgBuf, imgLen, drawX, drawY, maxW, maxH, offX, offY, scale_x, scale_y);
  } else {
    // Try JPEG first (most common in EPUBs), then PNG
    drawn = M5.Display.drawJpg(imgBuf, imgLen, drawX, drawY, maxW, maxH, offX, offY, scale_x, scale_y);
    if (!drawn) drawn = M5.Display.drawPng(imgBuf, imgLen, drawX, drawY, maxW, maxH, offX, offY, scale_x, scale_y);
  }

  free(imgBuf);

  if (drawn) {
    Serial.printf("EPUB IMG: Drew %s successfully\n", imagePath.c_str());
  } else {
    Serial.printf("EPUB IMG: Failed to decode %s\n", imagePath.c_str());
  }
  return drawn;
}

// ==================== End ZIP / EPUB Reader ====================
