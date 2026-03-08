// Fortune slip images sourced from www.chance.org.tw
#include "globals.h"
#include "labels/label_bitmaps.h"
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
//         2 bytes reserved
// Index:  count * 4 bytes - offset of each JPEG blob (uint32_t LE)
//         count * 4 bytes - size of each JPEG blob (uint32_t LE)
// Data:   concatenated JPEG blobs

static const char* FORTUNE_BIN_FILES[] = {
  "/fortune_slips/kuanyin.bin",
  "/fortune_slips/sensoji.bin"
};

static const char* FORTUNE_LABELS[] = {
  "觀音靈籖",
  "淺草寺靈籖"
};

// Read slip count from a .bin file header
static int getSlipCount(const char* binPath) {
  File f = SD.open(binPath, FILE_READ);
  if (!f) return 0;
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
  if (fortuneSlipCategory >= 0 && fortuneSlipCategory <= 1) {
    const char* binPath = FORTUNE_BIN_FILES[fortuneSlipCategory];
    File f = SD.open(binPath, FILE_READ);
    if (f) {
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
          f.seek(12 + count * 4 * 2);
          uint32_t coverOffset = 0, coverSize = 0;
          f.read((uint8_t*)&coverOffset, 4);
          f.read((uint8_t*)&coverSize, 4);
          if (coverSize > 0 && coverSize < 2000000) {
            uint8_t* buf = (uint8_t*)ps_malloc(coverSize);
            if (!buf) buf = (uint8_t*)malloc(coverSize);
            if (buf) {
              f.seek(coverOffset);
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
      // Center each character vertically within its cell
      const LabelBitmap* rLbl = findLabelBitmap(rightCol[i], charSize);
      int rOffY = rLbl ? (charH - rLbl->h) / 2 : 0;
      drawSystemText(rightCol[i], rightX, cellTop + rOffY, charSize);
      const LabelBitmap* lLbl = findLabelBitmap(leftCol[i], charSize);
      int lOffY = lLbl ? (charH - lLbl->h) / 2 : 0;
      drawSystemText(leftCol[i],  leftX,  cellTop + lOffY, charSize);
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
        int count = getSlipCount(FORTUNE_BIN_FILES[fortuneSlipCategory]);
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

  const char* binPath = FORTUNE_BIN_FILES[fortuneSlipCategory];

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);

  File f = SD.open(binPath, FILE_READ);
  if (!f) {
    drawSystemTextCentered("無法開啟籖檔", M5.Display.width() / 2, M5.Display.height() / 2 - 20, 28);
    drawSystemTextCentered("請先轉換並上傳 .bin 檔案", M5.Display.width() / 2, M5.Display.height() / 2 + 20, 22, EPD_MID_GRAY);
    drawReturnButton();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  // Read header
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
  f.seek(12);  // skip reserved 2 bytes

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
  f.seek(12 + slipIdx * 4);
  f.read((uint8_t*)&offset, 4);
  f.seek(12 + count * 4 + slipIdx * 4);
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

  f.seek(offset);
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
