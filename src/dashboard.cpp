#include "globals.h"
#include "s3cover_jpg.h"
#include "embedded_icons.h"

void layoutIcons() {
  int w = M5.Display.width();
  int h = M5.Display.height();
  int margin = 20;
  int gap = 12;
  int rows = 4;
  int cols = 2;

  int iconW = (w - margin * 2 - gap * (cols - 1)) / cols;
  int iconH = (h - margin * 2 - gap * (rows - 1)) / rows;

  for (int i = 0; i < kIconCount; ++i) {
    int row = i / cols;
    int col = i % cols;
    int x = margin + col * (iconW + gap);
    int y = margin + row * (iconH + gap);
    g_icons[i] = {kIconLabels[i], x, y, iconW, iconH};
  }
}

// Draw welcome screen with embedded cover image
void drawWelcome() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();

  int w = M5.Display.width();
  int h = M5.Display.height();

  // Draw the embedded cover image (540x960, full screen - no fillScreen needed)
  M5.Display.drawJpg(s3cover_jpg, s3cover_jpg_len, 0, 0, w, h);
  M5.Display.endWrite();

  // Update e-ink display
  M5.Display.display();
  Serial.println("Welcome screen displayed");
}

void drawDashboard() {
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar first (time top-left, battery top-right)
  drawStatusBar();
  
  layoutIcons();
  
  // Draw each icon
  for (int i = 0; i < kIconCount; ++i) {
    const auto& icon = g_icons[i];
    
    bool iconDrawn = false;
    
    // Helper lambda to draw PNG data centered in icon cell
    auto drawIconData = [&](const uint8_t* data, size_t len) -> bool {
      int pngW = 0, pngH = 0;
      if (len > 24 && data[0] == 0x89 && data[1] == 'P') {
        pngW = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
        pngH = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
      }
      int imgX = icon.x + (icon.w - pngW) / 2;
      int imgY = icon.y + (icon.h - 20 - pngH) / 2;
      M5.Display.drawPng(data, len, imgX, imgY);
      Serial.printf("Icon %d: %dx%d drawn at (%d,%d)\n", i + 1, pngW, pngH, imgX, imgY);
      return true;
    };
    
    // Try SD card first (if enabled, allows user to customize icons)
    if (useSDCardIcons && sdCardAvailable) {
      char iconPath[32];
      snprintf(iconPath, sizeof(iconPath), "/icons/icon%d.png", i + 1);
      
      if (SD.exists(iconPath)) {
        File iconFile;
        if (sdMutex != NULL) {
          xSemaphoreTake(sdMutex, portMAX_DELAY);
          iconFile = SD.open(iconPath);
          xSemaphoreGive(sdMutex);
        } else {
          iconFile = SD.open(iconPath);
        }
        
        if (iconFile) {
          size_t fileSize = iconFile.size();
          uint8_t* buffer = (uint8_t*)malloc(fileSize);
          if (buffer) {
            size_t bytesRead = iconFile.read(buffer, fileSize);
            iconFile.close();
            if (bytesRead == fileSize) {
              iconDrawn = drawIconData(buffer, fileSize);
            }
            free(buffer);
          } else {
            iconFile.close();
          }
        }
      }
    }
    
    // Fall back to embedded icon
    if (!iconDrawn) {
      char iconName[16];
      snprintf(iconName, sizeof(iconName), "icon%d.png", i + 1);
      const EmbeddedIcon* emb = findEmbeddedIcon(iconName);
      if (emb) {
        iconDrawn = drawIconData(emb->data, emb->length);
      }
    }
    
    // If icon not loaded, draw a placeholder border
    if (!iconDrawn) {
      M5.Display.drawRoundRect(icon.x + 10, icon.y + 10, 
icon.w - 20, icon.h - 60, 
8, TFT_BLACK);
      
      // Draw app number in the center
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.setTextSize(3);
      char numStr[4];
      snprintf(numStr, sizeof(numStr), "%d", i + 1);
      M5.Display.drawString(numStr, icon.x + icon.w / 2, icon.y + (icon.h - 60) / 2 + 10);
    }
    
    // Always draw label below the icon
    int labelY = icon.y + icon.h - 30;
    drawSystemTextCentered(icon.label, icon.x + icon.w / 2, labelY, 32);
    
    M5.Display.setTextDatum(TL_DATUM);
  }
  
  // Weather mini-widget under weather icon (icon index 4)
  if (weatherData.valid) {
    const auto& wIcon = g_icons[4];
    char miniTemp[16];
    String unitSym = (weatherConfig.units == "imperial") ? "F" : "C";
    snprintf(miniTemp, sizeof(miniTemp), "%.0f\xC2\xB0%s %s", weatherData.tempCurrent, unitSym.c_str(), weatherData.descChinese.c_str());
    drawSystemTextCentered(miniTemp, wIcon.x + wIcon.w / 2, wIcon.y + wIcon.h + 2, 18);
  }

  // Item count badges on todo (icon 2) and shopping (icon 3)
  if (todoCount > 0) {
    const auto& tIcon = g_icons[2];
    int unchecked = 0;
    for (int i = 0; i < todoCount; i++) if (!todoList[i].checked) unchecked++;
    if (unchecked > 0) {
      char badge[8];
      snprintf(badge, sizeof(badge), "%d", unchecked);
      int bx = tIcon.x + tIcon.w - 25;
      int by = tIcon.y + 15;
      M5.Display.fillCircle(bx, by, 14, TFT_BLACK);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString(badge, bx, by);
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setTextDatum(TL_DATUM);
    }
  }
  if (shoppingCount > 0) {
    const auto& sIcon = g_icons[3];
    int unchecked = 0;
    for (int i = 0; i < shoppingCount; i++) if (!shoppingList[i].checked) unchecked++;
    if (unchecked > 0) {
      char badge[8];
      snprintf(badge, sizeof(badge), "%d", unchecked);
      int bx = sIcon.x + sIcon.w - 25;
      int by = sIcon.y + 15;
      M5.Display.fillCircle(bx, by, 14, TFT_BLACK);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString(badge, bx, by);
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setTextDatum(TL_DATUM);
    }
  }

M5.Display.endWrite();
  M5.Display.display();
}
