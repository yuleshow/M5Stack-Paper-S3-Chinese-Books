#include "globals.h"
#include "dictionary.h"

// ==================== Word Position Tracking ====================

WordPos engWordPositions[MAX_ENG_WORDS];
int engWordCount = 0;
char engWordPool[ENG_WORD_POOL_SIZE];
int engWordPoolLen = 0;

void engClearWords() {
  engWordCount = 0;
  engWordPoolLen = 0;
}

void engRecordWord(int x, int y, int w, int h, const char* word) {
  if (engWordCount >= MAX_ENG_WORDS) return;
  int len = strlen(word);
  if (engWordPoolLen + len + 1 > ENG_WORD_POOL_SIZE) return;

  WordPos& wp = engWordPositions[engWordCount];
  wp.x = x;
  wp.y = y;
  wp.w = w;
  wp.h = h;
  wp.poolOffset = engWordPoolLen;

  memcpy(&engWordPool[engWordPoolLen], word, len + 1);
  engWordPoolLen += len + 1;
  engWordCount++;
}

int engFindWordAt(int tx, int ty) {
  for (int i = 0; i < engWordCount; i++) {
    WordPos& wp = engWordPositions[i];
    if (tx >= wp.x && tx <= wp.x + wp.w &&
        ty >= wp.y && ty <= wp.y + wp.h) {
      return i;
    }
  }
  return -1;
}

// ==================== Dictionary Lookup ====================

// Binary search for exact word in dictionary file.
// File must be open; caller manages open/close.
static bool dictBinarySearch(File& f, uint32_t fileSize, const char* key,
                             char* definition, int maxLen) {
  uint32_t lo = 0, hi = fileSize;
  static char buf[512];

  for (int iter = 0; iter < 40 && lo < hi; iter++) {
    // When range is small, linear scan to avoid missing entries at boundaries
    if (hi - lo < 512) {
      f.seek(lo);
      while (f.available() && f.position() < hi + 256) {
        int ll = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
        if (ll <= 0) break;
        buf[ll] = '\0';
        if (ll > 0 && buf[ll - 1] == '\r') buf[--ll] = '\0';
        char* t = strchr(buf, '\t');
        if (!t) continue;
        *t = '\0';
        for (char* p = buf; *p; p++) { if (*p >= 'A' && *p <= 'Z') *p += 32; }
        int c = strcmp(key, buf);
        if (c == 0) {
          strncpy(definition, t + 1, maxLen - 1);
          definition[maxLen - 1] = '\0';
          return true;
        }
        if (c < 0) break;  // Past target alphabetically
      }
      return false;
    }

    uint32_t mid = lo + (hi - lo) / 2;
    f.seek(mid);

    // Skip to next line boundary (partial line after seek)
    if (mid > 0) {
      int skip = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
      if (skip <= 0 || !f.available()) { hi = mid; continue; }
    }

    uint32_t lineStart = f.position();
    if (lineStart >= fileSize) { hi = mid; continue; }

    int lineLen = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    if (lineLen <= 0) { hi = mid; continue; }
    buf[lineLen] = '\0';

    // Strip \r
    if (lineLen > 0 && buf[lineLen - 1] == '\r') buf[--lineLen] = '\0';

    // Find tab separator
    char* tab = strchr(buf, '\t');
    if (!tab) { lo = lineStart + lineLen + 1; continue; }
    *tab = '\0';

    // Lowercase dictionary word for comparison
    for (char* p = buf; *p; p++) {
      if (*p >= 'A' && *p <= 'Z') *p += 32;
    }

    int cmp = strcmp(key, buf);
    if (cmp == 0) {
      strncpy(definition, tab + 1, maxLen - 1);
      definition[maxLen - 1] = '\0';
      return true;
    } else if (cmp < 0) {
      hi = mid;
    } else {
      lo = lineStart + lineLen + 1;
    }
  }
  return false;
}

// Check if string ends with suffix
static bool endsWith(const char* s, int len, const char* suffix) {
  int slen = strlen(suffix);
  if (len < slen) return false;
  return strcmp(s + len - slen, suffix) == 0;
}

// Generate candidate base forms from an inflected English word.
// Tries common suffix rules for plurals, verb forms, comparatives, adverbs.
// Returns number of candidates written to candidates[] (max maxCandidates).
static int generateStems(const char* key, int klen, char candidates[][64], int maxCandidates) {
  int count = 0;
  auto addCandidate = [&](const char* stem, int stemLen) {
    if (stemLen <= 1 || stemLen > 62 || count >= maxCandidates) return;
    memcpy(candidates[count], stem, stemLen);
    candidates[count][stemLen] = '\0';
    // Deduplicate
    for (int i = 0; i < count; i++) {
      if (strcmp(candidates[i], candidates[count]) == 0) return;
    }
    count++;
  };
  auto addStem = [&](const char* base, int baseLen, const char* append) {
    char tmp[64];
    int appendLen = strlen(append);
    if (baseLen + appendLen > 62) return;
    memcpy(tmp, base, baseLen);
    memcpy(tmp + baseLen, append, appendLen);
    tmp[baseLen + appendLen] = '\0';
    addCandidate(tmp, baseLen + appendLen);
  };

  // -s / -es (plurals, 3rd person): books→book, watches→watch, buses→bus
  if (klen > 3 && endsWith(key, klen, "ies")) {
    // studies→study, cities→city
    addStem(key, klen - 3, "y");
  }
  if (klen > 3 && endsWith(key, klen, "ses")) {
    // buses→bus, cases→case
    addCandidate(key, klen - 2);  // bus
    addCandidate(key, klen - 1);  // buse→case (try with e)
  }
  if (klen > 4 && endsWith(key, klen, "ches")) {
    addCandidate(key, klen - 2);  // watch
  }
  if (klen > 4 && endsWith(key, klen, "shes")) {
    addCandidate(key, klen - 2);  // wish
  }
  if (klen > 3 && endsWith(key, klen, "xes")) {
    addCandidate(key, klen - 2);  // box
  }
  if (klen > 2 && key[klen-1] == 's' && key[klen-2] != 's') {
    addCandidate(key, klen - 1);  // books→book
  }

  // -ed (past tense): walked→walk, hoped→hope, studied→study, stopped→stop
  if (klen > 3 && endsWith(key, klen, "ied")) {
    addStem(key, klen - 3, "y");  // studied→study
  }
  if (klen > 3 && endsWith(key, klen, "ed")) {
    addCandidate(key, klen - 2);  // walked→walk
    addCandidate(key, klen - 1);  // hoped→hope (keep the e)
    // doubled consonant: stopped→stop, planned→plan
    if (klen > 4 && key[klen-3] == key[klen-4]) {
      addCandidate(key, klen - 3);  // stopped→stop
    }
  }

  // -ing (participle): running→run, making→make, lying→lie, dying→die
  if (klen > 4 && endsWith(key, klen, "ying")) {
    // dying→die, lying→lie
    addStem(key, klen - 4, "ie");
  }
  if (klen > 4 && endsWith(key, klen, "ing")) {
    addCandidate(key, klen - 3);  // walking→walk
    addStem(key, klen - 3, "e");  // making→make
    // doubled consonant: running→run, stopping→stop
    if (klen > 5 && key[klen-4] == key[klen-5]) {
      addCandidate(key, klen - 4);  // running→run
    }
  }

  // -er / -est (comparative/superlative): bigger→big, nicer→nice
  if (klen > 3 && endsWith(key, klen, "ier")) {
    addStem(key, klen - 3, "y");  // happier→happy
  }
  if (klen > 4 && endsWith(key, klen, "iest")) {
    addStem(key, klen - 4, "y");  // happiest→happy
  }
  if (klen > 3 && endsWith(key, klen, "er")) {
    addCandidate(key, klen - 2);  // smaller→small
    addCandidate(key, klen - 1);  // nicer→nice
    if (klen > 4 && key[klen-3] == key[klen-4]) {
      addCandidate(key, klen - 3);  // bigger→big
    }
  }
  if (klen > 4 && endsWith(key, klen, "est")) {
    addCandidate(key, klen - 3);  // smallest→small
    addCandidate(key, klen - 2);  // nicest→nice
    if (klen > 5 && key[klen-4] == key[klen-5]) {
      addCandidate(key, klen - 4);  // biggest→big
    }
  }

  // -ly (adverb): quickly→quick, happily→happy, simply→simple
  if (klen > 4 && endsWith(key, klen, "ily")) {
    addStem(key, klen - 3, "y");  // happily→happy
  }
  if (klen > 3 && endsWith(key, klen, "ly")) {
    addCandidate(key, klen - 2);  // quickly→quick
    addStem(key, klen - 2, "le");  // simply→simple
  }

  // -tion / -sion → -te / -se / -d / base: education→educate
  if (klen > 5 && endsWith(key, klen, "ation")) {
    addStem(key, klen - 5, "ate");  // education→educate
    addCandidate(key, klen - 5);    // education→educ (rare but try)
  }

  // -ness: happiness→happy, kindness→kind
  if (klen > 4 && endsWith(key, klen, "iness")) {
    addStem(key, klen - 5, "y");  // happiness→happy
  }
  if (klen > 4 && endsWith(key, klen, "ness")) {
    addCandidate(key, klen - 4);  // kindness→kind
  }

  // Common irregular past tenses / past participles
  struct { const char* form; const char* base; } irregulars[] = {
    {"shrunk", "shrink"}, {"shrank", "shrink"}, {"shrunken", "shrink"},
    {"spoke", "speak"}, {"spoken", "speak"},
    {"broke", "break"}, {"broken", "break"},
    {"chose", "choose"}, {"chosen", "choose"},
    {"froze", "freeze"}, {"frozen", "freeze"},
    {"stole", "steal"}, {"stolen", "steal"},
    {"wore", "wear"}, {"worn", "wear"},
    {"tore", "tear"}, {"torn", "tear"},
    {"bore", "bear"}, {"born", "bear"}, {"borne", "bear"},
    {"swore", "swear"}, {"sworn", "swear"},
    {"drove", "drive"}, {"driven", "drive"},
    {"rode", "ride"}, {"ridden", "ride"},
    {"wrote", "write"}, {"written", "write"},
    {"rose", "rise"}, {"risen", "rise"},
    {"gave", "give"}, {"given", "give"},
    {"took", "take"}, {"taken", "take"},
    {"shook", "shake"}, {"shaken", "shake"},
    {"fell", "fall"}, {"fallen", "fall"},
    {"knew", "know"}, {"known", "know"},
    {"grew", "grow"}, {"grown", "grow"},
    {"threw", "throw"}, {"thrown", "throw"},
    {"blew", "blow"}, {"blown", "blow"},
    {"drew", "draw"}, {"drawn", "draw"},
    {"flew", "fly"}, {"flown", "fly"},
    {"went", "go"}, {"gone", "go"},
    {"came", "come"},
    {"ran", "run"},
    {"saw", "see"}, {"seen", "see"},
    {"did", "do"}, {"done", "do"},
    {"had", "have"},
    {"was", "be"}, {"were", "be"}, {"been", "be"},
    {"said", "say"},
    {"told", "tell"},
    {"sold", "sell"},
    {"bought", "buy"},
    {"brought", "bring"},
    {"thought", "think"},
    {"caught", "catch"},
    {"taught", "teach"},
    {"fought", "fight"},
    {"sought", "seek"},
    {"felt", "feel"},
    {"left", "leave"},
    {"kept", "keep"},
    {"slept", "sleep"},
    {"swept", "sweep"},
    {"wept", "weep"},
    {"built", "build"},
    {"spent", "spend"},
    {"sent", "send"},
    {"lent", "lend"},
    {"bent", "bend"},
    {"lost", "lose"},
    {"stood", "stand"},
    {"understood", "understand"},
    {"held", "hold"},
    {"sat", "sit"},
    {"set", "set"},
    {"led", "lead"},
    {"read", "read"},
    {"fed", "feed"},
    {"met", "meet"},
    {"won", "win"},
    {"begun", "begin"}, {"began", "begin"},
    {"sung", "sing"}, {"sang", "sing"},
    {"rung", "ring"}, {"rang", "ring"},
    {"drunk", "drink"}, {"drank", "drink"},
    {"sunk", "sink"}, {"sank", "sink"},
    {"swum", "swim"}, {"swam", "swim"},
    {"hung", "hang"},
    {"dug", "dig"},
    {"stuck", "stick"},
    {"struck", "strike"},
    {"wound", "wind"},
    {"bound", "bind"},
    {"found", "find"},
    {"ground", "grind"},
    {"laid", "lay"},
    {"paid", "pay"},
    {"woken", "wake"}, {"woke", "wake"},
    {"hidden", "hide"}, {"hid", "hide"},
    {"bitten", "bite"}, {"bit", "bite"},
    {"eaten", "eat"}, {"ate", "eat"},
    {"forgiven", "forgive"}, {"forgave", "forgive"},
    {"forgotten", "forget"}, {"forgot", "forget"},
  };
  for (auto& ir : irregulars) {
    if (strcmp(key, ir.form) == 0) {
      int blen = strlen(ir.base);
      addCandidate(ir.base, blen);
      break;
    }
  }

  return count;
}

bool dictLookup(const char* word, char* definition, int maxLen) {
  // Normalize: lowercase, strip leading/trailing punctuation
  static char key[64];
  int klen = 0;
  for (int i = 0; word[i] && klen < 62; i++) {
    char c = word[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    key[klen++] = c;
  }
  key[klen] = '\0';
  // Strip trailing non-alpha
  while (klen > 0 && !((key[klen-1] >= 'a' && key[klen-1] <= 'z'))) klen--;
  // Strip leading non-alpha
  int start = 0;
  while (start < klen && !((key[start] >= 'a' && key[start] <= 'z'))) start++;
  if (start > 0) {
    memmove(key, key + start, klen - start);
    klen -= start;
  }
  key[klen] = '\0';
  if (klen == 0) return false;

  Serial.printf("DICT: looking up '%s'\n", key);

  // Hold SD lock for entire file access (open + all binary search reads + close)
  ScopedSDLock lock;
  File f = SD.open("/dict/en-zh.txt");
  if (!f) {
    Serial.println("DICT: /dict/en-zh.txt not found");
    return false;
  }

  uint32_t fileSize = f.size();
  if (fileSize == 0) { f.close(); return false; }

  // Try exact match first
  bool found = dictBinarySearch(f, fileSize, key, definition, maxLen);
  if (found) {
    Serial.printf("DICT: found '%s' (exact)\n", key);
    f.close();
    return true;
  }

  // Try stem forms (plurals, past tense, participles, etc.)
  static char candidates[16][64];
  int nCandidates = generateStems(key, klen, candidates, 16);
  for (int i = 0; i < nCandidates; i++) {
    if (dictBinarySearch(f, fileSize, candidates[i], definition, maxLen)) {
      Serial.printf("DICT: found '%s' via stem '%s'\n", key, candidates[i]);
      f.close();
      return true;
    }
  }

  f.close();
  Serial.printf("DICT: '%s' not found (%d stems tried)\n", key, nCandidates);
  return false;
}

// ==================== Dictionary Popup ====================

void drawDictPopup(const char* word, const char* definition) {
  const int cardW = 500, cardH = 560;
  const int cardX = (DISPLAY_WIDTH - cardW) / 2;
  const int cardY = 160;
  const int pad = 20;
  const int innerW = cardW - pad * 2;

  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();

  // Card background with double border
  M5.Display.fillRoundRect(cardX, cardY, cardW, cardH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(cardX, cardY, cardW, cardH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 11, TFT_BLACK);

  // Word title (large, using built-in font for English)
  int textY = cardY + pad;
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.drawString(word, cardX + pad, textY);
  textY += 50;

  // Divider line
  M5.Display.drawFastHLine(cardX + pad, textY, innerW, TFT_BLACK);
  textY += 15;

  // Check if this is a "not found" result — render as centered labels (fast)
  bool notFound = (strstr(definition, "查無此字") != nullptr);
  if (notFound) {
    // Render "查無此字" and "輕觸關閉" as single centered labels
    int centerX = DISPLAY_WIDTH / 2;
    int midY = cardY + cardH / 2 - 30;
    drawSystemTextCentered("查無此字", centerX, midY, 40);
    drawSystemTextCentered("輕觸關閉", centerX, midY + 60, 28);
  } else {
    // Definition: render with system font (supports CJK)
    // Draw character by character with line wrapping
    int defFontSize = 40;
    int lineH = defFontSize + 10;
    int maxY = cardY + cardH - pad - 40;  // Leave room for dismiss hint
    int curX = cardX + pad;

    const char* p = definition;
    while (*p && textY + lineH <= maxY) {
      // Decode one UTF-8 character
      int charBytes = 1;
      unsigned char uc = (unsigned char)*p;
      if      (uc >= 0xF0) charBytes = 4;
      else if (uc >= 0xE0) charBytes = 3;
      else if (uc >= 0xC0) charBytes = 2;

      // Build single-character string
      char ch[5] = {0};
      int actualBytes = 0;
      for (int i = 0; i < charBytes && p[i]; i++) {
        ch[i] = p[i];
        actualBytes++;
      }

      // Estimate character width
      int charW;
      if (charBytes > 1) {
        charW = defFontSize;       // CJK characters ≈ square
      } else if (*p == ' ') {
        charW = defFontSize / 3;
      } else {
        charW = defFontSize * 6 / 10;  // ASCII ≈ 60% of font size
      }

      // Line wrap
      if (curX + charW > cardX + cardW - pad) {
        curX = cardX + pad;
        textY += lineH;
        if (textY + lineH > maxY) break;
      }

      // Skip rendering spaces at line start
      if (*p == ' ' && curX == cardX + pad) {
        p += actualBytes;
        continue;
      }

      drawSystemText(ch, curX, textY, defFontSize);
      curX += charW;
      p += actualBytes;
    }

    // Dismiss hint at bottom
    drawSystemTextCentered("輕觸關閉", DISPLAY_WIDTH / 2, cardY + cardH - 38, 28);
  }

  M5.Display.endWrite();
  M5.Display.display();

  Serial.printf("DICT: popup shown for '%s'\n", word);
}
