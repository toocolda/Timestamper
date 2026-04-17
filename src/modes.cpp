#include <Arduino.h>
#include "st7036.h"
#include "config.h"
#include <TinyGPS++.h>
#include "time_edit.h"
#include "mcu_time.h"
#include "local_time.h"
#include "stopwatch.h"
#include "timer_mode.h"
#include "timestamp_store.h"
#include "buttons.h"

// Forward declarations from main.cpp
extern ST7036 lcd;
extern TinyGPSPlus gps;

// ===== Buzzer Pin =====
#define BUZZER PIN_BUZZER

// ===== Backlight Control =====
#include "backlight.h"

// ===== Global Mode Variables =====
uint8_t g_currentMode = MODE_UTC_ONLY;
uint32_t g_modeEpoch = 1;  // Start at 1 so first display triggers a clear and resets all caches

// ===== Buzzer Control =====
void buzzOnce(uint16_t durationMs) {
  tone(BUZZER, 1000);  // 1000 Hz tone
  delay(durationMs);
  noTone(BUZZER);
}

static void buzzTimestampStamp() {
  // Distinct two-tone stamp confirmation.
  tone(BUZZER, 1760);
  delay(55);
  noTone(BUZZER);
  delay(20);
  tone(BUZZER, 1175);
  delay(85);
  noTone(BUZZER);
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
    char gpsStatus[3];
    if (!isGPSTimeReliable()) {
      strcpy(gpsStatus, "NO");
    } else if (gps.location.isValid()) {
      strcpy(gpsStatus, "FX");
    } else if (sat > 0) {
      strcpy(gpsStatus, "AC");
    } else {
      strcpy(gpsStatus, "TI");
    }
    snprintf(buf, LCD_BUF_SIZE, "GPS:%s SAT:%02d BAT:XX", gpsStatus, sat);
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
    if (timeValid && !shouldSkipGPSSync()) {
      TimeEdit_t gpsTime;
      gpsTime.year = gps.date.year();
      gpsTime.month = gps.date.month();
      gpsTime.day = gps.date.day();
      gpsTime.hour = gps.time.hour();
      gpsTime.minute = gps.time.minute();
      gpsTime.second = gps.time.second();
      mcuTimeSync(&gpsTime);
    }
    
    // Get current time from MCU (ticks based on elapsed ms)
    TimeEdit_t currentTime = mcuTimeGetCurrent();
    int h = currentTime.hour;
    int m = currentTime.minute;
    int s = currentTime.second;
    
    char gpsStatus[3];
    if (!isGPSTimeReliable()) {
      strcpy(gpsStatus, "NO");
    } else if (gps.location.isValid()) {
      strcpy(gpsStatus, "FX");
    } else if (sat > 0) {
      strcpy(gpsStatus, "AC");
    } else {
      strcpy(gpsStatus, "TI");
    }
    
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
      snprintf(buf2, LCD_BUF_SIZE,
               "GPS:%s SAT:%02d BAT:XX",
               gpsStatus, sat);
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
  if (timeValid && !shouldSkipGPSSync()) {
    TimeEdit_t gpsTime;
    gpsTime.year = gps.date.year();
    gpsTime.month = gps.date.month();
    gpsTime.day = gps.date.day();
    gpsTime.hour = gps.time.hour();
    gpsTime.minute = gps.time.minute();
    gpsTime.second = gps.time.second();
    mcuTimeSync(&gpsTime);
  }
  
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
static bool g_tsScrollActive = false;
static bool g_tsConfirmDeleteAll = false;
static uint8_t g_tsSelectedNewest = 0;  // 0 = newest
static bool g_tsShowLocal = false;      // false=UTC, true=Local
static uint32_t g_tsDeleteAnimUntil = 0;

bool timestampModeIsScrollActive() {
  return g_tsScrollActive;
}

void timestampModeScrollBy(int32_t delta) {
  if (!g_tsScrollActive || delta == 0) return;

  uint8_t count = timestampStoreCount();
  if (count == 0) {
    g_tsSelectedNewest = 0;
    return;
  }

  int16_t idx = (int16_t)g_tsSelectedNewest + (int16_t)delta;
  if (idx < 0) idx = 0;
  if (idx > (int16_t)count - 1) idx = (int16_t)count - 1;

  uint8_t next = (uint8_t)idx;
  if (next != g_tsSelectedNewest) {
    g_tsSelectedNewest = next;
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
  if (count == 0) g_tsSelectedNewest = 0;
  else if (g_tsSelectedNewest >= count) g_tsSelectedNewest = (uint8_t)(count - 1);

  bool markerVisible = true;
  if (g_tsScrollActive) {
    markerVisible = ((millis() / 300UL) % 2UL) == 0UL;
  }

  bool deleteAnimActive = millis() < g_tsDeleteAnimUntil;

  uint32_t sig = ((uint32_t)count)
               | ((uint32_t)g_tsSelectedNewest << 8)
               | ((uint32_t)g_tsScrollActive << 16)
               | ((uint32_t)g_tsConfirmDeleteAll << 17)
               | ((uint32_t)g_tsShowLocal << 18)
               | ((uint32_t)markerVisible << 19)
               | ((uint32_t)deleteAnimActive << 20);

  TimeEdit_t t0 = {0, 0, 0, 0, 0, 0};
  TimeEdit_t t1 = {0, 0, 0, 0, 0, 0};
  bool hasSelected = timestampStoreGetByNewest(g_tsSelectedNewest, &t0);
  bool hasSecond = false;
  if (count >= 2 && (g_tsSelectedNewest + 1U) < count) {
    hasSecond = timestampStoreGetByNewest((uint8_t)(g_tsSelectedNewest + 1U), &t1);
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

  if (g_tsConfirmDeleteAll) {
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

  bool hasNewerAbove = (g_tsSelectedNewest > 0U);
  bool hasOlderBelow = (g_tsSelectedNewest + 2U < count);
  char upArrow = hasNewerAbove ? '^' : ' ';
  char downArrow = hasOlderBelow ? 'v' : ' ';
  char marker = markerVisible ? '>' : ' ';
  if (deleteAnimActive) {
    marker = '+';  // Brief visual cue that a new line moved into current slot.
  }

  lcd.setCursor(0, 0);
  char line1[LCD_BUF_SIZE];
  formatTimestampLine(line1, marker, (uint8_t)(g_tsSelectedNewest + 1U), &t0, g_tsShowLocal, upArrow);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  char line2[LCD_BUF_SIZE];
  if (hasSecond) {
    formatTimestampLine(line2, ' ', (uint8_t)(g_tsSelectedNewest + 2U), &t1, g_tsShowLocal, downArrow);
  } else {
    snprintf(line2, LCD_BUF_SIZE, " 00 -- -- --:--:--%c%c",
             g_tsShowLocal ? 'L' : 'Z', downArrow);
  }
  lcd.print(line2);
}

// ===== Mode: Stopwatch =====
void displayModeStopwatch() {
  static uint32_t lastEpoch = 0;
  static uint32_t lastTenths1 = 0xFFFFFFFFUL;
  static uint32_t lastTenths2 = 0xFFFFFFFFUL;
  static bool lastRunning1 = false;
  static bool lastRunning2 = false;
  static uint8_t lastSelected = 255;

  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lastEpoch = g_modeEpoch;
    lastTenths1 = 0xFFFFFFFFUL;
    lastTenths2 = 0xFFFFFFFFUL;
    lastRunning1 = !stopwatchIsRunning(0);  // force redraw below
    lastRunning2 = !stopwatchIsRunning(1);  // force redraw below
    lastSelected = 255;
  }

  uint32_t tenths1 = stopwatchGetTenths(0);
  uint32_t tenths2 = stopwatchGetTenths(1);
  bool running1 = stopwatchIsRunning(0);
  bool running2 = stopwatchIsRunning(1);
  uint8_t selected = stopwatchGetSelected();

  if (tenths1 != lastTenths1 || tenths2 != lastTenths2 ||
      running1 != lastRunning1 || running2 != lastRunning2 ||
      selected != lastSelected) {
    uint8_t h1 = 0, m1 = 0, s1 = 0, t1 = 0;
    uint8_t h2 = 0, m2 = 0, s2 = 0, t2 = 0;
    stopwatchGetDisplay(0, &h1, &m1, &s1, &t1);
    stopwatchGetDisplay(1, &h2, &m2, &s2, &t2);

    lcd.setCursor(0, 0);
    char line1[LCD_BUF_SIZE];
    snprintf(line1, LCD_BUF_SIZE, "%cSW1 %02d:%02d:%02d.%1d %s",
             (selected == 0) ? '>' : ' ', h1, m1, s1, t1, running1 ? "RUN" : "STP");
    lcd.print(line1);

    lcd.setCursor(0, 1);
    char line2[LCD_BUF_SIZE];
    snprintf(line2, LCD_BUF_SIZE, "%cSW2 %02d:%02d:%02d.%1d %s",
             (selected == 1) ? '>' : ' ', h2, m2, s2, t2, running2 ? "RUN" : "STP");
    lcd.print(line2);

    lastTenths1 = tenths1;
    lastTenths2 = tenths2;
    lastRunning1 = running1;
    lastRunning2 = running2;
    lastSelected = selected;
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

  uint8_t sel = timerGetSelected();
  bool editActive = timerEditIsActive();
  bool editFlashShow = true;
  if (editActive) {
    editFlashShow = timerEditShouldFlash();
  }

  uint8_t h1 = 0, m1 = 0, s1 = 0;
  uint8_t h2 = 0, m2 = 0, s2 = 0;
  bool e1 = false, r1 = false, a1 = false;
  bool e2 = false, r2 = false, a2 = false;

  timerGetDisplay(0, &h1, &m1, &s1, &e1, &r1, &a1);
  timerGetDisplay(1, &h2, &m2, &s2, &e2, &r2, &a2);

  // 32-bit signature with non-overlapping bit zones per channel so equal
  // values between T1 and T2 never XOR-cancel each other.
  uint32_t sig = 0;
  sig  = (uint32_t)(s1 & 0x3F);           // bits  5: 0  - T1 seconds
  sig |= (uint32_t)(s2 & 0x3F) << 6;      // bits 11: 6  - T2 seconds
  sig |= (uint32_t)(m1 & 0x3F) << 12;     // bits 17:12  - T1 minutes
  sig |= (uint32_t)(m2 & 0x3F) << 18;     // bits 23:18  - T2 minutes
  sig |= (uint32_t)(h1 & 0x0F) << 24;     // bits 27:24  - T1 hours
  sig |= (uint32_t)(h2 & 0x0F) << 28;     // bits 31:28  - T2 hours
  // Flags XOR'd in; collision probability is negligible for state transitions.
  uint16_t flagBits = ((uint16_t)(sel != 0)    ) |
                      ((uint16_t)r1        << 1 ) |
                      ((uint16_t)r2        << 2 ) |
                      ((uint16_t)a1        << 3 ) |
                      ((uint16_t)a2        << 4 ) |
                      ((uint16_t)e1        << 5 ) |
                      ((uint16_t)e2        << 6 ) |
                      ((uint16_t)editActive << 7 ) |
                      ((uint16_t)editFlashShow << 8);
  sig ^= (uint32_t)flagBits;

  if (sig != lastSig) {
    if (editActive) {
      uint8_t eh = 0, em = 0, es = 0;
      timerEditGetPreview(&eh, &em, &es);
      bool show = editFlashShow;
      uint8_t editIdx = timerEditGetIndex();
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

      // Keep non-edited channel visible for context.
      lcd.setCursor(0, 0);
      char line1[LCD_BUF_SIZE];
      if (editIdx == 0) {
        snprintf(line1, LCD_BUF_SIZE, ">T1  %s:%s:%s EDT", hh, mm, ss);
      } else {
        snprintf(line1, LCD_BUF_SIZE, " T1 %c%02d:%02d:%02d %s",
                 e1 ? '+' : ' ', h1, m1, s1, a1 ? "ALM" : (r1 ? "RUN" : "STP"));
      }
      lcd.print(line1);

      lcd.setCursor(0, 1);
      char line2[LCD_BUF_SIZE];
      if (editIdx == 1) {
        snprintf(line2, LCD_BUF_SIZE, ">T2  %s:%s:%s EDT", hh, mm, ss);
      } else {
        snprintf(line2, LCD_BUF_SIZE, " T2 %c%02d:%02d:%02d %s",
                 e2 ? '+' : ' ', h2, m2, s2, a2 ? "ALM" : (r2 ? "RUN" : "STP"));
      }
      lcd.print(line2);
    } else {
      lcd.setCursor(0, 0);
      char line1[LCD_BUF_SIZE];
      snprintf(line1, LCD_BUF_SIZE, "%cT1 %c%02d:%02d:%02d %s",
               (sel == 0) ? '>' : ' ',
               e1 ? '+' : ' ',
               h1, m1, s1,
               a1 ? "ALM" : (r1 ? "RUN" : "STP"));
      lcd.print(line1);

      lcd.setCursor(0, 1);
      char line2[LCD_BUF_SIZE];
      snprintf(line2, LCD_BUF_SIZE, "%cT2 %c%02d:%02d:%02d %s",
               (sel == 1) ? '>' : ' ',
               e2 ? '+' : ' ',
               h2, m2, s2,
               a2 ? "ALM" : (r2 ? "RUN" : "STP"));
      lcd.print(line2);
    }

    lastSig = sig;
  }
}

// ===== Desk Mode (Local Only) State =====
static uint8_t g_deskDateFmt  = 0;    // 0=ISO  1=ISO+day  2=US  3=US+day  4=EU+day
static bool    g_deskIs12H    = false;
static bool    g_deskShowUTC  = false;

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
  TimeEdit_t t   = g_deskShowUTC ? utc : calculateLocalTime(utc);

  // Compact signature across all displayed state.
  uint32_t sig = (uint32_t)t.second
               | ((uint32_t)t.minute  <<  6)
               | ((uint32_t)t.hour    << 12)
               | ((uint32_t)t.day     << 18)
               | ((uint32_t)t.month   << 24);
  sig ^= (uint32_t)t.year;
  sig ^= ((uint32_t)g_deskDateFmt << 28);
  sig ^= ((uint32_t)g_deskIs12H   << 1);
  sig ^= ((uint32_t)g_deskShowUTC << 2);

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
  switch (g_deskDateFmt) {
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
  if (g_deskIs12H) {
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
    default:
      break;
  }
}

// ===== Mode Event Handler =====
void handleModeEvent(uint8_t mode, ButtonEvent_t event) {
  // Global timestamp capture in all modes.
  if (event == BUTTON_TOP_LONG) {
    TimeEdit_t now = mcuTimeGetCurrent();
    timestampStoreAdd(&now);
    backlightTriggerTimestamp();
    g_tsSelectedNewest = 0;   // Snap view to the latest stamp.
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
    } else if (event == BUTTON_ENC_SHORT && timeEditIsActive()) {
      timeEditButtonPress();
    }
  } else if (mode == MODE_UTC_LOCAL) {
    if (event == BUTTON_ENC_LONG) {
      int8_t currentOffset = getUTCOffset();
      offsetEditStart(currentOffset);
    } else if (event == BUTTON_ENC_SHORT && offsetEditIsActive()) {
      offsetEditStop();
    }
  } else if (mode == MODE_TIMESTAMP_REVIEW) {
    if (g_tsConfirmDeleteAll) {
      if (event == BUTTON_LEFT_SHORT) {
        g_tsConfirmDeleteAll = false;
        g_modeEpoch++;
      } else if (event == BUTTON_RIGHT_SHORT) {
        timestampStoreClearAll();
        g_tsConfirmDeleteAll = false;
        g_tsSelectedNewest = 0;
        g_tsScrollActive = false;
        g_modeEpoch++;
      }
      return;
    }

    if (event == BUTTON_ENC_SHORT) {
      g_tsScrollActive = !g_tsScrollActive;
      g_modeEpoch++;
    } else if (event == BUTTON_RIGHT_SHORT) {
      g_tsShowLocal = !g_tsShowLocal;
      g_modeEpoch++;
    } else if (event == BUTTON_LEFT_SHORT) {
      if (g_tsScrollActive && timestampStoreDeleteByNewest(g_tsSelectedNewest)) {
        uint8_t count = timestampStoreCount();
        if (count == 0) {
          g_tsSelectedNewest = 0;
        } else if (g_tsSelectedNewest >= count) {
          g_tsSelectedNewest = (uint8_t)(count - 1);
        }
        g_tsDeleteAnimUntil = millis() + 700UL;
        g_modeEpoch++;
      }
    } else if (event == BUTTON_LEFT_LONG) {
      g_tsConfirmDeleteAll = true;
      g_modeEpoch++;
    }
  } else if (mode == MODE_STOPWATCH) {
    if (event == BUTTON_ENC_SHORT) {
      stopwatchToggleSelected();
    } else if (event == BUTTON_RIGHT_SHORT) {
      stopwatchStartStopToggle(stopwatchGetSelected());
    } else if (event == BUTTON_LEFT_SHORT) {
      stopwatchReset(stopwatchGetSelected());
    }
  } else if (mode == MODE_TIMER) {
    if (event == BUTTON_ENC_LONG) {
      timerEditStart(timerGetSelected());
    } else if (event == BUTTON_ENC_SHORT && timerEditIsActive()) {
      timerEditButtonPress();
    } else if (event == BUTTON_ENC_SHORT) {
      timerToggleSelected();
    } else if (event == BUTTON_RIGHT_SHORT) {
      timerStartStopToggle(timerGetSelected());
    } else if (event == BUTTON_LEFT_SHORT) {
      timerReset(timerGetSelected());
    }
  } else if (mode == MODE_LOCAL_ONLY) {
    if (event == BUTTON_LEFT_SHORT) {
      g_deskDateFmt = (g_deskDateFmt + 1) % 5;
      g_modeEpoch++;
    } else if (event == BUTTON_RIGHT_SHORT) {
      g_deskIs12H = !g_deskIs12H;
      g_modeEpoch++;
    } else if (event == BUTTON_ENC_SHORT) {
      g_deskShowUTC = !g_deskShowUTC;
      g_modeEpoch++;
    }
  }
}
