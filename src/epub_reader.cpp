#include "globals.h"

// ==================== ZIP / EPUB Reader ====================

// Little-endian binary readers
static uint16_t zipU16(const uint8_t* b) { return b[0] | (b[1] << 8); }
static uint32_t zipU32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24); }

#define MAX_ZIP_ENTRIES 200

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
    if (!compBuf) return nullptr;
    f.read(compBuf, entry.compSize);

    uint8_t* outBuf = (uint8_t*)ps_malloc(entry.uncompSize + 1);
    if (!outBuf) { free(compBuf); return nullptr; }

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

// Strip HTML/XHTML tags and convert entities to plain text
String htmlToText(const String& html) {
  // Pre-allocate with estimated size
  String text;
  text.reserve(html.length() / 2);
  bool inTag = false;
  bool inScript = false;   // Skip <script>/<style> content
  bool lastWasNewline = false;
  String tagName;

  for (int i = 0; i < (int)html.length(); i++) {
    char c = html.charAt(i);

    if (c == '<') {
      inTag = true;
      tagName = "";
      continue;
    }
    if (c == '>' && inTag) {
      inTag = false;
      tagName.toLowerCase();
      // Check for block elements → insert newline
      if (tagName.startsWith("script") || tagName.startsWith("style")) {
        inScript = true;
      } else if (tagName.startsWith("/script") || tagName.startsWith("/style")) {
        inScript = false;
      } else if (!inScript) {
        if (tagName.startsWith("/p") || tagName.startsWith("/div") ||
            tagName.startsWith("/h") || tagName.startsWith("br") ||
            tagName.startsWith("/li") || tagName.startsWith("/tr")) {
          if (!lastWasNewline) {
            text += '\n';
            lastWasNewline = true;
          }
        }
      }
      continue;
    }
    if (inTag) {
      if (tagName.length() < 20) tagName += c;
      continue;
    }
    if (inScript) continue;

    // Handle HTML entities
    if (c == '&') {
      // Look for entity end
      String entity = "&";
      int j = i + 1;
      while (j < (int)html.length() && j - i < 10 && html.charAt(j) != ';') {
        entity += html.charAt(j);
        j++;
      }
      if (j < (int)html.length() && html.charAt(j) == ';') {
        entity += ';';
        i = j; // Skip past entity
        if (entity == "&amp;") { text += '&'; lastWasNewline = false; }
        else if (entity == "&lt;") { text += '<'; lastWasNewline = false; }
        else if (entity == "&gt;") { text += '>'; lastWasNewline = false; }
        else if (entity == "&quot;") { text += '"'; lastWasNewline = false; }
        else if (entity == "&apos;") { text += '\''; lastWasNewline = false; }
        else if (entity == "&nbsp;") { text += ' '; lastWasNewline = false; }
        else if (entity.startsWith("&#")) {
          // Numeric entity — decode
          long code = 0;
          if (entity.charAt(2) == 'x' || entity.charAt(2) == 'X') {
            code = strtol(entity.c_str() + 3, nullptr, 16);
          } else {
            code = strtol(entity.c_str() + 2, nullptr, 10);
          }
          if (code > 0) {
            utf8Encode((uint32_t)code, text);
          }
          lastWasNewline = false;
        }
        // Unknown entity — skip
        continue;
      }
    }

    // Normal character
    if (c == '\n' || c == '\r') {
      if (!lastWasNewline) {
        text += '\n';
        lastWasNewline = true;
      }
    } else if (c == ' ' || c == '\t') {
      // Collapse whitespace
      if (text.length() > 0 && text.charAt(text.length() - 1) != ' ' && !lastWasNewline) {
        text += ' ';
      }
    } else {
      text += c;
      lastWasNewline = false;
    }
  }
  return text;
}

// Get directory part of a path (e.g., "OEBPS/content.opf" → "OEBPS/")
String pathDir(const String& path) {
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash >= 0) return path.substring(0, lastSlash + 1);
  return "";
}

// Extract title from EPUB's OPF metadata (lightweight, for book list display)
String epubGetTitle(const String& epubPath) {
  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubPath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubPath.c_str()); }
  if (!f) return "";

  ZipEntry* entries = (ZipEntry*)ps_malloc(sizeof(ZipEntry) * MAX_ZIP_ENTRIES);
  if (!entries) { f.close(); return ""; }
  for (int i = 0; i < MAX_ZIP_ENTRIES; i++) new (&entries[i]) ZipEntry();
  int entryCount = zipReadDirectory(f, entries, MAX_ZIP_ENTRIES);
  if (entryCount == 0) {
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
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
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return "";
  }

  // Read OPF and extract <dc:title>
  String opfContent = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == opfPath) { opfContent = zipExtractString(f, entries[i]); break; }
  }
  for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
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

// Open EPUB and extract all text into epubFullText (PSRAM buffer)
bool epubLoad(const String& epubPath) {
  Serial.printf("\n=== EPUB: Loading %s ===\n", epubPath.c_str());

  // Free previous EPUB buffer
  if (epubFullText) { free(epubFullText); epubFullText = nullptr; epubFullTextLen = 0; }

  File f;
  if (sdMutex) { xSemaphoreTake(sdMutex, portMAX_DELAY); f = SD.open(epubPath.c_str()); xSemaphoreGive(sdMutex); }
  else { f = SD.open(epubPath.c_str()); }
  if (!f) { Serial.println("EPUB: Cannot open file"); return false; }

  // Parse ZIP central directory
  ZipEntry* entries = (ZipEntry*)ps_malloc(sizeof(ZipEntry) * MAX_ZIP_ENTRIES);
  if (!entries) { f.close(); return false; }
  // CRITICAL: ps_malloc doesn't call constructors — must placement-new
  // so String members are properly initialized before use
  for (int i = 0; i < MAX_ZIP_ENTRIES; i++) {
    new (&entries[i]) ZipEntry();
  }
  int entryCount = zipReadDirectory(f, entries, MAX_ZIP_ENTRIES);
  Serial.printf("EPUB: ZIP has %d entries\n", entryCount);
  if (entryCount == 0) {
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return false;
  }

  // Step 1: Find container.xml → get OPF path
  String opfPath = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == "META-INF/container.xml") {
      String containerXml = zipExtractString(f, entries[i]);
      // Parse: <rootfile full-path="..." .../>
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
    // Fallback: look for any .opf file
    for (int i = 0; i < entryCount; i++) {
      if (entries[i].filename.endsWith(".opf")) { opfPath = entries[i].filename; break; }
    }
  }
  Serial.printf("EPUB: OPF path = %s\n", opfPath.c_str());
  if (opfPath.length() == 0) {
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return false;
  }

  String basePath = pathDir(opfPath);

  // Step 2: Read OPF → build manifest (id→href) and read spine order
  String opfContent = "";
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].filename == opfPath) {
      opfContent = zipExtractString(f, entries[i]);
      break;
    }
  }
  if (opfContent.length() == 0) {
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return false;
  }

  // Parse manifest: <item id="..." href="..." media-type="..."/>
  struct ManifestItem { String id; String href; bool isContent; };
  ManifestItem* manifest = (ManifestItem*)ps_malloc(sizeof(ManifestItem) * 200);
  if (!manifest) {
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return false;
  }
  for (int i = 0; i < 200; i++) {
    new (&manifest[i]) ManifestItem();
  }
  int manifestCount = 0;

  {
    int searchFrom = 0;
    while (searchFrom < (int)opfContent.length() && manifestCount < 200) {
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
  // Allocate on PSRAM to avoid stack overflow (200 String objects)
  String* spineRefs = (String*)ps_malloc(sizeof(String) * 200);
  if (!spineRefs) {
    for (int i = 0; i < 200; i++) manifest[i].~ManifestItem();
    free(manifest);
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close(); return false;
  }
  for (int i = 0; i < 200; i++) new (&spineRefs[i]) String();
  int spineCount = 0;
  {
    int spineStart = opfContent.indexOf("<spine");
    int spineEnd = opfContent.indexOf("</spine>");
    if (spineStart >= 0 && spineEnd > spineStart) {
      String spineSection = opfContent.substring(spineStart, spineEnd);
      int searchFrom = 0;
      while (searchFrom < (int)spineSection.length() && spineCount < 200) {
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

  // Step 3: Extract all chapters in spine order → concatenate text
  // First pass: estimate total size
  size_t estimatedSize = 0;
  for (int s = 0; s < spineCount; s++) {
    for (int m = 0; m < manifestCount; m++) {
      if (manifest[m].id == spineRefs[s] && manifest[m].isContent) {
        String fullPath = basePath + manifest[m].href;
        for (int z = 0; z < entryCount; z++) {
          if (entries[z].filename == fullPath) {
            estimatedSize += entries[z].uncompSize;
            break;
          }
        }
        break;
      }
    }
  }
  Serial.printf("EPUB: Estimated raw chapter size: %u bytes\n", estimatedSize);

  // Allocate PSRAM buffer (text after HTML stripping is usually smaller than raw HTML)
  epubFullText = (char*)ps_malloc(estimatedSize + 4096);
  if (!epubFullText) {
    Serial.println("EPUB: Failed to allocate text buffer");
    for (int i = 0; i < 200; i++) spineRefs[i].~String();
    free(spineRefs);
    for (int i = 0; i < 200; i++) manifest[i].~ManifestItem();
    free(manifest);
    for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
    free(entries); f.close();
    return false;
  }
  epubFullTextLen = 0;

  // Second pass: extract and strip each chapter
  int chaptersLoaded = 0;
  for (int s = 0; s < spineCount; s++) {
    for (int m = 0; m < manifestCount; m++) {
      if (manifest[m].id == spineRefs[s] && manifest[m].isContent) {
        String fullPath = basePath + manifest[m].href;
        for (int z = 0; z < entryCount; z++) {
          if (entries[z].filename == fullPath) {
            Serial.printf("  Chapter %d: %s (%u bytes)\n", chaptersLoaded + 1,
                          fullPath.c_str(), entries[z].uncompSize);

            String html = zipExtractString(f, entries[z]);
            if (html.length() > 0) {
              String text = htmlToText(html);
              html = "";  // Free HTML memory immediately

              if (text.length() > 0) {
                // Add chapter separator
                if (epubFullTextLen > 0) {
                  epubFullText[epubFullTextLen++] = '\n';
                  epubFullText[epubFullTextLen++] = '\n';
                }
                memcpy(epubFullText + epubFullTextLen, text.c_str(), text.length());
                epubFullTextLen += text.length();
                chaptersLoaded++;
              }
            }

            yield();  // Let system breathe
            break;
          }
        }
        break;
      }
    }
  }

  epubFullText[epubFullTextLen] = '\0';
  for (int i = 0; i < 200; i++) spineRefs[i].~String();
  free(spineRefs);
  for (int i = 0; i < 200; i++) manifest[i].~ManifestItem();
  free(manifest);
  for (int i = 0; i < MAX_ZIP_ENTRIES; i++) entries[i].~ZipEntry();
  free(entries);
  f.close();

  Serial.printf("EPUB: Loaded %d chapters, %u bytes of text\n", chaptersLoaded, epubFullTextLen);
  Serial.printf("  Free PSRAM: %u bytes\n", ESP.getFreePsram());

  return (chaptersLoaded > 0 && epubFullTextLen > 0);
}

// ==================== End ZIP / EPUB Reader ====================
