#include "globals.h"

void loadCheckedItems() {
  if (!sdCardAvailable) return;
  
  Serial.println("Loading checked items...");
  File file = SD.open("/shopping_checked.txt");
  if (!file) {
    Serial.println("No checked items file found (fresh start)");
    return;
  }
  
  int loadedCount = 0;
  String line = "";
  while (file.available()) {
    char c = file.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        int pipePos = line.indexOf('|');
        if (pipePos > 0) {
          String groupName = line.substring(0, pipePos);
          String itemName = line.substring(pipePos + 1);
          
          // Find matching item in shopping list
          for (int i = 0; i < shoppingCount; i++) {
            if (shoppingList[i].groupName == groupName && 
                shoppingList[i].itemName == itemName) {
              shoppingList[i].checked = true;
              loadedCount++;
              break;
            }
          }
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
  file.close();
  Serial.printf("✓ Loaded %d checked items\n", loadedCount);
}

void saveCheckedItems() {
  if (!sdCardAvailable) return;
  
  Serial.println("Saving checked items...");
  SD.remove("/shopping_checked.txt");  // Clear old file
  
  File file = SD.open("/shopping_checked.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open checked items file for writing");
    return;
  }
  
  int savedCount = 0;
  for (int i = 0; i < shoppingCount; i++) {
    if (shoppingList[i].checked) {
      file.print(shoppingList[i].groupName);
      file.print("|");
      file.println(shoppingList[i].itemName);
      savedCount++;
    }
  }
  file.close();
  Serial.printf("✓ Saved %d checked items\n", savedCount);
}

// Save remaining shopping items back to CSV, removing empty groups
void saveShoppingList() {
  if (!sdCardAvailable) return;

  Serial.println("Saving shopping list to CSV...");
  SD.remove("/shopping_list.csv");

  File csvFile = SD.open("/shopping_list.csv", FILE_WRITE);
  if (!csvFile) {
    Serial.println("Failed to create shopping_list.csv");
    return;
  }

  // Group items by groupName and write one CSV line per group
  // Strict 4-column format: col1,col2,group,items
  // CJK items space-separated, English items comma-separated
  // Never modify the CSV structure — always exactly 4 columns
  // Quote col4 if it contains commas (standard CSV quoting)
  int groupNum = 0;
  String currentGroup = "";
  for (int i = 0; i < shoppingCount; i++) {
    if (shoppingList[i].groupName != currentGroup) {
      // Close previous group line
      if (currentGroup != "") {
        csvFile.println();
      }
      currentGroup = shoppingList[i].groupName;
      groupNum++;
      
      // Collect all items for this group
      String allItems = "";
      bool prevWasEnglish = false;
      for (int k = i; k < shoppingCount && shoppingList[k].groupName == currentGroup; k++) {
        String item = shoppingList[k].itemName;
        bool isEnglish = (item.length() > 0 && (unsigned char)item.charAt(0) < 0x80 && isalpha(item.charAt(0)));
        
        if (allItems.length() > 0) {
          if (isEnglish || prevWasEnglish) {
            allItems += ", ";
          } else {
            allItems += " ";
          }
        }
        allItems += item;
        prevWasEnglish = isEnglish;
      }
      // Write strict 4-column CSV; quote col4 only if it contains commas
      bool needsQuote = (allItems.indexOf(',') >= 0);
      if (needsQuote) {
        csvFile.printf("%d,%s,%s,\"%s\"", groupNum,
          currentGroup.c_str(), currentGroup.c_str(),
          allItems.c_str());
      } else {
        csvFile.printf("%d,%s,%s,%s", groupNum,
          currentGroup.c_str(), currentGroup.c_str(),
          allItems.c_str());
      }
      // Skip to end of this group
      while (i + 1 < shoppingCount && shoppingList[i + 1].groupName == currentGroup) {
        i++;
      }
    }
  }
  if (shoppingCount > 0) {
    csvFile.println();  // Final newline
  }
  csvFile.close();
  Serial.printf("✓ Saved %d shopping items in %d groups\n", shoppingCount, groupNum);
}

// Remove all checked items from shopping list in memory
void clearCheckedShopping() {
  int writeIdx = 0;
  int removedCount = 0;
  for (int i = 0; i < shoppingCount; i++) {
    if (!shoppingList[i].checked) {
      if (writeIdx != i) {
        shoppingList[writeIdx] = shoppingList[i];
      }
      writeIdx++;
    } else {
      removedCount++;
    }
  }
  shoppingCount = writeIdx;
  Serial.printf("✓ Cleared %d checked shopping items, %d remaining\n", removedCount, shoppingCount);
  
  // Recalculate pagination, stay on current page if possible
  lastRenderedItem = -1;
  calculateShoppingPages();
  if (currentShoppingPage >= totalShoppingPages) {
    currentShoppingPage = totalShoppingPages - 1;
  }
  if (currentShoppingPage < 0) currentShoppingPage = 0;
  
  // Save cleared state (no checked items now)
  saveCheckedItems();
  
  // Persist the updated list to CSV (removes empty groups permanently)
  saveShoppingList();
}

void loadShoppingList() {
  shoppingCount = 0;
  if (!sdCardAvailable) {
    Serial.println("SD card not available for shopping list");
    return;
  }
  
  Serial.println("Loading shopping list...");
  
  // Log file size for diagnostics
  {
    File checkFile;
    if (sdMutex != NULL) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      checkFile = SD.open("/shopping_list.csv");
      xSemaphoreGive(sdMutex);
    } else {
      checkFile = SD.open("/shopping_list.csv");
    }
    if (checkFile) {
      Serial.printf("CSV file size: %d bytes\n", checkFile.size());
      checkFile.close();
    }
  }
  
  File csvFile;
  if (sdMutex != NULL) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    csvFile = SD.open("/shopping_list.csv");
    xSemaphoreGive(sdMutex);
  } else {
    csvFile = SD.open("/shopping_list.csv");
  }
  
  if (!csvFile) {
    Serial.println("shopping_list.csv not found");
    return;
  }
  
  // First pass: read all data into temporary storage
  struct TempItem {
    String groupName;
    String itemName;
  };
  TempItem tempItems[MAX_SHOPPING];
  int tempCount = 0;
  
  String line = "";
  while (csvFile.available() && tempCount < MAX_SHOPPING) {
    char c = csvFile.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        // Parse CSV line: col0,col1,col2,col3...
        // col2 = group name, col3+ = items (may span multiple comma-separated fields)
        // Items field may or may not be quoted; unquoted commas create extra fields
        // Strategy: parse first 3 fields normally, then take EVERYTHING after col2's comma as items
        String fields[3];
        int fieldIdx = 0;
        bool inQuote = false;
        int fieldStart = 0;
        int itemsStart = -1;
        for (int ci = 0; ci <= (int)line.length() && fieldIdx < 3; ci++) {
          char ch = (ci < (int)line.length()) ? line.charAt(ci) : '\0';
          if (ch == '"') {
            inQuote = !inQuote;
          } else if ((ch == ',' && !inQuote) || ci == (int)line.length()) {
            fields[fieldIdx] = line.substring(fieldStart, ci);
            fields[fieldIdx].trim();
            // Strip outer quotes
            if (fields[fieldIdx].length() >= 2 && fields[fieldIdx].charAt(0) == '"' && fields[fieldIdx].charAt(fields[fieldIdx].length() - 1) == '"') {
              fields[fieldIdx] = fields[fieldIdx].substring(1, fields[fieldIdx].length() - 1);
            }
            fieldIdx++;
            fieldStart = ci + 1;
            if (fieldIdx == 3) {
              itemsStart = ci + 1;  // Everything after this comma is items
            }
          }
        }
        
        if (fieldIdx >= 3 && itemsStart >= 0 && itemsStart < (int)line.length()) {
          String groupName = fields[2];
          // Get everything from itemsStart to end of line as the items string
          String itemsStr = line.substring(itemsStart);
          // Strip trailing empty fields (commas at end like ,,,)
          while (itemsStr.endsWith(",")) {
            itemsStr = itemsStr.substring(0, itemsStr.length() - 1);
          }
          itemsStr.trim();
          // Remove all CSV quote characters — they are formatting, not content
          // e.g. "eggs, 肝醬", bread → eggs, 肝醬, bread
          String cleaned = "";
          for (int qi = 0; qi < (int)itemsStr.length(); qi++) {
            if (itemsStr.charAt(qi) != '"') {
              cleaned += itemsStr.charAt(qi);
            }
          }
          itemsStr = cleaned;
          itemsStr.trim();
          
          Serial.printf("CSV group=[%s] items=[%s] (%d bytes)\n", 
            groupName.c_str(), itemsStr.c_str(), itemsStr.length());
          
          // Smart mixed-language parser:
          // - CJK sections: split by space
          // - ASCII/English sections: split by comma
          int pos = 0;
          while (pos < itemsStr.length() && tempCount < MAX_SHOPPING) {
            while (pos < itemsStr.length() && itemsStr.charAt(pos) == ' ') pos++;
            if (pos >= itemsStr.length()) break;
            
            unsigned char firstByte = (unsigned char)itemsStr.charAt(pos);
            bool isCJKSection = (firstByte >= 0x80);
            
            if (isCJKSection) {
              int runStart = pos;
              while (pos < itemsStr.length()) {
                unsigned char cb = (unsigned char)itemsStr.charAt(pos);
                if (cb < 0x80 && (isalpha(cb) || cb == ',')) break;
                pos++;
              }
              String cjkRun = itemsStr.substring(runStart, pos);
              cjkRun.trim();
              int sp = 0;
              while (sp < cjkRun.length() && tempCount < MAX_SHOPPING) {
                int spacePos = cjkRun.indexOf(' ', sp);
                String item;
                if (spacePos == -1) {
                  item = cjkRun.substring(sp);
                  sp = cjkRun.length();
                } else {
                  item = cjkRun.substring(sp, spacePos);
                  sp = spacePos + 1;
                }
                item.trim();
                // Strip trailing ASCII punctuation (commas, periods) from CJK items
                while (item.length() > 0) {
                  char lastCh = item.charAt(item.length() - 1);
                  if (lastCh == ',' || lastCh == '.' || lastCh == ';') {
                    item = item.substring(0, item.length() - 1);
                    item.trim();
                  } else break;
                }
                if (item.length() > 0) {
                  tempItems[tempCount].groupName = groupName;
                  tempItems[tempCount].itemName = item;
                  tempCount++;
                }
              }
            } else {
              int runStart = pos;
              while (pos < itemsStr.length()) {
                unsigned char cb = (unsigned char)itemsStr.charAt(pos);
                if (cb >= 0x80) break;
                pos++;
              }
              String engRun = itemsStr.substring(runStart, pos);
              engRun.trim();
              if (engRun.endsWith(",")) engRun = engRun.substring(0, engRun.length() - 1);
              engRun.trim();
              int sp = 0;
              while (sp < engRun.length() && tempCount < MAX_SHOPPING) {
                int commaPos = engRun.indexOf(',', sp);
                String item;
                if (commaPos == -1) {
                  item = engRun.substring(sp);
                  sp = engRun.length();
                } else {
                  item = engRun.substring(sp, commaPos);
                  sp = commaPos + 1;
                }
                item.trim();
                if (item.length() > 0) {
                  tempItems[tempCount].groupName = groupName;
                  tempItems[tempCount].itemName = item;
                  tempCount++;
                }
              }
            }
          }
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
  
  csvFile.close();
  
  // Second pass: copy data without any markers or numbering
  String currentGroup = "";
  int groupNumber = 0;
  
  for (int i = 0; i < tempCount; i++) {
    // Check if this is a new group
    bool isNewGroup = (tempItems[i].groupName != currentGroup);
    if (isNewGroup) {
      currentGroup = tempItems[i].groupName;
      groupNumber++;
    }
    
    // Store in shopping list without any prefixes or suffixes
    if (shoppingCount >= MAX_SHOPPING) { Serial.println("Shopping list full"); break; }
    shoppingList[shoppingCount].groupName = tempItems[i].groupName;
    shoppingList[shoppingCount].itemName = tempItems[i].itemName;
    shoppingList[shoppingCount].checked = false;
    shoppingCount++;
    
    Serial.printf("Loaded: %s -> %s\n", tempItems[i].groupName.c_str(), tempItems[i].itemName.c_str());
  }
  
  Serial.printf("Loaded %d shopping items in %d groups\n", shoppingCount, groupNumber);
  
  // Initialize pagination
  shoppingPageStarts[0] = 0;
  currentShoppingPage = 0;
  totalShoppingPages = 1;
  lastRenderedItem = -1;
  
  // Load previously checked items
  loadCheckedItems();
}

// Helper: measure vertical height of an ASCII run split by words
// Font/textSize must be set on M5.Display before calling
static const int WORD_GAP = 8;  // Vertical gap between words to show space
static int measureAsciiRunHeight(const String& run) {
  int h = 0;
  int p = 0;
  int wordCount = 0;
  while (p < (int)run.length()) {
    while (p < (int)run.length() && run.charAt(p) == ' ') p++;
    if (p >= (int)run.length()) break;
    int e = p;
    while (e < (int)run.length() && run.charAt(e) != ' ') e++;
    if (wordCount > 0) h += WORD_GAP;
    h += M5.Display.textWidth(run.substring(p, e)) + 4;
    wordCount++;
    p = e;
  }
  return h;
}

// Returns the final Y position after drawing
int drawVerticalMixedText(String text, int x, int startY, int charSpacing) {
  int y = startY;
  M5.Display.setFont(&fonts::efontTW_24);
  M5.Display.setTextSize(1.5);
  
  for (int j = 0; j < text.length(); ) {
    unsigned char c = text.charAt(j);
    bool isASCII = (c < 0x80);
    
    if (isASCII) {
      // Collect consecutive ASCII characters into one run
      int runStart = j;
      while (j < text.length() && (unsigned char)text.charAt(j) < 0x80) {
        j++;
      }
      String run = text.substring(runStart, j);
      run.trim();
      if (run.length() == 0) continue;
      
      // Split run into words and render each word as a separate sprite
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextSize(1.5);
      int textH = charSpacing;
      
      int wp = 0;
      int wordIdx = 0;
      while (wp < (int)run.length()) {
        while (wp < (int)run.length() && run.charAt(wp) == ' ') wp++;
        if (wp >= (int)run.length()) break;
        int we = wp;
        while (we < (int)run.length() && run.charAt(we) != ' ') we++;
        String word = run.substring(wp, we);
        wp = we;
        
        if (wordIdx > 0) y += WORD_GAP;  // Space gap between words
        int textW = M5.Display.textWidth(word);
        int rotatedH = textW + 4;
        
        LGFX_Sprite sprite(&M5.Display);
        if (!sprite.createSprite(textW + 4, textH)) {
          y += rotatedH;
          wordIdx++;
          continue;
        }
        sprite.fillSprite(TFT_WHITE);
        sprite.setFont(&fonts::efontTW_24);
        sprite.setTextColor(TFT_BLACK);
        sprite.setTextSize(1.5);
        sprite.setTextDatum(ML_DATUM);
        sprite.drawString(word, 2, textH / 2);
        sprite.pushRotateZoom(&M5.Display, x, y + rotatedH / 2, 90, 1.0, 1.0);
        sprite.deleteSprite();
        y += rotatedH;
        wordIdx++;
      }
    } else {
      // CJK character - decode and draw upright
      int charStart = j;
      uint32_t unicode = utf8Decode(text, j);
      String ch = text.substring(charStart, j);
      applyVerticalPunct(ch, unicode);
      
      if (ofrFontLoaded) {
        ofr.setFontSize(charSpacing);
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
        ofr.cdrawString(ch.c_str(), x, y, TFT_BLACK, TFT_WHITE);
      } else if (g_binFont.loaded) {
        drawBinFontChar(unicode, x - 15, y);
      } else {
        M5.Display.setCursor(x - 12, y);
        M5.Display.print(ch);
      }
      
      y += charSpacing;
    }
  }
  
  return y;
}

// Pre-calculate all page breaks by simulating layout without drawing
void calculateShoppingPages() {
  if (shoppingCount == 0) {
    totalShoppingPages = 1;
    shoppingPageStarts[0] = 0;
    return;
  }
  
  // Layout parameters (must match drawShoppingList)
  int fontSizePt = ofrFontLoaded ? 42 : (g_binFont.loaded ? g_binFont.fontSize : 36);
  int columnSpacing = fontSizePt + 18;
  int groupSpacing = fontSizePt + 38;
  int startY = VERTICAL_TEXT_START_Y;
  int maxY = VERTICAL_TEXT_MAX_Y;
  int leftMargin = 20;  // Tighter left margin for shopping list
  int rightMargin = VERTICAL_RIGHT_MARGIN;
  int displayWidth = 540;  // M5Paper S3 width
  
  shoppingPageStarts[0] = 0;
  int pageCount = 1;
  int pageStartItem = 0;
  
  while (pageStartItem < shoppingCount && pageCount <= MAX_SHOPPING_PAGES) {
    int columnX = displayWidth - rightMargin;
    String currentGroup = "";
    int currentY = startY;
    int lastRendered = pageStartItem - 1;
    
    for (int i = pageStartItem; i < shoppingCount && columnX >= (leftMargin + 40); i++) {
      // New group -> new column (except first group on page)
      if (shoppingList[i].groupName != currentGroup) {
        if (currentGroup != "") {
          columnX -= groupSpacing;
          currentY = startY;
          if (columnX < (leftMargin + 40)) break;
        }
        currentGroup = shoppingList[i].groupName;
        
        // Calculate group text height: measure ASCII runs (split by words), CJK=56px each
        int groupTextHeight = 0;
        M5.Display.setFont(&fonts::efontTW_24);
        M5.Display.setTextSize(1.5);
        for (int j = 0; j < currentGroup.length(); ) {
          unsigned char c = currentGroup.charAt(j);
          if (c < 0x80) {
            int runStart = j;
            while (j < currentGroup.length() && (unsigned char)currentGroup.charAt(j) < 0x80) j++;
            String run = currentGroup.substring(runStart, j);
            run.trim();
            if (run.length() > 0) {
              groupTextHeight += measureAsciiRunHeight(run);
            }
          } else {
            j += utf8CharLen(c);
            groupTextHeight += 56;
          }
        }
        
        int bgWidth = 56;
        if (columnX - bgWidth/2 < leftMargin) break;
        
        currentY = startY + groupTextHeight + 15;  // After group name
      }
      
      // Calculate item height
      String item = shoppingList[i].itemName;
      if (item.length() > 0) {
        int itemHeight = 28 + 10;  // checkbox + gap
        // Estimate height: measure ASCII runs (split by words), CJK=56px each
        M5.Display.setFont(&fonts::efontTW_24);
        M5.Display.setTextSize(1.5);
        for (int j = 0; j < item.length(); ) {
          unsigned char c = item.charAt(j);
          if (c < 0x80) {
            int runStart = j;
            while (j < item.length() && (unsigned char)item.charAt(j) < 0x80) j++;
            String run = item.substring(runStart, j);
            run.trim();
            if (run.length() > 0) {
              itemHeight += measureAsciiRunHeight(run);
            }
          } else {
            j += utf8CharLen(c);
            itemHeight += 56;
          }
        }
        itemHeight += 10;
        
        // Check if item fits
        if (currentY + itemHeight > maxY - 60) {
          columnX -= columnSpacing;
          currentY = startY;
          if (columnX < (leftMargin + 40)) break;
        }
        if (columnX < (leftMargin + 40)) break;
        
        // Simulate rendering: track Y for text that might wrap
        int y = currentY + 28 + 10;  // after checkbox
        for (int j = 0; j < item.length(); ) {
          unsigned char c = item.charAt(j);
          if (c < 0x80) {
            // Estimate ASCII run height
            int runStart = j;
            while (j < item.length() && (unsigned char)item.charAt(j) < 0x80) j++;
            String run = item.substring(runStart, j);
            run.trim();
            if (run.length() == 0) continue;
            M5.Display.setFont(&fonts::efontTW_24);
            M5.Display.setTextSize(1.5);
            int runHeight = M5.Display.textWidth(run) + 4;
            if (y + runHeight > maxY - 60) {
              columnX -= columnSpacing;
              if (columnX < (leftMargin + 40)) break;
              y = startY;
            }
            y += runHeight;
          } else {
            int charSpacing = 56;
            if (y + charSpacing > maxY - 60) {
              columnX -= columnSpacing;
              if (columnX < (leftMargin + 40)) break;
              y = startY;
            }
            j += utf8CharLen(c);
            y += charSpacing;
          }
        }
        if (columnX < (leftMargin + 40)) break;
        
        currentY = y + 20;
        lastRendered = i;
      }
    }
    
    // Check if there are more items for next page
    if (lastRendered < shoppingCount - 1) {
      if (pageCount < MAX_SHOPPING_PAGES) {
        // Guard: if nothing rendered, force advance to avoid infinite loop
        int nextStart = (lastRendered >= pageStartItem) ? lastRendered + 1 : pageStartItem + 1;
        if (nextStart >= shoppingCount) break;  // No more items
        shoppingPageStarts[pageCount] = nextStart;
        pageCount++;
        pageStartItem = nextStart;
      } else {
        break;
      }
    } else {
      break;  // All items fit
    }
  }
  
  totalShoppingPages = pageCount;
  Serial.printf("Shopping pages calculated: %d pages for %d items\n", totalShoppingPages, shoppingCount);
}

void drawShoppingList() {
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  
  // Calculate all page breaks before drawing nav bar
  calculateShoppingPages();
  
  // Clamp page to valid range (may have changed after cleanup or data reload)
  if (currentShoppingPage >= totalShoppingPages) {
    currentShoppingPage = totalShoppingPages - 1;
  }
  if (currentShoppingPage < 0) currentShoppingPage = 0;
  
  {
    bool hasPrev = (currentShoppingPage > 0);
    bool hasNext = (currentShoppingPage < totalShoppingPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);
  }
  
  // Use MingLiU binary font if loaded
  if (!g_binFont.loaded) {
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextSize(1.2);
  }
  
  // Vertical text layout parameters
  int fontSizePt = ofrFontLoaded ? 42 : (g_binFont.loaded ? g_binFont.fontSize : 36);
  int charHeight = fontSizePt + 10;  // Spacing between characters vertically
  int columnSpacing = fontSizePt + 18;  // Tighter spacing between item columns
  int groupSpacing = fontSizePt + 38;  // Spacing after group name
  int startY = VERTICAL_TEXT_START_Y;
  int maxY = VERTICAL_TEXT_MAX_Y;
  int leftMargin = 20;   // Tighter left margin for shopping list
  int rightMargin = VERTICAL_RIGHT_MARGIN;  // Right edge margin
  int displayWidth = M5.Display.width();  // 540 for Paper S3
  int columnX = displayWidth - rightMargin;  // Start from right side
  int maxColumnX = displayWidth - rightMargin;  // Track rightmost position for pagination
  
  Serial.printf("Draw shopping: fontSizePt=%d, colSpacing=%d, grpSpacing=%d, displayW=%d\n",
    fontSizePt, columnSpacing, groupSpacing, displayWidth);
  Serial.printf("Draw shopping: page=%d, startItem=%d/%d items\n", 
    currentShoppingPage, shoppingPageStarts[currentShoppingPage], shoppingCount);
  
  // Nav bar and page indicator drawn AFTER pagination is calculated (see below)
  
  // Use dynamic page breaks - start from the item index for this page
  int startItem = shoppingPageStarts[currentShoppingPage];
  int endItem = shoppingCount;  // Try to render all remaining items
  
  // Reset last rendered tracker
  lastRenderedItem = startItem - 1;
  shoppingCheckboxCount = 0;  // Reset saved checkbox positions
  
  // Process and display items grouped
  String currentGroup = "";
  int itemsRendered = 0;
  int groupStartX = columnX;  // Track where group starts
  int currentY = startY;  // Track current Y position in column
  
  for (int i = startItem; i < endItem && columnX >= (leftMargin + 40); i++) {
    // Check for nav touch between items
    if (checkNavTouch()) {
      Serial.println("Nav touch during shopping render - aborting");
      return;
    }
    
    // Check if we need to display group name
    if (shoppingList[i].groupName != currentGroup) {
      // If not first group, move to next column for new group
      if (currentGroup != "") {
        columnX -= groupSpacing;  // Move to next column for new group
        currentY = startY;  // Reset Y position for new column
        
        // Check if we still have space for the new group
        if (columnX < (leftMargin + 40)) {
          break;  // No more space, stop rendering
        }
      }
      
      currentGroup = shoppingList[i].groupName;
      groupStartX = columnX;  // Mark start of new group
      
      // Group name already contains "G#." prefix from loadShoppingList
      // Calculate group text height: measure ASCII runs (split by words), CJK=56px each
      int groupTextHeight = 0;
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextSize(1.5);
      for (int j = 0; j < currentGroup.length(); ) {
        unsigned char c = currentGroup.charAt(j);
        if (c < 0x80) {
          // Collect ASCII run and measure word-split height
          int runStart = j;
          while (j < currentGroup.length() && (unsigned char)currentGroup.charAt(j) < 0x80) j++;
          String run = currentGroup.substring(runStart, j);
          run.trim();
          if (run.length() > 0) {
            groupTextHeight += measureAsciiRunHeight(run);
          }
        } else {
          j += utf8CharLen(c);
          groupTextHeight += 56;
        }
      }
      
      // Check if we have enough space to draw this group
      int bgWidth = 56;
      if (columnX - bgWidth/2 < leftMargin) {
        break;  // Not enough space, stop rendering
      }
      
      // Draw grey background for group
      int bgPadding = 4;
      M5.Display.fillRect(columnX - bgWidth/2, currentY - bgPadding, 
bgWidth, groupTextHeight + bgPadding * 2, 
0x5AEB);  // Darker grey for better contrast with white text
      
      // Draw group name vertically (white text on grey)
      int y = currentY;
      M5.Display.setTextColor(TFT_WHITE);  // White text for group
      
      if (!g_binFont.loaded) {
        M5.Display.setFont(&fonts::efontTW_24);
        M5.Display.setTextSize(1.0);
      }
      
      // Use helper function for mixed text rendering (white color for group)
      for (int j = 0; j < currentGroup.length(); ) {
        unsigned char c = currentGroup.charAt(j);
        bool isASCII = (c < 0x80);
        
        if (isASCII) {
          // Collect consecutive ASCII characters into one run
          int runStart = j;
          while (j < currentGroup.length() && (unsigned char)currentGroup.charAt(j) < 0x80) j++;
          String run = currentGroup.substring(runStart, j);
          run.trim();
          if (run.length() == 0) continue;
          
          // Split run into words and render each as a separate sprite (white on grey)
          M5.Display.setFont(&fonts::efontTW_24);
          M5.Display.setTextSize(1.5);
          int textH = 45;
          
          int wp = 0;
          int wordIdx = 0;
          while (wp < (int)run.length()) {
            while (wp < (int)run.length() && run.charAt(wp) == ' ') wp++;
            if (wp >= (int)run.length()) break;
            int we = wp;
            while (we < (int)run.length() && run.charAt(we) != ' ') we++;
            String word = run.substring(wp, we);
            wp = we;
            
            if (wordIdx > 0) y += WORD_GAP;  // Space gap between words
            int textW = M5.Display.textWidth(word);
            int rotatedH = textW + 4;
            
            LGFX_Sprite sprite(&M5.Display);
            if (!sprite.createSprite(textW + 4, textH)) {
              y += rotatedH;
              wordIdx++;
              continue;
            }
            sprite.fillSprite(0x5AEB);  // Match grey background
            sprite.setFont(&fonts::efontTW_24);
            sprite.setTextColor(TFT_WHITE);
            sprite.setTextSize(1.5);
            sprite.setTextDatum(ML_DATUM);
            sprite.drawString(word, 2, textH / 2);
            sprite.pushRotateZoom(&M5.Display, columnX, y + rotatedH / 2, 90, 1.0, 1.0);
            sprite.deleteSprite();
            y += rotatedH;
            wordIdx++;
          }
        } else {
          int charStart = j;
          uint32_t unicode = utf8Decode(currentGroup, j);
          String ch = currentGroup.substring(charStart, j);
          applyVerticalPunct(ch, unicode);
          if (ofrFontLoaded) {
            ofr.setFontSize(fontSizePt);
            ofr.setFontColor(TFT_WHITE, 0x5AEB);
            ofr.cdrawString(ch.c_str(), columnX, y, TFT_WHITE, 0x5AEB);
          } else if (g_binFont.loaded) {
            drawBinFontChar(unicode, columnX - fontSizePt/2, y, TFT_WHITE);
          } else {
            M5.Display.setCursor(columnX - 14, y);
            M5.Display.print(ch);
          }
          y += 56;  // Chinese spacing
        }
      }
      
      // Reset text color to black for items
      M5.Display.setTextColor(TFT_BLACK);
      
      // Continue from where group ended for items
      currentY = y + 15;  // Start items below group with small gap
    }
    
    // Display single item vertically UNDER the group (same column)
    // Item name already contains "[#]" prefix and "END"/"FIN" suffix from loadShoppingList
    String item = shoppingList[i].itemName;
    
    if (item.length() > 0) {
      // Calculate item height: measure ASCII runs (split by words), CJK=56px each
      int itemHeight = 28 + 10;  // checkbox + gap
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextSize(1.5);
      for (int j = 0; j < item.length(); ) {
        unsigned char c = item.charAt(j);
        if (c < 0x80) {
          int runStart = j;
          while (j < item.length() && (unsigned char)item.charAt(j) < 0x80) j++;
          String run = item.substring(runStart, j);
          run.trim();
          if (run.length() > 0) {
            itemHeight += measureAsciiRunHeight(run);
          }
        } else {
          j += utf8CharLen(c);
          itemHeight += 56;
        }
      }
      itemHeight += 10;  // Bottom spacing
      
      // Check if item fits in current column
      if (currentY + itemHeight > maxY - 60) {
        // Move to next column (within same group)
        columnX -= columnSpacing;
        currentY = startY;
        
        // Check if we've run out of horizontal space
        // Need at least 40px margin to prevent text clipping
        if (columnX < (leftMargin + 40)) {
          break;  // Stop rendering on this page
        }
      }
      
      // Check if we have enough space to draw this item
      if (columnX < (leftMargin + 40)) {
        break;  // Not enough space, stop rendering
      }
      
      // Draw checkbox BEFORE item text (centered at columnX)
      int checkboxY = currentY;
      int checkboxSize = 28;
      int checkboxX = columnX - checkboxSize/2;  // Center checkbox at columnX
      int checkboxColumnX = columnX;  // Remember column for touch zone
      M5.Display.drawRect(checkboxX, checkboxY, checkboxSize, checkboxSize, TFT_BLACK);
      if (shoppingList[i].checked) {
        M5.Display.fillRect(checkboxX + 3, checkboxY + 3, checkboxSize - 6, checkboxSize - 6, TFT_BLACK);
      }
      
      // Reserve a slot for this checkbox's touch zone (touchMaxY filled in after rendering)
      int cbSlot = -1;
      if (shoppingCheckboxCount < 50) {
        cbSlot = shoppingCheckboxCount;
        shoppingCheckboxes[cbSlot].x = checkboxX;
        shoppingCheckboxes[cbSlot].y = checkboxY;
        shoppingCheckboxes[cbSlot].size = checkboxSize;
        shoppingCheckboxes[cbSlot].itemIdx = i;
        shoppingCheckboxes[cbSlot].touchMinX = checkboxColumnX - columnSpacing/2;
        shoppingCheckboxes[cbSlot].touchMaxX = checkboxColumnX + columnSpacing/2;
        shoppingCheckboxes[cbSlot].touchMinY = checkboxY;
        shoppingCheckboxes[cbSlot].touchMaxY = checkboxY;  // Placeholder, updated after render
        shoppingCheckboxCount++;
      }
      
      // Draw item vertically below checkbox (25px gap to clear rotated ASCII sprites)
      int y = currentY + checkboxSize + 10;
      int itemBottomY = y;  // Track maximum Y extent in the checkbox's column
      
      if (!g_binFont.loaded) {
        M5.Display.setFont(&fonts::efontTW_24);
        M5.Display.setTextSize(1.0);
      }
      
      // Render item text vertically: collect ASCII runs, draw CJK upright
      for (int j = 0; j < item.length(); ) {
        unsigned char c = item.charAt(j);
        bool isASCII = (c < 0x80);
        
        if (isASCII) {
          // Collect consecutive ASCII characters into one run
          int runStart = j;
          while (j < item.length() && (unsigned char)item.charAt(j) < 0x80) j++;
          String run = item.substring(runStart, j);
          run.trim();
          if (run.length() == 0) continue;
          
          // Split run into words and render each as a separate rotated sprite
          M5.Display.setFont(&fonts::efontTW_24);
          M5.Display.setTextSize(1.5);
          int textH = 45;
          
          int wp = 0;
          int wordIdx = 0;
          while (wp < (int)run.length()) {
            while (wp < (int)run.length() && run.charAt(wp) == ' ') wp++;
            if (wp >= (int)run.length()) break;
            int we = wp;
            while (we < (int)run.length() && run.charAt(we) != ' ') we++;
            String word = run.substring(wp, we);
            wp = we;
            
            if (wordIdx > 0) y += WORD_GAP;  // Space gap between words
            int textW = M5.Display.textWidth(word);
            int rotatedH = textW + 4;
            
            // Check if rotated word fits in current column
            if (y + rotatedH > maxY - 60) {
              if (y > itemBottomY) itemBottomY = y;
              columnX -= columnSpacing;
              if (columnX < (leftMargin + 40)) break;
              y = startY;
            }
            
            LGFX_Sprite sprite(&M5.Display);
            if (!sprite.createSprite(textW + 4, textH)) {
              y += rotatedH;
              wordIdx++;
              continue;
            }
            sprite.fillSprite(TFT_WHITE);
            sprite.setFont(&fonts::efontTW_24);
            sprite.setTextColor(TFT_BLACK);
            sprite.setTextSize(1.5);
            sprite.setTextDatum(ML_DATUM);
            sprite.drawString(word, 2, textH / 2);
            sprite.pushRotateZoom(&M5.Display, columnX, y + rotatedH / 2, 90, 1.0, 1.0);
            sprite.deleteSprite();
            y += rotatedH;
            wordIdx++;
          }
        } else {
          int charSpacing = 56;
          
          // Check if next character will exceed bottom margin
          if (y + charSpacing > maxY - 60) {
            if (y > itemBottomY) itemBottomY = y;
            columnX -= columnSpacing;
            if (columnX < (leftMargin + 40)) break;
            y = startY;
          }
          
          int charStart = j;
          uint32_t unicode = utf8Decode(item, j);
          String ch = item.substring(charStart, j);
          applyVerticalPunct(ch, unicode);
          if (ofrFontLoaded) {
            ofr.setFontSize(fontSizePt);
            ofr.setFontColor(TFT_BLACK, TFT_WHITE);
            ofr.cdrawString(ch.c_str(), columnX, y, TFT_BLACK, TFT_WHITE);
          } else if (g_binFont.loaded) {
            drawBinFontChar(unicode, columnX - fontSizePt/2, y);
          } else {
            M5.Display.setCursor(columnX - 14, y);
            M5.Display.print(ch);
          }
          y += 56;  // Chinese spacing
        }
      }
      
      // Update currentY for next item in same column
      currentY = y + 20;
      
      // Track final Y position for touch zone
      if (y > itemBottomY) itemBottomY = y;
      
      // Now update the touch zone's maxY to cover only this item's actual area
      if (cbSlot >= 0) {
        shoppingCheckboxes[cbSlot].touchMaxY = itemBottomY + 20;
      }
      
      itemsRendered++;
      lastRenderedItem = i;  // Track this as successfully rendered
    }
  }
  
  Serial.printf("Draw shopping: rendered %d items (idx %d to %d), final columnX=%d\n",
    itemsRendered, startItem, lastRenderedItem, columnX);
  
  // Page indicator next to right arrow, larger font
  M5.Display.setFont(&fonts::efontTW_24);
  M5.Display.setTextSize(1.0);
  M5.Display.setCursor(155, 910);
  M5.Display.printf("%d/%d", currentShoppingPage + 1, totalShoppingPages);
  
  // "清除" (Clear checked) button - only show if there are checked items
  int checkedCount = 0;
  for (int i = 0; i < shoppingCount; i++) {
    if (shoppingList[i].checked) checkedCount++;
  }
  if (checkedCount > 0) {
    int btnX = 280, btnY = 900, btnW = 120, btnH = 44;
    M5.Display.fillRect(btnX, btnY, btnW, btnH, TFT_BLACK);
    drawSystemText("清除", btnX + 14, btnY + 8, 24, TFT_WHITE, TFT_BLACK);
    // Show count
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(btnX + 70, btnY + 14);
    M5.Display.printf("x%d", checkedCount);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }
  
  if (pendingNavTouch) {
    Serial.println("Skipping display() - nav touch pending");
    M5.Display.endWrite();
    return;
  }
  M5.Display.endWrite();
  M5.Display.display();
}
