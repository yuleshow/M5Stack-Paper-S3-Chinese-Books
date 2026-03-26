#include "globals.h"

// ==================== WiFi Configuration Functions ====================

void loadWiFiConfig() {
  wifiConfig.configured = false;
  wifiConfig.ssid = "";
  wifiConfig.password = "";
  timeConfig.timezone = "CST-8";
  timeConfig.gmtOffset = 28800;
  timeConfig.timeSynced = false;
  weatherConfig.configured = false;
  weatherConfig.units = "metric";
  weatherConfig.city = "";
  weatherConfig.apiKey = "";
  
  if (!sdCardAvailable) {
    Serial.println("SD card not available for WiFi config");
    return;
  }
  
  File configFile = SD.open("/config.ini", FILE_READ);
  if (!configFile) {
    Serial.println("config.ini not found");
    return;
  }
  
  Serial.println("Loading config from config.ini");
  String section = "";
  int lineCount = 0;
  while (configFile.available()) {
    String line = configFile.readStringUntil('\n');
    line.trim();
    lineCount++;
    if (lineCount > 200) { Serial.println("Config too long, stopping"); break; }
    if (lineCount % 20 == 0) yield();
    
    if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) {
      continue;
    }
    
    if (line.startsWith("[") && line.endsWith("]")) {
      section = line.substring(1, line.length() - 1);
      section.toLowerCase();
      continue;
    }
    
    int eqPos = line.indexOf('=');
    if (eqPos > 0) {
      String key = line.substring(0, eqPos);
      String value = line.substring(eqPos + 1);
      key.trim();
      value.trim();
      key.toLowerCase();
      
      if (section == "wifi") {
        if (key == "ssid") {
          wifiConfig.ssid = value;
        } else if (key == "password") {
          wifiConfig.password = value;
        }
      } else if (section == "time") {
        if (key == "timezone") {
          timeConfig.timezone = value;
        } else if (key == "gmtoffset") {
          timeConfig.gmtOffset = value.toInt();
        }
      } else if (section == "weather") {
        if (key == "apikey") {
          weatherConfig.apiKey = value;
        } else if (key == "city") {
          weatherConfig.city = value;
        } else if (key == "units") {
          weatherConfig.units = value;
        }
      } else if (section == "unlock") {
        if (key == "enabled") {
          bleUnlockConfig.enabled = (value == "true" || value == "1" || value == "yes");
        } else if (key == "password") {
          bleUnlockConfig.password = value;
        } else if (key == "device_name") {
          bleUnlockConfig.deviceName = value;
        }
      } else if (section == "medicine") {
        if (key == "passcode") {
          if (value.length() > 0 && value.length() <= 8) {
            medPasscode = value;
          }
        }
      }
    }
  }
  
  configFile.close();
  
  if (wifiConfig.ssid.length() > 0) {
    wifiConfig.configured = true;
    Serial.println("WiFi config loaded: SSID=" + wifiConfig.ssid);
  }
  if (weatherConfig.apiKey.length() > 0 && weatherConfig.city.length() > 0) {
    weatherConfig.configured = true;
    Serial.printf("Weather config loaded: city=%s, units=%s\n",
      weatherConfig.city.c_str(), weatherConfig.units.c_str());
  }
  Serial.println("Timezone: " + timeConfig.timezone);
  
  // BLE Unlock config
  if (bleUnlockConfig.enabled && bleUnlockConfig.password.length() > 0) {
    Serial.printf("BLE Unlock config loaded: device=%s\n",
      bleUnlockConfig.deviceName.c_str());
  }
  
  // Med passcode
  if (medPasscode.length() > 0) {
    Serial.printf("Med passcode loaded (%d digits)\n", medPasscode.length());
  }
  
  for (int i = 0; i < timezoneCount; i++) {
    if (timeConfig.timezone == String(timezones[i].tzString)) {
      selectedTimezone = i;
      Serial.printf("Matched timezone index: %d (%s)\n", i, timezones[i].name);
      break;
    }
  }
}

void saveWiFiConfig() {
  if (!sdCardAvailable) {
    Serial.println("SD card not available for saving config");
    return;
  }
  
  if (SD.exists("/config.ini")) {
    SD.remove("/config.ini");
    Serial.println("Removed old config.ini");
  }
  
  File configFile = SD.open("/config.ini", FILE_WRITE);
  if (!configFile) {
    Serial.println("Failed to create config.ini - check SD card and directory");
    return;
  }
  
  Serial.println("Writing config to file...");
  Serial.println("  SSID: " + wifiConfig.ssid);
  Serial.println("  Password length: " + String(wifiConfig.password.length()));
  Serial.println("  Timezone: " + timeConfig.timezone);
  Serial.println("  GMT Offset: " + String(timeConfig.gmtOffset));
  
  configFile.println("# M5Stack Paper S3 Configuration");
  configFile.println();
  configFile.println("[wifi]");
  configFile.println("ssid=" + wifiConfig.ssid);
  configFile.println("password=" + wifiConfig.password);
  configFile.println();
  configFile.println("[time]");
  configFile.println("timezone=" + timeConfig.timezone);
  configFile.println("gmtoffset=" + String(timeConfig.gmtOffset));
  configFile.println();
  configFile.println("[weather]");
  configFile.println("apikey=" + weatherConfig.apiKey);
  configFile.println("city=" + weatherConfig.city);
  configFile.println("units=" + weatherConfig.units);
  configFile.println();
  configFile.println("[unlock]");
  configFile.println("enabled=" + String(bleUnlockConfig.enabled ? "true" : "false"));
  configFile.println("password=" + bleUnlockConfig.password);
  configFile.println("device_name=" + bleUnlockConfig.deviceName);
  configFile.println();
  if (medPasscode.length() > 0) {
    configFile.println("[medicine]");
    configFile.println("passcode=" + medPasscode);
    configFile.println();
  }
  
  configFile.flush();
  configFile.close();
  wifiConfig.configured = true;
  
  Serial.println("✓ Config saved successfully to /config.ini");
}

void scanWiFiNetworks() {
  networkCount = 0;
  wifiScanning = true;
  
  WiFi.mode(WIFI_STA);
  
  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    Serial.println("No networks found");
  } else {
    Serial.printf("Found %d networks\n", n);
    for (int i = 0; i < n && i < MAX_WIFI_NETWORKS; i++) {
      scannedNetworks[networkCount].ssid = WiFi.SSID(i);
      scannedNetworks[networkCount].rssi = WiFi.RSSI(i);
      scannedNetworks[networkCount].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      networkCount++;
    }
  }
  
  wifiScanning = false;
}

void syncTimeNTP() {
  Serial.println("Syncing time via NTP...");
  Serial.println("Timezone: " + timeConfig.timezone);
  
  configTzTime(timeConfig.timezone.c_str(), ntpServer);
  
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 10) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println();
  
  if (getLocalTime(&timeinfo)) {
    timeConfig.timeSynced = true;
    Serial.println("Time synchronized!");
    Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    m5::rtc_datetime_t rtcTime;
    rtcTime.date.year = timeinfo.tm_year + 1900;
    rtcTime.date.month = timeinfo.tm_mon + 1;
    rtcTime.date.date = timeinfo.tm_mday;
    rtcTime.date.weekDay = timeinfo.tm_wday;
    rtcTime.time.hours = timeinfo.tm_hour;
    rtcTime.time.minutes = timeinfo.tm_min;
    rtcTime.time.seconds = timeinfo.tm_sec;
    
    M5.Rtc.setDateTime(&rtcTime);
    Serial.println("✓ RTC time updated from NTP");
  } else {
    Serial.println("Failed to sync time");
    timeConfig.timeSynced = false;
  }
}

bool connectToWiFi() {
  if (wifiConfig.ssid.length() == 0) {
    Serial.println("No WiFi SSID configured");
    return false;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  
  Serial.println("Connecting to WiFi: " + wifiConfig.ssid);
  Serial.println("Password length: " + String(wifiConfig.password.length()));
  WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_CONNECT_ATTEMPTS) {
    delay(500);
    yield();
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  Serial.printf("WiFi connect took %d attempts (%d ms)\n", attempts, attempts * 500);
  
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);  // Auto-reconnect if connection drops
    Serial.println("WiFi connected!");
    Serial.println("IP address: " + WiFi.localIP().toString());
    syncTimeNTP();
    return true;
  } else {
    Serial.println("WiFi connection failed");
    return false;
  }
}

String getCurrentTimeString() {
  if (!timeConfig.timeSynced) {
    return "--:--";
  }
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "--:--";
  }
  
  char timeStr[32];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  return String(timeStr);
}

String getCurrentDateString() {
  if (!timeConfig.timeSynced) {
    return "----/--/--";
  }
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "----/--/--";
  }
  
  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%04d/%02d/%02d", 
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  return String(dateStr);
}
