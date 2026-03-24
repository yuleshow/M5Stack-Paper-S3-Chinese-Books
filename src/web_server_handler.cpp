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
// Serves the current e-ink framebuffer as a BMP image via HTTP

void handleScreenshot() {
  int w = M5.Display.width();   // 540
  int h = M5.Display.height();  // 960

  // BMP row stride must be multiple of 4 bytes (8-bit grayscale: stride = w rounded up)
  int rowStride = (w + 3) & ~3;
  int imageSize = rowStride * h;
  int paletteSize = 256 * 4;  // 256 RGBX entries
  int headerSize = 14 + 40;   // BMP file header + DIB header
  int fileSize = headerSize + paletteSize + imageSize;

  size_t freeHeap = ESP.getFreeHeap();
  size_t freePsram = ESP.getFreePsram();
  Serial.printf("Screenshot: freeHeap=%u, freePsram=%u\n", (unsigned)freeHeap, (unsigned)freePsram);

  if (freeHeap < 8192) {
    Serial.printf("Screenshot: insufficient heap (%u bytes), aborting\n", (unsigned)freeHeap);
    webServer->send(503, "text/plain", "Screenshot: insufficient memory, try again later");
    return;
  }

  // Allocate full grayscale image buffer in PSRAM (~520KB for 540x960)
  uint8_t* imgBuf = (uint8_t*)ps_malloc(imageSize);
  if (!imgBuf) {
    Serial.println("Screenshot: PSRAM allocation failed");
    webServer->send(503, "text/plain", "Screenshot: PSRAM allocation failed");
    return;
  }
  memset(imgBuf, 0xFF, imageSize);  // default white

  // Allocate RGB chunk buffer in PSRAM (multiple rows at once)
  const int READ_CHUNK = 32;
  uint8_t* rgbChunk = (uint8_t*)ps_malloc(w * 3 * READ_CHUNK);
  if (!rgbChunk) {
    free(imgBuf);
    webServer->send(503, "text/plain", "Screenshot: row buffer allocation failed");
    return;
  }

  // Wait for e-ink refresh to complete before reading the framebuffer
  {
    unsigned long busyStart = millis();
    while (M5.Display.displayBusy()) {
      delay(50);
      esp_task_wdt_reset();
      if (millis() - busyStart > 8000) {
        Serial.println("Screenshot: display busy timeout, proceeding anyway");
        break;
      }
    }
  }

  // ── Phase 1: Read display into PSRAM buffer in multi-row chunks ──
  // Each readRectRGB call reads READ_CHUNK rows in a single readRect call,
  // which only allocates/frees IT8951 buffers once per chunk (~30 times total
  // instead of 960 single-row calls).
  Serial.println("Screenshot: reading display...");
  unsigned long readStart = millis();
  bool readOK = true;
  for (int y = 0; y < h; y += READ_CHUNK) {
    int rows = min(READ_CHUNK, h - y);
    M5.Display.readRectRGB(0, y, w, rows, rgbChunk);
    // Convert RGB to grayscale in BMP bottom-up order
    for (int r = 0; r < rows; r++) {
      int srcRow = r;
      int bmpRow = h - 1 - (y + r);
      uint8_t* src = rgbChunk + srcRow * w * 3;
      uint8_t* dst = imgBuf + bmpRow * rowStride;
      for (int x = 0; x < w; x++) {
        dst[x] = (uint8_t)((src[x * 3] * 77 + src[x * 3 + 1] * 150 + src[x * 3 + 2] * 29) >> 8);
      }
    }
    esp_task_wdt_reset();
    yield();
    // Abort if reading takes too long (prevent system halt)
    if (millis() - readStart > 30000) {
      Serial.printf("Screenshot: read timeout at row %d, aborting\n", y);
      readOK = false;
      break;
    }
  }
  free(rgbChunk);
  Serial.printf("Screenshot: display read %s in %lu ms\n",
                readOK ? "complete" : "partial", millis() - readStart);

  // ── Phase 2: Build and send BMP over WiFi (no SPI needed) ──
  uint8_t bmpHeader[14 + 40];
  memset(bmpHeader, 0, sizeof(bmpHeader));
  bmpHeader[0] = 'B'; bmpHeader[1] = 'M';
  *((uint32_t*)&bmpHeader[2])  = fileSize;
  *((uint32_t*)&bmpHeader[10]) = headerSize + paletteSize;
  *((uint32_t*)&bmpHeader[14]) = 40;
  *((int32_t*)&bmpHeader[18])  = w;
  *((int32_t*)&bmpHeader[22])  = h;
  *((uint16_t*)&bmpHeader[26]) = 1;
  *((uint16_t*)&bmpHeader[28]) = 8;
  *((uint32_t*)&bmpHeader[30]) = 0;
  *((uint32_t*)&bmpHeader[34]) = imageSize;
  *((int32_t*)&bmpHeader[38])  = 2835;
  *((int32_t*)&bmpHeader[42])  = 2835;
  *((uint32_t*)&bmpHeader[46]) = 256;
  *((uint32_t*)&bmpHeader[50]) = 0;

  uint8_t palette[256 * 4];
  for (int i = 0; i < 256; i++) {
    palette[i * 4 + 0] = i;
    palette[i * 4 + 1] = i;
    palette[i * 4 + 2] = i;
    palette[i * 4 + 3] = 0;
  }

  webServer->setContentLength(fileSize);
  webServer->send(200, "image/bmp", "");

  WiFiClient client = webServer->client();
  client.write(bmpHeader, sizeof(bmpHeader));
  client.write(palette, sizeof(palette));

  // Send image data in chunks with WDT feeds
  const int CHUNK_ROWS = 64;
  for (int startRow = 0; startRow < h; startRow += CHUNK_ROWS) {
    if (!client.connected()) {
      Serial.printf("Screenshot: client disconnected at row %d\n", startRow);
      break;
    }
    int rows = min(CHUNK_ROWS, h - startRow);
    client.write(imgBuf + startRow * rowStride, rows * rowStride);
    esp_task_wdt_reset();
    yield();
  }

  free(imgBuf);
  Serial.println("Screenshot sent successfully");
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
  html += ".controls { margin-bottom: 15px; display: flex; gap: 10px; align-items: center; }";
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
  html += "<h1>📱 M5Paper S3 Screen</h1>";
  html += "<div class='info'>540 × 960 E-Ink Display</div>";
  html += "<div class='controls'>";
  html += "<button onclick='capture()'>📸 Capture</button>";
  html += "<button id='autoBtn' onclick='toggleAuto()'>▶ Auto</button>";
  html += "<select id='interval' onchange='resetAuto()'>";
  html += "<option value='5'>5s</option><option value='10' selected>10s</option>";
  html += "<option value='30'>30s</option><option value='60'>60s</option></select>";
  html += "<button onclick='download()'>💾 Save</button>";
  html += "<select id='fmt'><option value='png'>PNG</option><option value='jpg'>JPG</option></select>";
  html += "</div>";
  html += "<div class='frame'><img id='screen' src='/screenshot' alt='Screen capture'></div>";
  html += "<div id='status'>Ready</div>";
  html += "<script>";
  html += "let timer=null, autoOn=false;";
  html += "function capture(){";
  html += "  let img=document.getElementById('screen');";
  html += "  img.src='/screenshot?t='+Date.now();";
  html += "  document.getElementById('status').textContent='Captured: '+new Date().toLocaleTimeString();";
  html += "}";
  html += "function toggleAuto(){";
  html += "  autoOn=!autoOn;";
  html += "  let btn=document.getElementById('autoBtn');";
  html += "  if(autoOn){btn.textContent='⏸ Stop';btn.classList.add('active');startAuto();}";
  html += "  else{btn.textContent='▶ Auto';btn.classList.remove('active');clearInterval(timer);}";
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
  html += "</script></body></html>";

  webServer->send(200, "text/html", html);
}

void startWebServer() {
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
  }
  
  webServer->begin();
  webServerRunning = true;
  Serial.print("Web server started at http://");
  Serial.println(WiFi.localIP());
}

void stopWebServer() {
  if (webServer != nullptr) {
    webServer->stop();
    webServerRunning = false;
    Serial.println("Web server stopped");
  }
}
