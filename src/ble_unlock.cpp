// BLE Proximity Unlock — Beacon Mode
//
// The Paper S3 simply starts the existing BLE UART service for advertising.
// The macOS companion script monitors RSSI to detect proximity and handles
// password typing locally on the Mac — no BLE HID keyboard needed.
//
// Config (config.ini on SD card):
//   [unlock]
//   enabled=true
//   password=YOUR_MAC_PASSWORD
//   device_name=M5Paper-BLE

#include "globals.h"
#include "ble_unlock.h"

// ── Globals ──────────────────────────────────────────────────────
BLEUnlockConfig bleUnlockConfig = { false, "", "M5Paper-BLE" };
static bool bleUnlockRunning = false;

// ── Initialize: just start the existing BLE UART service ────────
void bleUnlockInit() {
  if (bleUnlockRunning) return;
  if (!bleUnlockConfig.enabled) {
    Serial.println("BLE Unlock: disabled");
    return;
  }

  Serial.printf("BLE Unlock: starting BLE beacon as '%s'...\n",
    bleUnlockConfig.deviceName.c_str());

  // Use the existing BLE UART service — it already works reliably
  if (!bluetoothActive) {
    startBLE();
  }

  bleUnlockRunning = true;
  Serial.println("BLE Unlock: beacon active (companion will handle unlock)");
}

void bleUnlockStop() {
  if (!bleUnlockRunning) return;

  stopBLE();
  bleUnlockRunning = false;
  Serial.println("BLE Unlock: stopped");
}

bool isBleUnlockActive() {
  return bleUnlockRunning;
}
