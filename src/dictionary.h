#pragma once
#include <Arduino.h>

// Word position tracking for dictionary tap in English reading mode
struct WordPos {
  int16_t x, y, w, h;
  int16_t poolOffset;  // Offset into engWordPool for null-terminated word
};

static const int MAX_ENG_WORDS = 512;
static const int ENG_WORD_POOL_SIZE = 4096;

extern WordPos engWordPositions[];
extern int engWordCount;
extern char engWordPool[];
extern int engWordPoolLen;

// Clear word positions (call at start of each page render)
void engClearWords();

// Record word position during English rendering
void engRecordWord(int x, int y, int w, int h, const char* word);

// Find word index at screen coordinates, returns index or -1
int engFindWordAt(int tx, int ty);

// Look up word in dictionary file on SD card (/dict/en-zh.txt)
// Returns true if found, fills definition buffer
bool dictLookup(const char* word, char* definition, int maxLen);

// Draw dictionary popup overlay on current screen
void drawDictPopup(const char* word, const char* definition);
