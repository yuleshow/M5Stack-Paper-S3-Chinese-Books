#include "globals.h"

// ==================== Global Variable Definitions ====================

// Core / System
USBMSC msc;
sdmmc_card_t* sdCard = nullptr;
Preferences prefs;
String currentFont = "efont";
String currentBook = "";
unsigned long lastActivityTime = 0;
const unsigned long IDLE_SLEEP_TIMEOUT = 10 * 60 * 1000;  // 10 minutes
WebServer* webServer = nullptr;
bool webServerEnabled = false;
bool webServerRunning = false;

// Pending nav touch (for mid-render touch detection)
bool pendingNavTouch = false;
int pendingTouchX = 0;
int pendingTouchY = 0;
unsigned long lastTouchProcessedTime = 0;  // Debounce: time of last processed touch
bool usbMSCEnabled = false;
bool useSDCardIcons = true;
bool usbMSCActive = false;
bool useSxwnlCalendar = false;  // Default: Meeus (our way)

// Font selection
int numFonts = 4;
int selectedFontIndex = 0;
int fontMenuPage = 0;
const int FONTS_PER_PAGE = 9;
String fontFileList[MAX_FONT_FILES];
String fontDisplayNames[MAX_FONT_FILES];
int fontFileCount = 0;

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
  "壁紙",
  "設定",
  "睡眠"
};
Mode fontMenuReturnMode = MODE_DASHBOARD;

// SD / Book
bool sdCardAvailable = false;
bool sdCardChecked = false;
String bookList[MAX_BOOKS];
String bookDisplayName[MAX_BOOKS];
int bookCount = 0;
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
Bookmark bookmarks[5];
int bookmarkCount = 0;

// EPUB
bool currentBookIsEpub = false;
char* epubFullText = nullptr;
size_t epubFullTextLen = 0;

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
int selectedWallpaper = 0;
int wallpaperScrollOffset = 0;

// WiFi / Config
WiFiConfig wifiConfig;
WeatherData weatherData;
WeatherConfig weatherConfig;
TimeConfig timeConfig;
const char* ntpServer = "pool.ntp.org";
TimezoneInfo timezones[] = {
  {"UTC+0", "UTC0", 0},
  {"Beijing/Taipei +8", "CST-8", 28800},
  {"Tokyo +9", "JST-9", 32400},
  {"US West -8/-7", "PST8PDT", -28800},
  {"US East -5/-4", "EST5EDT", -18000},
  {"UK +0/+1", "GMT0BST", 0},
  {"Sydney +10/+11", "AEST-10AEDT", 36000}
};
int timezoneCount = 7;
int selectedTimezone = 1;
WiFiNetwork scannedNetworks[MAX_WIFI_NETWORKS];
int networkCount = 0;
int selectedNetworkIndex = -1;
bool wifiScanning = false;
bool showingKeyboard = false;
bool showingTimezone = false;
String passwordInput = "";
bool keyboardShift = false;
bool keyboardSymbols = false;
int setupSubmenu = 0;
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

// Navigation bar constants
const int NAV_ICON_SIZE = 64;
const int NAV_Y = 886;
const int NAV_PREV_X = 10;
const int NAV_NEXT_X = 84;
const int NAV_RETURN_X = 466;
const int NAV_TOUCH_Y_MIN = 870;
const int NAV_TOUCH_Y_MAX = 960;
