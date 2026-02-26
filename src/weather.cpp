#include "globals.h"
#include "labels/label_bitmaps.h"

// ===== Weather Functions =====

// ===== Bitmap helper functions for font-free weather rendering =====

// Get width of a bitmap for text at given size, 0 if not found
static int wGetBitmapWidth(const char* text, int size) {
  const LabelBitmap* lb = findLabelBitmap(text, size);
  return lb ? lb->w : 0;
}

// Draw a string char-by-char using bitmaps at the given size, returns total width
static int drawCharByChar(const char* str, int x, int y, int fontSize, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE) {
  int totalW = 0;
  for (int i = 0; str[i]; i++) {
    char ch[2] = {str[i], 0};
    totalW += drawSystemText(ch, x + totalW, y, fontSize, color, bg);
  }
  return totalW;
}

// Calculate total pixel width of a string rendered char-by-char
static int getCharByCharWidth(const char* str, int fontSize) {
  int totalW = 0;
  for (int i = 0; str[i]; i++) {
    char ch[2] = {str[i], 0};
    totalW += wGetBitmapWidth(ch, fontSize);
  }
  return totalW;
}

// Draw a numeric string + unit suffix centered
static void drawValueCentered(const char* numStr, const char* suffix, int centerX, int y, int fontSize, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE) {
  int totalW = getCharByCharWidth(numStr, fontSize)
             + (suffix ? wGetBitmapWidth(suffix, fontSize) : 0);
  int xp = centerX - totalW / 2;
  xp += drawCharByChar(numStr, xp, y, fontSize, color, bg);
  if (suffix) drawSystemText(suffix, xp, y, fontSize, color, bg);
}

// Draw circular refresh icon in the lower-left nav area
static void drawRefreshIcon() {
  int rcx = NAV_PREV_X + 30;
  int rcy = NAV_Y + 30;
  int r = 18;
  M5.Display.drawCircle(rcx, rcy, r, TFT_BLACK);
  M5.Display.drawCircle(rcx, rcy, r - 1, TFT_BLACK);
  M5.Display.fillTriangle(rcx, rcy, rcx + r + 2, rcy, rcx + r + 2, rcy + r + 2, TFT_WHITE);
  M5.Display.fillTriangle(rcx, rcy, rcx, rcy + r + 2, rcx + r + 2, rcy + r + 2, TFT_WHITE);
  int ax = rcx + r;
  int ay = rcy;
  M5.Display.fillTriangle(ax - 8, ay - 2, ax + 2, ay - 2, ax - 3, ay - 12, TFT_BLACK);
}

void loadWeatherConfig() {
  // Weather config is now loaded from [weather] section in config.ini
  // by loadWiFiConfig(). This function serves as fallback for legacy weather.cfg.
  if (weatherConfig.configured) return;  // Already loaded from config.ini

  weatherConfig.configured = false;
  weatherConfig.units = "metric";
  weatherConfig.city = "";
  weatherConfig.apiKey = "";

  if (!sdCardAvailable) return;

  // Fallback: try legacy weather.cfg
  File f = SD.open("/weather.cfg");
  if (!f) {
    Serial.println("No weather.cfg found (use [weather] section in config.ini)");
    return;
  }
  Serial.println("Loading weather from legacy weather.cfg");

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim(); val.trim();

    if (key == "apikey") weatherConfig.apiKey = val;
    else if (key == "city") weatherConfig.city = val;
    else if (key == "units") weatherConfig.units = val;
  }
  f.close();

  if (weatherConfig.apiKey.length() > 0 && weatherConfig.city.length() > 0) {
    weatherConfig.configured = true;
    Serial.printf("Weather config (legacy): city=%s, units=%s\n",
      weatherConfig.city.c_str(), weatherConfig.units.c_str());
  }
}

// Map OpenWeatherMap description to Chinese
String weatherDescToChinese(String desc) {
  desc.toLowerCase();
  if (desc.indexOf("clear") >= 0)           return "晴天";
  if (desc.indexOf("few clouds") >= 0)      return "少雲";
  if (desc.indexOf("scattered") >= 0)       return "疏雲";
  if (desc.indexOf("broken") >= 0)          return "多雲";
  if (desc.indexOf("overcast") >= 0)        return "陰天";
  if (desc.indexOf("drizzle") >= 0)         return "毛毛雨";
  if (desc.indexOf("light rain") >= 0)      return "小雨";
  if (desc.indexOf("moderate rain") >= 0)   return "中雨";
  if (desc.indexOf("heavy rain") >= 0)      return "大雨";
  if (desc.indexOf("thunderstorm") >= 0)    return "雷雨";
  if (desc.indexOf("snow") >= 0)            return "下雪";
  if (desc.indexOf("mist") >= 0)            return "薄霧";
  if (desc.indexOf("fog") >= 0)             return "霧";
  if (desc.indexOf("haze") >= 0)            return "霾";
  if (desc.indexOf("cloud") >= 0)           return "多雲";
  if (desc.indexOf("rain") >= 0)            return "下雨";
  return "—";
}

// Map weather description to ASCII art icon for e-ink
void drawWeatherIcon(int cx, int cy, int size, String desc) {
  desc.toLowerCase();
  int r = size / 2;

  if (desc.indexOf("clear") >= 0) {
    // Sun: circle with rays
    M5.Display.fillCircle(cx, cy, r * 0.4, TFT_BLACK);
    for (int a = 0; a < 360; a += 45) {
      float rad = a * PI / 180.0;
      int x1 = cx + cos(rad) * r * 0.55;
      int y1 = cy + sin(rad) * r * 0.55;
      int x2 = cx + cos(rad) * r * 0.85;
      int y2 = cy + sin(rad) * r * 0.85;
      M5.Display.drawLine(x1, y1, x2, y2, TFT_BLACK);
    }
  } else if (desc.indexOf("cloud") >= 0 || desc.indexOf("overcast") >= 0) {
    // Cloud shape
    M5.Display.fillCircle(cx - r*0.2, cy, r*0.35, TFT_BLACK);
    M5.Display.fillCircle(cx + r*0.2, cy - r*0.1, r*0.3, TFT_BLACK);
    M5.Display.fillCircle(cx + r*0.5, cy + r*0.05, r*0.25, TFT_BLACK);
    M5.Display.fillRect(cx - r*0.55, cy + r*0.05, r*1.3, r*0.35, TFT_BLACK);
  } else if (desc.indexOf("rain") >= 0 || desc.indexOf("drizzle") >= 0) {
    // Cloud + rain drops
    M5.Display.fillCircle(cx - r*0.15, cy - r*0.2, r*0.28, TFT_BLACK);
    M5.Display.fillCircle(cx + r*0.2, cy - r*0.25, r*0.22, TFT_BLACK);
    M5.Display.fillRect(cx - r*0.45, cy - r*0.05, r*0.9, r*0.25, TFT_BLACK);
    // Rain drops
    for (int i = -1; i <= 1; i++) {
      int dx = cx + i * r * 0.3;
      M5.Display.drawLine(dx, cy + r*0.35, dx - r*0.1, cy + r*0.65, TFT_BLACK);
      M5.Display.drawLine(dx, cy + r*0.55, dx - r*0.1, cy + r*0.85, TFT_BLACK);
    }
  } else if (desc.indexOf("thunder") >= 0) {
    // Cloud + lightning bolt
    M5.Display.fillCircle(cx - r*0.15, cy - r*0.3, r*0.28, TFT_BLACK);
    M5.Display.fillCircle(cx + r*0.2, cy - r*0.35, r*0.22, TFT_BLACK);
    M5.Display.fillRect(cx - r*0.45, cy - r*0.15, r*0.9, r*0.25, TFT_BLACK);
    // Lightning bolt
    M5.Display.drawLine(cx, cy + r*0.2, cx - r*0.15, cy + r*0.5, TFT_BLACK);
    M5.Display.drawLine(cx - r*0.15, cy + r*0.5, cx + r*0.05, cy + r*0.5, TFT_BLACK);
    M5.Display.drawLine(cx + r*0.05, cy + r*0.5, cx - r*0.1, cy + r*0.85, TFT_BLACK);
  } else if (desc.indexOf("snow") >= 0) {
    // Snowflake pattern
    for (int a = 0; a < 360; a += 60) {
      float rad = a * PI / 180.0;
      M5.Display.drawLine(cx, cy, cx + cos(rad)*r*0.7, cy + sin(rad)*r*0.7, TFT_BLACK);
    }
    M5.Display.fillCircle(cx, cy, r*0.12, TFT_BLACK);
  } else if (desc.indexOf("fog") >= 0 || desc.indexOf("mist") >= 0 || desc.indexOf("haze") >= 0) {
    // Horizontal lines for fog
    for (int i = -2; i <= 2; i++) {
      int ly = cy + i * r * 0.3;
      M5.Display.drawLine(cx - r*0.6, ly, cx + r*0.6, ly, TFT_BLACK);
    }
  } else {
    // Default: question mark
    M5.Display.drawCircle(cx, cy, r*0.6, TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_24);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("?", cx, cy);
    M5.Display.setTextDatum(TL_DATUM);
  }
}

// Simple JSON value extractor (avoids ArduinoJson dependency)
String jsonGetValue(const String& json, const String& key) {
  String searchKey = "\"" + key + "\"";
  int keyPos = json.indexOf(searchKey);
  if (keyPos < 0) return "";

  int colonPos = json.indexOf(':', keyPos + searchKey.length());
  if (colonPos < 0) return "";

  int start = colonPos + 1;
  while (start < json.length() && json[start] == ' ') start++;

  if (json[start] == '"') {
    int end = json.indexOf('"', start + 1);
    return json.substring(start + 1, end);
  } else {
    int end = start;
    while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') end++;
    return json.substring(start, end);
  }
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Weather: WiFi not connected, attempting to connect...");
    if (wifiConfig.configured) {
      if (!connectToWiFi()) {
        Serial.println("Weather: WiFi connection failed");
        return false;
      }
    } else {
      Serial.println("Weather: WiFi not configured");
      return false;
    }
  }
  if (!weatherConfig.configured) {
    Serial.println("Weather: not configured");
    return false;
  }

  HTTPClient http;
  String encodedCity = weatherConfig.city;
  encodedCity.replace(" ", "%20");
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" +
    encodedCity + "&units=" + weatherConfig.units +
    "&appid=" + weatherConfig.apiKey;

  Serial.printf("Weather URL: %s\n", url.c_str());
  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("Weather HTTP error: %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();
  Serial.printf("Weather response: %d bytes\n", body.length());

  // Parse current weather
  // Use API "name" field for clean display (e.g. "Pasadena" instead of "Pasadena,CA,US")
  String apiCityName = jsonGetValue(body, "name");
  weatherData.city = (apiCityName.length() > 0) ? apiCityName : weatherConfig.city;
  weatherData.tempCurrent = jsonGetValue(body, "temp").toFloat();
  weatherData.tempMin = jsonGetValue(body, "temp_min").toFloat();
  weatherData.tempMax = jsonGetValue(body, "temp_max").toFloat();
  weatherData.feelsLike = jsonGetValue(body, "feels_like").toFloat();
  weatherData.humidity = jsonGetValue(body, "humidity").toInt();
  weatherData.windSpeed = jsonGetValue(body, "speed").toFloat();
  weatherData.fetchedUnits = weatherConfig.units;  // Remember API fetch units
  // Save originals for drift-free unit toggle
  weatherData.origTempCurrent = weatherData.tempCurrent;
  weatherData.origTempMin     = weatherData.tempMin;
  weatherData.origTempMax     = weatherData.tempMax;
  weatherData.origFeelsLike   = weatherData.feelsLike;
  weatherData.origWindSpeed   = weatherData.windSpeed;
  weatherData.pressure = jsonGetValue(body, "pressure").toInt();
  weatherData.visibility = jsonGetValue(body, "visibility").toInt();
  weatherData.description = jsonGetValue(body, "description");
  weatherData.descChinese = weatherDescToChinese(weatherData.description);
  weatherData.icon = jsonGetValue(body, "icon");
  
  // Extract coordinates for Air Pollution API
  weatherData.lat = jsonGetValue(body, "lat").toFloat();
  weatherData.lon = jsonGetValue(body, "lon").toFloat();
  
  // Extract sunrise/sunset (Unix timestamps)
  weatherData.sunrise = jsonGetValue(body, "sunrise").toInt();
  weatherData.sunset = jsonGetValue(body, "sunset").toInt();
  
  weatherData.fetchTime = millis();
  weatherData.valid = true;

  Serial.printf("Weather: %.1f°C, %s (%s)\n",
    weatherData.tempCurrent, weatherData.description.c_str(),
    weatherData.descChinese.c_str());

  // Fetch 3-day forecast
  HTTPClient http2;
  String forecastUrl = "http://api.openweathermap.org/data/2.5/forecast?q=" +
    encodedCity + "&units=" + weatherConfig.units +
    "&cnt=24&appid=" + weatherConfig.apiKey;

  http2.begin(forecastUrl);
  int code2 = http2.GET();
  weatherData.forecastCount = 0;

  if (code2 == 200) {
    String fbody = http2.getString();

    // Extract forecast data for next 3 days (every 8th entry = 24h apart)
    int searchFrom = 0;
    String lastDate = "";
    for (int i = 0; i < 24 && weatherData.forecastCount < 3; i++) {
      int listPos = fbody.indexOf("\"dt_txt\"", searchFrom);
      if (listPos < 0) break;
      searchFrom = listPos + 10;

      String dtTxt = jsonGetValue(fbody.substring(listPos - 1), "dt_txt");
      String dateOnly = dtTxt.substring(5, 10);  // MM-DD

      if (dateOnly != lastDate && dtTxt.indexOf("12:00:00") >= 0) {
        lastDate = dateOnly;
        // Extract the full list item (search back ~600 chars to cover entire entry)
        int itemStart = max(0, listPos - 600);
        String block = fbody.substring(itemStart, listPos + 50);

        int idx = weatherData.forecastCount;
        weatherData.forecast[idx].date = dateOnly.substring(0,2) + "/" + dateOnly.substring(3,5);
        weatherData.forecast[idx].tempMin = jsonGetValue(block, "temp_min").toFloat();
        weatherData.forecast[idx].tempMax = jsonGetValue(block, "temp_max").toFloat();
        weatherData.forecast[idx].desc = jsonGetValue(block, "description");
        weatherData.forecast[idx].descChinese = weatherDescToChinese(weatherData.forecast[idx].desc);
        Serial.printf("Forecast %d: %s desc=%s min=%.1f max=%.1f\n", idx,
          weatherData.forecast[idx].date.c_str(),
          weatherData.forecast[idx].desc.c_str(),
          weatherData.forecast[idx].tempMin, weatherData.forecast[idx].tempMax);

        // Weekday from date
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          int daysAhead = i / 8 + 1;
          int wday = (timeinfo.tm_wday + daysAhead) % 7;
          static const char* wdCN[] = {"日", "一", "二", "三", "四", "五", "六"};
          weatherData.forecast[idx].weekday = wdCN[wday];
        }

        weatherData.forecastCount++;
        // Save original forecast values for drift-free toggle
        weatherData.origForecast[idx].tempMin = weatherData.forecast[idx].tempMin;
        weatherData.origForecast[idx].tempMax = weatherData.forecast[idx].tempMax;
      }
    }
    Serial.printf("Forecast: %d days loaded\n", weatherData.forecastCount);
  }
  http2.end();

  // Fetch Air Quality Index (AQI)
  weatherData.aqiValid = false;
  if (weatherData.lat != 0 || weatherData.lon != 0) {
    HTTPClient http3;
    char aqiUrl[256];
    snprintf(aqiUrl, sizeof(aqiUrl),
      "http://api.openweathermap.org/data/2.5/air_pollution?lat=%.4f&lon=%.4f&appid=%s",
      weatherData.lat, weatherData.lon, weatherConfig.apiKey.c_str());
    
    Serial.printf("AQI URL: %s\n", aqiUrl);
    http3.begin(String(aqiUrl));
    int code3 = http3.GET();
    
    if (code3 == 200) {
      String abody = http3.getString();
      weatherData.aqi = jsonGetValue(abody, "aqi").toInt();
      weatherData.pm25 = jsonGetValue(abody, "pm2_5").toFloat();
      weatherData.pm10 = jsonGetValue(abody, "pm10").toFloat();
      weatherData.o3 = jsonGetValue(abody, "o3").toFloat();
      weatherData.no2 = jsonGetValue(abody, "no2").toFloat();
      weatherData.co = jsonGetValue(abody, "co").toFloat();
      weatherData.aqiValid = true;
      Serial.printf("AQI: %d, PM2.5: %.1f, PM10: %.1f\n",
        weatherData.aqi, weatherData.pm25, weatherData.pm10);
    } else {
      Serial.printf("AQI HTTP error: %d\n", code3);
    }
    http3.end();
  }

  return true;
}

void drawWeather(bool fast) {
  M5.Display.setEpdMode(fast ? epd_mode_t::epd_fast : epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  int w = M5.Display.width();   // 540
  int h = M5.Display.height();  // 960

  // Load config if not yet loaded
  if (!weatherConfig.configured) {
    loadWeatherConfig();
  }

  // Status bar + nav bar first
  drawStatusBar();
  drawReturnButton();

  // Refresh button (lower-left)
  drawRefreshIcon();

  if (!weatherConfig.configured) {
    // Show setup instructions
    drawSystemTextCentered("天氣 未設定", w/2, 100, 36);
    drawSystemTextCentered("請在 SD 卡 config.ini 中設定", w/2, 200, 24);

    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.5);
    M5.Display.setTextColor(TFT_BLACK);
    int y = 280;
    M5.Display.setCursor(40, y); M5.Display.print("File: /config.ini"); y += 40;
    M5.Display.setCursor(40, y); M5.Display.print("Add [weather] section:"); y += 40;
    M5.Display.setCursor(60, y); M5.Display.print("[weather]"); y += 35;
    M5.Display.setCursor(60, y); M5.Display.print("apikey=YOUR_API_KEY"); y += 35;
    M5.Display.setCursor(60, y); M5.Display.print("city=Taipei"); y += 35;
    M5.Display.setCursor(60, y); M5.Display.print("units=metric"); y += 60;

    drawSystemText("API Key 申請：", 40, y, 22); y += 35;
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(40, y); M5.Display.print("openweathermap.org/api"); y += 50;

    drawSystemText("city 範例：", 40, y, 22); y += 35;
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(60, y); M5.Display.print("Taipei, London, Tokyo"); y += 30;
    M5.Display.setCursor(60, y); M5.Display.print("New York, San Francisco"); y += 50;

    drawSystemText("units 選項：", 40, y, 22); y += 35;
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.2);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(60, y); M5.Display.print("metric (C) / imperial (F)");

    M5.Display.display();
    return;
  }

  // Show "loading" while fetching
  if (!weatherData.valid || (millis() - weatherData.fetchTime > WEATHER_STALE_TIMEOUT)) {
    if (WiFi.status() != WL_CONNECTED) {
      drawSystemTextCentered("連接 WiFi...", w/2, h/2, 36);
    } else {
      drawSystemTextCentered("載入天氣中...", w/2, h/2, 36);
    }
    M5.Display.display();

    if (!fetchWeather()) {
      M5.Display.fillScreen(TFT_WHITE);
      drawSystemTextCentered("天氣載入失敗", w/2, h/2 - 40, 36);
      if (WiFi.status() != WL_CONNECTED)
        drawSystemTextCentered("WiFi 未連線", w/2, h/2 + 20, 24);
      else
        drawSystemTextCentered("請檢查 API Key 與城市名稱", w/2, h/2 + 20, 24);

      // Redraw buttons
    drawStatusBar();
    drawReturnButton();
    drawRefreshIcon();
      M5.Display.display();  // Flush failure message to e-ink
      return;
    }

    // Clear screen for fresh draw
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);

    // Redraw buttons
    drawStatusBar();
    drawReturnButton();
    drawRefreshIcon();
  }

  String unitSymbol = (weatherConfig.units == "imperial") ? "°F" : "°C";
  const char* unitSym = unitSymbol.c_str();

  // === Title: City name (user-entered, may use font fallback) ===
  drawSystemTextCentered(weatherData.city.c_str(), w/2, 10, 38);

  // Divider
  M5.Display.drawLine(30, 52, w - 30, 52, TFT_BLACK);

  // === Weather icon (large, centered) ===
  drawWeatherIcon(w/2, 130, 120, weatherData.description);

  // === Description in Chinese ===
  drawSystemTextCentered(weatherData.descChinese.c_str(), w/2, 210, 40);

  // === Temperature (big, char-by-char bitmaps) ===
  {
    char tempNum[16];
    snprintf(tempNum, sizeof(tempNum), "%.0f", weatherData.tempCurrent);
    drawValueCentered(tempNum, unitSym, w/2, 260, 64);
  }

  // === Feels like (bitmap components) ===
  {
    char feelsNum[16];
    snprintf(feelsNum, sizeof(feelsNum), "%.0f", weatherData.feelsLike);
    int xp = w/2 - 60;
    xp += drawSystemText("體感", xp, 340, 24);
    xp += 6;
    xp += drawCharByChar(feelsNum, xp, 340, 24);
    drawSystemText(unitSym, xp, 340, 24);
  }

  // Divider
  M5.Display.drawLine(30, 375, w - 30, 375, TFT_BLACK);

  // === Details grid (2 columns) ===
  int detailY = 390;
  int col1 = 50;
  int col2 = w/2 + 20;
  int lineH = 38;
  int detailSize = 26;

  // Row 1: Min/Max temp (bitmap components)
  {
    char val[16];
    int xp = col1;
    xp += drawSystemText("最低", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%.0f", weatherData.tempMin);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText(unitSym, xp, detailY, detailSize);
    
    xp = col2;
    xp += drawSystemText("最高", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%.0f", weatherData.tempMax);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText(unitSym, xp, detailY, detailSize);
  }
  detailY += lineH;

  // Row 2: Humidity / Wind (bitmap components)
  {
    char val[16];
    int xp = col1;
    xp += drawSystemText("濕度", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%d", weatherData.humidity);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText("%", xp, detailY, detailSize);
    
    const char* windUnit = (weatherConfig.units == "imperial") ? "mph" : "m/s";
    xp = col2;
    xp += drawSystemText("風速", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%.1f", weatherData.windSpeed);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText(windUnit, xp, detailY, detailSize);
  }
  detailY += lineH;

  // Row 3: Pressure / Visibility (bitmap components)
  {
    char val[16];
    int xp = col1;
    xp += drawSystemText("氣壓", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%d", weatherData.pressure);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText("hPa", xp, detailY, detailSize);
    
    xp = col2;
    xp += drawSystemText("能見", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%d", weatherData.visibility / 1000);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText("km", xp, detailY, detailSize);
  }
  detailY += lineH;

  // Row 4: Sunrise / Sunset (bitmap components)
  if (weatherData.sunrise > 0 && weatherData.sunset > 0) {
    time_t sr = (time_t)weatherData.sunrise;
    time_t ss = (time_t)weatherData.sunset;
    struct tm srTm, ssTm;
    localtime_r(&sr, &srTm);
    localtime_r(&ss, &ssTm);
    char hh[4], mm[4];
    int xp = col1;
    xp += drawSystemText("日出", xp, detailY, detailSize);
    xp += 3;
    snprintf(hh, sizeof(hh), "%02d", srTm.tm_hour);
    xp += drawCharByChar(hh, xp, detailY, detailSize);
    xp += drawSystemText(":", xp, detailY, detailSize);
    snprintf(mm, sizeof(mm), "%02d", srTm.tm_min);
    drawCharByChar(mm, xp, detailY, detailSize);

    xp = col2;
    xp += drawSystemText("日落", xp, detailY, detailSize);
    xp += 3;
    snprintf(hh, sizeof(hh), "%02d", ssTm.tm_hour);
    xp += drawCharByChar(hh, xp, detailY, detailSize);
    xp += drawSystemText(":", xp, detailY, detailSize);
    snprintf(mm, sizeof(mm), "%02d", ssTm.tm_min);
    drawCharByChar(mm, xp, detailY, detailSize);
    detailY += lineH;
  }

  // Row 5: AQI / PM2.5 (bitmap components)
  if (weatherData.aqiValid) {
    const char* aqiLabels[] = {"", "優", "良", "中等", "差", "極差"};
    int aqiIdx = weatherData.aqi;
    if (aqiIdx < 1 || aqiIdx > 5) aqiIdx = 0;
    int xp = col1;
    xp += drawSystemText("空氣", xp, detailY, detailSize);
    xp += 3;
    xp += drawSystemText(aqiLabels[aqiIdx], xp, detailY, detailSize);
    xp += drawSystemText("(", xp, detailY, detailSize);
    char aqiNum[8];
    snprintf(aqiNum, sizeof(aqiNum), "%d", weatherData.aqi);
    xp += drawCharByChar(aqiNum, xp, detailY, detailSize);
    drawSystemText(")", xp, detailY, detailSize);

    int xp2 = col2;
    xp2 += drawSystemText("PM2.5", xp2, detailY, detailSize);
    xp2 += 3;
    char pm25Num[16];
    snprintf(pm25Num, sizeof(pm25Num), "%.1f", weatherData.pm25);
    drawCharByChar(pm25Num, xp2, detailY, detailSize);
    detailY += lineH;
  }

  // Divider
  M5.Display.drawLine(30, detailY + 5, w - 30, detailY + 5, TFT_BLACK);
  detailY += 20;

  // === 3-Day Forecast ===
  if (weatherData.forecastCount > 0) {
    drawSystemTextCentered("未來預報", w/2, detailY, 28);
    detailY += 40;

    int fColW = (w - 60) / 3;
    for (int i = 0; i < weatherData.forecastCount; i++) {
      int fcx = 30 + fColW * i + fColW / 2;

      // Date + weekday (bitmap components)
      {
        // date is like "02/25", weekday is like "三"
        const char* dateStr = weatherData.forecast[i].date.c_str();
        const char* wkStr = weatherData.forecast[i].weekday.c_str();
        bool hasWk = weatherData.forecast[i].weekday.length() > 0;
        int tw = getCharByCharWidth(dateStr, 24);
        if (hasWk) {
          tw += 3 + wGetBitmapWidth("(", 24) + wGetBitmapWidth(wkStr, 24) + wGetBitmapWidth(")", 24);
        }
        int xp = fcx - tw / 2;
        xp += drawCharByChar(dateStr, xp, detailY, 24);
        if (hasWk) {
          xp += 3;
          xp += drawSystemText("(", xp, detailY, 24);
          xp += drawSystemText(wkStr, xp, detailY, 24);
          drawSystemText(")", xp, detailY, 24);
        }
      }

      // Forecast icon
      drawWeatherIcon(fcx, detailY + 60, 60, weatherData.forecast[i].desc);

      // Chinese desc
      drawSystemTextCentered(weatherData.forecast[i].descChinese.c_str(), fcx, detailY + 100, 24);

      // Temp range (bitmap components, centered)
      {
        char ftemp[16];
        if ((int)roundf(weatherData.forecast[i].tempMin) == (int)roundf(weatherData.forecast[i].tempMax)) {
          snprintf(ftemp, sizeof(ftemp), "%.0f", weatherData.forecast[i].tempMin);
          drawValueCentered(ftemp, unitSym, fcx, detailY + 135, 24);
        } else {
          char tmin[8], tmax[8];
          snprintf(tmin, sizeof(tmin), "%.0f", weatherData.forecast[i].tempMin);
          snprintf(tmax, sizeof(tmax), "%.0f", weatherData.forecast[i].tempMax);
          int tw = getCharByCharWidth(tmin, 24) + wGetBitmapWidth("~", 24)
                 + getCharByCharWidth(tmax, 24) + wGetBitmapWidth(unitSym, 24);
          int xp = fcx - tw / 2;
          xp += drawCharByChar(tmin, xp, detailY + 135, 24);
          xp += drawSystemText("~", xp, detailY + 135, 24);
          xp += drawCharByChar(tmax, xp, detailY + 135, 24);
          drawSystemText(unitSym, xp, detailY + 135, 24);
        }
      }
    }
  }

  // °C/°F toggle button (bitmap)
  {
    int btnX = NAV_NEXT_X;
    int btnY = NAV_Y;
    int btnW = 64, btnH = 64;
    M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_BLACK);
    const char* unitLabel = (weatherConfig.units == "imperial") ? "°F" : "°C";
    drawSystemTextCentered(unitLabel, btnX + btnW / 2, btnY + 18, 28);
  }

  // Update time at bottom center (bitmap components)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char hh[4], mm[4];
    snprintf(hh, sizeof(hh), "%02d", timeinfo.tm_hour);
    snprintf(mm, sizeof(mm), "%02d", timeinfo.tm_min);
    int tw = wGetBitmapWidth("更新", 20) + 4
           + getCharByCharWidth(hh, 20) + wGetBitmapWidth(":", 20)
           + getCharByCharWidth(mm, 20);
    int xp = w/2 - tw/2;
    xp += drawSystemText("更新", xp, 855, 20);
    xp += 4;
    xp += drawCharByChar(hh, xp, 855, 20);
    xp += drawSystemText(":", xp, 855, 20);
    drawCharByChar(mm, xp, 855, 20);
  }

  M5.Display.display();
}

// Partial update: only redraw temperature/wind values + °C/°F button
void redrawWeatherUnits() {
  if (!weatherData.valid) return;

  int w = M5.Display.width();
  String unitSymbol = (weatherConfig.units == "imperial") ? "°F" : "°C";

  int col1 = 50;
  int col2 = w / 2 + 20;
  int detailY = 390;
  int lineH = 38;
  int detailSize = 26;

  // Calculate forecast Y (depends on optional rows)
  int fDetailY = detailY;
  fDetailY += lineH;  // after min/max
  fDetailY += lineH;  // after humidity/wind
  fDetailY += lineH;  // after pressure/visibility
  if (weatherData.sunrise > 0 && weatherData.sunset > 0) fDetailY += lineH;
  if (weatherData.aqiValid) fDetailY += lineH;
  fDetailY += 20;     // after divider
  fDetailY += 40;     // after "未來預報" heading

  // Collect all rects that need updating
  struct Rect { int x, y, w, h; };
  Rect areas[12];
  int n = 0;

  // Big temp (centered at w/2, y=260, font 64)
  areas[n++] = {120, 248, 300, 82};
  // Feels like (starts at w/2-60, y=340, font 24)
  areas[n++] = {w/2 - 65, 332, 200, 35};
  // Min/Max row (y=390)
  areas[n++] = {col1, detailY - 5, w - col1 * 2, 35};
  // Wind value only - col2 side (y=428)
  areas[n++] = {col2, detailY + lineH - 5, w / 2 - 20, 35};
  // Forecast temps (3 columns)
  if (weatherData.forecastCount > 0) {
    int fColW = (w - 60) / 3;
    for (int i = 0; i < weatherData.forecastCount && n < 10; i++) {
      int fcx = 30 + fColW * i + fColW / 2;
      areas[n++] = {fcx - 70, fDetailY + 127, 140, 32};
    }
  }
  // °C/°F button
  int btnIdx = n;
  areas[n++] = {NAV_NEXT_X, NAV_Y, 64, 64};

  // === Two-pass partial update (same pattern as shopping list checkboxes) ===
  M5.Display.setEpdMode(epd_mode_t::epd_fast);

  // Pass 1: flash all areas black
  for (int i = 0; i < n; i++) {
    M5.Display.fillRect(areas[i].x, areas[i].y, areas[i].w, areas[i].h, TFT_BLACK);
  }
  M5.Display.display();

  // Pass 2: clear to white and redraw content
  for (int i = 0; i < n; i++) {
    M5.Display.fillRect(areas[i].x, areas[i].y, areas[i].w, areas[i].h, TFT_WHITE);
  }

  const char* unitSym = unitSymbol.c_str();

  // -- Big temp (bitmap components) --
  {
    char tempNum[16];
    snprintf(tempNum, sizeof(tempNum), "%.0f", weatherData.tempCurrent);
    drawValueCentered(tempNum, unitSym, w / 2, 260, 64);
  }

  // -- Feels like (bitmap components) --
  {
    char feelsNum[16];
    snprintf(feelsNum, sizeof(feelsNum), "%.0f", weatherData.feelsLike);
    int xp = w/2 - 60;
    xp += drawSystemText("體感", xp, 340, 24);
    xp += 6;
    xp += drawCharByChar(feelsNum, xp, 340, 24);
    drawSystemText(unitSym, xp, 340, 24);
  }

  // -- Min / Max (bitmap components) --
  {
    char val[16];
    int xp = col1;
    xp += drawSystemText("最低", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%.0f", weatherData.tempMin);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText(unitSym, xp, detailY, detailSize);

    xp = col2;
    xp += drawSystemText("最高", xp, detailY, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%.0f", weatherData.tempMax);
    xp += drawCharByChar(val, xp, detailY, detailSize);
    drawSystemText(unitSym, xp, detailY, detailSize);
  }

  // -- Wind (bitmap components) --
  {
    char val[16];
    const char* windUnit = (weatherConfig.units == "imperial") ? "mph" : "m/s";
    int xp = col2;
    xp += drawSystemText("風速", xp, detailY + lineH, detailSize);
    xp += 3;
    snprintf(val, sizeof(val), "%.1f", weatherData.windSpeed);
    xp += drawCharByChar(val, xp, detailY + lineH, detailSize);
    drawSystemText(windUnit, xp, detailY + lineH, detailSize);
  }

  // -- Forecast temps (bitmap components) --
  if (weatherData.forecastCount > 0) {
    int fColW = (w - 60) / 3;
    for (int i = 0; i < weatherData.forecastCount; i++) {
      int fcx = 30 + fColW * i + fColW / 2;
      if ((int)roundf(weatherData.forecast[i].tempMin) == (int)roundf(weatherData.forecast[i].tempMax)) {
        char ftemp[16];
        snprintf(ftemp, sizeof(ftemp), "%.0f", weatherData.forecast[i].tempMin);
        drawValueCentered(ftemp, unitSym, fcx, fDetailY + 135, 24);
      } else {
        char tmin[8], tmax[8];
        snprintf(tmin, sizeof(tmin), "%.0f", weatherData.forecast[i].tempMin);
        snprintf(tmax, sizeof(tmax), "%.0f", weatherData.forecast[i].tempMax);
        int tw = getCharByCharWidth(tmin, 24) + wGetBitmapWidth("~", 24)
               + getCharByCharWidth(tmax, 24) + wGetBitmapWidth(unitSym, 24);
        int xp = fcx - tw / 2;
        xp += drawCharByChar(tmin, xp, fDetailY + 135, 24);
        xp += drawSystemText("~", xp, fDetailY + 135, 24);
        xp += drawCharByChar(tmax, xp, fDetailY + 135, 24);
        drawSystemText(unitSym, xp, fDetailY + 135, 24);
      }
    }
  }

  // -- °C/°F button (bitmap) --
  {
    M5.Display.drawRoundRect(NAV_NEXT_X, NAV_Y, 64, 64, 8, TFT_BLACK);
    const char* unitLabel = (weatherConfig.units == "imperial") ? "°F" : "°C";
    drawSystemTextCentered(unitLabel, NAV_NEXT_X + 32, NAV_Y + 18, 28);
  }

  M5.Display.setEpdMode(epd_mode_t::epd_quality);  // Restore quality mode
  M5.Display.display();
}
