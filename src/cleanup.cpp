#include "globals.h"

// Delete macOS dot files from SD card (recursive)
void deleteDotFiles(const String& path) {
  File dir = SD.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    return;
  }
  
  File file = dir.openNextFile();
  while (file) {
    String rawName = String(file.name());
    
    // ESP32 SD library may return name without leading '/'
    // Build the correct absolute path from parent path + basename
    String filename = rawName;
    int lastSlash = rawName.lastIndexOf('/');
    if (lastSlash >= 0) {
      filename = rawName.substring(lastSlash + 1);
    }
    
    // Build full absolute path: parent + "/" + filename
    String fullPath;
    if (path.endsWith("/")) {
      fullPath = path + filename;
    } else {
      fullPath = path + "/" + filename;
    }
    
    bool isDir = file.isDirectory();
    file.close();
    
    if (isDir) {
      // Recursively clean subdirectory
      deleteDotFiles(fullPath);
      
      // Delete empty dot directories like .Trashes, .Spotlight-V100
      if (filename.startsWith(".")) {
        if (SD.rmdir(fullPath.c_str())) {
          Serial.printf("Deleted dot dir: %s\n", fullPath.c_str());
        }
      }
    } else {
      // Check if it's a dot file or AppleDouble file
      bool shouldDelete = false;
      
      if (filename.startsWith(".")) {
        // Standard dot files: .DS_Store, .localized, .hidden, etc.
        shouldDelete = true;
      } else if (filename.startsWith("._")) {
        // AppleDouble resource fork files
        shouldDelete = true;
      } else if (filename == "Thumbs.db" || filename == "desktop.ini") {
        // Windows junk files
        shouldDelete = true;
      }
      
      if (shouldDelete) {
        if (SD.remove(fullPath.c_str())) {
          Serial.printf("Deleted: %s\n", fullPath.c_str());
        } else {
          Serial.printf("Failed to delete: %s\n", fullPath.c_str());
        }
      }
    }
    
    // Get next file (don't reopen directory!)
    file = dir.openNextFile();
  }
  
  dir.close();
}

void cleanupMacOSFiles() {
  if (!sdCardAvailable) {
    Serial.println("⚠️  SD card not available for cleanup");
    return;
  }
  
  Serial.println("🧹 Cleaning up macOS/Windows junk files...");
  Serial.println("   (dot files, AppleDouble, Thumbs.db, etc.)");
  
  unsigned long startTime = millis();
  deleteDotFiles("/");
  unsigned long elapsed = millis() - startTime;
  
  Serial.printf("✅ Cleanup complete in %lu ms\n", elapsed);
}
