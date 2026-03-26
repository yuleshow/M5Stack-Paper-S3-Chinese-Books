#include "globals.h"
#include "esp_task_wdt.h"

// Web server file manager functions
String formatBytes(size_t bytes) {
  if (bytes < 1024) return String(bytes) + "B";
  else if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + "KB";
  else return String(bytes / 1024.0 / 1024.0, 1) + "MB";
}

String urlDecode(String str) {
  String decoded = "";
  char c;
  for (int i = 0; i < (int)str.length(); i++) {
    c = str.charAt(i);
    if (c == '+') decoded += ' ';
    else if (c == '%' && i + 2 < (int)str.length()) {
      char hex[3] = {str.charAt(i+1), str.charAt(i+2), 0};
      decoded += (char)strtol(hex, NULL, 16);
      i += 2;
    } else decoded += c;
  }
  return decoded;
}

void handleFileList() {
  ScopedSDLock lock;
  String path = "/";
  if (webServer->hasArg("dir")) {
    path = urlDecode(webServer->arg("dir"));
  }
  if (!path.endsWith("/")) path += "/";
  
  Serial.println("Listing directory: " + path);
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>M5Stack File Manager</title>";
  html += "<style>";
  html += "body{font-family:Arial;margin:0;padding:20px;background:#f5f5f5}";
  html += ".header{background:#2196F3;color:#fff;padding:20px;margin:-20px -20px 20px;border-radius:0}";
  html += ".path{background:#fff;padding:10px;margin:10px 0;border-radius:5px;font-family:monospace}";
  html += ".file-list{background:#fff;border-radius:5px;overflow:hidden}";
  html += ".file-item{display:flex;align-items:center;padding:12px;border-bottom:1px solid #eee;transition:background .2s}";
  html += ".file-item:hover{background:#f0f0f0}";
  html += ".file-icon{width:24px;margin-right:10px;font-size:20px}";
  html += ".file-name{flex:1;word-break:break-all}";
  html += ".file-size{color:#666;margin:0 10px;min-width:60px;text-align:right}";
  html += ".btn{padding:8px 15px;margin:0 5px;border:none;border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;font-size:14px}";
  html += ".btn-primary{background:#2196F3;color:#fff}";
  html += ".btn-danger{background:#f44336;color:#fff}";
  html += ".btn-success{background:#4CAF50;color:#fff}";
  html += ".upload-box{background:#fff;padding:20px;margin:20px 0;border-radius:5px}";
  html += "input[type=file]{padding:10px;border:2px dashed #ccc;border-radius:5px;width:100%;box-sizing:border-box}";
  html += "</style></head><body>";
  
  html += "<div class='header'>";
  html += "<h1>📁 M5Stack File Manager</h1>";
  html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
  html += "</div>";
  
  html += "<div class='path'>📍 Current: " + path + "</div>";
  
  // Parent directory link
  if (path != "/") {
    String parent = path;
    parent = parent.substring(0, parent.length() - 1);
    int lastSlash = parent.lastIndexOf('/');
    parent = parent.substring(0, lastSlash + 1);
    html += "<div class='file-list'>";
    html += "<div class='file-item'>";
    html += "<span class='file-icon'>📁</span>";
    html += "<a class='file-name' href='/?dir=" + parent + "'>.. (Parent Directory)</a>";
    html += "</div>";
  } else {
    html += "<div class='file-list'>";
  }
  
  // List directories and files
  // ESP32 SD library may fail with trailing slash on non-root paths
  String openPath = path;
  if (openPath.length() > 1 && openPath.endsWith("/")) {
    openPath = openPath.substring(0, openPath.length() - 1);
  }
  File dir = SD.open(openPath);
  if (dir && dir.isDirectory()) {
    File entry = dir.openNextFile();
    while (entry) {
      String rawName = String(entry.name());
      // ESP32 name() returns full path — extract just the filename
      int lastSlash = rawName.lastIndexOf('/');
      String entryName = (lastSlash >= 0) ? rawName.substring(lastSlash + 1) : rawName;
      String fullPath = path + entryName;
      
      if (entryName.length() > 0) {
        html += "<div class='file-item'>";
        
        if (entry.isDirectory()) {
          html += "<span class='file-icon'>📁</span>";
          html += "<a class='file-name' href='/?dir=" + fullPath + "/'>" + entryName + "/</a>";
          html += "<span class='file-size'>DIR</span>";
        } else {
          html += "<span class='file-icon'>📄</span>";
          html += "<span class='file-name'>" + entryName + "</span>";
          html += "<span class='file-size'>" + formatBytes(entry.size()) + "</span>";
          html += "<a class='btn btn-primary' href='/download?file=" + fullPath + "' download>⬇️</a>";
        }
        
        html += "<a class='btn btn-danger' href='/delete?file=" + fullPath + "&dir=" + path + "' onclick='return confirm(\"Delete " + entryName + "?\")'>🗑️</a>";
        html += "</div>";
      }
      
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  }
  
  html += "</div>";
  
  // Upload form
  html += "<div class='upload-box'>";
  html += "<h3>⬆️ Upload File to " + path + "</h3>";
  html += "<form method='POST' action='/upload?dir=" + path + "' enctype='multipart/form-data'>";
  html += "<input type='file' name='file' required>";
  html += "<button class='btn btn-success' style='margin-top:10px;width:100%'>Upload</button>";
  html += "</form>";
  html += "</div>";
  
  html += "<div style='text-align:center;color:#666;margin-top:20px'>";
  html += "<p>💡 Tip: Upload books to /books/, fonts to /fonts/, wallpapers to /wallpapers/</p>";
  html += "</div>";
  
  html += "</body></html>";
  
  webServer->send(200, "text/html", html);
}

void handleFileUpload() {
  HTTPUpload& upload = webServer->upload();
  static File uploadFile;
  static String uploadPath;
  
  if (upload.status == UPLOAD_FILE_START) {
    String uploadDir = "/";
    if (webServer->hasArg("dir")) {
      uploadDir = urlDecode(webServer->arg("dir"));
    }
    if (!uploadDir.endsWith("/")) uploadDir += "/";
    
    uploadPath = uploadDir + upload.filename;
    Serial.printf("Upload Start: [%s] (%d chars)\n", uploadPath.c_str(), uploadPath.length());
    
    // Hold SD mutex for the entire upload duration (open → write → close)
    // to prevent other tasks from accessing SD and corrupting the write.
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
    // Remove existing file first to ensure clean write
    if (SD.exists(uploadPath)) {
      SD.remove(uploadPath);
      Serial.printf("Removed existing file: %s\n", uploadPath.c_str());
    }
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.printf("Failed to open file for writing: %s\n", uploadPath.c_str());
      if (sdMutex) xSemaphoreGive(sdMutex);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        Serial.printf("Upload write error: wanted %u, wrote %u\n", upload.currentSize, written);
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.flush();
      size_t finalSize = uploadFile.size();
      uploadFile.close();
      Serial.printf("Upload Complete: %s, %u bytes (on SD: %u)\n",
                    uploadPath.c_str(), upload.totalSize, finalSize);
      if (finalSize != upload.totalSize) {
        Serial.printf("Upload SIZE MISMATCH: expected %u, got %u on SD!\n",
                      upload.totalSize, finalSize);
      }
      if (sdMutex) xSemaphoreGive(sdMutex);
    } else {
      Serial.println("Upload END but file was not open");
      // Mutex was already released in UPLOAD_FILE_START on open failure
    }
  }
}

void handleFileDownload() {
  ScopedSDLock lock;
  if (!webServer->hasArg("file")) {
    webServer->send(400, "text/plain", "Missing file parameter");
    return;
  }
  
  String filepath = urlDecode(webServer->arg("file"));
  Serial.println("Download request: " + filepath);
  
  File file = SD.open(filepath, FILE_READ);
  if (!file || file.isDirectory()) {
    webServer->send(404, "text/plain", "File not found");
    return;
  }
  
  // Determine content type
  String contentType = "application/octet-stream";
  if (filepath.endsWith(".txt")) contentType = "text/plain";
  else if (filepath.endsWith(".html")) contentType = "text/html";
  else if (filepath.endsWith(".css")) contentType = "text/css";
  else if (filepath.endsWith(".js")) contentType = "application/javascript";
  else if (filepath.endsWith(".json")) contentType = "application/json";
  else if (filepath.endsWith(".jpg") || filepath.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (filepath.endsWith(".png")) contentType = "image/png";
  else if (filepath.endsWith(".gif")) contentType = "image/gif";
  else if (filepath.endsWith(".pdf")) contentType = "application/pdf";
  
  webServer->streamFile(file, contentType);
  file.close();
  Serial.println("Download complete");
}

void handleFileDelete() {
  if (!webServer->hasArg("file")) {
    webServer->send(400, "text/plain", "Missing file parameter");
    return;
  }
  
  String filepath = urlDecode(webServer->arg("file"));
  String returnDir = "/";
  if (webServer->hasArg("dir")) {
    returnDir = urlDecode(webServer->arg("dir"));
  }
  
  Serial.printf("Delete request: [%s] (%d chars)\n", filepath.c_str(), filepath.length());
  
  bool exists = false;
  bool isDir = false;
  {
    ScopedSDLock lock;
    exists = SD.exists(filepath);
    if (exists) {
      File file = SD.open(filepath);
      if (file) {
        isDir = file.isDirectory();
        file.close();
      }
    }
  }
  
  if (!exists) {
    Serial.printf("Delete: file not found [%s]\n", filepath.c_str());
    webServer->send(404, "text/plain", "File not found: " + filepath);
    return;
  }
  
  bool success = false;
  {
    ScopedSDLock lock;
    if (isDir) {
      success = SD.rmdir(filepath);
    } else {
      success = SD.remove(filepath);
    }
  }
  
  Serial.printf("Delete %s: %s\n", filepath.c_str(), success ? "OK" : "FAILED");
  
  if (success) {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='1;url=/?dir=" + returnDir + "'>";
    html += "</head><body><h2>✅ Deleted!</h2><p>Redirecting...</p></body></html>";
    webServer->send(200, "text/html", html);
  } else {
    webServer->send(500, "text/plain", "Delete failed: " + filepath);
  }
}

void handleUploadComplete() {
  String returnDir = "/";
  if (webServer->hasArg("dir")) {
    returnDir = urlDecode(webServer->arg("dir"));
  }
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='1;url=/?dir=" + returnDir + "'>";
  html += "</head><body><h2>✅ Upload Complete!</h2><p>Redirecting...</p></body></html>";
  
  webServer->send(200, "text/html", html);
}

// ===== Screen Capture =====
// Non-blocking deferred capture: reads IT8951 VRAM one chunk per main-loop
// iteration so HTTP requests are still processed during the slow SPI read.

static uint8_t* cachedBmp = nullptr;
static volatile bool captureRequested = false;
static bool captureReady = false;

// Non-blocking state machine
static bool captureInProgress = false;
static int captureY = 0;
static uint8_t* captureRgbChunk = nullptr;
static unsigned long captureStartTime = 0;

static const int SCR_W = 540;
static const int SCR_H = 960;
static const int SCR_STRIDE = (SCR_W + 3) & ~3;  // 540
static const int CAPTURE_CHUNK = 32;              // rows per iteration
static const int BMP_HDR_SZ = 14 + 40;
static const int BMP_PAL_SZ = 256 * 4;
static const int BMP_IMG_SZ = SCR_STRIDE * SCR_H;
static const int BMP_TOTAL  = BMP_HDR_SZ + BMP_PAL_SZ + BMP_IMG_SZ;

static bool initScreenBuffer() {
  if (cachedBmp) return true;
  cachedBmp = (uint8_t*)ps_malloc(BMP_TOTAL);
  if (!cachedBmp) {
    sdLog("Screenshot: PSRAM alloc failed (%d bytes, free=%u)",
          BMP_TOTAL, (unsigned)ESP.getFreePsram());
    return false;
  }
  // BMP file header
  memset(cachedBmp, 0, BMP_HDR_SZ);
  cachedBmp[0] = 'B'; cachedBmp[1] = 'M';
  *(uint32_t*)(cachedBmp + 2)  = BMP_TOTAL;
  *(uint32_t*)(cachedBmp + 10) = BMP_HDR_SZ + BMP_PAL_SZ;
  *(uint32_t*)(cachedBmp + 14) = 40;
  *(int32_t*)(cachedBmp + 18)  = SCR_W;
  *(int32_t*)(cachedBmp + 22)  = SCR_H;
  *(uint16_t*)(cachedBmp + 26) = 1;
  *(uint16_t*)(cachedBmp + 28) = 8;
  *(uint32_t*)(cachedBmp + 34) = BMP_IMG_SZ;
  *(int32_t*)(cachedBmp + 38)  = 2835;
  *(int32_t*)(cachedBmp + 42)  = 2835;
  *(uint32_t*)(cachedBmp + 46) = 256;
  // Grayscale palette
  for (int i = 0; i < 256; i++) {
    cachedBmp[BMP_HDR_SZ + i * 4]     = i;
    cachedBmp[BMP_HDR_SZ + i * 4 + 1] = i;
    cachedBmp[BMP_HDR_SZ + i * 4 + 2] = i;
    cachedBmp[BMP_HDR_SZ + i * 4 + 3] = 0;
  }
  memset(cachedBmp + BMP_HDR_SZ + BMP_PAL_SZ, 0xFF, BMP_IMG_SZ);
  sdLog("Screenshot: BMP buffer allocated (%d bytes PSRAM)", BMP_TOTAL);
  return true;
}

static void freeScreenBuffer() {
  if (captureRgbChunk) {
    free(captureRgbChunk);
    captureRgbChunk = nullptr;
  }
  captureInProgress = false;
  captureY = 0;
  if (cachedBmp) {
    free(cachedBmp);
    cachedBmp = nullptr;
    captureReady = false;
    captureRequested = false;
  }
}

// Called every main-loop iteration. Reads one chunk (32 rows) per call so
// the loop stays responsive for HTTP handling between chunks.
void updateScreenCapture() {
  if (!cachedBmp) return;

  // ── Start a new capture ──
  if (captureRequested && !captureInProgress) {
    captureRequested = false;

    // If display is still refreshing, defer to next iteration
    if (M5.Display.displayBusy()) {
      captureRequested = true;
      return;
    }

    captureRgbChunk = (uint8_t*)ps_malloc(SCR_W * 3 * CAPTURE_CHUNK);
    if (!captureRgbChunk) {
      sdLog("Capture: rgbChunk alloc failed psram=%u", (unsigned)ESP.getFreePsram());
      return;
    }

    captureInProgress = true;
    captureY = 0;
    captureStartTime = millis();
    sdLog("Capture: start mode=%d heap=%u psram=%u",
          (int)currentMode, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  }

  // ── Process one chunk ──
  if (!captureInProgress) return;

  int rows = min(CAPTURE_CHUNK, SCR_H - captureY);
  M5.Display.readRectRGB(0, captureY, SCR_W, rows, captureRgbChunk);

  uint8_t* pixels = cachedBmp + BMP_HDR_SZ + BMP_PAL_SZ;
  for (int r = 0; r < rows; r++) {
    int bmpRow = SCR_H - 1 - (captureY + r);
    uint8_t* src = captureRgbChunk + r * SCR_W * 3;
    uint8_t* dst = pixels + bmpRow * SCR_STRIDE;
    for (int x = 0; x < SCR_W; x++) {
      dst[x] = (uint8_t)((src[x*3] * 77 + src[x*3+1] * 150 + src[x*3+2] * 29) >> 8);
    }
  }
  esp_task_wdt_reset();

  captureY += rows;

  if (captureY >= SCR_H) {
    free(captureRgbChunk);
    captureRgbChunk = nullptr;
    captureInProgress = false;
    captureReady = true;
    sdLog("Capture: done %lu ms", millis() - captureStartTime);
  }
}

static void serveCachedBmp() {
  WiFiClient client = webServer->client();
  client.setTimeout(30);
  webServer->setContentLength(BMP_TOTAL);
  webServer->send(200, "image/bmp", "");
  int sent = 0;
  while (sent < BMP_TOTAL && client.connected()) {
    int chunk = min(4096, BMP_TOTAL - sent);
    size_t written = client.write(cachedBmp + sent, chunk);
    if (written == 0) break;
    sent += written;
    yield();
  }
  Serial.printf("Screenshot: served %d/%d bytes\n", sent, BMP_TOTAL);
}

void handleScreenshot() {
  sdLog("Screenshot: req ready=%d inProg=%d y=%d heap=%u psram=%u mode=%d",
        (int)captureReady, (int)captureInProgress, captureY,
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), (int)currentMode);

  // Test mode: fill buffer with gradient, serve immediately
  if (webServer->hasArg("test")) {
    if (!cachedBmp && !initScreenBuffer()) {
      webServer->send(503, "text/plain", "Screenshot: memory allocation failed");
      return;
    }
    uint8_t* pixels = cachedBmp + BMP_HDR_SZ + BMP_PAL_SZ;
    for (int y = 0; y < SCR_H; y++) {
      uint8_t gray = (uint8_t)((y * 255) / SCR_H);
      memset(pixels + (SCR_H - 1 - y) * SCR_STRIDE, gray, SCR_W);
    }
    captureReady = true;
    serveCachedBmp();
    return;
  }

  // If cached VRAM image is ready, serve it
  if (cachedBmp && captureReady) {
    serveCachedBmp();
    captureReady = false;
    captureRequested = true;
    return;
  }

  // Capture in progress — report progress
  if (captureInProgress) {
    char buf[64];
    snprintf(buf, sizeof(buf), "capturing %d%%", captureY * 100 / SCR_H);
    webServer->send(202, "text/plain", buf);
    return;
  }

  // Try blocking VRAM capture if buffer is available
  if (cachedBmp || initScreenBuffer()) {
    captureRequested = true;
    if (!captureRgbChunk) {
      captureRgbChunk = (uint8_t*)ps_malloc(SCR_W * 3 * CAPTURE_CHUNK);
    }
    if (captureRgbChunk) {
      captureInProgress = true;
      captureY = 0;
      captureStartTime = millis();
      sdLog("Capture: blocking start heap=%u psram=%u",
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
      uint8_t* pixels = cachedBmp + BMP_HDR_SZ + BMP_PAL_SZ;
      for (int y = 0; y < SCR_H; y += CAPTURE_CHUNK) {
        int rows = min(CAPTURE_CHUNK, SCR_H - y);
        M5.Display.readRectRGB(0, y, SCR_W, rows, captureRgbChunk);
        for (int r = 0; r < rows; r++) {
          int bmpRow = SCR_H - 1 - (y + r);
          uint8_t* src = captureRgbChunk + r * SCR_W * 3;
          uint8_t* dst = pixels + bmpRow * SCR_STRIDE;
          for (int xx = 0; xx < SCR_W; xx++) {
            dst[xx] = (uint8_t)((src[xx*3]*77 + src[xx*3+1]*150 + src[xx*3+2]*29) >> 8);
          }
        }
        esp_task_wdt_reset();
        yield();
      }
      captureInProgress = false;
      captureReady = true;
      captureRequested = false;
      sdLog("Capture: blocking done %lu ms", millis() - captureStartTime);
      serveCachedBmp();
      captureReady = false;
      captureRequested = true;
      return;
    }
  }

  webServer->send(503, "text/plain", "Screenshot: no capture available");
}

void handleScreenView() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>M5Paper S3 Screen</title>";
  html += "<style>";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; ";
  html += "background: #1a1a2e; color: #e0e0e0; margin: 0; padding: 20px; ";
  html += "display: flex; flex-direction: column; align-items: center; }";
  html += "h1 { color: #00d4ff; margin-bottom: 5px; }";
  html += ".info { color: #888; font-size: 14px; margin-bottom: 15px; }";
  html += ".controls { margin-bottom: 15px; display: flex; gap: 10px; flex-wrap: wrap; align-items: center; justify-content: center; }";
  html += "button { background: #16213e; color: #00d4ff; border: 1px solid #00d4ff; ";
  html += "padding: 8px 20px; border-radius: 6px; cursor: pointer; font-size: 14px; }";
  html += "button:hover { background: #00d4ff; color: #1a1a2e; }";
  html += "button.active { background: #00d4ff; color: #1a1a2e; }";
  html += "select { background: #16213e; color: #e0e0e0; border: 1px solid #444; ";
  html += "padding: 8px; border-radius: 6px; font-size: 14px; }";
  html += ".frame { background: #222; border-radius: 12px; padding: 12px; ";
  html += "box-shadow: 0 4px 20px rgba(0,0,0,0.5); display: inline-block; }";
  html += "img { display: block; image-rendering: pixelated; max-height: 85vh; ";
  html += "border-radius: 4px; }";
  html += "#status { font-size: 12px; color: #666; margin-top: 8px; }";
  html += "</style></head><body>";
  html += "<h1>\xF0\x9F\x93\xB1 M5Paper S3 Screen</h1>";
  html += "<div class='info'>540 x 960 E-Ink Display</div>";
  html += "<div class='controls'>";
  html += "<button onclick='capture()'>Capture</button>";
  html += "<button onclick='captureTest()'>Test</button>";
  html += "<button id='autoBtn' onclick='toggleAuto()'>Auto</button>";
  html += "<select id='interval' onchange='resetAuto()'>";
  html += "<option value='5'>5s</option><option value='10' selected>10s</option>";
  html += "<option value='30'>30s</option><option value='60'>60s</option></select>";
  html += "<button onclick='download()'>Save</button>";
  html += "<select id='fmt'><option value='png'>PNG</option><option value='jpg'>JPG</option></select>";
  html += "</div>";
  html += "<div class='frame'><img id='screen' alt='Screen capture'></div>";
  html += "<div id='status'>Loading...</div>";
  html += "<script>";
  html += "let timer=null,autoOn=false,busy=false;";
  html += "function setStatus(msg){document.getElementById('status').textContent=msg;}";
  html += "function doCapture(url,attempt){";
  html += "  if(busy)return;busy=true;attempt=attempt||1;";
  html += "  setStatus('Requesting capture...');";
  html += "  fetch(url).then(function(r){";
  html += "    if(r.status===202){";
  html += "      return r.text().then(function(t){";
  html += "        busy=false;";
  html += "        setStatus(t||'capturing...');";
  html += "        setTimeout(function(){doCapture(url,attempt+1);},1000);";
  html += "        return null;";
  html += "      });";
  html += "    }";
  html += "    if(!r.ok)throw new Error('HTTP '+r.status);";
  html += "    return r.blob();";
  html += "  }).then(function(blob){";
  html += "    if(!blob)return;";
  html += "    let img=document.getElementById('screen');";
  html += "    let old=img.src;";
  html += "    img.src=URL.createObjectURL(blob);";
  html += "    if(old&&old.startsWith('blob:'))URL.revokeObjectURL(old);";
  html += "    busy=false;";
  html += "    setStatus('Captured: '+new Date().toLocaleTimeString()+' ('+Math.round(blob.size/1024)+'KB)');";
  html += "  }).catch(function(e){";
  html += "    busy=false;";
  html += "    setStatus('Error: '+e.message);";
  html += "    if(attempt<30)setTimeout(function(){doCapture(url,attempt+1);},2000);";
  html += "  });";
  html += "}";
  html += "function capture(){doCapture('/screenshot?t='+Date.now());}";
  html += "function captureTest(){doCapture('/screenshot?test=1&t='+Date.now());}";
  html += "function toggleAuto(){";
  html += "  autoOn=!autoOn;";
  html += "  let btn=document.getElementById('autoBtn');";
  html += "  if(autoOn){btn.textContent='Stop';btn.classList.add('active');startAuto();}";
  html += "  else{btn.textContent='Auto';btn.classList.remove('active');clearInterval(timer);}";
  html += "}";
  html += "function startAuto(){";
  html += "  clearInterval(timer);";
  html += "  let sec=parseInt(document.getElementById('interval').value);";
  html += "  capture();";
  html += "  timer=setInterval(capture, sec*1000);";
  html += "}";
  html += "function resetAuto(){if(autoOn)startAuto();}";
  html += "function download(){";
  html += "  let fmt=document.getElementById('fmt').value;";
  html += "  let mime=fmt==='jpg'?'image/jpeg':'image/png';";
  html += "  let img=document.getElementById('screen');";
  html += "  let c=document.createElement('canvas');c.width=img.naturalWidth;c.height=img.naturalHeight;";
  html += "  c.getContext('2d').drawImage(img,0,0);";
  html += "  c.toBlob(function(b){";
  html += "    let a=document.createElement('a');a.href=URL.createObjectURL(b);";
  html += "    a.download='m5paper_'+Date.now()+'.'+fmt;a.click();URL.revokeObjectURL(a.href);";
  html += "  },mime,0.95);";
  html += "}";
  html += "capture();";
  html += "</script></body></html>";

  webServer->send(200, "text/html", html);
}

// ==================== SD Card Debug Logger ====================
// Appends timestamped lines to /debug.log on SD card.
// Safe to call from any context; silently fails if SD unavailable.
void sdLog(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (len <= 0) return;

  // Also mirror to Serial
  Serial.printf("[LOG] %s\n", buf);

  if (!sdCardAvailable) return;

  // Build timestamp prefix
  char line[320];
  unsigned long ms = millis();
  unsigned long sec = ms / 1000;
  int h = (sec / 3600) % 24;
  int m = (sec / 60) % 60;
  int s = sec % 60;
  int lineLen = snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03lu] %s\n",
                         h, m, s, ms % 1000, buf);

  File f = SD.open("/debug.log", FILE_APPEND);
  if (f) {
    f.write((const uint8_t*)line, lineLen);
    f.close();
  }
}

void handleDebugLog() {
  ScopedSDLock lock;
  File f = SD.open("/debug.log", FILE_READ);
  if (!f) {
    webServer->send(200, "text/plain", "(no log file yet)");
    return;
  }
  webServer->setContentLength(f.size());
  webServer->send(200, "text/plain", "");
  uint8_t buf[512];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n > 0) webServer->client().write(buf, n);
  }
  f.close();
}

void handleClearLog() {
  {
    ScopedSDLock lock;
    SD.remove("/debug.log");
  }
  webServer->send(200, "text/plain", "Log cleared");
}

void startWebServer() {
  sdLog("startWebServer: WiFi.status=%d, heap=%u, psram=%u",
        (int)WiFi.status(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot start web server: WiFi not connected");
    webServerRunning = false;
    return;
  }
  
  if (webServer == nullptr) {
    webServer = new WebServer(80);
    webServer->on("/", HTTP_GET, handleFileList);
    webServer->on("/upload", HTTP_POST, handleUploadComplete, handleFileUpload);
    webServer->on("/download", HTTP_GET, handleFileDownload);
    webServer->on("/delete", HTTP_GET, handleFileDelete);
    webServer->on("/screenshot", HTTP_GET, handleScreenshot);
    webServer->on("/screen", HTTP_GET, handleScreenView);
    webServer->on("/log", HTTP_GET, handleDebugLog);
    webServer->on("/log/clear", HTTP_GET, handleClearLog);
  }
  
  webServer->begin();
  webServerRunning = true;
  sdLog("startWebServer: OK at %s", WiFi.localIP().toString().c_str());
}

void stopWebServer() {
  if (webServer != nullptr) {
    sdLog("stopWebServer: WiFi.status=%d", (int)WiFi.status());
    webServer->stop();
    webServerRunning = false;
    freeScreenBuffer();
    Serial.println("Web server stopped");
  }
}
