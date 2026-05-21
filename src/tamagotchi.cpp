// Tamagotchi easter egg — hidden behind 歌子靈籖 on the fortune slips menu.
// A tiny virtual pet named 歌子 that lives on the device. State is persisted to
// /tamagotchi.dat on the SD card and stats decay according to wall-clock time,
// so the pet keeps "living" between sessions.
//
// Mechanics:
//   * Stats: hunger, happiness, cleanliness, health (sick flag), discipline,
//     weight, age, poop count, care score.
//   * Care score accumulates good/bad events (timely feeding, play, cleaning,
//     healing, neglect) and drives the evolution branch.
//   * Evolution tree:
//       egg → baby → (good|bad child) → (A/B/C/D adult) → elder
//   * Sickness: triggered by leaving poops uncleaned, starvation, or very low
//     happiness.  Medicine clears it; ignoring it drains health every tick
//     until death.
//   * Four actions: 餵食 / 逗玩 / 清掃 / 醫治.
//
// Intentionally self-contained: no images, drawn with primitives + system font.

#include "globals.h"
#include <time.h>
#include <math.h>
#include <esp_random.h>

static const char* TAMAGOTCHI_FILE = "/tamagotchi.dat";
// Bump magic whenever the struct layout changes so stale files reset cleanly.
static const uint32_t TAMAGOTCHI_MAGIC = 0x47544D35;  // "5MTG"

// Stat decay cadence (wall-clock seconds)
static const uint32_t HUNGER_TICK_SEC   = 600;   // +1 hunger every 10 min
static const uint32_t HAPPY_TICK_SEC    = 900;   // -1 happy every 15 min
static const uint32_t POOP_TICK_SEC     = 1200;  // +1 poop risk every 20 min
static const uint32_t DIRTY_TICK_SEC    = 1800;  // -1 cleanliness every 30 min
static const uint32_t SICK_DRAIN_SEC    = 3600;  // -1 health every hour while sick
static const uint32_t HEAL_COOLDOWN_SEC = 600;   // 10 min immunity after medicine
static uint32_t lastHealEpoch = 0;                // wall-clock when medicine was last given

// Stage thresholds (seconds since birth)
static const uint32_t STAGE_BABY_AT   = 60 * 30;          // 30 min
static const uint32_t STAGE_CHILD_AT  = 60 * 60 * 3;      // 3 h
static const uint32_t STAGE_TEEN_AT   = 60 * 60 * 12;     // 12 h
static const uint32_t STAGE_ADULT_AT  = 60 * 60 * 48;     // 2 d
static const uint32_t STAGE_ELDER_AT  = 60 * 60 * 24 * 7; // 7 d

enum TamaStage {
  STAGE_EGG   = 0,
  STAGE_BABY  = 1,
  STAGE_CHILD = 2,   // branch: good (2) or bad (12)
  STAGE_CHILD_BAD = 12,
  STAGE_TEEN  = 3,
  STAGE_TEEN_BAD = 13,
  STAGE_ADULT_A = 4,   // best: scholar  士
  STAGE_ADULT_B = 14,  // good: artisan  工
  STAGE_ADULT_C = 24,  // mid:  merchant 商
  STAGE_ADULT_D = 34,  // bad:  wanderer 野
  STAGE_ELDER   = 5,
  STAGE_DEAD    = 99,
};

#pragma pack(push, 1)
struct TamaState {
  uint32_t magic;
  uint32_t birthEpoch;      // unix time when egg was created
  uint32_t lastTickEpoch;
  uint8_t  hunger;          // 0 = full, 10 = starving
  uint8_t  happiness;       // 0 = sad, 10 = cheerful
  uint8_t  cleanliness;     // 0 = filthy, 10 = spotless
  uint8_t  health;          // 0 = dead, 10 = strong
  uint8_t  discipline;      // 0 = spoiled, 10 = well-trained
  uint8_t  poopCount;       // number of piles on screen (0..3)
  uint8_t  stage;           // TamaStage
  uint8_t  mood;            // 0=neutral,1=happy,2=hungry,3=sleepy,4=sick,5=dirty
  uint8_t  sick;            // 1 if currently ill
  int16_t  careScore;       // can go negative; drives evolution branch
  uint16_t feedCount;
  uint16_t playCount;
  uint16_t cleanCount;
  uint16_t healCount;
  uint8_t  reserved[6];
};
#pragma pack(pop)

static TamaState tama;
static bool      tamaLoaded = false;
static uint32_t  lastPollMs = 0;

// Shake-to-play state (same debouncing pattern as fortune_slips.cpp).
static const float         TAMA_SHAKE_THRESHOLD    = 2.0f;  // G deviation
static const int           TAMA_SHAKE_COUNT_NEEDED = 3;
static const unsigned long TAMA_SHAKE_WINDOW_MS    = 1500;
static const unsigned long TAMA_SHAKE_COOLDOWN_MS  = 2500;  // longer than poll/redraw
static int                 tamaShakeCount    = 0;
static unsigned long       tamaShakeWindow   = 0;
static unsigned long       tamaLastShakeTime = 0;
static bool                tamaAboveThresh   = false;

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return (t > 0) ? (uint32_t)t : 0;
}

static void tamaReset() {
  memset(&tama, 0, sizeof(tama));
  tama.magic         = TAMAGOTCHI_MAGIC;
  tama.birthEpoch    = nowEpoch();
  tama.lastTickEpoch = tama.birthEpoch;
  tama.hunger        = 3;
  tama.happiness     = 7;
  tama.cleanliness   = 10;
  tama.health        = 10;
  tama.discipline    = 5;
  tama.poopCount     = 0;
  tama.stage         = STAGE_EGG;
  tama.mood          = 0;
  tama.sick          = 0;
  tama.careScore     = 0;
}

static bool tamaSave() {
  if (!sdCardAvailable) return false;
  File f = SD.open(TAMAGOTCHI_FILE, FILE_WRITE);
  if (!f) return false;
  f.write((const uint8_t*)&tama, sizeof(tama));
  f.close();
  return true;
}

static bool tamaLoad() {
  if (!sdCardAvailable) return false;
  File f = SD.open(TAMAGOTCHI_FILE, FILE_READ);
  if (!f) return false;
  if (f.size() < sizeof(TamaState)) { f.close(); return false; }
  f.read((uint8_t*)&tama, sizeof(tama));
  f.close();
  return tama.magic == TAMAGOTCHI_MAGIC;
}

// Determine adult branch from care score at adulthood transition.
static uint8_t pickAdultStage(int16_t care) {
  if (care >=  40) return STAGE_ADULT_A;   // 士 scholar
  if (care >=  10) return STAGE_ADULT_B;   // 工 artisan
  if (care >= -20) return STAGE_ADULT_C;   // 商 merchant
  return                 STAGE_ADULT_D;    // 野 wanderer
}

// Apply accumulated real-time decay based on wall-clock delta since last tick.
// The pet "sleeps" when unattended — decay is capped at 2 hours max to prevent
// overnight death.  This means the pet can survive ~8h+ of neglect, waking up
// hungry and dirty but still alive.
static const uint32_t MAX_DECAY_SEC = 7200;  // 2 hours cap

static void tamaUpdateMood() {
  if (tama.stage == STAGE_DEAD) return;
  if      (tama.sick)              tama.mood = 4;  // sick
  else if (tama.poopCount >= 2 ||
           tama.cleanliness <= 2)  tama.mood = 5;  // dirty
  else if (tama.hunger >= 7)       tama.mood = 2;  // hungry
  else if (tama.happiness <= 3)    tama.mood = 3;  // sad/sleepy
  else if (tama.happiness >= 8)    tama.mood = 1;  // happy
  else                             tama.mood = 0;  // neutral
}

static void tamaTick() {
  if (tama.stage == STAGE_DEAD) return;
  uint32_t now = nowEpoch();
  if (now == 0 || tama.lastTickEpoch == 0 || now <= tama.lastTickEpoch) {
    tama.lastTickEpoch = (now > 0) ? now : tama.lastTickEpoch;
    tamaUpdateMood();
    return;
  }
  uint32_t rawDelta = now - tama.lastTickEpoch;
  uint32_t delta = (rawDelta > MAX_DECAY_SEC) ? MAX_DECAY_SEC : rawDelta;

  uint32_t hungerSteps = delta / HUNGER_TICK_SEC;
  uint32_t happySteps  = delta / HAPPY_TICK_SEC;
  uint32_t poopSteps   = delta / POOP_TICK_SEC;
  uint32_t dirtySteps  = delta / DIRTY_TICK_SEC;
  uint32_t sickSteps   = delta / SICK_DRAIN_SEC;

  // Hunger ↑
  if (hungerSteps > 0) {
    int oldHunger = (int)tama.hunger;
    int h = oldHunger + (int)hungerSteps;
    tama.hunger = (h > 10) ? 10 : (uint8_t)h;
    // Starvation: only penalize for steps spent at max hunger
    int starvingSteps = (h >= 10) ? (int)hungerSteps - (10 - oldHunger) : 0;
    if (starvingSteps < 0) starvingSteps = 0;
    if (starvingSteps > 0) {
      tama.careScore -= (int16_t)starvingSteps;
      int hp = (int)tama.health - starvingSteps;
      tama.health = (hp < 0) ? 0 : (uint8_t)hp;
    }
  }
  // Happiness ↓
  if (happySteps > 0) {
    int oldHappy = (int)tama.happiness;
    int p = oldHappy - (int)happySteps;
    tama.happiness = (p < 0) ? 0 : (uint8_t)p;
    // Only penalize for steps spent at zero happiness
    int sadSteps = (p < 0) ? (int)happySteps - oldHappy : 0;
    if (sadSteps > 0) tama.careScore -= (int16_t)sadSteps;
  }
  // Cleanliness ↓ (accelerated when poops present — use pre-decay count)
  if (dirtySteps > 0) {
    int c = (int)tama.cleanliness - (int)dirtySteps - (int)tama.poopCount;
    tama.cleanliness = (c < 0) ? 0 : (uint8_t)c;
  }
  // Poops ↑ (one pile per POOP_TICK_SEC, max 3 on screen at once)
  if (poopSteps > 0 && tama.stage != STAGE_EGG) {
    int pc = (int)tama.poopCount + (int)poopSteps;
    tama.poopCount = (pc > 3) ? 3 : (uint8_t)pc;
  }

  // Sickness onset — skip if recently healed (immunity window)
  bool wasSick = tama.sick;
  if (!tama.sick) {
    bool immune = (lastHealEpoch > 0 && now - lastHealEpoch < HEAL_COOLDOWN_SEC);
    if (!immune) {
      bool filthy  = tama.cleanliness <= 1 && tama.poopCount >= 2;
      bool starved = tama.hunger     >= 10;
      bool miser   = tama.happiness  == 0 && tama.cleanliness <= 3;
      if (filthy || starved || miser) {
        tama.sick = 1;
        tama.careScore -= 5;
      }
    }
  }
  // Sickness drain — only if already sick before this tick (no retroactive drain)
  if (wasSick && tama.sick && sickSteps > 0) {
    int hp = (int)tama.health - (int)sickSteps;
    tama.health = (hp < 0) ? 0 : (uint8_t)hp;
    tama.careScore -= (int16_t)sickSteps;
  }

  // Advance last-tick forward.  When delta was capped (pet was sleeping),
  // jump all the way to now so the skipped time doesn't re-apply next tick.
  if (rawDelta > MAX_DECAY_SEC) {
    tama.lastTickEpoch = now;
  } else {
    // Normal: advance by the largest consumed chunk so partial ticks accumulate.
    uint32_t consumed = 0;
    if (hungerSteps * HUNGER_TICK_SEC > consumed) consumed = hungerSteps * HUNGER_TICK_SEC;
    if (happySteps  * HAPPY_TICK_SEC  > consumed) consumed = happySteps  * HAPPY_TICK_SEC;
    if (poopSteps   * POOP_TICK_SEC   > consumed) consumed = poopSteps   * POOP_TICK_SEC;
    if (dirtySteps  * DIRTY_TICK_SEC  > consumed) consumed = dirtySteps  * DIRTY_TICK_SEC;
    if (sickSteps   * SICK_DRAIN_SEC  > consumed) consumed = sickSteps   * SICK_DRAIN_SEC;
    tama.lastTickEpoch += consumed;
  }

  // Death
  if (tama.health == 0) {
    tama.stage = STAGE_DEAD;
    return;
  }

  // Stage progression based on age and care score
  uint32_t age = now - tama.birthEpoch;
  uint8_t newStage = tama.stage;
  if (tama.stage == STAGE_EGG && age >= STAGE_BABY_AT)   newStage = STAGE_BABY;
  else if ((tama.stage == STAGE_BABY) && age >= STAGE_CHILD_AT) {
    newStage = (tama.careScore < -5) ? STAGE_CHILD_BAD : STAGE_CHILD;
  }
  else if ((tama.stage == STAGE_CHILD || tama.stage == STAGE_CHILD_BAD) && age >= STAGE_TEEN_AT) {
    newStage = (tama.careScore < 0) ? STAGE_TEEN_BAD : STAGE_TEEN;
  }
  else if ((tama.stage == STAGE_TEEN || tama.stage == STAGE_TEEN_BAD) && age >= STAGE_ADULT_AT) {
    newStage = pickAdultStage(tama.careScore);
  }
  else if ((tama.stage == STAGE_ADULT_A || tama.stage == STAGE_ADULT_B ||
            tama.stage == STAGE_ADULT_C || tama.stage == STAGE_ADULT_D) && age >= STAGE_ELDER_AT) {
    newStage = STAGE_ELDER;
  }
  tama.stage = newStage;

  tamaUpdateMood();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// 16x16 pixel-art sprites, classic Tamagotchi-LCD style.  '#' = black pixel,
// anything else = blank.  Upscaled at render time by `scale` to make chunky
// e-ink-friendly artwork.
static const int SPRITE_W = 16;
static const int SPRITE_H = 16;

// Each sprite is 16 rows of 16 characters.
static const char* const SPRITE_EGG[SPRITE_H] = {
  "................",
  ".....######.....",
  "....########....",
  "...##########...",
  "..############..",
  "..############..",
  ".##############.",
  ".##############.",
  ".##############.",
  ".##############.",
  "..############..",
  "..############..",
  "...##########...",
  "....########....",
  ".....######.....",
  "................",
};

static const char* const SPRITE_BABY[SPRITE_H] = {
  "................",
  "......##........",
  ".....##.........",
  "....##..........",
  "...####...###...",
  "..######.####...",
  ".##.###..####...",
  ".##.####.####...",
  ".##.####.####...",
  ".###########....",
  ".###########....",
  "..#########.....",
  "...#######......",
  "...##...##......",
  "...##...##......",
  "................",
};

static const char* const SPRITE_CHILD[SPRITE_H] = {
  "................",
  "..##........##..",
  ".####......####.",
  ".######..######.",
  "..############..",
  "..####.##.####..",
  "..####.##.####..",
  "..############..",
  "..############..",
  "..############..",
  "..###.####.###..",
  "..############..",
  "..##........##..",
  "..##........##..",
  "..##........##..",
  "..##........##..",
};

static const char* const SPRITE_CHILD_BAD[SPRITE_H] = {
  "................",
  "...#..##..#.....",
  "...##.##.##.....",
  "...########.....",
  "..##########....",
  ".############...",
  ".##.######.##...",
  ".##.######.##...",
  ".############...",
  ".############...",
  ".###.####.###...",
  ".############...",
  "..##......##....",
  "..##......##....",
  "..##......##....",
  "..##......##....",
};

static const char* const SPRITE_TEEN[SPRITE_H] = {
  "................",
  "....######......",
  "....######......",
  "...########.....",
  "..##########....",
  ".##.######.##...",
  ".##.######.##...",
  ".############...",
  ".###.####.###...",
  ".############...",
  "..##########....",
  "...########.....",
  "...##....##.....",
  "...##....##.....",
  "...##....##.....",
  "...##....##.....",
};

static const char* const SPRITE_TEEN_BAD[SPRITE_H] = {
  "................",
  "....######......",
  "...########.....",
  "..##########....",
  "..##.####.##....",
  "..##.####.##....",
  "..##########....",
  ".############...",
  ".#.########.#...",
  "..##########....",
  "..##########....",
  "...########.....",
  "....##...##.....",
  "....##...##.....",
  "....##...##.....",
  "...###...##.....",
};

// 士人 — scholar with tall cap (掛帽)
static const char* const SPRITE_ADULT_A[SPRITE_H] = {
  ".....######.....",
  ".....######.....",
  "....########....",
  "...##########...",
  "..############..",
  ".##############.",
  "......####......",
  "....########....",
  "...##########...",
  "..##.######.##..",
  "..##.######.##..",
  "..############..",
  "..###.####.###..",
  "..############..",
  "...##......##...",
  "...##......##...",
};

// 工匠 — artisan gripping a hammer
static const char* const SPRITE_ADULT_B[SPRITE_H] = {
  "................",
  "....######......",
  "...########.....",
  "..##########....",
  ".##.######.##...",
  ".##.######.##...",
  ".############...",
  ".############.##",
  ".############.##",
  ".############.##",
  ".###.####.###.##",
  ".############.##",
  "..##########..##",
  "..##......##....",
  "..##......##....",
  "..##......##....",
};

// 商賈 — merchant with a round belly and a coin
static const char* const SPRITE_ADULT_C[SPRITE_H] = {
  "................",
  "....######......",
  "...########.....",
  "..##########....",
  ".##.######.##...",
  ".##.######.##...",
  ".############...",
  ".############...",
  "##############..",
  "###########.###.",
  "###########.###.",
  "############.##.",
  "##############..",
  ".############...",
  "...##......##...",
  "...##......##...",
};

// 野人 — wanderer with scar, leaning on a staff
static const char* const SPRITE_ADULT_D[SPRITE_H] = {
  "...............#",
  "....######.....#",
  "...########....#",
  "..##########...#",
  "..##.####.##..#.",
  "..##.####.##..#.",
  ".#############..",
  ".##########.##..",
  ".##########..#..",
  "..##########....",
  "..##########....",
  "...########.....",
  "....##...##.....",
  "....##...##.....",
  "....##...##.....",
  "....##....##....",
};

// 長者 — elder, hunched with flowing beard
static const char* const SPRITE_ELDER[SPRITE_H] = {
  "................",
  ".....######.....",
  "....########....",
  "...##########...",
  "..##.####.##....",
  "..##.####.##....",
  "..##########....",
  "..##########....",
  "...########.....",
  "....######......",
  "....######......",
  "....#.##.#......",
  "...##.##.##.....",
  "...##.##.##.....",
  "...##....##.....",
  "...##....##.....",
};

// Small overlay patches for mood.  (px, py) are sprite-space coordinates at
// which to stamp the overlay; blank-char ('.') leaves the underlying sprite
// untouched.  The sprites were authored with "neutral + happy" faces; mood
// overlays alter eye and mouth patches in-place.
struct MoodPatch {
  int px, py;
  const char* rows[3];
};

// Sick X-eyes — stamped at typical eye location (col 3, row 5) spanning 10 cols
static const MoodPatch MOOD_SICK = {
  3, 5, {
    "#.#....#.#",
    ".#......#.",
    "#.#....#.#",
  }
};

// Sleepy — horizontal eye lines
static const MoodPatch MOOD_SLEEPY = {
  3, 6, {
    "##.#..##.#",
    "..........",
    "..........",
  }
};

// Hungry — open mouth (O) under the face
static const MoodPatch MOOD_HUNGRY = {
  6, 12, {
    "####",
    "#..#",
    "####",
  }
};

// Dirty — wavy mouth
static const MoodPatch MOOD_DIRTY = {
  5, 12, {
    "#.#.#.",
    ".#.#.#",
    "......",
  }
};

static void drawSprite16(int cx, int cy, const char* const* rows, int scale) {
  int w = SPRITE_W * scale;
  int h = SPRITE_H * scale;
  int ox = cx - w / 2;
  int oy = cy - h / 2;
  for (int r = 0; r < SPRITE_H; r++) {
    const char* row = rows[r];
    for (int c = 0; c < SPRITE_W; c++) {
      if (row[c] == '#') {
        M5.Display.fillRect(ox + c * scale, oy + r * scale, scale, scale, TFT_BLACK);
      }
    }
  }
}

static void stampMoodPatch(int cx, int cy, const MoodPatch& patch, int scale) {
  int w = SPRITE_W * scale;
  int h = SPRITE_H * scale;
  int ox = cx - w / 2;
  int oy = cy - h / 2;
  for (int r = 0; patch.rows[r] != nullptr && r < 3; r++) {
    const char* row = patch.rows[r];
    for (int c = 0; row[c]; c++) {
      int sx = ox + (patch.px + c) * scale;
      int sy = oy + (patch.py + r) * scale;
      if (row[c] == '#') {
        M5.Display.fillRect(sx, sy, scale, scale, TFT_BLACK);
      } else if (row[c] == '.') {
        // clear pixel (erase part of base sprite)
        M5.Display.fillRect(sx, sy, scale, scale, TFT_WHITE);
      }
      // space = leave alone
    }
  }
}

static void drawPetSprite(int cx, int cy, uint8_t stage, uint8_t mood) {
  if (stage == STAGE_DEAD) {
    // Tombstone — drawn with primitives, not a sprite
    int w = 180, h = 220;
    M5.Display.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 24, EPD_LIGHT_GRAY);
    M5.Display.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 24, TFT_BLACK);
    drawSystemTextCentered("歿", cx, cy - h / 4, 80);
    return;
  }

  // Scale chosen so the whole cast fits in roughly 160x160 px.
  int scale;
  switch (stage) {
    case STAGE_EGG:  scale = 10; break;
    case STAGE_BABY: scale =  8; break;
    case STAGE_CHILD:
    case STAGE_CHILD_BAD: scale = 9; break;
    case STAGE_TEEN:
    case STAGE_TEEN_BAD:  scale = 10; break;
    case STAGE_ADULT_A:
    case STAGE_ADULT_B:
    case STAGE_ADULT_C:
    case STAGE_ADULT_D:   scale = 11; break;
    case STAGE_ELDER:     scale = 10; break;
    default:              scale = 10; break;
  }

  const char* const* sprite = SPRITE_BABY;
  switch (stage) {
    case STAGE_EGG:         sprite = SPRITE_EGG;       break;
    case STAGE_BABY:        sprite = SPRITE_BABY;      break;
    case STAGE_CHILD:       sprite = SPRITE_CHILD;     break;
    case STAGE_CHILD_BAD:   sprite = SPRITE_CHILD_BAD; break;
    case STAGE_TEEN:        sprite = SPRITE_TEEN;      break;
    case STAGE_TEEN_BAD:    sprite = SPRITE_TEEN_BAD;  break;
    case STAGE_ADULT_A:     sprite = SPRITE_ADULT_A;   break;
    case STAGE_ADULT_B:     sprite = SPRITE_ADULT_B;   break;
    case STAGE_ADULT_C:     sprite = SPRITE_ADULT_C;   break;
    case STAGE_ADULT_D:     sprite = SPRITE_ADULT_D;   break;
    case STAGE_ELDER:       sprite = SPRITE_ELDER;     break;
  }

  drawSprite16(cx, cy, sprite, scale);

  // Mood overlays — only for creature forms (not egg).
  if (stage != STAGE_EGG) {
    if      (mood == 4) stampMoodPatch(cx, cy, MOOD_SICK,   scale);
    else if (mood == 3) stampMoodPatch(cx, cy, MOOD_SLEEPY, scale);
    else if (mood == 2) stampMoodPatch(cx, cy, MOOD_HUNGRY, scale);
    else if (mood == 5) stampMoodPatch(cx, cy, MOOD_DIRTY,  scale);
  }

  // Sickness aura: tildes above the head
  if (mood == 4) {
    int half = SPRITE_W * scale / 2;
    drawSystemText("～", cx - half - 10,       cy - half - 30, 32);
    drawSystemText("～", cx + half - 20,       cy - half - 30, 32);
  }
}

static void drawPoops(int cx, int cy, int scale, uint8_t count) {
  // Draw up to 3 little piles to the right of the pet.
  int half = SPRITE_W * scale / 2;
  int startX = cx + half + 10;
  int y = cy + half - scale * 2;
  int size = scale * 3;
  for (int i = 0; i < count && i < 3; i++) {
    int x = startX + i * (size + 6);
    M5.Display.fillTriangle(x, y + size, x + size / 2, y, x + size, y + size, EPD_DARK_GRAY);
    M5.Display.drawTriangle(x, y + size, x + size / 2, y, x + size, y + size, TFT_BLACK);
  }
}

static const char* stageLabel(uint8_t s) {
  switch (s) {
    case STAGE_EGG:        return "籖子";
    case STAGE_BABY:       return "寶寶";
    case STAGE_CHILD:      return "小童";
    case STAGE_CHILD_BAD:  return "頑童";
    case STAGE_TEEN:       return "少年";
    case STAGE_TEEN_BAD:   return "浪子";
    case STAGE_ADULT_A:    return "士人";
    case STAGE_ADULT_B:    return "工匠";
    case STAGE_ADULT_C:    return "商賈";
    case STAGE_ADULT_D:    return "野人";
    case STAGE_ELDER:      return "長者";
    case STAGE_DEAD:       return "西遊";
  }
  return "?";
}

static void drawStatBar(int x, int y, int w, int h, int val, int maxVal,
                        const char* label) {
  drawSystemText(label, x, y - 6, 28);
  int barX = x + 70;
  int barW = w - 70;
  M5.Display.drawRoundRect(barX, y, barW, h, 6, TFT_BLACK);
  if (val < 0) val = 0;
  if (val > maxVal) val = maxVal;
  int fillW = (barW - 4) * val / maxVal;
  M5.Display.fillRoundRect(barX + 2, y + 2, fillW, h - 4, 4, EPD_DARK_GRAY);
}

static void drawActionButton(int x, int y, int w, int h, const char* label,
                             bool disabled) {
  M5.Display.fillRoundRect(x, y, w, h, 10, TFT_WHITE);
  uint16_t edge = disabled ? EPD_LIGHT_GRAY : TFT_BLACK;
  M5.Display.drawRoundRect(x, y, w, h, 10, edge);
  M5.Display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, edge);
  drawSystemTextCentered(label, x + w / 2, y + h / 2 - 14, 28);
}

static bool tamaCanFeed() {
  return tama.stage != STAGE_DEAD && tama.hunger > 0;
}

static bool tamaCanClean() {
  return tama.stage != STAGE_DEAD && (tama.poopCount > 0 || tama.cleanliness < 10);
}

static bool tamaCanHeal() {
  return tama.stage != STAGE_DEAD && tama.sick;
}

static bool tamaCanPerformAction(int action) {
  switch (action) {
    case 0: return tamaCanFeed();
    case 1: return tama.stage != STAGE_DEAD;
    case 2: return tamaCanClean();
    case 3: return tamaCanHeal();
  }
  return false;
}

// Four buttons across the bottom.
static const int BTN_COUNT = 3;
static const int BTN_Y = 800;
static const int BTN_H = 80;

static int buttonX(int idx) {
  const int margin = 20;
  const int gap = 12;
  int w = (DISPLAY_WIDTH - margin * 2 - gap * (BTN_COUNT - 1)) / BTN_COUNT;
  return margin + idx * (w + gap);
}
static int buttonW() {
  const int margin = 20;
  const int gap = 12;
  return (DISPLAY_WIDTH - margin * 2 - gap * (BTN_COUNT - 1)) / BTN_COUNT;
}

// Dynamic region geometry — these are the areas that change from tick to tick
// and from button taps. Everything outside is painted once in the full draw
// and left alone so we can do partial/fast refreshes.
static const int DYN_X = 0;
static const int DYN_Y = 105;     // just below the header separator line
static const int DYN_W = DISPLAY_WIDTH;
static const int DYN_H = 795 - 105;  // up to (not including) the button row

// Tracks whether the currently-rendered buttons are enabled, so we only
// repaint the button strip when those states actually change.
static bool lastFeedEnabled = false;
static bool lastCleanEnabled = false;
static bool lastHealEnabled = false;
static bool lastDead = false;

static void drawTamagotchiDynamic() {
  // Partial refresh: only the middle region (age, sprite, mood, stats).
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillRect(DYN_X, DYN_Y, DYN_W, DYN_H, TFT_WHITE);

  // Age + care score readout
  uint32_t now = nowEpoch();
  uint32_t age = (now > tama.birthEpoch) ? now - tama.birthEpoch : 0;
  char ageBuf[64];
  if (age < 60 * 60 * 48) {
    float hours = age / 3600.0f;
    snprintf(ageBuf, sizeof(ageBuf), "齡：%.1f 時   德：%d", hours, tama.careScore);
  } else {
    int days = age / (60 * 60 * 24);
    snprintf(ageBuf, sizeof(ageBuf), "齡：%d 日   德：%d", days, tama.careScore);
  }
  drawSystemText(ageBuf, 20, 115, 28);
  if (tama.sick) drawSystemText("（疾）", 380, 115, 28);

  // Pet sprite + poops
  int cx = DISPLAY_WIDTH / 2;
  int cy = 350;
  drawPetSprite(cx, cy, tama.stage, tama.mood);
  drawPoops(cx, cy, 10, tama.poopCount);

  // Mood line under the sprite
  const char* moodText = "";
  switch (tama.mood) {
    case 0: moodText = "歌子安然"; break;
    case 1: moodText = "歌子甚悅"; break;
    case 2: moodText = "歌子飢矣"; break;
    case 3: moodText = "歌子閒閒"; break;
    case 4: moodText = "歌子患疾"; break;
    case 5: moodText = "歌子污穢"; break;
  }
  if (tama.stage == STAGE_DEAD) moodText = "歌子仙逝";
  drawSystemTextCentered(moodText, DISPLAY_WIDTH / 2, 560, 32);

  // Stats bars
  int barX = 30, barW = DISPLAY_WIDTH - 60;
  drawStatBar(barX, 625, barW, 26, 10 - tama.hunger, 10, "飽");
  drawStatBar(barX, 665, barW, 26, tama.happiness,   10, "樂");
  drawStatBar(barX, 705, barW, 26, tama.cleanliness, 10, "潔");
  drawStatBar(barX, 745, barW, 26, tama.health,      10, "康");

  // Repaint buttons only when their enabled state changes (avoids flicker).
  bool dead = (tama.stage == STAGE_DEAD);
  bool feedEnabled = tamaCanFeed();
  bool cleanEnabled = tamaCanClean();
  bool healEnabled = tamaCanHeal();
  if (dead != lastDead || feedEnabled != lastFeedEnabled ||
      cleanEnabled != lastCleanEnabled || healEnabled != lastHealEnabled) {
    M5.Display.fillRect(0, BTN_Y - 2, DISPLAY_WIDTH, BTN_H + 4, TFT_WHITE);
    drawActionButton(buttonX(0), BTN_Y, buttonW(), BTN_H, "餵食", !feedEnabled);
    drawActionButton(buttonX(1), BTN_Y, buttonW(), BTN_H, "清掃", !cleanEnabled);
    drawActionButton(buttonX(2), BTN_Y, buttonW(), BTN_H, "醫治", !healEnabled);
    lastDead = dead;
    lastFeedEnabled = feedEnabled;
    lastCleanEnabled = cleanEnabled;
    lastHealEnabled = healEnabled;
  }

  M5.Display.endWrite();
  M5.Display.display();
}

void drawTamagotchi() {
  if (!tamaLoaded) {
    if (!tamaLoad()) tamaReset();
    tamaLoaded = true;
  }
  tamaTick();
  tamaSave();

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  drawStatusBar();

  // Title (static header — never repainted on partial refresh)
  drawSystemText("歌子", 20, 42, 40);
  drawSystemText(stageLabel(tama.stage), 200, 50, 28);
  M5.Display.drawLine(20, 95, 520, 95, TFT_BLACK);

  // Age + care score readout
  uint32_t now = nowEpoch();
  uint32_t age = (now > tama.birthEpoch) ? now - tama.birthEpoch : 0;
  char ageBuf[64];
  if (age < 60 * 60 * 48) {
    float hours = age / 3600.0f;
    snprintf(ageBuf, sizeof(ageBuf), "齡：%.1f 時   德：%d", hours, tama.careScore);
  } else {
    int days = age / (60 * 60 * 24);
    snprintf(ageBuf, sizeof(ageBuf), "齡：%d 日   德：%d", days, tama.careScore);
  }
  drawSystemText(ageBuf, 20, 115, 28);
  if (tama.sick) drawSystemText("（疾）", 380, 115, 28);

  // Pet sprite + poops
  int cx = DISPLAY_WIDTH / 2;
  int cy = 350;
  drawPetSprite(cx, cy, tama.stage, tama.mood);
  drawPoops(cx, cy, 10, tama.poopCount);

  // Mood line under the sprite
  const char* moodText = "";
  switch (tama.mood) {
    case 0: moodText = "歌子安然"; break;
    case 1: moodText = "歌子甚悅"; break;
    case 2: moodText = "歌子飢矣"; break;
    case 3: moodText = "歌子閒閒"; break;
    case 4: moodText = "歌子患疾"; break;
    case 5: moodText = "歌子污穢"; break;
  }
  if (tama.stage == STAGE_DEAD) moodText = "歌子仙逝";
  drawSystemTextCentered(moodText, DISPLAY_WIDTH / 2, 560, 32);

  // Stats bars — hunger inverted so "full" reads left-to-right like the others
  int barX = 30, barW = DISPLAY_WIDTH - 60;
  drawStatBar(barX, 625, barW, 26, 10 - tama.hunger, 10, "飽");
  drawStatBar(barX, 665, barW, 26, tama.happiness,   10, "樂");
  drawStatBar(barX, 705, barW, 26, tama.cleanliness, 10, "潔");
  drawStatBar(barX, 745, barW, 26, tama.health,      10, "康");

  // Action buttons
  bool dead = (tama.stage == STAGE_DEAD);
  bool feedEnabled = tamaCanFeed();
  bool cleanEnabled = tamaCanClean();
  bool healEnabled = tamaCanHeal();
  drawActionButton(buttonX(0), BTN_Y, buttonW(), BTN_H, "餵食", !feedEnabled);
  drawActionButton(buttonX(1), BTN_Y, buttonW(), BTN_H, "清掃", !cleanEnabled);
  drawActionButton(buttonX(2), BTN_Y, buttonW(), BTN_H, "醫治", !healEnabled);
  lastDead = dead;
  lastFeedEnabled = feedEnabled;
  lastCleanEnabled = cleanEnabled;
  lastHealEnabled = healEnabled;

  if (dead) {
    drawSystemTextCentered("觸下方 〔返〕 重啟", DISPLAY_WIDTH / 2, 910, 28);
  } else if (tama.stage != STAGE_EGG) {
    drawSystemTextCentered("搖動裝置以逗玩", DISPLAY_WIDTH / 2, 910, 24, EPD_MID_GRAY);
  }

  drawReturnButton();

  M5.Display.endWrite();
  M5.Display.display();
}

// Forward declarations for action animations (defined after playShakeAnimation)
static void playFeedAnimation();
static void playCleanAnimation();
static void playHealAnimation();

static void performAction(int action) {
  if (tama.stage == STAGE_DEAD) return;
  tamaTick();
  if (!tamaCanPerformAction(action)) return;
  // Play animation before applying stat changes so visual feedback is immediate
  switch (action) {
    case 0: playFeedAnimation();  break;
    case 2: playCleanAnimation(); break;
    case 3: if (tama.sick) playHealAnimation(); break;
  }
  switch (action) {
    case 0: {  // feed
      uint8_t hungerBefore = tama.hunger;
      if (tama.hunger >= 3) tama.hunger -= 3; else tama.hunger = 0;
      // Overfeeding a non-hungry pet: small discipline/care hit
      if (hungerBefore == 0 && tama.feedCount > 0 && (esp_random() & 1)) {
        tama.careScore -= 1;
        tama.discipline = (tama.discipline > 0) ? tama.discipline - 1 : 0;
      } else {
        tama.careScore += 1;
      }
      tama.feedCount++;
      break;
    }
    case 1:  // play
      tama.happiness += 3;
      if (tama.happiness > 10) tama.happiness = 10;
      tama.playCount++;
      tama.careScore += 1;
      break;
    case 2:  // clean
      if (tama.poopCount > 0) {
        tama.poopCount = 0;
        tama.careScore += 2;
      }
      tama.cleanliness = 10;
      tama.cleanCount++;
      break;
    case 3:  // heal
      if (tama.sick) {
        tama.sick = 0;
        int hp = (int)tama.health + 3;
        tama.health = (hp > 10) ? 10 : (uint8_t)hp;
        tama.healCount++;
        tama.careScore += 3;
        // Grant 10 min immunity so underlying causes can be addressed
        lastHealEpoch = nowEpoch();
      }
      break;
  }
  tamaUpdateMood();
  tamaSave();
  drawTamagotchiDynamic();
}

void tamagotchiHandleTap(int x, int y) {
  // Dead pet: tapping the return button (handled by caller) is the only exit,
  // but tapping anywhere else on the dead screen revives by hatching a new egg.
  if (tama.stage == STAGE_DEAD) {
    tamaReset();
    tamaSave();
    drawTamagotchi();   // full redraw — stage label in title changes
    return;
  }
  if (y < BTN_Y || y > BTN_Y + BTN_H) return;
  int w = buttonW();
  // Buttons: 0=餵食, 1=清掃, 2=醫治 (逗玩 removed — use shake)
  static const int btnAction[] = { 0, 2, 3 };
  for (int i = 0; i < BTN_COUNT; i++) {
    int bx = buttonX(i);
    if (x >= bx && x <= bx + w) {
      performAction(btnAction[i]);
      return;
    }
  }
}

// Quick wiggle animation played before performAction(1) when the user shakes.
// Redraws the sprite band a few times at alternating horizontal offsets using
// the fast partial-refresh path.  Bounded in time so it doesn't block the loop
// noticeably (~500ms total).
static void playShakeAnimation() {
  if (tama.stage == STAGE_EGG || tama.stage == STAGE_DEAD) return;
  const int regionY = 240;
  const int regionH = 240;  // covers sprite + poops, stops above mood line
  const int cy      = 350;
  // Offset sequence: decaying back-and-forth wiggle ending centered.
  const int frames[]   = { -22, 22, -16, 16, -8, 8, 0 };
  const int nframes    = (int)(sizeof(frames) / sizeof(frames[0]));

  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  for (int i = 0; i < nframes; i++) {
    M5.Display.startWrite();
    M5.Display.fillRect(0, regionY, DISPLAY_WIDTH, regionH, TFT_WHITE);
    int cx = DISPLAY_WIDTH / 2 + frames[i];
    // Force "happy" mood face during the wiggle — you're playing with the pet.
    drawPetSprite(cx, cy, tama.stage, 1);
    drawPoops(cx, cy, 10, tama.poopCount);
    M5.Display.endWrite();
    M5.Display.display();
    delay(60);
  }
}

// Feed animation: pet bobs down (chomping), then springs back up happy.
// A small "🍙" rice ball drops from above and lands at mouth level.
static void playFeedAnimation() {
  if (tama.stage == STAGE_EGG || tama.stage == STAGE_DEAD) return;
  const int regionY = 200;
  const int regionH = 300;
  const int baseCy  = 350;
  const int cx      = DISPLAY_WIDTH / 2;
  // Bob sequence: down, down more, chomp, back up, settle
  const int bobY[]  = { 8, 16, 20, 16, 8, 0 };
  const int nframes = (int)(sizeof(bobY) / sizeof(bobY[0]));
  // Rice ball falls from above to mouth height
  const int foodStartY = regionY + 10;
  const int foodEndY   = baseCy - 20;

  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  for (int i = 0; i < nframes; i++) {
    M5.Display.startWrite();
    M5.Display.fillRect(0, regionY, DISPLAY_WIDTH, regionH, TFT_WHITE);
    int cy = baseCy + bobY[i];
    drawPetSprite(cx, cy, tama.stage, 1);
    // Draw food item dropping in first frames, eaten after midpoint
    if (i < 3) {
      int foodY = foodStartY + (foodEndY - foodStartY) * i / 2;
      drawSystemTextCentered("●", cx, foodY, 32);
    }
    M5.Display.endWrite();
    M5.Display.display();
    delay(80);
  }
}

// Clean animation: sparkles sweep across the pet, poops disappear.
// Pet wiggles left/right as if being scrubbed.
static void playCleanAnimation() {
  if (tama.stage == STAGE_EGG || tama.stage == STAGE_DEAD) return;
  const int regionY = 200;
  const int regionH = 300;
  const int cy      = 350;
  // Scrub: left-right-left-right, ending centered
  const int scrubX[] = { -12, 12, -8, 8, 0 };
  const int nframes  = (int)(sizeof(scrubX) / sizeof(scrubX[0]));
  // Sparkle positions relative to pet center (3 sparkles cycling)
  const int sparkOffs[][2] = { {-60, -40}, {50, -20}, {-30, 30}, {40, 50}, {-50, 10} };

  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  for (int i = 0; i < nframes; i++) {
    M5.Display.startWrite();
    M5.Display.fillRect(0, regionY, DISPLAY_WIDTH, regionH, TFT_WHITE);
    int cx = DISPLAY_WIDTH / 2 + scrubX[i];
    drawPetSprite(cx, cy, tama.stage, tama.mood);
    // Draw sparkle symbols at rotating positions
    drawSystemText("✦", cx + sparkOffs[i][0], cy + sparkOffs[i][1], 28);
    drawSystemText("✦", cx + sparkOffs[(i + 2) % 5][0], cy + sparkOffs[(i + 2) % 5][1], 28);
    M5.Display.endWrite();
    M5.Display.display();
    delay(80);
  }
}

// Heal animation: a cross/plus symbol pulses over the pet while it stands still.
// Sick aura tildes fade away as the cross appears.
static void playHealAnimation() {
  if (tama.stage == STAGE_EGG || tama.stage == STAGE_DEAD) return;
  const int regionY = 200;
  const int regionH = 300;
  const int cy      = 350;
  const int cx      = DISPLAY_WIDTH / 2;
  // Cross pulsing: show cross at different sizes, then clear
  const int crossSz[] = { 20, 32, 40, 32, 20 };
  const int nframes   = (int)(sizeof(crossSz) / sizeof(crossSz[0]));

  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  for (int i = 0; i < nframes; i++) {
    M5.Display.startWrite();
    M5.Display.fillRect(0, regionY, DISPLAY_WIDTH, regionH, TFT_WHITE);
    // First half: still sick face; second half: cured happy face
    uint8_t animMood = (i < 3) ? 4 : 1;
    drawPetSprite(cx, cy, tama.stage, animMood);
    // Draw cross (plus sign) symbol above the pet
    int sz = crossSz[i];
    int crossY = cy - 100;
    // Horizontal bar
    M5.Display.fillRect(cx - sz, crossY - sz / 6, sz * 2, sz / 3, TFT_BLACK);
    // Vertical bar
    M5.Display.fillRect(cx - sz / 6, crossY - sz, sz / 3, sz * 2, TFT_BLACK);
    M5.Display.endWrite();
    M5.Display.display();
    delay(100);
  }
}

void pollTamagotchi() {
  if (!tamaLoaded) return;
  uint32_t nowMs = millis();
  if (tama.stage != STAGE_EGG && tama.stage != STAGE_DEAD) {
    float ax = 0, ay = 0, az = 0;
    if (M5.Imu.getAccelData(&ax, &ay, &az)) {
      if (tamaLastShakeTime == 0 || nowMs - tamaLastShakeTime >= TAMA_SHAKE_COOLDOWN_MS) {
        float magnitude = sqrtf(ax * ax + ay * ay + az * az);
        float deviation = fabsf(magnitude - 1.0f);
        bool nowAbove = (deviation > (TAMA_SHAKE_THRESHOLD - 1.0f));
        if (nowAbove && !tamaAboveThresh) {
          if (tamaShakeCount == 0 || (nowMs - tamaShakeWindow < TAMA_SHAKE_WINDOW_MS)) {
            if (tamaShakeCount == 0) tamaShakeWindow = nowMs;
            tamaShakeCount++;
            if (tamaShakeCount >= TAMA_SHAKE_COUNT_NEEDED) {
              tamaShakeCount = 0;
              tamaLastShakeTime = nowMs;
              Serial.println("Tamagotchi: shake → 逗玩");
              playShakeAnimation();
              performAction(1);  // play
              return;
            }
          } else {
            tamaShakeCount = 1;
            tamaShakeWindow = nowMs;
          }
        }
        tamaAboveThresh = nowAbove;
      }
    }
  }

  // Rate-limit the stat tick + redraw to once a minute to avoid e-ink flicker.
  if (lastPollMs != 0 && nowMs - lastPollMs < 60000) return;
  lastPollMs = nowMs;

  uint8_t  oldH = tama.hunger,     oldP = tama.happiness;
  uint8_t  oldC = tama.cleanliness,oldK = tama.health;
  uint8_t  oldS = tama.stage,      oldM = tama.mood;
  uint8_t  oldPp= tama.poopCount,  oldSk= tama.sick;
  tamaTick();
  bool stageChanged = (tama.stage != oldS);
  bool sickChanged  = (tama.sick  != oldSk);
  if (tama.hunger != oldH || tama.happiness != oldP ||
      tama.cleanliness != oldC || tama.health != oldK ||
      stageChanged || tama.mood != oldM ||
      tama.poopCount != oldPp || sickChanged) {
    tamaSave();
    // Stage or sick-flag changes also touch the static header
    // (stage label and "（疾）" marker) — do a full redraw in that case.
    if (stageChanged || sickChanged) drawTamagotchi();
    else drawTamagotchiDynamic();
  }
}

void tamagotchiExit() {
  if (tamaLoaded) {
    tamaTick();
    tamaSave();
  }
  lastPollMs = 0;
}
