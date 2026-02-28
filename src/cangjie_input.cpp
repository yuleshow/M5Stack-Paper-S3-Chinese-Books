#include "globals.h"
#include "cangjie.h"

// ==================== Cangjie Input UI ====================

// Cangjie input state
char cjInputCode[CJ_MAX_CODE_LEN + 1] = "";  // Current typed code (a-z)
int cjInputLen = 0;
uint16_t cjCandidates[CJ_MAX_CANDIDATES];
int cjCandidateCount = 0;
int cjCandidatePage = 0;
String cjComposedText = "";         // The composed Chinese text so far
Mode cjReturnMode = MODE_TODO_LIST; // Where to return after input
int cjReturnPage = 0;              // Page to return to

// Convert a single UTF-16 codepoint to a UTF-8 String
static String unicodeToUTF8(uint16_t cp) {
  char buf[4];
  if (cp < 0x80) {
    buf[0] = (char)cp;
    buf[1] = 0;
  } else if (cp < 0x800) {
    buf[0] = 0xC0 | (cp >> 6);
    buf[1] = 0x80 | (cp & 0x3F);
    buf[2] = 0;
  } else {
    buf[0] = 0xE0 | (cp >> 12);
    buf[1] = 0x80 | ((cp >> 6) & 0x3F);
    buf[2] = 0x80 | (cp & 0x3F);
    buf[3] = 0;
  }
  return String(buf);
}

void drawCangjieInput() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

  // === Title bar ===
  drawSystemText("倉頡輸入", 10, 8, 28, TFT_BLACK, TFT_WHITE);

  // === Composed text display area (Y=40..130) ===
  M5.Display.drawRect(10, 42, 520, 90, TFT_BLACK);
  if (cjComposedText.length() > 0) {
    // Draw composed text with OFR (horizontal for display)
    if (ofrFontLoaded) {
      ofr.setFontSize(32);
      ofr.setCursor(18, 56);
      ofr.setDrawer(M5.Display);
      // Draw up to what fits in the box
      ofr.printf("%s", cjComposedText.c_str());
    } else {
      drawSystemText(cjComposedText.c_str(), 18, 56, 28, TFT_BLACK, TFT_WHITE);
    }
  } else {
    // Placeholder
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
    M5.Display.setCursor(18, 76);
    M5.Display.print("Type Cangjie code...");
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }

  // === Input code display (Y=140..180) ===
  M5.Display.fillRect(10, 140, 520, 42, TFT_LIGHTGRAY);
  M5.Display.drawRect(10, 140, 520, 42, TFT_BLACK);

  // Show typed Cangjie code with root labels
  if (cjInputLen > 0) {
    int drawX = 18;
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);

    for (int i = 0; i < cjInputLen; i++) {
      int keyIdx = cjInputCode[i] - 'a';
      if (keyIdx >= 0 && keyIdx < 26) {
        // Draw the Cangjie root character
        if (ofrFontLoaded) {
          ofr.setFontSize(26);
          ofr.setCursor(drawX, 146);
          ofr.setDrawer(M5.Display);
          ofr.printf("%s", CJ_KEY_LABELS[keyIdx]);
        } else {
          drawSystemText(CJ_KEY_LABELS[keyIdx], drawX, 146, 24, TFT_BLACK, TFT_LIGHTGRAY);
        }
        drawX += 30;
      }
    }

    // Also show the raw code letters
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_LIGHTGRAY);
    M5.Display.setCursor(drawX + 10, 155);
    M5.Display.print(cjInputCode);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }

  // === Candidate bar (Y=190..250) ===
  M5.Display.drawRect(10, 190, 520, 60, TFT_BLACK);

  if (cjCandidateCount > 0) {
    int startIdx = cjCandidatePage * CJ_CANDIDATES_PER_PAGE;
    int endIdx = min(startIdx + CJ_CANDIDATES_PER_PAGE, cjCandidateCount);
    int cellW = 520 / CJ_CANDIDATES_PER_PAGE;  // = 65px each

    for (int i = startIdx; i < endIdx; i++) {
      int col = i - startIdx;
      int cx = 10 + col * cellW;

      // Draw candidate character
      String ch = unicodeToUTF8(cjCandidates[i]);
      if (ofrFontLoaded) {
        ofr.setFontSize(32);
        ofr.setDrawer(M5.Display);
        // Center in cell
        ofr.setCursor(cx + 16, 196);
        ofr.printf("%s", ch.c_str());
      } else {
        drawSystemText(ch.c_str(), cx + 8, 196, 28, TFT_BLACK, TFT_WHITE);
      }

      // Draw number label (1-8)
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
      M5.Display.setCursor(cx + 4, 230);
      M5.Display.printf("%d", col + 1);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

      // Separator line
      if (col > 0) {
        M5.Display.drawFastVLine(cx, 192, 56, TFT_LIGHTGRAY);
      }
    }

    // Page indicator
    int totalPages = (cjCandidateCount + CJ_CANDIDATES_PER_PAGE - 1) / CJ_CANDIDATES_PER_PAGE;
    if (totalPages > 1) {
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
      M5.Display.setCursor(440, 255);
      M5.Display.printf("%d/%d", cjCandidatePage + 1, totalPages);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
  } else if (cjInputLen > 0) {
    // No matches
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
    M5.Display.setCursor(200, 212);
    M5.Display.print("No match");
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }

  // Candidate page nav arrows (shown if multi-page)
  {
    int totalPages = (cjCandidateCount + CJ_CANDIDATES_PER_PAGE - 1) / CJ_CANDIDATES_PER_PAGE;
    if (totalPages > 1) {
      // Left arrow (prev candidates)
      if (cjCandidatePage > 0) {
        M5.Display.fillTriangle(15, 265, 30, 258, 30, 272, TFT_BLACK);
      }
      // Right arrow (next candidates)
      if (cjCandidatePage < totalPages - 1) {
        M5.Display.fillTriangle(525, 265, 510, 258, 510, 272, TFT_BLACK);
      }
    }
  }

  // === QWERTY Keyboard with Cangjie labels (Y=290...) ===
  const char* rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
  int keyW = 48;
  int keyH = 68;
  int keySpacing = 6;
  int startY = 290;

  for (int row = 0; row < 3; row++) {
    int numKeys = strlen(rows[row]);
    int rowWidth = numKeys * keyW + (numKeys - 1) * keySpacing;
    int startX = (DISPLAY_WIDTH - rowWidth) / 2;
    int rowY = startY + row * (keyH + keySpacing);

    for (int i = 0; i < numKeys; i++) {
      int kx = startX + i * (keyW + keySpacing);
      char keyChar = rows[row][i];
      int keyIdx = keyChar - 'a';

      // Key background
      M5.Display.fillRect(kx, rowY, keyW, keyH, TFT_LIGHTGRAY);
      M5.Display.drawRect(kx, rowY, keyW, keyH, TFT_BLACK);

      // Cangjie root label (top of key)
      if (keyIdx >= 0 && keyIdx < 26) {
        if (ofrFontLoaded) {
          ofr.setFontSize(20);
          ofr.setDrawer(M5.Display);
          ofr.setCursor(kx + 8, rowY + 2);
          ofr.printf("%s", CJ_KEY_LABELS[keyIdx]);
        } else {
          drawSystemText(CJ_KEY_LABELS[keyIdx], kx + 4, rowY + 2, 18, TFT_BLACK, TFT_LIGHTGRAY);
        }
      }

      // Latin letter (bottom of key)
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_DARKGRAY, TFT_LIGHTGRAY);
      char letterStr[2] = { (char)(keyChar - 32), 0 };  // uppercase
      int tw = M5.Display.textWidth(letterStr);
      M5.Display.setCursor(kx + (keyW - tw) / 2, rowY + keyH - 22);
      M5.Display.print(letterStr);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
  }

  // === Special keys row ===
  int specialY = startY + 3 * (keyH + keySpacing);

  // Backspace button
  int bsW = 120;
  int bsX = 20;
  M5.Display.fillRect(bsX, specialY, bsW, 56, TFT_LIGHTGRAY);
  M5.Display.drawRect(bsX, specialY, bsW, 56, TFT_BLACK);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK, TFT_LIGHTGRAY);
  M5.Display.setCursor(bsX + 20, specialY + 18);
  M5.Display.print("<- Del");

  // Space button (inserts a space in composed text)
  int spX = 150;
  int spW = 120;
  M5.Display.fillRect(spX, specialY, spW, 56, TFT_LIGHTGRAY);
  M5.Display.drawRect(spX, specialY, spW, 56, TFT_BLACK);
  M5.Display.setCursor(spX + 30, specialY + 18);
  M5.Display.print("Space");

  // Confirm button (green-ish)
  int cfX = 280;
  int cfW = 120;
  M5.Display.fillRect(cfX, specialY, cfW, 56, TFT_DARKGRAY);
  M5.Display.drawRect(cfX, specialY, cfW, 56, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKGRAY);
  drawSystemText("確定", cfX + 20, specialY + 14, 24, TFT_WHITE, TFT_DARKGRAY);

  // Cancel button
  int caX = 410;
  int caW = 120;
  M5.Display.fillRect(caX, specialY, caW, 56, TFT_LIGHTGRAY);
  M5.Display.drawRect(caX, specialY, caW, 56, TFT_BLACK);
  M5.Display.setTextColor(TFT_BLACK, TFT_LIGHTGRAY);
  drawSystemText("取消", caX + 20, specialY + 14, 24, TFT_BLACK, TFT_LIGHTGRAY);

  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.endWrite();
  M5.Display.display();
}

// Fast partial update: only redraws composed text, input code, and candidates
// Skips redrawing the keyboard (which never changes)
void updateCangjieInputArea() {
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();

  // === Clear and redraw composed text area (Y=42..132) ===
  M5.Display.fillRect(11, 43, 518, 88, TFT_WHITE);
  M5.Display.drawRect(10, 42, 520, 90, TFT_BLACK);
  if (cjComposedText.length() > 0) {
    if (ofrFontLoaded) {
      ofr.setFontSize(32);
      ofr.setCursor(18, 56);
      ofr.setDrawer(M5.Display);
      ofr.printf("%s", cjComposedText.c_str());
    } else {
      drawSystemText(cjComposedText.c_str(), 18, 56, 28, TFT_BLACK, TFT_WHITE);
    }
  }

  // === Clear and redraw input code bar (Y=140..182) ===
  M5.Display.fillRect(10, 140, 520, 42, TFT_LIGHTGRAY);
  M5.Display.drawRect(10, 140, 520, 42, TFT_BLACK);
  if (cjInputLen > 0) {
    int drawX = 18;
    for (int i = 0; i < cjInputLen; i++) {
      int keyIdx = cjInputCode[i] - 'a';
      if (keyIdx >= 0 && keyIdx < 26) {
        if (ofrFontLoaded) {
          ofr.setFontSize(26);
          ofr.setCursor(drawX, 146);
          ofr.setDrawer(M5.Display);
          ofr.printf("%s", CJ_KEY_LABELS[keyIdx]);
        } else {
          drawSystemText(CJ_KEY_LABELS[keyIdx], drawX, 146, 24, TFT_BLACK, TFT_LIGHTGRAY);
        }
        drawX += 30;
      }
    }
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_LIGHTGRAY);
    M5.Display.setCursor(drawX + 10, 155);
    M5.Display.print(cjInputCode);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }

  // === Clear and redraw candidate bar (Y=190..280) ===
  M5.Display.fillRect(10, 190, 520, 90, TFT_WHITE);
  M5.Display.drawRect(10, 190, 520, 60, TFT_BLACK);

  if (cjCandidateCount > 0) {
    int startIdx = cjCandidatePage * CJ_CANDIDATES_PER_PAGE;
    int endIdx = min(startIdx + CJ_CANDIDATES_PER_PAGE, cjCandidateCount);
    int cellW = 520 / CJ_CANDIDATES_PER_PAGE;

    for (int i = startIdx; i < endIdx; i++) {
      int col = i - startIdx;
      int cx = 10 + col * cellW;
      String ch = unicodeToUTF8(cjCandidates[i]);
      if (ofrFontLoaded) {
        ofr.setFontSize(32);
        ofr.setDrawer(M5.Display);
        ofr.setCursor(cx + 16, 196);
        ofr.printf("%s", ch.c_str());
      } else {
        drawSystemText(ch.c_str(), cx + 8, 196, 28, TFT_BLACK, TFT_WHITE);
      }
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
      M5.Display.setCursor(cx + 4, 230);
      M5.Display.printf("%d", col + 1);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
      if (col > 0) {
        M5.Display.drawFastVLine(cx, 192, 56, TFT_LIGHTGRAY);
      }
    }

    int totalPages = (cjCandidateCount + CJ_CANDIDATES_PER_PAGE - 1) / CJ_CANDIDATES_PER_PAGE;
    if (totalPages > 1) {
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
      M5.Display.setCursor(440, 255);
      M5.Display.printf("%d/%d", cjCandidatePage + 1, totalPages);
      if (cjCandidatePage > 0)
        M5.Display.fillTriangle(15, 265, 30, 258, 30, 272, TFT_BLACK);
      if (cjCandidatePage < totalPages - 1)
        M5.Display.fillTriangle(525, 265, 510, 258, 510, 272, TFT_BLACK);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
  } else if (cjInputLen > 0) {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
    M5.Display.setCursor(200, 212);
    M5.Display.print("No match");
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }

  M5.Display.endWrite();
  M5.Display.display();
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
}

void cangjieKeyPress(char key) {
  if (key < 'a' || key > 'z') return;
  if (cjInputLen >= CJ_MAX_CODE_LEN) return;

  cjInputCode[cjInputLen++] = key;
  cjInputCode[cjInputLen] = '\0';

  // Search for candidates
  cjCandidateCount = cangjieSearch(cjInputCode, cjCandidates, CJ_MAX_CANDIDATES);
  cjCandidatePage = 0;

  Serial.printf("Cangjie: code='%s' → %d candidates\n", cjInputCode, cjCandidateCount);
}

void cangjieBackspace() {
  if (cjInputLen > 0) {
    // Remove last code character
    cjInputLen--;
    cjInputCode[cjInputLen] = '\0';

    if (cjInputLen > 0) {
      cjCandidateCount = cangjieSearch(cjInputCode, cjCandidates, CJ_MAX_CANDIDATES);
    } else {
      cjCandidateCount = 0;
    }
    cjCandidatePage = 0;
  } else if (cjComposedText.length() > 0) {
    // Remove last composed character (may be multi-byte UTF-8)
    // Find the start of the last character
    int len = cjComposedText.length();
    int i = len - 1;
    while (i > 0 && (cjComposedText[i] & 0xC0) == 0x80) {
      i--;  // Skip continuation bytes
    }
    cjComposedText.remove(i);
  }
}

void cangjieSelectCandidate(int index) {
  int absIdx = cjCandidatePage * CJ_CANDIDATES_PER_PAGE + index;
  if (absIdx < 0 || absIdx >= cjCandidateCount) return;

  // Append selected character to composed text
  String ch = unicodeToUTF8(cjCandidates[absIdx]);
  cjComposedText += ch;

  Serial.printf("Cangjie: selected '%s', composed='%s'\n", ch.c_str(), cjComposedText.c_str());

  // Clear input code for next character
  cjInputLen = 0;
  cjInputCode[0] = '\0';
  cjCandidateCount = 0;
  cjCandidatePage = 0;
}

void cangjieConfirmInput() {
  Serial.printf("Cangjie: confirmed text='%s'\n", cjComposedText.c_str());

  if (cjComposedText.length() > 0 && cjReturnMode == MODE_TODO_LIST) {
    // Add as new todo item
    if (todoCount < MAX_TODO) {
      // Get today's date
      struct tm ti;
      getLocalTime(&ti);
      char dateBuf[16];
      snprintf(dateBuf, sizeof(dateBuf), "%d/%d/%02d",
        ti.tm_mon + 1, ti.tm_mday, (ti.tm_year + 1900) % 100);

      todoList[todoCount].date = String(dateBuf);
      todoList[todoCount].task = cjComposedText;
      todoList[todoCount].checked = false;
      todoCount++;

      saveTodoList();
      sortTodoListByDate();
      currentTodoPage = 0;
      calculateTodoPages();
      Serial.printf("Added todo: %s\n", cjComposedText.c_str());
    }
  }

  // Clean up and return
  cjComposedText = "";
  cjInputLen = 0;
  cjInputCode[0] = '\0';
  cjCandidateCount = 0;
  cjCandidatePage = 0;

  currentMode = cjReturnMode;
  if (cjReturnMode == MODE_TODO_LIST) {
    drawTodoList();
  } else {
    drawDashboard();
  }
}

void cangjieCancel() {
  Serial.println("Cangjie: cancelled");
  cjComposedText = "";
  cjInputLen = 0;
  cjInputCode[0] = '\0';
  cjCandidateCount = 0;
  cjCandidatePage = 0;

  currentMode = cjReturnMode;
  if (cjReturnMode == MODE_TODO_LIST) {
    currentTodoPage = cjReturnPage;
    drawTodoList();
  } else {
    drawDashboard();
  }
}
