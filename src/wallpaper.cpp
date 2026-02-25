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

void drawWallpaperList() {
  Serial.println("Drawing wallpaper list...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setFont(&fonts::efontTW_24);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();
  
  // Title
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(TC_DATUM);
  M5.Display.drawString("壁紙選擇", M5.Display.width() / 2, 20);
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
  } else {
    // Display list of wallpapers
    M5.Display.setTextSize(1);
    int y = 80;
    int itemHeight = 60;
    int maxVisible = 12;  // Show up to 12 items
    
    int startIdx = wallpaperScrollOffset;
    int endIdx = min(startIdx + maxVisible, wallpaperCount);
    
    for (int i = startIdx; i < endIdx; i++) {
      // Draw list item
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
    
    // Scroll indicator if needed
    if (wallpaperCount > maxVisible) {
      M5.Display.setTextSize(1);
      M5.Display.setTextDatum(BC_DATUM);
      String scrollInfo = String(startIdx + 1) + "-" + String(endIdx) + " / " + String(wallpaperCount);
      M5.Display.drawString(scrollInfo, M5.Display.width() / 2, M5.Display.height() - 80);
      M5.Display.setTextDatum(TL_DATUM);
    }
  }
  
  // Universal return button (lower-right)
  M5.Display.setTextDatum(TL_DATUM);
  
  M5.Display.display();
  Serial.println("Wallpaper list displayed");
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
  Serial.printf("Filename length: %d bytes\n", wallpaperFiles[selectedWallpaper].length());
  
  // Try alternate path if filename looks like it might already have path
  if (wallpaperFiles[selectedWallpaper].startsWith("/")) {
    Serial.println("WARNING: Filename starts with /, using it directly");
    filepath = wallpaperFiles[selectedWallpaper];
  }
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  
  // Try to open and check if file exists
  File testFile = SD.open(filepath.c_str());
  if (!testFile) {
    Serial.printf("FAILED to open: '%s'\n", filepath.c_str());
    
    // Try listing directory to debug
    Serial.println("Listing /wallpapers directory:");
    File dir = SD.open("/wallpapers");
    if (dir) {
      File entry = dir.openNextFile();
      while (entry) {
        Serial.printf("  Found: '%s'\n", entry.name());
        entry.close();
        entry = dir.openNextFile();
      }
      dir.close();
    }
    
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("找不到檔案", M5.Display.width() / 2, M5.Display.height() / 2 - 30);
    M5.Display.setTextSize(1);
    M5.Display.drawString(filepath, M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.display();
    return;
  }
  // Check file size
  size_t fileSize = testFile.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  Serial.printf("Free heap before allocation: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  testFile.close();
  
  // Check if file is too large
  if (fileSize > 2000000) {  // 2MB limit
    Serial.println("File too large!");
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.drawString("檔案過大", M5.Display.width() / 2, M5.Display.height() / 2 - 30);
    M5.Display.setTextSize(0.8);
    M5.Display.drawString("請使用小於2MB的圖片", M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.display();
    return;
  }
  
  // Load image data into buffer and draw
  File imgFile = SD.open(filepath.c_str());
  if (!imgFile) {
    Serial.println("Failed to reopen file");
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("無法開啟檔案", M5.Display.width() / 2, M5.Display.height() / 2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.display();
    return;
  }
  
  bool loaded = false;
  Serial.printf("Allocating %d bytes for image buffer...\n", fileSize);
  
  // Check file type (case-insensitive)
  String lowerPath = filepath;
  lowerPath.toLowerCase();
  
  if (lowerPath.endsWith(".jpg") || lowerPath.endsWith(".jpeg")) {
    // Allocate buffer for JPEG
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) {
      Serial.println("Failed to allocate memory for JPEG!");
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString("記憶體不足", M5.Display.width() / 2, M5.Display.height() / 2);
      M5.Display.setTextDatum(TL_DATUM);
      M5.Display.display();
      imgFile.close();
      return;
    }
    Serial.println("Reading JPEG data...");
    size_t bytesRead = imgFile.read(buf, fileSize);
    Serial.printf("Read %d bytes\n", bytesRead);
    Serial.println("Decoding JPEG...");
    loaded = M5.Display.drawJpg(buf, fileSize, 0, 0);
    Serial.printf("JPEG decode result: %s\n", loaded ? "SUCCESS" : "FAILED");
    free(buf);
  } else if (lowerPath.endsWith(".png")) {
    // Allocate buffer for PNG
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) {
      Serial.println("Failed to allocate memory for PNG!");
      imgFile.close();
      return;
    }
    Serial.println("Reading PNG data...");
    imgFile.read(buf, fileSize);
    Serial.println("Decoding PNG...");
    loaded = M5.Display.drawPng(buf, fileSize, 0, 0);
    Serial.printf("PNG decode result: %s\n", loaded ? "SUCCESS" : "FAILED");
    free(buf);
  } else if (lowerPath.endsWith(".bmp")) {
    // Allocate buffer for BMP
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) {
      Serial.println("Failed to allocate memory for BMP!");
      imgFile.close();
      return;
    }
    Serial.println("Reading BMP data...");
    imgFile.read(buf, fileSize);
    Serial.println("Decoding BMP...");
    loaded = M5.Display.drawBmp(buf, fileSize, 0, 0);
    Serial.printf("BMP decode result: %s\n", loaded ? "SUCCESS" : "FAILED");
    free(buf);
  }
  
  imgFile.close();
  
  Serial.printf("Image load result: %s\n", loaded ? "SUCCESS" : "FAILED");
  
  if (!loaded) {
    Serial.println("=== JPEG DECODE FAILED ===");
    Serial.println("Common GIMP export issues:");
    Serial.println("1. Use 'Export As' not 'Save As'");
    Serial.println("2. In export options, uncheck 'Progressive'");
    Serial.println("3. Set quality to 85-95");
    Serial.println("4. Try exporting as BMP instead");
    
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
  
  M5.Display.display();
  Serial.println("Wallpaper displayed");
}
