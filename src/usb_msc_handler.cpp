#include "globals.h"
#include "tusb.h"
#include "driver/periph_ctrl.h"

// USB MSC callbacks for direct SD card sector access
static int32_t onMSCWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (!sdCard) return -1;
  
  // Write directly to SD card sectors
  uint32_t sector_count = (bufsize + 511) / 512;
  esp_err_t err = sdmmc_write_sectors(sdCard, buffer, lba, sector_count);
  
  return (err == ESP_OK) ? bufsize : -1;
}

static int32_t onMSCRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (!sdCard) return -1;
  
  // Read directly from SD card sectors
  uint32_t sector_count = (bufsize + 511) / 512;
  esp_err_t err = sdmmc_read_sectors(sdCard, buffer, lba, sector_count);
  
  return (err == ESP_OK) ? bufsize : -1;
}

static bool onMSCStartStop(uint8_t power_condition, bool start, bool load_eject) {
  Serial.printf("MSC: power=%d, start=%d, eject=%d\n", power_condition, start, load_eject);
  return true;
}

// USB Mass Storage functions
void startUSBMSC() {
  if (usbMSCActive) {
    Serial.println("USB MSC already active");
    return;
  }
  
  if (!sdCardAvailable) {
    Serial.println("Cannot start USB MSC: SD card not available");
    return;
  }
  
  Serial.println("Starting USB Mass Storage...");
  Serial.println("Preparing SD card for direct access");
  
  // End high-level SD access
  SD.end();
  delay(500);
  
  // Configure SPI bus
  spi_bus_config_t bus_cfg = {
    .mosi_io_num = 38,
    .miso_io_num = 40,
    .sclk_io_num = 39,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 4092,
  };
  
  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("❌ Failed to init SPI bus: %d\n", ret);
    sdSPI.begin(39, 40, 38, 47);
    SD.begin(47, sdSPI, 25000000);
    return;
  }
  
  // Initialize SD card at low level using ESP-IDF
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = (gpio_num_t)47;
  slot_config.host_id = (spi_host_device_t)host.slot;
  
  sdCard = (sdmmc_card_t*)malloc(sizeof(sdmmc_card_t));
  if (!sdCard) {
    Serial.println("❌ Failed to allocate sdmmc_card_t");
    spi_bus_free(SPI2_HOST);
    sdSPI.begin(39, 40, 38, 47);
    SD.begin(47, sdSPI, 25000000);
    return;
  }
  
  ret = sdspi_host_init();
  if (ret != ESP_OK) {
    Serial.printf("❌ Failed to init SDSPI host: %d\n", ret);
    free(sdCard);
    sdCard = nullptr;
    spi_bus_free(SPI2_HOST);
    sdSPI.begin(39, 40, 38, 47);
    SD.begin(47, sdSPI, 25000000);
    return;
  }
  
  ret = sdspi_host_init_device(&slot_config, &host.slot);
  if (ret != ESP_OK) {
    Serial.printf("❌ Failed to init SDSPI device: %d\n", ret);
    sdspi_host_deinit();
    free(sdCard);
    sdCard = nullptr;
    spi_bus_free(SPI2_HOST);
    sdSPI.begin(39, 40, 38, 47);
    SD.begin(47, sdSPI, 25000000);
    return;
  }
  
  ret = sdmmc_card_init(&host, sdCard);
  if (ret != ESP_OK) {
    Serial.printf("❌ Failed to init SD card: %d\n", ret);
    sdspi_host_remove_device(host.slot);
    sdspi_host_deinit();
    free(sdCard);
    sdCard = nullptr;
    spi_bus_free(SPI2_HOST);
    sdSPI.begin(39, 40, 38, 47);
    SD.begin(47, sdSPI, 25000000);
    return;
  }
  
  uint32_t sector_count = sdCard->csd.capacity;
  Serial.printf("SD Card: %u sectors (%llu MB)\n", sector_count, (uint64_t)sector_count * 512 / 1024 / 1024);
  
  // Initialize USB
  USB.productName("M5Stack Paper S3");
  USB.manufacturerName("M5Stack");
  USB.serialNumber("123456");
  USB.begin();
  
  // Configure MSC
  msc.vendorID("M5Stack");
  msc.productID("Paper SD");
  msc.productRevision("1.0");
  msc.onRead(onMSCRead);
  msc.onWrite(onMSCWrite);
  msc.onStartStop(onMSCStartStop);
  msc.mediaPresent(true);
  msc.begin(sector_count, 512);
  
  usbMSCActive = true;
  sdCardAvailable = false;  // Not available to device while USB active
  Serial.println("✓ USB MSC started - SD card exposed to computer");
  Serial.println("⚠️  Device cannot access SD card until MSC is stopped");
}

// --- Safe Preferences helpers (always open/close namespace properly) ---
void savePrefInt(const char* ns, const char* key, int value) {
  prefs.begin(ns, false);
  prefs.putInt(key, value);
  prefs.end();
}

void savePrefBool(const char* ns, const char* key, bool value) {
  prefs.begin(ns, false);
  prefs.putBool(key, value);
  prefs.end();
}

int loadPrefInt(const char* ns, const char* key, int defaultVal) {
  prefs.begin(ns, true);
  int val = prefs.getInt(key, defaultVal);
  prefs.end();
  return val;
}

bool loadPrefBool(const char* ns, const char* key, bool defaultVal) {
  prefs.begin(ns, true);
  bool val = prefs.getBool(key, defaultVal);
  prefs.end();
  return val;
}

void stopUSBMSC() {
  if (!usbMSCActive) {
    Serial.println("USB MSC not active");
    return;
  }
  
  Serial.println("Stopping USB Mass Storage...");
  
  // Save state before restarting
  savePrefBool("m5paper", "usbMSC", false);
  
  // End USB MSC
  msc.end();
  usbMSCActive = false;
  
  // Disconnect USB OTG from host
  tud_disconnect();
  delay(100);
  
  // Disable USB OTG peripheral at hardware level so esp_restart()
  // can complete cleanly on ESP32-S3.
  periph_module_disable(PERIPH_USB_MODULE);
  delay(100);
  
  esp_restart();
}

void drawUSBMSCSetup() {
  Serial.println("Drawing USB MSC setup screen...");
  
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  if (!usbMSCActive) {
    drawReturnButton();
  }
  
  // Title
  drawSystemText("USB 外接磁碟", 20, 30, 40);
  
  // Current status
  if (usbMSCActive) {
    drawSystemText("狀態: ", 20, 120, 32);
    drawSystemText("執行中", 160, 120, 32, EPD_DARK_GRAY);
    
    drawSystemText("SD 卡已連接到電腦", 20, 180, 24);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 210);
    M5.Display.print("SD card connected to computer");
    
    drawSystemText("裝置無法存取 SD 卡", 20, 250, 24, EPD_DARK_GRAY);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 280);
    M5.Display.print("Device cannot access SD card");
    
    drawSystemText("關閉將重新啟動裝置", 20, 320, 24, EPD_DARK_GRAY);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 350);
    M5.Display.print("Disabling will restart device");
  } else {
    drawSystemText("狀態: ", 20, 120, 32);
    drawSystemText("未啟用", 160, 120, 32, TFT_DARKGRAY);
    
    drawSystemText("將整張 SD 卡作為 USB 磁碟", 20, 180, 24);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 210);
    M5.Display.print("Expose entire SD card as USB drive");
  }
  
  // Toggle button (full width)
  int btnY = 400;
  if (usbMSCActive) {
    M5.Display.fillRect(20, btnY, 500, 90, TFT_BLACK);
    drawSystemTextCentered("關閉 USB 磁碟", 270, btnY + 28, 36, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Display.fillRect(20, btnY, 500, 90, EPD_DARK_GRAY);
    drawSystemTextCentered("啟用 USB 磁碟", 270, btnY + 28, 36, TFT_WHITE, EPD_DARK_GRAY);
  }
  
  // Info section — large, filling the lower half
  int infoY = 530;
  M5.Display.drawRect(20, infoY, 500, 370, TFT_BLACK);
  
  drawSystemText("說明", 30, infoY + 15, 32);
  M5.Display.drawLine(20, infoY + 55, 520, infoY + 55, EPD_LIGHT_GRAY);
  
  drawSystemText("完整 SD 卡存取", 40, infoY + 75, 28);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(40, infoY + 110);
  M5.Display.print("Full SD card access");
  
  drawSystemText("可讀寫所有檔案", 40, infoY + 155, 28);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(40, infoY + 190);
  M5.Display.print("Read/write all files");
  
  drawSystemText("關閉將重新啟動", 40, infoY + 245, 28, EPD_DARK_GRAY);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(40, infoY + 280);
  M5.Display.print("Disabling will restart device");
  
  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("USB MSC setup displayed");
}
