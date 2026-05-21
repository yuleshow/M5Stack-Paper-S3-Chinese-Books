// M5Stack Paper S3 Chinese E-Book Reader
// Main entry point: setup() and loop()

#include "globals.h"
#include "conv_table.h"
#include "dictionary.h"
#include "s3cover_jpg.h"
#include "sleeping_jpg.h"
#include <esp_sleep.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>

SET_LOOP_TASK_STACK_SIZE(64 * 1024);

// RTC_DATA_ATTR survives deep sleep
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int setupCrashCount = 0;  // counts consecutive incomplete boots
RTC_DATA_ATTR bool loadBookCrashed = false;  // set before loadBook, cleared after success
bool safeMode = false;  // skip dangerous SD ops if boot keeps crashing

static bool seedExactPageOffsetForJump(size_t targetOffset) {
  if (bytesPerPage <= 0 || totalPages <= 0) return false;

  int estimatedPage = targetOffset / bytesPerPage;
  if (estimatedPage >= totalPages) estimatedPage = totalPages - 1;
  if (estimatedPage < 0) estimatedPage = 0;
  currentPage = estimatedPage;

  lastRenderedForPage = currentPage - 1;
  lastRenderedNextOffset = targetOffset;

  if (pageByteOffsets && currentPage < MAX_PAGE_OFFSETS) {
    while (pageOffsetsCount < currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
      int gap = currentPage - pageOffsetsCount;
      size_t est = (targetOffset > (size_t)gap * bytesPerPage) ?
                   targetOffset - (size_t)gap * bytesPerPage : 0;
      pageByteOffsets[pageOffsetsCount] = est;
      pageOffsetsCount++;
    }
    pageByteOffsets[currentPage] = targetOffset;
    if (pageOffsetsCount <= currentPage) pageOffsetsCount = currentPage + 1;
  }

  return true;
}

static bool jumpToEpubChapterOffset(int chapterIdx, size_t storedOffset, bool hasStoredOffset) {
  if (!epubChapters || chapterIdx < 0 || chapterIdx >= epubChapterCount) return false;

  if (epubIsImageBased) {
    currentPage = chapterIdx;
    return true;
  }

  size_t oldChapterStart = epubChapters[chapterIdx].cumulativeOffset;
  size_t relativeOffset = (hasStoredOffset && storedOffset >= oldChapterStart) ?
                          storedOffset - oldChapterStart : 0;

  bool chapterLoaded = (epubFullText &&
      chapterIdx >= epubLoadedStartChapter &&
      chapterIdx < epubLoadedEndChapter);
  if (!chapterLoaded) {
    if (!epubLoadChapterRange(chapterIdx)) return false;
    totalBookBytes = epubEstimatedTotalBytes;
    if (bytesPerPage > 0) totalPages = (totalBookBytes / bytesPerPage) + 1;
  }

  size_t newChapterStart = epubChapters[chapterIdx].cumulativeOffset;
  size_t actualSize = epubChapters[chapterIdx].actualTextSize;
  if (actualSize > 0 && relativeOffset >= actualSize) relativeOffset = 0;

  size_t targetOffset = newChapterStart + relativeOffset;
  if (targetOffset >= epubEstimatedTotalBytes) targetOffset = newChapterStart;

  return seedExactPageOffsetForJump(targetOffset);
}

static int resolveEpubHrefToChapter(const String& href) {
  String targetFile = href;
  int hashPos = targetFile.indexOf('#');
  if (hashPos >= 0) targetFile = targetFile.substring(0, hashPos);
  targetFile = pathNormalize(targetFile);

  if (targetFile.length() == 0 || targetFile.endsWith("/")) {
    int sourceChapter = epubChapterForOffset(currentPageByteOffset);
    if (sourceChapter >= 0 && sourceChapter < epubChapterCount) return sourceChapter;
  }

  for (int ci = 0; ci < epubChapterCount; ci++) {
    int zi = epubChapters[ci].zipEntryIndex;
    if (zi >= 0 && zi < epubZipEntryCount) {
      if (epubZipEntries[zi].filename == targetFile) return ci;
    }
  }
  return -1;
}

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
  if (epubIsHorizontal) {
    prefs.putInt("fontIdxEn", selectedFontIndex);
    prefs.putInt("rdFontSzEn", readingFontSize);
  } else {
    prefs.putInt("fontIdx", selectedFontIndex);
    prefs.putInt("rdFontSz", readingFontSize);
  }
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
  
  // Check wake-up reason early to optimize delay
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool isDeepSleepWake = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || 
                          wakeup_reason == ESP_SLEEP_WAKEUP_TIMER ||
                          wakeup_reason == ESP_SLEEP_WAKEUP_EXT1);
  
  // Shorter delay for deep sleep wake (serial port already initialized by USB stack)
  delay(isDeepSleepWake ? 100 : 1000);
  
  // Initialize watchdog timer: auto-reboot if device hangs for 30 seconds
  // This prevents USB from entering a bad state that interferes with
  // Bluetooth peripherals on macOS (shared USB/BT controller)
  esp_task_wdt_init(30, true);  // 30s timeout, panic (reboot) on expiry
  esp_task_wdt_add(NULL);       // Add current task (loopTask) to watchdog
  
  bootCount++;
  
  Serial.println("\n\n=== M5Paper S3 E-Book Reader ===");
  Serial.printf("Boot #%d, Wakeup cause: %d\n", bootCount, wakeup_reason);
  
  // Detect and break boot crash loops
  setupCrashCount++;
  if (setupCrashCount > 3) {
    safeMode = true;
    Serial.println("\n!!! SAFE MODE: Too many incomplete boots - skipping SD cleanup/WiFi !!!");
  }
  Serial.printf("Setup crash counter: %d%s\n", setupCrashCount, safeMode ? " (SAFE MODE)" : "");
  
  bool timerWakeHandled = false;  // track if M5.begin was already called
  
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
    timerWakeHandled = true;  // M5.begin already called
    delay(200);
    M5.update();
    
    auto touch = M5.Touch.getDetail();
    if (touch.isPressed()) {
      Serial.println("Touch detected during timer wake - resuming fully");
      // Fall through to normal boot (M5.begin already done)
    } else {
      // No touch — check if external power was just connected
      bool usbConnected = isExternalPowerConnected();
      if (usbConnected) {
        Serial.println("USB power detected during timer wake - resuming fully");
        // Fall through to normal boot (M5.begin already done)
      } else {
        Serial.println("No interaction - refreshing sleep screen and going back to sleep");
        setupCrashCount = 0;  // not a crash, normal sleep cycle
        enterDeepSleep();
        // enterDeepSleep won't return (unless USB is connected)
        // If it returned, USB was just plugged in — fall through
      }
    }
  }
  
  // Only call M5.begin if not already done in timer wake path
  if (!timerWakeHandled) {
    auto cfg = M5.config();
    // Cold boot: clear display to eliminate grey ghosting from previous content
    // Deep sleep wake: skip clear for faster resume (handled by timer path above)
    bool isColdBoot = (wakeup_reason != ESP_SLEEP_WAKEUP_EXT0 && 
                       wakeup_reason != ESP_SLEEP_WAKEUP_TIMER &&
                       wakeup_reason != ESP_SLEEP_WAKEUP_EXT1);
    cfg.clear_display = isColdBoot;
    M5.begin(cfg);
  }
  
  Serial.println("M5 initialized");
  
  // Show loading screen (skip for deep sleep wake to avoid extra refresh)
  if (!isDeepSleepWake) {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);
    // Status bar: time (top-left) + battery (top-right)
    drawStatusBar();
    
    // Loading text centered
    if (safeMode) {
      drawSystemTextCentered("安全模式", M5.Display.width() / 2, M5.Display.height() / 2 - 30, 36);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString("Safe Mode", M5.Display.width() / 2, M5.Display.height() / 2 + 30);
    } else {
      drawSystemTextCentered("AI智能整合中...", M5.Display.width() / 2, M5.Display.height() / 2 - 30, 36);
      // Progress bar outline (same style as book loading bar)
      int barX = 70, barY = M5.Display.height() / 2 + 50;
      int barW = 400, barH = 30;
      M5.Display.drawRect(barX, barY, barW, barH, TFT_BLACK);
      M5.Display.drawRect(barX + 1, barY + 1, barW - 2, barH - 2, TFT_BLACK);
    }
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
    sdLog("BOOT #%d wakeup=%d heap=%u psram=%u safeMode=%d",
          bootCount, (int)wakeup_reason, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), safeMode);
    if (!isDeepSleepWake && !safeMode) updateLoadProgress(10);
    // CRITICAL: Let SD subsystem fully initialize before using it
    delay(isDeepSleepWake ? 100 : 500);
    yield();
    
    // Clean up macOS dot files (skip in safe mode - this can cause WDT resets)
    if (!safeMode) {
      cleanupMacOSFiles();
    } else {
      Serial.println("SAFE MODE: Skipping macOS cleanup");
    }
    
    // Scan books immediately while SD is fresh
    Serial.println("Pre-scanning books...");
    scanBooks();
    if (bookCount > 0) {
      Serial.printf("✓ Loaded %d books\n", bookCount);
    }
    // Load S2T/T2S conversion tables into PSRAM
    loadConvTables();
    if (!isDeepSleepWake && !safeMode) updateLoadProgress(20);
    
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
    if (!isDeepSleepWake && !safeMode) updateLoadProgress(35);

    // Med passcode is loaded from config.ini by loadWiFiConfig()

    // Load custom calendar events from SD card
    Serial.println("Loading custom calendar events...");
    loadCustomEvents();

    if (!isDeepSleepWake && !safeMode) updateLoadProgress(40);
    // Try to load a TTF font for better character coverage
    Serial.println("Scanning and loading fonts...");
    
    // Initialize OpenFontRender
    ofr.setSerial(Serial);
    
    // Scan SD card for all available fonts (.ttf, .ttc, .bin, .otf)
    scanFontFiles();
    
    numFonts = fontFileCount;
    
    // Restore system font preference (0=GenYoMinTW, 1=Unifont)
    systemFontChoice = loadPrefInt("m5paper", "sysFont", 0);
    
    // Find and load the preferred system font
    bool fontLoaded = false;
    if (systemFontChoice == 1) {
      // Silver font mode: search for Silver.ttf
      for (int fi = 0; fi < fontFileCount; fi++) {
        String lower = fontFileList[fi];
        lower.toLowerCase();
        if (lower.startsWith("silver") && lower.endsWith(".ttf")) {
          Serial.printf("Loading Silver system font: %s\n", fontFileList[fi].c_str());
          systemFontIndex = fi;
          systemFontFile = fontFileList[fi];
          if (loadTTFFont(fontFileList[fi].c_str(), 30)) {
            fontLoaded = true;
            Serial.println("✓ Silver loaded as system font!");
          }
          break;
        }
      }
      // Load SD-card labels for Silver
      if (fontLoaded) {
        loadSDLabels();
      }
    }
    
    if (!fontLoaded) {
      // Default: find and load GenYoMinTW-Regular.ttf as system font
      for (int fi = 0; fi < fontFileCount; fi++) {
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
    }
    
    // Restore saved reading font selection (default to system font if no preference)
    readingFontIndex = loadPrefInt("ereader", "fontIdx", -1);
    if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) {
      // When Silver is the system font, default reading to GenYoMinTW
      // (Silver lacks full CJK coverage needed for book reading)
      int defaultReadingIdx = max(0, systemFontIndex);
      if (systemFontChoice == 1) {
        for (int fi = 0; fi < fontFileCount; fi++) {
          if (fontFileList[fi].equalsIgnoreCase("GenYoMinTW-Regular.ttf")) {
            defaultReadingIdx = fi;
            break;
          }
        }
      }
      readingFontIndex = defaultReadingIdx;
    }
    if (readingFontIndex < 0 || readingFontIndex >= fontFileCount) readingFontIndex = 0;
    selectedFontIndex = readingFontIndex;
    // Restore saved reading font file (may be paired BIN)
    readingFontFile = loadPrefStr("ereader", "fontFile",
                       (readingFontIndex >= 0 && readingFontIndex < fontFileCount) ? fontFileList[readingFontIndex] : String(""));
    
    // If no system font found, try any TTF/OTF as system font
    if (!fontLoaded) {
      for (int fi = 0; fi < fontFileCount; fi++) {
        String fname = fontFileList[fi];
        if (fname.endsWith(".ttf") || fname.endsWith(".TTF") ||
            fname.endsWith(".ttc") || fname.endsWith(".TTC") ||
            fname.endsWith(".otf") || fname.endsWith(".OTF")) {
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
      for (int fi = 0; fi < fontFileCount; fi++) {
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
    
    if (!isDeepSleepWake && !safeMode) updateLoadProgress(70);
    if (!fontLoaded) {
      Serial.println("\n=== Font Loading Summary ===");
      Serial.println("No custom fonts loaded - using built-in CJK fonts");
      
      // Fallback to built-in fonts
      Serial.println("No custom fonts loaded - using Font2 (built-in)");
      M5.Display.setFont(&fonts::Font2);
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
  
  if (!isDeepSleepWake && !safeMode) updateLoadProgress(80);
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
  
  if (!isDeepSleepWake && !safeMode) updateLoadProgress(90);
  // Load WiFi config first (need timezone before RTC restore)
  Serial.println("\n=== Loading Configuration ===");
  loadWiFiConfig();
  Serial.printf("Config loaded - WiFi configured: %s\n", wifiConfig.configured ? "YES" : "NO");
  Serial.println("Timezone: " + timeConfig.timezone);

  // Load auto-sleep setting
  prefs.begin("ereader", true);
  autoSleepEnabled = prefs.getBool("autoSleep", false);  // Default: disabled
  comicZoomMode = prefs.getInt("comicZoom", 0);  // Default: quadrant mode
  pageRefreshMode = prefs.getInt("pgRefresh", 0);  // Default: system
  paragraphIndent = prefs.getBool("paraIndent", false);  // Default: no indent
  prefs.end();
  Serial.printf("Auto-sleep setting loaded: %s\n", autoSleepEnabled ? "ENABLED" : "DISABLED");
  Serial.printf("Comic zoom mode loaded: %d\n", comicZoomMode);
  Serial.printf("Page refresh mode loaded: %d\n", pageRefreshMode);
  Serial.printf("Paragraph indent loaded: %s\n", paragraphIndent ? "YES" : "NO");
  
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
  
  // Auto-connect WiFi (skip in safe mode and deep sleep wake to avoid long timeout)
  // Deep sleep wake: defer WiFi to loop() reconnection logic for faster resume
  Serial.println("\n=== WiFi Auto-Connect ===");
  if (safeMode) {
    Serial.println("SAFE MODE: Skipping WiFi auto-connect");
  } else if (isDeepSleepWake) {
    Serial.println("Deep sleep wake: deferring WiFi to loop() for faster resume");
    if (wifiConfig.configured) {
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
      // Non-blocking: let it connect in background, loop() will pick it up
    }
  } else if (wifiConfig.configured) {
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
  
  // Load web server preference. Default to enabled, but respect the settings toggle.
  webServerEnabled = loadPrefBool("m5paper", "webServer", true);
  usbMSCEnabled = loadPrefBool("m5paper", "usbMSC", false);
  useSDCardIcons = loadPrefBool("m5paper", "sdIcons", false);
  useSxwnlCalendar = loadPrefBool("m5paper", "sxwnl", false);

  // Load medication reminder state (persists across reboots)
  prefs.begin("m5paper", true);
  medReminderPressTime = (time_t)prefs.getLong("medTime", 0);
  prefs.end();
  if (medReminderPressTime != 0) {
    Serial.printf("Med reminder: restored press time %ld\n", (long)medReminderPressTime);
  }
  Serial.printf("USB MSC enabled: %s\n", usbMSCEnabled ? "YES" : "NO");
  Serial.printf("Web server enabled: %s\n", webServerEnabled ? "YES" : "NO");

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
  setupCrashCount = 0;  // setup completed successfully - reset crash counter

  // If the last book load crashed (WDT reset mid-loadBook), clear the
  // "last book" preference so the user isn't stuck in a crash loop when
  // tapping "最後閱讀".
  if (loadBookCrashed) {
    Serial.println("WARNING: previous loadBook() crashed — clearing lastBook preference");
    savePrefStr("ereader", "lastBook", "");
    loadBookCrashed = false;
  }

  Serial.println("Setup complete!");
}

void loop() {
  M5.update();
  esp_task_wdt_reset();  // Feed watchdog every loop iteration
  
  // Poll IMU for fortune slip shake detection
  if (currentMode == MODE_FORTUNE_SHAKE) {
    pollFortuneShake();
  }

  // Tamagotchi tick (easter egg)
  if (currentMode == MODE_TAMAGOTCHI) {
    pollTamagotchi();
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

  // Handle web server if enabled (skip while USB MSC active — SD card not accessible)
  if (webServerEnabled && !usbMSCActive) {
    // Start server on router WiFi when available, otherwise via fallback AP.
    if (!webServerRunning) {
      startWebServer();
    }
    // Stop only if both router WiFi and fallback AP are unavailable.
    else if (WiFi.status() != WL_CONNECTED && (WiFi.getMode() & WIFI_AP) == 0) {
      stopWebServer();
    }
    // Handle client requests
    if (webServerRunning) {
      handleWebClients();
      updateScreenCapture();
    }
  }

  // Periodic WiFi health check (every 30s, log to SD if status changed)
  // Also attempt reconnect if WiFi dropped
  {
    static int lastWiFiStatus = -1;
    static unsigned long lastWiFiCheck = 0;
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastWiFiCheck > 30000) {
      lastWiFiCheck = millis();
      int st = (int)WiFi.status();
      if (st != lastWiFiStatus) {
        sdLog("WiFi status changed: %d -> %d (mode=%d heap=%u)",
              lastWiFiStatus, st, (int)currentMode, (unsigned)ESP.getFreeHeap());
        lastWiFiStatus = st;
      }
      // If WiFi is not connected and we have credentials, try reconnect every 60s
      if (st != WL_CONNECTED && webServerEnabled &&
          wifiConfig.ssid.length() > 0 &&
          millis() - lastReconnectAttempt > 60000) {
        lastReconnectAttempt = millis();
        sdLog("WiFi reconnect attempt (status=%d)", st);
        WiFi.disconnect(false);
        delay(100);
        WiFi.mode(webServerRunning ? WIFI_AP_STA : WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
      }
    }
  }

  // Medication reminder auto-reset after 18 hours
  if (medReminderPressTime != 0) {
    time_t now = time(NULL);
    if (now > 1000000000 && medReminderPressTime <= now && (now - medReminderPressTime > (time_t)MED_REMINDER_RESET_SEC)) {
      Serial.println("Med reminder: auto-reset after 18 hours");
      medReminderPressTime = 0;
      prefs.begin("m5paper", false);
      prefs.putLong("medTime", 0);
      prefs.end();
      if (currentMode == MODE_MED_REMINDER) {
        drawMedReminder();
      }
    }
  }

  // Idle sleep disabled — keep device awake for web server / screenshot access
  // if (autoSleepEnabled) {
  //   bool hasExternalPower = isExternalPowerConnected();
  //   if (lastActivityTime > 0 && !hasExternalPower && (millis() - lastActivityTime > IDLE_SLEEP_TIMEOUT)) {
  //     if (currentMode != MODE_CLOCK) {
  //       Serial.println("Idle timeout - entering deep sleep");
  //       enterDeepSleep();
  //     }
  //   }
  // }
  
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
  
  // Swipe detection for reading mode
  static int swipeTouchStartX = -1;
  static int swipeTouchStartY = -1;
  static unsigned long swipeTouchStartTime = 0;
  static bool longPressHandled = false;
  bool hasSwipeEvent = false;
  int swipeDeltaX = 0;
  
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
      // Record swipe start
      swipeTouchStartX = touch.x;
      swipeTouchStartY = touch.y;
      swipeTouchStartTime = millis();
      longPressHandled = false;
      // In reading mode, defer tap until release (to distinguish from swipes)
      if (currentMode != MODE_READING && currentMode != MODE_DICT_POPUP) {
        hasTouchEvent = true;
      }
    }
    // Long press detection for English dictionary popup
    if (currentMode == MODE_READING && epubIsHorizontal && !epubIsImageBased &&
        swipeTouchStartX >= 0 && !longPressHandled &&
        !touch.wasPressed() && !touch.wasReleased()) {
      unsigned long holdMs = millis() - swipeTouchStartTime;
      if (holdMs > 600) {
        int dx = abs(touch.x - swipeTouchStartX);
        int dy = abs(touch.y - swipeTouchStartY);
        if (dx < 20 && dy < 20) {
          longPressHandled = true;
          int wordIdx = engFindWordAt(swipeTouchStartX, swipeTouchStartY);
          if (wordIdx >= 0) {
            const char* tappedWord = &engWordPool[engWordPositions[wordIdx].poolOffset];
            // Clean word: strip punctuation, lowercase, keep alpha/apostrophe/hyphen
            char cleanWord[64];
            int ci = 0;
            for (int wi = 0; tappedWord[wi] && ci < 62; wi++) {
              char c = tappedWord[wi];
              if (c >= 'A' && c <= 'Z') {
                cleanWord[ci++] = c + 32;  // lowercase
              } else if ((c >= 'a' && c <= 'z') || c == '\'') {
                cleanWord[ci++] = c;
              } else if (c == '-') {
                cleanWord[ci++] = c;  // keep hyphens in compound words
              }
            }
            cleanWord[ci] = '\0';
            // Strip trailing punctuation
            while (ci > 0 && (cleanWord[ci-1] == '\'' || cleanWord[ci-1] == '-')) cleanWord[--ci] = '\0';
            Serial.printf("DICT: long press on '%s' clean='%s' at (%d,%d)\n", tappedWord, cleanWord, swipeTouchStartX, swipeTouchStartY);
            static char def[256];
            bool found = dictLookup(cleanWord, def, sizeof(def));
            drawDictPopup(cleanWord, found ? def : "查無此字 (Not found)");
            currentMode = MODE_DICT_POPUP;
          }
        }
      }
    }
    if (touch.wasReleased() && swipeTouchStartX >= 0) {
      if (longPressHandled) {
        // Long press was handled — skip normal tap/swipe processing
        swipeTouchStartX = -1;
        longPressHandled = false;
      } else {
      swipeDeltaX = touch.x - swipeTouchStartX;
      int swipeDeltaY = touch.y - swipeTouchStartY;
      unsigned long swipeDuration = millis() - swipeTouchStartTime;
      // Horizontal swipe: min 60px, mostly horizontal, within 800ms
      if (abs(swipeDeltaX) > 60 && abs(swipeDeltaX) > abs(swipeDeltaY) * 2 && swipeDuration < 800) {
        hasSwipeEvent = true;
        Serial.printf("Swipe detected: dx=%d, dy=%d, duration=%lums\n", swipeDeltaX, swipeDeltaY, swipeDuration);
      } else if (currentMode == MODE_READING || currentMode == MODE_DICT_POPUP) {
        // Not a swipe in reading mode — process as tap at original press location
        hasTouchEvent = true;
        x = swipeTouchStartX;
        y = swipeTouchStartY;
      }
      swipeTouchStartX = -1;
      } // end else (not longPressHandled)
    }
  }
  
  if (hasTouchEvent || hasSwipeEvent) {
    lastActivityTime = millis();
    lastTouchProcessedTime = millis();
    // Skip generic TOUCH log in reading mode — page-turn actions are logged individually (PAGE:)
    if (currentMode != MODE_READING) {
      sdLog("TOUCH: x=%d y=%d mode=%d swipe=%d WiFi=%d heap=%u",
            x, y, currentMode, hasSwipeEvent, (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
    }
    DEBUG_LOG_THROTTLE(200, "Touch: %d, %d (mode=%d)", x, y, currentMode);
    
    // Sleep button — only active in modes where the button is actually drawn
    if (hasTouchEvent && touchedSleepButton(x, y)
        && (currentMode == MODE_DASHBOARD || currentMode == MODE_BOOK_LIST
            || currentMode == MODE_CALENDAR || currentMode == MODE_FORTUNE_SLIPS
            || currentMode == MODE_TOOLS_MENU || currentMode == MODE_MED_REMINDER
            || currentMode == MODE_WEATHER
            || (currentMode == MODE_SETUP && setupSubmenu == 0))) {
      sdLog("USER: sleep button pressed mode=%d", currentMode);
      Serial.println("Sleep button pressed");
      enterDeepSleep();
      // enterDeepSleep returns if USB power is connected — show motto screen instead
      if (currentMode != MODE_MOTTO_TEST) {
        currentMode = MODE_MOTTO_TEST;
        drawMottoScreen();
      }
    }
    else if (currentMode == MODE_WELCOME) {
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
          
          // Icon 0 is E-Book — always show book list (no auto-resume)
          if (i == 0) {
            sdLog("MODE: DASHBOARD -> BOOK_LIST");
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
            loadShoppingList();
            loadCheckedItems();
            calculateShoppingPages();
            currentShoppingPage = 0;
            drawShoppingList();
          }
          // Icon 4 is Weather (天氣)
          else if (i == 4) {
            DEBUG_LOG("Opening weather...");
            currentMode = MODE_WEATHER;
            drawWeather(true);
          }
          // Icon 5 is Tools (工具)
          else if (i == 5) {
            DEBUG_LOG("Opening tools menu...");
            currentMode = MODE_TOOLS_MENU;
            drawToolsMenu();
          }
          // Icon 6 is Settings (設定)
          else if (i == 6) {
            DEBUG_LOG("Opening settings...");
            currentMode = MODE_SETUP;
            setupSubmenu = 0;  // Start at main setup menu
            setupMenuPage = 0;
            drawSetupMenu();
          }
          // Icon 7 is Fortune Slips (求籖)
          else if (i == 7) {
            Serial.println("Fortune slips icon touched...");
            currentMode = MODE_FORTUNE_SLIPS;
            drawFortuneSlipsMenu();
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
      Serial.printf("FONT_MENU touch: x=%d, y=%d\n", x, y);
      // Return button (lower-right)
      if (touchedReturnButton(x, y)) {
        Serial.println("Font menu back button - returning to previous mode");
        if (epubIsHorizontal) {
          savePrefInt("ereader", "fontIdxEn", selectedFontIndex);
          savePrefStr("ereader", "fontFileEn", readingFontFile);
        } else {
          savePrefInt("ereader", "fontIdx", selectedFontIndex);
          savePrefStr("ereader", "fontFile", readingFontFile);
        }
        fontMenuPage = 0;
        if (fontMenuReturnMode == MODE_READING) {
          currentMode = MODE_READING;
          size_t savedOffset = currentPageByteOffset;
          Serial.printf("Font return: heap=%u psram=%u offset=%u\n",
                        ESP.getFreeHeap(), ESP.getFreePsram(), (unsigned)savedOffset);
          // Free EPUB text buffer to reduce PSRAM fragmentation (same rationale
          // as font selection — preview pass fragmented PSRAM).
          if (currentBookIsEpub && epubFullText) {
            free(epubFullText);
            epubFullText = nullptr;
            epubFullTextLen = 0;
          }
          yield(); esp_task_wdt_reset();
          bool fontOK = loadReadingFont();
          yield(); esp_task_wdt_reset();
          if (!fontOK) {
            Serial.println("ERROR: loadReadingFont failed on font menu return");
          }
          recalculatePages();
          // Restore reading position from byte offset
          if (bytesPerPage > 0 && savedOffset > 0) {
            currentPage = savedOffset / bytesPerPage;
          }
          if (currentPage >= totalPages) currentPage = totalPages - 1;
          // Seed page offset array for accurate navigation
          if (pageByteOffsets && currentPage > 0 && savedOffset > 0) {
            while ((int)pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
              int gap = currentPage - pageOffsetsCount;
              size_t est = (savedOffset > (size_t)gap * bytesPerPage) ?
                           savedOffset - (size_t)gap * bytesPerPage : 0;
              pageByteOffsets[pageOffsetsCount] = est;
              pageOffsetsCount++;
            }
            if (currentPage < (int)pageOffsetsCount)
              pageByteOffsets[currentPage] = savedOffset;
          }
          yield(); esp_task_wdt_reset();
          if (!loadCurrentPage()) {
            Serial.println("ERROR: loadCurrentPage failed on font menu return");
          }
          pagesSinceFullRefresh = 0;  // Force quality e-ink refresh after font change
          drawReading();
        } else {
          currentMode = MODE_DASHBOARD;
          loadSystemFont();
          drawDashboard();
        }
      }
      // Next page button (left = next/forward)
      else if (touchedPrevPage(x, y)) {
        int visibleCount = fontMenuFilteredCount;
        int totalPages = (visibleCount + FONTS_PER_PAGE - 1) / FONTS_PER_PAGE;
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
      // Select font by touch Y position
      else if (y >= 90 && y <= 800) {
        int slot = fontMenuPage * FONTS_PER_PAGE + (y - 90) / 100;
        // Map visual slot to real font index
        int fontIdx;
        int visibleCount = fontMenuFilteredCount;
        if (slot < 0 || slot >= visibleCount) { /* out of range */ }
        else {
        fontIdx = fontMenuFilteredMap[slot];
        Serial.printf("FONT_MENU: tap y=%d, x=%d → slot=%d fontIdx=%d (fontFileCount=%d)\n", y, x, slot, fontIdx, fontFileCount);
        if (fontIdx >= 0 && fontIdx < fontFileCount) {
        // English mode: tap anywhere on font row to select directly
        bool tappedBin = false;
        bool tappedTTF = false;
        bool isStandaloneBin = (fontFileList[fontIdx].endsWith(".bin") || fontFileList[fontIdx].endsWith(".BIN"));
        bool englishFontMode = (fontMenuReturnMode == MODE_READING && epubIsHorizontal);
        if (englishFontMode) {
          // English mode — direct select
          tappedTTF = true;
          selectedFontIndex = fontIdx;
          readingFontIndex = fontIdx;
          readingFontFile = fontFileList[fontIdx];
          Serial.printf("English font select: %s\n", readingFontFile.c_str());
          savePrefInt("ereader", "fontIdxEn", readingFontIndex);
          savePrefStr("ereader", "fontFileEn", readingFontFile);
        } else {
        // Chinese mode: BIN/TTF button selection
        {
          int itemY = 90 + (slot - fontMenuPage * FONTS_PER_PAGE) * 100;
          int btnRelY = y - itemY;
          if (btnRelY >= 25 && btnRelY <= 61) {
            bool isSilverFont = (fontFileList[fontIdx].indexOf("Silver") >= 0 || fontFileList[fontIdx].indexOf("silver") >= 0);
            int btnX = 510;
            
            // TTF button (rightmost, for any font that has a TTF)
            if (!isStandaloneBin) {
              String ttfLabel = "TTF";
              int ttfBtnW = ttfLabel.length() * 12 + 16;
              btnX -= (ttfBtnW + 4);
              if (x >= btnX && x <= btnX + ttfBtnW) {
                tappedTTF = true;
                selectedFontIndex = fontIdx;
                readingFontIndex = fontIdx;
                readingFontFile = fontFileList[fontIdx];
                Serial.printf("Switched to TTF: %s\n", readingFontFile.c_str());
                savePrefInt("ereader", "fontIdx", readingFontIndex);
                savePrefStr("ereader", "fontFile", readingFontFile);
              }
            }
            
            // BIN size buttons (to the left of TTF button)
            if (!tappedTTF && fontBinCount[fontIdx] > 0) {
              for (int b = fontBinCount[fontIdx] - 1; b >= 0; b--) {
                int displaySize = isSilverFont ? silverNominalSize(fontBinSizes[fontIdx][b]) : (int)fontBinSizes[fontIdx][b];
                String label = String(displaySize);
                int btnW = label.length() * 12 + 16;
                btnX -= (btnW + 4);
                if (x >= btnX && x <= btnX + btnW) {
                  tappedBin = true;
                  selectedFontIndex = fontIdx;
                  readingFontIndex = fontIdx;
                  readingFontFile = fontBinFiles[fontIdx][b];
                  readingFontSize = isSilverFont ? silverNominalSize(fontBinSizes[fontIdx][b]) : (int)fontBinSizes[fontIdx][b];
                  Serial.printf("Switched to BIN: %s (size=%dpt)\n", readingFontFile.c_str(), readingFontSize);
                  savePrefInt("ereader", "fontIdx", readingFontIndex);
                  savePrefStr("ereader", "fontFile", readingFontFile);
                  savePrefInt("ereader", epubIsHorizontal ? "rdFontSzEn" : "rdFontSz", readingFontSize);
                  break;
                }
              }
            }
          }
        }
        } // end Chinese mode
        if (tappedBin || tappedTTF) {
                if (fontMenuReturnMode == MODE_READING) {
                  currentMode = MODE_READING;
                  fontMenuPage = 0;
                  size_t savedOffset = currentPageByteOffset;
                  Serial.printf("Font select: heap=%u psram=%u offset=%u\n",
                                ESP.getFreeHeap(), ESP.getFreePsram(), (unsigned)savedOffset);
                  // Free EPUB text buffer to reduce PSRAM fragmentation before font
                  // loading. The font preview pass cycled through many fonts, each
                  // allocating/freeing large PSRAM blocks. Freeing the EPUB buffer
                  // creates a large contiguous block for loadBinaryFont(). The buffer
                  // will be reloaded by loadCurrentPage() → epubLoadChapterRange().
                  if (currentBookIsEpub && epubFullText) {
                    Serial.printf("Font select: freeing EPUB buffer (%u bytes) to defrag PSRAM\n",
                                  (unsigned)epubFullTextLen);
                    free(epubFullText);
                    epubFullText = nullptr;
                    epubFullTextLen = 0;
                  }
                  yield(); esp_task_wdt_reset();
                  bool fontOK = loadReadingFont();
                  yield(); esp_task_wdt_reset();
                  if (!fontOK) {
                    Serial.println("ERROR: loadReadingFont failed after font selection");
                  }
                  recalculatePages();
                  if (bytesPerPage > 0 && savedOffset > 0) {
                    currentPage = savedOffset / bytesPerPage;
                  }
                  if (currentPage >= totalPages) currentPage = totalPages - 1;
                  if (pageByteOffsets && currentPage > 0 && savedOffset > 0) {
                    while ((int)pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
                      int gap = currentPage - pageOffsetsCount;
                      size_t est = (savedOffset > (size_t)gap * bytesPerPage) ?
                                   savedOffset - (size_t)gap * bytesPerPage : 0;
                      pageByteOffsets[pageOffsetsCount] = est;
                      pageOffsetsCount++;
                    }
                    if (currentPage < (int)pageOffsetsCount)
                      pageByteOffsets[currentPage] = savedOffset;
                  }
                  yield(); esp_task_wdt_reset();
                  if (!loadCurrentPage()) {
                    Serial.println("ERROR: loadCurrentPage failed after font selection");
                  }
                  drawReading();
                } else {
                  loadSystemFont();
                  drawFontMenu();
                }
        }
        // Font name tap is disabled — selection only via BIN/TTF buttons
        } // end if (fontIdx in range)
        } // end else (slot in range)
      } else {
        Serial.printf("FONT_MENU: unhandled touch x=%d, y=%d\n", x, y);
      }
    } 
    else if (currentMode == MODE_DICT_POPUP) {
      // Any tap dismisses the dictionary popup and redraws the reading page
      if (hasTouchEvent) {
        Serial.println("DICT: popup dismissed");
        currentMode = MODE_READING;
        pagesSinceFullRefresh = 0;
        drawReading();
      }
    }
    else if (currentMode == MODE_READING) {
      // Swipe gesture for page turning (both image and text reading)
      if (hasSwipeEvent) {
        // Horizontal LTR: swipe left (deltaX<0) = next, swipe right (deltaX>0) = prev
        // Vertical CJK:   swipe right (deltaX>0) = next, swipe left (deltaX<0) = prev
        bool swipeNext = epubIsHorizontal ? (swipeDeltaX < 0) : (swipeDeltaX > 0);
        bool swipePrev = epubIsHorizontal ? (swipeDeltaX > 0) : (swipeDeltaX < 0);
        if (swipeNext && currentPage < totalPages - 1) {
          sdLog("PAGE: swipe next %d->%d/%d WiFi=%d heap=%u",
                currentPage, currentPage+1, totalPages, (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
          Serial.printf("Swipe next page %d -> %d (totalPages=%d, heap=%u, psram=%u)\n",
                        currentPage, currentPage + 1, totalPages,
                        ESP.getFreeHeap(), ESP.getFreePsram());
          currentPage++;
          if (currentBookIsEpub && epubIsImageBased) comicZoomQuadrant = -1;
          if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
          else { Serial.printf("ERROR: loadCurrentPage failed for page %d\n", currentPage); currentPage--; }
        } else if (swipePrev && currentPage > 0) {
          sdLog("PAGE: swipe prev %d->%d/%d WiFi=%d heap=%u",
                currentPage, currentPage-1, totalPages, (int)WiFi.status(), (unsigned)ESP.getFreeHeap());
          Serial.printf("Swipe prev page %d -> %d\n", currentPage, currentPage - 1);
          currentPage--;
          if (currentBookIsEpub && epubIsImageBased) comicZoomQuadrant = -1;
          if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
          else { Serial.printf("ERROR: loadCurrentPage failed for page %d\n", currentPage); currentPage++; }
        }
      }
      else if (hasTouchEvent) {
      Serial.printf("READING touch: x=%d, y=%d\n", x, y);
      // Image-based EPUB (comic/manga) zoom handling
      if (currentBookIsEpub && epubIsImageBased) {
        if (comicZoomQuadrant >= 0) {
          // Zoomed view: tap anywhere to return to full view
          Serial.println("Comic zoom: tap to return to full view");
          comicZoomQuadrant = -1;
          drawReading();
        }
        // Arrow navigation (works in full view only)
        else if (touchedPrevPage(x, y) && currentPage < totalPages - 1) {
          currentPage++;
          comicZoomQuadrant = -1;
          if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
        }
        else if (touchedNextPage(x, y) && currentPage > 0) {
          currentPage--;
          comicZoomQuadrant = -1;
          if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
        }
        // Comic zoom mode toggle button (x: 355-425)
        else if (x >= 345 && x <= 435 && y >= NAV_Y && y <= NAV_Y + 69) {
          comicZoomMode = (comicZoomMode == 0) ? 1 : 0;
          prefs.begin("ereader", false);
          prefs.putInt("comicZoom", comicZoomMode);
          prefs.end();
          Serial.printf("Comic zoom mode toggled to: %d\n", comicZoomMode);
        }
        // Tap progress bar / page number area to open page jump popup
        else if (y >= 840 && y <= 890) {
          pageJumpInput = "";
          currentMode = MODE_PAGE_JUMP;
          drawPageJumpPopup();
        }
        // Return button (top-middle)
        else if (touchedReadingReturnButton(x, y)) {
          Serial.printf("Return from image reading - Free heap: %u, Free PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
          yield(); esp_task_wdt_reset();
          saveReadingPosition();
          savePrefInt("ereader", "page", currentPage);
          comicZoomQuadrant = -1;
          yield(); esp_task_wdt_reset();
          epubCleanup();
          currentBookIsEpub = false;
          currentPageContent = "";
          currentMode = MODE_BOOK_LIST;
          yield(); esp_task_wdt_reset();
          resetToSystemFont();
          yield(); esp_task_wdt_reset();
          drawBookList();
        }
        // Full view: tap on image area to zoom
        else if (x >= READING_AREA_LEFT && x <= READING_AREA_RIGHT &&
                 y >= READING_AREA_TOP && y <= READING_AREA_BOTTOM) {
          if (comicZoomMode == 1) {
            // Free-point zoom: use tap position as center
            comicZoomCX = (float)(x - READING_AREA_LEFT) / (float)(READING_AREA_RIGHT - READING_AREA_LEFT);
            comicZoomCY = (float)(y - READING_AREA_TOP) / (float)(READING_AREA_BOTTOM - READING_AREA_TOP);
            comicZoomQuadrant = 100;
            Serial.printf("Comic zoom: free-point (%.2f, %.2f)\n", comicZoomCX, comicZoomCY);
          } else {
            // Quadrant zoom
            int midX = (READING_AREA_LEFT + READING_AREA_RIGHT) / 2;
            int midY = (READING_AREA_TOP + READING_AREA_BOTTOM) / 2;
            if (x < midX && y < midY)      comicZoomQuadrant = 0;  // TL
            else if (x >= midX && y < midY) comicZoomQuadrant = 1;  // TR
            else if (x < midX && y >= midY) comicZoomQuadrant = 2;  // BL
            else                             comicZoomQuadrant = 3;  // BR
            Serial.printf("Comic zoom: quadrant %d\n", comicZoomQuadrant);
          }
          drawReading();
        }
      }
      // Non-image EPUB / plain text reading
      else {
      // Return button (top-middle)
      if (touchedReadingReturnButton(x, y)) {
        sdLog("USER: return to booklist from '%s' page=%d/%d WiFi=%d",
              currentBook.c_str(), currentPage, totalPages, (int)WiFi.status());
        Serial.printf("Back button touched - returning to book list. Free heap: %u, Free PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
        yield(); esp_task_wdt_reset();
        saveReadingPosition();
        savePrefInt("ereader", "page", currentPage);
        yield(); esp_task_wdt_reset();
        epubCleanup();
        currentBookIsEpub = false;
        currentPageContent = "";
        currentMode = MODE_BOOK_LIST;
        yield(); esp_task_wdt_reset();
        resetToSystemFont();
        yield(); esp_task_wdt_reset();
        drawBookList();
      }
      // Inline EPUB link tap: check if touch hit a link region
      else if (currentBookIsEpub && inlineLinkCount > 0) {
        bool linkTapped = false;
        for (int li = 0; li < inlineLinkCount; li++) {
          InlineLink& lnk = inlineLinks[li];
          if (x >= lnk.x && x <= lnk.x + lnk.w && y >= lnk.y && y <= lnk.y + lnk.h) {
            Serial.printf("Link tapped: '%s'\n", lnk.href.c_str());
            int targetChapter = resolveEpubHrefToChapter(lnk.href);
            if (targetChapter >= 0 && targetChapter < epubChapterCount) {
              Serial.printf("Link resolved to chapter %d\n", targetChapter);
              if (jumpToEpubChapterOffset(targetChapter, 0, false) && loadCurrentPage()) {
                saveReadingPosition();
                drawReading();
              } else {
                drawReading();
              }
              linkTapped = true;
            } else {
              Serial.printf("Link target not found in chapters: '%s'\n", lnk.href.c_str());
            }
            break;
          }
        }
        if (!linkTapped) goto normalReadingTouch;
      }
      else {
        normalReadingTouch:
      // Vertical CJK: left button = NEXT page (forward in book)
      // Horizontal LTR: left button = PREV page (backward in book)
      if (touchedPrevPage(x, y)) {
        if (epubIsHorizontal) {
          // LTR: left arrow = prev page
          if (currentPage > 0) {
            sdLog("PAGE: btn prev %d->%d/%d WiFi=%d", currentPage, currentPage-1, totalPages, (int)WiFi.status());
            Serial.printf("LEFT arrow - prev page %d -> %d\n", currentPage, currentPage - 1);
            currentPage--;
            if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
            else { currentPage++; }
          }
        } else {
          // CJK: left arrow = next page
          if (currentPage < totalPages - 1) {
            sdLog("PAGE: btn next %d->%d/%d WiFi=%d", currentPage, currentPage+1, totalPages, (int)WiFi.status());
            Serial.printf("LEFT arrow - page %d -> %d (totalPages=%d, freePSRAM=%u)\n",
                          currentPage, currentPage + 1, totalPages, ESP.getFreePsram());
            currentPage++;
            if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
            else { currentPage--; }
          }
        }
      }
      // Vertical CJK: right button = PREV page (backward in book)
      // Horizontal LTR: right button = NEXT page (forward in book)
      else if (touchedNextPage(x, y)) {
        if (epubIsHorizontal) {
          // LTR: right arrow = next page
          if (currentPage < totalPages - 1) {
            Serial.printf("RIGHT arrow - next page %d -> %d\n", currentPage, currentPage + 1);
            currentPage++;
            if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
            else { currentPage--; }
          }
        } else {
          // CJK: right arrow = prev page
          if (currentPage > 0) {
            Serial.printf("RIGHT arrow - page %d -> %d\n", currentPage, currentPage - 1);
            currentPage--;
            if (loadCurrentPage()) { drawReading(); if (currentPage % 5 == 0) saveReadingPosition(); }
            else { currentPage++; }
          }
        }
      }
      // Image page: tap reading area to advance to next page
      // (cover images fill the screen — arrows may be hard to find)
      else if (lastPageWasImage && currentPage < totalPages - 1 &&
               x >= READING_AREA_LEFT && x <= READING_AREA_RIGHT &&
               y >= READING_AREA_TOP && y <= READING_AREA_BOTTOM) {
        Serial.printf("Image page tap - page %d -> %d\n", currentPage, currentPage + 1);
        currentPage++;
        if (loadCurrentPage()) {
          drawReading();
          if (currentPage % 5 == 0) saveReadingPosition();
        } else {
          Serial.printf("ERROR: loadCurrentPage failed for page %d\n", currentPage);
          currentPage--;
        }
      }
      // Font size decrease (−A) — toolbar cell 0
      else if (y > 900 && x >= 150 && x <= 201) {
        if (readingFontSize > MIN_READING_FONT_SIZE) {
          // Snap down to nearest grid-aligned size (handles off-grid values like 46→44)
          int grid = ((readingFontSize - MIN_READING_FONT_SIZE - 1) / FONT_SIZE_STEP) * FONT_SIZE_STEP + MIN_READING_FONT_SIZE;
          readingFontSize = max(grid, MIN_READING_FONT_SIZE);
          // Auto-select best BIN for new size (reload if changed)
          if (selectBestBinForSize(readingFontSize)) {
            loadReadingFont();
          }
          clearGlyphCache();
          if (!(currentBookIsEpub && epubIsImageBased)) {
            size_t savedOffset = currentPageByteOffset;
            recalculatePages();
            if (bytesPerPage > 0 && savedOffset > 0) {
              currentPage = savedOffset / bytesPerPage;
            }
            if (currentPage >= totalPages) currentPage = totalPages - 1;
            if (pageByteOffsets && currentPage > 0 && savedOffset > 0) {
              while ((int)pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
                int gap = currentPage - pageOffsetsCount;
                size_t est = (savedOffset > (size_t)gap * bytesPerPage) ?
                             savedOffset - (size_t)gap * bytesPerPage : 0;
                pageByteOffsets[pageOffsetsCount] = est;
                pageOffsetsCount++;
              }
              if (currentPage < (int)pageOffsetsCount)
                pageByteOffsets[currentPage] = savedOffset;
            }
          }
          savePrefInt("ereader", epubIsHorizontal ? "rdFontSzEn" : "rdFontSz", readingFontSize);
          saveReadingPosition();
          loadCurrentPage();
          pagesSinceFullRefresh = 0;  // Force quality e-ink refresh after layout change
          drawReading();
        }
      }
      // Font size increase (+A) — toolbar cell 2
      else if (y > 900 && x >= 254 && x <= 305) {
        if (readingFontSize < MAX_READING_FONT_SIZE) {
          // Snap up to nearest grid-aligned size (handles off-grid values like 46→48)
          int grid = ((readingFontSize - MIN_READING_FONT_SIZE) / FONT_SIZE_STEP + 1) * FONT_SIZE_STEP + MIN_READING_FONT_SIZE;
          readingFontSize = min(grid, MAX_READING_FONT_SIZE);
          // Auto-select best BIN for new size (reload if changed)
          if (selectBestBinForSize(readingFontSize)) {
            loadReadingFont();
          }
          clearGlyphCache();
          if (!(currentBookIsEpub && epubIsImageBased)) {
            size_t savedOffset = currentPageByteOffset;
            recalculatePages();
            if (bytesPerPage > 0 && savedOffset > 0) {
              currentPage = savedOffset / bytesPerPage;
            }
            if (currentPage >= totalPages) currentPage = totalPages - 1;
            if (pageByteOffsets && currentPage > 0 && savedOffset > 0) {
              while ((int)pageOffsetsCount <= currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
                int gap = currentPage - pageOffsetsCount;
                size_t est = (savedOffset > (size_t)gap * bytesPerPage) ?
                             savedOffset - (size_t)gap * bytesPerPage : 0;
                pageByteOffsets[pageOffsetsCount] = est;
                pageOffsetsCount++;
              }
              if (currentPage < (int)pageOffsetsCount)
                pageByteOffsets[currentPage] = savedOffset;
            }
          }
          savePrefInt("ereader", epubIsHorizontal ? "rdFontSzEn" : "rdFontSz", readingFontSize);
          saveReadingPosition();
          loadCurrentPage();
          pagesSinceFullRefresh = 0;  // Force quality e-ink refresh after layout change
          drawReading();
        }
      }
      // Font selection button (Aa) — toolbar cell 3
      else if (y > 900 && x >= 306 && x <= 357) {
        Serial.printf("Opening font menu: heap=%u psram=%u\n",
                       ESP.getFreeHeap(), ESP.getFreePsram());
        fontMenuReturnMode = MODE_READING;
        fontMenuPage = 0;  // Reset to first page on mode switch
        currentMode = MODE_FONT_MENU;
        // Free PSRAM-heavy buffers before font operations.
        // resetToSystemFont loads a TTF via FreeType which needs contiguous PSRAM.
        // These are all re-created lazily when returning to reading mode.
        if (currentBookIsEpub && epubFullText) {
          Serial.printf("Font menu: freeing epubFullText (%u bytes)\n", (unsigned)epubFullTextLen);
          free(epubFullText);
          epubFullText = nullptr;
          epubFullTextLen = 0;
        }
        freeGlyphCache();   // Free 192KB bitmap pool + 9KB hash table
        yield(); esp_task_wdt_reset();
        Serial.printf("Font menu: before resetToSystemFont heap=%u psram=%u\n",
                       ESP.getFreeHeap(), ESP.getFreePsram());
        resetToSystemFont();
        Serial.printf("Font menu: after resetToSystemFont heap=%u psram=%u\n",
                       ESP.getFreeHeap(), ESP.getFreePsram());
        drawFontMenu();
      }
      // Index / TOC button (≡) — toolbar cell 4
      else if (y > 900 && x >= 358 && x <= 409) {
        if (currentBookIsEpub) {
          Serial.println("Opening TOC...");
          if (!epubTocEntries || epubTocCount == 0) {
            epubParseToc();
            tocPolishLabels();  // Polish embedded TOC labels (strip punct, split couplets)
          }
          // If TOC has only a license entry (e.g., Project Gutenberg), treat as no TOC
          if (epubTocCount == 1 && epubTocEntries) {
            String lbl = epubTocEntries[0].label;
            lbl.toUpperCase();
            if (lbl.indexOf("GUTENBERG") >= 0 || lbl.indexOf("LICENSE") >= 0) {
              Serial.println("TOC: only license entry, generating virtual TOC");
              epubFreeToc();
            }
          }
          if (!epubTocEntries || epubTocCount == 0) {
            // Virtual TOC scans epubFullText — ensure all chapters are loaded from the start
            if (!epubFullText || epubLoadedStartChapter != 0) {
              Serial.println("TOC: loading all chapters from start for virtual TOC scan");
              epubLoadChapterRange(0);
            }
            epubGenerateVirtualToc();
          }
          tocListPage = 0;
          tocTab = (epubTocEntries && epubTocCount > 0) ? 0 : 1;
          currentMode = MODE_TOC;
          if (!epubTocEntries || epubTocCount == 0) {
            Serial.println("TOC: no entries found, showing bookmarks tab");
          }
          drawTocList();
        } else {
          // TXT: generate virtual TOC from chapter patterns
          if (!epubTocEntries || epubTocCount == 0) {
            txtGenerateVirtualToc();
            tocPolishLabels();  // Polish TXT TOC labels (strip punct, split couplets)
          }
          tocListPage = 0;
          tocTab = (epubTocEntries && epubTocCount > 0) ? 0 : 1;
          currentMode = MODE_TOC;
          drawTocList();
        }
      }
      // Bookmark button (★) — toolbar cell 5
      else if (y > 900 && x >= 410 && x <= 461) {
        toggleBookmark();
        Serial.printf("Bookmark toggled: page %d\n", currentPage + 1);
        drawReading();
      }
      // Tap progress bar / page number area to open page jump popup
      else if (y >= 840 && y <= 890) {
        pageJumpInput = "";
        currentMode = MODE_PAGE_JUMP;
        drawPageJumpPopup();
      }
      // Dead zone: lower-right corner (old return button area) — ignore taps
      else if (x >= NAV_RETURN_X && y >= NAV_Y) {
        // No action — prevent accidental page turns in this corner
      }
      else if (x < 270) {
        // Left side tap
        // Horizontal LTR: prev page; Vertical CJK: next page
        if (epubIsHorizontal) {
          if (currentPage > 0) {
            currentPage--;
            if (loadCurrentPage()) { if (currentPage % 5 == 0) saveReadingPosition(); drawReading(); }
          }
        } else {
          if (currentPage < totalPages - 1) {
            currentPage++;
            if (loadCurrentPage()) { if (currentPage % 5 == 0) saveReadingPosition(); drawReading(); }
          }
        }
      } else {
        // Right side tap
        // Horizontal LTR: next page; Vertical CJK: prev page
        if (epubIsHorizontal) {
          if (currentPage < totalPages - 1) {
            currentPage++;
            if (loadCurrentPage()) { if (currentPage % 5 == 0) saveReadingPosition(); drawReading(); }
          }
        } else {
          if (currentPage > 0) {
            currentPage--;
            if (loadCurrentPage()) { if (currentPage % 5 == 0) saveReadingPosition(); drawReading(); }
          }
        }
      }
      } // end normalReadingTouch else block
      } // end non-image else block
      } // end hasTouchEvent
    }
    else if (currentMode == MODE_PAGE_JUMP) {
      handlePageJumpTouch(x, y);
    }
    else if (currentMode == MODE_TOC) {
      // TOC layout (must match drawTocList)
      int tocRowH = epubIsHorizontal ? 46 : 42;

      // Return button → back to reading
      if (touchedReturnButton(x, y)) {
        currentMode = MODE_READING;
        drawReading();
      }
      // Tab bar touch (y 35..85)
      else if (y >= 35 && y <= 85) {
        if (x >= 10 && x < 270 && tocTab != 0) {
          tocTab = 0;
          tocListPage = 0;
          drawTocList();
        } else if (x >= 270 && x <= 530 && tocTab != 1) {
          tocTab = 1;
          drawTocList();
        }
      }
      // Pagination: next page (left arrow, CJK forward)
      else if (touchedPrevPage(x, y)) {
        if (tocTab == 0) {
          tocListPage++;
          drawTocList();  // drawTocList will clamp tocListPage
        }
      }
      // Pagination: prev page (right arrow, CJK backward)
      else if (touchedNextPage(x, y)) {
        if (tocTab == 0) {
          if (tocListPage > 0) {
            tocListPage--;
            drawTocList();
          }
        }
      }
      // Tap on list entries
      else if (y >= 100 && y <= 100 + tocVisualRowCount * tocRowH) {
        int row = (y - 100) / tocRowH;

        if (tocTab == 0) {
          // Chapter list tap — use tocRowToEntry mapping for multi-line titles
          int tocIdx = (row >= 0 && row < tocVisualRowCount) ? tocRowToEntry[row] : -1;
          if (tocIdx >= 0 && tocIdx < epubTocCount) {
            size_t targetOffset = epubTocEntries[tocIdx].byteOffset;
            Serial.printf("TOC: jumping to \"%s\" @ offset %u\n",
                          epubTocEntries[tocIdx].label.c_str(), (unsigned)targetOffset);

            if (currentBookIsEpub) {
              int chapterIdx = epubTocEntries[tocIdx].chapterIndex;
              if (chapterIdx >= 0 && chapterIdx < epubChapterCount) {
                jumpToEpubChapterOffset(chapterIdx, targetOffset, targetOffset != 0);
              }
            } else {
              // TXT file: jump by byte offset
              int estimatedPage = 0;
              if (bytesPerPage > 0) {
                estimatedPage = targetOffset / bytesPerPage;
                if (estimatedPage >= totalPages) estimatedPage = totalPages - 1;
              }
              currentPage = estimatedPage;
              lastRenderedForPage = -1;
              if (pageByteOffsets) {
                // Fill gap entries with backward estimates to avoid uninitialized garbage
                while (pageOffsetsCount < currentPage && pageOffsetsCount < MAX_PAGE_OFFSETS) {
                  int gap = currentPage - pageOffsetsCount;
                  size_t est = (targetOffset > (size_t)gap * bytesPerPage) ?
                               targetOffset - (size_t)gap * bytesPerPage : 0;
                  pageByteOffsets[pageOffsetsCount] = est;
                  pageOffsetsCount++;
                }
                pageByteOffsets[currentPage] = targetOffset;
                if (pageOffsetsCount <= currentPage) {
                  pageOffsetsCount = currentPage + 1;
                }
              }
            }

            currentMode = MODE_READING;
            if (loadCurrentPage()) {
              saveReadingPosition();
              drawReading();
            } else {
              drawReading();
            }
          }
        } else {
          // Bookmark list tap
          if (row >= 0 && row < bookmarkCount) {
            // Check if delete button was tapped (x >= 460)
            if (x >= 460 && x <= 510) {
              Serial.printf("Deleting bookmark at page %d\n", bookmarks[row].page + 1);
              removeBookmark(bookmarks[row].page);
              drawTocList();
            } else {
              // Jump to bookmarked page
              Serial.printf("Jumping to bookmark page %d\n", bookmarks[row].page + 1);
              currentPage = bookmarks[row].page;
              currentMode = MODE_READING;
              if (loadCurrentPage()) {
                saveReadingPosition();
                drawReading();
              } else {
                drawReading();
              }
            }
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
        drawWeather(false);  // Full quality redraw to avoid e-ink ghosting
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
        int itemHeight = 78;
        int itemGap = 8;

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
          // Page 1: 8 items
          
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
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Timezone settings selected");
            setupSubmenu = 2;
            showingTimezone = true;
            tzScrollOffset = 0;
            drawWiFiSetup();
            return;
          }
          
          // USB MSC Settings item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("USB MSC settings selected");
            setupSubmenu = 4;
            drawUSBMSCSetup();
            return;
          }
          
          // Icon Source Settings item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Icon source settings selected");
            setupSubmenu = 5;
            drawIconSetup();
            return;
          }

          // Calendar Calculation Settings item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Calendar settings selected");
            setupSubmenu = 6;
            drawCalendarSetup();
            return;
          }

          // Bluetooth Settings item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Bluetooth settings selected");
            setupSubmenu = 7;
            drawBluetoothSetup();
            return;
          }

          // Auto-Sleep Settings item
          y1 += itemHeight + itemGap;
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

          // Comic Zoom Mode item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Comic zoom mode selected - toggling");
            comicZoomMode = (comicZoomMode == 0) ? 1 : 0;
            prefs.begin("ereader", false);
            prefs.putInt("comicZoom", comicZoomMode);
            prefs.end();
            Serial.printf("Comic zoom mode toggled to: %d\n", comicZoomMode);
            drawSetupMenu();
            return;
          }
        } else if (setupMenuPage == 1) {
          // Page 2: 5 items

          // Web Server Settings item
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Web server settings selected");
            setupSubmenu = 3;
            drawWebServerSetup();
            return;
          }

          // Page Refresh Mode item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Page refresh mode selected - cycling");
            pageRefreshMode = (pageRefreshMode + 1) % 3;
            pagesSinceFullRefresh = 0;
            prefs.begin("ereader", false);
            prefs.putInt("pgRefresh", pageRefreshMode);
            prefs.end();
            Serial.printf("Page refresh mode set to: %d\n", pageRefreshMode);
            drawSetupMenu();
            return;
          }

          // System Font Settings item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("System font settings selected");
            setupSubmenu = 8;
            drawSystemFontSetup();
            return;
          }

          // Paragraph Indent item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("Paragraph indent selected - toggling");
            paragraphIndent = !paragraphIndent;
            prefs.begin("ereader", false);
            prefs.putBool("paraIndent", paragraphIndent);
            prefs.end();
            Serial.printf("Paragraph indent toggled to: %s\n", paragraphIndent ? "YES" : "NO");
            drawSetupMenu();
            return;
          }

          // About item
          y1 += itemHeight + itemGap;
          if (x >= 20 && x <= 520 && y >= y1 && y <= y1 + itemHeight) {
            Serial.println("About page selected");
            setupSubmenu = 9;
            drawAboutPage();
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
          int startY = 320;
          
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
          
          // Connect button (at y=620, height 70)
          if (x >= 20 && x <= 220 && y >= 620 && y <= 690) {
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
          
          // Scan button (y=832, height 44, width 100)
          if (x >= 20 && x <= 120 && y >= 832 && y <= 876) {
            Serial.println("Scan button touched");
            scanWiFiNetworks();
            drawWiFiSetup();
          }
          // Universal return button (lower-right)
          else if (touchedReturnButton(x, y)) {
            Serial.println("Back button touched");
            setupSubmenu = 0;
            drawSetupMenu();
          }
          // Network selection (skip connected WiFi row, match other networks)
          else if (!wifiScanning && networkCount > 0) {
            // Determine list start: connected WiFi takes y=100..167, others start after
            bool hasConnected = (WiFi.status() == WL_CONNECTED);
            String connectedSSID = hasConnected ? WiFi.SSID() : "";
            int networkY = hasConnected && connectedSSID.length() > 0 ? 180 : 100;

            for (int i = 0; i < networkCount; i++) {
              if (networkY > 740) break;
              // Skip the connected network (it's shown as info, not tappable)
              if (hasConnected && scannedNetworks[i].ssid == connectedSSID) continue;

              if (y >= networkY && y <= networkY + 64 && x >= 20 && x <= 520) {
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
              networkY += 68;
            }
          }
        }
      }
      else if (setupSubmenu == 2) {
        // Timezone setup submenu
        if (showingTimezone) {
          int rowH = 62;
          int listTop = 100;
          int listBottom = 830;
          int maxVisible = (listBottom - listTop) / rowH;

          // Save button (保存) at y=832, height 44, width 100
          if (x >= 20 && x <= 120 && y >= 832 && y <= 876) {
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
          // Left arrow (prev page)
          else if (touchedPrevPage(x, y) && tzScrollOffset > 0) {
            tzScrollOffset -= maxVisible;
            if (tzScrollOffset < 0) tzScrollOffset = 0;
            drawWiFiSetup();
            return;
          }
          // Right arrow (next page)
          else if (touchedNextPage(x, y) && tzScrollOffset + maxVisible < timezoneCount) {
            tzScrollOffset += maxVisible;
            drawWiFiSetup();
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
          else if (y >= listTop && y <= listBottom) {
            int tappedIndex = tzScrollOffset + (y - listTop) / rowH;
            if (tappedIndex >= 0 && tappedIndex < timezoneCount) {
              selectedTimezone = tappedIndex;
              Serial.printf("Timezone %d selected: %s\n", tappedIndex, timezones[tappedIndex].name);
              drawWiFiSetup();
              return;
            }
          }
        }
      }
      else if (setupSubmenu == 3) {
        // Web server setup submenu
        int btnY = 400;
        
        // Toggle button (full width)
        if (x >= 20 && x <= 520 && y >= btnY && y <= btnY + 90) {
          Serial.println("Web server toggle button touched");
          webServerEnabled = !webServerEnabled;
          
          // Save preference
          savePrefBool("m5paper", "webServer", webServerEnabled);
          
          if (webServerEnabled) {
            Serial.println("Web server enabled");
            startWebServer();
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
          // Ignore if no SD card
          if (!sdCardAvailable && !usbMSCActive) {
            Serial.println("USB MSC toggle ignored: no SD card");
            return;
          }
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
            
            // Stop web server and WiFi before starting MSC (SD card won't be accessible)
            if (webServerRunning) {
              stopWebServer();
            }
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            Serial.println("WiFi stopped for USB MSC");
            
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
        int btnY = 400;
        
        // Toggle button (full width)
        if (x >= 20 && x <= 520 && y >= btnY && y <= btnY + 90) {
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
        int btnY = 400;
        
        // Toggle button (full width)
        if (x >= 20 && x <= 520 && y >= btnY && y <= btnY + 90) {
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
            drawSystemText("藍牙", 20, 42, 40);
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
      else if (setupSubmenu == 8) {
        // System font setup submenu
        int btnY = 400;
        
        // Toggle button (full width)
        if (x >= 20 && x <= 520 && y >= btnY && y <= btnY + 90) {
          Serial.println("System font toggle button touched");
          systemFontChoice = (systemFontChoice == 0) ? 1 : 0;
          savePrefInt("m5paper", "sysFont", systemFontChoice);
          Serial.printf("System font set to: %d (%s)\n", systemFontChoice,
                        systemFontChoice == 0 ? "GenYoMinTW" : "Silver");
          
          // Show restart message
          M5.Display.setEpdMode(epd_mode_t::epd_quality);
          M5.Display.startWrite();
          M5.Display.fillScreen(TFT_WHITE);
          drawSystemTextCentered("重新啟動中...", DISPLAY_WIDTH / 2, 400, 32);
          M5.Display.setFont(&fonts::Font2);
          M5.Display.setTextSize(1);
          M5.Display.setTextColor(TFT_BLACK);
          M5.Display.setTextDatum(MC_DATUM);
          M5.Display.drawString("Restarting to apply font change...", DISPLAY_WIDTH / 2, 450);
          M5.Display.setTextDatum(TL_DATUM);
          M5.Display.endWrite();
          M5.Display.display();
          
          delay(2000);
          ESP.restart();
          return;
        }
        
        // Return button (lower-right)
        if (touchedReturnButton(x, y)) {
          Serial.println("System font setup back button touched");
          setupSubmenu = 0;
          drawSetupMenu();
          return;
        }
      }
      else if (setupSubmenu == 9) {
        // About page - just return button
        if (touchedReturnButton(x, y)) {
          Serial.println("About page back button touched");
          setupSubmenu = 0;
          drawSetupMenu();
          return;
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
    else if (currentMode == MODE_TOOLS_MENU) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Tools menu: return to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
      // Option 1: 壁紙 (y=200..310)
      else if (x >= 40 && x <= 500 && y >= 200 && y <= 310) {
        Serial.println("Tools: opening wallpaper list");
        currentMode = MODE_WALLPAPER_LIST;
        drawWallpaperList();
      }
      // Option 2: 吃藥提醒器 (y=330..440)
      else if (x >= 40 && x <= 500 && y >= 330 && y <= 440) {
        Serial.println("Tools: opening medication reminder");
        currentMode = MODE_MED_REMINDER;
        drawMedReminder();
      }
      // Option 3: 醒世格言 (y=460..570)
      else if (x >= 40 && x <= 500 && y >= 460 && y <= 570) {
        Serial.println("Tools: opening motto screen");
        currentMode = MODE_MOTTO_TEST;
        if (sdCardAvailable) {
          drawMottoScreen();
        } else {
          drawMottoBuiltinPage();
        }
      }
      // Option 4: 檔案管理 (y=590..700)
      else if (x >= 40 && x <= 500 && y >= 590 && y <= 700) {
        Serial.println("Tools: opening file manager");
        if (sdCardAvailable) {
          fmPath = "/";
          loadFileManagerDir();
          currentMode = MODE_FILE_MANAGER;
          drawFileManager();
        }
      }
    }
    else if (currentMode == MODE_FILE_MANAGER) {
      if (touchedReturnButton(x, y)) {
        // Go up one directory, or back to tools menu if at root
        if (fmPath == "/") {
          Serial.println("FM: return to tools menu");
          currentMode = MODE_TOOLS_MENU;
          drawToolsMenu();
        } else {
          // Go to parent directory
          int lastSlash = fmPath.lastIndexOf('/');
          if (lastSlash <= 0) {
            fmPath = "/";
          } else {
            fmPath = fmPath.substring(0, lastSlash);
          }
          Serial.printf("FM: navigate up to '%s'\n", fmPath.c_str());
          loadFileManagerDir();
          drawFileManager();
        }
      }
      // Next page (← arrow)
      else if (touchedPrevPage(x, y)) {
        int maxVisible = 10;
        int endIdx = fmScrollOffset + maxVisible;
        if (endIdx < fmCount) {
          fmScrollOffset += maxVisible;
          drawFileManager();
        }
      }
      // Prev page (→ arrow)
      else if (touchedNextPage(x, y)) {
        if (fmScrollOffset > 0) {
          int maxVisible = 10;
          fmScrollOffset -= maxVisible;
          if (fmScrollOffset < 0) fmScrollOffset = 0;
          drawFileManager();
        }
      }
      // Item touch (y=92..875)
      else if (y >= 92 && y <= 875 && x >= 20 && x <= 520) {
        int itemHeight = 73;  // 68 + 5 gap
        int idx = fmScrollOffset + (y - 92) / itemHeight;
        if (idx >= 0 && idx < fmCount) {
          if (fmIsDir[idx]) {
            // Navigate into directory
            if (fmPath == "/") {
              fmPath = "/" + fmEntries[idx];
            } else {
              fmPath = fmPath + "/" + fmEntries[idx];
            }
            Serial.printf("FM: enter dir '%s'\n", fmPath.c_str());
            loadFileManagerDir();
            drawFileManager();
          }
          // Files: just highlight (no action for now)
        }
      }
    }
    else if (currentMode == MODE_MED_REMINDER) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Med reminder: return to tools menu");
        currentMode = MODE_TOOLS_MENU;
        drawToolsMenu();
      }
      // Manual reset button (y=640..690) — only when taken
      else if (medReminderPressTime != 0 && x >= 170 && x <= 370 && y >= 640 && y <= 690) {
        medPasscodeInput = "";
        medPasscodeFirst = "";
        if (medPasscode.length() == 0) {
          // No passcode set — prompt to create one
          Serial.println("Med reminder: no passcode set, prompting to create");
          medSettingNewPasscode = true;
          currentMode = MODE_MED_PASSCODE;
          drawMedPasscode();
        } else {
          Serial.println("Med reminder: opening passcode for reset");
          medSettingNewPasscode = false;
          currentMode = MODE_MED_PASSCODE;
          drawMedPasscode();
        }
      }
      // Big button area (y=280..580) — only marks as taken
      else if (x >= 70 && x <= 470 && y >= 280 && y <= 580) {
        if (medReminderPressTime == 0) {
          Serial.println("Med reminder: marked as taken");
          medReminderPressTime = time(NULL);
          if (medReminderPressTime == 0) medReminderPressTime = 1;  // avoid 0
          prefs.begin("m5paper", false);
          prefs.putLong("medTime", (long)medReminderPressTime);
          prefs.end();
        } else {
          Serial.println("Med reminder: already taken, ignoring big button tap");
          return;  // ignore tap on big button when already taken
        }
        drawMedReminder();
      }
    }
    else if (currentMode == MODE_MED_PASSCODE) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Passcode: cancel, return to med reminder");
        currentMode = MODE_MED_REMINDER;
        drawMedReminder();
      }
      // Numeric keypad: 3x4 grid + backspace + confirm
      else {
        int kx0 = 70, ky0 = 350, kw = 120, kh = 100, gap = 10;
        int col = (x - kx0) / (kw + gap);
        int row = (y - ky0) / (kh + gap);
        if (col >= 0 && col < 3 && row >= 0 && row < 4 &&
            x >= kx0 && x <= kx0 + 3 * (kw + gap) - gap &&
            y >= ky0 && y <= ky0 + 4 * (kh + gap) - gap) {
          int kxStart = kx0 + col * (kw + gap);
          int kyStart = ky0 + row * (kh + gap);
          if (x >= kxStart && x <= kxStart + kw && y >= kyStart && y <= kyStart + kh) {
            if (row == 3 && col == 0) {
              // Backspace
              if (medPasscodeInput.length() > 0) {
                medPasscodeInput.remove(medPasscodeInput.length() - 1);
              }
              updateMedPasscodeInput();
            } else if (row == 3 && col == 2) {
              // Confirm
              if (medSettingNewPasscode) {
                if (medPasscodeInput.length() < 1) return;  // need at least 1 digit
                if (medPasscodeFirst.length() == 0) {
                  // First entry — save and ask to confirm
                  medPasscodeFirst = medPasscodeInput;
                  medPasscodeInput = "";
                  drawMedPasscode();  // full redraw to show "confirm" title
                } else {
                  // Second entry — check match
                  if (medPasscodeInput == medPasscodeFirst) {
                    Serial.println("New passcode set successfully");
                    medPasscode = medPasscodeFirst;
                    saveMedPasscode();
                    // Now reset the med reminder
                    medReminderPressTime = 0;
                    prefs.begin("m5paper", false);
                    prefs.putLong("medTime", 0);
                    prefs.end();
                    currentMode = MODE_MED_REMINDER;
                    drawMedReminder();
                  } else {
                    Serial.println("Passcode mismatch, try again");
                    medPasscodeFirst = "";
                    medPasscodeInput = "";
                    drawMedPasscode();  // full redraw back to "set new" title
                  }
                }
              } else {
                // Verify mode
                if (medPasscodeInput == medPasscode) {
                  Serial.println("Passcode correct: resetting med reminder");
                  medReminderPressTime = 0;
                  prefs.begin("m5paper", false);
                  prefs.putLong("medTime", 0);
                  prefs.end();
                  currentMode = MODE_MED_REMINDER;
                  drawMedReminder();
                } else {
                  Serial.println("Passcode incorrect");
                  medPasscodeInput = "";
                  updateMedPasscodeInput();
                }
              }
            } else {
              // Digit key
              int digit;
              if (row == 3 && col == 1) digit = 0;
              else digit = row * 3 + col + 1;
              if (medPasscodeInput.length() < 8) {
                medPasscodeInput += String(digit);
              }
              updateMedPasscodeInput();
            }
          }
        }
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
          maxVisible = 9;
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
            maxVisible = 9;
          }
          Serial.println("Wallpaper list: prev page (scroll up)");
          wallpaperScrollOffset -= maxVisible;
          if (wallpaperScrollOffset < 0) wallpaperScrollOffset = 0;
          drawWallpaperList();
        }
      }
      // Universal nav: return button
      else if (touchedReturnButton(x, y)) {
        Serial.println("Wallpaper list back button touched - returning to tools menu");
        currentMode = MODE_TOOLS_MENU;
        drawToolsMenu();
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
      else if (y >= 80 && y <= 875) {
        if (wallpaperViewMode == 0) {
          // Name list view
          int itemHeight = 65;
          if (wallpaperCount > 0) {
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
          }
        } else {
          // Thumbnail view
          int cols = 3, rows = 3, thumbPad = 8;
          int thumbW = (DISPLAY_WIDTH - thumbPad * (cols + 1)) / cols;
          int thumbH = (875 - 80 - thumbPad * (rows + 1)) / rows;
          int maxVisible = cols * rows;  // 9
          int startIdx = wallpaperScrollOffset;
          int endIdx = min(startIdx + maxVisible, wallpaperCount);
          
          for (int i = startIdx; i < endIdx; i++) {
            int slot = i - startIdx;
            int col = slot % cols;
            int row = slot / cols;
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
        Serial.println("Motto test: return to tools menu");
        currentMode = MODE_TOOLS_MENU;
        drawToolsMenu();
      } else if (!sdCardAvailable) {
        // No SD: paginated builtin motto list
        if (x < DISPLAY_WIDTH / 2) {
          mottoBuiltinPrev();
        } else {
          mottoBuiltinNext();
        }
      } else {
        // SD available: show next random motto on wallpaper
        Serial.println("Motto test: showing next motto...");
        drawMottoScreen();
      }
    }
    else if (currentMode == MODE_FORTUNE_SLIPS) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Fortune slips: return to dashboard");
        currentMode = MODE_DASHBOARD;
        drawDashboard();
      }
      // Option 1: 觀音靈籖 (y=180..280)
      else if (x >= 40 && x <= 500 && y >= 180 && y <= 280) {
        Serial.println("Fortune slips: kuanyin selected");
        fortuneSlipCategory = 0;
        currentMode = MODE_FORTUNE_SHAKE;
        drawFortuneShakeScreen();
      }
      // Option 2: 淺草寺靈籖 (y=320..420)
      else if (x >= 40 && x <= 500 && y >= 320 && y <= 420) {
        Serial.println("Fortune slips: senso-ji selected");
        fortuneSlipCategory = 1;
        currentMode = MODE_FORTUNE_SHAKE;
        drawFortuneShakeScreen();
      }
      // Option 3: 歌子靈籖 (y=460..560) — easter egg, launches tamagotchi
      else if (x >= 40 && x <= 500 && y >= 460 && y <= 560) {
        Serial.println("Fortune slips: tamagotchi easter egg!");
        currentMode = MODE_TAMAGOTCHI;
        drawTamagotchi();
      }
    }
    else if (currentMode == MODE_FORTUNE_SHAKE) {
      // Tap anywhere to return to menu
      Serial.println("Fortune shake: return to menu");
      currentMode = MODE_FORTUNE_SLIPS;
      drawFortuneSlipsMenu();
    }
    else if (currentMode == MODE_TAMAGOTCHI) {
      if (touchedReturnButton(x, y)) {
        Serial.println("Tamagotchi: return to fortune slips menu");
        tamagotchiExit();
        currentMode = MODE_FORTUNE_SLIPS;
        drawFortuneSlipsMenu();
      } else {
        tamagotchiHandleTap(x, y);
      }
    }
    else if (currentMode == MODE_FORTUNE_SLIP_VIEW) {
      if (touchedReturnButton(x, y)) {
        // Lower-right corner: return to fortune slips menu
        Serial.println("Fortune slip: return to menu");
        currentMode = MODE_FORTUNE_SLIPS;
        drawFortuneSlipsMenu();
      } else {
        // Tap anywhere else → show wording page
        Serial.println("Fortune slip: showing wording page");
        currentMode = MODE_FORTUNE_SLIP_WORDING;
        sensoji_wording_page = 0;
        drawFortuneSlipWording();
      }
    }
    else if (currentMode == MODE_FORTUNE_SLIP_WORDING) {
      if (touchedReturnButton(x, y)) {
        // Return to fortune slips menu
        Serial.println("Fortune wording: return to menu");
        sensoji_wording_page = 0;
        currentMode = MODE_FORTUNE_SLIPS;
        drawFortuneSlipsMenu();
      } else if (touchedPrevPage(x, y)) {
        // Left arrow: next page / go to story
        if (fortuneSlipCategory == 1 && sensoji_hasMore) {
          sensoji_wording_page++;
          Serial.printf("Fortune wording: page %d\n", sensoji_wording_page + 1);
          drawFortuneSlipWording();
        } else if (fortuneSlipCategory == 0) {
          Serial.println("Fortune wording: showing story page");
          kuanyin_story_page = 0;
          currentMode = MODE_FORTUNE_SLIP_STORY;
          drawFortuneSlipStory();
        }
      } else if (touchedNextPage(x, y)) {
        // Right arrow: previous page
        if (fortuneSlipCategory == 1 && sensoji_wording_page > 0) {
          sensoji_wording_page--;
          Serial.printf("Fortune wording: page %d\n", sensoji_wording_page + 1);
          drawFortuneSlipWording();
        }
      } else if (fortuneSlipCategory == 0) {
        // Kuanyin: tap anywhere else → show story
        Serial.println("Fortune wording: showing story page");
        kuanyin_story_page = 0;
        currentMode = MODE_FORTUNE_SLIP_STORY;
        drawFortuneSlipStory();
      } else if (fortuneSlipCategory == 1 && sensoji_hasMore) {
        // Sensoji: tap → next page
        sensoji_wording_page++;
        Serial.printf("Fortune wording: page %d\n", sensoji_wording_page + 1);
        drawFortuneSlipWording();
      } else {
        // Sensoji last page: tap → return to menu
        Serial.println("Fortune wording: return to menu");
        sensoji_wording_page = 0;
        currentMode = MODE_FORTUNE_SLIPS;
        drawFortuneSlipsMenu();
      }
    }
    else if (currentMode == MODE_FORTUNE_SLIP_STORY) {
      if (touchedReturnButton(x, y)) {
        // Return to fortune slips menu
        Serial.println("Fortune story: return to menu");
        kuanyin_story_page = 0;
        currentMode = MODE_FORTUNE_SLIPS;
        drawFortuneSlipsMenu();
      } else if (touchedPrevPage(x, y)) {
        // Left arrow: next page (if more text)
        if (kuanyin_story_hasMore) {
          kuanyin_story_page++;
          Serial.printf("Fortune story: page %d\n", kuanyin_story_page + 1);
          drawFortuneSlipStory();
        }
      } else if (touchedNextPage(x, y)) {
        // Right arrow: previous page or back to wording
        if (kuanyin_story_page > 0) {
          kuanyin_story_page--;
          Serial.printf("Fortune story: page %d\n", kuanyin_story_page + 1);
          drawFortuneSlipStory();
        } else {
          // Page 0 right arrow → back to explanation
          Serial.println("Fortune story: back to wording");
          currentMode = MODE_FORTUNE_SLIP_WORDING;
          drawFortuneSlipWording();
        }
      } else {
        // Tap anywhere: next page or return to menu
        if (kuanyin_story_hasMore) {
          kuanyin_story_page++;
          Serial.printf("Fortune story: page %d\n", kuanyin_story_page + 1);
          drawFortuneSlipStory();
        } else {
          Serial.println("Fortune story: return to menu");
          kuanyin_story_page = 0;
          currentMode = MODE_FORTUNE_SLIPS;
          drawFortuneSlipsMenu();
        }
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
      // Pagination: prev page (right arrow)
      else if (touchedNextPage(x, y) && bookListPage > 0) {
        bookListPage--;
        drawBookList();
      }
      // Pagination: next page (left arrow)
      else if (touchedPrevPage(x, y)) {
        int perPage = (bookViewMode == 0) ? BOOKS_PER_PAGE : 12;
        int totalBookPages = (bookCount + perPage - 1) / perPage;
        if (bookListPage < totalBookPages - 1) {
          bookListPage++;
          drawBookList();
        }
      }
      // "最後閱讀" button in header area
      else if (x >= 380 && x <= 520 && y >= 30 && y <= 82) {
        String lastBook = loadPrefStr("ereader", "lastBook", "");
        if (lastBook.length() > 0 && sdCardAvailable && bookCount > 0) {
          int bookIndex = -1;
          for (int i = 0; i < bookCount; i++) {
            if (bookList[i] == lastBook) {
              bookIndex = i;
              break;
            }
          }
          if (bookIndex >= 0) {
            Serial.printf("Opening last book: %s (index %d)\n", lastBook.c_str(), bookIndex);
            sdLog("USER: open last-read '%s' idx=%d", lastBook.c_str(), bookIndex);
            if (openBookFromList(bookIndex)) {
              swipeTouchStartX = -1;
            }
          } else {
            Serial.printf("Last book '%s' not found in current book list\n", lastBook.c_str());
          }
        }
      }
      // Touch book to open (or 简/正 buttons)
      else if (y >= 92 && y <= 835) {
        Serial.printf("Touch on book list: x=%d y=%d mode=%d\n", x, y, bookViewMode);
        if (sdCardAvailable && bookCount > 0) {
          int bookIndex = -1;
          int convMode = CONV_ORIGINAL;  // default: open original
          if (bookViewMode == 0) {
            // List view
            if (y >= 120) {
              int row = (y - 120) / BOOK_ROW_HEIGHT;
              bookIndex = bookListPage * BOOKS_PER_PAGE + row;
              // Check if 简 or 正 button was tapped (only for CAT_CN books)
              if (bookIndex >= 0 && bookIndex < bookCount && bookCategory[bookIndex] == CAT_CN) {
                int btnY = 120 + (row * BOOK_ROW_HEIGHT) - 2;
                int btnH = 36;
                if (y >= btnY && y <= btnY + btnH) {
                  if (x >= 421 && x <= 471) {
                    convMode = CONV_SIMPLIFIED;
                    Serial.printf("  -> 简 (simplified) for book %d\n", bookIndex);
                  } else if (x >= 477 && x <= 527) {
                    convMode = CONV_TRADITIONAL;
                    Serial.printf("  -> 正 (traditional) for book %d\n", bookIndex);
                  }
                }
              }
            }
          } else {
            // Grid view: 3 columns × 4 rows
            int cols = 3, gridRows = 4, gridPad = 8;
            int contentTop = 92;
            int contentBot = 835;
            int cellW = (DISPLAY_WIDTH - gridPad * (cols + 1)) / cols;
            int cellH = (contentBot - contentTop - gridPad * (gridRows + 1)) / gridRows;
            int perPage = cols * gridRows;

            for (int slot = 0; slot < perPage; slot++) {
              int col = slot % cols;
              int row = slot / cols;
              int cx = gridPad + col * (cellW + gridPad);
              int cy = contentTop + gridPad + row * (cellH + gridPad);
              if (x >= cx && x <= cx + cellW && y >= cy && y <= cy + cellH) {
                bookIndex = bookListPage * perPage + slot;
                // Check 简/正 buttons at bottom of card (for CAT_CN books)
                if (bookIndex >= 0 && bookIndex < bookCount && bookCategory[bookIndex] == CAT_CN) {
                  int btnW = 44, btnH2 = 30;
                  int btnY2 = cy + cellH - btnH2 - 4;
                  if (y >= btnY2 && y <= btnY2 + btnH2) {
                    int jX = cx + cellW / 2 - btnW - 4;
                    int zX = cx + cellW / 2 + 4;
                    if (x >= jX && x <= jX + btnW) {
                      convMode = CONV_SIMPLIFIED;
                      Serial.printf("  -> 简 grid for book %d\n", bookIndex);
                    } else if (x >= zX && x <= zX + btnW) {
                      convMode = CONV_TRADITIONAL;
                      Serial.printf("  -> 正 grid for book %d\n", bookIndex);
                    }
                  }
                }
                break;
              }
            }
          }
          if (bookIndex >= 0 && bookIndex < bookCount) {
            bookConvMode = convMode;
            sdLog("USER: tap book idx=%d '%s' conv=%d", bookIndex, bookList[bookIndex].c_str(), convMode);
            if (openBookFromList(bookIndex)) {
              swipeTouchStartX = -1;
            }
          }  // end if (bookIndex valid)
        } else {
          // No SD card, show sample text with instructions
          Serial.println("No SD - showing sample text");
          currentPageContent = "這是示例文本。\n\n這裡是第一段內容，用於展示直式排版效果。文字從右到左，從上到下排列。\n\n中文傳統排版方式，讓閱讀更加自然流暢。您可以觸控左右區域翻頁。\n\n請在SD卡的/books/目錄下:\n1. 將書籍重命名為book1.txt, book2.txt等ASCII文件名\n2. 創建books.txt清單文件\n3. 格式：book1.txt|三國演義\n\n觸控左側上一頁，右側下一頁。";
          totalPages = 1;
          currentPage = 0;
          currentMode = MODE_READING;
          drawReading();
          swipeTouchStartX = -1;  // Consume pending touch
        }
      }
    }
  }
  
  delay(50);
}
