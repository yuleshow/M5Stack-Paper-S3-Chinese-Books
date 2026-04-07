#pragma once

// BLE Proximity Unlock
// Paper S3 acts as a BLE proximity beacon using the existing UART service.
// The macOS companion script monitors RSSI and types the password locally
// when the device is detected nearby — no BLE HID keyboard needed.

#include <Arduino.h>

// Configuration
struct BLEUnlockConfig {
  bool enabled;
  String password;   // stored for future use; companion handles typing
  String deviceName;
};

extern BLEUnlockConfig bleUnlockConfig;

void bleUnlockInit();
void bleUnlockStop();
bool isBleUnlockActive();
