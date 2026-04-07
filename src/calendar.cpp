#include "globals.h"
#include "labels/label_bitmaps.h"

static const uint32_t lunarData[] = {
  0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2, // 1900-1909
  0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977, // 1910-1919
  0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, // 1920-1929
  0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950, // 1930-1939
  0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, // 1940-1949
  0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, // 1950-1959
  0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, // 1960-1969
  0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6, // 1970-1979
  0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570, // 1980-1989
  0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0, // 1990-1999
  0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, // 2000-2009
  0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, // 2010-2019
  0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, // 2020-2029
  0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, // 2030-2039
  0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, // 2040-2049
  0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06aa0, 0x1a6c4, 0x0aae0, // 2050-2059
  0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4, // 2060-2069
  0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0, // 2070-2079
  0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160, // 2080-2089
  0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a4d0, 0x0d150, 0x0f252, // 2090-2099
  0x0d520 // 2100
};

// Forward declarations
int lunarLeapDays(int year);
int lunarLeapMonth(int year);
int lunarMonthDays(int year, int month);

// Get number of days in a lunar year
int lunarYearDays(int year) {
  int idx = year - 1900;
  if (idx < 0 || idx > 200) return 348;
  int sum = 348;  // 12 months * 29 days base
  uint32_t info = lunarData[idx];
  for (int i = 0x8000; i > 0x8; i >>= 1) {
    sum += (info & i) ? 1 : 0;
  }
  return sum + lunarLeapDays(year);
}

// Get leap month of a lunar year (0 = no leap)
int lunarLeapMonth(int year) {
  int idx = year - 1900;
  if (idx < 0 || idx > 200) return 0;
  return lunarData[idx] & 0xf;
}

// Get days in leap month
int lunarLeapDays(int year) {
  int lm = lunarLeapMonth(year);
  if (lm == 0) return 0;
  int idx = year - 1900;
  return (lunarData[idx] & 0x10000) ? 30 : 29;
}

// Get days in a lunar month (month: 1-12)
int lunarMonthDays(int year, int month) {
  int idx = year - 1900;
  if (idx < 0 || idx > 200 || month < 1 || month > 12) return 29;
  return (lunarData[idx] & (0x10000 >> month)) ? 30 : 29;
}

// LunarDate struct is defined in globals.h

// Pure math day count (no mktime - ESP32 can't handle 1900)
long solarDayNumber(int y, int m, int d) {
  // Julian Day Number formula (valid for all Gregorian dates)
  int a = (14 - m) / 12;
  int yy = y + 4800 - a;
  int mm = m + 12 * a - 3;
  return d + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

// Check if a solar date has valid lunar data (Jan 31, 1900 to end of 2100)
static bool hasLunarData(int y, int m, int d) {
  if (y < 1900 || y > 2100) return false;
  if (y == 1900 && (m < 1 || (m == 1 && d < 31))) return false;
  return true;
}

// Convert solar date to lunar date
LunarDate solarToLunar(int sYear, int sMonth, int sDay) {
  LunarDate result = {0, 0, 0, false};
  
  // Base date: Jan 31, 1900 = Lunar year 1900, month 1, day 1
  long baseJDN = solarDayNumber(1900, 1, 31);
  long targetJDN = solarDayNumber(sYear, sMonth, sDay);
  
  int offset = (int)(targetJDN - baseJDN);
  if (offset < 0) {
    result.year = sYear; result.month = 1; result.day = 1;
    return result;
  }
  
  // Find lunar year
  int lunarYear = 1900;
  int daysInYear;
  for (; lunarYear < 2101 && offset >= 0; lunarYear++) {
    daysInYear = lunarYearDays(lunarYear);
    if (offset < daysInYear) break;
    offset -= daysInYear;
  }
  
  result.year = lunarYear;
  
  int leapMonth = lunarLeapMonth(lunarYear);
  int totalMonths = leapMonth > 0 ? 13 : 12;
  int lunarMonth = 1;
  bool isLeap = false;
  int daysInMonth;
  
  for (int m = 0; m < totalMonths; m++) {
    if (leapMonth > 0 && m == leapMonth) {
      // This iteration is the leap month
      isLeap = true;
      lunarMonth = leapMonth;
      daysInMonth = lunarLeapDays(lunarYear);
    } else {
      isLeap = false;
      if (leapMonth == 0 || m < leapMonth) {
        lunarMonth = m + 1;
      } else {
        lunarMonth = m;  // After leap month, shift back
      }
      daysInMonth = lunarMonthDays(lunarYear, lunarMonth);
    }
    
    if (offset < daysInMonth) break;
    offset -= daysInMonth;
  }
  
  result.month = lunarMonth;
  result.day = offset + 1;
  result.isLeapMonth = isLeap;
  
  Serial.printf("Solar %d/%d/%d -> Lunar %d/%d/%d (leap=%d)\n", 
                sYear, sMonth, sDay, result.year, result.month, result.day, result.isLeapMonth);
  return result;
}

// Chinese day names (初一 to 三十)
const char* lunarDayName(int day) {
  static const char* days[] = {
    "", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"
  };
  if (day >= 1 && day <= 30) return days[day];
  return "";
}

// Chinese month names
const char* lunarMonthName(int month) {
  static const char* months[] = {
    "", "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "臘月"
  };
  if (month >= 1 && month <= 12) return months[month];
  return "";
}

// Heavenly Stems (天干)
const char* tianGan[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
// Earthly Branches (地支)
const char* diZhi[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
// Chinese Zodiac (生肖)
const char* shengXiao[] = {"鼠", "牛", "虎", "兔", "龍", "蛇", "馬", "羊", "猴", "雞", "狗", "豬"};

// Day of week in Chinese
const char* weekDayCN[] = {"日", "一", "二", "三", "四", "五", "六"};

// Get year's Heavenly Stem + Earthly Branch (天干地支)
void getYearGanZhi(int lunarYear, char* buf) {
  int ganIdx = (lunarYear - 4) % 10;
  int zhiIdx = (lunarYear - 4) % 12;
  snprintf(buf, 16, "%s%s", tianGan[ganIdx], diZhi[zhiIdx]);
}

// Get year's zodiac animal
const char* getZodiac(int lunarYear) {
  return shengXiao[(lunarYear - 4) % 12];
}

// Get day's Heavenly Stem + Earthly Branch
// Get day's gan/zhi indices (for reuse)
void getDayGanZhiIdx(int year, int month, int day, int &ganIdx, int &zhiIdx) {
  // Jan 1, 1900 is 甲戌 (gan=0, zhi=10) — verified against 壽星曆 sxwnl
  // sxwnl mingLiBaZi: v = D - 6 + 9000000 where D = JDN - 2451545
  // For Jan 1, 1900: D = 2415021 - 2451545 = -36524
  //   v = -36524 - 6 + 9000000 = 8963470, v%10=0(甲), v%12=10(戌) → 甲戌 ✓
  long refJDN = solarDayNumber(1900, 1, 1);
  long curJDN = solarDayNumber(year, month, day);
  int dayOffset = (int)(curJDN - refJDN);
  ganIdx = (((dayOffset + 0) % 10) + 10) % 10;
  zhiIdx = (((dayOffset + 10) % 12) + 12) % 12;
}

void getDayGanZhi(int year, int month, int day, char* buf) {
  int ganIdx, zhiIdx;
  getDayGanZhiIdx(year, month, day, ganIdx, zhiIdx);
  snprintf(buf, 16, "%s%s", tianGan[ganIdx], diZhi[zhiIdx]);
}

// Forward declarations for solar term infrastructure (defined later)
static int cachedTermDates[24];
static void computeTermDates(int year);

// Get month's Heavenly Stem + Earthly Branch  
void getMonthGanZhi(int year, int month, int day, char* buf) {
  // Month pillar is determined by solar term (節氣) boundaries, NOT calendar month.
  // The 12 "jie" (節) terms at even indices of termNamesCalendar[] mark month starts:
  //   Index 0  小寒 → GZ month 12 (丑)    Index 12 小暑 → GZ month 6 (未)
  //   Index 2  立春 → GZ month 1  (寅)    Index 14 立秋 → GZ month 7 (申)
  //   Index 4  驚蟄 → GZ month 2  (卯)    Index 16 白露 → GZ month 8 (酉)
  //   Index 6  清明 → GZ month 3  (辰)    Index 18 寒露 → GZ month 9 (戌)
  //   Index 8  立夏 → GZ month 4  (巳)    Index 20 立冬 → GZ month 10(亥)
  //   Index 10 芒種 → GZ month 5  (午)    Index 22 大雪 → GZ month 11(子)

  computeTermDates(year);

  // Find the most recent "jie" term (even indices) on or before the exact date
  int gzMonth = -1;
  long todayJDN = solarDayNumber(year, month, day);

  // Check jie terms (even indices: 0,2,4,...,22)
  for (int i = 22; i >= 0; i -= 2) {
    int tm = cachedTermDates[i] / 100;
    int td = cachedTermDates[i] % 100;
    long termJDN = solarDayNumber(year, tm, td);
    if (todayJDN >= termJDN) {
      // GZ month = ((i/2) + 11) % 12 + 1
      // Index 0 (小寒) → 12, Index 2 (立春) → 1, ...
      gzMonth = ((i / 2) + 11) % 12 + 1;
      break;
    }
  }

  // Edge case: before 小寒 of this year → use 大雪 from previous year → GZ month 11
  if (gzMonth < 0) {
    gzMonth = 11;  // 子月 (大雪 of previous year)
  }

  // Year stem for month pillar: if before 立春, use previous year's stem
  int stemYear = year;
  if (gzMonth >= 11) {
    // GZ months 11 (子) and 12 (丑) belong to the previous year's stem cycle
    // because the GZ year starts at 立春
    // Actually: 小寒 starts month 12. If month 12 or 11, check if 立春 passed.
    // Months 11-12 straddle the year boundary. 
    // 大雪 (month 11) is in Dec, 小寒 (month 12) in Jan.
    // For the stem, use the year in which 立春 falls (the GZ year).
    // Before 立春 → previous GZ year.
    int lichunM = cachedTermDates[2] / 100;
    int lichunD = cachedTermDates[2] % 100;
    long lichunJDN = solarDayNumber(year, lichunM, lichunD);
    if (todayJDN < lichunJDN) {
      stemYear = year - 1;
    }
  }

  int yearGan = (stemYear - 4) % 10;
  int monthGanBase;
  switch (yearGan % 5) {
    case 0: monthGanBase = 2; break;  // 甲/己 -> 丙
    case 1: monthGanBase = 4; break;  // 乙/庚 -> 戊
    case 2: monthGanBase = 6; break;  // 丙/辛 -> 庚
    case 3: monthGanBase = 8; break;  // 丁/壬 -> 壬
    case 4: monthGanBase = 0; break;  // 戊/癸 -> 甲
    default: monthGanBase = 0;
  }
  int ganIdx = (monthGanBase + gzMonth - 1) % 10;
  int zhiIdx = (gzMonth + 1) % 12;  // GZ month 1=寅(2), month 2=卯(3), ...
  snprintf(buf, 16, "%s%s", tianGan[ganIdx], diZhi[zhiIdx]);
}

// ===== Solar terms (節氣) - Astronomical calculation =====
// Calculate exact solar term dates using the Sun's ecliptic longitude
// instead of hardcoded dates which can be off by 1-2 days per year.

#include <math.h>

// Solar term names in ecliptic longitude order (starting from 春分 = 0°)
static const char* termNamesEcliptic[] = {
  "春分", "清明", "穀雨", "立夏", "小滿", "芒種",
  "夏至", "小暑", "大暑", "立秋", "處暑", "白露",
  "秋分", "寒露", "霜降", "立冬", "小雪", "大雪",
  "冬至", "小寒", "大寒", "立春", "雨水", "驚蟄"
};

// Solar term names in calendar order (starting from 小寒)
static const char* termNamesCalendar[] = {
  "小寒", "大寒", "立春", "雨水", "驚蟄", "春分",
  "清明", "穀雨", "立夏", "小滿", "芒種", "夏至",
  "小暑", "大暑", "立秋", "處暑", "白露", "秋分",
  "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"
};

// Target longitude for each term in calendar order (小寒=285°, 大寒=300°, ...)
static const double termTargetLon[] = {
  285, 300, 315, 330, 345, 0,
  15, 30, 45, 60, 75, 90,
  105, 120, 135, 150, 165, 180,
  195, 210, 225, 240, 255, 270
};

// Approximate month for each term in calendar order (for search start)
static const int termApproxMonth[] = {
  1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
  7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12
};

// Julian Day Number for a date at noon UT
static double calcJD(int y, int m, int d) {
  if (m <= 2) { y--; m += 12; }
  int A = y / 100;
  int B = 2 - A + A / 4;
  return (int)(365.25 * (y + 4716)) + (int)(30.6001 * (m + 1)) + d + B - 1524.5;
}

// Convert JD back to calendar date
static void jdToDate(double jd, int &y, int &m, int &d) {
  jd += 0.5;
  int Z = (int)jd;
  int A;
  if (Z < 2299161) {
    A = Z;
  } else {
    int alpha = (int)((Z - 1867216.25) / 36524.25);
    A = Z + 1 + alpha - alpha / 4;
  }
  int B = A + 1524;
  int C = (int)((B - 122.1) / 365.25);
  int D = (int)(365.25 * C);
  int E = (int)((B - D) / 30.6001);
  d = B - D - (int)(30.6001 * E);
  m = (E < 14) ? E - 1 : E - 13;
  y = (m > 2) ? C - 4716 : C - 4715;
}

// Sun's ecliptic longitude (degrees, 0-360) for a given Julian Day
static double calcSunLongitude(double jd) {
  double T = (jd - 2451545.0) / 36525.0;
  // Mean longitude
  double L0 = fmod(280.46646 + 36000.76983 * T + 0.0003032 * T * T, 360.0);
  if (L0 < 0) L0 += 360.0;
  // Mean anomaly
  double M = fmod(357.52911 + 35999.05029 * T - 0.0001537 * T * T, 360.0);
  if (M < 0) M += 360.0;
  double Mrad = M * M_PI / 180.0;
  // Equation of center
  double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(Mrad)
           + (0.019993 - 0.000101 * T) * sin(2.0 * Mrad)
           + 0.000289 * sin(3.0 * Mrad);
  // Apparent longitude (simplified nutation correction)
  double omega = 125.04 - 1934.136 * T;
  double lon = L0 + C - 0.00569 - 0.00478 * sin(omega * M_PI / 180.0);
  lon = fmod(lon, 360.0);
  if (lon < 0) lon += 360.0;
  return lon;
}

// Find the JD when Sun's longitude reaches targetLon, searching near startJD
// Uses binary search; accurate to ~1 minute
static double findTermJD(double startJD, double targetLon) {
  // Bracket: search ±20 days from start
  double lo = startJD - 20;
  double hi = startJD + 20;
  
  for (int iter = 0; iter < 50; iter++) {
    double mid = (lo + hi) / 2.0;
    double lon = calcSunLongitude(mid);
    double diff = lon - targetLon;
    // Normalize to [-180, 180]
    if (diff > 180.0) diff -= 360.0;
    if (diff < -180.0) diff += 360.0;
    if (diff < 0) lo = mid;
    else hi = mid;
  }
  return (lo + hi) / 2.0;
}

// ===== 壽星天文曆 qi_low(): self-contained solar term finder =====
// From sxwnl by 許劍偉. Max error < 30 min.
// W = target longitude in radians (unwrapped, qi number * π/12)
// Returns J2000-based days in UT.
static double sxwnl_qi_low(double W) {
  double v = 628.3319653318;
  double t = (W - 4.895062166) / v;
  t -= (53.0 * t * t + 334116.0 * cos(4.67 + 628.307585 * t)
        + 2061.0 * cos(2.678 + 628.3076 * t) * t) / v / 10000000.0;
  double L = 48950621.66 + 6283319653.318 * t + 53.0 * t * t
       + 334166.0 * cos(4.669257 + 628.307585 * t)
       + 3489.0 * cos(4.6261 + 1256.61517 * t)
       + 2060.6 * cos(2.67823 + 628.307585 * t) * t
       - 994.0 - 834.0 * sin(2.1824 - 33.75705 * t);
  t -= (L / 10000000.0 - W) / 628.332 + (32.0 * (t + 1.8) * (t + 1.8) - 20.0) / 86400.0 / 36525.0;
  return t * 36525.0;  // J2000-based days in UT
}

// Cache for solar term dates (year, and 24 dates as month*100+day)
static int cachedTermYear = -1;
static int cachedTermGmtOffset = -1;
static bool cachedTermMethod = false;  // false=Meeus, true=sxwnl
// cachedTermDates[24] declared above getMonthGanZhi()

// Compute all 24 solar term start dates for a given year
static void computeTermDates(int year) {
  // Invalidate cache if year, timezone, or method changed
  if (cachedTermYear == year && cachedTermGmtOffset == timeConfig.gmtOffset
      && cachedTermMethod == useSxwnlCalendar) return;
  
  if (useSxwnlCalendar) {
    // 壽星天文曆 method: qi_low
    for (int i = 0; i < 24; i++) {
      // Map calendar index to ecliptic index
      int eclipticIdx = (i + 19) % 24;
      int cycleYear = (eclipticIdx >= 19) ? year - 1 : year;
      int k = (cycleYear - 2000) * 24 + eclipticIdx + 24;
      double W = k * M_PI / 12.0;
      double j2k = sxwnl_qi_low(W);
      double jd = j2k + 2451545.0;
      jd += (double)timeConfig.gmtOffset / 86400.0;
      int ty, tm, td;
      jdToDate(jd, ty, tm, td);
      cachedTermDates[i] = tm * 100 + td;
    }
  } else {
    // Meeus method: binary search for exact solar longitude
    for (int i = 0; i < 24; i++) {
      double approxJD = calcJD(year, termApproxMonth[i], 15);
      double targetLon = termTargetLon[i];
      double termJD = findTermJD(approxJD, targetLon);
      termJD += (double)timeConfig.gmtOffset / 86400.0;
      int ty, tm, td;
      jdToDate(termJD, ty, tm, td);
      cachedTermDates[i] = tm * 100 + td;
    }
  }
  cachedTermYear = year;
  cachedTermGmtOffset = timeConfig.gmtOffset;
  cachedTermMethod = useSxwnlCalendar;
}

// Get solar term name if the given date is the exact start of a term (for year)
const char* getSolarTerm(int year, int month, int day) {
  computeTermDates(year);
  int target = month * 100 + day;
  for (int i = 0; i < 24; i++) {
    if (cachedTermDates[i] == target) {
      return termNamesCalendar[i];
    }
  }
  return nullptr;
}

// Get current (most recent) solar term and next upcoming one
const char* getCurrentSolarTerm(int year, int month, int day, const char** nextTerm, int* daysToNext) {
  computeTermDates(year);
  long todayJDN = solarDayNumber(year, month, day);
  
  const char* current = nullptr;
  const char* next = nullptr;
  int bestDaysToNext = 999;
  
  // Check all 24 terms for this year
  for (int i = 0; i < 24; i++) {
    int tm = cachedTermDates[i] / 100;
    int td = cachedTermDates[i] % 100;
    long termJDN = solarDayNumber(year, tm, td);
    int diff = (int)(todayJDN - termJDN);
    
    if (diff >= 0) {
      current = termNamesCalendar[i];
    }
    if (diff < 0 && next == nullptr) {
      next = termNamesCalendar[i];
      bestDaysToNext = -diff;
    }
  }
  
  // Edge cases: before first term or after last term of year
  if (!current) {
    // Before 小寒: use 冬至 from previous year
    current = "冬至";
  }
  if (!next) {
    // After 冬至: next is 小寒 of next year
    // Compute next year's first term
    int savedYear = cachedTermYear;
    int savedDates[24];
    memcpy(savedDates, cachedTermDates, sizeof(cachedTermDates));
    computeTermDates(year + 1);
    int nm = cachedTermDates[0] / 100;
    int nd = cachedTermDates[0] % 100;
    next = "小寒";
    bestDaysToNext = (int)(solarDayNumber(year + 1, nm, nd) - todayJDN);
    // Restore cache
    cachedTermYear = savedYear;
    memcpy(cachedTermDates, savedDates, sizeof(cachedTermDates));
  }
  
  if (nextTerm) *nextTerm = next;
  if (daysToNext) *daysToNext = bestDaysToNext;
  return current;
}

// Auspicious activities (宜)
const char* getYiActivities(int dayGanIdx, int dayZhiIdx) {
  // Rotate through common auspicious activities based on day
  static const char* activities[][6] = {
    {"祈福", "出行", "納采", "嫁娶", "修造", "動土"},
    {"開市", "交易", "立券", "納財", "安床", "裁衣"},
    {"祭祀", "祈福", "求嗣", "開光", "出行", "解除"},
    {"嫁娶", "納采", "訂盟", "祭祀", "祈福", "修造"},
    {"安葬", "啟鑽", "除服", "成服", "移柩", "入殮"},
    {"出行", "教牛馬", "豎柱", "上樑", "修造", "開市"},
    {"祈福", "齋醮", "出行", "移徙", "入宅", "修造"},
    {"嫁娶", "祭祀", "開市", "出行", "動土", "安床"},
    {"祭祀", "祈福", "求嗣", "齋醮", "納采", "嫁娶"},
    {"修造", "動土", "豎柱", "上樑", "安門", "造廟"}
  };
  int idx = (dayGanIdx + dayZhiIdx) % 10;
  static char buf[128];
  snprintf(buf, sizeof(buf), "%s %s %s %s", activities[idx][0], activities[idx][1], 
          activities[idx][2], activities[idx][3]);
  return buf;
}

// Inauspicious activities (忌)
const char* getJiActivities(int dayGanIdx, int dayZhiIdx) {
  static const char* activities[][4] = {
    {"開市", "動土", "破土", "安葬"},
    {"嫁娶", "安葬", "出行", "動土"},
    {"移徙", "入宅", "安門", "作灶"},
    {"開倉", "出貨", "安葬", "破土"},
    {"嫁娶", "開市", "入宅", "移徙"},
    {"祈福", "嫁娶", "安葬", "破土"},
    {"安葬", "破土", "動土", "開市"},
    {"破土", "安葬", "開倉", "出貨"},
    {"動土", "破土", "安葬", "開市"},
    {"嫁娶", "入宅", "移徙", "出行"}
  };
  int idx = (dayGanIdx + dayZhiIdx + 5) % 10;
  static char buf[96];
  snprintf(buf, sizeof(buf), "%s %s %s %s", activities[idx][0], activities[idx][1],
          activities[idx][2], activities[idx][3]);
  return buf;
}

// Auspicious direction (喜神方位)
const char* getXiShen(int dayGanIdx) {
  static const char* dirs[] = {
    "東北", "西北", "西南", "正南", "東南", "東北", "西北", "西南", "正南", "東南"
  };
  return dirs[dayGanIdx % 10];
}

// 福神方位
const char* getFuShen(int dayGanIdx) {
  static const char* dirs[] = {
    "東南", "東北", "正北", "正東", "正南", "東南", "東北", "正北", "正東", "正南"
  };
  return dirs[dayGanIdx % 10];
}

// 財神方位
const char* getCaiShen(int dayGanIdx) {
  static const char* dirs[] = {
    "東北", "正東", "正南", "正南", "正北", "正北", "正東", "正南", "正南", "正北"
  };
  return dirs[dayGanIdx % 10];
}

// 沖煞 (Clash)  
const char* getClash(int dayZhiIdx) {
  static const char* clashes[] = {
    "沖馬", "沖羊", "沖猴", "沖雞", "沖狗", "沖豬",
    "沖鼠", "沖牛", "沖虎", "沖兔", "沖龍", "沖蛇"
  };
  return clashes[dayZhiIdx % 12];
}

// 煞方 (Evil direction)
const char* getSha(int dayZhiIdx) {
  // 煞方 follows a pattern based on 地支: 子午卯酉=南, 寅申巳亥=東, 辰戌丑未=北/西
  static const char* sha[] = {
    "煞南", "煞西", "煞東", "煞南", "煞北", "煞西",
    "煞南", "煞東", "煞北", "煞南", "煞西", "煞東"
  };
  return sha[dayZhiIdx % 12];
}

// 六曜 (Six luminaries: 大安,留連,速喜,赤口,小吉,空亡)
const char* getLiuYao(int lunarMonth, int lunarDay) {
  static const char* liuyao[] = {"大安", "留連", "速喜", "赤口", "小吉", "空亡"};
  // Formula: (lunarMonth + lunarDay - 2) % 6
  int idx = ((lunarMonth - 1) + (lunarDay - 1)) % 6;
  return liuyao[idx];
}

// 胎神 (Fetal god position) - based on day's 天干地支
const char* getTaiShen(int dayGanIdx, int dayZhiIdx) {
  // 60 甲子胎神佔方 table (simplified cycle of 10 based on day stem)
  static const char* taishen[] = {
    "佔門碓外東南",  // 甲
    "碓磨廁外東南",  // 乙
    "廚灶爐外正南",  // 丙
    "倉庫門外正南",  // 丁
    "房床棲外正南",  // 戊
    "佔門床外正南",  // 己
    "佔碓磨外西南",  // 庚
    "廚灶廁外西南",  // 辛
    "倉庫爐外正西",  // 壬
    "房床門外正西"   // 癸
  };
  return taishen[dayGanIdx % 10];
}

// 納音五行 (Heavenly Sound Five Elements) - 60 甲子 cycle
const char* getNaYin(int ganIdx, int zhiIdx) {
  // Each pair of 干支 shares a 納音, so use combined index / 2
  static const char* nayin[] = {
    "海中金", "爐中火", "大林木", "路旁土", "劍鋒金",
    "山頭火", "澗下水", "城頭土", "白蠟金", "楊柳木",
    "泉中水", "屋上土", "霹靂火", "松柏木", "長流水",
    "沙中金", "山下火", "平地木", "壁上土", "金箔金",
    "覆燈火", "天河水", "大驛土", "釵釧金", "桑拓木",
    "大溪水", "沙中土", "天上火", "石榴木", "大海水"
  };
  // Combined sexagenary index
  int combined = (ganIdx % 10) + (zhiIdx % 12);
  // Proper 60-cycle index: find n where n%10==ganIdx and n%12==zhiIdx
  // Simple: iterate
  int sixtyIdx = 0;
  for (int n = 0; n < 60; n++) {
    if (n % 10 == ganIdx && n % 12 == zhiIdx) {
      sixtyIdx = n;
      break;
    }
  }
  return nayin[sixtyIdx / 2];
}

// Hour stems (時辰) - 12 two-hour periods
void getHourInfo(int dayGanIdx, char hourBuf[][16], char hourZhiBuf[][8]) {
  // Day's gan determines hour gan cycle
  int hourGanBase;
  switch (dayGanIdx % 5) {
    case 0: hourGanBase = 0; break;  // 甲/己日起甲子
    case 1: hourGanBase = 2; break;  // 乙/庚日起丙子
    case 2: hourGanBase = 4; break;  // 丙/辛日起戊子
    case 3: hourGanBase = 6; break;  // 丁/壬日起庚子
    case 4: hourGanBase = 8; break;  // 戊/癸日起壬子
  }
  for (int i = 0; i < 12; i++) {
    int ganIdx = (hourGanBase + i) % 10;
    snprintf(hourBuf[i], 16, "%s%s", tianGan[ganIdx], diZhi[i]);
    strcpy(hourZhiBuf[i], diZhi[i]);
  }
}

// ===== Festivals (道教節日, 民俗節日, 佛教節日, 西方節日, 自訂) =====
enum FestivalType : uint8_t { FEST_FOLK = 0, FEST_TAOIST = 1, FEST_BUDDHIST = 2, FEST_US = 3, FEST_CUSTOM = 4 };

struct Festival {
  uint8_t month;
  uint8_t day;
  const char* name;
  FestivalType type;
};

static const Festival festivalList[] = {
  // ── 正月 ──
  {1,  1,  "春節",         FEST_FOLK},
  {1,  1,  "彌勒菩薩誕",   FEST_BUDDHIST},
  {1,  5,  "破五節",       FEST_FOLK},
  {1,  9,  "玉皇誕",       FEST_TAOIST},
  {1,  15, "元宵節",       FEST_FOLK},
  {1,  15, "上元天官誕",   FEST_TAOIST},
  // ── 二月 ──
  {2,  2,  "龍抬頭",       FEST_FOLK},
  {2,  2,  "福德正神誕",   FEST_TAOIST},
  {2,  3,  "文昌帝君誕",   FEST_TAOIST},
  {2,  8,  "釋迦出家日",   FEST_BUDDHIST},
  {2,  15, "太上老君誕",   FEST_TAOIST},
  {2,  15, "釋迦涅槃日",   FEST_BUDDHIST},
  {2,  19, "觀音菩薩誕",   FEST_BUDDHIST},
  // ── 三月 ──
  {3,  3,  "上巳節",       FEST_FOLK},
  {3,  3,  "玄天上帝誕",   FEST_TAOIST},
  {3,  15, "保生大帝誕",   FEST_TAOIST},
  {3,  16, "準提菩薩誕",   FEST_BUDDHIST},
  {3,  23, "天上聖母誕",   FEST_TAOIST},
  // ── 四月 ──
  {4,  4,  "文殊菩薩誕",   FEST_BUDDHIST},
  {4,  8,  "佛誕日",       FEST_BUDDHIST},
  {4,  14, "呂祖誕",       FEST_TAOIST},
  {4,  26, "神農大帝誕",   FEST_TAOIST},
  // ── 五月 ──
  {5,  5,  "端午節",       FEST_FOLK},
  {5,  13, "關聖帝君誕",   FEST_TAOIST},
  // ── 六月 ──
  {6,  3,  "韋馱菩薩誕",   FEST_BUDDHIST},
  {6,  19, "觀音成道日",   FEST_BUDDHIST},
  {6,  24, "關帝誕",       FEST_TAOIST},
  // ── 七月 ──
  {7,  7,  "七夕",         FEST_FOLK},
  {7,  13, "大勢至菩薩誕", FEST_BUDDHIST},
  {7,  15, "中元節",       FEST_FOLK},
  {7,  15, "中元地官誕",   FEST_TAOIST},
  {7,  15, "盂蘭盆節",     FEST_BUDDHIST},
  {7,  30, "地藏菩薩誕",   FEST_BUDDHIST},
  // ── 八月 ──
  {8,  3,  "灶君誕",       FEST_TAOIST},
  {8,  15, "中秋節",       FEST_FOLK},
  // ── 九月 ──
  {9,  9,  "重陽節",       FEST_FOLK},
  {9,  9,  "中壇元帥誕",   FEST_TAOIST},
  {9,  19, "觀音出家日",   FEST_BUDDHIST},
  {9,  30, "藥師佛誕",     FEST_BUDDHIST},
  // ── 十月 ──
  {10, 1,  "寒衣節",       FEST_FOLK},
  {10, 15, "下元水官誕",   FEST_TAOIST},
  // ── 十一月 ──
  {11, 17, "阿彌陀佛誕",   FEST_BUDDHIST},
  // ── 十二月 ──
  {12, 8,  "臘八節",       FEST_FOLK},
  {12, 8,  "釋迦成道日",   FEST_BUDDHIST},
  {12, 23, "小年",         FEST_FOLK},
};

static const int festivalCount = sizeof(festivalList) / sizeof(festivalList[0]);

// Look up festivals for a given lunar date
// Returns the number of festivals found (up to maxResults)
int lookupFestivals(int lunarMonth, int lunarDay, int lunarYear,
                    const char* names[], uint8_t types[], int maxResults) {
  int count = 0;

  // Special: 除夕 is the last day of the 12th month
  if (lunarMonth == 12 && lunarDay == lunarMonthDays(lunarYear, 12)) {
    if (count < maxResults) {
      names[count] = "除夕";
      types[count] = FEST_FOLK;
      count++;
    }
  }

  // Check all festivals
  for (int i = 0; i < festivalCount; i++) {
    if (festivalList[i].month == lunarMonth && festivalList[i].day == lunarDay) {
      if (count < maxResults) {
        names[count] = festivalList[i].name;
        types[count] = (uint8_t)festivalList[i].type;
        count++;
      }
    }
  }

  return count;
}

// Get short festival name for the calendar grid (major folk festivals only)
const char* getGridFestival(int lunarMonth, int lunarDay, int lunarYear) {
  if (lunarMonth == 1  && lunarDay == 1)  return "春節";
  if (lunarMonth == 1  && lunarDay == 15) return "元宵";
  if (lunarMonth == 2  && lunarDay == 2)  return "龍抬頭";
  if (lunarMonth == 5  && lunarDay == 5)  return "端午";
  if (lunarMonth == 7  && lunarDay == 7)  return "七夕";
  if (lunarMonth == 7  && lunarDay == 15) return "中元";
  if (lunarMonth == 8  && lunarDay == 15) return "中秋";
  if (lunarMonth == 9  && lunarDay == 9)  return "重陽";
  if (lunarMonth == 12 && lunarDay == 8)  return "臘八";
  if (lunarMonth == 12 && lunarDay == 23) return "小年";
  if (lunarMonth == 12 && lunarDay == lunarMonthDays(lunarYear, 12)) return "除夕";
  return nullptr;
}

// Days in a solar month
int solarMonthDays(int year, int month) {
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
  return days[month];
}

// Day of week for any date (0=Sunday)
int dayOfWeek(int y, int m, int d) {
  // Use Julian Day Number for correct weekday across calendar reform
  long jdn;
  int a = (14 - m) / 12;
  int yy = y + 4800 - a;
  int mm = m + 12 * a - 3;
  if (y < 1582 || (y == 1582 && (m < 10 || (m == 10 && d < 15)))) {
    // Julian calendar (before Oct 15, 1582)
    jdn = d + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - 32083;
  } else {
    // Gregorian calendar
    jdn = d + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
  }
  return (int)((jdn + 1) % 7);  // 0=Sunday
}

// Nth weekday of month (weekday: 0=Sun..6=Sat, nth: 1..5). Returns 0 if not exists.
static int nthWeekdayOfMonth(int year, int month, int weekday, int nth) {
  int firstDow = dayOfWeek(year, month, 1);
  int day = 1 + ((weekday - firstDow + 7) % 7) + (nth - 1) * 7;
  int dim = solarMonthDays(year, month);
  return (day <= dim) ? day : 0;
}

// Last weekday of month (weekday: 0=Sun..6=Sat)
static int lastWeekdayOfMonth(int year, int month, int weekday) {
  int dim = solarMonthDays(year, month);
  int lastDow = dayOfWeek(year, month, dim);
  return dim - ((lastDow - weekday + 7) % 7);
}

// Gregorian Easter Sunday (valid for years 1583+)
static void calcEasterSunday(int year, int &month, int &day) {
  int a = year % 19;
  int b = year / 100;
  int c = year % 100;
  int d = b / 4;
  int e = b % 4;
  int f = (b + 8) / 25;
  int g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30;
  int i = c / 4;
  int k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451;
  month = (h + l - 7 * m + 114) / 31;
  day = ((h + l - 7 * m + 114) % 31) + 1;
}

// Look up major Western holidays/events (U.S. + Christian + Mexico) for a given solar date
int lookupUSHolidays(int year, int month, int day, const char* names[], uint8_t types[], int maxResults) {
  int count = 0;

  auto addHoliday = [&](const char* name) {
    if (count < maxResults) {
      names[count] = name;
      types[count] = FEST_US;
      count++;
    }
  };

  // Fixed-date major holidays / events
  // Christian
  if (month == 1 && day == 6) addHoliday("主顯節");
  if (month == 12 && day == 24) addHoliday("平安夜");
  if (month == 12 && day == 25) addHoliday("聖誕節");

  // U.S.
  if (month == 1 && day == 1) addHoliday("美國元旦");
  if (month == 6 && day == 19) addHoliday("六月節");
  if (month == 7 && day == 4) addHoliday("美國獨立日");
  if (month == 11 && day == 11) addHoliday("退伍軍人節");

  // Western cultural (shared)
  if (month == 10 && day == 31) addHoliday("萬聖節");

  // Mexico
  // if (month == 2 && day == 5) addHoliday("墨西哥憲法日");
  // if (month == 3 && day == 21) addHoliday("胡亞雷斯誕辰紀念日");
  if (month == 5 && day == 5) addHoliday("五月五日節");
  if (month == 9 && day == 16) addHoliday("墨西哥獨立日");
  if (month == 11 && day == 2) addHoliday("亡靈節");
  // if (month == 11 && day == 20) addHoliday("墨西哥革命紀念日");
  // if (month == 12 && day == 12) addHoliday("瓜達露佩聖母日");

  // Movable holidays
  int em = 0, ed = 0;
  calcEasterSunday(year, em, ed);
  if (month == em && day == ed) addHoliday("復活節");

  // Christian holidays relative to Easter
  long easterJDN = solarDayNumber(year, em, ed);
  auto isEasterOffset = [&](int offsetDays) {
    int y2 = 0, m2 = 0, d2 = 0;
    jdToDate((double)(easterJDN + offsetDays), y2, m2, d2);
    return (y2 == year && m2 == month && d2 == day);
  };
  if (isEasterOffset(-46)) addHoliday("聖灰星期三");
  if (isEasterOffset(-7)) addHoliday("聖枝主日");
  if (isEasterOffset(-2)) addHoliday("耶穌受難日");
  if (isEasterOffset(+1)) addHoliday("復活節星期一");
  if (isEasterOffset(+39)) addHoliday("耶穌升天節");
  if (isEasterOffset(+49)) addHoliday("聖靈降臨節");

  if (month == 1 && day == nthWeekdayOfMonth(year, 1, 1, 3)) addHoliday("馬丁路德金紀念日");      // 3rd Mon Jan
  if (month == 2 && day == nthWeekdayOfMonth(year, 2, 1, 3)) addHoliday("總統日");                // 3rd Mon Feb
  if (month == 5 && day == nthWeekdayOfMonth(year, 5, 0, 2)) addHoliday("母親節");                // 2nd Sun May
  if (month == 5 && day == lastWeekdayOfMonth(year, 5, 1)) addHoliday("陣亡將士紀念日");          // Last Mon May
  if (month == 6 && day == nthWeekdayOfMonth(year, 6, 0, 3)) addHoliday("父親節");                // 3rd Sun Jun
  if (month == 9 && day == nthWeekdayOfMonth(year, 9, 1, 1)) addHoliday("勞動節");                // 1st Mon Sep
  if (month == 10 && day == nthWeekdayOfMonth(year, 10, 1, 2)) addHoliday("哥倫布日");            // 2nd Mon Oct
  if (month == 11 && day == nthWeekdayOfMonth(year, 11, 4, 4)) addHoliday("感恩節");              // 4th Thu Nov

  // Diwali (Hindu festival of lights) — dates from Hindu lunisolar calendar (Amavasya of Kartik)
  static const struct { int16_t y; uint8_t m, d; } diwaliDates[] = {
    {2024,11,1}, {2025,10,20}, {2026,11,8}, {2027,10,29}, {2028,10,17},
    {2029,11,5}, {2030,10,26}, {2031,11,14}, {2032,11,2}, {2033,10,22},
    {2034,11,10}, {2035,10,31}, {2036,10,19}, {2037,11,7}, {2038,10,27},
    {2039,11,15}, {2040,11,3}, {2041,10,24}, {2042,11,12}, {2043,11,1},
    {2044,10,20}, {2045,11,8}, {2046,10,29}, {2047,10,17}, {2048,11,5},
    {2049,10,25}, {2050,11,13},
  };
  for (int i = 0; i < (int)(sizeof(diwaliDates)/sizeof(diwaliDates[0])); i++) {
    if (year == diwaliDates[i].y && month == diwaliDates[i].m && day == diwaliDates[i].d) {
      addHoliday("排燈節");
      break;
    }
  }

  // Eid al-Fitr (開齋節) — dates from Islamic Hijri calendar (1 Shawwal)
  static const struct { int16_t y; uint8_t m, d; } eidDates[] = {
    {2024,4,10}, {2025,3,30}, {2026,3,20}, {2027,3,9}, {2028,2,27},
    {2029,2,14}, {2030,2,4}, {2031,1,24}, {2032,1,14}, {2032,12,2},
    {2033,11,22}, {2034,11,11}, {2035,11,1}, {2036,10,20}, {2037,10,9},
    {2038,9,29}, {2039,9,18}, {2040,9,6}, {2041,8,27}, {2042,8,16},
    {2043,8,5}, {2044,7,25}, {2045,7,15}, {2046,7,4}, {2047,6,23},
    {2048,6,12}, {2049,6,1}, {2050,5,22},
  };
  for (int i = 0; i < (int)(sizeof(eidDates)/sizeof(eidDates[0])); i++) {
    if (year == eidDates[i].y && month == eidDates[i].m && day == eidDates[i].d) {
      addHoliday("開齋節");
      break;
    }
  }

  return count;
}

// ===== Custom events from SD card =====

enum CustomEventType : uint8_t { EVT_BIRTHDAY = 0, EVT_ANNUAL = 1, EVT_MEMORIAL = 2 };
enum CustomCalendar : uint8_t { CAL_SOLAR = 0, CAL_LUNAR = 1 };

struct CustomEvent {
  CustomEventType evtType;
  CustomCalendar  calendar;
  uint8_t month;
  uint8_t day;
  char name[32];    // UTF-8 name (Chinese)
  int16_t year;     // birth/start year (0 = not set)
};

static const int MAX_CUSTOM_EVENTS = 32;
static CustomEvent customEvents[MAX_CUSTOM_EVENTS];
static int customEventCount = 0;

// Load custom events from /calendar_events.csv on SD card
void loadCustomEvents() {
  customEventCount = 0;
  if (!sdCardAvailable) return;

  ScopedSDLock lock;
  File f = SD.open("/calendar_events.csv", FILE_READ);
  if (!f) {
    Serial.println("No /calendar_events.csv found");
    return;
  }

  while (f.available() && customEventCount < MAX_CUSTOM_EVENTS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.charAt(0) == '#') continue;

    // Parse: type,calendar,month,day,name[,year]
    int p1 = line.indexOf(',');
    if (p1 < 0) continue;
    int p2 = line.indexOf(',', p1 + 1);
    if (p2 < 0) continue;
    int p3 = line.indexOf(',', p2 + 1);
    if (p3 < 0) continue;
    int p4 = line.indexOf(',', p3 + 1);
    if (p4 < 0) continue;

    String sType = line.substring(0, p1);
    String sCal  = line.substring(p1 + 1, p2);
    int month    = line.substring(p2 + 1, p3).toInt();
    int day      = line.substring(p3 + 1, p4).toInt();
    sType.trim(); sCal.trim();

    // Name and optional year
    String rest = line.substring(p4 + 1);
    rest.trim();
    String sName;
    int16_t year = 0;
    int p5 = rest.lastIndexOf(',');
    if (p5 > 0) {
      // Check if the part after last comma is a number (year)
      String tail = rest.substring(p5 + 1);
      tail.trim();
      bool isNum = tail.length() > 0;
      for (unsigned i = 0; i < tail.length(); i++) {
        if (!isDigit(tail.charAt(i))) { isNum = false; break; }
      }
      if (isNum && tail.toInt() > 1800) {
        year = (int16_t)tail.toInt();
        sName = rest.substring(0, p5);
      } else {
        sName = rest;
      }
    } else {
      sName = rest;
    }
    sName.trim();

    if (month < 1 || month > 12 || day < 1 || day > 31 || sName.length() == 0) continue;

    CustomEvent& evt = customEvents[customEventCount];
    if (sType == "birthday")       evt.evtType = EVT_BIRTHDAY;
    else if (sType == "memorial")  evt.evtType = EVT_MEMORIAL;
    else                           evt.evtType = EVT_ANNUAL;

    evt.calendar = (sCal == "lunar") ? CAL_LUNAR : CAL_SOLAR;
    evt.month = (uint8_t)month;
    evt.day   = (uint8_t)day;
    evt.year  = year;
    strncpy(evt.name, sName.c_str(), sizeof(evt.name) - 1);
    evt.name[sizeof(evt.name) - 1] = '\0';
    customEventCount++;
  }
  f.close();
  Serial.printf("Loaded %d custom calendar events\n", customEventCount);
}

// Temporary buffers for custom event display strings (with age/years suffix)
static char customDispBuf[MAX_CUSTOM_EVENTS][48];

// Look up custom events matching a given date
// sYear/sMonth/sDay = solar date, lMonth/lDay = lunar date (0 if no lunar data)
int lookupCustomEvents(int sYear, int sMonth, int sDay,
                       int lMonth, int lDay,
                       const char* names[], uint8_t types[], int maxResults) {
  int count = 0;
  for (int i = 0; i < customEventCount && count < maxResults; i++) {
    const CustomEvent& evt = customEvents[i];
    bool match = false;
    if (evt.calendar == CAL_SOLAR) {
      match = (evt.month == sMonth && evt.day == sDay);
    } else {
      match = (lMonth > 0 && evt.month == lMonth && evt.day == lDay);
    }
    if (!match) continue;

    // Build display string
    if (evt.year > 0 && evt.evtType == EVT_BIRTHDAY) {
      int age = sYear - evt.year;
      if (age > 0) {
        snprintf(customDispBuf[count], sizeof(customDispBuf[count]),
                 "%s(%d歲)", evt.name, age);
      } else {
        strncpy(customDispBuf[count], evt.name, sizeof(customDispBuf[count]) - 1);
      }
    } else if (evt.year > 0 && evt.evtType == EVT_MEMORIAL) {
      int years = sYear - evt.year;
      if (years > 0) {
        snprintf(customDispBuf[count], sizeof(customDispBuf[count]),
                 "%s(第%d年)", evt.name, years);
      } else {
        strncpy(customDispBuf[count], evt.name, sizeof(customDispBuf[count]) - 1);
      }
    } else {
      strncpy(customDispBuf[count], evt.name, sizeof(customDispBuf[count]) - 1);
    }
    customDispBuf[count][sizeof(customDispBuf[count]) - 1] = '\0';
    names[count] = customDispBuf[count];
    types[count] = FEST_CUSTOM;
    count++;
  }
  return count;
}

// ===== Bitmap helper functions for font-free calendar rendering =====

// Check if a given date has any festival, holiday, or custom event (for grid dot marker)
static bool hasAnyEvent(int sYear, int sMonth, int sDay, int lMonth, int lDay, int lYear) {
  const char* tmpN[1]; uint8_t tmpT[1];
  // Check lunar festivals
  if (lMonth > 0 && lookupFestivals(lMonth, lDay, lYear, tmpN, tmpT, 1) > 0) return true;
  // Check western/movable holidays
  if (lookupUSHolidays(sYear, sMonth, sDay, tmpN, tmpT, 1) > 0) return true;
  // Check custom events from SD card
  if (lookupCustomEvents(sYear, sMonth, sDay, lMonth, lDay, tmpN, tmpT, 1) > 0) return true;
  return false;
}

// Get width of a bitmap for text at given size, 0 if not found
static int getBitmapWidth(const char* text, int size) {
  return getSystemTextWidth(text, size);
}

// Draw integer number digit-by-digit using pre-rendered bitmaps, returns total width
static int drawNumberBitmaps(int number, int x, int y, int fontSize, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", number);
  int totalW = 0;
  for (int i = 0; buf[i]; i++) {
    char digit[2] = {buf[i], 0};
    totalW += drawSystemText(digit, x + totalW, y, fontSize, color, bg);
  }
  return totalW;
}

// Calculate total pixel width of digit bitmaps for a number
static int getNumberWidth(int number, int fontSize) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", number);
  int totalW = 0;
  for (int i = 0; buf[i]; i++) {
    char digit[2] = {buf[i], 0};
    totalW += getBitmapWidth(digit, fontSize);
  }
  return totalW;
}

// Draw number centered using digit bitmaps
static void drawNumberCentered(int number, int centerX, int y, int fontSize, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE) {
  int totalW = getNumberWidth(number, fontSize);
  int x = centerX - totalW / 2;
  drawNumberBitmaps(number, x, y, fontSize, color, bg);
}

// Draw zero-padded 2-digit number using bitmaps, returns total width
static int drawPaddedNumber(int number, int x, int y, int fontSize, uint16_t color = TFT_BLACK, uint16_t bg = TFT_WHITE) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", number);
  int totalW = 0;
  for (int i = 0; buf[i]; i++) {
    char digit[2] = {buf[i], 0};
    totalW += drawSystemText(digit, x + totalW, y, fontSize, color, bg);
  }
  return totalW;
}

// ===== Year-Month selection popup =====
// Layout (540 x 960):
//   Title "選擇年月" at top
//   Year display row showing entered year
//   Number pad: 4 rows x 3 cols (1-9, 清, 0, 刪)
//   Divider
//   Month grid: 4 rows x 3 cols (一月 through 十二月)
//   Bottom: 確定 / 取消

void drawCalendarYearMonth() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);

  int W = M5.Display.width();   // 540
  int centerX = W / 2;

  // === Title ===
  drawSystemTextCentered("選擇年月", centerX, 15, 36);
  M5.Display.drawLine(10, 60, W - 10, 60, TFT_BLACK);

  // === Year display row ===
  {
    int xp = centerX - 80;
    if (ymPickerYear > 0) {
      xp += drawNumberBitmaps(ymPickerYear, xp, 72, 36);
    } else {
      // Show placeholder underscores
      for (int i = 0; i < 4; i++) {
        M5.Display.fillRect(xp + i * 28, 105, 22, 3, TFT_BLACK);
      }
      xp += 112;
    }
    drawSystemText("年", xp, 72, 36);
  }

  // === Number pad (4 rows x 3 cols) ===
  // Buttons: 1 2 3 / 4 5 6 / 7 8 9 / 清 0 刪
  int padX = 40;          // left margin
  int padY = 130;         // top of number pad
  int btnW = 140;         // button width
  int btnH = 60;          // button height
  int gapX = 15;          // horizontal gap
  int gapY = 10;          // vertical gap

  const char* padLabels[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"清", "0", "刪"}
  };

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int bx = padX + c * (btnW + gapX);
      int by = padY + r * (btnH + gapY);
      M5.Display.drawRoundRect(bx, by, btnW, btnH, 6, TFT_BLACK);
      const char* label = padLabels[r][c];
      // Use size 32 for all button labels
      drawSystemTextCentered(label, bx + btnW / 2, by + 14, 32);
    }
  }

  // === Divider ===
  int divY = padY + 4 * (btnH + gapY) + 5;
  M5.Display.drawLine(10, divY, W - 10, divY, TFT_BLACK);

  // === Month grid (4 rows x 3 cols) ===
  int monthY = divY + 15;
  int mBtnW = 140;
  int mBtnH = 56;
  int mGapX = 15;
  int mGapY = 10;
  int mPadX = 40;

  static const char* monthNames[] = {
    "1", "2", "3", "4", "5", "6",
    "7", "8", "9", "10", "11", "12"
  };

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int mIdx = r * 3 + c;  // 0-11
      int bx = mPadX + c * (mBtnW + mGapX);
      int by = monthY + r * (mBtnH + mGapY);
      bool isSelected = (mIdx + 1 == ymPickerMonth);
      if (isSelected) {
        M5.Display.fillRoundRect(bx, by, mBtnW, mBtnH, 6, TFT_BLACK);
      } else {
        M5.Display.drawRoundRect(bx, by, mBtnW, mBtnH, 6, TFT_BLACK);
      }
      uint16_t fg = isSelected ? TFT_WHITE : TFT_BLACK;
      uint16_t bg = isSelected ? TFT_BLACK : TFT_WHITE;
      drawSystemTextCentered(monthNames[mIdx], bx + mBtnW / 2, by + 12, 32, fg, bg);
    }
  }

  // === Bottom buttons: 確定 / 取消 ===
  int bottomY = monthY + 4 * (mBtnH + mGapY) + 10;
  int okX = 80, cancelX = 310;
  int bbW = 130, bbH = 50;

  // 確定 button (dark)
  M5.Display.fillRoundRect(okX, bottomY, bbW, bbH, 8, TFT_BLACK);
  drawSystemTextCentered("確定", okX + bbW / 2, bottomY + 10, 28, TFT_WHITE, TFT_BLACK);

  // 取消 button (outline)
  M5.Display.drawRoundRect(cancelX, bottomY, bbW, bbH, 8, TFT_BLACK);
  drawSystemTextCentered("取消", cancelX + bbW / 2, bottomY + 10, 28);

  M5.Display.endWrite();
  M5.Display.display();
}

// Draw month calendar picker for date selection
void drawCalendarPicker() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
  drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
  drawReturnButton();
  
  int W = M5.Display.width();   // 540
  int centerX = W / 2;
  
  // Title: Year + Month centered (digit-by-digit bitmaps)
  {
    int tw = getNumberWidth(pickerYear, 36) + getBitmapWidth("年", 36) + 10
           + getNumberWidth(pickerMonth, 36) + getBitmapWidth("月", 36);
    int xp = centerX - tw / 2;
    xp += drawNumberBitmaps(pickerYear, xp, 15, 36);
    xp += drawSystemText("年", xp, 15, 36);
    xp += 10;
    xp += drawNumberBitmaps(pickerMonth, xp, 15, 36);
    drawSystemText("月", xp, 15, 36);
  }
  
  // Horizontal line
  M5.Display.drawLine(10, 65, W - 10, 65, TFT_BLACK);
  
  // Day of week headers
  const char* headers[] = {"日", "一", "二", "三", "四", "五", "六"};
  int cellW = W / 7;
  int headerY = 75;
  for (int i = 0; i < 7; i++) {
    uint16_t color = (i == 0) ? EPD_DARK_GRAY : TFT_BLACK;  // Sunday in dark gray
    drawSystemTextCentered(headers[i], cellW / 2 + i * cellW, headerY, 32);
  }
  
  // Calendar grid
  int totalDays = solarMonthDays(pickerYear, pickerMonth);
  int firstDow = dayOfWeek(pickerYear, pickerMonth, 1);
  bool isGregorianCutover = (pickerYear == 1582 && pickerMonth == 10);
  
  int cellH = 115;  // Height per row
  int startY = 120;
  int row = 0, col = firstDow;
  
  // Get today's date for highlighting
  struct tm ti;
  getLocalTime(&ti);
  int todayY = ti.tm_year + 1900, todayM = ti.tm_mon + 1, todayD = ti.tm_mday;
  
  for (int d = 1; d <= totalDays; d++) {
    // Skip Oct 5-14, 1582 (Gregorian reform: these dates don't exist)
    if (isGregorianCutover && d >= 5 && d <= 14) {
      if (d == 14) {
        // After skipping 5-14, reset col to weekday of Oct 15 (Friday)
        col = dayOfWeek(1582, 10, 15);
      }
      continue;
    }
    int cx = col * cellW + cellW / 2;
    int cy = startY + row * cellH;
    
    bool isToday = (pickerYear == todayY && pickerMonth == todayM && d == todayD);
    bool isSelected = (pickerYear == pickerSelectedYear && pickerMonth == pickerSelectedMonth && d == pickerSelectedDay);
    
    // Highlight selected date (filled)
    if (isSelected) {
      M5.Display.fillRoundRect(col * cellW + 4, cy - 5, cellW - 8, cellH - 6, 6, TFT_BLACK);
    } else if (isToday) {
      // Outline today
      M5.Display.drawRoundRect(col * cellW + 4, cy - 5, cellW - 8, cellH - 6, 6, TFT_BLACK);
    }
    
    // Solar day number
    char dayStr[8];
    snprintf(dayStr, sizeof(dayStr), "%d", d);
    uint16_t dayColor = (isSelected || isToday) ? TFT_WHITE : (col == 0 ? EPD_DARK_GRAY : TFT_BLACK);
    uint16_t dayBg = (isSelected || isToday) ? TFT_BLACK : TFT_WHITE;
    drawSystemTextCentered(dayStr, cx, cy, 36, dayColor, dayBg);
    
    // Lunar day below (show festival name if available) — only when lunar data is valid
    if (hasLunarData(pickerYear, pickerMonth, d)) {
      LunarDate ld = solarToLunar(pickerYear, pickerMonth, d);
      const char* gridFest = getGridFestival(ld.month, ld.day, ld.year);
      const char* lunarStr;
      if (gridFest) {
        lunarStr = gridFest;
      } else if (ld.day == 1) {
        lunarStr = lunarMonthName(ld.month);
      } else {
        lunarStr = lunarDayName(ld.day);
      }
      drawSystemTextCentered(lunarStr, cx, cy + 42, 22, dayColor, dayBg);
      
      // 朔/望 markers
      if (ld.day == 1) {
        drawSystemTextCentered("朔", cx, cy + 68, 20, (isSelected || isToday) ? TFT_WHITE : EPD_DARK_GRAY, dayBg);
      } else if (ld.day == 15) {
        drawSystemTextCentered("望", cx, cy + 68, 20, (isSelected || isToday) ? TFT_WHITE : EPD_DARK_GRAY, dayBg);
      }
    }
    
    // Solar term check (works for any year)
    const char* st = getSolarTerm(pickerYear, pickerMonth, d);
    if (st) {
      drawSystemTextCentered(st, cx, cy + 68, 20, (isSelected || isToday) ? TFT_WHITE : EPD_DARK_GRAY, dayBg);
    }

    // Holiday/event dot marker
    {
      int lm = 0, ld = 0, ly = 0;
      if (hasLunarData(pickerYear, pickerMonth, d)) {
        LunarDate tmpLd = solarToLunar(pickerYear, pickerMonth, d);
        lm = tmpLd.month; ld = tmpLd.day; ly = tmpLd.year;
      }
      if (hasAnyEvent(pickerYear, pickerMonth, d, lm, ld, ly)) {
        uint16_t dotColor = (isSelected || isToday) ? TFT_WHITE : EPD_DARK_GRAY;
        M5.Display.fillCircle(cx + cellW / 2 - 10, cy + 2, 3, dotColor);
      }
    }
    
    col++;
    if (col >= 7) { col = 0; row++; }
  }
  
  // "今天" (Today) button
  int todayBtnX = 200, todayBtnY = 900, todayBtnW = 100, todayBtnH = 44;
  M5.Display.drawRoundRect(todayBtnX, todayBtnY, todayBtnW, todayBtnH, 6, TFT_BLACK);
  drawSystemTextCentered("今天", todayBtnX + todayBtnW/2, todayBtnY + 8, 28);
  
  M5.Display.endWrite();
  M5.Display.display();
}

// Helper: get date with offset from today (pure math, no mktime)
void getDateWithOffset(int offset, int &outYear, int &outMonth, int &outDay, int &outWday) {
  // Get today's date from system clock
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int todayY = timeinfo.tm_year + 1900;
  int todayM = timeinfo.tm_mon + 1;
  int todayD = timeinfo.tm_mday;
  
  // Calculate target JDN using pure math
  long todayJDN = solarDayNumber(todayY, todayM, todayD);
  long targetJDN = todayJDN + offset;
  
  // Convert JDN back to Gregorian date (algorithm from Meeus)
  long l = targetJDN + 68569;
  long n = (4 * l) / 146097;
  l = l - (146097 * n + 3) / 4;
  long i = (4000 * (l + 1)) / 1461001;
  l = l - (1461 * i) / 4 + 31;
  long j = (80 * l) / 2447;
  outDay = (int)(l - (2447 * j) / 80);
  l = j / 11;
  outMonth = (int)(j + 2 - 12 * l);
  outYear = (int)(100 * (n - 49) + i + l);
  
  outWday = dayOfWeek(outYear, outMonth, outDay);
}

// Draw the Chinese Almanac page
void drawCalendar() {
  Serial.println("drawCalendar() start");
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  
  // Status bar + nav bar first
  drawStatusBar();
  drawNavIcon("back.png", NAV_PREV_X, NAV_Y);
  drawNavIcon("next.png", NAV_NEXT_X, NAV_Y);
  drawReturnButton();
  
  // Get current time with offset
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("drawCalendar: getLocalTime failed");
    drawSystemTextCentered("無法取得時間", 270, 400, 36);
    M5.Display.endWrite();
    M5.Display.display();
    return;
  }
  
  int sYear, sMonth, sDay, wday;
  getDateWithOffset(calendarDayOffset, sYear, sMonth, sDay, wday);
  Serial.printf("drawCalendar: date %d/%d/%d wday=%d offset=%d\n", sYear, sMonth, sDay, wday, calendarDayOffset);
  
  // Lunar data is only valid for 1900-2100
  bool hasLunar = hasLunarData(sYear, sMonth, sDay);

  LunarDate lunar = {0, 0, 0, false};
  char yearGZ[16] = "", monthGZ[16] = "", dayGZ[16] = "";
  int dayGanIdx = 0, dayZhiIdx = 0;
  const char* zodiac = "";
  int gzYear = sYear;  // GanZhi year based on 立春 boundary (default = solar year)
  const char* solarTerm = getSolarTerm(sYear, sMonth, sDay);  // solar terms work for any year
  const char* nextTerm = nullptr;
  int daysToNext = 0;
  const char* currentTerm = getCurrentSolarTerm(sYear, sMonth, sDay, &nextTerm, &daysToNext);

  if (hasLunar) {
    lunar = solarToLunar(sYear, sMonth, sDay);
    Serial.printf("drawCalendar: lunar %d/%d/%d\n", lunar.year, lunar.month, lunar.day);
    // Determine GanZhi year based on 立春 boundary (per GB/T 33661-2017)
    // GanZhi year changes at 立春 (solar term index 2), NOT at lunar new year (春節)
    computeTermDates(sYear);
    int lichunM = cachedTermDates[2] / 100;
    int lichunD = cachedTermDates[2] % 100;
    if (sMonth < lichunM || (sMonth == lichunM && sDay < lichunD)) {
      gzYear = sYear - 1;
    }
    getYearGanZhi(gzYear, yearGZ);
    getMonthGanZhi(sYear, sMonth, sDay, monthGZ);
    getDayGanZhi(sYear, sMonth, sDay, dayGZ);
    getDayGanZhiIdx(sYear, sMonth, sDay, dayGanIdx, dayZhiIdx);
    zodiac = getZodiac(gzYear);
  }
  Serial.println("drawCalendar: calculations done, drawing...");
  
  int W = M5.Display.width();   // 540
  int centerX = W / 2;          // 270
  
  // ===== TOP SECTION: Year and Month =====
  // Year line (digit-by-digit bitmaps)
  drawNumberCentered(sYear, centerX, 10, 32);
  
  // Month (Arabic number): "1月" "12月" etc.
  {
    int mw = getNumberWidth(sMonth, 26) + getBitmapWidth("月", 26);
    int mx = centerX - mw / 2;
    mx += drawNumberBitmaps(sMonth, mx, 45, 26);
    drawSystemText("月", mx, 45, 26);
  }
  
  // Horizontal line
  M5.Display.drawLine(20, 80, W - 20, 80, TFT_BLACK);
  
  // ===== LARGE DATE NUMBER =====
  drawNumberCentered(sDay, centerX, 90, 160);
  
  // Left side: Year GanZhi + Zodiac — enlarged text — only for 1900+
  if (hasLunar) {
    {
      int yearGanIdx3 = (gzYear - 4) % 10;
      int yearZhiIdx3 = (gzYear - 4) % 12;
      int xp = 12;
      xp += drawSystemText(tianGan[yearGanIdx3], xp, 92, 40);
      xp += drawSystemText(diZhi[yearZhiIdx3], xp, 92, 40);
      drawSystemText("年", xp, 92, 40);
    }
    {
      int xp = 12;
      xp += drawSystemText(zodiac, xp, 138, 40);
      drawSystemText("年", xp, 138, 40);
    }
  }
  
  // Right side: Solar term + festivals — only for 1900+
  {
    int rightX = W - 150;  // Right column x position
    int rightY = 92;
    
    // Solar term (if today is the exact date)
    if (solarTerm) {
      drawSystemText(solarTerm, rightX, rightY, 34, EPD_DARK_GRAY);
      rightY += 42;
    }
    
    // All festivals (民俗/道教/佛教/西方/自訂) next to big day
    const char* festNames[12];
    uint8_t festTypes[12];
    int festCount = 0;
    if (hasLunar) {
      festCount += lookupFestivals(lunar.month, lunar.day, lunar.year,
                                   festNames + festCount, festTypes + festCount, 12 - festCount);
    }
    festCount += lookupUSHolidays(sYear, sMonth, sDay,
                                  festNames + festCount, festTypes + festCount, 12 - festCount);
    festCount += lookupCustomEvents(sYear, sMonth, sDay,
                                    hasLunar ? lunar.month : 0, hasLunar ? lunar.day : 0,
                                    festNames + festCount, festTypes + festCount, 12 - festCount);
    for (int i = 0; i < festCount && i < 4; i++) {
      drawSystemText(festNames[i], rightX, rightY, 28);
      rightY += 36;
    }
  }
  
  // ===== Horizontal divider =====  
  M5.Display.drawLine(20, 265, W - 20, 265, TFT_BLACK);
  
  // ===== MIDDLE SECTION: Lunar date and day of week =====
  // Dark banner bar — vertically center text
  int bannerH = 55;
  int bannerY = 270;
  int bannerTextSize = 30;
  int bannerTextY = bannerY + (bannerH - bannerTextSize) / 2;
  M5.Display.fillRect(0, bannerY, W, bannerH, TFT_BLACK);
  
  if (hasLunar) {
    // Lunar date (left side of banner) with 朔/望 marker (bitmap components)
    int xp = 20;
    if (lunar.isLeapMonth) {
      xp += drawSystemText("閏", xp, bannerTextY, bannerTextSize, TFT_WHITE, TFT_BLACK);
    }
    xp += drawSystemText(lunarMonthName(lunar.month), xp, bannerTextY, bannerTextSize, TFT_WHITE, TFT_BLACK);
    xp += drawSystemText(lunarDayName(lunar.day), xp, bannerTextY, bannerTextSize, TFT_WHITE, TFT_BLACK);
    if (lunar.day == 1) {
      xp += 6;
      drawSystemText("朔", xp, bannerTextY, bannerTextSize, TFT_WHITE, TFT_BLACK);
    } else if (lunar.day == 15) {
      xp += 6;
      drawSystemText("望", xp, bannerTextY, bannerTextSize, TFT_WHITE, TFT_BLACK);
    }
  }
  
  // Day of week (right side of banner)
  {
    static const char* weekNames[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    int wkX = hasLunar ? (W - 160) : 20;
    drawSystemText(weekNames[wday], wkX, bannerTextY, bannerTextSize, TFT_WHITE, TFT_BLACK);
  }
  
  // ===== Festival display (道教/民俗/佛教/西方/自訂節日) =====
  {
    const char* festNames[12];
    uint8_t festTypes[12];
    int festCount = 0;
    if (hasLunar) {
      festCount += lookupFestivals(lunar.month, lunar.day, lunar.year,
                                   festNames + festCount, festTypes + festCount, 12 - festCount);
    }
    festCount += lookupUSHolidays(sYear, sMonth, sDay,
                                  festNames + festCount, festTypes + festCount, 12 - festCount);
    festCount += lookupCustomEvents(sYear, sMonth, sDay,
                                    hasLunar ? lunar.month : 0, hasLunar ? lunar.day : 0,
                                    festNames + festCount, festTypes + festCount, 12 - festCount);

    if (festCount > 0) {
      int fy = 333;
      const char* catNames[] = {"民俗", "道教", "佛教", "西方", "個人"};
      int xp = 20;
      for (int i = 0; i < festCount && i < 4; i++) {
        if (i > 0) xp += 12;
        xp += drawSystemText(catNames[festTypes[i]], xp, fy, 20, EPD_DARK_GRAY);
        xp += 2;
        xp += drawSystemText(festNames[i], xp, fy, 20);
      }
    }
  }
  
  // Lunar year info line (bitmap components, centered) — only for 1900+
  if (hasLunar) {
    const char* sizeStr = (lunarMonthDays(lunar.year, lunar.month) == 30) ? "大" : "小";
    int nlSize = 30;
    int tw = getBitmapWidth("農曆", nlSize) + getBitmapWidth(yearGZ, nlSize) + getBitmapWidth(zodiac, nlSize)
           + getBitmapWidth("年", nlSize) + 6 + getBitmapWidth(monthGZ, nlSize) + getBitmapWidth("月", nlSize)
           + 6 + getBitmapWidth(dayGZ, nlSize) + getBitmapWidth("日", nlSize) + 6 + getBitmapWidth(sizeStr, nlSize);
    int xp = centerX - tw / 2;
    xp += drawSystemText("農曆", xp, 360, nlSize);
    xp += drawSystemText(yearGZ, xp, 360, nlSize);
    xp += drawSystemText(zodiac, xp, 360, nlSize);
    xp += drawSystemText("年", xp, 360, nlSize);
    xp += 6;
    xp += drawSystemText(monthGZ, xp, 360, nlSize);
    xp += drawSystemText("月", xp, 360, nlSize);
    xp += 6;
    xp += drawSystemText(dayGZ, xp, 360, nlSize);
    xp += drawSystemText("日", xp, 360, nlSize);
    xp += 6;
    drawSystemText(sizeStr, xp, 360, nlSize);
  
  // ===== Horizontal divider =====
  M5.Display.drawLine(20, 390, W - 20, 390, TFT_BLACK);
  
  // ===== BOTTOM SECTION: GanZhi, Yi/Ji, Hours =====
  int secY = 400;
  
  // --- Info box with 五行, 沖煞, 六曜, 胎神, 喜福財神 ---
  M5.Display.drawRect(18, secY, W - 36, 210, TFT_BLACK);
  
  // Year GanZhi + NaYin
  int yearGanIdx2 = (gzYear - 4) % 10;
  int yearZhiIdx2 = (gzYear - 4) % 12;
  
  // Fixed column positions for alignment
  int colLabel = 30;   // 年柱：/ 月柱：/ 日柱：
  int colGZ = 100;     // GanZhi characters
  int colNaYin = 155;  // NaYin text
  
  {
    drawSystemText("年柱：", colLabel, secY + 5, 22);
    drawSystemText(yearGZ, colGZ, secY + 5, 22);
    drawSystemText(getNaYin(yearGanIdx2, yearZhiIdx2), colNaYin, secY + 5, 22);
  }
  
  // Month GanZhi + NaYin
  int monthGanBase2;
  int yearGan2 = (gzYear - 4) % 10;
  switch (yearGan2 % 5) {
    case 0: monthGanBase2 = 2; break;
    case 1: monthGanBase2 = 4; break;
    case 2: monthGanBase2 = 6; break;
    case 3: monthGanBase2 = 8; break;
    case 4: monthGanBase2 = 0; break;
    default: monthGanBase2 = 0;
  }
  int mGanIdx = (monthGanBase2 + sMonth - 1) % 10;
  int mZhiIdx = (sMonth + 1) % 12;
  {
    drawSystemText("月柱：", colLabel, secY + 32, 22);
    drawSystemText(monthGZ, colGZ, secY + 32, 22);
    drawSystemText(getNaYin(mGanIdx, mZhiIdx), colNaYin, secY + 32, 22);
  }
  
  // Day GanZhi + NaYin
  {
    drawSystemText("日柱：", colLabel, secY + 59, 22);
    drawSystemText(dayGZ, colGZ, secY + 59, 22);
    drawSystemText(getNaYin(dayGanIdx, dayZhiIdx), colNaYin, secY + 59, 22);
  }
  
  // Right column: 六曜
  int colRLabel = 300;  // Right column label
  int colRVal = 370;    // Right column value
  {
    const char* liuyao = getLiuYao(lunar.month, lunar.day);
    drawSystemText("六曜：", colRLabel, secY + 5, 22);
    drawSystemText(liuyao, colRVal, secY + 5, 22);
  }
  
  // 沖煞
  {
    int xp = 300;
    xp += drawSystemText(getClash(dayZhiIdx), xp, secY + 32, 22);
    xp += 6;
    drawSystemText(getSha(dayZhiIdx), xp, secY + 32, 22);
  }
  
  // 節氣 (always show current + countdown to next, bitmap components)
  drawSystemText("節氣：", colRLabel, secY + 59, 22);
  if (solarTerm) {
    int xp = colRVal;
    xp += drawSystemText("今日", xp, secY + 59, 22, EPD_DARK_GRAY);
    drawSystemText(solarTerm, xp, secY + 59, 22, EPD_DARK_GRAY);
  } else {
    int xp = colRVal;
    xp += drawSystemText(currentTerm, xp, secY + 59, 20);
    xp += drawSystemText("→", xp, secY + 59, 20);
    xp += drawSystemText(nextTerm, xp, secY + 59, 20);
    xp += 3;
    xp += drawNumberBitmaps(daysToNext, xp, secY + 59, 20);
    drawSystemText("天", xp, secY + 59, 20);
  }
  
  // Vertical divider
  M5.Display.drawLine(290, secY, 290, secY + 85, TFT_BLACK);
  // Horizontal divider
  M5.Display.drawLine(18, secY + 86, W - 18, secY + 86, TFT_BLACK);
  
  // Bottom section: 喜神 福神 財神 (fixed columns)
  {
    drawSystemText("喜神：", 30, secY + 92, 22);
    drawSystemText(getXiShen(dayGanIdx), 100, secY + 92, 22);
  }
  {
    drawSystemText("福神：", 200, secY + 92, 22);
    drawSystemText(getFuShen(dayGanIdx), 270, secY + 92, 22);
  }
  {
    drawSystemText("財神：", 370, secY + 92, 22);
    drawSystemText(getCaiShen(dayGanIdx), 440, secY + 92, 22);
  }
  
  // Horizontal divider before 胎神
  M5.Display.drawLine(18, secY + 120, W - 18, secY + 120, TFT_BLACK);
  
  // 胎神
  {
    drawSystemText("胎神：", 30, secY + 126, 22);
    drawSystemText(getTaiShen(dayGanIdx, dayZhiIdx), 100, secY + 126, 22);
  }
  
  // 彭祖百忌 (day-based taboo)
  {
    const char* pengzu_gan[] = {"甲不開倉", "乙不栽植", "丙不修灶", "丁不剃頭", "戊不受田",
                                 "己不破券", "庚不經絡", "辛不合醬", "壬不汲水", "癸不詞訟"};
    const char* pengzu_zhi[] = {"子不問卜", "丑不冠帶", "寅不祭祀", "卯不穿井", "辰不哭泣", "巳不遠行",
                                 "午不苗蓋", "未不服藥", "申不安床", "酉不會客", "戌不吃犬", "亥不嫁娶"};
    drawSystemText("彭祖：", 30, secY + 160, 22);
    int xp = 100;
    xp += drawSystemText(pengzu_gan[dayGanIdx], xp, secY + 160, 22);
    xp += 6;
    drawSystemText(pengzu_zhi[dayZhiIdx], xp, secY + 160, 22);
  }
  
  secY += 220;
  
  // ===== 宜 (Auspicious) =====
  M5.Display.fillRect(18, secY, 60, 55, TFT_BLACK);
  drawSystemTextCentered("宜", 48, secY + 10, 34, TFT_WHITE, TFT_BLACK);
  
  const char* yiText = getYiActivities(dayGanIdx, dayZhiIdx);
  drawSystemText(yiText, 90, secY + 8, 32);
  
  secY += 65;
  
  // ===== 忌 (Inauspicious) =====
  M5.Display.fillRect(18, secY, 60, 55, TFT_BLACK);
  drawSystemTextCentered("忌", 48, secY + 10, 34, TFT_WHITE, TFT_BLACK);
  
  const char* jiText = getJiActivities(dayGanIdx, dayZhiIdx);
  drawSystemText(jiText, 90, secY + 8, 32);
  
  secY += 65;
  
  // ===== Horizontal divider =====
  M5.Display.drawLine(20, secY, W - 20, secY, TFT_BLACK);
  secY += 5;
  
  // ===== 時辰吉凶 (Hourly fortune) =====
  drawSystemText("時辰吉凶", 20, secY, 24);
  secY += 32;
  
  char hourGZ[12][16];
  char hourZhi[12][8];
  getHourInfo(dayGanIdx, hourGZ, hourZhi);
  
  // Draw all 12 hours in a single row — enlarged
  int colW = (W - 20) / 12;
  int hourFontSize = 20;
  
  for (int idx = 0; idx < 12; idx++) {
    int hx = 10 + idx * colW;
    int hy = secY;
    
    // Full GanZhi (e.g. 丙子, 丁丑)
    drawSystemText(hourGZ[idx], hx + 2, hy, hourFontSize);
    
    // Fortune (吉/凶)
    int daySum = dayGanIdx + dayZhiIdx + idx;
    bool isJi = (daySum % 3 != 0);
    if (isJi) {
      drawSystemText("吉", hx + 2, hy + 26, hourFontSize);
    } else {
      drawSystemText("凶", hx + 2, hy + 26, hourFontSize, 0x7BEF);
    }
  }
  
  secY += 55;
  
  // ===== Auspicious numbers (bitmap components, centered) =====
  {
    int n1 = (dayGanIdx * 3 + 1) % 49 + 1;
    int n2 = (dayZhiIdx * 7 + 3) % 49 + 1;
    int n3 = (dayGanIdx + dayZhiIdx + sDay) % 49 + 1;
    int n4 = (n1 + n2) % 49 + 1;
    int n5 = (sYear + sMonth + sDay) % 49 + 1;
    int dw = getBitmapWidth("0", 20);
    int titleW = getBitmapWidth("今日吉數", 20);
    int tw = titleW + 6 + (dw * 2 + 6) * 5 - 6;
    int xp = centerX - tw / 2;
    xp += drawSystemText("今日吉數", xp, secY, 20);
    xp += 6;
    int nums[] = {n1, n2, n3, n4, n5};
    for (int i = 0; i < 5; i++) {
      xp += drawPaddedNumber(nums[i], xp, secY, 20);
      if (i < 4) xp += 6;
    }
  }
  
  } // end hasLunar - all lunar-dependent almanac sections

  // Solar term line (for pre-1900 dates, show solar term info)
  if (!hasLunar && currentTerm) {
    int secYSolar = 370;
    drawSystemText("節氣：", 30, secYSolar, 22);
    if (solarTerm) {
      int xp = 100;
      xp += drawSystemText("今日", xp, secYSolar, 22, EPD_DARK_GRAY);
      drawSystemText(solarTerm, xp, secYSolar, 22, EPD_DARK_GRAY);
    } else {
      int xp = 100;
      xp += drawSystemText(currentTerm, xp, secYSolar, 20);
      xp += drawSystemText("→", xp, secYSolar, 20);
      xp += drawSystemText(nextTerm, xp, secYSolar, 20);
      xp += 3;
      xp += drawNumberBitmaps(daysToNext, xp, secYSolar, 20);
      drawSystemText("天", xp, secYSolar, 20);
    }
  }

  // Note for non-Beijing timezones: solar term dates may differ by one day
  if (timeConfig.gmtOffset != 28800) {
    drawSystemText("節氣依當地時間，與北京或差一天", 20, 940, 18, EPD_LIGHT_GRAY);
  }

  M5.Display.endWrite();
  M5.Display.display();
  Serial.println("drawCalendar() done");
}
