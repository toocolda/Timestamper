#include <Arduino.h>
#include <TinyGPS++.h>

#include "hardware/backlight.h"
#include "hardware/battery.h"
#include "hardware/buttons.h"
#include "core/config.h"
#include "time/local_time.h"
#include "time/mcu_time.h"
#include "display/st7036.h"
#include "features/stopwatch.h"
#include "time/time_edit.h"
#include "features/timer.h"
#include "features/timestamp.h"
#include "hardware/buzzer.h"

// Module architecture:
// - Maintains mode-global UI state (selected mode + redraw epoch)
// - Uses per-mode cache signatures to minimize LCD writes
// - Owns button event routing for mode-specific behavior

// Forward declarations from main.cpp
extern ST7036 lcd;
extern TinyGPSPlus gps;

// ===== Global Mode Variables =====
uint8_t g_currentMode = MODE_UTC_ONLY;
uint32_t g_modeEpoch = 1;  // Start at 1 so first display triggers a clear and resets all caches

// ===== Buzzer Control =====
void buzzOnce(uint16_t durationMs) {
  buzzerStart(1000);  // 1000 Hz tone
  delay(durationMs);
  buzzerStop();
}

static void buzzTimestampStamp() {
  // Distinct two-tone stamp confirmation.
  buzzerStart(1760);
  delay(55);
  buzzerStop();
  delay(20);
  buzzerStart(1175);
  delay(85);
  buzzerStop();
}

// Build compact GPS status token used in the status line:
// NO=no reliable date/time, FX=3D fix, AC=acquiring, TI=time-only input/no sats.
static void buildGpsStatus(char out[3], bool timeReliable, int satCount) {
  if (!timeReliable) {
    strcpy(out, "NO");
  } else if (gps.location.isValid()) {
    strcpy(out, "FX");
  } else if (satCount > 0) {
    strcpy(out, "AC");
  } else {
    strcpy(out, "TI");
  }
}

// Keep MCU clock disciplined to GPS when valid, unless manual-set grace period is active.
static void syncMcuFromGpsIfAllowed(bool timeReliable) {
  if (!timeReliable || shouldSkipGPSSync()) {
    return;
  }

  TimeEdit_t gpsTime;
  gpsTime.year = gps.date.year();
  gpsTime.month = gps.date.month();
  gpsTime.day = gps.date.day();
  gpsTime.hour = gps.time.hour();
  gpsTime.minute = gps.time.minute();
  gpsTime.second = gps.time.second();
  mcuTimeSync(&gpsTime);
}

// ===== GPS Validation Helper =====
bool isGPSTimeReliable() {
  if (!gps.time.isValid() || !gps.date.isValid()) {
    return false;
  }
  
  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();
  
  // Sanity check: year should be reasonable (>= 2020)
  if (year < 2020) return false;
  
  // Month should be 1-12
  if (month < 1 || month > 12) return false;
  
  // Day should be 1-31
  if (day < 1 || day > 31) return false;
  
  return true;
}

// ===== Mode: UTC Only =====
void displayModeUTCOnly() {
  static int lastSec = -1;
  static int lastSat = -1;
  static bool lastTimeValid = false;
  static uint32_t lastEpoch = 0;
  static bool lastEditMode = false;
  
  // Reset cache if mode changed
  if (lastEpoch != g_modeEpoch) {
    lastSec = -1;
    lastSat = -1;
    lastTimeValid = false;
    lastEpoch = g_modeEpoch;
    lastEditMode = false;
    lcd.clear();
  }
  
  bool editMode = timeEditIsActive();
  
  // If entering/exiting edit mode, clear display and reset cache
  if (editMode != lastEditMode) {
    lcd.clear();
    lastEditMode = editMode;
    if (!editMode) {
      // Just exited edit mode - reset display cache to force redraw
      lastSec = -1;
      lastSat = -1;
      lastTimeValid = false;
    }
  }
  
  if (editMode) {
    // ===== EDIT MODE DISPLAY =====
    TimeEdit_t editData = timeEditGetData();
    EditField_t field = timeEditGetCurrentField();
    bool shouldShow = timeEditShouldFlash();
    
    lcd.setCursor(0, 0);
    char buf[LCD_BUF_SIZE];
    
    // Display all fields with current field flashing
    char year_str[5] = "YYYY";
    char month_str[3] = "MM";
    char day_str[3] = "DD";
    char hour_str[3] = "HH";
    char min_str[3] = "MM";
    char sec_str[3] = "SS";
    
    if (field == EDIT_FIELD_YEAR && shouldShow) {
      snprintf(year_str, sizeof(year_str), "%04d", editData.year);
    } else if (field == EDIT_FIELD_YEAR && !shouldShow) {
      strcpy(year_str, "    ");
    } else {
      snprintf(year_str, sizeof(year_str), "%04d", editData.year);
    }
    
    if (field == EDIT_FIELD_MONTH && shouldShow) {
      snprintf(month_str, sizeof(month_str), "%02d", editData.month);
    } else if (field == EDIT_FIELD_MONTH && !shouldShow) {
      strcpy(month_str, "  ");
    } else {
      snprintf(month_str, sizeof(month_str), "%02d", editData.month);
    }
    
    if (field == EDIT_FIELD_DAY && shouldShow) {
      snprintf(day_str, sizeof(day_str), "%02d", editData.day);
    } else if (field == EDIT_FIELD_DAY && !shouldShow) {
      strcpy(day_str, "  ");
    } else {
      snprintf(day_str, sizeof(day_str), "%02d", editData.day);
    }
    
    if (field == EDIT_FIELD_HOUR && shouldShow) {
      snprintf(hour_str, sizeof(hour_str), "%02d", editData.hour);
    } else if (field == EDIT_FIELD_HOUR && !shouldShow) {
      strcpy(hour_str, "  ");
    } else {
      snprintf(hour_str, sizeof(hour_str), "%02d", editData.hour);
    }
    
    if (field == EDIT_FIELD_MINUTE && shouldShow) {
      snprintf(min_str, sizeof(min_str), "%02d", editData.minute);
    } else if (field == EDIT_FIELD_MINUTE && !shouldShow) {
      strcpy(min_str, "  ");
    } else {
      snprintf(min_str, sizeof(min_str), "%02d", editData.minute);
    }
    
    if (field == EDIT_FIELD_SECOND && shouldShow) {
      snprintf(sec_str, sizeof(sec_str), "%02d", editData.second);
    } else if (field == EDIT_FIELD_SECOND && !shouldShow) {
      strcpy(sec_str, "  ");
    } else {
      snprintf(sec_str, sizeof(sec_str), "%02d", editData.second);
    }
    
    snprintf(buf, LCD_BUF_SIZE, "%s-%s-%s %s:%s:%sZ",
             year_str, month_str, day_str, hour_str, min_str, sec_str);
    lcd.print(buf);
    
    // Show GPS status on line 2 as hint (blinking field is visual hint)
    lcd.setCursor(0, 1);
    int sat = gps.satellites.value();
    bool timeReliable = isGPSTimeReliable();
    char gpsStatus[3];
    buildGpsStatus(gpsStatus, timeReliable, sat);
    uint8_t batPercent = batteryGetPercentage();
    snprintf(buf, LCD_BUF_SIZE, "GPS:%s SAT:%02d BAT:%02d", gpsStatus, sat, batPercent);
    lcd.print(buf);
    
  } else {
    // ===== NORMAL DISPLAY - Uses MCU time (ticks independently) =====
    // GPS sync happens here: MCU clock is continuously synced to GPS when GPS signal is valid
    // This allows GPS to provide periodic correction while MCU provides independent ticking
    bool timeValid = isGPSTimeReliable();
    int sat = gps.satellites.value();
    
    // GPS SYNC: Every display update, if GPS is valid, MCU time syncs to GPS
    // (see line ~168 in modes.cpp, displayModeUTCOnly function)
    // BUT: Skip GPS sync if manual time was just set (5 second grace period)
    syncMcuFromGpsIfAllowed(timeValid);
    
    // Get current time from MCU (ticks based on elapsed ms)
    TimeEdit_t currentTime = mcuTimeGetCurrent();
    int h = currentTime.hour;
    int m = currentTime.minute;
    int s = currentTime.second;
    
    char gpsStatus[3];
    buildGpsStatus(gpsStatus, timeValid, sat);
    
    if (s != lastSec || sat != lastSat || timeValid != lastTimeValid) {
      lcd.setCursor(0, 0);
      
      char buf[LCD_BUF_SIZE];
      if (timeValid || hasManualTime()) {
        // Display with MCU time (which keeps ticking)
        snprintf(buf, LCD_BUF_SIZE,
                 "%04d-%02d-%02d %02d:%02d:%02dZ",
                 currentTime.year, currentTime.month, currentTime.day,
                 h, m, s);
      } else {
        snprintf(buf, LCD_BUF_SIZE,
                 "YYYY-MM-DD HH:MM:SSZ");
      }
      lcd.print(buf);
      
      lcd.setCursor(0, 1);
      char buf2[LCD_BUF_SIZE];
      uint8_t batPercent = batteryGetPercentage();
      snprintf(buf2, LCD_BUF_SIZE,
               "GPS:%s SAT:%02d BAT:%02d",
               gpsStatus, sat, batPercent);
      lcd.print(buf2);
      
      lastSec = s;
      lastSat = sat;
      lastTimeValid = timeValid;
    }
  }
}

// ===== Mode: UTC & Local =====
void displayModeUTCLocal() {
  static int lastSecUTC = -1;
  static int lastSecLocal = -1;
  static int lastOffsetShown = 127;
  static bool lastTimeValid = false;
  static uint32_t lastEpoch = 0;
  static bool lastOffsetEditMode = false;
  
  // Reset cache if mode changed
  if (lastEpoch != g_modeEpoch) {
    lastSecUTC = -1;
    lastSecLocal = -1;
    lastOffsetShown = 127;
    lastTimeValid = false;
    lastEpoch = g_modeEpoch;
    lastOffsetEditMode = false;
    lcd.clear();
  }
  
  bool offsetEditMode = offsetEditIsActive();
  
  // If entering/exiting offset edit mode, clear display and reset cache
  if (offsetEditMode != lastOffsetEditMode) {
    lcd.clear();
    lastOffsetEditMode = offsetEditMode;
    if (!offsetEditMode) {
      // Just exited edit mode - reset display cache to force redraw
      lastSecUTC = -1;
      lastSecLocal = -1;
      lastOffsetShown = 127;
      lastTimeValid = false;
    }
  }

  bool timeValid = isGPSTimeReliable();
  syncMcuFromGpsIfAllowed(timeValid);
  
  if (offsetEditMode) {
    // ===== OFFSET EDIT DISPLAY =====
    TimeEdit_t utcTime = mcuTimeGetCurrent();
    int8_t currentOffset = offsetEditGetValue();
    bool shouldShowOffset = offsetEditShouldFlash();
    TimeEdit_t localTime = calculateLocalTimeWithOffset(utcTime, currentOffset);
    
    // Line 1: UTC time (MM-DD HH:MM:SS UTC)
    lcd.setCursor(0, 0);
    char buf1[LCD_BUF_SIZE];
    snprintf(buf1, LCD_BUF_SIZE, "%02d-%02d %02d:%02d:%02d UTC",
             utcTime.month, utcTime.day, utcTime.hour, utcTime.minute, utcTime.second);
    lcd.print(buf1);
    
    // Line 2: Local time with blinking offset
    lcd.setCursor(0, 1);
    char buf2[LCD_BUF_SIZE];
    char offsetStr[5];
    if (shouldShowOffset) {
      snprintf(offsetStr, sizeof(offsetStr), "%+03d", currentOffset);  // Show as +00/-07/+14
    } else {
      strcpy(offsetStr, "   ");  // Blank during flash-off
    }
    snprintf(buf2, LCD_BUF_SIZE, "%02d-%02d %02d:%02d:%02d %s",
             localTime.month, localTime.day, localTime.hour, localTime.minute, localTime.second, offsetStr);
    lcd.print(buf2);
    
  } else {
    // ===== NORMAL DISPLAY =====
    int8_t offset = getUTCOffset();

    if (!timeValid && !hasManualTime()) {
      if (lastTimeValid || lastOffsetShown != offset) {
        lcd.setCursor(0, 0);
        lcd.print("MM-DD HH:MM:SS UTC ");
        lcd.setCursor(0, 1);
        lcd.print("MM-DD HH:MM:SS +00 ");
        lastTimeValid = false;
        lastOffsetShown = offset;
        lastSecUTC = -1;
        lastSecLocal = -1;
      }
      return;
    }

    TimeEdit_t utcTime = mcuTimeGetCurrent();
    TimeEdit_t localTime = calculateLocalTime(utcTime);
    
    // Only refresh if seconds changed
    if (utcTime.second != lastSecUTC || localTime.second != lastSecLocal || offset != lastOffsetShown || !lastTimeValid) {
      lastSecUTC = utcTime.second;
      lastSecLocal = localTime.second;
      lastOffsetShown = offset;
      lastTimeValid = true;
      
      // Line 1: UTC time (MM-DD HH:MM:SS UTC)
      lcd.setCursor(0, 0);
      char buf1[LCD_BUF_SIZE];
      snprintf(buf1, LCD_BUF_SIZE, "%02d-%02d %02d:%02d:%02d UTC",
               utcTime.month, utcTime.day, utcTime.hour, utcTime.minute, utcTime.second);
      lcd.print(buf1);
      
      // Line 2: Local time (MM-DD HH:MM:SS +/-offset)
      lcd.setCursor(0, 1);
      char buf2[LCD_BUF_SIZE];
      snprintf(buf2, LCD_BUF_SIZE, "%02d-%02d %02d:%02d:%02d %+03d",
               localTime.month, localTime.day, localTime.hour, localTime.minute, localTime.second, offset);
      lcd.print(buf2);
    }
  }
}

// ===== Mode: Timestamp Review =====
static bool s_tsScrollActive = false;
static bool s_tsConfirmDeleteAll = false;
static uint8_t s_tsSelectedNewest = 0;  // 0 = newest
static bool s_tsShowLocal = true;       // false=UTC, true=Local
static uint32_t s_tsDeleteAnimUntil = 0;
static const uint16_t kTsScrollBlinkMs = 300;
static const uint16_t kTsDeleteCueMs = 700;

bool timestampModeIsScrollActive() {
  return s_tsScrollActive;
}

void timestampModeScrollBy(int32_t delta) {
  if (!s_tsScrollActive || delta == 0) return;

  uint8_t count = timestampStoreCount();
  if (count == 0) {
    s_tsSelectedNewest = 0;
    return;
  }

  int16_t idx = (int16_t)s_tsSelectedNewest + (int16_t)delta;
  if (idx < 0) idx = 0;
  if (idx > (int16_t)count - 1) idx = (int16_t)count - 1;

  uint8_t next = (uint8_t)idx;
  if (next != s_tsSelectedNewest) {
    s_tsSelectedNewest = next;
    g_modeEpoch++;
  }
}

static void formatTimestampLine(char* out,
                                char prefix,
                                uint8_t number,
                                const TimeEdit_t* utcStamp,
                                bool showLocal,
                                char navArrow) {
  TimeEdit_t shown = showLocal ? calculateLocalTime(*utcStamp) : *utcStamp;
  char tzSuffix = showLocal ? 'L' : 'Z';
  snprintf(out, LCD_BUF_SIZE, "%c%02d %02d-%02d %02d:%02d:%02d%c%c",
           prefix, number,
           shown.month, shown.day,
           shown.hour, shown.minute, shown.second,
           tzSuffix, navArrow);
}

void displayModeTimestampReview() {
  static uint32_t lastEpoch = 0;
  static uint32_t lastSig = 0xFFFFFFFFUL;

  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lastEpoch = g_modeEpoch;
    lastSig = 0xFFFFFFFFUL;
  }

  uint8_t count = timestampStoreCount();
  if (count == 0) s_tsSelectedNewest = 0;
  else if (s_tsSelectedNewest >= count) s_tsSelectedNewest = (uint8_t)(count - 1);

  bool markerVisible = true;
  if (s_tsScrollActive) {
    markerVisible = ((millis() / (uint32_t)kTsScrollBlinkMs) % 2UL) == 0UL;
  }

  bool deleteAnimActive = millis() < s_tsDeleteAnimUntil;

  uint32_t sig = ((uint32_t)count)
               | ((uint32_t)s_tsSelectedNewest << 8)
               | ((uint32_t)s_tsScrollActive << 16)
               | ((uint32_t)s_tsConfirmDeleteAll << 17)
               | ((uint32_t)s_tsShowLocal << 18)
               | ((uint32_t)markerVisible << 19)
               | ((uint32_t)deleteAnimActive << 20);

  TimeEdit_t t0 = {0, 0, 0, 0, 0, 0};
  TimeEdit_t t1 = {0, 0, 0, 0, 0, 0};
  bool hasSelected = timestampStoreGetByNewest(s_tsSelectedNewest, &t0);
  bool hasSecond = false;
  if (count >= 2 && (s_tsSelectedNewest + 1U) < count) {
    hasSecond = timestampStoreGetByNewest((uint8_t)(s_tsSelectedNewest + 1U), &t1);
  }

  if (hasSelected) {
    sig ^= (uint32_t)t0.second;
    sig ^= (uint32_t)t0.minute << 6;
    sig ^= (uint32_t)t0.hour << 12;
    sig ^= (uint32_t)t0.day << 17;
    sig ^= (uint32_t)t0.month << 22;
  }
  if (hasSecond) {
    sig ^= (uint32_t)t1.second << 1;
    sig ^= (uint32_t)t1.minute << 7;
    sig ^= (uint32_t)t1.hour << 13;
    sig ^= (uint32_t)t1.day << 18;
    sig ^= (uint32_t)t1.month << 23;
  }

  if (sig == lastSig) return;
  lastSig = sig;

  if (s_tsConfirmDeleteAll) {
    lcd.setCursor(0, 0);
    lcd.print(" Delete ALL stamps? ");
    lcd.setCursor(0, 1);
    lcd.print(" L:Cancel R:Delete ");
    return;
  }

  if (!hasSelected) {
    char marker = markerVisible ? '>' : ' ';
    lcd.setCursor(0, 0);
    char line1[LCD_BUF_SIZE];
    snprintf(line1, LCD_BUF_SIZE, "%c00 -- -- --:--:--Z ", marker);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(" 00 -- -- --:--:-- ");
    return;
  }

  bool hasNewerAbove = (s_tsSelectedNewest > 0U);
  bool hasOlderBelow = (s_tsSelectedNewest + 2U < count);
  char upArrow = hasNewerAbove ? '^' : ' ';
  char downArrow = hasOlderBelow ? 'v' : ' ';
  char marker = markerVisible ? '>' : ' ';
  if (deleteAnimActive) {
    marker = '+';  // Brief visual cue that a new line moved into current slot.
  }

  lcd.setCursor(0, 0);
  char line1[LCD_BUF_SIZE];
  formatTimestampLine(line1, marker, (uint8_t)(s_tsSelectedNewest + 1U), &t0, s_tsShowLocal, upArrow);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  char line2[LCD_BUF_SIZE];
  if (hasSecond) {
    formatTimestampLine(line2, ' ', (uint8_t)(s_tsSelectedNewest + 2U), &t1, s_tsShowLocal, downArrow);
  } else {
    snprintf(line2, LCD_BUF_SIZE, " 00 -- -- --:--:--%c%c",
             s_tsShowLocal ? 'L' : 'Z', downArrow);
  }
  lcd.print(line2);
}

// ===== Mode: Stopwatch =====
void displayModeStopwatch() {
  static uint32_t lastEpoch = 0;
  static uint32_t lastTenths = 0xFFFFFFFFUL;
  static bool lastRunning = false;

  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lastEpoch = g_modeEpoch;
    lastTenths = 0xFFFFFFFFUL;
    lastRunning = !stopwatchIsRunning(0);  // force redraw below
  }

  uint32_t tenths = stopwatchGetTenths(0);
  bool running = stopwatchIsRunning(0);

  if (tenths != lastTenths || running != lastRunning) {
    uint8_t h = 0, m = 0, s = 0, t = 0;
    stopwatchGetDisplay(0, &h, &m, &s, &t);

    lcd.setCursor(0, 0);
    char line1[LCD_BUF_SIZE];
    snprintf(line1, LCD_BUF_SIZE, "STOPWATCH       %s", running ? "RUN" : "STP");
    lcd.print(line1);

    lcd.setCursor(0, 1);
    char line2[LCD_BUF_SIZE];
    snprintf(line2, LCD_BUF_SIZE, "     %02d:%02d:%02d.%1d     ", h, m, s, t);
    lcd.print(line2);

    lastTenths = tenths;
    lastRunning = running;
  }
}

// ===== Mode: Timer =====
void displayModeTimer() {
  static uint32_t lastEpoch = 0;
  static uint32_t lastSig = 0xFFFFFFFFUL;

  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lastEpoch = g_modeEpoch;
    lastSig = 0xFFFFFFFFUL;
  }

  bool editActive = timerEditIsActive();
  bool editFlashShow = true;
  if (editActive) {
    editFlashShow = timerEditShouldFlash();
  }

  uint8_t h = 0, m = 0, s = 0;
  bool elapsed = false, running = false, alarm = false;

  timerGetDisplay(0, &h, &m, &s, &elapsed, &running, &alarm);

  // Signature for one timer display state.
  uint32_t sig = 0;
  sig  = (uint32_t)(s & 0x3F);
  sig |= (uint32_t)(m & 0x3F) << 6;
  sig |= (uint32_t)(h & 0x7F) << 12;
  uint16_t flagBits = ((uint16_t)running       ) |
                      ((uint16_t)alarm      << 1 ) |
                      ((uint16_t)elapsed    << 2 ) |
                      ((uint16_t)editActive << 3 ) |
                      ((uint16_t)editFlashShow << 4);
  sig ^= (uint32_t)flagBits;

  if (sig != lastSig) {
    lcd.setCursor(0, 0);
    char head[LCD_BUF_SIZE];
    const char* state = alarm ? "ALM" : (running ? "RUN" : "STP");
    snprintf(head, LCD_BUF_SIZE, "FUEL TIMER      %s", state);
    lcd.print(head);

    if (editActive) {
      uint8_t eh = 0, em = 0, es = 0;
      timerEditGetPreview(&eh, &em, &es);
      bool show = editFlashShow;
      TimerEditField_t field = timerEditGetField();

      char hh[3], mm[3], ss[3];
      snprintf(hh, sizeof(hh), "%02d", eh);
      snprintf(mm, sizeof(mm), "%02d", em);
      snprintf(ss, sizeof(ss), "%02d", es);

      if (!show) {
        if (field == TIMER_EDIT_HOUR) strcpy(hh, "  ");
        else if (field == TIMER_EDIT_MINUTE) strcpy(mm, "  ");
        else if (field == TIMER_EDIT_SECOND) strcpy(ss, "  ");
      }

      lcd.setCursor(0, 1);
      char line2[LCD_BUF_SIZE];
      snprintf(line2, LCD_BUF_SIZE, "     - %s:%s:%s      ", hh, mm, ss);
      lcd.print(line2);
    } else {
      lcd.setCursor(0, 1);
      char line2[LCD_BUF_SIZE];
      snprintf(line2, LCD_BUF_SIZE, "     %c %02d:%02d:%02d     ",
               elapsed ? '+' : '-', h, m, s);
      lcd.print(line2);
    }

    lastSig = sig;
  }
}

// ===== Desk Mode (Local Only) State =====
static uint8_t s_deskDateFmt  = 0;    // 0=ISO  1=ISO+day  2=US  3=US+day  4=EU+day
static bool    s_deskIs12H    = false;
static bool    s_deskShowUtc  = false;
static const uint8_t kDeskDateFormatCount = 5;

// Center a string into a 20-char null-terminated buffer.
static void deskCenterLine(char* buf, const char* s) {
  uint8_t len = (uint8_t)strlen(s);
  if (len > LCD_COLS) len = LCD_COLS;
  uint8_t lpad = (LCD_COLS - len) / 2;
  memset(buf, ' ', LCD_COLS);
  buf[LCD_COLS] = '\0';
  memcpy(buf + lpad, s, len);
}

// Tomohiko Sakamoto day-of-week: 0=Sun … 6=Sat
static uint8_t deskDayOfWeek(uint16_t y, uint8_t m, uint8_t d) {
  static const uint8_t t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (uint8_t)((y + y/4 - y/100 + y/400 + t[m - 1] + d) % 7);
}

void displayModeLocalOnly() {
  static uint32_t lastEpoch = 0;
  static uint32_t lastSig   = 0xFFFFFFFFUL;

  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lastEpoch = g_modeEpoch;
    lastSig   = 0xFFFFFFFFUL;
  }

  TimeEdit_t utc = mcuTimeGetCurrent();
  TimeEdit_t t   = s_deskShowUtc ? utc : calculateLocalTime(utc);

  // Compact signature across all displayed state.
  uint32_t sig = (uint32_t)t.second
               | ((uint32_t)t.minute  <<  6)
               | ((uint32_t)t.hour    << 12)
               | ((uint32_t)t.day     << 18)
               | ((uint32_t)t.month   << 24);
  sig ^= (uint32_t)t.year;
  sig ^= ((uint32_t)s_deskDateFmt << 28);
  sig ^= ((uint32_t)s_deskIs12H   << 1);
  sig ^= ((uint32_t)s_deskShowUtc << 2);

  if (sig == lastSig) return;
  lastSig = sig;

  static const char* const kMon[13] = {
    "???","Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
  };
  static const char* const kDow[7] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
  };

  uint8_t dow = deskDayOfWeek(t.year, t.month, t.day);
  uint8_t mo  = (t.month >= 1 && t.month <= 12) ? t.month : 0;

  // Build date part string.
  char datePart[20];
  switch (s_deskDateFmt) {
    case 0:  // ISO:        2026-04-16
      snprintf(datePart, sizeof(datePart), "%04d-%02d-%02d",
               t.year, t.month, t.day);
      break;
    case 1:  // ISO+day:    Thu 2026-04-16
      snprintf(datePart, sizeof(datePart), "%s %04d-%02d-%02d",
               kDow[dow], t.year, t.month, t.day);
      break;
    case 2:  // US:         Apr 16, 2026
      snprintf(datePart, sizeof(datePart), "%s %02d, %04d",
               kMon[mo], t.day, t.year);
      break;
    case 3:  // US+day:     Thu Apr 16 2026
      snprintf(datePart, sizeof(datePart), "%s %s %02d %04d",
               kDow[dow], kMon[mo], t.day, t.year);
      break;
    case 4:  // EU+day:     Thu 16 Apr 2026
    default:
      snprintf(datePart, sizeof(datePart), "%s %02d %s %04d",
               kDow[dow], t.day, kMon[mo], t.year);
      break;
  }

  // Build time part string.
  char timePart[14];
  if (s_deskIs12H) {
    uint8_t hh = t.hour % 12;
    if (hh == 0) hh = 12;
    snprintf(timePart, sizeof(timePart), "%02d:%02d:%02d %s",
             hh, t.minute, t.second, t.hour < 12 ? "AM" : "PM");
  } else {
    snprintf(timePart, sizeof(timePart), "%02d:%02d:%02d",
             t.hour, t.minute, t.second);
  }

  // Center both lines.
  char line1[LCD_BUF_SIZE], line2[LCD_BUF_SIZE];
  deskCenterLine(line1, datePart);
  deskCenterLine(line2, timePart);

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// ===== Mode: GPS Info =====
void displayModeGpsInfo() {
  static uint32_t lastEpoch = 0;
  static uint32_t lastSig = 0xFFFFFFFFUL;

  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lastEpoch = g_modeEpoch;
    lastSig = 0xFFFFFFFFUL;
  }

  bool gsValid = gps.speed.isValid();
  bool hdgValid = gps.course.isValid();
  bool altValid = gps.altitude.isValid();

  uint16_t gsTenths = 0;
  uint16_t headingDeg = 0;
  int16_t altFeet = 0;

  if (gsValid) {
    double gsKnots = gps.speed.knots();
    if (gsKnots < 0.0) gsKnots = 0.0;
    if (gsKnots > 999.9) gsKnots = 999.9;
    gsTenths = (uint16_t)(gsKnots * 10.0 + 0.5);
  }

  if (hdgValid) {
    double hdg = gps.course.deg();
    while (hdg < 0.0) hdg += 360.0;
    while (hdg >= 360.0) hdg -= 360.0;
    headingDeg = (uint16_t)(hdg + 0.5);
    if (headingDeg >= 360U) headingDeg = 0;
  }

  if (altValid) {
    double alt = gps.altitude.feet();
    if (alt < -999.0) alt = -999.0;
    if (alt > 32767.0) alt = 32767.0;
    altFeet = (int16_t)(alt + (alt >= 0.0 ? 0.5 : -0.5));
  }

  uint32_t sig = 0;
  sig |= (uint32_t)gsValid;
  sig |= (uint32_t)hdgValid << 1;
  sig |= (uint32_t)altValid << 2;
  sig ^= (uint32_t)gsTenths << 3;
  sig ^= (uint32_t)headingDeg << 14;
  sig ^= (uint32_t)((uint16_t)altFeet) << 23;

  if (sig == lastSig) {
    return;
  }
  lastSig = sig;

  char gsStr[7] = "  --.-";
  char hdgStr[4] = "---";
  char altStr[8] = "   ----";

  if (gsValid) {
    snprintf(gsStr, sizeof(gsStr), "%6.1f", gps.speed.knots());
  }
  if (hdgValid) {
    snprintf(hdgStr, sizeof(hdgStr), "%03u", (unsigned int)headingDeg);
  }
  if (altValid) {
    snprintf(altStr, sizeof(altStr), "%7.0f", gps.altitude.feet());
  }

  lcd.setCursor(0, 0);
  char line1[LCD_BUF_SIZE];
  snprintf(line1, LCD_BUF_SIZE, "GS:%sKT HDG:%s ", gsStr, hdgStr);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  char line2[LCD_BUF_SIZE];
  snprintf(line2, LCD_BUF_SIZE, "ALT:%sFT       ", altStr);
  lcd.print(line2);
}

// ===== Mode Dispatcher =====
void updateDisplay(uint8_t mode) {
  switch (mode) {
    case MODE_UTC_ONLY:
      displayModeUTCOnly();
      break;
    case MODE_UTC_LOCAL:
      displayModeUTCLocal();
      break;
    case MODE_TIMESTAMP_REVIEW:
      displayModeTimestampReview();
      break;
    case MODE_STOPWATCH:
      displayModeStopwatch();
      break;
    case MODE_TIMER:
      displayModeTimer();
      break;
    case MODE_LOCAL_ONLY:
      displayModeLocalOnly();
      break;
    case MODE_GPS_INFO:
      displayModeGpsInfo();
      break;
    default:
      break;
  }
}

// ===== Mode Event Handler =====
void handleModeEvent(uint8_t mode, ButtonEvent_t event) {
  // Global backlight toggle (short press) in all modes.
  if (event == BUTTON_TOP_SHORT) {
    backlightToggle();
    return;
  }

  // Global timestamp capture in all modes.
  if (event == BUTTON_TOP_LONG) {
    TimeEdit_t now = mcuTimeGetCurrent();
    timestampStoreAdd(&now);
    backlightTriggerTimestamp();
    s_tsSelectedNewest = 0;   // Snap view to the latest stamp.
    if (timerAnyAlarmActive()) {
      timerAcknowledgeAllAlarms();
    }
    buzzTimestampStamp();
    g_modeEpoch++;
    return;
  }

  // Any button acknowledges active timer alarms and consumes the event.
  if (event != BUTTON_NONE && timerAnyAlarmActive()) {
    timerAcknowledgeAllAlarms();
    return;
  }

  if (mode == MODE_UTC_ONLY) {
    if (event == BUTTON_ENC_LONG) {
      TimeEdit_t currentTime = mcuTimeGetCurrent();
      timeEditStart(&currentTime);
    } else if (event == BUTTON_RIGHT_SHORT && timeEditIsActive()) {
      timeEditStop();
    } else if (event == BUTTON_ENC_SHORT && timeEditIsActive()) {
      timeEditButtonPress();
    }
  } else if (mode == MODE_UTC_LOCAL) {
    if (event == BUTTON_ENC_LONG) {
      int8_t currentOffset = getUTCOffset();
      offsetEditStart(currentOffset);
    } else if (event == BUTTON_RIGHT_SHORT && offsetEditIsActive()) {
      offsetEditStop();
    } else if (event == BUTTON_ENC_SHORT && offsetEditIsActive()) {
      offsetEditStop();
    }
  } else if (mode == MODE_TIMESTAMP_REVIEW) {
    if (s_tsConfirmDeleteAll) {
      if (event == BUTTON_LEFT_SHORT) {
        s_tsConfirmDeleteAll = false;
        g_modeEpoch++;
      } else if (event == BUTTON_RIGHT_SHORT) {
        timestampStoreClearAll();
        s_tsConfirmDeleteAll = false;
        s_tsSelectedNewest = 0;
        s_tsScrollActive = false;
        g_modeEpoch++;
      }
      return;
    }

    if (event == BUTTON_ENC_SHORT) {
      s_tsScrollActive = !s_tsScrollActive;
      g_modeEpoch++;
    } else if (event == BUTTON_RIGHT_SHORT) {
      s_tsShowLocal = !s_tsShowLocal;
      g_modeEpoch++;
    } else if (event == BUTTON_LEFT_SHORT) {
      if (s_tsScrollActive && timestampStoreDeleteByNewest(s_tsSelectedNewest)) {
        uint8_t count = timestampStoreCount();
        if (count == 0) {
          s_tsSelectedNewest = 0;
        } else if (s_tsSelectedNewest >= count) {
          s_tsSelectedNewest = (uint8_t)(count - 1);
        }
        s_tsDeleteAnimUntil = millis() + (uint32_t)kTsDeleteCueMs;
        g_modeEpoch++;
      }
    } else if (event == BUTTON_LEFT_LONG) {
      s_tsConfirmDeleteAll = true;
      g_modeEpoch++;
    }
  } else if (mode == MODE_STOPWATCH) {
    if (event == BUTTON_ENC_SHORT || event == BUTTON_RIGHT_SHORT) {
      stopwatchStartStopToggle(0);
    } else if (event == BUTTON_LEFT_SHORT) {
      stopwatchReset(0);
    }
  } else if (mode == MODE_TIMER) {
    if (event == BUTTON_ENC_LONG) {
      timerEditStart(0);
    } else if (event == BUTTON_RIGHT_SHORT && timerEditIsActive()) {
      timerEditFinish();
    } else if (event == BUTTON_ENC_SHORT && timerEditIsActive()) {
      timerEditButtonPress();
    } else if (event == BUTTON_ENC_SHORT) {
      timerStartStopToggle(0);
    } else if (event == BUTTON_RIGHT_SHORT) {
      timerStartStopToggle(0);
    } else if (event == BUTTON_LEFT_SHORT) {
      timerReset(0);
    }
  } else if (mode == MODE_LOCAL_ONLY) {
    if (event == BUTTON_LEFT_SHORT) {
      s_deskDateFmt = (s_deskDateFmt + 1) % kDeskDateFormatCount;
      g_modeEpoch++;
    } else if (event == BUTTON_RIGHT_SHORT) {
      s_deskIs12H = !s_deskIs12H;
      g_modeEpoch++;
    } else if (event == BUTTON_ENC_SHORT) {
      s_deskShowUtc = !s_deskShowUtc;
      g_modeEpoch++;
    }
  }
}
