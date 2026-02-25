#pragma once

#include <Arduino.h>

// Forward declaration (Mode is defined in globals.h before this file is included)
enum Mode;

// ==================== Cangjie Input Method ====================

// Maximum candidates to show per page
static const int CJ_MAX_CANDIDATES = 200;
// Maximum input code length (Cangjie 5 max = 5)
static const int CJ_MAX_CODE_LEN = 5;
// Candidate bar shows this many chars per page
static const int CJ_CANDIDATES_PER_PAGE = 8;

// Cangjie key labels (A-Z mapped to Cangjie roots)
// Standard Cangjie 5 mapping
static const char* const CJ_KEY_LABELS[] = {
  "日", "月", "金", "木", "水", "火", "土", "竹", "戈", "十",
  "大", "中", "一", "弓", "人", "心", "手", "口", "尸", "廿",
  "山", "女", "田", "難", "卜", "重"
};

// Binary table entry size (5 bytes code + 2 bytes unicode)
static const int CJ_ENTRY_SIZE = 7;

// Cangjie input state (defined in cangjie_input.cpp)
extern char cjInputCode[];
extern int cjInputLen;
extern uint16_t cjCandidates[];
extern int cjCandidateCount;
extern int cjCandidatePage;
extern String cjComposedText;
extern Mode cjReturnMode;
extern int cjReturnPage;

// Cangjie dictionary functions
bool loadCangjieTable();
void freeCangjieTable();
int cangjieSearch(const char* code, uint16_t* results, int maxResults);

// Cangjie input UI functions
void drawCangjieInput();
void cangjieKeyPress(char key);
void cangjieBackspace();
void cangjieSelectCandidate(int index);
void cangjieConfirmInput();
void cangjieCancel();

// Fast partial update (only input bar + candidates, not keyboard)
void updateCangjieInputArea();

