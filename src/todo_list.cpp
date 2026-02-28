#include "globals.h"

// ============= TODO LIST FUNCTIONS =============

void loadCheckedTodos() {
  if (!sdCardAvailable) return;
  
  Serial.println("Loading checked todos...");
  File file = SD.open("/todo_checked.txt");
  if (!file) {
    Serial.println("No checked todos file found (fresh start)");
    return;
  }
  
  int loadedCount = 0;
  String line = "";
  while (file.available()) {
    char c = file.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        // Find matching task in todo list
        for (int i = 0; i < todoCount; i++) {
          if (todoList[i].task == line) {
            todoList[i].checked = true;
            loadedCount++;
            break;
          }
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
  file.close();
  Serial.printf("✓ Loaded %d checked todos\n", loadedCount);
}

void saveCheckedTodos() {
  if (!sdCardAvailable) return;
  
  Serial.println("Saving checked todos...");
  SD.remove("/todo_checked.txt");
  
  File file = SD.open("/todo_checked.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create checked todos file");
    return;
  }
  
  int savedCount = 0;
  for (int i = 0; i < todoCount; i++) {
    if (todoList[i].checked) {
      file.println(todoList[i].task);
      savedCount++;
    }
  }
  file.close();
  Serial.printf("✓ Saved %d checked todos\n", savedCount);
}

void saveTodoList() {
  if (!sdCardAvailable) {
    Serial.println("SD card not available for saving todo list");
    return;
  }
  
  Serial.println("Saving todo list with edited dates...");
  SD.remove("/todo_list.csv");
  
  File csvFile = SD.open("/todo_list.csv", FILE_WRITE);
  if (!csvFile) {
    Serial.println("Failed to create todo list file");
    return;
  }
  
  for (int i = 0; i < todoCount; i++) {
    csvFile.print(todoList[i].date);
    csvFile.print(",");
    csvFile.println(todoList[i].task);
  }
  csvFile.close();
  Serial.printf("✓ Saved %d todo items\n", todoCount);
}

// Remove all checked items from todo list in memory and update CSV
void clearCheckedTodos() {
  int writeIdx = 0;
  int removedCount = 0;
  for (int i = 0; i < todoCount; i++) {
    if (!todoList[i].checked) {
      if (writeIdx != i) {
        todoList[writeIdx] = todoList[i];
      }
      writeIdx++;
    } else {
      removedCount++;
    }
  }
  todoCount = writeIdx;
  Serial.printf("✓ Cleared %d checked todos, %d remaining\n", removedCount, todoCount);
  
  // Recalculate pagination, stay on current page if possible
  lastRenderedTodoItem = -1;
  calculateTodoPages();
  if (currentTodoPage >= totalTodoPages) {
    currentTodoPage = totalTodoPages - 1;
  }
  if (currentTodoPage < 0) currentTodoPage = 0;
  
  // Save updated list to CSV (permanently remove completed items)
  saveTodoList();
  // Save cleared checked state
  saveCheckedTodos();
}

// Helper function to parse date MM/DD/YY to comparable value
long parseDateToNumber(String date) {
  if (date.length() == 0) return 999999999;  // No date goes to end
  
  // Parse MM/DD/YY format
  int firstSlash = date.indexOf('/');
  int secondSlash = date.indexOf('/', firstSlash + 1);
  
  if (firstSlash < 0 || secondSlash < 0) return 999999999;  // Invalid format
  
  int month = date.substring(0, firstSlash).toInt();
  int day = date.substring(firstSlash + 1, secondSlash).toInt();
  int year = date.substring(secondSlash + 1).toInt();
  
  // Convert to 4-digit year (assume 20xx for 00-99)
  if (year < 100) year += 2000;
  
  // Return YYYYMMDD as comparable number
  return (long)year * 10000 + month * 100 + day;
}

void sortTodoListByDate() {
  // Simple bubble sort (adequate for small lists)
  for (int i = 0; i < todoCount - 1; i++) {
    for (int j = 0; j < todoCount - i - 1; j++) {
      long date1 = parseDateToNumber(todoList[j].date);
      long date2 = parseDateToNumber(todoList[j + 1].date);
      
      if (date1 > date2) {
        // Swap items
        TodoItem temp = todoList[j];
        todoList[j] = todoList[j + 1];
        todoList[j + 1] = temp;
      }
    }
  }
  Serial.println("Todo list sorted by date");
}

void loadTodoList() {
  todoCount = 0;
  if (!sdCardAvailable) {
    Serial.println("SD card not available for todo list");
    return;
  }
  
  Serial.println("Loading todo list...");
  
  File csvFile = SD.open("/todo_list.csv");
  if (!csvFile) {
    Serial.println("No todo_list.csv found on SD card");
    return;
  }
  
  String line = "";
  while (csvFile.available() && todoCount < MAX_TODO) {
    char c = csvFile.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        // Parse CSV line: date,task
        int commaPos = line.indexOf(',');
        if (commaPos >= 0) {
          String date = line.substring(0, commaPos);
          String task = line.substring(commaPos + 1);
          
          // Store in todo list
          if (todoCount >= MAX_TODO) { Serial.println("Todo list full"); break; }
          todoList[todoCount].date = date;
          todoList[todoCount].task = task;
          todoList[todoCount].checked = false;
          todoCount++;
          
          Serial.printf("Loaded: %s -> %s\n", date.c_str(), task.c_str());
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
  
  csvFile.close();
  Serial.printf("Loaded %d todo items\n", todoCount);
  
  // Initialize pagination
  todoPageStarts[0] = 0;
  currentTodoPage = 0;
  totalTodoPages = 1;
  lastRenderedTodoItem = -1;
  
  // Load previously checked items
  loadCheckedTodos();
  
  // Sort by date (earliest first, items without date at the end)
  sortTodoListByDate();
}

void calculateTodoPages() {
  if (todoCount == 0) {
    totalTodoPages = 1;
    todoPageStarts[0] = 0;
    return;
  }

  // Layout parameters (must match drawTodoList)
  int fontSizePt = ofrFontLoaded ? 36 : (g_binFont.loaded ? g_binFont.fontSize : 30);
  int columnSpacing = fontSizePt + 15;
  int startY = VERTICAL_TEXT_START_Y;
  int maxY = VERTICAL_TEXT_MAX_Y;
  int leftMargin = 20;  // Tighter left margin for todo list
  int rightMargin = VERTICAL_RIGHT_MARGIN;
  int displayWidth = 540;  // M5Paper S3 width

  const int checkboxSize = 24;
  const int dateSpaceReserved = (8 * 24) + 10;
  const int dateTaskGap = 30;
  int taskTextStartOffset = checkboxSize + 15 + dateSpaceReserved + dateTaskGap;

  todoPageStarts[0] = 0;
  int pageCount = 1;
  int pageStartItem = 0;

  while (pageStartItem < todoCount && pageCount <= MAX_TODO_PAGES) {
    int columnX = displayWidth - rightMargin;
    int lastRendered = pageStartItem - 1;

    for (int i = pageStartItem; i < todoCount && columnX >= (leftMargin + 40); i++) {
      // Each todo item starts at top of its own column
      int y = startY + taskTextStartOffset;

      // Simulate task text layout
      for (int j = 0; j < todoList[i].task.length(); ) {
        unsigned char c = todoList[i].task.charAt(j);
        bool isASCII = (c < 0x80);
        int charSpacing = isASCII ? 30 : 48;

        if (isASCII) {
          // Look ahead to find whole word height
          int wordEnd = j;
          int wordHeight = 0;
          while (wordEnd < todoList[i].task.length()) {
            unsigned char wc = todoList[i].task.charAt(wordEnd);
            if (wc >= 0x80 || wc == ' ' || wc == '\n') break;
            wordHeight += 30;
            wordEnd++;
          }
          // Check if whole word fits
          if (y + wordHeight > maxY - 60) {
            columnX -= columnSpacing;
            if (columnX < (leftMargin + 40)) break;
            y = startY + taskTextStartOffset;
          }
        } else {
          if (y + charSpacing > maxY - 60) {
            columnX -= columnSpacing;
            if (columnX < (leftMargin + 40)) break;
            y = startY + taskTextStartOffset;
          }
        }

        if (isASCII && c == ' ') {
          j++;
          y += 20;  // Space = vertical gap
          continue;
        }

        j += utf8CharLen(c);
        y += charSpacing;
      }

      if (columnX < (leftMargin + 40)) break;

      // Move to next column
      columnX -= columnSpacing;
      lastRendered = i;

      if (columnX < (leftMargin + 40)) break;
    }

    // Check if there are more items for next page
    if (lastRendered < todoCount - 1) {
      if (pageCount < MAX_TODO_PAGES) {
        // Guard: if nothing rendered, force advance to avoid infinite loop
        int nextStart = (lastRendered >= pageStartItem) ? lastRendered + 1 : pageStartItem + 1;
        if (nextStart >= todoCount) break;  // No more items
        todoPageStarts[pageCount] = nextStart;
        pageCount++;
        pageStartItem = nextStart;
      } else {
        break;
      }
    } else {
      break;  // All items fit
    }
  }

  totalTodoPages = pageCount;
  Serial.printf("Todo pages calculated: %d pages for %d items\n", totalTodoPages, todoCount);
}

void drawTodoList() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  // Status bar + nav bar first
  drawStatusBar();
  
  // Calculate all page breaks before drawing nav bar
  calculateTodoPages();
  
  // Clamp page to valid range (may have changed after cleanup or data reload)
  if (currentTodoPage >= totalTodoPages) {
    currentTodoPage = totalTodoPages - 1;
  }
  if (currentTodoPage < 0) currentTodoPage = 0;
  
  {
    bool hasPrev = (currentTodoPage > 0);
    bool hasNext = (currentTodoPage < totalTodoPages - 1);
    drawVerticalNavBar(hasPrev, hasNext);
  }
  
  // Use OFR TTF font if loaded, else binary font, else built-in
  if (!ofrFontLoaded && !g_binFont.loaded) {
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextSize(1.2);
  }
  
  // Vertical text layout parameters
  int fontSizePt = ofrFontLoaded ? 36 : (g_binFont.loaded ? g_binFont.fontSize : 30);
  int charHeight = fontSizePt + 8;
  int columnSpacing = fontSizePt + 15;
  int startY = VERTICAL_TEXT_START_Y;
  int maxY = VERTICAL_TEXT_MAX_Y;
  int leftMargin = 20;  // Tighter left margin for todo list
  int rightMargin = VERTICAL_RIGHT_MARGIN;
  int displayWidth = M5.Display.width();
  int columnX = displayWidth - rightMargin;
  
  // Use pre-calculated page breaks
  int startItem = todoPageStarts[currentTodoPage];
  int endItem = todoCount;
  lastRenderedTodoItem = startItem - 1;
  todoCheckboxCount = 0;  // Reset saved checkbox positions
  todoDateZoneCount = 0;  // Reset saved date touch zones
  
  int currentY = startY;
  
  for (int i = startItem; i < endItem && columnX >= (leftMargin + 40); i++) {
    // Check for nav touch between items
    if (checkNavTouch()) {
      Serial.println("Nav touch during todo render - aborting");
      return;
    }
    
    // Each todo item starts at top of its own column
    currentY = startY;
    int itemStartColumnX = columnX;  // Track starting column
    
    // Draw checkbox
    int checkboxY = currentY;
    int checkboxSize = 24;
    int checkboxX = columnX - checkboxSize/2;
    M5.Display.drawRect(checkboxX, checkboxY, checkboxSize, checkboxSize, TFT_BLACK);
    if (todoList[i].checked) {
      M5.Display.fillRect(checkboxX + 3, checkboxY + 3, checkboxSize - 6, checkboxSize - 6, TFT_BLACK);
    }
    
    int y = currentY + checkboxSize + 15;  // More spacing so text doesn't cover checkbox
    
    // Always reserve space for date (8 chars * 24px + 10px gap) to align all tasks
    const int dateSpaceReserved = (8 * 24) + 10;  // Space for "MM/DD/YY" format
    const int dateTaskGap = 30;  // Gap between date and task
    
    // Save date touch zone (covers checkbox + date area; tapping opens date picker)
    if (todoDateZoneCount < 50) {
      todoDateZones[todoDateZoneCount].itemIdx = i;
      todoDateZones[todoDateZoneCount].touchMinX = columnX - columnSpacing/2;
      todoDateZones[todoDateZoneCount].touchMaxX = columnX + columnSpacing/2;
      todoDateZones[todoDateZoneCount].touchMinY = startY;
      todoDateZones[todoDateZoneCount].touchMaxY = startY + checkboxSize + 15 + dateSpaceReserved + dateTaskGap;
      todoDateZoneCount++;
    }
    
    // Save checkbox position for touch detection (task area only, below date)
    // Pre-allocate slot; touchMinX will be updated after rendering to cover all columns
    int cbSlot = -1;
    if (todoCheckboxCount < 50) {
      cbSlot = todoCheckboxCount;
      todoCheckboxes[cbSlot].x = checkboxX;
      todoCheckboxes[cbSlot].y = checkboxY;
      todoCheckboxes[cbSlot].size = checkboxSize;
      todoCheckboxes[cbSlot].itemIdx = i;
      // Touch area covers task text area only (below date)
      todoCheckboxes[cbSlot].touchMinX = columnX - columnSpacing/2;
      todoCheckboxes[cbSlot].touchMaxX = columnX + columnSpacing/2;
      todoCheckboxes[cbSlot].touchMinY = startY + checkboxSize + 15 + dateSpaceReserved + dateTaskGap;
      todoCheckboxes[cbSlot].touchMaxY = maxY;
      todoCheckboxCount++;
    }
    
    // Draw date if present (in blue, tappable for editing)
    if (todoList[i].date.length() > 0) {
      M5.Display.setTextColor(EPD_DARK_GRAY);
      if (!g_binFont.loaded) {
        M5.Display.setFont(&fonts::efontTW_24);
        M5.Display.setTextSize(1.0);
      }
      
      for (int j = 0; j < todoList[i].date.length(); j++) {
        String ch = todoList[i].date.substring(j, j + 1);
        // Rotate date characters 90° clockwise using sprite
        LGFX_Sprite sprite(&M5.Display);
        if (!sprite.createSprite(48, 48)) {
          y += 24;  // Skip but advance position
          continue;
        }
        sprite.fillSprite(TFT_WHITE);
        if (ofrFontLoaded) {
          ofr.setDrawer(sprite);
          ofr.setFontSize(28);
          ofr.setFontColor(EPD_DARK_GRAY, TFT_WHITE);
          ofr.drawString(ch.c_str(), 10, 10, EPD_DARK_GRAY, TFT_WHITE);
          ofr.drawString(ch.c_str(), 11, 10, EPD_DARK_GRAY, TFT_WHITE);  // Faux bold
          ofr.setDrawer(M5.Display);  // Restore
        } else {
          sprite.setFont(&fonts::efontTW_24);
          sprite.setTextColor(EPD_DARK_GRAY);
          sprite.setTextSize(1.8);
          sprite.drawString(ch, 8, 8);
          sprite.drawString(ch, 9, 8);  // Faux bold
        }
        sprite.pushRotateZoom(&M5.Display, columnX, y + 14, 90, 1.0, 1.0);
        sprite.deleteSprite();
        y += 24;
      }
      M5.Display.setTextColor(TFT_BLACK);
      if (ofrFontLoaded) {
        ofr.setFontColor(TFT_BLACK, TFT_WHITE);
      }
    }
    
    // Always advance y by reserved date space to align all tasks
    y = currentY + checkboxSize + 15 + dateSpaceReserved + dateTaskGap;
    
    // Draw task vertically
    if (!g_binFont.loaded) {
      M5.Display.setFont(&fonts::efontTW_24);
      M5.Display.setTextSize(1.0);
    }
    
    for (int j = 0; j < todoList[i].task.length(); ) {
      unsigned char c = todoList[i].task.charAt(j);
      String ch = "";
      bool isASCII = (c < 0x80);
      int charSpacing = 48;  // Default for Chinese
      
      // For ASCII, check if we need to keep whole word together
      if (isASCII) {
        // Look ahead to find end of current word
        int wordEnd = j;
        int wordHeight = 0;
        
        while (wordEnd < todoList[i].task.length()) {
          unsigned char wc = todoList[i].task.charAt(wordEnd);
          if (wc >= 0x80 || wc == ' ' || wc == '\n') break;  // End of word
          wordHeight += 30;  // Fixed 30px per rotated ASCII character
          wordEnd++;
        }
        
        // Check if whole word fits in current column
        if (y + wordHeight > maxY - 60) {
          // Wrap to next column before starting word
          columnX -= columnSpacing;
          if (columnX < (leftMargin + 40)) {
            break;
          }
          y = currentY + checkboxSize + 15 + dateSpaceReserved + dateTaskGap;
        }
      } else {
        // For Chinese characters, check individual character wrapping
        if (y + charSpacing > maxY - 60) {
          columnX -= columnSpacing;
          if (columnX < (leftMargin + 40)) {
            break;
          }
          y = currentY + checkboxSize + 15 + dateSpaceReserved + dateTaskGap;
        }
      }
      
      int charStart = j;
      uint32_t unicode = utf8Decode(todoList[i].task, j);
      ch = todoList[i].task.substring(charStart, j);
      applyVerticalPunct(ch, unicode);
      
      if (isASCII) {
        // Render spaces as vertical gaps between words
        if (ch == " ") {
          y += 20;  // Space = vertical gap in vertical text
          continue;
        }
        
        // Rotate ASCII 90° clockwise using sprite (use TTF if available for smooth text)
        {
          LGFX_Sprite sprite(&M5.Display);
          if (!sprite.createSprite(48, 48)) {
            y += 30;  // Skip but advance position
            continue;
          }
          sprite.fillSprite(TFT_WHITE);
          if (ofrFontLoaded) {
            ofr.setDrawer(sprite);
            ofr.setFontSize(28);
            ofr.setFontColor(TFT_BLACK, TFT_WHITE);
            ofr.drawString(ch.c_str(), 10, 10, TFT_BLACK, TFT_WHITE);
            ofr.drawString(ch.c_str(), 11, 10, TFT_BLACK, TFT_WHITE);  // Faux bold
            ofr.setDrawer(M5.Display);  // Restore
          } else {
            sprite.setFont(&fonts::efontTW_24);
            sprite.setTextColor(TFT_BLACK);
            sprite.setTextSize(1.8);
            sprite.drawString(ch, 8, 8);
            sprite.drawString(ch, 9, 8);  // Faux bold
          }
          sprite.pushRotateZoom(&M5.Display, columnX, y + 14, 90, 1.0, 1.0);
          sprite.deleteSprite();
          charSpacing = 30;
          y += charSpacing;
        }
      } else {
        if (ofrFontLoaded) {
          ofr.setFontSize(fontSizePt);
          ofr.setFontColor(TFT_BLACK, TFT_WHITE);
          ofr.cdrawString(ch.c_str(), columnX, y - 5, TFT_BLACK, TFT_WHITE);
        } else if (g_binFont.loaded) {
          drawBinFontChar(unicode, columnX - fontSizePt/2, y - 5);
        } else {
          M5.Display.setCursor(columnX - 12, y - 5);
          M5.Display.print(ch);
        }
        y += 48;
      }
    }
    
    // Move to next column from where item actually ended (not from where it started)
    columnX -= columnSpacing;
    currentY = startY;  // Reset to top of column
    lastRenderedTodoItem = i;
    
    // Update touch area to cover all columns this item spans (columnX is now past the last used column)
    if (cbSlot >= 0) {
      // columnX just moved past the item, so columnX + columnSpacing is the last column used
      // touchMinX should be the leftmost column edge
      todoCheckboxes[cbSlot].touchMinX = columnX + columnSpacing - columnSpacing/2;
    }
    
    // Check if we have space for next item
    if (columnX < (leftMargin + 40)) {
      break;  // No more space on this page
    }
  }
  
  // Page indicator next to right arrow, larger font
  M5.Display.setFont(&fonts::efontTW_24);
  M5.Display.setTextSize(1.0);
  M5.Display.setCursor(155, 910);
  M5.Display.printf("%d/%d", currentTodoPage + 1, totalTodoPages);
  
  // "新增" (Add new) button
  {
    int addBtnX = 210, addBtnY = 900, addBtnW = 60, addBtnH = 44;
    M5.Display.drawRoundRect(addBtnX, addBtnY, addBtnW, addBtnH, 6, TFT_BLACK);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(addBtnX + 18, addBtnY + 8);
    M5.Display.print("+");
    M5.Display.setTextSize(1);
  }

  // "清除" (Clear completed) button - only show if there are checked items
  int checkedCount = 0;
  for (int i = 0; i < todoCount; i++) {
    if (todoList[i].checked) checkedCount++;
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

void drawTodoDatePicker() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
  drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
  drawReturnButton();
  
  int W = M5.Display.width();   // 540
  int centerX = W / 2;
  
  // Title: Year + Month centered
  char title[32];
  snprintf(title, sizeof(title), "%d年 %d月", todoDatePickerYear, todoDatePickerMonth);
  drawSystemTextCentered(title, centerX, 15, 36);
  
  // Show which task we're editing
  if (todoDatePickerItem >= 0 && todoDatePickerItem < todoCount) {
    String taskPreview = todoList[todoDatePickerItem].task;
    if (taskPreview.length() > 30) taskPreview = taskPreview.substring(0, 30) + "...";
    // Draw task preview below title
    drawSystemTextCentered(taskPreview.c_str(), centerX, 55, 20);
  }
  
  // Horizontal line
  M5.Display.drawLine(10, 80, W - 10, 80, TFT_BLACK);
  
  // Day of week headers
  const char* headers[] = {"日", "一", "二", "三", "四", "五", "六"};
  int cellW = W / 7;
  int headerY = 90;
  for (int i = 0; i < 7; i++) {
    drawSystemTextCentered(headers[i], cellW / 2 + i * cellW, headerY, 28);
  }
  
  // Calendar grid
  int totalDays = solarMonthDays(todoDatePickerYear, todoDatePickerMonth);
  int firstDow = dayOfWeek(todoDatePickerYear, todoDatePickerMonth, 1);
  
  int cellH = 110;
  int gridStartY = 130;
  int row = 0, col = firstDow;
  
  // Get today's date and current item's date for highlighting
  struct tm ti;
  getLocalTime(&ti);
  int todayY = ti.tm_year + 1900, todayM = ti.tm_mon + 1, todayD = ti.tm_mday;
  
  // Parse item's current date
  int itemDay = -1;
  if (todoDatePickerItem >= 0 && todoDatePickerItem < todoCount) {
    String d = todoList[todoDatePickerItem].date;
    if (d.length() > 0) {
      int s1 = d.indexOf('/');
      int s2 = d.indexOf('/', s1 + 1);
      if (s1 > 0 && s2 > 0) {
        int im = d.substring(0, s1).toInt();
        int id = d.substring(s1 + 1, s2).toInt();
        int iy = d.substring(s2 + 1).toInt();
        if (iy < 100) iy += 2000;
        if (iy == todoDatePickerYear && im == todoDatePickerMonth) {
          itemDay = id;
        }
      }
    }
  }
  
  for (int d = 1; d <= totalDays; d++) {
    int cx = col * cellW + cellW / 2;
    int cy = gridStartY + row * cellH;
    
    bool isToday = (todoDatePickerYear == todayY && todoDatePickerMonth == todayM && d == todayD);
    bool isSelected = (d == itemDay);
    
    if (isSelected) {
      // Highlight selected date with filled rounded rect
      M5.Display.fillRoundRect(col * cellW + 4, cy - 5, cellW - 8, cellH - 6, 6, TFT_BLACK);
    } else if (isToday) {
      // Outline today
      M5.Display.drawRoundRect(col * cellW + 4, cy - 5, cellW - 8, cellH - 6, 6, TFT_BLACK);
    }
    
    char dayStr[8];
    snprintf(dayStr, sizeof(dayStr), "%d", d);
    uint16_t dayColor = isSelected ? TFT_WHITE : (col == 0 ? EPD_DARK_GRAY : TFT_BLACK);
    uint16_t dayBg = isSelected ? TFT_BLACK : TFT_WHITE;
    drawSystemTextCentered(dayStr, cx, cy + 20, 36, dayColor, dayBg);
    
    col++;
    if (col >= 7) { col = 0; row++; }
  }
  
  // "清除日期" (Clear date) button
  int clrBtnX = 160, clrBtnY = 900, clrBtnW = 140, clrBtnH = 44;
  M5.Display.drawRoundRect(clrBtnX, clrBtnY, clrBtnW, clrBtnH, 6, TFT_BLACK);
  drawSystemTextCentered("清除日期", clrBtnX + clrBtnW/2, clrBtnY + 8, 24);
  
  // "今天" (Today) button
  int todayBtnX = 320, todayBtnY = 900, todayBtnW = 100, todayBtnH = 44;
  M5.Display.drawRoundRect(todayBtnX, todayBtnY, todayBtnW, todayBtnH, 6, TFT_BLACK);
  drawSystemTextCentered("今天", todayBtnX + todayBtnW/2, todayBtnY + 8, 24);
  
  M5.Display.endWrite();
  M5.Display.display();
}
