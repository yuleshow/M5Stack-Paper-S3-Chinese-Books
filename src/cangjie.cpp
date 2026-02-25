#include "globals.h"
#include "cangjie.h"

// ==================== Cangjie Dictionary Engine ====================

// The binary table loaded into PSRAM
static uint8_t* cjTable = nullptr;
static uint32_t cjEntryCount = 0;
static bool cjLoaded = false;

bool loadCangjieTable() {
  if (cjLoaded) return true;

  if (!sdCardAvailable) {
    Serial.println("Cangjie: SD card not available");
    return false;
  }

  ScopedSDLock lock;
  File f = SD.open("/cangjie5.bin", FILE_READ);
  if (!f) {
    Serial.println("Cangjie: /cangjie5.bin not found on SD");
    return false;
  }

  // Read and verify header
  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (memcmp(magic, "CJ5", 3) != 0) {
    Serial.println("Cangjie: bad magic in cangjie5.bin");
    f.close();
    return false;
  }

  uint32_t count;
  f.read((uint8_t*)&count, 4);
  Serial.printf("Cangjie: loading %u entries...\n", count);

  // Allocate in PSRAM (7 bytes per entry)
  size_t tableSize = (size_t)count * CJ_ENTRY_SIZE;
  cjTable = (uint8_t*)ps_malloc(tableSize);
  if (!cjTable) {
    Serial.printf("Cangjie: failed to allocate %u bytes in PSRAM\n", tableSize);
    f.close();
    return false;
  }

  // Read entire table
  size_t bytesRead = f.read(cjTable, tableSize);
  f.close();

  if (bytesRead != tableSize) {
    Serial.printf("Cangjie: read %u of %u bytes\n", bytesRead, tableSize);
    free(cjTable);
    cjTable = nullptr;
    return false;
  }

  cjEntryCount = count;
  cjLoaded = true;
  Serial.printf("Cangjie: loaded %u entries (%u KB)\n", count, tableSize / 1024);
  return true;
}

void freeCangjieTable() {
  if (cjTable) {
    free(cjTable);
    cjTable = nullptr;
  }
  cjEntryCount = 0;
  cjLoaded = false;
}

// Compare a code prefix against an entry's code field
// Returns: -1 if prefix < entry, 0 if prefix matches, 1 if prefix > entry
static int cjComparePrefix(const char* prefix, int prefixLen, const uint8_t* entry) {
  for (int i = 0; i < prefixLen; i++) {
    char ec = (char)entry[i];
    if (ec == 0) return 1;  // entry code is shorter than prefix
    if (prefix[i] < ec) return -1;
    if (prefix[i] > ec) return 1;
  }
  return 0;  // prefix matches
}

// Compare full code for exact match
static int cjCompareExact(const char* code, int codeLen, const uint8_t* entry) {
  for (int i = 0; i < 5; i++) {
    char cc = (i < codeLen) ? code[i] : 0;
    char ec = (char)entry[i];
    if (cc < ec) return -1;
    if (cc > ec) return 1;
  }
  return 0;
}

// Binary search for the first entry that matches the given code prefix
// Then scan forward to collect all matches
int cangjieSearch(const char* code, uint16_t* results, int maxResults) {
  if (!cjLoaded || !cjTable || cjEntryCount == 0) return 0;

  int codeLen = strlen(code);
  if (codeLen == 0 || codeLen > CJ_MAX_CODE_LEN) return 0;

  // Binary search for the first matching entry
  uint32_t lo = 0, hi = cjEntryCount;
  uint32_t firstMatch = cjEntryCount;  // sentinel

  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    const uint8_t* entry = cjTable + mid * CJ_ENTRY_SIZE;
    int cmp = cjComparePrefix(code, codeLen, entry);
    if (cmp <= 0) {
      if (cmp == 0) firstMatch = mid;
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }

  // Check if we found any match
  if (firstMatch == cjEntryCount) {
    // Check lo position
    if (lo < cjEntryCount) {
      const uint8_t* entry = cjTable + lo * CJ_ENTRY_SIZE;
      if (cjComparePrefix(code, codeLen, entry) == 0) {
        firstMatch = lo;
      }
    }
    if (firstMatch == cjEntryCount) return 0;
  }

  // Scan forward from firstMatch to collect results
  int count = 0;
  for (uint32_t i = firstMatch; i < cjEntryCount && count < maxResults; i++) {
    const uint8_t* entry = cjTable + i * CJ_ENTRY_SIZE;
    if (cjComparePrefix(code, codeLen, entry) != 0) break;

    // Extract UTF-16 codepoint (little-endian)
    uint16_t unicode = entry[5] | (entry[6] << 8);
    results[count++] = unicode;
  }

  return count;
}
