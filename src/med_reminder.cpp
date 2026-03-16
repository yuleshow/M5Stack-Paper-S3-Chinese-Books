#include "globals.h"

// ==================== Tools Menu ====================

void drawToolsMenu() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();

  // Title
  drawSystemText("工具", 20, 30, 36);

  // Option 1: 壁紙
  int optY = 200;
  int optH = 100;
  int optW = 460;
  int optX = 40;

  M5.Display.fillRoundRect(optX, optY, optW, optH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(optX, optY, optW, optH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(optX + 1, optY + 1, optW - 2, optH - 2, 11, TFT_BLACK);
  drawSystemTextCentered("壁紙", optX + optW / 2, optY + optH / 2 - 18, 36);

  // Option 2: 吃藥提醒器
  optY = 330;
  M5.Display.fillRoundRect(optX, optY, optW, optH, 12, TFT_WHITE);
  M5.Display.drawRoundRect(optX, optY, optW, optH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(optX + 1, optY + 1, optW - 2, optH - 2, 11, TFT_BLACK);
  drawSystemTextCentered("吃藥提醒器", optX + optW / 2, optY + optH / 2 - 18, 36);

  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}

// ==================== Medication Reminder ====================

void drawMedReminder() {
  // Auto-reset check
  if (medReminderPressTime != 0) {
    time_t now = time(NULL);
    if (now > 1000000000 && (now - medReminderPressTime > (time_t)MED_REMINDER_RESET_SEC)) {
      medReminderPressTime = 0;
      prefs.begin("m5paper", false);
      prefs.putLong("medTime", 0);
      prefs.end();
    }
  }

  bool taken = (medReminderPressTime != 0);

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();

  // Title
  drawSystemText("吃藥提醒器", 20, 30, 36);

  // Big button area
  int btnX = 70, btnY = 300, btnW = 400, btnH = 350;
  int btnCX = btnX + btnW / 2;
  int radius = 20;

  if (taken) {
    // Taken state: filled dark button with white text
    M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, radius, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    drawSystemTextCentered("已吃藥", btnCX, btnY + btnH / 2 - 60, 48);
    M5.Display.setTextColor(TFT_WHITE);

    // Show elapsed time
    time_t now = time(NULL);
    long elapsed = (long)(now - medReminderPressTime);  // seconds
    if (elapsed < 0) elapsed = 0;
    int hours = elapsed / 3600;
    int mins  = (elapsed % 3600) / 60;
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%d小時%d分鐘前", hours, mins);
    drawSystemTextCentered(timeStr, btnCX, btnY + btnH / 2 + 20, 28);

    // Show remaining time until auto-reset
    long remaining = (long)MED_REMINDER_RESET_SEC - elapsed;
    if (remaining < 0) remaining = 0;
    int rHours = remaining / 3600;
    int rMins  = (remaining % 3600) / 60;
    char resetStr[32];
    snprintf(resetStr, sizeof(resetStr), "%d小時%d分後復位", rHours, rMins);
    drawSystemTextCentered(resetStr, btnCX, btnY + btnH / 2 + 70, 22);

    // Small reset button below big button
    M5.Display.setTextColor(TFT_BLACK);
    int rstX = 150, rstY = 670, rstW = 240, rstH = 50;
    M5.Display.drawRoundRect(rstX, rstY, rstW, rstH, 8, TFT_BLACK);
    drawSystemTextCentered("手動復位", rstX + rstW / 2, rstY + 10, 22);
  } else {
    // Not taken state: outlined button, waiting
    M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, radius, TFT_BLACK);
    M5.Display.drawRoundRect(btnX + 1, btnY + 1, btnW - 2, btnH - 2, radius - 1, TFT_BLACK);
    M5.Display.drawRoundRect(btnX + 2, btnY + 2, btnW - 4, btnH - 4, radius - 2, TFT_BLACK);

    drawSystemTextCentered("未吃藥", btnCX, btnY + btnH / 2 - 40, 48);
    drawSystemTextCentered("吃藥後請按此", btnCX, btnY + btnH / 2 + 30, 28);
  }

  M5.Display.setTextColor(TFT_BLACK);

  // Instructions at top
  drawSystemTextCentered("吃藥時按一下按鈕", btnCX, 180, 24);
  drawSystemTextCentered("忘了是否吃過，看一眼就知道", btnCX, 215, 22);

  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}

// ==================== Passcode Load/Save ====================

void loadMedPasscode() {
  // Med passcode is now loaded from [medicine] section in config.ini
  // by loadWiFiConfig(). Nothing to do here.
}

void saveMedPasscode() {
  // Save to config.ini (rewrites whole file including all sections)
  saveWiFiConfig();
}

// ==================== Passcode Keyboard ====================

// Constants for keypad layout
static const int KP_X0 = 70, KP_Y0 = 350, KP_KW = 120, KP_KH = 100, KP_GAP = 10;
static const int KP_DISP_X = 70, KP_DISP_Y = 180, KP_DISP_W = 400, KP_DISP_H = 70;
static const int KP_MSG_Y = 270;  // message line below input display

// Fast partial update: only redraws the input dots and message area
void updateMedPasscodeInput() {
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();

  // Clear input display area
  M5.Display.fillRect(KP_DISP_X + 2, KP_DISP_Y + 2, KP_DISP_W - 4, KP_DISP_H - 4, TFT_WHITE);

  // Draw dots for each entered digit
  int dotRadius = 12;
  int maxDots = 8;
  int n = medPasscodeInput.length();
  if (n > 0) {
    int dotsStartX = KP_DISP_X + KP_DISP_W / 2 - (n * 30) / 2 + 15;
    for (int i = 0; i < n && i < maxDots; i++) {
      M5.Display.fillCircle(dotsStartX + i * 30, KP_DISP_Y + KP_DISP_H / 2, dotRadius, TFT_BLACK);
    }
  }

  M5.Display.endWrite();
  M5.Display.display();
}

void drawMedPasscode() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();

  // Title — different text depending on mode
  if (medSettingNewPasscode) {
    if (medPasscodeFirst.length() == 0) {
      drawSystemText("設定新密碼", 20, 30, 36);
      drawSystemTextCentered("請輸入新的數字密碼", 270, KP_MSG_Y, 22);
    } else {
      drawSystemText("確認密碼", 20, 30, 36);
      drawSystemTextCentered("請再輸入一次", 270, KP_MSG_Y, 22);
    }
  } else {
    drawSystemText("輸入密碼", 20, 30, 36);
  }

  // Input display box
  M5.Display.drawRoundRect(KP_DISP_X, KP_DISP_Y, KP_DISP_W, KP_DISP_H, 8, TFT_BLACK);
  M5.Display.drawRoundRect(KP_DISP_X + 1, KP_DISP_Y + 1, KP_DISP_W - 2, KP_DISP_H - 2, 7, TFT_BLACK);

  // Draw dots for current input
  int dotRadius = 12;
  int maxDots = 8;
  int n = medPasscodeInput.length();
  if (n > 0) {
    int dotsStartX = KP_DISP_X + KP_DISP_W / 2 - (n * 30) / 2 + 15;
    for (int i = 0; i < n && i < maxDots; i++) {
      M5.Display.fillCircle(dotsStartX + i * 30, KP_DISP_Y + KP_DISP_H / 2, dotRadius, TFT_BLACK);
    }
  }

  // Numeric keypad: 3x4 grid
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int kx = KP_X0 + col * (KP_KW + KP_GAP);
      int ky = KP_Y0 + row * (KP_KH + KP_GAP);

      if (row == 3 && col == 0) {
        // Backspace
        M5.Display.fillRoundRect(kx, ky, KP_KW, KP_KH, 10, 0xC618);
        M5.Display.drawRoundRect(kx, ky, KP_KW, KP_KH, 10, TFT_BLACK);
        M5.Display.setTextColor(TFT_BLACK);
        // Use Font2 for "X" symbol (backspace)
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextSize(2);
        const char* bsLabel = "X";
        int tw = M5.Display.textWidth(bsLabel);
        int th = M5.Display.fontHeight();
        M5.Display.setCursor(kx + (KP_KW - tw) / 2, ky + (KP_KH - th) / 2);
        M5.Display.print(bsLabel);
        M5.Display.setTextSize(1);
      } else if (row == 3 && col == 2) {
        // Confirm
        M5.Display.fillRoundRect(kx, ky, KP_KW, KP_KH, 10, TFT_BLACK);
        M5.Display.setTextColor(TFT_WHITE);
        drawSystemTextCentered("OK", kx + KP_KW / 2, ky + KP_KH / 2 - 16, 32);
      } else {
        // Normal digit key
        M5.Display.fillRoundRect(kx, ky, KP_KW, KP_KH, 10, TFT_WHITE);
        M5.Display.drawRoundRect(kx, ky, KP_KW, KP_KH, 10, TFT_BLACK);
        M5.Display.drawRoundRect(kx + 1, ky + 1, KP_KW - 2, KP_KH - 2, 9, TFT_BLACK);
        M5.Display.setTextColor(TFT_BLACK);

        int digit = (row == 3 && col == 1) ? 0 : row * 3 + col + 1;
        char digitStr[2] = {(char)('0' + digit), '\0'};
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextSize(2);
        int tw = M5.Display.textWidth(digitStr);
        int th = M5.Display.fontHeight();
        M5.Display.setCursor(kx + (KP_KW - tw) / 2, ky + (KP_KH - th) / 2);
        M5.Display.print(digitStr);
        M5.Display.setTextSize(1);
      }
    }
  }

  M5.Display.setTextColor(TFT_BLACK);

  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}
