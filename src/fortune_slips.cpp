// Fortune slip images sourced from www.chance.org.tw
#include "globals.h"
#include "labels/label_bitmaps.h"
#include "utf8_utils.h"
#include <esp_random.h>
#include <math.h>

// IMU shake detection parameters
static const float SHAKE_THRESHOLD = 2.5f;   // acceleration magnitude threshold (in G)
static const int   SHAKE_COUNT_NEEDED = 3;    // number of threshold crossings needed
static const unsigned long SHAKE_WINDOW_MS = 1500; // time window for detecting shakes
static const unsigned long SHAKE_COOLDOWN_MS = 500; // cooldown after a shake event

static int   shakeCount = 0;
static unsigned long shakeWindowStart = 0;
static unsigned long lastShakeTime = 0;
static bool  aboveThreshold = false;

// Fortune slip binary file format:
// Header: 4 bytes magic "FSLP"
//         2 bytes count (uint16_t LE)
//         2 bytes image width (uint16_t LE)
//         2 bytes image height (uint16_t LE)
//         2 bytes flags (uint16_t LE)
// Index:  count * 4 bytes - offset of each JPEG blob (uint32_t LE)
//         count * 4 bytes - size of each JPEG blob (uint32_t LE)
// Data:   concatenated JPEG blobs
//
// Combined pack format (FSPK):
// Header: 4 bytes magic "FSPK"
//         2 bytes num_categories (uint16_t LE)
//         2 bytes reserved
// TOC:    N * (4 bytes offset + 4 bytes size) per category
// Data:   concatenated FSLP blocks

static const char* FORTUNE_PACK_FILE = "/fortune_slips/fortune_slips.bin";

static const char* FORTUNE_LABELS[] = {
  "觀音靈籖",
  "淺草寺靈籖"
};

// Cached FSPK category offsets
static uint32_t categoryBaseOffset[2] = {0, 0};
static uint32_t categoryBlockSize[2] = {0, 0};
static bool packHeaderLoaded = false;

static bool loadPackHeader() {
  if (packHeaderLoaded) return true;
  File f = SD.open(FORTUNE_PACK_FILE, FILE_READ);
  if (!f) {
    Serial.printf("FSPK: Cannot open %s\n", FORTUNE_PACK_FILE);
    return false;
  }
  Serial.printf("FSPK: Opened %s (%u bytes)\n", FORTUNE_PACK_FILE, (uint32_t)f.size());
  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (magic[0] != 'F' || magic[1] != 'S' || magic[2] != 'P' || magic[3] != 'K') {
    Serial.printf("FSPK: Bad magic: %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
    f.close();
    return false;
  }
  uint16_t numCats = 0;
  f.read((uint8_t*)&numCats, 2);
  f.seek(8); // skip reserved
  Serial.printf("FSPK: %d categories\n", numCats);
  for (int i = 0; i < numCats && i < 2; i++) {
    f.read((uint8_t*)&categoryBaseOffset[i], 4);
    f.read((uint8_t*)&categoryBlockSize[i], 4);
    Serial.printf("FSPK: cat[%d] offset=%u size=%u\n", i, categoryBaseOffset[i], categoryBlockSize[i]);
  }
  f.close();
  packHeaderLoaded = true;
  return true;
}

// Read slip count from a category's FSLP block
static int getSlipCount(int category) {
  if (!loadPackHeader() || category < 0 || category > 1) return 0;
  uint32_t base = categoryBaseOffset[category];
  File f = SD.open(FORTUNE_PACK_FILE, FILE_READ);
  if (!f) return 0;
  f.seek(base);
  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (magic[0] != 'F' || magic[1] != 'S' || magic[2] != 'L' || magic[3] != 'P') {
    f.close();
    return 0;
  }
  uint16_t count = 0;
  f.read((uint8_t*)&count, 2);
  f.close();
  return count;
}

void drawFortuneSlipsMenu() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();

  // Title
  drawSystemText("求籖", 20, 30, 36);

  // Option 1: 觀音靈籖
  int optY = 200;
  int optH = 100;
  int optW = 460;
  int optX = 40;

  M5.Display.fillRoundRect(optX, optY, optW, optH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(optX, optY, optW, optH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(optX + 1, optY + 1, optW - 2, optH - 2, 11, TFT_BLACK);
  drawSystemTextCentered("觀音靈籖", optX + optW / 2, optY + optH / 2 - 18, 36);

  // Option 2: 淺草寺靈籖
  optY = 340;
  M5.Display.fillRoundRect(optX, optY, optW, optH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(optX, optY, optW, optH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(optX + 1, optY + 1, optW - 2, optH - 2, 11, TFT_BLACK);
  drawSystemTextCentered("淺草寺靈籖", optX + optW / 2, optY + optH / 2 - 18, 36);

  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}

void drawFortuneShakeScreen() {
  // Reset shake detection state
  shakeCount = 0;
  shakeWindowStart = 0;
  lastShakeTime = 0;
  aboveThreshold = false;

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);

  // Try to display cover image from .bin file
  bool coverShown = false;
  if (fortuneSlipCategory >= 0 && fortuneSlipCategory <= 1 && loadPackHeader()) {
    uint32_t base = categoryBaseOffset[fortuneSlipCategory];
    File f = SD.open(FORTUNE_PACK_FILE, FILE_READ);
    if (f) {
      f.seek(base);
      // Read header: magic(4) + count(2) + w(2) + h(2) + flags(2)
      char magic[4];
      f.read((uint8_t*)magic, 4);
      if (magic[0] == 'F' && magic[1] == 'S' && magic[2] == 'L' && magic[3] == 'P') {
        uint16_t count = 0, imgW = 0, imgH = 0, flags = 0;
        f.read((uint8_t*)&count, 2);
        f.read((uint8_t*)&imgW, 2);
        f.read((uint8_t*)&imgH, 2);
        f.read((uint8_t*)&flags, 2);
        if (flags & 1) {
          // Cover index is at offset 12 + count*8
          f.seek(base + 12 + count * 4 * 2);
          uint32_t coverOffset = 0, coverSize = 0;
          f.read((uint8_t*)&coverOffset, 4);
          f.read((uint8_t*)&coverSize, 4);
          if (coverSize > 0 && coverSize < 2000000) {
            uint8_t* buf = (uint8_t*)ps_malloc(coverSize);
            if (!buf) buf = (uint8_t*)malloc(coverSize);
            if (buf) {
              f.seek(base + coverOffset);
              f.read(buf, coverSize);
              M5.Display.drawJpg(buf, coverSize, 0, 0);
              free(buf);
              coverShown = true;
            }
          }
        }
      }
      f.close();
    }
  }

  if (!coverShown) {
    // Fallback: text-only shake screen
    M5.Display.setTextColor(TFT_BLACK);
    drawStatusBar();
    const char* label = FORTUNE_LABELS[fortuneSlipCategory];
    drawSystemTextCentered(label, DISPLAY_WIDTH / 2, 120, 36);
  }

  // Draw instruction overlay in motto-style frame
  if (fortuneSlipCategory == 0) {
    // Kuanyin: horizontal layout at bottom
    // Labels at 64pt are ~255x62, two lines with gap => total ~134px
    int textBlockH = 62 + 10 + 62;  // line1_h + gap + line2_h = 134
    int cardW = 420, cardH = textBlockH + 50;  // padding top/bottom 25px each
    int cardX = (DISPLAY_WIDTH - cardW) / 2;
    int cardY = DISPLAY_HEIGHT - cardH - 60;

    M5.Display.fillRoundRect(cardX, cardY, cardW, cardH, 12, TFT_WHITE);
    M5.Display.drawRoundRect(cardX, cardY, cardW, cardH, 12, EPD_DARK_GRAY);
    M5.Display.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 11, EPD_DARK_GRAY);
    // Decorative vertical lines
    M5.Display.drawLine(cardX + 12, cardY + 15, cardX + 12, cardY + cardH - 15, EPD_LIGHT_GRAY);
    M5.Display.drawLine(cardX + cardW - 12, cardY + 15, cardX + cardW - 12, cardY + cardH - 15, EPD_LIGHT_GRAY);

    int topY = cardY + (cardH - textBlockH) / 2;
    drawSystemTextCentered("誠心祝禱", cardX + cardW / 2, topY, 64);
    drawSystemTextCentered("輕搖求籖", cardX + cardW / 2, topY + 62 + 10, 64);
  } else {
    // Senso-ji: vertical right-to-left layout, centered
    int charSize = 64;
    int charH = 68;   // rendered char height (~62) + spacing
    int colW = 72;    // column width
    int nChars = 4;   // characters per column
    int textH = nChars * charH;
    int cardW = colW * 2 + 70;  // two columns + padding
    int cardH = textH + 60;     // text height + top/bottom padding
    int cardX = (DISPLAY_WIDTH - cardW) / 2;
    int cardY = (DISPLAY_HEIGHT - cardH) / 2;

    M5.Display.fillRoundRect(cardX, cardY, cardW, cardH, 12, TFT_WHITE);
    M5.Display.drawRoundRect(cardX, cardY, cardW, cardH, 12, EPD_DARK_GRAY);
    M5.Display.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 11, EPD_DARK_GRAY);
    // Decorative horizontal lines
    M5.Display.drawLine(cardX + 15, cardY + 12, cardX + cardW - 15, cardY + 12, EPD_LIGHT_GRAY);
    M5.Display.drawLine(cardX + 15, cardY + cardH - 12, cardX + cardW - 15, cardY + cardH - 12, EPD_LIGHT_GRAY);

    // Right-to-left: right column = "誠心祝禱", left column = "輕搖求籖"
    const char* rightCol[] = {"誠", "心", "祝", "禱"};
    const char* leftCol[]  = {"輕", "搖", "求", "籖"};
    int startY = cardY + (cardH - textH) / 2;
    int rightX = cardX + cardW / 2 + colW / 2 - charSize / 2 + 2;
    int leftX  = cardX + cardW / 2 - colW / 2 - charSize / 2 - 2;
    for (int i = 0; i < nChars; i++) {
      int cellTop = startY + i * charH;
      // Align both columns to the same bottom within each row
      const LabelBitmap* rLbl = findLabelBitmap(rightCol[i], charSize);
      const LabelBitmap* lLbl = findLabelBitmap(leftCol[i], charSize);
      int rH = rLbl ? rLbl->h : charSize;
      int lH = lLbl ? lLbl->h : charSize;
      int maxH = (rH > lH) ? rH : lH;
      int offY = charH - maxH;  // bottom-align using the tallest char
      drawSystemText(rightCol[i], rightX, cellTop + offY, charSize);
      drawSystemText(leftCol[i],  leftX,  cellTop + offY, charSize);
    }
  }

  M5.Display.endWrite();
  M5.Display.display();

  Serial.println("Fortune shake screen shown - waiting for shake...");
}

void pollFortuneShake() {
  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.getAccelData(&ax, &ay, &az)) return;

  unsigned long now = millis();

  // Cooldown after last shake event
  if (lastShakeTime > 0 && (now - lastShakeTime < SHAKE_COOLDOWN_MS)) return;

  // Calculate acceleration magnitude (subtract 1G gravity)
  float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  float deviation = fabsf(magnitude - 1.0f);

  bool nowAbove = (deviation > (SHAKE_THRESHOLD - 1.0f));

  // Detect rising edge (below → above threshold)
  if (nowAbove && !aboveThreshold) {
    if (shakeCount == 0 || (now - shakeWindowStart < SHAKE_WINDOW_MS)) {
      if (shakeCount == 0) shakeWindowStart = now;
      shakeCount++;
      Serial.printf("Shake detected: count=%d, deviation=%.2f\n", shakeCount, deviation);

      if (shakeCount >= SHAKE_COUNT_NEEDED) {
        // Shake gesture recognized!
        Serial.println("Shake gesture recognized - drawing fortune slip!");
        shakeCount = 0;
        lastShakeTime = now;
        lastActivityTime = now;

        // Read slip count from bin file
        int count = getSlipCount(fortuneSlipCategory);
        if (count <= 0) count = 100;
        fortuneSlipNumber = esp_random() % count;
        currentMode = MODE_FORTUNE_SLIP_VIEW;
        drawFortuneSlip();
        return;
      }
    } else {
      // Window expired, restart
      shakeCount = 1;
      shakeWindowStart = now;
    }
  }
  aboveThreshold = nowAbove;
}

void drawFortuneSlip() {
  if (fortuneSlipCategory < 0 || fortuneSlipCategory > 1) return;

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);

  if (!loadPackHeader()) {
    drawSystemTextCentered("無法開啟籖檔", M5.Display.width() / 2, M5.Display.height() / 2 - 20, 28);
    drawSystemTextCentered("請將 fortune_slips.bin 放至 SD 卡", M5.Display.width() / 2, M5.Display.height() / 2 + 20, 22, EPD_MID_GRAY);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  uint32_t base = categoryBaseOffset[fortuneSlipCategory];

  File f = SD.open(FORTUNE_PACK_FILE, FILE_READ);
  if (!f) {
    drawSystemTextCentered("無法開啟籖檔", M5.Display.width() / 2, M5.Display.height() / 2 - 20, 28);
    drawSystemTextCentered("請先轉換並上傳 .bin 檔案", M5.Display.width() / 2, M5.Display.height() / 2 + 20, 22, EPD_MID_GRAY);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  // Read header
  f.seek(base);
  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (magic[0] != 'F' || magic[1] != 'S' || magic[2] != 'L' || magic[3] != 'P') {
    f.close();
    drawSystemTextCentered("籖檔格式錯誤", M5.Display.width() / 2, M5.Display.height() / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  uint16_t count = 0, imgW = 0, imgH = 0;
  f.read((uint8_t*)&count, 2);
  f.read((uint8_t*)&imgW, 2);
  f.read((uint8_t*)&imgH, 2);
  f.seek(base + 12);  // skip flags

  if (count == 0) {
    f.close();
    drawSystemTextCentered("籖檔為空", M5.Display.width() / 2, M5.Display.height() / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  // Clamp slip number
  int slipIdx = fortuneSlipNumber % count;

  // Read offset and size for this slip from index
  // Index starts at byte 12: count * 4 bytes offsets, then count * 4 bytes sizes
  uint32_t offset = 0, size = 0;
  f.seek(base + 12 + slipIdx * 4);
  f.read((uint8_t*)&offset, 4);
  f.seek(base + 12 + count * 4 + slipIdx * 4);
  f.read((uint8_t*)&size, 4);

  if (size == 0 || size > 2000000) {
    f.close();
    drawSystemTextCentered("籖圖資料異常", M5.Display.width() / 2, M5.Display.height() / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  // Read JPEG data
  uint8_t* buf = (uint8_t*)ps_malloc(size);
  if (!buf) buf = (uint8_t*)malloc(size);
  if (!buf) {
    f.close();
    drawSystemTextCentered("記憶體不足", M5.Display.width() / 2, M5.Display.height() / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  f.seek(base + offset);
  size_t bytesRead = f.read(buf, size);
  f.close();

  if (bytesRead != size) {
    free(buf);
    drawSystemTextCentered("讀取籖圖失敗", M5.Display.width() / 2, M5.Display.height() / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  // Images are pre-sized to DISPLAY_WIDTH x DISPLAY_HEIGHT by the converter
  M5.Display.drawJpg(buf, size, 0, 0);
  free(buf);

  M5.Display.endWrite();
  M5.Display.display();
  Serial.printf("Fortune slip displayed: category=%d, slip=%d/%d\n", fortuneSlipCategory, slipIdx + 1, count);
}

// ==================== Fortune Slip Wording Display ====================

// Read wording image JPEG for a given slip (image mode: fields_per_slip == 0).
// Returns true if image data was read successfully. Caller must free outData.
static bool readWordingImage(int slipIdx, uint8_t*& outData, uint32_t& outSize) {
  outData = nullptr;
  outSize = 0;
  if (fortuneSlipCategory < 0 || fortuneSlipCategory > 1) return false;
  if (!loadPackHeader()) return false;

  uint32_t base = categoryBaseOffset[fortuneSlipCategory];
  File f = SD.open(FORTUNE_PACK_FILE, FILE_READ);
  if (!f) return false;

  f.seek(base);
  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (magic[0] != 'F' || magic[1] != 'S' || magic[2] != 'L' || magic[3] != 'P') {
    f.close();
    return false;
  }

  uint16_t count = 0, imgW = 0, imgH = 0, flags = 0;
  f.read((uint8_t*)&count, 2);
  f.read((uint8_t*)&imgW, 2);
  f.read((uint8_t*)&imgH, 2);
  f.read((uint8_t*)&flags, 2);

  if (!(flags & 2) || slipIdx < 0 || slipIdx >= count) {
    f.close();
    return false;
  }

  uint32_t wordingIndexPos = 12 + count * 8;
  if (flags & 1) wordingIndexPos += 8;

  f.seek(base + wordingIndexPos);
  uint32_t wordingBlockOffset = 0;
  uint16_t fieldsPer = 0, reserved = 0;
  f.read((uint8_t*)&wordingBlockOffset, 4);
  f.read((uint8_t*)&fieldsPer, 2);
  f.read((uint8_t*)&reserved, 2);

  if (fieldsPer != 0) {
    f.close();
    return false;  // Not image mode
  }

  // Image mode: wording block = [count * 4 offsets][count * 4 sizes][JPEG data]
  f.seek(base + wordingBlockOffset + slipIdx * 4);
  uint32_t imgOffset = 0;
  f.read((uint8_t*)&imgOffset, 4);

  f.seek(base + wordingBlockOffset + count * 4 + slipIdx * 4);
  uint32_t imgSize = 0;
  f.read((uint8_t*)&imgSize, 4);

  if (imgSize == 0 || imgSize > 2000000) {
    f.close();
    return false;
  }

  uint8_t* buf = (uint8_t*)ps_malloc(imgSize);
  if (!buf) buf = (uint8_t*)malloc(imgSize);
  if (!buf) { f.close(); return false; }

  f.seek(base + imgOffset);
  size_t bytesRead = f.read(buf, imgSize);
  f.close();

  if (bytesRead != imgSize) {
    free(buf);
    return false;
  }

  outData = buf;
  outSize = imgSize;
  return true;
}

// Read wording fields for a given slip from the FSLP binary (flags bit 1 = has wording).
// Returns a heap-allocated array of field strings. Caller must delete[] result.
// numFields is set to the field count. Returns nullptr on error.
static String* readWordingFields(int slipIdx, int& numFields) {
  numFields = 0;
  if (fortuneSlipCategory < 0 || fortuneSlipCategory > 1) return nullptr;
  if (!loadPackHeader()) return nullptr;

  uint32_t base = categoryBaseOffset[fortuneSlipCategory];
  uint32_t blockSize = categoryBlockSize[fortuneSlipCategory];
  File f = SD.open(FORTUNE_PACK_FILE, FILE_READ);
  if (!f) return nullptr;

  // Read FSLP header
  f.seek(base);
  char magic[4];
  f.read((uint8_t*)magic, 4);
  if (magic[0] != 'F' || magic[1] != 'S' || magic[2] != 'L' || magic[3] != 'P') {
    f.close();
    return nullptr;
  }

  uint16_t count = 0, imgW = 0, imgH = 0, flags = 0;
  f.read((uint8_t*)&count, 2);
  f.read((uint8_t*)&imgW, 2);
  f.read((uint8_t*)&imgH, 2);
  f.read((uint8_t*)&flags, 2);

  if (!(flags & 2) || slipIdx < 0 || slipIdx >= count) {
    f.close();
    return nullptr;
  }

  // Seek to wording index: after header(12) + slip index(count*8) + cover index(8 if bit0)
  uint32_t wordingIndexPos = 12 + count * 8;
  if (flags & 1) wordingIndexPos += 8; // skip cover index

  f.seek(base + wordingIndexPos);
  uint32_t wordingBlockOffset = 0;
  uint16_t fieldsPer = 0, reserved = 0;
  f.read((uint8_t*)&wordingBlockOffset, 4);
  f.read((uint8_t*)&fieldsPer, 2);
  f.read((uint8_t*)&reserved, 2);

  if (fieldsPer == 0) {
    f.close();
    return nullptr;
  }

  // Read this slip's wording sub-offset from the wording block index
  f.seek(base + wordingBlockOffset + slipIdx * 4);
  uint32_t dataOffset = 0;
  f.read((uint8_t*)&dataOffset, 4);

  // Determine data length from next offset or end of block
  uint32_t nextOffset;
  if (slipIdx + 1 < count) {
    f.read((uint8_t*)&nextOffset, 4);
  } else {
    nextOffset = blockSize;
  }

  uint32_t dataLen = nextOffset - dataOffset;
  if (dataLen == 0 || dataLen > 100000) {
    f.close();
    return nullptr;
  }

  char* buf = (char*)ps_malloc(dataLen + 1);
  if (!buf) buf = (char*)malloc(dataLen + 1);
  if (!buf) { f.close(); return nullptr; }

  f.seek(base + dataOffset);
  f.read((uint8_t*)buf, dataLen);
  buf[dataLen] = '\0';
  f.close();

  // Parse null-terminated strings
  numFields = fieldsPer;
  String* result = new String[fieldsPer];
  int pos = 0;
  for (int i = 0; i < fieldsPer && pos < (int)dataLen; i++) {
    result[i] = String(&buf[pos]);
    pos += strlen(&buf[pos]) + 1;
  }
  free(buf);
  return result;
}

// Draw horizontally-wrapped Chinese text. Returns y after last line.
static int drawWrappedText(const char* text, int startX, int y, int maxW,
                           int fontSize, uint16_t color = TFT_BLACK, int maxY = 870) {
  String str(text);
  int lineH = fontSize + 6;
  int charsPerLine = maxW / fontSize;
  if (charsPerLine < 1) charsPerLine = 1;

  int pos = 0;
  while (pos < (int)str.length()) {
    int lineStart = pos;
    int charCount = 0;
    while (pos < (int)str.length() && charCount < charsPerLine) {
      unsigned char c = str.charAt(pos);
      pos += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
      charCount++;
    }
    if (y > maxY) break; // don't overrun nav bar
    String line = str.substring(lineStart, pos);
    drawSystemText(line.c_str(), startX, y, fontSize, color);
    y += lineH;
    delay(1);
  }
  return y;
}

void drawFortuneSlipWording() {
  // Try image mode first (pre-rendered wording page)
  uint8_t* imgData = nullptr;
  uint32_t imgSize = 0;
  if (readWordingImage(fortuneSlipNumber, imgData, imgSize)) {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.startWrite();
    M5.Display.drawJpg(imgData, imgSize, 0, 0);
    free(imgData);
    drawStatusBar();
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    Serial.printf("Fortune wording image displayed: slip=%d\n", fortuneSlipNumber + 1);
    return;
  }

  // Fall through to text rendering mode
  int numFields = 0;
  String* fields = readWordingFields(fortuneSlipNumber, numFields);

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  drawStatusBar();

  if (!fields || numFields < 4) {
    drawSystemTextCentered("無法讀取籖文", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    if (fields) delete[] fields;
    return;
  }

  if (fortuneSlipCategory == 1) {
    // ---- Senso-ji wording layout ----
    // Fields: 0=籤號, 1=等級, 2=詩曰, 3=詩意,
    //         4=願望, 5=疾病, 6=遺失物, 7=盼望的人, 8=蓋新居搬家, 9=結婚交往, 10=旅行

    // --- Title: 籤號 + 等級 ---
    String title = fields[0] + "  " + fields[1];
    drawSystemTextCentered(title.c_str(), DISPLAY_WIDTH / 2, 45, 32);
    M5.Display.drawLine(40, 82, 500, 82, EPD_LIGHT_GRAY);

    // --- Poem: vertical columns, right-to-left, Kai font ---
    String poemLines[8];
    int lineCount = 0;
    {
      const String& poem = fields[2];
      int start = 0;
      for (int i = 0; i <= (int)poem.length() && lineCount < 8; i++) {
        if (i == (int)poem.length() || poem.charAt(i) == '\n') {
          if (i > start) poemLines[lineCount++] = poem.substring(start, i);
          start = i + 1;
        }
      }
    }

    int maxChars = 0;
    for (int i = 0; i < lineCount; i++) {
      int cnt = 0, j = 0;
      while (j < (int)poemLines[i].length()) {
        unsigned char c = poemLines[i].charAt(j);
        j += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        cnt++;
      }
      if (cnt > maxChars) maxChars = cnt;
    }
    if (maxChars < 1) maxChars = 7;

    int charSize = 48;
    int charSpacing = 52;
    int colSpacing = 56;
    int poemH = maxChars * charSpacing;
    int poemW = lineCount * colSpacing;
    int poemStartY = 88;
    int poemLeftEdge = (DISPLAY_WIDTH - poemW) / 2;

    // Decorative frame
    int frameX = poemLeftEdge - 18;
    int frameY = poemStartY - 12;
    int frameW = poemW + 36;
    int frameH = poemH + 24;
    M5.Display.drawRoundRect(frameX, frameY, frameW, frameH, 8, EPD_LIGHT_GRAY);

    // Load Kai font for poem
    String prevFontFile = currentFontFile;
    bool prevOfrLoaded = ofrFontLoaded;
    bool kaiFontLoaded = false;
    if (sdCardAvailable) {
      kaiFontLoaded = loadTTFFont("/fonts/TW-Kai-98_1.ttf", charSize);
    }
    delay(1);

    for (int col = 0; col < lineCount; col++) {
      int cx = poemLeftEdge + poemW - colSpacing / 2 - col * colSpacing;
      int j = 0, row = 0;
      while (j < (int)poemLines[col].length()) {
        String ch = utf8ExtractChar(poemLines[col], j);
        {
          int tmp = 0;
          uint32_t cp = utf8Decode(ch, tmp);
          uint32_t mapped = toVerticalPunct(cp);
          if (mapped != cp) { ch = ""; utf8Encode(mapped, ch); }
        }
        int cy = poemStartY + row * charSpacing + charSpacing / 2;
        if (kaiFontLoaded) {
          ofr.setFontSize(charSize);
          ofr.setFontColor(TFT_BLACK, TFT_WHITE);
          ofr.cdrawString(ch.c_str(), cx, cy, TFT_BLACK, TFT_WHITE);
        } else {
          drawSystemText(ch.c_str(), cx - charSize / 2, cy - charSize / 2, charSize);
        }
        row++;
      }
      delay(1);
    }

    // Restore font state
    if (kaiFontLoaded && prevOfrLoaded && prevFontFile.length() > 0) {
      loadTTFFont(prevFontFile.c_str(), 30);
    }
    delay(1);

    // --- 詩意 section ---
    int sectionY = poemStartY + poemH + 20;
    M5.Display.drawLine(40, sectionY, 500, sectionY, EPD_LIGHT_GRAY);
    sectionY += 8;
    drawSystemText("【詩意】", 30, sectionY, 26, EPD_DARK_GRAY);
    sectionY += 32;
    sectionY = drawWrappedText(fields[3].c_str(), 30, sectionY, 480, 26);
    sectionY += 4;
    delay(1);

    // --- 聖意 grid (sensoji categories) ---
    if (numFields > 4) {
      M5.Display.drawLine(40, sectionY, 500, sectionY, EPD_LIGHT_GRAY);
      sectionY += 8;

      static const char* sensLabels[] = {
        "願望", "疾病", "遺失物", "盼望的人", "蓋新居搬家",
        "結婚交往", "旅行"
      };
      int catCount = 7;
      int gridX = 30;
      int cellH = 32;

      for (int i = 0; i < catCount && (4 + i) < numFields; i++) {
        int y = sectionY + i * cellH;
        if (y > 855) break;
        const String& val = fields[4 + i];
        if (val.length() > 0) {
          String cell = String(sensLabels[i]) + "：" + val;
          drawSystemText(cell.c_str(), gridX, y, 24, TFT_BLACK);
        }
      }
    }

  } else {
    // ---- Kuanyin wording layout (original) ----
    // Field indices: 0=籤號, 1=等級, 2=宮位, 3=詩曰一, 4=詩意, 5=解曰,
    //                6=故事, 7=故事內容, 8-22=聖意

    if (numFields < 8) {
      drawSystemTextCentered("無法讀取籖文", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 28);
      drawReturnButton();
      M5.Display.endWrite();
      M5.Display.display();
      delete[] fields;
      return;
    }

    // --- Title: 籤號 + 等級 + 宮位 ---
    String title = fields[0] + "  " + fields[1] + "  " + fields[2];
    drawSystemTextCentered(title.c_str(), DISPLAY_WIDTH / 2, 45, 32);
    M5.Display.drawLine(40, 82, 500, 82, EPD_LIGHT_GRAY);

    // --- Poem: vertical columns, right-to-left, Kai font ---
    String poemLines[8];
    int lineCount = 0;
    {
      const String& poem = fields[3];
      int start = 0;
      for (int i = 0; i <= (int)poem.length() && lineCount < 8; i++) {
        if (i == (int)poem.length() || poem.charAt(i) == '\n') {
          if (i > start) poemLines[lineCount++] = poem.substring(start, i);
          start = i + 1;
        }
      }
    }

    int maxChars = 0;
    for (int i = 0; i < lineCount; i++) {
      int cnt = 0, j = 0;
      while (j < (int)poemLines[i].length()) {
        unsigned char c = poemLines[i].charAt(j);
        j += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        cnt++;
      }
      if (cnt > maxChars) maxChars = cnt;
    }
    if (maxChars < 1) maxChars = 7;

    int charSize = 48;
    int charSpacing = 52;
    int colSpacing = 56;
    int poemH = maxChars * charSpacing;
    int poemW = lineCount * colSpacing;
    int poemStartY = 88;
    int poemLeftEdge = (DISPLAY_WIDTH - poemW) / 2;

    // Decorative frame
    int frameX = poemLeftEdge - 18;
    int frameY = poemStartY - 12;
    int frameW = poemW + 36;
    int frameH = poemH + 24;
    M5.Display.drawRoundRect(frameX, frameY, frameW, frameH, 8, EPD_LIGHT_GRAY);

    // Load Kai font for poem
    String prevFontFile = currentFontFile;
    bool prevOfrLoaded = ofrFontLoaded;
    bool kaiFontLoaded = false;
    if (sdCardAvailable) {
      kaiFontLoaded = loadTTFFont("/fonts/TW-Kai-98_1.ttf", charSize);
    }
    delay(1);

    for (int col = 0; col < lineCount; col++) {
      int cx = poemLeftEdge + poemW - colSpacing / 2 - col * colSpacing;
      int j = 0, row = 0;
      while (j < (int)poemLines[col].length()) {
        String ch = utf8ExtractChar(poemLines[col], j);
        {
          int tmp = 0;
          uint32_t cp = utf8Decode(ch, tmp);
          uint32_t mapped = toVerticalPunct(cp);
          if (mapped != cp) { ch = ""; utf8Encode(mapped, ch); }
        }
        int cy = poemStartY + row * charSpacing + charSpacing / 2;
        if (kaiFontLoaded) {
          ofr.setFontSize(charSize);
          ofr.setFontColor(TFT_BLACK, TFT_WHITE);
          ofr.cdrawString(ch.c_str(), cx, cy, TFT_BLACK, TFT_WHITE);
        } else {
          drawSystemText(ch.c_str(), cx - charSize / 2, cy - charSize / 2, charSize);
        }
        row++;
      }
      delay(1);
    }

    // Restore font state
    if (kaiFontLoaded && prevOfrLoaded && prevFontFile.length() > 0) {
      loadTTFFont(prevFontFile.c_str(), 30);
    }
    delay(1);

    // --- 詩意 section ---
    int sectionY = poemStartY + poemH + 20;
    M5.Display.drawLine(40, sectionY, 500, sectionY, EPD_LIGHT_GRAY);
    sectionY += 8;
    drawSystemText("【詩意】", 30, sectionY, 26, EPD_DARK_GRAY);
    sectionY += 32;
    sectionY = drawWrappedText(fields[4].c_str(), 30, sectionY, 480, 26);
    sectionY += 4;
    delay(1);

    // --- 解曰 section ---
    M5.Display.drawLine(40, sectionY, 500, sectionY, EPD_LIGHT_GRAY);
    sectionY += 8;
    drawSystemText("【解曰】", 30, sectionY, 26, EPD_DARK_GRAY);
    sectionY += 32;
    sectionY = drawWrappedText(fields[5].c_str(), 30, sectionY, 480, 26);
    sectionY += 4;
    delay(1);

    // --- 聖意 grid ---
    if (numFields > 8) {
      M5.Display.drawLine(40, sectionY, 500, sectionY, EPD_LIGHT_GRAY);
      sectionY += 8;

      static const char* sacredLabels[] = {
        "家宅", "自身", "求財", "交易", "婚姻",
        "六甲", "行人", "田蠶", "六畜", "尋人",
        "訟詞", "移徙", "失物", "疾病", "山墳"
      };
      int gridCols = 3;
      int gridRows = 5;
      int cellW = 170;
      int cellH = 32;
      int gridX = 30;

      for (int i = 0; i < 15 && (8 + i) < numFields; i++) {
        int gc = i / gridRows;
        int gr = i % gridRows;
        int x = gridX + gc * cellW;
        int y = sectionY + gr * cellH;
        if (y > 855) break;
        const String& val = fields[8 + i];
        if (val.length() > 0) {
          String cell = String(sacredLabels[i]) + "：" + val;
          drawSystemText(cell.c_str(), x, y, 24, TFT_BLACK);
        }
      }
    }
  }

  drawReturnButton();
  M5.Display.endWrite();
  M5.Display.display();

  delete[] fields;
  Serial.printf("Fortune wording displayed: slip=%d\n", fortuneSlipNumber + 1);
}

void drawFortuneSlipStory() {
  Serial.println("drawFortuneSlipStory: start");
  delay(1);

  int numFields = 0;
  String* fields = readWordingFields(fortuneSlipNumber, numFields);
  delay(1);

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  drawStatusBar();

  if (!fields || numFields < 8) {
    drawSystemTextCentered("無法讀取故事", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 28);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    if (fields) delete[] fields;
    return;
  }

  // Field 6 = 故事 (title), Field 7 = 故事內容 (body)
  const String& storyTitle = fields[6];
  const String& storyBody  = fields[7];

  // --- Title ---
  drawSystemTextCentered(storyTitle.c_str(), DISPLAY_WIDTH / 2, 45, 32);
  M5.Display.drawLine(40, 82, 500, 82, EPD_LIGHT_GRAY);
  delay(1);

  // --- Story body with wrapping (use system font to avoid reloading 52MB Kai) ---
  drawWrappedText(storyBody.c_str(), 30, 95, 480, 28);

  drawReturnButton();
  M5.Display.endWrite();
  M5.Display.display();

  delete[] fields;
  Serial.printf("Fortune story displayed: slip=%d\n", fortuneSlipNumber + 1);
}
