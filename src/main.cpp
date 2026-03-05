// M5Stack Paper S3 Chinese E-Book Reader
// Main entry point: setup() and loop()

#include "globals.h"
#include "s3cover_jpg.h"
#include "sleeping_jpg.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

// RTC_DATA_ATTR survives deep sleep
RTC_DATA_ATTR int bootCount = 0;

// Check if external power (USB) is connected
bool isExternalPowerConnected() {
  return (M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging)
      || (M5.Power.getBatteryLevel() == -1);  // -1 sometimes means USB-only
}

void enterDeepSleep() {
  // Don't sleep when external power is connected
  if (isExternalPowerConnected()) {
    Serial.println("External power connected - sleep disabled");
    return;
  }
  
  Serial.println("Preparing for deep sleep...");
  
  // Stop web server first
  if (webServerRunning) {
    stopWebServer();
  }
  
  // Disconnect WiFi to save power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  // Save current state
  prefs.begin("ereader", false);
  prefs.putInt("page", currentPage);
  prefs.putInt("fontIdx", selectedFontIndex);
  prefs.putInt("rdFontSz", readingFontSize);
  prefs.end();
  
  // Show welcome page with "休眠中 Sleeping" in bottom-right corner
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();

  int w = M5.Display.width();
  int h = M5.Display.height();

  // Draw the sleeping wallpaper (full screen)
  M5.Display.drawJpg(sleeping_jpg, sleeping_jpg_len, 0, 0, w, h);

  // Draw vertical motto on sleep screen
  drawMottoOnSleep();

  // Small "休眠中 Sleeping" text at bottom-right corner
  drawSystemText("休眠中", w - 180, h - 30, 20);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(w - 90, h - 25);
  M5.Display.print("Sleeping");
  M5.Display.endWrite();

  M5.Display.display();
  delay(2000);
  
  // Configure wake-up sources
  // 1. Touch wake: GT911 INT pin on GPIO 21 - goes LOW on touch
  // GT911 may power down during deep sleep, making touch wake unreliable.
  // We enable it as a best-effort source.
  pinMode(GPIO_NUM_21, INPUT_PULLUP);
  delay(50);  // Let pin settle with pull-up
  
  // Check if pin is already LOW (touch controller holding it) - if so, wait
  int pinWait = 0;
  while (digitalRead(GPIO_NUM_21) == LOW && pinWait < 20) {
    delay(50);
    pinWait++;
  }
  
  if (digitalRead(GPIO_NUM_21) != LOW) {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);  // Wake on LOW (touch)
    Serial.println("Touch wake enabled (GPIO 21)");
  } else {
    Serial.println("WARNING: Touch INT pin stuck LOW - touch wake disabled");
  }
  
  // 2. USB charge wake: GPIO 4 is the charge status pin on M5Paper S3.
  // It goes LOW when USB is connected (charging). Use ext1 to wake on LOW.
  gpio_pullup_en(GPIO_NUM_4);
  gpio_pulldown_dis(GPIO_NUM_4);
  if (digitalRead(GPIO_NUM_4) != LOW) {
    esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_4, ESP_EXT1_WAKEUP_ANY_LOW);
    Serial.println("USB charge wake enabled (GPIO 4)");
  } else {
    Serial.println("WARNING: Charge pin already LOW - USB wake skipped");
  }
  
  // 3. Timer wake: always enabled as a reliable fallback
  // Wakes every 5 minutes to check for touch/activity, refresh motto, etc.
  // The device will re-enter sleep immediately if no interaction occurs.
  esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);  // 5 minutes
  Serial.println("Timer wake enabled (5 min)");
  
  Serial.println("Entering deep sleep now");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootCount++;
  
  // Check wake-up reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  Serial.println("\n\n=== M5Paper S3 E-Book Reader ===");
  Serial.printf("Boot #%d, Wakeup cause: %d\n", bootCount, wakeup_reason);
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Touch wake-up - resuming");
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("USB cable wake-up (charging detected) - resuming");
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Timer wake-up - checking for interaction...");
    
    // Timer wake: briefly init M5 to check touch, then go back to sleep
    // unless someone is actively touching the screen
    auto cfgTimer = M5.config();
    cfgTimer.clear_display = false;
    M5.begin(cfgTimer);
    delay(200);
    M5.update();
    
    auto touch = M5.Touch.getDetail();
    if (touch.isPressed()) {
      Serial.println("Touch detected during timer wake - resuming fully");
      // Fall through to normal boot
    } else {
      // No touch — check if external power was just connected
      bool usbConnected = isExternalPowerConnected();
      if (usbConnected) {
        Serial.println("USB power detected during timer wake - resuming fully");
        // Fall through to normal boot
      } else {
        Serial.println("No interaction - refreshing sleep screen and going back to sleep");
        // Optionally refresh the motto on the sleep screen
        // (keeps the display fresh and shows a new motto)
        enterDeepSleep();
        // enterDeepSleep won't return (unless USB is connected)
        // If it returned, USB was just plugged in — fall through
      }
    }
  }
  
  auto cfg = M5.config();
  // E-ink retains its image during deep sleep, so no need to clear on wake.
  // clear_display = true causes a hardware reset that flashes partial buffer content.
  cfg.clear_display = false;
  M5.begin(cfg);
  
  Serial.println("M5 initialized");
  
  // Show loading screen (skip for deep sleep wake to avoid extra refresh)
  bool isDeepSleepWake = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || 
                          wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);
  if (!isDeepSleepWake) {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    
    // Status bar: time (top-left) + battery (top-right)
    drawStatusBar();
    
    // Loading text centered
    drawSystemTextCentered("載入中...", M5.Display.width() / 2, M5.Display.height() / 2 - 30, 36);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("Loading...", M5.Display.width() / 2, M5.Display.height() / 2 + 30);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.endWrite();
    
    M5.Display.display();
    Serial.println("Loading screen displayed");
  } else {
    Serial.println("Skipping loading screen (deep sleep wake)");
  }
  
  // Create mutex for SD card access
  sdMutex = xSemaphoreCreateMutex();
  if (sdMutex == NULL) {
    Serial.println("Failed to create SD mutex!");
  }
  
  // Initialize SD card EARLY, before display and preferences
  Serial.println("Initializing SD card with M5Paper S3 pins...");
  sdSPI.begin(39, 40, 38, 47);  // SCK, MISO, MOSI, CS
  
  if (SD.begin(47, sdSPI, 25000000)) {
    sdCardAvailable = true;
    Serial.println("✓ SD Card initialized");
    // CRITICAL: Let SD subsystem fully initialize before using it
    delay(500);
    yield();
    
    // Clean up macOS dot files
    cleanupMacOSFiles();
    
    // Scan books immediately while SD is fresh
    Serial.println("Pre-scanning books...");
    scanBooks();
    if (bookCount > 0) {
      Serial.printf("✓ Loaded %d books\n", bookCount);
    }
    
    // Load shopping list
    Serial.println("Loading shopping list...");
    loadShoppingList();
    if (shoppingCount > 0) {
      Serial.printf("✓ Loaded %d shopping items\n", shoppingCount);
    }
    
    // Load todo list (for dashboard badge count)
    Serial.println("Loading todo list...");
    loadTodoList();
    if (todoCount > 0) {
      Serial.printf("✓ Loaded %d todo items\n", todoCount);
    }
    
    // Load mottos for sleep screen
    Serial.println("Loading mottos...");
    loadMottos();

    // Load custom calendar events from SD card
    Serial.println("Loading custom calendar events...");
    loadCustomEvents();

    // Try to load a TTF font for better character coverage
    Serial.println("Scanning and loading fonts...");
    
    // Initialize OpenFontRender
    ofr.setSerial(Serial);
    
    // Scan SD card for all available fonts (.ttf, .ttc, .bin)
    scanFontFiles();
    
    numFonts = fontFileCount;
    
    // First priority: find and load GenYoMinTW-Regular.ttf as system font
    bool fontLoaded = false;
    for (int fi = 1; fi < fontFileCount; fi++) {
      String fname = fontFileList[fi];
      if (fname.equalsIgnoreCase("GenYoMinTW-Regular.ttf")) {
        Serial.printf("Loading default system font: %s\n", fname.c_str());
        systemFontIndex = fi;
        systemFontFile = fname;
        if (loadTTFFont(fname.c_str(), 30)) {
          fontLoaded = true;
          Serial.println("✓ GenYoMinTW-Regular.ttf loaded as default system font!");
        }
        break;
      }
    }
    
    // Restore saved reading font selection
    readingFontIndex = loadPrefInt("ereader", "fontIdx", 0);
    if (readingFontIndex >= fontFileCount) readingFontIndex = 0;
    selectedFontIndex = readingFontIndex;
    
    // If no system font found, try any TTF as system font
    if (!fontLoaded) {
      for (int fi = 1; fi < fontFileCount; fi++) {
        String fname = fontFileList[fi];
        if (fname.endsWith(".ttf") || fname.endsWith(".TTF") ||
            fname.endsWith(".ttc") || fname.endsWith(".TTC")) {
          Serial.printf("Using %s as system font (fallback)\n", fname.c_str());
          systemFontIndex = fi;
          systemFontFile = fname;
          if (loadTTFFont(fname.c_str(), 30)) {
            fontLoaded = true;
            break;
          }
        }
      }
    }
    
    // Last resort: try .bin font
    if (!fontLoaded) {
      String bestBinFont = "";
      int bestBinPriority = 0;
      for (int fi = 1; fi < fontFileCount; fi++) {
        String fname = fontFileList[fi];
        if (fname.endsWith(".bin") || fname.endsWith(".BIN")) {
          int priority = 10;
          if (fname.indexOf("MingLiU") >= 0 || fname.indexOf("mingliu") >= 0) priority += 5;
          if (fname.indexOf("40pt") >= 0) priority += 10;
          if (fname.indexOf("30pt") >= 0) priority += 5;
          if (priority > bestBinPriority) {
            bestBinFont = fname;
            bestBinPriority = priority;
          }
        }
      }
      if (bestBinFont.length() > 0) {
        Serial.printf("Loading best .bin font: %s\n", bestBinFont.c_str());
        if (loadBinaryFont(bestBinFont.c_str())) {
          useTTFFont = true;
          fontLoaded = true;
        }
      }
    }
    
    if (!fontLoaded) {
      Serial.println("\n=== Font Loading Summary ===");
      Serial.println("No custom fonts loaded - using built-in CJK fonts");
      
      // Fallback to built-in fonts
      Serial.println("Using fonts::efontCN_24 (built-in)");
      M5.Display.setFont(&fonts::efontCN_24);
      M5.Display.setTextSize(1.2);
      
      canvas.deleteSprite();
    }
  } else {
    sdCardAvailable = false;
    Serial.println("✗ SD Card failed");
  }
  
  Serial.printf("Display: %dx%d\n", M5.Display.width(), M5.Display.height());
  
  // Check available PSRAM
  Serial.println("=== Memory Information ===");
  Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.printf("Total Heap: %d bytes\n", ESP.getHeapSize());
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  // E-ink initialization
  Serial.println("Setting up e-ink display...");
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  
  Serial.println("Display ready");
  
  // Load preferences
  currentPage = loadPrefInt("ereader", "page", 0);
  readingFontSize = loadPrefInt("ereader", "rdFontSz", DEFAULT_READING_FONT_SIZE);
  readingFontSize = constrain(readingFontSize, MIN_READING_FONT_SIZE, MAX_READING_FONT_SIZE);
  updateBytesPerPage();
  
  Serial.println("Preferences loaded");
  
  // Load WiFi config first (need timezone before RTC restore)
  Serial.println("\n=== Loading Configuration ===");
  loadWiFiConfig();
  Serial.printf("Config loaded - WiFi configured: %s\n", wifiConfig.configured ? "YES" : "NO");
  Serial.println("Timezone: " + timeConfig.timezone);

  // Load auto-sleep setting
  prefs.begin("ereader", true);
  autoSleepEnabled = prefs.getBool("autoSleep", false);  // Default: disabled
  prefs.end();
  Serial.printf("Auto-sleep setting loaded: %s\n", autoSleepEnabled ? "ENABLED" : "DISABLED");
  
  // BLE Proximity Unlock — deferred start (not at boot to avoid memory issues)
  // Config is loaded but init is deferred to after display setup
  if (bleUnlockConfig.enabled && bleUnlockConfig.password.length() > 0) {
    Serial.println("BLE Unlock: configured, will start after welcome screen");
  }
  
  // Set timezone BEFORE reading RTC so mktime/getLocalTime work correctly
  configTzTime(timeConfig.timezone.c_str(), ntpServer);
  Serial.println("✓ Timezone configured: " + timeConfig.timezone);
  
  // Try to read time from hardware RTC
  Serial.println("\n=== RTC Time Check ===");
  m5::rtc_datetime_t rtcTime;
  M5.Rtc.getDateTime(&rtcTime);
  Serial.printf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
rtcTime.date.year, rtcTime.date.month, rtcTime.date.date,
rtcTime.time.hours, rtcTime.time.minutes, rtcTime.time.seconds);
  
  // Check if RTC time looks valid (year should be reasonable)
  if (rtcTime.date.year >= 2024 && rtcTime.date.year <= 2100) {
    // Set system time from RTC (RTC stores local time)
    struct tm timeinfo;
    timeinfo.tm_year = rtcTime.date.year - 1900;
    timeinfo.tm_mon = rtcTime.date.month - 1;
    timeinfo.tm_mday = rtcTime.date.date;
    timeinfo.tm_hour = rtcTime.time.hours;
    timeinfo.tm_min = rtcTime.time.minutes;
    timeinfo.tm_sec = rtcTime.time.seconds;
    timeinfo.tm_wday = rtcTime.date.weekDay;
    timeinfo.tm_isdst = -1;  // Let mktime determine DST
    
    time_t t = mktime(&timeinfo);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
    
    timeConfig.timeSynced = true;
    Serial.println("✓ System time initialized from RTC");
  } else {
    Serial.println("✗ RTC time appears invalid (will sync from NTP if WiFi connects)");
  }
  Serial.println("=====================\n");
  
  // Auto-connect WiFi
  Serial.println("\n=== WiFi Auto-Connect ===");
  if (wifiConfig.configured) {
    Serial.printf("SSID: %s\n", wifiConfig.ssid.c_str());
    Serial.println("Attempting auto-connect...");
    if (connectToWiFi()) {
      Serial.println("✓ Auto-connected to WiFi successfully!");
    } else {
      Serial.println("✗ Auto-connect failed");
    }
  } else {
    Serial.println("No WiFi config found - skipping auto-connect");
  }
  Serial.println("========================\n");
  
  // Load web server preference
  webServerEnabled = true;  // Always enable web server by default
  savePrefBool("m5paper", "webServer", true);
  usbMSCEnabled = loadPrefBool("m5paper", "usbMSC", false);
  useSDCardIcons = loadPrefBool("m5paper", "sdIcons", false);
  useSxwnlCalendar = loadPrefBool("m5paper", "sxwnl", false);
  Serial.printf("Web server enabled: %s\n", webServerEnabled ? "YES" : "NO");
  Serial.printf("USB MSC enabled: %s\n", usbMSCEnabled ? "YES" : "NO");
  Serial.printf("Use SD card icons: %s\n", useSDCardIcons ? "YES" : "NO");
  Serial.printf("Calendar method: %s\n", useSxwnlCalendar ? "sxwnl" : "Meeus");
  
  // Start web server at startup if WiFi connected
  if (WiFi.status() == WL_CONNECTED) {
    startWebServer();
  }
  
  // Don't auto-start USB MSC on boot - user must enable manually
  // USB MSC disables SD card access for the device
  if (usbMSCEnabled) {
    Serial.println("USB MSC auto-start disabled - enable manually from Setup");
    // startUSBMSC();  // Commented out - must be started manually
  }
  
  // Show welcome screen
  Serial.println("Drawing welcome screen...");
  drawWelcome();
  
  // Start BLE Proximity Unlock AFTER everything else is initialized
  if (bleUnlockConfig.enabled && bleUnlockConfig.password.length() > 0) {
    Serial.println("\n=== Starting BLE Proximity Unlock ===");
    delay(500);  // Let system stabilize
    bleUnlockInit();
  }
  
  lastActivityTime = millis();
  Serial.println("Setup complete!");
}

void loop() {
  M5.update();
  
  // Handle web server if enabled
  if (webServerEnabled && webServer != nullptr) {
    // Start server if WiFi connected but server not running
    if (WiFi.status() == WL_CONNECTED && !webServerRunning) {
      startWebServer();
    }
    // Stop server if WiFi disconnected
    else if (WiFi.status() != WL_CONNECTED && webServerRunning) {
      stopWebServer();
    }
    // Handle client requests
    if (webServerRunning) {
      webServer->handleClient();
    }
  }
  
  // Auto-refresh weather every 15 minutes while on weather screen
  if (currentMode == MODE_WEATHER && weatherData.valid && 
      (millis() - weatherData.fetchTime > WEATHER_REFRESH_INTERVAL)) {
    Serial.println("Weather auto-refresh triggered");
    if (fetchWeather()) {
      drawWeather(true);
    }
  }
  
  // Wallpaper auto-rotate: change wallpaper every 60 seconds when active
  if (wallpaperRotateActive && currentMode == MODE_WALLPAPER && wallpaperCount > 1) {
    if (millis() - wallpaperRotateLastChange > 60000) {
      wallpaperRotateLastChange = millis();
      int newIdx;
      do {
        newIdx = random(0, wallpaperCount);
      } while (newIdx == selectedWallpaper && wallpaperCount > 1);
      Serial.printf("Auto-rotate: switching to wallpaper %d\n", newIdx);
      drawWallpaperWithIndex(newIdx);
    }
  }

  // Idle sleep: auto-sleep after 10 minutes of inactivity (when enabled and no external power)
  if (autoSleepEnabled) {
    bool hasExternalPower = isExternalPowerConnected();
    if (lastActivityTime > 0 && !hasExternalPower && (millis() - lastActivityTime > IDLE_SLEEP_TIMEOUT)) {
      if (currentMode != MODE_CLOCK) {
        Serial.println("Idle timeout - entering deep sleep");
        enterDeepSleep();
      }
    }
  }
  
  // Auto-refresh clock - full refresh every minute, second hand update every second
  if (currentMode == MODE_CLOCK && timeConfig.timeSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      // Check if minute has changed - do full refresh
      if (timeinfo.tm_min != lastClockMinute) {
        Serial.println("Clock minute changed - refresh");
        drawClock();
      }
    }
  }
  
  // Handle Serial commands for WiFi password entry (non-blocking)
  static String serialCmdBuffer;
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      serialCmdBuffer.trim();
      if (serialCmdBuffer.startsWith("wifi_password=")) {
        passwordInput = serialCmdBuffer.substring(14);  // Get everything after "wifi_password="
        Serial.println("Password set: " + passwordInput);
        
        if (currentMode == MODE_SETUP && showingKeyboard) {
          drawWiFiSetup();  // Refresh screen to show entered password
        }
      }
      serialCmdBuffer = "";
    } else {
      serialCmdBuffer += ch;
      if (serialCmdBuffer.length() > 256) {
        serialCmdBuffer = "";
      }
    }
  }
  
  // Touch handling — check for pending nav touch (from mid-render) or new touch
  int x = -1, y = -1;
  bool hasTouchEvent = false;
  
  if (pendingNavTouch) {
    // Process touch detected during rendering
    x = pendingTouchX;
    y = pendingTouchY;
    pendingNavTouch = false;
    pendingTouchX = 0;
    pendingTouchY = 0;
    hasTouchEvent = true;
    Serial.printf("Processing pending nav touch: %d, %d (mode=%d)\n", x, y, currentMode);
  } else {
    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
      x = touch.x;
      y = touch.y;
      hasTouchEvent = true;
    }
  }
  
  if (hasTouchEvent) {
    lastActivityTime = millis();
    lastTouchProcessedTime = millis();
    
    DEBUG_LOG_THROTTLE(200, "Touch: %d, %d (mode=%d)", x, y, currentMode);
    
    if (currentMode == MODE_WELCOME) {
      // Any touch on welcome screen goes to dashboard
      currentMode = MODE_DASHBOARD;
      drawDashboard();
    } 
    else if (currentMode == MODE_DASHBOARD) {
      // Detect which icon was touched
      layoutIcons();
      for (int i = 0; i < kIconCount; ++i) {
        const auto& icon = g_icons[i];
        if (x >= icon.x && x <= (icon.x + icon.w) &&
            y >= icon.y && y <= (icon.y + icon.h)) {
          DEBUG_LOG("Icon %d touched: %s", i, icon.label);
          
          // Icon 0 is E-Book
          if (i == 0) {
            currentMode = MODE_BOOK_LIST;
            drawBookList();
          }
          // Icon 1 is Calendar (日曆/農民曆)
          else if (i == 1) {
            DEBUG_LOG("Opening calendar...");
            currentMode = MODE_CALENDAR;
            drawCalendar();
          }
          // Icon 2 is Todo List (待辦事項)
          else if (i == 2) {
            DEBUG_LOG("Opening todo list...");
            currentMode = MODE_TODO_LIST;
            drawTodoList();
          }
          // Icon 3 is Shopping List (採辦)
          else if (i == 3) {
            DEBUG_LOG("Opening shopping list...");
            currentMode = MODE_SHOPPING_LIST;
            drawShoppingList();
          }
          // Icon 4 is Weather (天氣)
          else if (i == 4) {
            DEBUG_LOG("Opening weather...");
            currentMode = MODE_WEATHER;
            drawWeather(true);
          }
          // Icon 5 is Wallpaper (壁紙)
          else if (i == 5) {
            DEBUG_LOG("Opening wallpaper list...");
            currentMode = MODE_WALLPAPER_LIST;
            drawWallpaperList();
          }
          // Icon 6 is Settings (設定)
          else if (i == 6) {
            DEBUG_LOG("Opening settings...");
            currentMode = MODE_SETUP;
            setupSubmenu = 0;  // Start at main setup menu
            setupMenuPage = 0;
            drawSetupMenu();
          }
          // Icon 7 is Sleep — show sleeping page for testing (tap to cycle mottos)
          else if (i == 7) {
            Serial.println("Sleep icon touched - entering motto test mode...");
            currentMode = MODE_MOTTO_TEST;
            drawMottoScreen();
          }
          else {
            Serial.printf("App %d not implemented yet\n", i);
          }
          break;
        }
      }
    }
    else if (currentMode == MODE_CALENDAR) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Calendar back - returning to dashboard");
        calendarDayOffset = 0;  // Reset to today
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
      // Left icon (tomorrow) - lower-left nav bar
      else if (touchedPrevPage(x, y)) {
        DEBUG_LOG("Calendar: next day");
        calendarDayOffset++;
        drawCalendar();
      }
      // Right icon (yesterday) - lower-left nav bar
      else if (touchedNextPage(x, y)) {
        DEBUG_LOG("Calendar: previous day");
        calendarDayOffset--;
        drawCalendar();
      }
      // Tap on big date number - open date picker
      else if (x > 100 && x < 440 && y > 80 && y < 290) {
        DEBUG_LOG("Calendar: open date picker");
        int py, pm, pd, pw;
        getDateWithOffset(calendarDayOffset, py, pm, pd, pw);
        pickerYear = py;
        pickerMonth = pm;
        pickerSelectedYear = py;
        pickerSelectedMonth = pm;
        pickerSelectedDay = pd;
        currentMode = MODE_CALENDAR_PICKER;
        drawCalendarPicker();
      }
    }
    else if (currentMode == MODE_CALENDAR_PICKER) {
      int W = M5.Display.width();
      if (touchedReturnButton(x, y)) {
        DEBUG_LOG("Picker: back to calendar");
        currentMode = MODE_CALENDAR;
        drawCalendar();
      }
      // Tap on title area (year/month line) → open year-month popup
      else if (y < 65 && x > 80 && x < W - 80) {
        Serial.println("Picker: open year-month popup");
        ymPickerYear = pickerYear;
        ymPickerMonth = pickerMonth;
        currentMode = MODE_CALENDAR_YEAR_MONTH;
        drawCalendarYearMonth();
      }
      // Left icon (next month)
      else if (touchedPrevPage(x, y)) {
        pickerMonth++;
        if (pickerMonth > 12) { pickerMonth = 1; pickerYear++; }
        Serial.printf("Picker: next month %d/%d\n", pickerYear, pickerMonth);
        drawCalendarPicker();
      }
      // Right icon (previous month)
      else if (touchedNextPage(x, y)) {
        pickerMonth--;
        if (pickerMonth < 1) { pickerMonth = 12; pickerYear--; }
        Serial.printf("Picker: prev month %d/%d\n", pickerYear, pickerMonth);
        drawCalendarPicker();
      }
      // Tap on a day cell
      else if (y >= 115 && y < 900) {
        int cellW = W / 7;
        int cellH = 110;
        int col = x / cellW;
        int row = (y - 115) / cellH;
        int firstDow = dayOfWeek(pickerYear, pickerMonth, 1);
        int dayIdx = row * 7 + col - firstDow + 1;
        // Gregorian reform: Oct 1582, days 5-14 don't exist, grid positions shift
        if (pickerYear == 1582 && pickerMonth == 10 && dayIdx >= 5) dayIdx += 10;
        int totalDays = solarMonthDays(pickerYear, pickerMonth);
        if (dayIdx >= 1 && dayIdx <= totalDays) {
          Serial.printf("Picker: selected %d/%d/%d\n", pickerYear, pickerMonth, dayIdx);
          // Calculate offset from today
          struct tm ti;
          getLocalTime(&ti);
          long todayJDN = solarDayNumber(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
          long selJDN = solarDayNumber(pickerYear, pickerMonth, dayIdx);
          calendarDayOffset = (int)(selJDN - todayJDN);
          currentMode = MODE_CALENDAR;
          drawCalendar();
        }
      }
      // "今天" (Today) button - jump to today's month
      else if (x >= 200 && x <= 300 && y >= 900 && y <= 944) {
        Serial.println("Picker: jump to today's month");
        struct tm ti;
        getLocalTime(&ti);
        pickerYear = ti.tm_year + 1900;
        pickerMonth = ti.tm_mon + 1;
        drawCalendarPicker();
      }
    }
    else if (currentMode == MODE_CALENDAR_YEAR_MONTH) {
      // Layout constants (must match drawCalendarYearMonth)
      int padX = 40, padY = 130, btnW = 140, btnH = 60, gapX = 15, gapY = 10;
      int divY = padY + 4 * (btnH + gapY) + 5;
      int monthY = divY + 15;
      int mBtnW = 140, mBtnH = 56, mGapX = 15, mGapY = 10, mPadX = 40;
      int bottomY = monthY + 4 * (mBtnH + mGapY) + 10;
      int centerX = M5.Display.width() / 2;

      // Helper lambda: partial-update the year display row only
      auto updateYearDisplay = [&]() {
        int yrX = centerX - 100, yrY = 65, yrW = 220, yrH = 55;
        M5.Display.setEpdMode(epd_mode_t::epd_fast);
        M5.Display.startWrite();
        M5.Display.fillRect(yrX, yrY, yrW, yrH, TFT_BLACK);
        M5.Display.endWrite();
        M5.Display.display();
        M5.Display.startWrite();
        M5.Display.fillRect(yrX, yrY, yrW, yrH, TFT_WHITE);
        int xp = centerX - 80;
        if (ymPickerYear > 0) {
          // Draw each digit of the year
          char buf[8];
          snprintf(buf, sizeof(buf), "%d", ymPickerYear);
          char single[4] = {0, 0, 0, 0};
          for (int i = 0; buf[i]; i++) {
            single[0] = buf[i];
            xp += drawSystemText(single, xp, 72, 36);
          }
        } else {
          for (int i = 0; i < 4; i++)
            M5.Display.fillRect(xp + i * 28, 105, 22, 3, TFT_BLACK);
          xp += 112;
        }
        drawSystemText("年", xp, 72, 36);
        M5.Display.endWrite();
        M5.Display.display();
        M5.Display.setEpdMode(epd_mode_t::epd_quality);  // Restore quality mode
      };

      // Number pad area (rows 0-3, cols 0-2)
      if (y >= padY && y < padY + 4 * (btnH + gapY)) {
        int r = (y - padY) / (btnH + gapY);
        int c = (x - padX) / (btnW + gapX);
        if (c >= 0 && c < 3 && r >= 0 && r < 4) {
          int bx = padX + c * (btnW + gapX);
          int by = padY + r * (btnH + gapY);
          if (x >= bx && x < bx + btnW && y >= by && y < by + btnH) {
            if (r == 3 && c == 0) {
              // "清" - clear year
              ymPickerYear = 0;
              Serial.println("YM: clear year");
              updateYearDisplay();
            } else if (r == 3 && c == 2) {
              // "刪" - backspace
              ymPickerYear /= 10;
              Serial.printf("YM: backspace -> %d\n", ymPickerYear);
              updateYearDisplay();
            } else {
              // Digit 0-9
              int digit;
              if (r == 3) digit = 0;  // bottom-center is 0
              else digit = r * 3 + c + 1;  // 1-9
              if (ymPickerYear < 1000) {
                ymPickerYear = ymPickerYear * 10 + digit;
                Serial.printf("YM: digit %d -> year %d\n", digit, ymPickerYear);
                updateYearDisplay();
              }
            }
          }
        }
      }
      // Month grid area
      else if (y >= monthY && y < monthY + 4 * (mBtnH + mGapY)) {
        int r = (y - monthY) / (mBtnH + mGapY);
        int c = (x - mPadX) / (mBtnW + mGapX);
        if (c >= 0 && c < 3 && r >= 0 && r < 4) {
          int bx = mPadX + c * (mBtnW + mGapX);
          int by = monthY + r * (mBtnH + mGapY);
          if (x >= bx && x < bx + mBtnW && y >= by && y < by + mBtnH) {
            int mIdx = r * 3 + c + 1;  // 1-12
            if (mIdx != ymPickerMonth) {
              int oldMonth = ymPickerMonth;
              ymPickerMonth = mIdx;
              Serial.printf("YM: selected month %d\n", mIdx);

              static const char* mNames[] = {
                "1","2","3","4","5","6",
                "7","8","9","10","11","12"
              };

              M5.Display.setEpdMode(epd_mode_t::epd_fast);

              // Collect rects to update (old + new)
              struct { int x, y, w, h; } rects[2];
              int nR = 0;
              // New selection
              rects[nR++] = {bx, by, mBtnW, mBtnH};
              // Old selection (if valid)
              if (oldMonth >= 1 && oldMonth <= 12) {
                int or2 = (oldMonth - 1) / 3;
                int oc2 = (oldMonth - 1) % 3;
                rects[nR++] = {mPadX + oc2 * (mBtnW + mGapX),
                               monthY + or2 * (mBtnH + mGapY), mBtnW, mBtnH};
              }

              // Pass 1: flash black
              M5.Display.startWrite();
              for (int i = 0; i < nR; i++)
                M5.Display.fillRect(rects[i].x, rects[i].y, rects[i].w, rects[i].h, TFT_BLACK);
              M5.Display.endWrite();
              M5.Display.display();

              // Pass 2: redraw
              M5.Display.startWrite();
              for (int i = 0; i < nR; i++)
                M5.Display.fillRect(rects[i].x, rects[i].y, rects[i].w, rects[i].h, TFT_WHITE);

              // Draw new selected (filled black)
              M5.Display.fillRoundRect(bx, by, mBtnW, mBtnH, 6, TFT_BLACK);
              drawSystemTextCentered(mNames[mIdx - 1], bx + mBtnW / 2, by + 12, 28, TFT_WHITE, TFT_BLACK);

              // Draw old deselected (outline)
              if (oldMonth >= 1 && oldMonth <= 12) {
                int or2 = (oldMonth - 1) / 3;
                int oc2 = (oldMonth - 1) % 3;
                int obx = mPadX + oc2 * (mBtnW + mGapX);
                int oby = monthY + or2 * (mBtnH + mGapY);
                M5.Display.drawRoundRect(obx, oby, mBtnW, mBtnH, 6, TFT_BLACK);
                drawSystemTextCentered(mNames[oldMonth - 1], obx + mBtnW / 2, oby + 12, 28);
              }

              M5.Display.endWrite();
              M5.Display.display();
              M5.Display.setEpdMode(epd_mode_t::epd_quality);  // Restore quality mode
            }
          }
        }
      }
      // 確定 button
      else if (x >= 80 && x < 210 && y >= bottomY && y < bottomY + 50) {
        if (ymPickerYear >= 1 && ymPickerYear <= 9999 && ymPickerMonth >= 1 && ymPickerMonth <= 12) {
          pickerYear = ymPickerYear;
          pickerMonth = ymPickerMonth;
          Serial.printf("YM: confirmed %d/%d\n", pickerYear, pickerMonth);
          currentMode = MODE_CALENDAR_PICKER;
          drawCalendarPicker();
        } else {
          Serial.println("YM: invalid year/month, ignoring");
        }
      }
      // 取消 button
      else if (x >= 310 && x < 440 && y >= bottomY && y < bottomY + 50) {
        Serial.println("YM: cancelled");
        currentMode = MODE_CALENDAR_PICKER;
        drawCalendarPicker();
      }
    }
    else if (currentMode == MODE_FONT_MENU) {
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        Serial.println("Font menu back button - returning to previous mode");
        savePrefInt("ereader", "fontIdx", selectedFontIndex);
        fontMenuPage = 0;
        if (fontMenuReturnMode == MODE_READING) {
          currentMode = MODE_READING;
          loadReadingFont();
          recalculatePages();
          if (currentPage >= totalPages) currentPage = totalPages - 1;
          loadCurrentPage();
          drawReading();
        } else {
          currentMode = MODE_DASHBOARD;
          loadSystemFont();
          drawDashboard();
        }
      }
      // Next page button (left = next/forward)
      else if (touchedPrevPage(x, y)) {
        int totalPages = (fontFileCount + FONTS_PER_PAGE - 1) / FONTS_PER_PAGE;
        if (fontMenuPage < totalPages - 1) {
          fontMenuPage++;
          drawFontMenu();
        }
      }
      // Prev page button (right = prev/backward)
      else if (touchedNextPage(x, y)) {
        if (fontMenuPage > 0) {
          fontMenuPage--;
          drawFontMenu();
        }
      }
      // Select font by touch Y position (75px per item, starting at y=100)
      else if (y >= 100 && y <= 800) {
      int fontIdx = fontMenuPage * FONTS_PER_PAGE + (y - 100) / 75;
      if (fontIdx >= 0 && fontIdx < fontFileCount) {
        selectedFontIndex = fontIdx;
        readingFontIndex = fontIdx;
        readingFontFile = fontFileList[fontIdx];
        Serial.printf("Reading font selected: %d = '%s'\n", fontIdx, fontFileList[fontIdx].c_str());
        savePrefInt("ereader", "fontIdx", readingFontIndex);
        
        if (fontMenuReturnMode == MODE_READING) {
          // Return directly to reading with the new font
          currentMode = MODE_READING;
          fontMenuPage = 0;
          loadReadingFont();
          recalculatePages();
          if (currentPage >= totalPages) currentPage = totalPages - 1;
          loadCurrentPage();
          drawReading();
        } else {
          loadSystemFont();
          drawFontMenu();
        }
      }
      }
    } 
    else if (currentMode == MODE_READING) {
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        Serial.println("Back button touched - returning to book list");
        saveReadingPosition();
        savePrefInt("ereader", "page", currentPage);
        currentMode = MODE_BOOK_LIST;
        loadSystemFont();  // Restore system font for UI
        drawBookList();
      }
      // Vertical CJK: left button = NEXT page (forward in book)
      // Only respond if the left arrow is visible (hasNext)
      else if (touchedPrevPage(x, y) && currentPage < totalPages - 1) {
        DEBUG_LOG_THROTTLE(1000, "LEFT arrow - page %d -> %d", currentPage, currentPage + 1);
        currentPage++;
        if (loadCurrentPage()) {
          saveReadingPosition();
          drawReading();
        }
      }
      // Vertical CJK: right button = PREV page (backward in book)
      // Only respond if the right arrow is visible (hasPrev)
      else if (touchedNextPage(x, y) && currentPage > 0) {
        DEBUG_LOG_THROTTLE(1000, "RIGHT arrow - page %d -> %d", currentPage, currentPage - 1);
        currentPage--;
        if (loadCurrentPage()) {
          saveReadingPosition();
          drawReading();
        }
      }
      // Font size decrease (字-)
      else if (y > 900 && x >= 155 && x <= 200) {
        if (readingFontSize > MIN_READING_FONT_SIZE) {
          readingFontSize -= FONT_SIZE_STEP;
          if (readingFontSize < MIN_READING_FONT_SIZE) readingFontSize = MIN_READING_FONT_SIZE;
          if (!(currentBookIsEpub && epubIsImageBased)) {
            size_t byteOffset = (pageByteOffsets && currentPage < pageOffsetsCount)
                                ? pageByteOffsets[currentPage]
                                : (size_t)currentPage * bytesPerPage;
            recalculatePages();
            currentPage = byteOffset / bytesPerPage;
            if (currentPage >= totalPages) currentPage = totalPages - 1;
          }
          savePrefInt("ereader", "rdFontSz", readingFontSize);
          saveReadingPosition();
          loadCurrentPage();
          drawReading();
        }
      }
      // Font size increase (字+)
      else if (y > 900 && x >= 230 && x <= 275) {
        if (readingFontSize < MAX_READING_FONT_SIZE) {
          int savedPage = currentPage;  // Save for image-based EPUBs
          readingFontSize += FONT_SIZE_STEP;
          if (readingFontSize > MAX_READING_FONT_SIZE) readingFontSize = MAX_READING_FONT_SIZE;
          if (!(currentBookIsEpub && epubIsImageBased)) {
            size_t byteOffset = (pageByteOffsets && currentPage < pageOffsetsCount)
                                ? pageByteOffsets[savedPage]
                                : (size_t)savedPage * bytesPerPage;
            recalculatePages();
            currentPage = byteOffset / bytesPerPage;
            if (currentPage >= totalPages) currentPage = totalPages - 1;
          }
          savePrefInt("ereader", "rdFontSz", readingFontSize);
          saveReadingPosition();
          loadCurrentPage();
          drawReading();
        }
      }
      // Bookmark button
      else if (y > 900 && x >= 330 && x <= 375) {
        addBookmark();
        Serial.printf("Bookmark added: page %d\n", currentPage + 1);
        drawReading();
      }
      // Font selection button
      else if (y > 900 && x >= 280 && x <= 325) {
        Serial.println("Opening font menu from reading mode...");
        fontMenuReturnMode = MODE_READING;
        currentMode = MODE_FONT_MENU;
        loadSystemFont();  // Switch to system font for menu UI
        drawFontMenu();
      }
      else if (x < 270) {
        // Left side tap - NEXT page (vertical CJK: reading flows right→left)
        if (currentPage < totalPages - 1) {
          Serial.printf("Left tap - page %d -> %d\n", currentPage, currentPage + 1);
          currentPage++;
          if (loadCurrentPage()) {
            saveReadingPosition();
            drawReading();
          }
        }
      } else {
        // Right side tap - PREV page (go back in vertical CJK reading)
        if (currentPage > 0) {
          Serial.printf("Right tap - page %d -> %d\n", currentPage, currentPage - 1);
          currentPage--;
          if (loadCurrentPage()) {
            saveReadingPosition();
            drawReading();
          }
        }
      }
    }
    else if (currentMode == MODE_SHOPPING_LIST) {
      // Vertical CJK: left button = NEXT page (forward)
      if (touchedPrevPage(x, y)) {
        if (currentShoppingPage < totalShoppingPages - 1) {
          currentShoppingPage++;
          drawShoppingList();
        }
      }
      // Vertical CJK: right button = PREV page (backward)
      else if (touchedNextPage(x, y)) {
        if (currentShoppingPage > 0) {
          currentShoppingPage--;
          drawShoppingList();
        }
      }
      // Return button (lower-right)
      else if (touchedReturnButton(x, y)) {
        Serial.println("Back button touched - returning to dashboard");
        currentMode = MODE_DASHBOARD;
        currentShoppingPage = 0;  // Reset page
        drawDashboard();
      }
      // "清除" (Clear checked) button — partial erase only
      else if (x >= 280 && x <= 400 && y >= 900 && y <= 944) {
        int checkedCount = 0;
        for (int i = 0; i < shoppingCount; i++) {
          if (shoppingList[i].checked) checkedCount++;
        }
        if (checkedCount > 0) {
          Serial.printf("Clearing %d checked shopping items (partial erase)\n", checkedCount);

          M5.Display.setEpdMode(epd_mode_t::epd_fast);

          // First pass: black flash on checked items' areas
          M5.Display.startWrite();
          for (int c = 0; c < shoppingCheckboxCount; c++) {
            int idx = shoppingCheckboxes[c].itemIdx;
            if (idx < shoppingCount && shoppingList[idx].checked) {
              int rx = shoppingCheckboxes[c].touchMinX;
              int ry = shoppingCheckboxes[c].touchMinY;
              int rw = shoppingCheckboxes[c].touchMaxX - rx;
              int rh = shoppingCheckboxes[c].touchMaxY - ry;
              M5.Display.fillRect(rx, ry, rw, rh, TFT_BLACK);
            }
          }
          M5.Display.endWrite();
          M5.Display.display();

          // Second pass: white-out checked items' areas
          M5.Display.startWrite();
          for (int c = 0; c < shoppingCheckboxCount; c++) {
            int idx = shoppingCheckboxes[c].itemIdx;
            if (idx < shoppingCount && shoppingList[idx].checked) {
              int rx = shoppingCheckboxes[c].touchMinX;
              int ry = shoppingCheckboxes[c].touchMinY;
              int rw = shoppingCheckboxes[c].touchMaxX - rx;
              int rh = shoppingCheckboxes[c].touchMaxY - ry;
              M5.Display.fillRect(rx, ry, rw, rh, TFT_WHITE);
            }
          }

          // Rebuild checkbox array: remove cleared items, remap indices
          int newCbCount = 0;
          for (int c = 0; c < shoppingCheckboxCount; c++) {
            int idx = shoppingCheckboxes[c].itemIdx;
            if (idx < shoppingCount && !shoppingList[idx].checked) {
              int offset = 0;
              for (int k = 0; k < idx; k++) {
                if (shoppingList[k].checked) offset++;
              }
              shoppingCheckboxes[newCbCount] = shoppingCheckboxes[c];
              shoppingCheckboxes[newCbCount].itemIdx = idx - offset;
              newCbCount++;
            }
          }
          shoppingCheckboxCount = newCbCount;

          // Clear data (compact array + save)
          clearCheckedShopping();

          // Hide 清除 button
          int bx = 280, by = 900, bw = 120, bh = 44;
          M5.Display.endWrite();
          M5.Display.startWrite();
          M5.Display.fillRect(bx, by, bw, bh, TFT_BLACK);
          M5.Display.endWrite();
          M5.Display.display();
          M5.Display.startWrite();
          M5.Display.fillRect(bx, by, bw, bh, TFT_WHITE);
          M5.Display.endWrite();
          M5.Display.display();

          M5.Display.setEpdMode(epd_mode_t::epd_quality);
        }
      }
      else {
        // Use saved checkbox positions from drawShoppingList() for accurate touch detection
        for (int c = 0; c < shoppingCheckboxCount; c++) {
          if (x >= shoppingCheckboxes[c].touchMinX && x <= shoppingCheckboxes[c].touchMaxX &&
              y >= shoppingCheckboxes[c].touchMinY && y <= shoppingCheckboxes[c].touchMaxY) {
            int idx = shoppingCheckboxes[c].itemIdx;
            int cbX = shoppingCheckboxes[c].x;
            int cbY = shoppingCheckboxes[c].y;
            int cbSize = shoppingCheckboxes[c].size;
            
            // Toggle checkbox
            shoppingList[idx].checked = !shoppingList[idx].checked;
            DEBUG_LOG_THROTTLE(300, "Shopping %d toggled: %s (checkbox at %d,%d)", idx,
              shoppingList[idx].checked ? "checked" : "unchecked", cbX, cbY);
            
            // Save checked items to SD card
            saveCheckedItems();
            
            // Two-pass partial update using exact saved position
            int pad = 6;
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            M5.Display.startWrite();
            M5.Display.fillRect(cbX - pad, cbY - pad, cbSize + pad*2, cbSize + pad*2, TFT_BLACK);
            M5.Display.endWrite();
            M5.Display.display();
            M5.Display.startWrite();
            M5.Display.fillRect(cbX - pad, cbY - pad, cbSize + pad*2, cbSize + pad*2, TFT_WHITE);
            M5.Display.drawRect(cbX, cbY, cbSize, cbSize, TFT_BLACK);
            if (shoppingList[idx].checked) {
              M5.Display.fillRect(cbX + 3, cbY + 3, cbSize - 6, cbSize - 6, TFT_BLACK);
            }
            M5.Display.endWrite();
            M5.Display.display();
            
            // Update 清除 button: show/hide based on checked count
            {
              int chkCnt = 0;
              for (int ci = 0; ci < shoppingCount; ci++) {
                if (shoppingList[ci].checked) chkCnt++;
              }
              int bx = 280, by = 900, bw = 120, bh = 44;
              M5.Display.startWrite();
              M5.Display.fillRect(bx, by, bw, bh, TFT_BLACK);
              M5.Display.endWrite();
              M5.Display.display();
              M5.Display.startWrite();
              M5.Display.fillRect(bx, by, bw, bh, TFT_WHITE);
              if (chkCnt > 0) {
                M5.Display.fillRect(bx, by, bw, bh, TFT_BLACK);
                drawSystemText("清除", bx + 14, by + 8, 24, TFT_WHITE, TFT_BLACK);
                M5.Display.setFont(&fonts::Font2);
                M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
                M5.Display.setCursor(bx + 70, by + 14);
                M5.Display.printf("x%d", chkCnt);
                M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
              }
              M5.Display.endWrite();
              M5.Display.display();
            }
            
            M5.Display.setEpdMode(epd_mode_t::epd_quality);
            return;
          }
        }
      }
    } 
    else if (currentMode == MODE_WEATHER) {
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
      // Refresh button (lower-left, uses prev page position)
      else if (touchedPrevPage(x, y)) {
        weatherData.valid = false;  // Force refresh
        drawWeather(true);
      }
      // °C/°F toggle button (next page icon area: NAV_NEXT_X)
      else if (touchedNextPage(x, y)) {
        bool toImperial = (weatherConfig.units != "imperial");
        weatherConfig.units = toImperial ? "imperial" : "metric";
        Serial.printf("Weather units toggled to: %s\n", weatherConfig.units.c_str());
        // Always convert from original API values (avoids cumulative float drift)
        if (weatherData.valid) {
          bool fetchedMetric = (weatherData.fetchedUnits != "imperial");
          bool needConvert = (toImperial != !fetchedMetric);  // display unit differs from fetch unit
          // Restore original API values first
          weatherData.tempCurrent = weatherData.origTempCurrent;
          weatherData.tempMin     = weatherData.origTempMin;
          weatherData.tempMax     = weatherData.origTempMax;
          weatherData.feelsLike   = weatherData.origFeelsLike;
          weatherData.windSpeed   = weatherData.origWindSpeed;
          for (int i = 0; i < weatherData.forecastCount; i++) {
            weatherData.forecast[i].tempMin = weatherData.origForecast[i].tempMin;
            weatherData.forecast[i].tempMax = weatherData.origForecast[i].tempMax;
          }
          if (needConvert) {
            if (toImperial) {
              // metric -> imperial
              weatherData.tempCurrent = weatherData.tempCurrent * 9.0f / 5.0f + 32.0f;
              weatherData.tempMin     = weatherData.tempMin * 9.0f / 5.0f + 32.0f;
              weatherData.tempMax     = weatherData.tempMax * 9.0f / 5.0f + 32.0f;
              weatherData.feelsLike   = weatherData.feelsLike * 9.0f / 5.0f + 32.0f;
              weatherData.windSpeed   = weatherData.windSpeed * 2.237f;
              for (int i = 0; i < weatherData.forecastCount; i++) {
                weatherData.forecast[i].tempMin = weatherData.forecast[i].tempMin * 9.0f / 5.0f + 32.0f;
                weatherData.forecast[i].tempMax = weatherData.forecast[i].tempMax * 9.0f / 5.0f + 32.0f;
              }
            } else {
              // imperial -> metric
              weatherData.tempCurrent = (weatherData.tempCurrent - 32.0f) * 5.0f / 9.0f;
              weatherData.tempMin     = (weatherData.tempMin - 32.0f) * 5.0f / 9.0f;
              weatherData.tempMax     = (weatherData.tempMax - 32.0f) * 5.0f / 9.0f;
              weatherData.feelsLike   = (weatherData.feelsLike - 32.0f) * 5.0f / 9.0f;
              weatherData.windSpeed   = weatherData.windSpeed / 2.237f;
              for (int i = 0; i < weatherData.forecastCount; i++) {
                weatherData.forecast[i].tempMin = (weatherData.forecast[i].tempMin - 32.0f) * 5.0f / 9.0f;
                weatherData.forecast[i].tempMax = (weatherData.forecast[i].tempMax - 32.0f) * 5.0f / 9.0f;
              }
            }
          }
        }
        // Save preference so next fetch uses the new unit
        saveWiFiConfig();
        redrawWeatherUnits();  // partial update — only temp/wind values
      }
    }
    else if (currentMode == MODE_TODO_LIST) {
      DEBUG_LOG_THROTTLE(500, "Todo touch: x=%d y=%d", x, y);
      // Vertical CJK: left button = NEXT page (forward)
      if (touchedPrevPage(x, y)) {
        if (currentTodoPage < totalTodoPages - 1) {
          currentTodoPage++;
          drawTodoList();
        }
      }
      // Vertical CJK: right button = PREV page (backward)
      else if (touchedNextPage(x, y)) {
        if (currentTodoPage > 0) {
          currentTodoPage--;
          drawTodoList();
        }
      }
      // Return button (lower-right)
      else if (touchedReturnButton(x, y)) {
        Serial.println("Back button touched - returning to dashboard");
        currentMode = MODE_DASHBOARD;
        currentTodoPage = 0;  // Reset page
        drawDashboard();
      }
      // "+" (Add new todo) button
      else if (x >= 200 && x <= 280 && y >= 890 && y <= 950) {
        Serial.println("Add new todo - opening Cangjie input");
        if (loadCangjieTable()) {
          cjReturnMode = MODE_TODO_LIST;
          cjReturnPage = currentTodoPage;
          currentMode = MODE_CANGJIE_INPUT;
          drawCangjieInput();
        } else {
          Serial.println("Failed to load Cangjie table");
          // Show error message on screen
          M5.Display.setEpdMode(epd_mode_t::epd_fast);
          M5.Display.startWrite();
          M5.Display.fillRect(100, 400, 340, 80, TFT_BLACK);
          M5.Display.endWrite();
          M5.Display.display();
          M5.Display.startWrite();
          M5.Display.fillRect(100, 400, 340, 80, TFT_WHITE);
          M5.Display.drawRect(100, 400, 340, 80, TFT_BLACK);
          drawSystemText("找不到", 120, 412, 28, TFT_BLACK, TFT_WHITE);
          M5.Display.setFont(&fonts::Font2);
          M5.Display.setTextSize(1);
          M5.Display.setCursor(120, 450);
          M5.Display.print("cangjie5.bin on SD");
          M5.Display.endWrite();
          M5.Display.display();
          M5.Display.setEpdMode(epd_mode_t::epd_quality);
          delay(2000);
          drawTodoList();
        }
      }
      // "清除" (Clear completed) button — partial erase only
      else if (x >= 280 && x <= 400 && y >= 900 && y <= 944) {
        int checkedCount = 0;
        for (int i = 0; i < todoCount; i++) {
          if (todoList[i].checked) checkedCount++;
        }
        if (checkedCount > 0) {
          Serial.printf("Clearing %d checked todo items (partial erase)\n", checkedCount);

          M5.Display.setEpdMode(epd_mode_t::epd_fast);

          // First pass: black flash on checked items' areas
          M5.Display.startWrite();
          for (int c = 0; c < todoCheckboxCount; c++) {
            int idx = todoCheckboxes[c].itemIdx;
            if (idx < todoCount && todoList[idx].checked) {
              // Erase from checkbox top to touch zone bottom
              int rx = todoCheckboxes[c].touchMinX;
              int ry = todoCheckboxes[c].y - 5;
              int rw = todoCheckboxes[c].touchMaxX - rx;
              int rh = todoCheckboxes[c].touchMaxY - ry + 5;
              M5.Display.fillRect(rx, ry, rw, rh, TFT_BLACK);
            }
          }
          M5.Display.endWrite();
          M5.Display.display();

          // Second pass: white-out checked items' areas
          M5.Display.startWrite();
          for (int c = 0; c < todoCheckboxCount; c++) {
            int idx = todoCheckboxes[c].itemIdx;
            if (idx < todoCount && todoList[idx].checked) {
              int rx = todoCheckboxes[c].touchMinX;
              int ry = todoCheckboxes[c].y - 5;
              int rw = todoCheckboxes[c].touchMaxX - rx;
              int rh = todoCheckboxes[c].touchMaxY - ry + 5;
              M5.Display.fillRect(rx, ry, rw, rh, TFT_WHITE);
            }
          }

          // Rebuild checkbox array: remove cleared items, remap indices
          int newCbCount = 0;
          for (int c = 0; c < todoCheckboxCount; c++) {
            int idx = todoCheckboxes[c].itemIdx;
            if (idx < todoCount && !todoList[idx].checked) {
              int offset = 0;
              for (int k = 0; k < idx; k++) {
                if (todoList[k].checked) offset++;
              }
              todoCheckboxes[newCbCount] = todoCheckboxes[c];
              todoCheckboxes[newCbCount].itemIdx = idx - offset;
              newCbCount++;
            }
          }
          todoCheckboxCount = newCbCount;

          // Rebuild date zone array: remove cleared items, remap indices
          int newDzCount = 0;
          for (int d = 0; d < todoDateZoneCount; d++) {
            int idx = todoDateZones[d].itemIdx;
            if (idx < todoCount && !todoList[idx].checked) {
              int offset = 0;
              for (int k = 0; k < idx; k++) {
                if (todoList[k].checked) offset++;
              }
              todoDateZones[newDzCount] = todoDateZones[d];
              todoDateZones[newDzCount].itemIdx = idx - offset;
              newDzCount++;
            }
          }
          todoDateZoneCount = newDzCount;

          // Clear data (compact array + save)
          clearCheckedTodos();

          // Hide 清除 button
          int bx = 280, by = 900, bw = 120, bh = 44;
          M5.Display.endWrite();
          M5.Display.startWrite();
          M5.Display.fillRect(bx, by, bw, bh, TFT_BLACK);
          M5.Display.endWrite();
          M5.Display.display();
          M5.Display.startWrite();
          M5.Display.fillRect(bx, by, bw, bh, TFT_WHITE);
          M5.Display.endWrite();
          M5.Display.display();

          M5.Display.setEpdMode(epd_mode_t::epd_quality);
        }
      }
      else {
        // Check date touch zones FIRST (date area takes priority over checkbox)
        bool dateZoneTapped = false;
        for (int d = 0; d < todoDateZoneCount; d++) {
          if (x >= todoDateZones[d].touchMinX && x <= todoDateZones[d].touchMaxX &&
              y >= todoDateZones[d].touchMinY && y <= todoDateZones[d].touchMaxY) {
            int idx = todoDateZones[d].itemIdx;
            todoDatePickerItem = idx;
            // Parse existing date to initialize picker, or use today
            struct tm ti;
            getLocalTime(&ti);
            todoDatePickerYear = ti.tm_year + 1900;
            todoDatePickerMonth = ti.tm_mon + 1;
            if (todoList[idx].date.length() > 0) {
              int s1 = todoList[idx].date.indexOf('/');
              int s2 = todoList[idx].date.indexOf('/', s1 + 1);
              if (s1 > 0 && s2 > 0) {
                todoDatePickerMonth = todoList[idx].date.substring(0, s1).toInt();
                int iy = todoList[idx].date.substring(s2 + 1).toInt();
                if (iy < 100) iy += 2000;
                todoDatePickerYear = iy;
              }
            }
            Serial.printf("Todo %d: opening date picker (current: %s)\n", idx, todoList[idx].date.c_str());
            todoDatePickerReturnPage = currentTodoPage;  // Save current page
            currentMode = MODE_TODO_DATE_PICKER;
            drawTodoDatePicker();
            dateZoneTapped = true;
            break;
          }
        }
        if (!dateZoneTapped) {
          // Use saved checkbox positions from drawTodoList() for accurate touch detection
          for (int c = 0; c < todoCheckboxCount; c++) {
            if (x >= todoCheckboxes[c].touchMinX && x <= todoCheckboxes[c].touchMaxX &&
                y >= todoCheckboxes[c].touchMinY && y <= todoCheckboxes[c].touchMaxY) {
              int idx = todoCheckboxes[c].itemIdx;
              int cbX = todoCheckboxes[c].x;
              int cbY = todoCheckboxes[c].y;
              int cbSize = todoCheckboxes[c].size;
              
              // Toggle checkbox
              todoList[idx].checked = !todoList[idx].checked;
              DEBUG_LOG_THROTTLE(300, "Todo %d toggled: %s (checkbox at %d,%d)", idx,
                todoList[idx].checked ? "checked" : "unchecked", cbX, cbY);
              
              // Save checked items
              saveCheckedTodos();
              
              // Two-pass partial update using exact saved position
              int pad = 6;
              M5.Display.setEpdMode(epd_mode_t::epd_fast);
              M5.Display.startWrite();
              M5.Display.fillRect(cbX - pad, cbY - pad, cbSize + pad*2, cbSize + pad*2, TFT_BLACK);
              M5.Display.endWrite();
              M5.Display.display();
              M5.Display.startWrite();
              M5.Display.fillRect(cbX - pad, cbY - pad, cbSize + pad*2, cbSize + pad*2, TFT_WHITE);
              M5.Display.drawRect(cbX, cbY, cbSize, cbSize, TFT_BLACK);
              if (todoList[idx].checked) {
                M5.Display.fillRect(cbX + 3, cbY + 3, cbSize - 6, cbSize - 6, TFT_BLACK);
              }
              M5.Display.endWrite();
              M5.Display.display();
              
              // Update 清除 button: show/hide based on checked count
              {
                int chkCnt = 0;
                for (int ci = 0; ci < todoCount; ci++) {
                  if (todoList[ci].checked) chkCnt++;
                }
                int bx = 280, by = 900, bw = 120, bh = 44;
                M5.Display.startWrite();
                M5.Display.fillRect(bx, by, bw, bh, TFT_BLACK);
                M5.Display.endWrite();
                M5.Display.display();
                M5.Display.startWrite();
                M5.Display.fillRect(bx, by, bw, bh, TFT_WHITE);
                if (chkCnt > 0) {
                  M5.Display.fillRect(bx, by, bw, bh, TFT_BLACK);
                  drawSystemText("清除", bx + 14, by + 8, 24, TFT_WHITE, TFT_BLACK);
                  M5.Display.setFont(&fonts::Font2);
                  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
                  M5.Display.setCursor(bx + 70, by + 14);
                  M5.Display.printf("x%d", chkCnt);
                  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
                }
                M5.Display.endWrite();
                M5.Display.display();
              }
              
              M5.Display.setEpdMode(epd_mode_t::epd_quality);
              return;
            }
          }
        }
      }
    }
    else if (currentMode == MODE_TODO_DATE_PICKER) {
      int W = M5.Display.width();
      if (touchedReturnButton(x, y)) {
        Serial.println("Todo date picker: back to todo list");
        currentMode = MODE_TODO_LIST;
        currentTodoPage = todoDatePickerReturnPage;  // Return to the page we came from
        drawTodoList();
      }
      // Left icon (next month)
      else if (touchedPrevPage(x, y)) {
        todoDatePickerMonth++;
        if (todoDatePickerMonth > 12) { todoDatePickerMonth = 1; todoDatePickerYear++; }
        drawTodoDatePicker();
      }
      // Right icon (previous month)
      else if (touchedNextPage(x, y)) {
        todoDatePickerMonth--;
        if (todoDatePickerMonth < 1) { todoDatePickerMonth = 12; todoDatePickerYear--; }
        drawTodoDatePicker();
      }
      // "清除日期" (Clear date) button
      else if (x >= 160 && x <= 300 && y >= 900 && y <= 944) {
        if (todoDatePickerItem >= 0 && todoDatePickerItem < todoCount) {
          todoList[todoDatePickerItem].date = "";
          Serial.printf("Todo %d: date cleared\n", todoDatePickerItem);
          saveTodoList();
          sortTodoListByDate();
          currentMode = MODE_TODO_LIST;
          currentTodoPage = 0;  // Recalculate after date clear (sort changed)
          calculateTodoPages();
          currentTodoPage = min(todoDatePickerReturnPage, totalTodoPages - 1);
          drawTodoList();
        }
      }
      // "今天" (Today) button - jump to today's month
      else if (x >= 320 && x <= 420 && y >= 900 && y <= 944) {
        Serial.println("Todo date picker: jump to today's month");
        struct tm ti;
        getLocalTime(&ti);
        todoDatePickerYear = ti.tm_year + 1900;
        todoDatePickerMonth = ti.tm_mon + 1;
        drawTodoDatePicker();
      }
      // Tap on a day cell
      else if (y >= 130 && y < 900) {
        int cellW = W / 7;
        int cellH = 110;
        int col = x / cellW;
        int row = (y - 130) / cellH;
        int firstDow = dayOfWeek(todoDatePickerYear, todoDatePickerMonth, 1);
        int dayIdx = row * 7 + col - firstDow + 1;
        int totalDays = solarMonthDays(todoDatePickerYear, todoDatePickerMonth);
        if (dayIdx >= 1 && dayIdx <= totalDays && todoDatePickerItem >= 0 && todoDatePickerItem < todoCount) {
          // Format as MM/DD/YY
          char dateBuf[16];
          snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%02d",
            todoDatePickerMonth, dayIdx, todoDatePickerYear % 100);
          todoList[todoDatePickerItem].date = dateBuf;
          Serial.printf("Todo %d: date set to %s\n", todoDatePickerItem, dateBuf);
          saveTodoList();
          sortTodoListByDate();
          currentMode = MODE_TODO_LIST;
          currentTodoPage = 0;  // Recalculate after date change (sort changed)
          calculateTodoPages();
          currentTodoPage = min(todoDatePickerReturnPage, totalTodoPages - 1);
          drawTodoList();
        }
      }
    }
    else if (currentMode == MODE_CANGJIE_INPUT) {
      // === Cangjie Input Mode Touch Handling ===
      int keyW = 48, keyH = 68, keySpacing = 6;
      int startY = 290;
      int specialY = startY + 3 * (keyH + keySpacing);

      // Candidate bar touch (Y=190..250)
      if (y >= 190 && y <= 250 && cjCandidateCount > 0) {
        int cellW = 520 / CJ_CANDIDATES_PER_PAGE;
        int col = (x - 10) / cellW;
        if (col >= 0 && col < CJ_CANDIDATES_PER_PAGE) {
          cangjieSelectCandidate(col);
          updateCangjieInputArea();
          return;
        }
      }

      // Candidate page nav arrows (Y=255..280)
      if (y >= 255 && y <= 280) {
        int totalPages = (cjCandidateCount + CJ_CANDIDATES_PER_PAGE - 1) / CJ_CANDIDATES_PER_PAGE;
        if (x < 50 && cjCandidatePage > 0) {
          cjCandidatePage--;
          updateCangjieInputArea();
          return;
        }
        if (x > 490 && cjCandidatePage < totalPages - 1) {
          cjCandidatePage++;
          updateCangjieInputArea();
          return;
        }
      }

      // QWERTY key rows (Y=290... 3 rows of 68+6=74px each)
      if (y >= startY && y < specialY) {
        const char* rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
        for (int row = 0; row < 3; row++) {
          int numKeys = strlen(rows[row]);
          int rowWidth = numKeys * keyW + (numKeys - 1) * keySpacing;
          int rowStartX = (DISPLAY_WIDTH - rowWidth) / 2;
          int rowY = startY + row * (keyH + keySpacing);

          if (y >= rowY && y <= rowY + keyH) {
            for (int i = 0; i < numKeys; i++) {
              int kx = rowStartX + i * (keyW + keySpacing);
              if (x >= kx && x <= kx + keyW) {
                cangjieKeyPress(rows[row][i]);
                updateCangjieInputArea();
                return;
              }
            }
          }
        }
      }

      // Special keys row
      if (y >= specialY && y <= specialY + 56) {
        // Backspace (20..140)
        if (x >= 20 && x <= 140) {
          cangjieBackspace();
          updateCangjieInputArea();
          return;
        }
        // Space (150..270)
        if (x >= 150 && x <= 270) {
          if (cjInputLen == 0) {
            // Insert space in composed text
            cjComposedText += " ";
          }
          updateCangjieInputArea();
          return;
        }
        // Confirm 確定 (280..400)
        if (x >= 280 && x <= 400) {
          cangjieConfirmInput();
          return;
        }
        // Cancel 取消 (410..530)
        if (x >= 410 && x <= 530) {
          cangjieCancel();
          return;
        }
      }
    }
    else if (currentMode == MODE_SETUP) {
      // Handle setup screen touches based on which submenu we're in
      int btnY = 820;
      
      if (setupSubmenu == 0) {
        // Main setup menu
        int y1 = 85;
        int itemHeight = 100;

        // Menu paging
        // Left arrow (touchedPrevPage) = go forward/next page
        if (touchedPrevPage(x, y) && setupMenuPage < 1) {
          setupMenuPage++;
          drawSetupMenu();
          return;
        }
        // Right arrow (touchedNextPage) = go back/previous page
        if (touchedNextPage(x, y) && setupMenuPage > 0) {
          setupMenuPage--;
          drawSetupMenu();
          return;
        }

        if (setupMenuPage == 0) {
          // Page 1: 5 items
          
          // WiFi Settings item
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("WiFi settings selected");
            setupSubmenu = 1;
            showingKeyboard = false;
            showingTimezone = false;
            scanWiFiNetworks();
            drawWiFiSetup();
            return;
          }
          
          // Timezone Settings item
          y1 += itemHeight + 18;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Timezone settings selected");
            setupSubmenu = 2;
            showingTimezone = true;
            drawWiFiSetup();
            return;
          }
          
          // Web Server Settings item
          y1 += itemHeight + 18;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Web server settings selected");
            setupSubmenu = 3;
            drawWebServerSetup();
            return;
          }
          
          // USB MSC Settings item
          y1 += itemHeight + 18;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("USB MSC settings selected");
            setupSubmenu = 4;
            drawUSBMSCSetup();
            return;
          }
          
          // Icon Source Settings item
          y1 += itemHeight + 18;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Icon source settings selected");
            setupSubmenu = 5;
            drawIconSetup();
            return;
          }
        } else {
          // Page 2: 3 items
          
          // Calendar Calculation Settings item
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Calendar settings selected");
            setupSubmenu = 6;
            drawCalendarSetup();
            return;
          }
          
          // Bluetooth Settings item
          y1 += itemHeight + 18;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Bluetooth settings selected");
            setupSubmenu = 7;
            drawBluetoothSetup();
            return;
          }
          
          // Auto-Sleep Settings item
          y1 += itemHeight + 18;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Auto-sleep settings selected - toggling");
            autoSleepEnabled = !autoSleepEnabled;
            prefs.begin("ereader", false);
            prefs.putBool("autoSleep", autoSleepEnabled);
            prefs.end();
            Serial.printf("Auto-sleep toggled to: %d\n", autoSleepEnabled);
            drawSetupMenu();
            return;
          }
        }
        
        // Return button (lower-right)
        if (touchedReturnButton(x, y)) {
          Serial.println("Back button touched - returning to dashboard");
          setupSubmenu = 0;
          currentMode = MODE_DASHBOARD;
          drawDashboard();
        }
      }
      else if (setupSubmenu == 1) {
        // WiFi setup submenu
        if (showingKeyboard) {
          // Virtual keyboard touch detection
          int keyW = 48;
          int keyH = 60;
          int keySpacing = 6;
          int startY = 450;
          
          // Check special keys first
          int specialY = startY + 3 * (keyH + keySpacing);
          
          // Shift/123 button (left side)
          if (x >= 20 && x <= 100 && y >= specialY && y <= specialY + keyH) {
            keyboardSymbols = !keyboardSymbols;
            if (keyboardSymbols) {
              keyboardShift = false;  // Disable shift in symbol mode
            }
            Serial.printf("Keyboard mode: %s\n", keyboardSymbols ? "Symbols" : "Letters");
            drawWiFiSetup();
            return;
          }
          
          // Space button
          if (x >= 110 && x <= 310 && y >= specialY && y <= specialY + keyH) {
            passwordInput += " ";
            Serial.println("Added space");
            updatePasswordDisplay();  // Only update password field
            return;
          }
          
          // Backspace button
          if (x >= 320 && x <= 420 && y >= specialY && y <= specialY + keyH) {
            if (passwordInput.length() > 0) {
              passwordInput.remove(passwordInput.length() - 1);
              Serial.println("Backspace");
              updatePasswordDisplay();  // Only update password field
            }
            return;
          }
          
          // Shift toggle (right side, only when not in symbols mode)
          if (!keyboardSymbols && x >= 430 && x <= 520 && y >= specialY && y <= specialY + keyH) {
            keyboardShift = !keyboardShift;
            Serial.printf("Shift: %s\n", keyboardShift ? "ON" : "OFF");
            drawWiFiSetup();
            return;
          }
          
          // Check letter/number keys (3 rows)
          const char* rows[3];
          getKeyboardRows(keyboardSymbols, keyboardShift, rows);
          
          for (int row = 0; row < 3; row++) {
            int numKeys = strlen(rows[row]);
            int rowWidth = numKeys * keyW + (numKeys - 1) * keySpacing;
            int startX = (DISPLAY_WIDTH - rowWidth) / 2;
            int rowY = startY + row * (keyH + keySpacing);
            
            if (y >= rowY && y <= rowY + keyH) {
              for (int i = 0; i < numKeys; i++) {
                int keyX = startX + i * (keyW + keySpacing);
                if (x >= keyX && x <= keyX + keyW) {
                  char keyChar = rows[row][i];
                  passwordInput += keyChar;
                  Serial.printf("Key pressed: %c\n", keyChar);
                  updatePasswordDisplay();  // Only update password field
                  return;
                }
              }
            }
          }
          
          // Connect button
          if (x >= 20 && x <= 220 && y >= btnY && y <= btnY + 80) {
            Serial.println("Connect button touched");
            wifiConfig.ssid = scannedNetworks[selectedNetworkIndex].ssid;
            wifiConfig.password = passwordInput;
            
            // Save config
            saveWiFiConfig();
            
            // Try to connect
            bool connected = connectToWiFi();
            
            // Show result
            M5.Display.startWrite();
            M5.Display.fillScreen(TFT_WHITE);
            M5.Display.setTextColor(TFT_BLACK);
            M5.Display.setFont(&fonts::Font2);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(20, 400);
            if (connected) {
              M5.Display.println("WiFi Connected!");
              M5.Display.setCursor(20, 450);
              M5.Display.setTextSize(1);
              M5.Display.println("IP: " + WiFi.localIP().toString());
            } else {
              M5.Display.println("Connection Failed!");
            }
            M5.Display.endWrite();
            M5.Display.display();
            
            delay(3000);
            
            // Return to setup menu
            showingKeyboard = false;
            passwordInput = "";
            keyboardShift = false;
            keyboardSymbols = false;
            setupSubmenu = 0;
            drawSetupMenu();
          }
          // Universal return button (lower-right)
          else if (touchedReturnButton(x, y)) {
            Serial.println("Cancel button touched");
            showingKeyboard = false;
            passwordInput = "";
            keyboardShift = false;
            keyboardSymbols = false;
            selectedNetworkIndex = -1;
            drawWiFiSetup();
          }
        } else {
          // Main WiFi setup screen (network list)
          int btnY = 750;
          
          // Scan button
          if (x >= 20 && x <= 170 && y >= btnY && y <= btnY + 70) {
            Serial.println("Scan button touched");
            scanWiFiNetworks();
            drawWiFiSetup();
          }
          // Timezone button
          else if (x >= 190 && x <= 340 && y >= btnY && y <= btnY + 70) {
            Serial.println("Timezone button touched (from WiFi screen)");
            setupSubmenu = 2;
            showingTimezone = true;
            drawWiFiSetup();
          }
          // Universal return button (lower-right)
          else if (touchedReturnButton(x, y)) {
            Serial.println("Back button touched");
            setupSubmenu = 0;
            drawSetupMenu();
          }
          // Network selection
          else if (!wifiScanning && networkCount > 0) {
            int networkY = 120;
            for (int i = 0; i < networkCount && i < 7; i++) {
              if (y >= networkY && y <= networkY + 70 && x >= 20 && x <= 520) {
                Serial.printf("Network %d selected: %s\n", i, scannedNetworks[i].ssid.c_str());
                selectedNetworkIndex = i;
                
                // If network is encrypted, show keyboard for password
                if (scannedNetworks[i].encrypted) {
                  showingKeyboard = true;
                  passwordInput = "";
                  keyboardShift = false;
                  keyboardSymbols = false;
                  drawWiFiSetup();
                } else {
                  // Open network, connect directly
                  wifiConfig.ssid = scannedNetworks[i].ssid;
                  wifiConfig.password = "";
                  saveWiFiConfig();
                  
                  bool connected = connectToWiFi();
                  M5.Display.startWrite();
                  M5.Display.fillScreen(TFT_WHITE);
                  M5.Display.setTextColor(TFT_BLACK);
                  M5.Display.setFont(&fonts::Font2);
                  M5.Display.setTextSize(2);
                  M5.Display.setCursor(20, 400);
                  if (connected) {
                    M5.Display.println("WiFi Connected!");
                  } else {
                    M5.Display.println("Connection Failed!");
                  }
                  M5.Display.endWrite();
                  M5.Display.display();
                  delay(3000);
                  
                  setupSubmenu = 0;
                  drawSetupMenu();
                }
                break;
              }
              networkY += 80;
            }
          }
        }
      }
      else if (setupSubmenu == 2) {
        // Timezone setup submenu
        if (showingTimezone) {
          // Save button
          if (x >= 20 && x <= 220 && y >= btnY && y <= btnY + 80) {
            Serial.println("Save timezone button touched");
            timeConfig.timezone = String(timezones[selectedTimezone].tzString);
            timeConfig.gmtOffset = timezones[selectedTimezone].gmtOffset;
            saveWiFiConfig();  // Save timezone to config file
            
            // If WiFi connected, resync time with new timezone
            if (WiFi.status() == WL_CONNECTED) {
              syncTimeNTP();
            }
            
            showingTimezone = false;
            setupSubmenu = 0;
            drawSetupMenu();
            return;
          }
          // Universal return button (lower-right)
          else if (touchedReturnButton(x, y)) {
            Serial.println("Cancel timezone button touched");
            showingTimezone = false;
            setupSubmenu = 0;
            drawSetupMenu();
            return;
          }
          // Timezone list selection
          else {
            int tzY = 160;
            for (int i = 0; i < timezoneCount; i++) {
              if (y >= tzY && y <= tzY + 60 && x >= 15 && x <= 525) {
                selectedTimezone = i;
                Serial.printf("Timezone %d selected: %s\n", i, timezones[i].name);
                drawWiFiSetup();
                return;
              }
              tzY += 70;
              if (tzY > 750) break;
            }
          }
        }
      }
      else if (setupSubmenu == 3) {
        // Web server setup submenu
        int btnY = 400;
        
        // Toggle button
        if (x >= 20 && x <= 260 && y >= btnY && y <= btnY + 100) {
          Serial.println("Web server toggle button touched");
          webServerEnabled = !webServerEnabled;
          
          // Save preference
          savePrefBool("m5paper", "webServer", webServerEnabled);
          
          if (webServerEnabled) {
            Serial.println("Web server enabled");
            if (WiFi.status() == WL_CONNECTED) {
              startWebServer();
            }
          } else {
            Serial.println("Web server disabled");
            stopWebServer();
          }
          
          drawWebServerSetup();
          return;
        }
        
        // Return button (lower-right)
        if (touchedReturnButton(x, y)) {
          Serial.println("Web server setup back button touched");
          setupSubmenu = 0;
          drawSetupMenu();
          return;
        }
      }
      else if (setupSubmenu == 4) {
        // USB MSC setup submenu
        int btnY = 400;  // Match drawUSBMSCSetup
        
        // Toggle button (full width)
        if (x >= 20 && x <= 520 && y >= btnY && y <= btnY + 90) {
          Serial.println("USB MSC toggle button touched");
          
          if (usbMSCActive) {
            Serial.println("Stopping USB MSC...");
            
            // Show restarting message using pre-rendered label bitmaps (SD unavailable during MSC)
            M5.Display.startWrite();
            M5.Display.fillRect(0, 300, DISPLAY_WIDTH, 200, TFT_WHITE);
            drawSystemTextCentered("重新啟動中...", DISPLAY_WIDTH / 2, 340, 32);
            M5.Display.setFont(&fonts::Font2);
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(TFT_BLACK);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.drawString("Restarting device...", DISPLAY_WIDTH / 2, 390);
            M5.Display.setTextDatum(TL_DATUM);
            M5.Display.endWrite();
            M5.Display.display();
            
            stopUSBMSC();
            usbMSCEnabled = false;
          } else {
            Serial.println("Starting USB MSC...");
            
            // Show starting message
            M5.Display.startWrite();
            M5.Display.fillRect(0, 300, DISPLAY_WIDTH, 200, TFT_WHITE);
            drawSystemText("啟動 USB 中...", 100, 350, 32);
            drawSystemText("請查看序列輸出以了解詳情", 80, 400, 22);
            M5.Display.endWrite();
            M5.Display.display();
            
            usbMSCEnabled = true;
            startUSBMSC();
            
            // Show result
            delay(1000);
            M5.Display.startWrite();
            M5.Display.fillRect(0, 450, DISPLAY_WIDTH, 100, TFT_WHITE);
            if (usbMSCActive) {
              drawSystemText("✓ 成功啟動", 80, 450, 28, EPD_DARK_GRAY);
            } else {
              drawSystemText("✗ 啟動失敗 - 請查看序列輸出", 80, 450, 24, EPD_DARK_GRAY);
            }
            M5.Display.endWrite();
            M5.Display.display();
            delay(2000);
          }
          
          // Save preference
          savePrefBool("m5paper", "usbMSC", usbMSCEnabled);
          
          drawUSBMSCSetup();
          return;
        }
        
        // Return button (lower-right, ignore if USB MSC active)
        if (touchedReturnButton(x, y)) {
          if (usbMSCActive) {
            Serial.println("Back button disabled while USB MSC active");
            return;
          }
          Serial.println("USB MSC setup back button touched");
          setupSubmenu = 0;
          drawSetupMenu();
          return;
        }
      }
      else if (setupSubmenu == 5) {
        // Icon source setup submenu
        int btnY = 550;
        
        // Toggle button
        if (x >= 20 && x <= 260 && y >= btnY && y <= btnY + 100) {
          Serial.println("Icon source toggle button touched");
          useSDCardIcons = !useSDCardIcons;
          savePrefBool("m5paper", "sdIcons", useSDCardIcons);
          Serial.printf("Icon source: %s\n", useSDCardIcons ? "SD card" : "Embedded");
          drawIconSetup();
          return;
        }
        
        // Return button (lower-right)
        if (touchedReturnButton(x, y)) {
          Serial.println("Icon setup back button touched");
          setupSubmenu = 0;
          drawSetupMenu();
          return;
        }
      }
      else if (setupSubmenu == 6) {
        // Calendar calculation setup submenu
        int btnY = 550;
        
        // Toggle button
        if (x >= 20 && x <= 300 && y >= btnY && y <= btnY + 100) {
          Serial.println("Calendar method toggle button touched");
          useSxwnlCalendar = !useSxwnlCalendar;
          savePrefBool("m5paper", "sxwnl", useSxwnlCalendar);
          Serial.printf("Calendar method: %s\n", useSxwnlCalendar ? "sxwnl" : "Meeus");
          drawCalendarSetup();
          return;
        }
        
        // Return button (lower-right)
        if (touchedReturnButton(x, y)) {
          Serial.println("Calendar setup back button touched");
          setupSubmenu = 0;
          drawSetupMenu();
          return;
        }
      }
      else if (setupSubmenu == 7) {
        // Bluetooth setup submenu
        
        if (bleShowingScan) {
          // Scan view touch handling
          int scanBtnY = 750;
          
          // Scan / Re-scan button
          if (x >= 20 && x <= 170 && y >= scanBtnY && y <= scanBtnY + 70) {
            Serial.println("BLE scan button touched");
            // Show scanning state
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            M5.Display.startWrite();
            M5.Display.fillRect(20, 310, 500, 420, TFT_WHITE);
            drawSystemText("掃描中...", 20, 320, 22);
            M5.Display.endWrite();
            M5.Display.display();
            M5.Display.setEpdMode(epd_mode_t::epd_quality);
            
            scanBLEDevices();
            bleSelectedDevice = -1;
            drawBluetoothSetup();
            return;
          }
          
          // Connect button
          if (bleSelectedDevice >= 0 && bleSelectedDevice < bleDeviceCount) {
            if (x >= 190 && x <= 340 && y >= scanBtnY && y <= scanBtnY + 70) {
              Serial.println("BLE connect button touched");
              
              // Show connecting state
              M5.Display.setEpdMode(epd_mode_t::epd_fast);
              M5.Display.startWrite();
              M5.Display.fillRect(20, 310, 500, 420, TFT_WHITE);
              String msg = "連接中：" + bleDevices[bleSelectedDevice].name;
              drawSystemText(msg.c_str(), 20, 320, 22);
              M5.Display.endWrite();
              M5.Display.display();
              M5.Display.setEpdMode(epd_mode_t::epd_quality);
              
              connectBLEDevice(bleSelectedDevice);
              bleShowingScan = false;
              drawBluetoothSetup();
              return;
            }
          }
          
          // Device list touch (select a device)
          if (bleDeviceCount > 0 && y >= 320 && y < 320 + min(bleDeviceCount, 7) * 55) {
            int idx = (y - 320) / 55;
            if (idx >= 0 && idx < bleDeviceCount && idx < 7) {
              Serial.printf("BLE device %d selected: %s\n", idx, bleDevices[idx].name.c_str());
              bleSelectedDevice = idx;
              drawBluetoothSetup();
              return;
            }
          }
          
          // Return button
          if (touchedReturnButton(x, y)) {
            Serial.println("BLE scan back button touched");
            bleShowingScan = false;
            drawBluetoothSetup();
            return;
          }
        } else {
          // Main BT setup view
          int btnY = 300;
          int scanBtnY = btnY + 110;
          
          // Toggle button
          if (x >= 20 && x <= 260 && y >= btnY && y <= btnY + 90) {
            Serial.println("Bluetooth toggle button touched");
            if (bluetoothActive) {
              stopBLE();
            } else {
              startBLE();
            }
            drawBluetoothSetup();
            return;
          }
          
          // Scan button
          if (x >= 20 && x <= 260 && y >= scanBtnY && y <= scanBtnY + 90) {
            Serial.println("BLE scan devices button touched");
            bleShowingScan = true;
            bleSelectedDevice = -1;
            
            // Show scanning state
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            M5.Display.startWrite();
            M5.Display.fillScreen(TFT_WHITE);
            drawStatusBar();
            drawSystemText("藍牙", 20, 30, 40);
            drawSystemText("掃描中...", 20, 320, 22);
            M5.Display.endWrite();
            M5.Display.display();
            M5.Display.setEpdMode(epd_mode_t::epd_quality);
            
            scanBLEDevices();
            drawBluetoothSetup();
            return;
          }
          
          // Return button
          if (touchedReturnButton(x, y)) {
            Serial.println("Bluetooth setup back button touched");
            bleShowingScan = false;
            setupSubmenu = 0;
            drawSetupMenu();
            return;
          }
        }
      }
    }
    else if (currentMode == MODE_CLOCK) {
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        Serial.println("Clock back button touched - returning to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
    }
    else if (currentMode == MODE_WALLPAPER_LIST) {
      // View toggle button (top-right): x=380..520, y=30..70
      if (x >= 380 && x <= 520 && y >= 30 && y <= 70) {
        wallpaperViewMode = (wallpaperViewMode == 0) ? 1 : 0;
        wallpaperScrollOffset = 0;  // Reset scroll on view change
        Serial.printf("Wallpaper view mode: %s\n", wallpaperViewMode ? "thumbnails" : "list");
        drawWallpaperList();
      }
      // Left arrow (←) = next page (scroll down)
      else if (touchedPrevPage(x, y)) {
        int maxVisible;
        if (wallpaperViewMode == 0) {
          maxVisible = 12;
        } else {
          int cols = 3, rows = 3, thumbPad = 8;
          int thumbW = (DISPLAY_WIDTH - thumbPad * (cols + 1)) / cols;
          int thumbH = (875 - 80 - thumbPad * (rows + 1)) / rows;
          maxVisible = cols * rows;
        }
        int endIdx = min(wallpaperScrollOffset + maxVisible, wallpaperCount);
        if (endIdx < wallpaperCount) {
          Serial.println("Wallpaper list: next page (scroll down)");
          wallpaperScrollOffset += maxVisible;
          if (wallpaperScrollOffset >= wallpaperCount) wallpaperScrollOffset = wallpaperCount - 1;
          drawWallpaperList();
        }
      }
      // Right arrow (→) = prev page (scroll up)
      else if (touchedNextPage(x, y)) {
        if (wallpaperScrollOffset > 0) {
          int maxVisible;
          if (wallpaperViewMode == 0) {
            maxVisible = 12;
          } else {
            int cols = 3, rows = 3, thumbPad = 8;
            int thumbW = (DISPLAY_WIDTH - thumbPad * (cols + 1)) / cols;
            int thumbH = (875 - 80 - thumbPad * (rows + 1)) / rows;
            maxVisible = cols * rows;
          }
          Serial.println("Wallpaper list: prev page (scroll up)");
          wallpaperScrollOffset -= maxVisible;
          if (wallpaperScrollOffset < 0) wallpaperScrollOffset = 0;
          drawWallpaperList();
        }
      }
      // Universal nav: return button
      else if (touchedReturnButton(x, y)) {
        Serial.println("Wallpaper list back button touched - returning to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
      // Random button (隨機): x=200..300, y=900..944
      else if (x >= 200 && x <= 300 && y >= 900 && y <= 944) {
        Serial.println("Wallpaper list: random");
        if (wallpaperCount > 0) {
          selectedWallpaper = random(0, wallpaperCount);
          Serial.printf("Random wallpaper: %d\n", selectedWallpaper);
          currentMode = MODE_WALLPAPER;
          drawWallpaper();
        }
      }
      // Rotate button (輪播): x=330..440, y=900..944
      else if (x >= 330 && x <= 440 && y >= 900 && y <= 944) {
        wallpaperRotateActive = !wallpaperRotateActive;
        Serial.printf("Wallpaper rotate: %s\n", wallpaperRotateActive ? "ON" : "OFF");
        if (wallpaperRotateActive) {
          wallpaperRotateLastChange = millis();
          if (wallpaperCount > 0) {
            selectedWallpaper = random(0, wallpaperCount);
            currentMode = MODE_WALLPAPER;
            drawWallpaper();
          }
        } else {
          drawWallpaperList();
        }
      }
      // Check if wallpaper item was touched (list or thumbnail)
      else if (wallpaperCount > 0 && y >= 80 && y <= 875) {
        if (wallpaperViewMode == 0) {
          // Name list: item touch
          int itemHeight = 65;
          int startIdx = wallpaperScrollOffset;
          int maxVisible = 12;
          int endIdx = min(startIdx + maxVisible, wallpaperCount);
          for (int i = startIdx; i < endIdx; i++) {
            int itemY = 80 + (i - startIdx) * itemHeight;
            if (y >= itemY && y <= itemY + 60 && x >= 20 && x <= 520) {
              Serial.printf("Wallpaper %d selected: %s\n", i, wallpaperFiles[i].c_str());
              selectedWallpaper = i;
              currentMode = MODE_WALLPAPER;
              drawWallpaper();
              break;
            }
          }
        } else {
          // Thumbnail: grid touch
          int cols = 3, rows = 3, thumbPad = 8;
          int thumbW = (DISPLAY_WIDTH - thumbPad * (cols + 1)) / cols;
          int thumbH = (875 - 80 - thumbPad * (rows + 1)) / rows;
          int maxVisible = cols * rows;
          int startIdx = wallpaperScrollOffset;
          int endIdx = min(startIdx + maxVisible, wallpaperCount);
          
          for (int i = startIdx; i < endIdx; i++) {
            int idx = i - startIdx;
            int col = idx % cols;
            int row = idx / cols;
            int tx = thumbPad + col * (thumbW + thumbPad);
            int ty = 80 + thumbPad + row * (thumbH + thumbPad);
            if (x >= tx && x <= tx + thumbW && y >= ty && y <= ty + thumbH) {
              Serial.printf("Thumbnail %d selected: %s\n", i, wallpaperFiles[i].c_str());
              selectedWallpaper = i;
              currentMode = MODE_WALLPAPER;
              drawWallpaper();
              break;
            }
          }
        }
      }
    }
    else if (currentMode == MODE_WALLPAPER) {
      // Touch on wallpaper returns to list (also stops rotate)
      Serial.println("Wallpaper touched - returning to list");
      wallpaperRotateActive = false;
      currentMode = MODE_WALLPAPER_LIST;
      drawWallpaperList();
    }
    else if (currentMode == MODE_MOTTO_TEST) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Motto test: return to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      } else {
        // Any other touch = show next random motto
        Serial.println("Motto test: showing next motto...");
        drawMottoScreen();
      }
    }
    else if (currentMode == MODE_FONT_TEST) {
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        Serial.println("Font test back button touched - returning to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
    }
    else if (currentMode == MODE_BOOK_LIST) {
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        Serial.println("Back button touched - returning to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
      // Touch book to open
      else if (y >= 120 && y <= 620) {
        Serial.printf("Touch on book list: y=%d\n", y);
        if (sdCardAvailable && bookCount > 0) {
          // Load book from SD card
          int bookIndex = (y - 120) / 50;
          if (bookIndex >= 0 && bookIndex < bookCount) {
            currentBook = bookDisplayName[bookIndex];
            Serial.printf("Selected: %s (%s)\n", currentBook.c_str(), bookList[bookIndex].c_str());
            
            // Show loading indicator before attempting to load
            {
              M5.Display.setEpdMode(epd_mode_t::epd_fastest);
              M5.Display.fillRect(20, 800, 500, 60, TFT_WHITE);
              drawSystemText("載入中...", 20, 810, 24);
              M5.Display.display();
            }
            
            if (loadBook(bookIndex)) {
              currentMode = MODE_READING;
              drawReading();
            } else {
              // Show error message, then redraw book list
              Serial.println("Failed to load book, showing error");
              M5.Display.setEpdMode(epd_mode_t::epd_fastest);
              M5.Display.fillRect(20, 800, 500, 60, TFT_WHITE);
              drawSystemText("載入失敗 - 檔案可能過大或損壞", 20, 810, 20);
              M5.Display.display();
              delay(2000);
              drawBookList();
            }
          }
        } else {
          // No SD card, show sample text with instructions
          Serial.println("No SD - showing sample text");
          currentPageContent = "這是示例文本。\n\n這裡是第一段內容，用於展示直式排版效果。文字從右到左，從上到下排列。\n\n中文傳統排版方式，讓閱讀更加自然流暢。您可以觸控左右區域翻頁。\n\n請在SD卡的/books/目錄下:\n1. 將書籍重命名為book1.txt, book2.txt等ASCII文件名\n2. 創建books.txt清單文件\n3. 格式：book1.txt|三國演義\n\n觸控左側上一頁，右側下一頁。";
          totalPages = 1;
          currentPage = 0;
          currentMode = MODE_READING;
          drawReading();
        }
      }
    }
  }
  
  delay(50);
}
