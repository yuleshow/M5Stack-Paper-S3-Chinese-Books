#include "globals.h"

// ==================== Global Variable Definitions ====================

// Web server
bool webServerEnabled = false;
bool webServerRunning = false;

// Core / System
USBMSC msc;
sdmmc_card_t* sdCard = nullptr;
Preferences prefs;
String currentBook = "";
unsigned long lastActivityTime = 0;
const unsigned long IDLE_SLEEP_TIMEOUT = 10 * 60 * 1000;  // 10 minutes
bool autoSleepEnabled = false;  // Start with auto-sleep disabled (user can enable in settings)

// Pending nav touch (for mid-render touch detection)
bool pendingNavTouch = false;
int pendingTouchX = 0;
int pendingTouchY = 0;
unsigned long lastTouchProcessedTime = 0;  // Debounce: time of last processed touch
bool usbMSCEnabled = false;
bool useSDCardIcons = false;
bool usbMSCActive = false;
int setupFastRefreshCount = 0;
int todoFastRefreshCount = 0;
bool useSxwnlCalendar = false;  // Default: Meeus (our way)
bool bluetoothEnabled = false;
bool bluetoothActive = false;
BLEServer* pBLEServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;
BLEScannedDevice bleDevices[MAX_BLE_DEVICES];
int bleDeviceCount = 0;
bool bleScanning = false;
bool bleShowingScan = false;
int bleSelectedDevice = -1;
bool bleConnectedToDevice = false;
String bleConnectedName = "";

// BLE UUIDs (Nordic UART Service)
#define BLE_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_TX   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_RX   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class BLEServerCallbackHandler : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("BLE client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    Serial.println("BLE client disconnected");
    // Restart advertising
    if (bluetoothActive) {
      pServer->getAdvertising()->start();
    }
  }
};

class BLERxCallbackHandler : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.printf("BLE RX: %s\n", rxValue.c_str());
    }
  }
};

void startBLE() {
  if (bluetoothActive) return;
  
  BLEDevice::init("M5Paper-BLE");
  pBLEServer = BLEDevice::createServer();
  static BLEServerCallbackHandler serverCb;
  pBLEServer->setCallbacks(&serverCb);
  
  BLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);
  
  pTxCharacteristic = pService->createCharacteristic(
    BLE_CHARACTERISTIC_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  pRxCharacteristic = pService->createCharacteristic(
    BLE_CHARACTERISTIC_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  static BLERxCallbackHandler rxCb;
  pRxCharacteristic->setCallbacks(&rxCb);
  
  pService->start();
  pBLEServer->getAdvertising()->start();
  bluetoothActive = true;
  Serial.println("BLE started as M5Paper-BLE");
}

void stopBLE() {
  if (!bluetoothActive) return;
  
  BLEDevice::deinit(true);
  pBLEServer = nullptr;
  pTxCharacteristic = nullptr;
  pRxCharacteristic = nullptr;
  bluetoothActive = false;
  bleConnectedToDevice = false;
  bleConnectedName = "";
  Serial.println("BLE stopped");
}

void scanBLEDevices() {
  Serial.println("Starting BLE scan...");
  bleScanning = true;
  bleDeviceCount = 0;
  
  // If BLE server is running, we need to init differently
  // BLEDevice must be initialized
  if (!bluetoothActive) {
    BLEDevice::init("M5Paper-BLE");
  }
  
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  BLEScanResults foundDevices = pBLEScan->start(2, false);  // 2 second scan (shorter to reduce UI blocking)
  
  int count = foundDevices.getCount();
  Serial.printf("BLE scan found %d devices\n", count);
  
  bleDeviceCount = 0;
  for (int i = 0; i < count && bleDeviceCount < MAX_BLE_DEVICES; i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    String name = device.haveName() ? String(device.getName().c_str()) : "";
    
    // Only show devices with names (skip anonymous ones)
    if (name.length() > 0) {
      bleDevices[bleDeviceCount].name = name;
      bleDevices[bleDeviceCount].address = String(device.getAddress().toString().c_str());
      bleDevices[bleDeviceCount].rssi = device.getRSSI();
      bleDevices[bleDeviceCount].connectable = true;
      bleDeviceCount++;
      Serial.printf("  [%d] %s (%s) RSSI:%d\n", bleDeviceCount, name.c_str(), device.getAddress().toString().c_str(), device.getRSSI());
    }
  }
  
  pBLEScan->clearResults();
  
  // If we weren't running BLE server before, deinit
  if (!bluetoothActive) {
    BLEDevice::deinit(false);
  }
  
  bleScanning = false;
  Serial.printf("BLE scan complete: %d named devices\n", bleDeviceCount);
}

// BLE Client callbacks
static BLEClient* pBLEClient = nullptr;

class BLEClientCallbackHandler : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    Serial.println("BLE client: connected to remote device");
  }
  void onDisconnect(BLEClient* pClient) {
    Serial.println("BLE client: disconnected from remote device");
    bleConnectedToDevice = false;
    bleConnectedName = "";
  }
};

void connectBLEDevice(int index) {
  if (index < 0 || index >= bleDeviceCount) return;
  
  Serial.printf("Connecting to BLE device: %s (%s)\n", 
    bleDevices[index].name.c_str(), 
    bleDevices[index].address.c_str());
  
  // Make sure BLE is initialized
  if (!bluetoothActive) {
    BLEDevice::init("M5Paper-BLE");
    bluetoothActive = true;
  }
  
  // Disconnect existing client if any
  if (pBLEClient != nullptr) {
    if (pBLEClient->isConnected()) {
      pBLEClient->disconnect();
    }
    delete pBLEClient;
    pBLEClient = nullptr;
  }
  
  pBLEClient = BLEDevice::createClient();
  static BLEClientCallbackHandler clientCb;
  pBLEClient->setClientCallbacks(&clientCb);
  
  BLEAddress addr(bleDevices[index].address.c_str());
  
  if (pBLEClient->connect(addr)) {
    bleConnectedToDevice = true;
    bleConnectedName = bleDevices[index].name;
    Serial.printf("Connected to %s\n", bleConnectedName.c_str());
  } else {
    Serial.println("BLE connection failed");
    bleConnectedToDevice = false;
    bleConnectedName = "";
    delete pBLEClient;
    pBLEClient = nullptr;
  }
}

// Font selection
int numFonts = 4;
int selectedFontIndex = 0;
int fontMenuPage = 0;
const int FONTS_PER_PAGE = 7;  // (790-90)/100 = 7 items, last ends at y=790
String fontFileList[MAX_FONT_FILES];
String fontDisplayNames[MAX_FONT_FILES];
String fontBinFiles[MAX_FONT_FILES][MAX_BIN_PER_FONT];
uint8_t fontBinSizes[MAX_FONT_FILES][MAX_BIN_PER_FONT];
int fontBinCount[MAX_FONT_FILES];
bool fontIsCJK[MAX_FONT_FILES];
String fontStyleFiles[MAX_FONT_FILES][4];
int fontFileCount = 0;
int fontMenuFilteredMap[MAX_FONT_FILES];
int fontMenuFilteredCount = 0;

// Mode
Mode currentMode = MODE_WELCOME;

// Calendar
int calendarDayOffset = 0;
int pickerYear = 0, pickerMonth = 0;
int pickerSelectedYear = 0, pickerSelectedMonth = 0, pickerSelectedDay = 0;
int ymPickerYear = 0;
int ymPickerMonth = 0;

// Dashboard
Icon g_icons[kIconCount];
const char* kIconLabels[kIconCount] = {
  "電子書",
  "日曆",
  "待辦事項",
  "採辦",
  "天氣",
  "工具",
  "設定",
  "求籖"
};
Mode fontMenuReturnMode = MODE_DASHBOARD;

// File Manager
String fmPath = "/";
String fmEntries[FM_MAX_ENTRIES];
bool   fmIsDir[FM_MAX_ENTRIES];
size_t fmSizes[FM_MAX_ENTRIES];
int    fmCount = 0;
int    fmScrollOffset = 0;

// SD / Book
bool sdCardAvailable = false;
bool sdCardChecked = false;
String bookList[MAX_BOOKS];
String bookDisplayName[MAX_BOOKS];
BookCategory bookCategory[MAX_BOOKS];
int bookCount = 0;
int bookListPage = 0;
int bookViewMode = 0;  // 0=list, 1=grid
int bookConvMode = 0;   // 0=original, 1=simplified, 2=traditional
String currentBookPath = "";
String currentPageContent = "";
int currentPage = 0;
int totalPages = 1;
size_t totalBookBytes = 0;
int readingFontSize = DEFAULT_READING_FONT_SIZE;
int bytesPerPage = 750;
size_t* pageByteOffsets = nullptr;
int pageOffsetsCount = 0;
size_t currentPageByteOffset = 0;  // Actual byte offset used for current page
size_t lastRenderedNextOffset = 0;
int lastRenderedForPage = -1;
Bookmark bookmarks[5];
int bookmarkCount = 0;
String pageJumpInput = "";

// EPUB
bool currentBookIsEpub = false;
bool lastPageWasImage = false;
char* epubFullText = nullptr;
size_t epubFullTextLen = 0;

// EPUB persistent ZIP info (for on-demand image/chapter extraction)
String epubFilePath = "";
ZipEntry* epubZipEntries = nullptr;
int epubZipEntryCount = 0;

// EPUB chapter windowing
EpubChapterInfo* epubChapters = nullptr;
int epubChapterCount = 0;
int epubLoadedStartChapter = 0;
int epubLoadedEndChapter = 0;
size_t epubLoadedBaseOffset = 0;
String epubBasePath = "";
size_t epubEstimatedTotalBytes = 0;
bool epubIsImageBased = false;
bool epubHasMultiImageChapters = false;
bool epubIsHorizontal = false;
TocEntry* epubTocEntries = nullptr;
int epubTocCount = 0;
int tocListPage = 0;
int tocTab = 0;
int tocRowToEntry[MAX_TOC_VISUAL_ROWS] = {};
int tocVisualRowCount = 0;
InlineLink inlineLinks[MAX_INLINE_LINKS];
int inlineLinkCount = 0;
int comicZoomQuadrant = -1;
int comicZoomMode = 0;
float comicZoomCX = 0.5f;
float comicZoomCY = 0.5f;
int pageRefreshMode = 0;       // 0=system default (fast+quality every 10 pages), 1=every page quality, 2=fastest only
bool paragraphIndent = false;  // true=首行縮進(2 chars), false=首行不縮進
int pagesSinceFullRefresh = 0; // Counter for mode 2

// Todo
TodoItem todoList[MAX_TODO];
int todoCount = 0;
int currentTodoPage = 0;
int totalTodoPages = 1;
int todoPageStarts[MAX_TODO_PAGES];
int lastRenderedTodoItem = -1;
int selectedTodoItem = -1;
bool showCalendar = false;
CheckboxPos todoCheckboxes[50];
int todoCheckboxCount = 0;
TodoDateZone todoDateZones[50];
int todoDateZoneCount = 0;
int todoDatePickerItem = -1;
int todoDatePickerYear = 2026;
int todoDatePickerMonth = 1;
int todoDatePickerReturnPage = 0;

// Shopping
ShoppingItem shoppingList[MAX_SHOPPING];
int shoppingCount = 0;
int currentShoppingPage = 0;
int totalShoppingPages = 1;
int shoppingPageStarts[MAX_SHOPPING_PAGES];
int lastRenderedItem = -1;
CheckboxPos shoppingCheckboxes[50];
int shoppingCheckboxCount = 0;

// Motto
String mottoList[MAX_MOTTOS];
int mottoCount = 0;

// Wallpaper
String wallpaperFiles[MAX_WALLPAPERS];
int wallpaperCount = 0;
int selectedWallpaper = -1;
int wallpaperScrollOffset = 0;
bool wallpaperRotateActive = false;
unsigned long wallpaperRotateLastChange = 0;
int wallpaperViewMode = 0;  // 0=name list, 1=thumbnails

// Medication Reminder
time_t medReminderPressTime = 0;
String medPasscode = "";
String medPasscodeInput = "";
bool medSettingNewPasscode = false;
String medPasscodeFirst = "";

// Fortune Slips
int fortuneSlipCategory = -1;  // 0=kuanyin, 1=senso-ji
int fortuneSlipNumber = -1;
int sensoji_wording_page = 0;  // 0=first page, 1+=continuation pages (for Silver enlarged text)
bool sensoji_hasMore = false;   // true if sensoji wording has more pages
int kuanyin_story_page = 0;    // page index for kuanyin story (multi-page for Silver)
bool kuanyin_story_hasMore = false;  // true if story has more pages

// WiFi / Config
WiFiConfig wifiConfig;
WeatherData weatherData;
WeatherConfig weatherConfig;
TimeConfig timeConfig;
const char* ntpServer = "pool.ntp.org";
TimezoneInfo timezones[] = {
  {"UTC+0 \xe5\x8d\x94\xe8\xaa\xbf\xe4\xb8\x96\xe7\x95\x8c\xe6\x99\x82", "UTC0", 0},                         // UTC+0 協調世界時
  {"UTC-12 \xe5\x9c\x8b\xe9\x9a\x9b\xe6\x8f\x9b\xe6\x97\xa5\xe7\xb7\x9a\xe8\xa5\xbf", "MHT12", -43200},      // UTC-12
  {"UTC-11 \xe8\x96\xa9\xe6\x91\xa9\xe4\xba\x9e", "SST11", -39600},                                            // UTC-11 薩摩亞
  {"UTC-10 \xe5\xa4\x8f\xe5\xa8\x81\xe5\xa4\xb7", "HST10", -36000},                                            // UTC-10 夏威夷
  {"UTC-9 \xe9\x98\xbf\xe6\x8b\x89\xe6\x96\xaf\xe5\x8a\xa0", "AKST9AKDT", -32400},                            // UTC-9 阿拉斯加
  {"UTC-8 \xe7\xbe\x8e\xe5\x9c\x8b\xe5\xa4\xaa\xe5\xb9\xb3\xe6\xb4\x8b", "PST8PDT", -28800},                  // UTC-8 美國太平洋
  {"UTC-7 \xe7\xbe\x8e\xe5\x9c\x8b\xe5\xb1\xb1\xe5\x8d\x80", "MST7MDT", -25200},                              // UTC-7 美國山區
  {"UTC-6 \xe7\xbe\x8e\xe5\x9c\x8b\xe4\xb8\xad\xe9\x83\xa8", "CST6CDT", -21600},                              // UTC-6 美國中部
  {"UTC-5 \xe7\xbe\x8e\xe5\x9c\x8b\xe6\x9d\xb1\xe9\x83\xa8", "EST5EDT", -18000},                              // UTC-5 美國東部
  {"UTC-4 \xe5\xa4\xa7\xe8\xa5\xbf\xe6\xb4\x8b", "AST4ADT", -14400},                                           // UTC-4 大西洋
  {"UTC-3 \xe5\xb7\xb4\xe8\xa5\xbf", "BRT3", -10800},                                                           // UTC-3 巴西
  {"UTC-2 \xe4\xb8\xad\xe5\xa4\xa7\xe8\xa5\xbf\xe6\xb4\x8b", "GST2", -7200},                                   // UTC-2 中大西洋
  {"UTC-1 \xe4\xba\x9e\xe9\x80\x9f\xe7\x88\xbe", "CVT1", -3600},                                               // UTC-1 亞速爾
  {"UTC+1 \xe4\xb8\xad\xe6\xad\x90", "CET-1CEST", 3600},                                                        // UTC+1 中歐
  {"UTC+2 \xe6\x9d\xb1\xe6\xad\x90", "EET-2EEST", 7200},                                                        // UTC+2 東歐
  {"UTC+3 \xe8\x8e\xab\xe6\x96\xaf\xe7\xa7\x91", "MSK-3", 10800},                                               // UTC+3 莫斯科
  {"UTC+4 \xe6\x9d\x9c\xe6\x8b\x9c", "GST-4", 14400},                                                           // UTC+4 杜拜
  {"UTC+5 \xe5\x8d\xa1\xe6\x8b\x89\xe5\xa5\x87", "PKT-5", 18000},                                               // UTC+5 卡拉奇
  {"UTC+5:30 \xe5\x8d\xb0\xe5\xba\xa6", "IST-5:30", 19800},                                                     // UTC+5:30 印度
  {"UTC+6 \xe9\x81\x94\xe5\x8d\xa1", "BST-6", 21600},                                                           // UTC+6 達卡
  {"UTC+7 \xe6\x9b\xbc\xe8\xb0\xb7", "ICT-7", 25200},                                                           // UTC+7 曼谷
  {"UTC+8 \xe5\x8c\x97\xe4\xba\xac", "CST-8", 28800},                                                           // UTC+8 北京
  {"UTC+8 \xe4\xb8\x8a\xe6\xb5\xb7", "CST-8", 28800},                                                           // UTC+8 上海
  {"UTC+8 \xe8\x87\xba\xe5\x8c\x97", "CST-8", 28800},                                                           // UTC+8 臺北
  {"UTC+8 \xe9\xa6\x99\xe6\xb8\xaf", "HKT-8", 28800},                                                           // UTC+8 香港
  {"UTC+8 \xe6\x96\xb0\xe5\x8a\xa0\xe5\x9d\xa1", "SGT-8", 28800},                                               // UTC+8 新加坡
  {"UTC+9 \xe6\x9d\xb1\xe4\xba\xac", "JST-9", 32400},                                                           // UTC+9 東京
  {"UTC+9 \xe9\xa6\x96\xe7\x88\xbe", "KST-9", 32400},                                                           // UTC+9 首爾
  {"UTC+9:30 \xe9\x98\xbf\xe5\xbe\xb7\xe8\x90\x8a\xe5\xbe\xb7", "ACST-9:30ACDT", 34200},                       // UTC+9:30 阿德萊德
  {"UTC+10 \xe6\x82\x89\xe5\xb0\xbc", "AEST-10AEDT", 36000},                                                    // UTC+10 悉尼
  {"UTC+11 \xe6\x89\x80\xe7\xbe\x85\xe9\x96\x80\xe7\xbe\xa4\xe5\xb3\xb6", "SBT-11", 39600},                    // UTC+11 所羅門群島
  {"UTC+12 \xe5\xa5\xa7\xe5\x85\x8b\xe8\x98\xad", "NZST-12NZDT", 43200},                                        // UTC+12 奧克蘭
  {"UTC+13 \xe6\x9d\xb1\xe5\x8a\xa0", "TOT-13", 46800},                                                         // UTC+13 東加
};
int timezoneCount = sizeof(timezones) / sizeof(timezones[0]);
int selectedTimezone = 21;  // Default: UTC+8 北京
WiFiNetwork scannedNetworks[MAX_WIFI_NETWORKS];
int networkCount = 0;
int selectedNetworkIndex = -1;
bool wifiScanning = false;
bool showingKeyboard = false;
bool showingTimezone = false;
int tzScrollOffset = 0;
String passwordInput = "";
bool keyboardShift = false;
bool keyboardSymbols = false;
int setupSubmenu = 0;
int setupMenuPage = 0;
unsigned long lastClockUpdate = 0;
int lastClockMinute = -1;

// SD / SPI / Mutex
SPIClass sdSPI(HSPI);
SemaphoreHandle_t sdMutex = NULL;

// ScopedSDLock implementation
ScopedSDLock::ScopedSDLock() : locked(false) {
  if (sdMutex != NULL) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    locked = true;
  }
}
ScopedSDLock::~ScopedSDLock() {
  if (locked && sdMutex != NULL) {
    xSemaphoreGive(sdMutex);
  }
}

// Font management
M5Canvas canvas(&M5.Display);
bool useTTFFont = false;
String currentFontFile = "";
int systemFontIndex = -1;
int readingFontIndex = -1;
String systemFontFile = "";
String readingFontFile = "";
BinFont g_binFont = {0};
OpenFontRender ofr;
bool ofrFontLoaded = false;
std::list<File> ofr_file_list;
int systemFontChoice = 0;  // 0 = GenYoMinTW, 1 = Silver

// SD-card label bitmaps
uint8_t* sdLabelData = nullptr;
int sdLabelCount = 0;
SDLabelEntry* sdLabelEntries = nullptr;

// Navigation bar constants
const int NAV_ICON_SIZE = 64;
const int NAV_Y = 886;
const int NAV_PREV_X = 10;
const int NAV_NEXT_X = 84;
const int NAV_RETURN_X = 466;
const int NAV_SLEEP_X = 270;
const int NAV_TOUCH_Y_MIN = 870;
const int NAV_TOUCH_Y_MAX = 960;
