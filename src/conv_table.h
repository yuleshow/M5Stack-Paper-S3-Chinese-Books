#pragma once
#include <Arduino.h>

// ==================== Chinese S2T/T2S Conversion ====================
// Loads binary tables from SD card into PSRAM for runtime character conversion.

// Conversion modes
enum ConvMode { CONV_ORIGINAL = 0, CONV_SIMPLIFIED = 1, CONV_TRADITIONAL = 2 };

// Load both tables from SD card into PSRAM. Call once at boot.
bool loadConvTables();

// Free PSRAM tables.
void freeConvTables();

// Apply conversion to a String in-place based on mode.
// CONV_SIMPLIFIED: uses T2S table (traditional → simplified)
// CONV_TRADITIONAL: uses S2T table (simplified → traditional)
// CONV_ORIGINAL: no-op
void applyConversion(String &text, ConvMode mode);
