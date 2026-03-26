#include "conv_table.h"
#include "globals.h"

// ==================== Chinese S2T/T2S Conversion ====================

// Binary table format (little-endian):
//   4 bytes: magic ("S2T\0" or "T2S\0")
//   4 bytes: uint32 entry count
//   count × 4 bytes: (uint16 source, uint16 target), sorted by source

struct ConvEntry {
  uint16_t src;
  uint16_t dst;
} __attribute__((packed));

static ConvEntry* s2tTable = nullptr;
static uint32_t   s2tCount = 0;
static ConvEntry* t2sTable = nullptr;
static uint32_t   t2sCount = 0;

static bool loadOneTable(const char* path, const char* expectedMagic,
                         ConvEntry*& table, uint32_t& count) {
  ScopedSDLock lock;
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("Conv: %s not found\n", path);
    return false;
  }

  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (memcmp(magic, expectedMagic, 3) != 0) {
    Serial.printf("Conv: bad magic in %s\n", path);
    f.close();
    return false;
  }

  f.read((uint8_t*)&count, 4);

  size_t tableSize = (size_t)count * sizeof(ConvEntry);
  table = (ConvEntry*)ps_malloc(tableSize);
  if (!table) {
    Serial.printf("Conv: failed to allocate %u bytes for %s\n", tableSize, path);
    f.close();
    count = 0;
    return false;
  }

  size_t bytesRead = f.read((uint8_t*)table, tableSize);
  f.close();

  if (bytesRead != tableSize) {
    Serial.printf("Conv: read %u of %u bytes from %s\n", bytesRead, tableSize, path);
    free(table);
    table = nullptr;
    count = 0;
    return false;
  }

  Serial.printf("Conv: loaded %s — %u entries (%u bytes)\n", path, count, tableSize);
  return true;
}

bool loadConvTables() {
  if (!sdCardAvailable) return false;
  bool ok1 = loadOneTable("/s2t.bin", "S2T", s2tTable, s2tCount);
  bool ok2 = loadOneTable("/t2s.bin", "T2S", t2sTable, t2sCount);
  return ok1 || ok2;  // at least one loaded
}

void freeConvTables() {
  if (s2tTable) { free(s2tTable); s2tTable = nullptr; }
  if (t2sTable) { free(t2sTable); t2sTable = nullptr; }
  s2tCount = t2sCount = 0;
}

// Binary search for a codepoint in sorted table
static uint16_t lookupConv(const ConvEntry* table, uint32_t count, uint16_t cp) {
  if (!table || count == 0) return cp;
  int lo = 0, hi = (int)count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (table[mid].src == cp) return table[mid].dst;
    if (table[mid].src < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return cp;  // not found → unchanged
}

void applyConversion(String &text, ConvMode mode) {
  if (mode == CONV_ORIGINAL) return;

  const ConvEntry* table;
  uint32_t count;
  if (mode == CONV_SIMPLIFIED) {
    table = t2sTable;
    count = t2sCount;
  } else {
    table = s2tTable;
    count = s2tCount;
  }
  if (!table || count == 0) return;

  // Process UTF-8 text character by character
  String result;
  result.reserve(text.length());
  int pos = 0;
  int len = text.length();
  while (pos < len) {
    unsigned char c = (unsigned char)text.charAt(pos);
    if (c < 0x80) {
      // ASCII — pass through
      result += (char)c;
      pos++;
    } else if (c < 0xE0) {
      // 2-byte UTF-8 — not CJK, pass through
      result += text.substring(pos, pos + 2);
      pos += 2;
    } else if (c < 0xF0) {
      // 3-byte UTF-8 — potentially CJK
      if (pos + 2 >= len) { result += text.substring(pos); break; }
      uint16_t cp = ((c & 0x0F) << 12) |
                    ((text.charAt(pos + 1) & 0x3F) << 6) |
                    (text.charAt(pos + 2) & 0x3F);
      // Only lookup CJK range characters
      if (cp >= 0x3400) {
        uint16_t mapped = lookupConv(table, count, cp);
        if (mapped != cp) {
          // Encode mapped codepoint back to UTF-8
          result += (char)(0xE0 | (mapped >> 12));
          result += (char)(0x80 | ((mapped >> 6) & 0x3F));
          result += (char)(0x80 | (mapped & 0x3F));
        } else {
          result += text.substring(pos, pos + 3);
        }
      } else {
        result += text.substring(pos, pos + 3);
      }
      pos += 3;
    } else {
      // 4-byte UTF-8 — beyond BMP, pass through
      int charLen = 4;
      if (pos + charLen > len) charLen = len - pos;
      result += text.substring(pos, pos + charLen);
      pos += charLen;
    }
  }
  text = result;
}
