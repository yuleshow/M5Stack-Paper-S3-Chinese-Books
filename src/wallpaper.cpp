#include "globals.h"

// Wallpaper functions
void loadWallpaperFiles() {
  Serial.println("Loading wallpaper files from /wallpapers...");
  wallpaperCount = 0;
  
  // SD card is already initialized in setup(), just check if accessible
  File root = SD.open("/wallpapers");
  if (!root) {
    Serial.println("Failed to open /wallpapers directory");
    return;
  }
  
  if (!root.isDirectory()) {
    Serial.println("/wallpapers is not a directory");
    root.close();
    return;
  }
  
  File file = root.openNextFile();
  while (file && wallpaperCount < 30) {
    if (!file.isDirectory()) {
      String fullpath = String(file.name());
      Serial.printf("DEBUG: Raw filename from SD: '%s'\n", fullpath.c_str());
      
      // Extract just the filename from full path
      String filename = fullpath;
      int lastSlash = fullpath.lastIndexOf('/');
      if (lastSlash >= 0) {
        filename = fullpath.substring(lastSlash + 1);
      }
      
      Serial.printf("DEBUG: Extracted filename: '%s'\n", filename.c_str());
      
      // Skip dot files (hidden files created by macOS like .DS_Store)
      if (filename.startsWith(".")) {
        Serial.printf("Skipping dot file: %s\n", filename.c_str());
        file.close();
        file = root.openNextFile();
        continue;
      }
      
      // Check extension (case-insensitive)
      String lowerFilename = filename;
      lowerFilename.toLowerCase();
      bool isImage = lowerFilename.endsWith(".jpg") || 
lowerFilename.endsWith(".jpeg") ||
lowerFilename.endsWith(".png") || 
lowerFilename.endsWith(".bmp");
      
      Serial.printf("DEBUG: Lowercase: '%s', isImage: %s\n", 
lowerFilename.c_str(), isImage ? "YES" : "NO");
      
      // Only add image files
      if (isImage) {
        if (wallpaperCount >= MAX_WALLPAPERS) { Serial.println("Wallpaper list full"); break; }
        wallpaperFiles[wallpaperCount] = filename;
        Serial.printf("Stored wallpaper [%d]: '%s'\n", wallpaperCount, filename.c_str());
        wallpaperCount++;
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  Serial.printf("Loaded %d wallpaper files\n", wallpaperCount);
}

// Draw a single thumbnail by loading image, scaling and drawing into a small rect
static void drawThumbnail(int index, int tx, int ty, int tw, int th) {
  if (index < 0 || index >= wallpaperCount) return;
  
  String filepath = "/wallpapers/" + wallpaperFiles[index];
  if (wallpaperFiles[index].startsWith("/")) filepath = wallpaperFiles[index];
  
  File imgFile = SD.open(filepath.c_str());
  if (!imgFile) {
    M5.Display.drawRect(tx, ty, tw, th, TFT_DARKGRAY);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(0.8);
    M5.Display.drawString("?", tx + tw / 2, ty + th / 2);
    return;
  }
  
  size_t fileSize = imgFile.size();
  if (fileSize > 4000000) {
    imgFile.close();
    M5.Display.drawRect(tx, ty, tw, th, TFT_DARKGRAY);
    return;
  }
  
  uint8_t* buf = (uint8_t*)ps_malloc(fileSize);
  if (!buf) buf = (uint8_t*)malloc(fileSize);
  if (!buf) { imgFile.close(); M5.Display.drawRect(tx, ty, tw, th, TFT_DARKGRAY); return; }
  
  imgFile.read(buf, fileSize);
  imgFile.close();
  
  // Get image dimensions
  String lp = filepath;
  lp.toLowerCase();
  bool isJpeg = lp.endsWith(".jpg") || lp.endsWith(".jpeg");
  bool isPng = lp.endsWith(".png");
  bool isBmp = lp.endsWith(".bmp");
  
  int imgW = 0, imgH = 0;
  if (isJpeg) getJpegDimensions(buf, fileSize, imgW, imgH);
  else if (isPng) getPngDimensions(buf, fileSize, imgW, imgH);
  else if (isBmp && fileSize > 26) {
    imgW = buf[18] | (buf[19] << 8) | (buf[20] << 16) | (buf[21] << 24);
    imgH = buf[22] | (buf[23] << 8) | (buf[24] << 16) | (buf[25] << 24);
    if (imgH < 0) imgH = -imgH;
  }
  
  // Scale to fit thumbnail area
  float scale_x = 1.0f, scale_y = 1.0f;
  int drawX = tx, drawY = ty;
  if (imgW > 0 && imgH > 0) {
    float scaleW = (float)tw / (float)imgW;
    float scaleH = (float)th / (float)imgH;
    float scale = min(scaleW, scaleH);
    scale_x = scale;
    scale_y = scale;
    int scaledW = (int)(imgW * scale);
    int scaledH = (int)(imgH * scale);
    drawX = tx + (tw - scaledW) / 2;
    drawY = ty + (th - scaledH) / 2;
  }
  
  if (isJpeg) M5.Display.drawJpg(buf, fileSize, drawX, drawY, tw, th, 0, 0, scale_x, scale_y);
  else if (isPng) M5.Display.drawPng(buf, fileSize, drawX, drawY, tw, th, 0, 0, scale_x, scale_y);
  else if (isBmp) M5.Display.drawBmp(buf, fileSize, drawX, drawY, tw, th, 0, 0, scale_x, scale_y);
  
  free(buf);
  
  // Draw border (highlight selected)
  if (index == selectedWallpaper) {
    M5.Display.drawRect(tx, ty, tw, th, TFT_BLACK);
    M5.Display.drawRect(tx + 1, ty + 1, tw - 2, th - 2, TFT_BLACK);
    M5.Display.drawRect(tx + 2, ty + 2, tw - 4, th - 4, TFT_BLACK);
  } else {
    M5.Display.drawRect(tx, ty, tw, th, TFT_DARKGRAY);
  }
}

void drawWallpaperList() {
  Serial.println("Drawing wallpaper list...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setFont(&fonts::efontTW_24);
  
  // Status bar
  drawStatusBar();
  
  // Title + view toggle button at top
  M5.Display.setTextSize(1.5);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.drawString("壁紙選擇", 20, 30);
  M5.Display.setTextSize(1);
  
  // View toggle button (top-right, below status bar)
  int toggleX = 380, toggleY = 30, toggleW = 140, toggleH = 40;
  M5.Display.fillRoundRect(toggleX, toggleY, toggleW, toggleH, 6, TFT_BLACK);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextColor(TFT_WHITE);
  if (wallpaperViewMode == 0) {
    M5.Display.drawString("切換縮圖", toggleX + toggleW / 2, toggleY + toggleH / 2 + 2);
  } else {
    M5.Display.drawString("切換列表", toggleX + toggleW / 2, toggleY + toggleH / 2 + 2);
  }
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextDatum(TL_DATUM);
  
  // Load wallpaper files if not loaded
  if (wallpaperCount == 0) {
    loadWallpaperFiles();
  }
  
  if (wallpaperCount == 0) {
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("SD卡中沒有壁紙", M5.Display.width() / 2, M5.Display.height() / 2 - 20);
    M5.Display.drawString("請在 /wallpapers 資料夾中", M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.drawString("添加圖片檔案", M5.Display.width() / 2, M5.Display.height() / 2 + 60);
    M5.Display.setTextDatum(TL_DATUM);
    drawReturnButton();
  } else if (wallpaperViewMode == 0) {
    // ===== NAME LIST VIEW =====
    int y = 80;
    int itemHeight = 60;
    int maxVisible = 12;
    
    int startIdx = wallpaperScrollOffset;
    int endIdx = min(startIdx + maxVisible, wallpaperCount);
    
    for (int i = startIdx; i < endIdx; i++) {
      if (i == selectedWallpaper) {
        M5.Display.fillRoundRect(20, y, 500, itemHeight, 8, TFT_LIGHTGRAY);
      } else {
        M5.Display.fillRoundRect(20, y, 500, itemHeight, 8, TFT_WHITE);
      }
      M5.Display.drawRoundRect(20, y, 500, itemHeight, 8, TFT_BLACK);
      M5.Display.setCursor(35, y + 20);
      M5.Display.print(wallpaperFiles[i]);
      y += itemHeight + 5;
    }
    
    // Scroll indicator (top center, between title and toggle)
    if (wallpaperCount > maxVisible) {
      M5.Display.setTextSize(1);
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextDatum(MC_DATUM);
      char buf[32];
      snprintf(buf, sizeof(buf), "%d-%d / %d", startIdx + 1, endIdx, wallpaperCount);
      M5.Display.drawString(buf, 270, 50);
      M5.Display.setTextDatum(TL_DATUM);
    }
    
    // Nav bar arrows + return
    if (endIdx < wallpaperCount) drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
    if (wallpaperScrollOffset > 0) drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
    drawReturnButton();
    
    // Extra buttons: 隨機 and 輪播 — centered text
    int btnW = 100, btnH = 44, btnY = 900;
    M5.Display.fillRect(200, btnY, btnW, btnH, TFT_BLACK);
    drawSystemTextCentered("隨機", 250, btnY + 10, 24, TFT_WHITE, TFT_BLACK);
    
    if (wallpaperRotateActive) {
      M5.Display.fillRect(330, btnY, btnW + 10, btnH, TFT_BLACK);
      drawSystemTextCentered("輪播中", 385, btnY + 10, 24, TFT_WHITE, TFT_BLACK);
    } else {
      M5.Display.fillRect(330, btnY, btnW, btnH, TFT_BLACK);
      drawSystemTextCentered("輪播", 380, btnY + 10, 24, TFT_WHITE, TFT_BLACK);
    }
  } else {
    // ===== THUMBNAIL VIEW =====
    // Grid: 3 columns x 3 rows = 9 thumbnails per page
    int cols = 3;
    int rows = 3;
    int thumbPad = 8;
    int contentTop = 80;
    int contentBot = 875;
    int thumbW = (DISPLAY_WIDTH - thumbPad * (cols + 1)) / cols;
    int thumbH = (contentBot - contentTop - thumbPad * (rows + 1)) / rows;
    int maxVisible = cols * rows;  // 9
    
    int startIdx = wallpaperScrollOffset;
    int endIdx = min(startIdx + maxVisible, wallpaperCount);
    
    for (int i = startIdx; i < endIdx; i++) {
      int idx = i - startIdx;
      int col = idx % cols;
      int row = idx / cols;
      int tx = thumbPad + col * (thumbW + thumbPad);
      int ty = contentTop + thumbPad + row * (thumbH + thumbPad);
      drawThumbnail(i, tx, ty, thumbW, thumbH);
    }
    
    // Scroll indicator (top center, between title and toggle)
    if (wallpaperCount > maxVisible) {
      M5.Display.setTextSize(1);
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextDatum(MC_DATUM);
      char buf[32];
      snprintf(buf, sizeof(buf), "%d-%d / %d", startIdx + 1, endIdx, wallpaperCount);
      M5.Display.drawString(buf, 270, 50);
      M5.Display.setTextDatum(TL_DATUM);
    }
    
    // Nav bar arrows + return
    if (endIdx < wallpaperCount) drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
    if (wallpaperScrollOffset > 0) drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
    drawReturnButton();
    
    // Extra buttons: 隨機 and 輪播 — centered text
    int btnW = 100, btnH = 44, btnY = 900;
    M5.Display.fillRect(200, btnY, btnW, btnH, TFT_BLACK);
    drawSystemTextCentered("隨機", 250, btnY + 10, 24, TFT_WHITE, TFT_BLACK);
    
    if (wallpaperRotateActive) {
      M5.Display.fillRect(330, btnY, btnW + 10, btnH, TFT_BLACK);
      drawSystemTextCentered("輪播中", 385, btnY + 10, 24, TFT_WHITE, TFT_BLACK);
    } else {
      M5.Display.fillRect(330, btnY, btnW, btnH, TFT_BLACK);
      drawSystemTextCentered("輪播", 380, btnY + 10, 24, TFT_WHITE, TFT_BLACK);
    }
  }
  
  M5.Display.setTextDatum(TL_DATUM);
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Wallpaper list displayed");
}

// Helper: draw wallpaper by index (used by rotate timer and random)
void drawWallpaperWithIndex(int index) {
  if (index < 0 || index >= wallpaperCount) return;
  selectedWallpaper = index;
  drawWallpaper();
}

void drawWallpaper() {
  Serial.println("Drawing wallpaper...");
  
  if (selectedWallpaper < 0 || selectedWallpaper >= wallpaperCount) {
    Serial.println("Invalid wallpaper selection");
    return;
  }
  
  String filepath = "/wallpapers/" + wallpaperFiles[selectedWallpaper];
  Serial.printf("=== Wallpaper Loading Debug ===\n");
  Serial.printf("Selected index: %d\n", selectedWallpaper);
  Serial.printf("Stored filename: '%s'\n", wallpaperFiles[selectedWallpaper].c_str());
  Serial.printf("Constructed path: '%s'\n", filepath.c_str());
  
  // Try alternate path if filename looks like it might already have path
  if (wallpaperFiles[selectedWallpaper].startsWith("/")) {
    Serial.println("WARNING: Filename starts with /, using it directly");
    filepath = wallpaperFiles[selectedWallpaper];
  }
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  
  // Try to open and check if file exists
  File testFile = SD.open(filepath.c_str());
  if (!testFile) {
    Serial.printf("FAILED to open: '%s'\n", filepath.c_str());
    
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("找不到檔案", M5.Display.width() / 2, M5.Display.height() / 2 - 30);
    M5.Display.setTextSize(1);
    M5.Display.drawString(filepath, M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  // Check file size
  size_t fileSize = testFile.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  testFile.close();
  
  // Check if file is too large (4MB limit for PSRAM)
  if (fileSize > 4000000) {
    Serial.println("File too large!");
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.drawString("檔案過大", M5.Display.width() / 2, M5.Display.height() / 2 - 30);
    M5.Display.drawString("請使用小於4MB的圖片", M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  // Load image data into buffer
  File imgFile = SD.open(filepath.c_str());
  if (!imgFile) {
    Serial.println("Failed to reopen file");
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("無法開啟檔案", M5.Display.width() / 2, M5.Display.height() / 2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  uint8_t* buf = (uint8_t*)ps_malloc(fileSize);
  if (!buf) {
    buf = (uint8_t*)malloc(fileSize);
  }
  if (!buf) {
    Serial.println("Failed to allocate memory for image!");
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("記憶體不足", M5.Display.width() / 2, M5.Display.height() / 2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.endWrite();
    M5.Display.display();
    imgFile.close();
    return;
  }
  
  size_t bytesRead = imgFile.read(buf, fileSize);
  imgFile.close();
  Serial.printf("Read %d bytes\n", bytesRead);
  
  // Determine image dimensions and compute scaling
  String lowerPath = filepath;
  lowerPath.toLowerCase();
  bool isJpeg = lowerPath.endsWith(".jpg") || lowerPath.endsWith(".jpeg");
  bool isPng = lowerPath.endsWith(".png");
  bool isBmp = lowerPath.endsWith(".bmp");
  
  int imgW = 0, imgH = 0;
  if (isJpeg) {
    getJpegDimensions(buf, fileSize, imgW, imgH);
  } else if (isPng) {
    getPngDimensions(buf, fileSize, imgW, imgH);
  } else if (isBmp) {
    // BMP: width at offset 18 (4 bytes LE), height at offset 22 (4 bytes LE)
    if (fileSize > 26) {
      imgW = buf[18] | (buf[19] << 8) | (buf[20] << 16) | (buf[21] << 24);
      imgH = buf[22] | (buf[23] << 8) | (buf[24] << 16) | (buf[25] << 24);
      if (imgH < 0) imgH = -imgH;  // BMP height can be negative (top-down)
    }
  }
  Serial.printf("Image dimensions: %d x %d\n", imgW, imgH);
  
  // Calculate scale to fit display while preserving aspect ratio
  float scale_x = 1.0f, scale_y = 1.0f;
  int drawX = 0, drawY = 0;
  
  if (imgW > 0 && imgH > 0) {
    float scaleW = (float)DISPLAY_WIDTH / (float)imgW;
    float scaleH = (float)DISPLAY_HEIGHT / (float)imgH;
    float scale = min(scaleW, scaleH);   // Fit inside display
    
    scale_x = scale;
    scale_y = scale;
    
    // Center the image
    int scaledW = (int)(imgW * scale);
    int scaledH = (int)(imgH * scale);
    drawX = (DISPLAY_WIDTH - scaledW) / 2;
    drawY = (DISPLAY_HEIGHT - scaledH) / 2;
    
    Serial.printf("Scale: %.3f  drawPos: (%d, %d)  scaled: %dx%d\n",
                  scale, drawX, drawY, scaledW, scaledH);
  }
  
  bool loaded = false;
  
  if (isJpeg) {
    Serial.println("Decoding JPEG with scaling...");
    loaded = M5.Display.drawJpg(buf, fileSize, drawX, drawY,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                 0, 0, scale_x, scale_y);
    Serial.printf("JPEG decode result: %s\n", loaded ? "SUCCESS" : "FAILED");
  } else if (isPng) {
    Serial.println("Decoding PNG with scaling...");
    loaded = M5.Display.drawPng(buf, fileSize, drawX, drawY,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                 0, 0, scale_x, scale_y);
    Serial.printf("PNG decode result: %s\n", loaded ? "SUCCESS" : "FAILED");
  } else if (isBmp) {
    Serial.println("Decoding BMP with scaling...");
    loaded = M5.Display.drawBmp(buf, fileSize, drawX, drawY,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                 0, 0, scale_x, scale_y);
    Serial.printf("BMP decode result: %s\n", loaded ? "SUCCESS" : "FAILED");
  }
  
  free(buf);
  
  if (!loaded) {
    Serial.println("=== IMAGE DECODE FAILED ===");
    
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.drawString("無法載入圖片", M5.Display.width() / 2, M5.Display.height() / 2 - 80);
    M5.Display.setTextSize(0.7);
    M5.Display.drawString("GIMP 匯出設定:", M5.Display.width() / 2, M5.Display.height() / 2 - 30);
    M5.Display.drawString("1. 使用「匯出為」", M5.Display.width() / 2, M5.Display.height() / 2);
    M5.Display.drawString("2. 取消勾選「漸進式」", M5.Display.width() / 2, M5.Display.height() / 2 + 30);
    M5.Display.drawString("3. 或改用 BMP 格式", M5.Display.width() / 2, M5.Display.height() / 2 + 60);
    M5.Display.setTextDatum(TL_DATUM);
  }
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("Wallpaper displayed");
}
