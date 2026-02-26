#pragma once

// ==================== Common Includes ====================
#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <USB.h>
#include "USBMSC.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include <list>
#include <algorithm>
#include <new>
#include "OpenFontRender.h"
extern "C" {
  #include "esp32s3/rom/miniz.h"
}

// ==================== UTF-8 Utility Functions ====================
#include "utf8_utils.h"

// ==================== Named Constants ====================

// E-ink grayscale-safe colors (M5Paper S3 is 4-bit grayscale)
// TFT_RED/BLUE/GREEN/ORANGE/YELLOW map to unpredictable gray shades.
// Use these semantic constants for consistent e-ink rendering.
static const uint16_t EPD_DARK_GRAY   = 0x4208;  // ~25% gray - emphasis text, status indicators
static const uint16_t EPD_MID_GRAY    = 0x7BEF;  // ~50% gray - secondary/muted text
static const uint16_t EPD_LIGHT_GRAY  = 0xC618;  // ~75% gray - subtle fills, backgrounds
static const uint16_t EPD_HIGHLIGHT   = 0x4208;  // dark gray - replaces TFT_RED for attention items

// Display dimensions (M5Paper S3 e-ink)
static const int DISPLAY_WIDTH  = 540;
static const int DISPLAY_HEIGHT = 960;

// Reading area layout (vertical CJK text region)
static const int READING_AREA_TOP    = 60;
static const int READING_AREA_BOTTOM = 850;
static const int READING_AREA_LEFT   = 50;
static const int READING_AREA_RIGHT  = 520;

// Vertical text layout (shopping list / todo list)
static const int VERTICAL_TEXT_START_Y    = 60;
static const int VERTICAL_TEXT_MAX_Y      = 900;
static const int VERTICAL_LEFT_MARGIN     = 60;
static const int VERTICAL_RIGHT_MARGIN    = 40;

// Progress bar
static const int PROGRESS_BAR_X = 30;
static const int PROGRESS_BAR_Y = 900;

// Reading font size limits
static const int MIN_READING_FONT_SIZE = 20;
static const int MAX_READING_FONT_SIZE = 52;
static const int FONT_SIZE_STEP        = 4;
static const int DEFAULT_READING_FONT_SIZE = 30;

// Timeouts (milliseconds)
static const unsigned long WEATHER_STALE_TIMEOUT   = 600000UL;
static const unsigned long WEATHER_REFRESH_INTERVAL = 900000UL;

// Read padding for UTF-8 boundary safety
static const int UTF8_READ_PADDING = 100;

// WiFi limits
static const int MAX_WIFI_NETWORKS        = 20;
static const int MAX_WIFI_CONNECT_ATTEMPTS = 20;

// Navigation bar layout
extern const int NAV_ICON_SIZE;
extern const int NAV_Y;
extern const int NAV_PREV_X;
extern const int NAV_NEXT_X;
extern const int NAV_RETURN_X;
extern const int NAV_TOUCH_Y_MIN;
extern const int NAV_TOUCH_Y_MAX;

// Pending nav touch (mid-render touch detection)
extern bool pendingNavTouch;
extern int pendingTouchX;
extern int pendingTouchY;
extern unsigned long lastTouchProcessedTime;
bool checkNavTouch();  // Poll touch during rendering; returns true if nav button touched

// ==================== Enums ====================

enum Mode {
  MODE_WELCOME,
  MODE_DASHBOARD,
  MODE_BOOK_LIST,
  MODE_READING,
  MODE_FONT_MENU,
  MODE_TODO_LIST,
  MODE_SHOPPING_LIST,
  MODE_WALLPAPER_LIST,
  MODE_WALLPAPER,
  MODE_CLOCK,
  MODE_SETUP,
  MODE_FONT_TEST,
  MODE_WEATHER,
  MODE_CALENDAR,
  MODE_CALENDAR_PICKER,
  MODE_CALENDAR_YEAR_MONTH,
  MODE_TODO_DATE_PICKER,
  MODE_CANGJIE_INPUT,
  MODE_MOTTO_TEST
};

// ==================== Struct Definitions ====================

struct Icon {
  const char* label;
  int x;
  int y;
  int w;
  int h;
};

struct Bookmark {
  int page;
  String label;
};

struct TodoItem {
  String date;
  String task;
  bool checked;
};

struct ShoppingItem {
  String groupName;
  String itemName;
  bool checked;
};

struct CheckboxPos {
  int x, y, size;
  int itemIdx;
  int touchMinX, touchMaxX, touchMinY, touchMaxY;
};

// Touch zone for todo date area (column-based)
struct TodoDateZone {
  int itemIdx;
  int touchMinX, touchMaxX, touchMinY, touchMaxY;
};

struct WiFiConfig {
  String ssid;
  String password;
  bool configured;
};

struct WeatherData {
  String city;
  float tempCurrent;
  float tempMin;
  float tempMax;
  float feelsLike;
  int humidity;
  float windSpeed;
  String description;
  String descChinese;
  String icon;
  int pressure;
  int visibility;
  float lat;
  float lon;
  int aqi;
  float pm25;
  float pm10;
  float o3;
  float no2;
  float co;
  bool aqiValid;
  long sunrise;
  long sunset;
  unsigned long fetchTime;
  bool valid;
  String fetchedUnits;  // Track units from API fetch to avoid float drift on toggle
  // Original values from API fetch (never modified by toggle)
  float origTempCurrent, origTempMin, origTempMax, origFeelsLike, origWindSpeed;
  struct { float tempMin, tempMax; } origForecast[3];

  struct Forecast {
    String date;
    String weekday;
    float tempMin;
    float tempMax;
    String desc;
    String descChinese;
  };
  Forecast forecast[3];
  int forecastCount;
};

struct WeatherConfig {
  String apiKey;
  String city;
  String units;
  bool configured;
};

struct TimeConfig {
  String timezone;
  int gmtOffset;
  bool timeSynced;
};

struct TimezoneInfo {
  const char* name;
  const char* tzString;
  int gmtOffset;
};

struct WiFiNetwork {
  String ssid;
  int rssi;
  bool encrypted;
};

struct GlyphIndex {
  uint32_t unicode;
  uint16_t width;
  uint16_t height;
  uint32_t bitmapOffset;
  uint32_t bitmapSize;
};

struct BinFont {
  uint32_t charCount;
  uint8_t fontSize;
  uint32_t version;
  char familyName[64];
  char styleName[64];
  GlyphIndex* index;
  File fontFile;
  bool loaded;
};

struct ScopedSDLock {
  bool locked;
  ScopedSDLock();
  ~ScopedSDLock();
  ScopedSDLock(const ScopedSDLock&) = delete;
  ScopedSDLock& operator=(const ScopedSDLock&) = delete;
};

struct ZipEntry {
  String filename;
  uint16_t method;        // 0=stored, 8=deflated
  uint32_t compSize;
  uint32_t uncompSize;
  uint32_t localOffset;   // Offset to local file header
};

struct LunarDate {
  int year, month, day;
  bool isLeapMonth;
};

// ==================== Extern Global Variables ====================

// Core / System
extern USBMSC msc;
extern sdmmc_card_t* sdCard;
extern Preferences prefs;
extern String currentFont;
extern String currentBook;
extern unsigned long lastActivityTime;
extern const unsigned long IDLE_SLEEP_TIMEOUT;
extern WebServer* webServer;
extern bool webServerEnabled;
extern bool webServerRunning;
extern bool usbMSCEnabled;
extern bool useSDCardIcons;
extern bool usbMSCActive;
extern bool useSxwnlCalendar;  // true = 壽星天文曆, false = Meeus (our way)

// Font selection
extern int numFonts;
extern int selectedFontIndex;
extern int fontMenuPage;
extern const int FONTS_PER_PAGE;
#define MAX_FONT_FILES 100
extern String fontFileList[MAX_FONT_FILES];
extern String fontDisplayNames[MAX_FONT_FILES];
extern int fontFileCount;

// Mode
extern Mode currentMode;

// Calendar
extern int calendarDayOffset;
extern int pickerYear, pickerMonth;
extern int pickerSelectedYear, pickerSelectedMonth, pickerSelectedDay;
extern int ymPickerYear;       // year being edited in year-month popup
extern int ymPickerMonth;      // month selected in year-month popup

// Dashboard
constexpr int kIconCount = 8;
extern Icon g_icons[kIconCount];
extern const char* kIconLabels[kIconCount];
extern Mode fontMenuReturnMode;

// Book / Reader
extern bool sdCardAvailable;
extern bool sdCardChecked;
static const int MAX_BOOKS = 20;
extern String bookList[MAX_BOOKS];
extern String bookDisplayName[MAX_BOOKS];
extern int bookCount;
extern String currentBookPath;
extern String currentPageContent;
extern int currentPage;
extern int totalPages;
extern size_t totalBookBytes;
extern int readingFontSize;
extern int bytesPerPage;
static const int MAX_PAGE_OFFSETS = 5000;
extern size_t* pageByteOffsets;
extern int pageOffsetsCount;
extern size_t currentPageByteOffset;
extern Bookmark bookmarks[5];
extern int bookmarkCount;

// EPUB
extern bool currentBookIsEpub;
extern char* epubFullText;
extern size_t epubFullTextLen;

// Todo
static const int MAX_TODO = 50;
extern TodoItem todoList[MAX_TODO];
extern int todoCount;
extern int currentTodoPage;
extern int totalTodoPages;
static const int MAX_TODO_PAGES = 10;
extern int todoPageStarts[MAX_TODO_PAGES];
extern int lastRenderedTodoItem;
extern int selectedTodoItem;
extern bool showCalendar;
extern CheckboxPos todoCheckboxes[50];
extern int todoCheckboxCount;
extern TodoDateZone todoDateZones[50];
extern int todoDateZoneCount;
extern int todoDatePickerItem;
extern int todoDatePickerYear;
extern int todoDatePickerMonth;
extern int todoDatePickerReturnPage;

// Shopping
static const int MAX_SHOPPING = 50;
extern ShoppingItem shoppingList[MAX_SHOPPING];
extern int shoppingCount;
extern int currentShoppingPage;
extern int totalShoppingPages;
static const int MAX_SHOPPING_PAGES = 10;
extern int shoppingPageStarts[MAX_SHOPPING_PAGES];
extern int lastRenderedItem;
extern CheckboxPos shoppingCheckboxes[50];
extern int shoppingCheckboxCount;

// Motto
static const int MAX_MOTTOS = 50;
extern String mottoList[MAX_MOTTOS];
extern int mottoCount;

// Wallpaper
static const int MAX_WALLPAPERS = 30;
extern String wallpaperFiles[MAX_WALLPAPERS];
extern int wallpaperCount;
extern int selectedWallpaper;
extern int wallpaperScrollOffset;

// WiFi / Config / Time
extern WiFiConfig wifiConfig;
extern WeatherData weatherData;
extern WeatherConfig weatherConfig;
extern TimeConfig timeConfig;
extern const char* ntpServer;
extern TimezoneInfo timezones[];
extern int timezoneCount;
extern int selectedTimezone;
extern WiFiNetwork scannedNetworks[MAX_WIFI_NETWORKS];
extern int networkCount;
extern int selectedNetworkIndex;
extern bool wifiScanning;
extern bool showingKeyboard;
extern bool showingTimezone;
extern String passwordInput;
extern bool keyboardShift;
extern bool keyboardSymbols;
extern int setupSubmenu;
extern unsigned long lastClockUpdate;
extern int lastClockMinute;

// SD / SPI / Mutex
extern SPIClass sdSPI;
extern SemaphoreHandle_t sdMutex;

// Font management
extern M5Canvas canvas;
extern bool useTTFFont;
extern String currentFontFile;
extern int systemFontIndex;
extern int readingFontIndex;
extern String systemFontFile;
extern String readingFontFile;
extern BinFont g_binFont;
extern OpenFontRender ofr;
extern bool ofrFontLoaded;
extern std::list<File> ofr_file_list;

// ==================== Function Declarations ====================

// font_manager
void scanFontFiles();
bool loadBinaryFont(const char* fontPath);
GlyphIndex* findGlyph(uint32_t unicode);
bool drawBinFontChar(uint32_t unicode, int x, int y, uint16_t color = TFT_BLACK);
int drawBinFontString(const String &text, int x, int y, int charSpacing);
bool loadTTFFont(const char* fontPath, int size = 30);
bool loadSystemFont();
bool loadReadingFont();

// wifi_config
void loadWiFiConfig();
void saveWiFiConfig();
void scanWiFiNetworks();
void syncTimeNTP();
bool connectToWiFi();
String getCurrentTimeString();
String getCurrentDateString();

// shopping_list
void loadCheckedItems();
void saveCheckedItems();
void clearCheckedShopping();
void saveShoppingList();
void loadShoppingList();
void calculateShoppingPages();
void drawShoppingList();

// todo_list
void loadCheckedTodos();
void saveCheckedTodos();
void clearCheckedTodos();
void saveTodoList();
long parseDateToNumber(String date);
void sortTodoListByDate();
void loadTodoList();
void calculateTodoPages();
void drawTodoList();
void drawTodoDatePicker();

// epub_reader
String htmlToText(const String& html);
String epubGetTitle(const String& epubPath);
bool epubLoad(const String& epubPath);

// book_reader
void scanBooks();
void saveReadingPosition();
int loadReadingPosition();
void saveBookmarks();
void loadBookmarks();
void addBookmark();
void updateBytesPerPage();
void recalculatePages();
bool loadCurrentPage();
bool loadBook(int bookIndex);
void drawBookList();
void drawReading();

// ui_drawing
bool drawNavIcon(const char* iconName, int x, int y);
void drawReturnButton();
void drawPageButtons(bool showPrev, bool showNext);
void drawNavBar(bool showPrev, bool showNext);
void drawVerticalNavBar(bool hasPrev, bool hasNext);
bool touchedReturnButton(int x, int y);
bool touchedPrevPage(int x, int y);
bool touchedNextPage(int x, int y);
void drawBatteryIndicator();
void drawCurrentTime();
void drawStatusBar();
int drawSystemText(const char* text, int x, int y, int size = 28, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE);
void drawSystemTextCentered(const char* text, int centerX, int y, int size = 28, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE);
int drawVerticalMixedText(String text, int x, int startY, int charSpacing = 30);

// weather
void loadWeatherConfig();
String weatherDescToChinese(String desc);
void drawWeatherIcon(int cx, int cy, int size, String desc);
String jsonGetValue(const String& json, const String& key);
bool fetchWeather();
void drawWeather(bool fast = false);
void redrawWeatherUnits();

// calendar
void drawCalendarPicker();
void drawCalendarYearMonth();
void drawCalendar();
void getDateWithOffset(int offset, int &outYear, int &outMonth, int &outDay, int &outWeekDay);
int dayOfWeek(int y, int m, int d);
int solarMonthDays(int y, int m);
long solarDayNumber(int y, int m, int d);
LunarDate solarToLunar(int sYear, int sMonth, int sDay);

// dashboard
void layoutIcons();
void drawWelcome();
void drawDashboard();
void enterDeepSleep();

// motto
void loadMottos();
void drawMottoOnSleep();
void drawMottoScreen();

// wallpaper
void loadWallpaperFiles();
void drawWallpaperList();
void drawWallpaper();

// web_server_handler
void startWebServer();
void stopWebServer();

// usb_msc_handler
void startUSBMSC();
void stopUSBMSC();

// preferences_helper
void savePrefInt(const char* ns, const char* key, int value);
void savePrefBool(const char* ns, const char* key, bool value);
int loadPrefInt(const char* ns, const char* key, int defaultVal);
bool loadPrefBool(const char* ns, const char* key, bool defaultVal);

// cangjie input
#include "cangjie.h"

// setup_ui
void getKeyboardRows(bool symbols, bool shift, const char* outRows[3]);
void drawVirtualKeyboard();
void updatePasswordDisplay();
void drawClock();
void drawSetupMenu();
void drawWiFiSetup();
void drawWebServerSetup();
void drawUSBMSCSetup();
void drawIconSetup();
void drawCalendarSetup();
void drawFontMenu();

// cleanup
void deleteDotFiles(const String& path = "/");
void cleanupMacOSFiles();
