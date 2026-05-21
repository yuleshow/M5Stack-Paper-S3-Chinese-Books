#include "globals.h"

// ==================== File Manager ====================

static String formatSize(size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024) + " KB";
  return String(bytes / (1024 * 1024)) + "." + String((bytes % (1024 * 1024)) * 10 / (1024 * 1024)) + " MB";
}

void loadFileManagerDir() {
  fmCount = 0;
  fmScrollOffset = 0;

  File root = SD.open(fmPath.c_str());
  if (!root || !root.isDirectory()) {
    Serial.printf("FM: failed to open '%s'\n", fmPath.c_str());
    if (root) root.close();
    return;
  }

  // Collect entries
  File f = root.openNextFile();
  while (f && fmCount < FM_MAX_ENTRIES) {
    String fullpath = String(f.name());
    String name = fullpath;
    int lastSlash = fullpath.lastIndexOf('/');
    if (lastSlash >= 0) name = fullpath.substring(lastSlash + 1);

    // Skip hidden files
    if (name.startsWith(".")) {
      f.close();
      f = root.openNextFile();
      continue;
    }

    fmEntries[fmCount] = name;
    fmIsDir[fmCount] = f.isDirectory();
    fmSizes[fmCount] = f.isDirectory() ? 0 : f.size();
    fmCount++;

    f.close();
    f = root.openNextFile();
  }
  if (f) f.close();
  root.close();

  // Sort: directories first, then alphabetical
  for (int i = 0; i < fmCount - 1; i++) {
    for (int j = i + 1; j < fmCount; j++) {
      bool swap = false;
      if (fmIsDir[i] == fmIsDir[j]) {
        // Both same type — alphabetical (case-insensitive)
        String a = fmEntries[i]; a.toLowerCase();
        String b = fmEntries[j]; b.toLowerCase();
        swap = a > b;
      } else {
        swap = !fmIsDir[i] && fmIsDir[j];  // dirs before files
      }
      if (swap) {
        std::swap(fmEntries[i], fmEntries[j]);
        std::swap(fmIsDir[i], fmIsDir[j]);
        std::swap(fmSizes[i], fmSizes[j]);
      }
    }
  }

  Serial.printf("FM: loaded %d entries from '%s'\n", fmCount, fmPath.c_str());
}

void drawFileManager() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();

  // Title: current path
  String displayPath = fmPath;
  if (displayPath.length() > 25) {
    // Truncate from left with "..."
    displayPath = "..." + displayPath.substring(displayPath.length() - 22);
  }
  drawSystemText(displayPath.c_str(), 20, 42, 32);
  M5.Display.drawLine(20, 85, 520, 85, TFT_BLACK);

  // List entries
  int itemHeight = 68;
  int maxVisible = 10;
  int startY = 92;

  int startIdx = fmScrollOffset;
  int endIdx = startIdx + maxVisible;
  if (endIdx > fmCount) endIdx = fmCount;

  if (fmCount == 0) {
    drawSystemTextCentered("（空目錄）", DISPLAY_WIDTH / 2, 400, 32);
  }

  for (int i = startIdx; i < endIdx; i++) {
    int y = startY + (i - startIdx) * (itemHeight + 5);

    M5.Display.drawRoundRect(20, y, 500, itemHeight, 8, TFT_BLACK);

    if (fmIsDir[i]) {
      // Folder icon: filled rectangle with tab
      int ix = 30, iy = y + 18;
      M5.Display.fillRect(ix, iy, 8, 4, TFT_BLACK);         // tab
      M5.Display.fillRect(ix, iy + 4, 24, 18, TFT_WHITE);
      M5.Display.drawRect(ix, iy + 4, 24, 18, TFT_BLACK);   // folder body
      M5.Display.drawRect(ix + 1, iy + 5, 22, 16, TFT_BLACK);

      // Folder name
      drawSystemText(fmEntries[i].c_str(), 62, y + 17, 28);
    } else {
      // File icon: simple document shape
      int ix = 32, iy = y + 14;
      M5.Display.drawRect(ix, iy, 18, 24, TFT_BLACK);
      M5.Display.drawLine(ix + 12, iy, ix + 18, iy + 6, TFT_BLACK);  // corner fold
      M5.Display.drawLine(ix + 12, iy, ix + 12, iy + 6, TFT_BLACK);
      M5.Display.drawLine(ix + 12, iy + 6, ix + 18, iy + 6, TFT_BLACK);

      // Filename (truncate if too long)
      String name = fmEntries[i];
      if (name.length() > 22) name = name.substring(0, 19) + "...";
      drawSystemText(name.c_str(), 62, y + 10, 26);

      // File size (right-aligned, smaller)
      String sizeStr = formatSize(fmSizes[i]);
      int sw = sizeStr.length() * 10;
      drawSystemText(sizeStr.c_str(), 510 - sw, y + 38, 20, TFT_DARKGREY);
    }
  }

  // Page indicator
  int totalPages = (fmCount + maxVisible - 1) / maxVisible;
  if (totalPages < 1) totalPages = 1;
  int curPage = fmScrollOffset / maxVisible + 1;
  drawPageIndicator(curPage, totalPages);

  // Nav arrows
  if (endIdx < fmCount) drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
  if (fmScrollOffset > 0) drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}
