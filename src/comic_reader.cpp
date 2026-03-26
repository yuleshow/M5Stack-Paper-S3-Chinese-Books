#include "globals.h"
#include "esp_task_wdt.h"

// Draw comic reading mode: zoomed quadrant or full-page image view.
// Called from drawReading() after common preamble (screen clear, status bar).
// Expects: startWrite() already called, screen cleared, status bar drawn.
//
// IMPORTANT: This function immediately releases the display bus (endWrite)
// because comic rendering needs SD access for image extraction and icons,
// which conflicts with the SPI bus lock held by startWrite().
void drawComicReading() {
  // Release display bus — image extraction and icon loading need SD (shared SPI bus)
  M5.Display.endWrite();

  // ---- Zoom view: image fills entire display ----
  if (comicZoomQuadrant >= 0) {
    String displayText = currentPageContent;
    int markerPos = displayText.indexOf(EPUB_IMG_MARKER);
    if (markerPos >= 0) {
      int pathStart = markerPos + 1;
      int pathEnd = displayText.indexOf(EPUB_IMG_MARKER, pathStart);
      if (pathEnd > pathStart) {
        String imgPath = displayText.substring(pathStart, pathEnd);
        esp_task_wdt_reset();
        epubExtractAndDrawImage(imgPath, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                comicZoomQuadrant, comicZoomCX, comicZoomCY);
        esp_task_wdt_reset();
      }
    }
    // Overlay status bar on top of fullscreen image
    M5.Display.startWrite();
    drawStatusBar();
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }

  // ---- Full view: image scaled to fill entire screen ----
  String displayText = currentPageContent;
  int markerPos = displayText.indexOf(EPUB_IMG_MARKER);
  if (markerPos >= 0) {
    int pathStart = markerPos + 1;
    int pathEnd = displayText.indexOf(EPUB_IMG_MARKER, pathStart);
    if (pathEnd > pathStart) {
      String imgPath = displayText.substring(pathStart, pathEnd);
      Serial.printf("EPUB IMG full view: page %d/%d, image='%s'\n",
                    currentPage + 1, totalPages, imgPath.c_str());
      esp_task_wdt_reset();
      // Scale image to fill entire display (540×960)
      bool imgDrawn = epubExtractAndDrawImage(imgPath, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      esp_task_wdt_reset();
      if (!imgDrawn) {
        Serial.printf("EPUB IMG: Failed to draw image for page %d\n", currentPage + 1);
        drawSystemText("圖片載入失敗", 60, 400, 24);
      }
    }
  } else {
    Serial.printf("EPUB IMG: No image marker found in page %d content (%d bytes)\n",
                  currentPage + 1, displayText.length());
    drawSystemText("此頁無圖片內容", 60, 400, 24);
  }

  // Overlay chrome on top of image — no startWrite active, so SD access for icons is safe
  drawReadingReturnButton();
  {
    bool hasPrev = (currentPage > 0);
    bool hasNext = (currentPage < totalPages - 1);
    if (hasNext) drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
    if (hasPrev) drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
  }
  // Page number (pure ASCII — use built-in font, no FreeType needed)
  {
    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d / %d", currentPage + 1, totalPages);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(pageStr, 270, NAV_Y + NAV_ICON_SIZE / 2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextSize(1);
  }
  // Zoom mode toggle button (PROGMEM bitmap label, centered in button)
  {
    int btnX = 355;
    int btnY = NAV_Y + 8;
    int btnW = 70, btnH = 48;
    M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_BLACK);
    const char* zoomLabel = (comicZoomMode == 1) ? "自由" : "四分";
    drawSystemTextCentered(zoomLabel, btnX + btnW / 2, btnY + btnH / 2 - 12, 24);
  }

  // Status bar overlay (uses built-in fonts only — no SD access)
  M5.Display.startWrite();
  drawStatusBar();
  M5.Display.endWrite();
  M5.Display.display();
}
