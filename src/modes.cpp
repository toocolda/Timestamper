#include <Arduino.h>
#include <TinyGPS++.h>
#include <avr/pgmspace.h>

#include "hardware/backlight.h"
#include "hardware/battery.h"
#include "hardware/buttons.h"
#include "core/modes.h"
#include "core/config.h"
#include "time/local_time.h"
#include "time/mcu_time.h"
#include "time/time_utils.h"
#include "time/crystal_time.h"
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

// Firmware version
static const char kFirmwareVersion[] = "v0.0.1  20260602";

// Forward declarations from main.cpp
extern ST7036 lcd;
extern TinyGPSPlus gps;

// In GSA, PDOP is field 15 (mode1, mode2, 12 PRN slots, then PDOP).
static TinyGPSCustom s_gpgsaPdop;
static TinyGPSCustom s_bdgsaPdop;
static bool s_gpsCustomInited = false;

static void ensureGpsCustomParsersInit() {
  if (s_gpsCustomInited) return;
  s_gpgsaPdop.begin(gps, "GPGSA", 15);
  s_bdgsaPdop.begin(gps, "BDGSA", 15);
  s_gpsCustomInited = true;
}

static bool gpsTryGetAltitudeFeet(int16_t* altFeetOut) {
  if (altFeetOut == nullptr) return false;

  if (gps.altitude.isValid()) {
    // TinyGPS++ altitude.value() is meters * 100.
    int32_t altMetersX100 = gps.altitude.value();
    // Use meters*10 with 3281/10000 scale to keep 32-bit math and improve
    // precision vs the prior 328/10000 approximation.
    int32_t altMetersX10 = altMetersX100 / 10L;
    // Keep multiply in signed 32-bit: 650000 * 3281 ~= 2.13e9.
    if (altMetersX10 > 650000L) altMetersX10 = 650000L;
    if (altMetersX10 < -650000L) altMetersX10 = -650000L;
    int32_t scaled = altMetersX10 * 3281L;
    scaled += (scaled >= 0) ? 5000L : -5000L;
    int32_t altFeet = scaled / 10000L;
    if (altFeet < -999) altFeet = -999;
    if (altFeet > 32767) altFeet = 32767;
    *altFeetOut = (int16_t)altFeet;
    return true;
  }
  return false;
}

static bool parseDecimalTenths(const char* text, uint16_t* outX10) {
  if (text == nullptr || outX10 == nullptr) return false;

  uint16_t whole = 0;
  uint8_t frac = 0;
  bool seenDigit = false;
  bool seenDot = false;
  bool fracSet = false;

  for (const char* p = text; *p != '\0'; ++p) {
    char c = *p;
    if (c >= '0' && c <= '9') {
      seenDigit = true;
      if (!seenDot) {
        whole = (uint16_t)(whole * 10U + (uint16_t)(c - '0'));
        if (whole > 999U) whole = 999U;
      } else if (!fracSet) {
        frac = (uint8_t)(c - '0');
        fracSet = true;
      }
    } else if (c == '.' && !seenDot) {
      seenDot = true;
    } else {
      break;
    }
  }

  if (!seenDigit) return false;
  uint16_t v = (uint16_t)(whole * 10U + (uint16_t)frac);
  if (v > 999U) v = 999U;
  *outX10 = v;
  return true;
}

static void formatUint2(char out[3], uint8_t value) {
  out[0] = (char)('0' + (value / 10U));
  out[1] = (char)('0' + (value % 10U));
  out[2] = '\0';
}

static void formatUint4(char out[5], uint16_t value) {
  out[0] = (char)('0' + ((value / 1000U) % 10U));
  out[1] = (char)('0' + ((value / 100U) % 10U));
  out[2] = (char)('0' + ((value / 10U) % 10U));
  out[3] = (char)('0' + (value % 10U));
  out[4] = '\0';
}

static void formatUint3(char out[4], uint16_t value) {
  if (value > 999U) value = 999U;
  out[0] = (char)('0' + (value / 100U));
  out[1] = (char)('0' + ((value / 10U) % 10U));
  out[2] = (char)('0' + (value % 10U));
  out[3] = '\0';
}

static char* appendText(char* out, const char* text) {
  while (*text != '\0') {
    *out++ = *text++;
  }
  return out;
}

static char* appendUint2(char* out, uint8_t value) {
  out[0] = (char)('0' + (value / 10U));
  out[1] = (char)('0' + (value % 10U));
  return out + 2;
}

static char* appendUint3(char* out, uint16_t value) {
  if (value > 999U) value = 999U;
  out[0] = (char)('0' + (value / 100U));
  out[1] = (char)('0' + ((value / 10U) % 10U));
  out[2] = (char)('0' + (value % 10U));
  return out + 3;
}

static char* appendUint4(char* out, uint16_t value) {
  out[0] = (char)('0' + ((value / 1000U) % 10U));
  out[1] = (char)('0' + ((value / 100U) % 10U));
  out[2] = (char)('0' + ((value / 10U) % 10U));
  out[3] = (char)('0' + (value % 10U));
  return out + 4;
}

static void formatIntRightAligned5(char out[6], int16_t value) {
  uint16_t magnitude = (value < 0) ? (uint16_t)(-value) : (uint16_t)value;
  if (magnitude > 9999U) magnitude = 9999U;

  out[0] = ' ';
  out[1] = ' ';
  out[2] = ' ';
  out[3] = ' ';
  out[4] = ' ';
  out[5] = '\0';

  uint8_t index = 4;
  do {
    out[index--] = (char)('0' + (magnitude % 10U));
    magnitude /= 10U;
  } while (magnitude > 0U && index < 5U);

  if (value < 0) {
    out[index] = '-';
  }
}

static void buildUtcDateTimeLine(char out[LCD_BUF_SIZE], const TimeEdit_t* t) {
  char* p = out;
  p = appendUint4(p, t->year);
  *p++ = '-';
  p = appendUint2(p, t->month);
  *p++ = '-';
  p = appendUint2(p, t->day);
  *p++ = ' ';
  p = appendUint2(p, t->hour);
  *p++ = ':';
  p = appendUint2(p, t->minute);
  *p++ = ':';
  p = appendUint2(p, t->second);
  *p++ = 'Z';
  *p = '\0';
}

static void buildUtcEditLine(char out[LCD_BUF_SIZE],
                             const char yearStr[5],
                             const char monthStr[3],
                             const char dayStr[3],
                             const char hourStr[3],
                             const char minuteStr[3],
                             const char secondStr[3]) {
  char* p = out;
  p = appendText(p, yearStr);
  *p++ = '-';
  p = appendText(p, monthStr);
  *p++ = '-';
  p = appendText(p, dayStr);
  *p++ = ' ';
  p = appendText(p, hourStr);
  *p++ = ':';
  p = appendText(p, minuteStr);
  *p++ = ':';
  p = appendText(p, secondStr);
  *p++ = 'Z';
  *p = '\0';
}

static void buildUtcEditStatusLine(char out[LCD_BUF_SIZE], uint8_t batteryPercent) {
  char* p = out;
  p = appendText(p, "EDIT UTC      BAT:");
  p = appendUint2(p, batteryPercent);
  *p = '\0';
}

static void buildUtcFirmwareLine(char out[LCD_BUF_SIZE]) {
  char* p = out;
  p = appendText(p, "FW: ");
  p = appendText(p, kFirmwareVersion);
  *p = '\0';
}

static void buildUtcSyncSearchLine(char out[LCD_BUF_SIZE], uint16_t remaining, uint8_t batteryPercent) {
  char* p = out;
  p = appendText(p, "SYNC SRCH ");
  p = appendUint3(p, remaining);
  p = appendText(p, " BAT:");
  p = appendUint2(p, batteryPercent);
  *p = '\0';
}

static void buildUtcSyncOkLine(char out[LCD_BUF_SIZE], uint16_t syncAge, uint8_t batteryPercent) {
  char* p = out;
  p = appendText(p, "SYNC OK  ");
  p = appendUint3(p, syncAge);
  *p++ = 's';
  p = appendText(p, " BAT:");
  p = appendUint2(p, batteryPercent);
  *p = '\0';
}

static void buildUtcSyncTimeoutLine(char out[LCD_BUF_SIZE], uint8_t batteryPercent) {
  char* p = out;
  p = appendText(p, "SYNC TMO TRY  BAT:");
  p = appendUint2(p, batteryPercent);
  *p = '\0';
}

static void buildUtcSyncNoneLine(char out[LCD_BUF_SIZE], uint8_t batteryPercent) {
  char* p = out;
  p = appendText(p, "SYNC NO       BAT:");
  p = appendUint2(p, batteryPercent);
  *p = '\0';
}

static void formatUtcOffset3(char out[4], int8_t offset, bool blank) {
  if (blank) {
    out[0] = ' ';
    out[1] = ' ';
    out[2] = ' ';
    out[3] = '\0';
    return;
  }

  int8_t absOffset = (offset < 0) ? (int8_t)(-offset) : offset;
  if (absOffset > 99) absOffset = 99;
  out[0] = (offset < 0) ? '-' : '+';
  out[1] = (char)('0' + (absOffset / 10));
  out[2] = (char)('0' + (absOffset % 10));
  out[3] = '\0';
}

static void formatMmDdHhMmSsTail3(char out[LCD_BUF_SIZE], const TimeEdit_t* t, const char tail3[4]) {
  out[0] = ' ';
  formatUint2(&out[1], t->month);
  out[3] = '-';
  formatUint2(&out[4], t->day);
  out[6] = ' ';
  formatUint2(&out[7], t->hour);
  out[9] = ':';
  formatUint2(&out[10], t->minute);
  out[12] = ':';
  formatUint2(&out[13], t->second);
  out[15] = ' ';
  out[16] = tail3[0];
  out[17] = tail3[1];
  out[18] = tail3[2];
  out[19] = '\0';
}

static void formatTimestampEmptySelectedLine(char out[LCD_BUF_SIZE], char marker) {
  out[0] = marker;
  out[1] = '0';
  out[2] = '0';
  out[3] = ' ';
  out[4] = '-';
  out[5] = '-';
  out[6] = ' ';
  out[7] = '-';
  out[8] = '-';
  out[9] = ' ';
  out[10] = '-';
  out[11] = '-';
  out[12] = ':';
  out[13] = '-';
  out[14] = '-';
  out[15] = ':';
  out[16] = '-';
  out[17] = '-';
  out[18] = 'Z';
  out[19] = ' ';
  out[20] = '\0';
}

static void formatTimestampEmptySecondLine(char out[LCD_BUF_SIZE], char tzSuffix, char navArrow) {
  out[0] = ' ';
  out[1] = '0';
  out[2] = '0';
  out[3] = ' ';
  out[4] = '-';
  out[5] = '-';
  out[6] = ' ';
  out[7] = '-';
  out[8] = '-';
  out[9] = ' ';
  out[10] = '-';
  out[11] = '-';
  out[12] = ':';
  out[13] = '-';
  out[14] = '-';
  out[15] = ':';
  out[16] = '-';
  out[17] = '-';
  out[18] = tzSuffix;
  out[19] = navArrow;
  out[20] = '\0';
}

static bool gpsTryGetPdopX10(uint16_t* pdopX10Out) {
  if (pdopX10Out == nullptr) return false;
  ensureGpsCustomParsersInit();

  const char* rawPdop = nullptr;
  if (s_gpgsaPdop.isValid() && s_gpgsaPdop.value()[0] != '\0') {
    rawPdop = s_gpgsaPdop.value();
  } else if (s_bdgsaPdop.isValid() && s_bdgsaPdop.value()[0] != '\0') {
    rawPdop = s_bdgsaPdop.value();
  }
  if (rawPdop == nullptr) return false;
  return parseDecimalTenths(rawPdop, pdopX10Out);
}

// ===== Global Mode Variables =====
uint8_t g_currentMode = MODE_UTC_ONLY;
uint32_t g_modeEpoch = 1;  // Start at 1 so first display triggers a clear and resets all caches

// ===== Buzzer Control =====
enum BuzzerCue : uint8_t {
  BUZZ_CUE_NONE = 0,
  BUZZ_CUE_SINGLE,
  BUZZ_CUE_TIMESTAMP
};

static BuzzerCue s_buzzerCue = BUZZ_CUE_NONE;
static uint8_t s_buzzerCueStep = 0;
static uint16_t s_buzzerCueSingleMs = 0;
static uint32_t s_buzzerCueStepStartedMs = 0;

static const uint16_t kModeSwitchToneHz = 659;
static const uint8_t kModeSwitchDutyPercent = 6;
static const uint16_t kControlStartStopToneHz = 988;
static const uint16_t kControlResetEditToneHz = 740;
static const uint8_t kControlDutyPercent = 7;
static const uint16_t kTimestampToneAHz = 784;
static const uint16_t kTimestampToneBHz = 988;
static const uint8_t kTimestampDutyPercent = 7;

static void buzzerCueStop() {
  buzzerStop();
  s_buzzerCue = BUZZ_CUE_NONE;
  s_buzzerCueStep = 0;
  s_buzzerCueSingleMs = 0;
  s_buzzerCueStepStartedMs = 0;
}

static void buzzSingle(uint16_t durationMs, uint16_t frequencyHz, uint8_t dutyPercent) {
  if (timerAnyAlarmActive()) return;
  s_buzzerCue = BUZZ_CUE_SINGLE;
  s_buzzerCueStep = 0;
  s_buzzerCueSingleMs = durationMs;
  s_buzzerCueStepStartedMs = crystalTimeGetMillis();
  buzzerStartWithDuty(frequencyHz, dutyPercent);
}

void buzzOnce(uint16_t durationMs) {
  buzzSingle(durationMs, kModeSwitchToneHz, kModeSwitchDutyPercent);
}

static void buzzControlStartStop() {
  buzzSingle(34, kControlStartStopToneHz, kControlDutyPercent);
}

static void buzzControlResetEdit() {
  buzzSingle(42, kControlResetEditToneHz, kControlDutyPercent);
}

static void buzzTimestampStamp() {
  if (timerAnyAlarmActive()) return;
  s_buzzerCue = BUZZ_CUE_TIMESTAMP;
  s_buzzerCueStep = 0;
  s_buzzerCueStepStartedMs = crystalTimeGetMillis();
  buzzerStartWithDuty(kTimestampToneAHz, kTimestampDutyPercent);
}

void modeAudioUpdate() {
  if (s_buzzerCue == BUZZ_CUE_NONE) return;

  if (timerAnyAlarmActive()) {
    buzzerCueStop();
    return;
  }

  uint32_t nowMs = crystalTimeGetMillis();

  if (s_buzzerCue == BUZZ_CUE_SINGLE) {
    if (crystalTimeElapsedMs(s_buzzerCueStepStartedMs, s_buzzerCueSingleMs)) {
      buzzerCueStop();
    }
    return;
  }

  if (s_buzzerCue != BUZZ_CUE_TIMESTAMP) return;

  switch (s_buzzerCueStep) {
    case 0:
      if (crystalTimeElapsedMs(s_buzzerCueStepStartedMs, 44)) {
        buzzerStop();
        s_buzzerCueStep = 1;
        s_buzzerCueStepStartedMs = nowMs;
      }
      break;
    case 1:
      if (crystalTimeElapsedMs(s_buzzerCueStepStartedMs, 18)) {
        buzzerStartWithDuty(kTimestampToneBHz, kTimestampDutyPercent);
        s_buzzerCueStep = 2;
        s_buzzerCueStepStartedMs = nowMs;
      }
      break;
    default:
      if (crystalTimeElapsedMs(s_buzzerCueStepStartedMs, 54)) {
        buzzerCueStop();
      }
      break;
  }
}

// Build compact GPS status token used in the status line:
// NO=no reliable GPS time/date, 3D=3D-quality fix, 2D=position fix,
// AC=acquiring (satellites present but no position fix yet).
static void buildGpsStatus(char out[3], bool timeReliable, int satCount, bool has3d) {
  if (has3d) {
    out[0] = '3'; out[1] = 'D'; out[2] = '\0';
  } else if (gps.location.isValid()) {
    out[0] = '2'; out[1] = 'D'; out[2] = '\0';
  } else if (satCount > 0) {
    out[0] = 'A'; out[1] = 'C'; out[2] = '\0';
  } else {
    (void)timeReliable;
    out[0] = 'N'; out[1] = 'O'; out[2] = '\0';
  }
}

// Convert true heading degrees to a fixed-width 3-char cardinal token.
// Examples: 030 -> NNE, 270 -> W  , 359 -> N  .
static void headingDegToCardinal3(uint16_t headingDeg, char out[4]) {
  uint8_t idx = (uint8_t)(((uint32_t)headingDeg * 10UL + 112UL) / 225UL);
  idx &= 0x0F;

  switch (idx) {
    case 0:  out[0] = 'N'; out[1] = ' '; out[2] = ' '; break;
    case 1:  out[0] = 'N'; out[1] = 'N'; out[2] = 'E'; break;
    case 2:  out[0] = 'N'; out[1] = 'E'; out[2] = ' '; break;
    case 3:  out[0] = 'E'; out[1] = 'N'; out[2] = 'E'; break;
    case 4:  out[0] = 'E'; out[1] = ' '; out[2] = ' '; break;
    case 5:  out[0] = 'E'; out[1] = 'S'; out[2] = 'E'; break;
    case 6:  out[0] = 'S'; out[1] = 'E'; out[2] = ' '; break;
    case 7:  out[0] = 'S'; out[1] = 'S'; out[2] = 'E'; break;
    case 8:  out[0] = 'S'; out[1] = ' '; out[2] = ' '; break;
    case 9:  out[0] = 'S'; out[1] = 'S'; out[2] = 'W'; break;
    case 10: out[0] = 'S'; out[1] = 'W'; out[2] = ' '; break;
    case 11: out[0] = 'W'; out[1] = 'S'; out[2] = 'W'; break;
    case 12: out[0] = 'W'; out[1] = ' '; out[2] = ' '; break;
    case 13: out[0] = 'W'; out[1] = 'N'; out[2] = 'W'; break;
    case 14: out[0] = 'N'; out[1] = 'W'; out[2] = ' '; break;
    default: out[0] = 'N'; out[1] = 'N'; out[2] = 'W'; break;
  }
  out[3] = '\0';
}

// ===== GPS Validation Helper =====
bool isGPSTimeReliable() {
  if (!gps.time.isValid() || !gps.date.isValid()) {
    return false;
  }

  // Require at least one satellite in use so backup-RTC time (reported with 0
  // sats while no signal) is never mistaken for satellite-sourced time.
  if (gps.satellites.value() < 1) {
    return false;
  }

  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();

  if (year < 2020) return false;
  if (month < 1 || month > 12) return false;
  uint8_t maxDay = timeDaysInMonth((uint16_t)year, (uint8_t)month);
  if (day < 1 || day > (int)maxDay) return false;

  return true;
}

// ===== Mode: UTC Only State =====
static bool s_utcShowFirmware = false;

// ===== Mode: UTC Only =====
void displayModeUTCOnly() {
  static int lastSec = -1;
  static bool lastTimeValid = false;
  static uint32_t lastEpoch = 0;
  static bool lastEditMode = false;
  static uint16_t lastSyncElapsed = 0xFFFFU;
  static bool lastSyncSearching = false;
  static GpsSyncResult lastSyncResult = GPS_SYNC_RESULT_NONE;
  static uint16_t lastSyncAge = 0xFFFFU;
  static bool lastShowFirmware = false;
  
  // Reset cache if mode changed
  if (lastEpoch != g_modeEpoch) {
    lastSec = -1;
    lastTimeValid = false;
    lastEpoch = g_modeEpoch;
    lastEditMode = false;
    lastSyncElapsed = 0xFFFFU;
    lastSyncSearching = false;
    lastSyncResult = GPS_SYNC_RESULT_NONE;
    lastSyncAge = 0xFFFFU;
    lastShowFirmware = false;
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
      lastTimeValid = false;
      lastSyncElapsed = 0xFFFFU;
      lastSyncSearching = false;
      lastSyncResult = GPS_SYNC_RESULT_NONE;
      lastSyncAge = 0xFFFFU;
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
    
    formatUint4(year_str, editData.year);
    formatUint2(month_str, editData.month);
    formatUint2(day_str, editData.day);
    formatUint2(hour_str, editData.hour);
    formatUint2(min_str, editData.minute);
    formatUint2(sec_str, editData.second);

    if (field == EDIT_FIELD_YEAR && !shouldShow) {
      year_str[0] = ' '; year_str[1] = ' '; year_str[2] = ' '; year_str[3] = ' ';
    }
    if (field == EDIT_FIELD_MONTH && !shouldShow) {
      month_str[0] = ' '; month_str[1] = ' ';
    }
    if (field == EDIT_FIELD_DAY && !shouldShow) {
      day_str[0] = ' '; day_str[1] = ' ';
    }
    if (field == EDIT_FIELD_HOUR && !shouldShow) {
      hour_str[0] = ' '; hour_str[1] = ' ';
    }
    if (field == EDIT_FIELD_MINUTE && !shouldShow) {
      min_str[0] = ' '; min_str[1] = ' ';
    }
    if (field == EDIT_FIELD_SECOND && !shouldShow) {
      sec_str[0] = ' '; sec_str[1] = ' ';
    }
    
    buildUtcEditLine(buf, year_str, month_str, day_str, hour_str, min_str, sec_str);
    lcd.print(buf);
    
    // Show GPS status on line 2 as hint (blinking field is visual hint)
    lcd.setCursor(0, 1);
    uint8_t batPercent = batteryGetPercentage();
    buildUtcEditStatusLine(buf, batPercent);
    lcd.print(buf);
    
  } else {
    // ===== NORMAL DISPLAY - Uses MCU time (ticks independently) =====
    bool timeValid = isGPSTimeReliable();
    bool utcDisplayValid = mcuTimeHasSync() || hasManualTime();
    bool syncSearching = gpsSyncIsSearching();
    uint16_t syncElapsed = syncSearching ? gpsSyncGetElapsedSeconds() : 0U;
    GpsSyncResult syncResult = gpsSyncGetLastResult();
    uint16_t syncAge = gpsSyncGetLastResultAgeSeconds();

    // Get current time from MCU (ticks based on elapsed ms)
    TimeEdit_t currentTime = mcuTimeGetCurrent();
    int s = currentTime.second;

    if (s != lastSec || timeValid != lastTimeValid ||
      syncSearching != lastSyncSearching || syncElapsed != lastSyncElapsed ||
      syncResult != lastSyncResult || syncAge != lastSyncAge || s_utcShowFirmware != lastShowFirmware) {
      lcd.setCursor(0, 0);
      
      char buf[LCD_BUF_SIZE];
      if (utcDisplayValid) {
        // Display with MCU time (which keeps ticking)
        buildUtcDateTimeLine(buf, &currentTime);
      } else {
        memcpy(buf, "YYYY-MM-DD HH:MM:SSZ", 20);
        buf[20] = '\0';
      }
      lcd.print(buf);
      
      lcd.setCursor(0, 1);
      char buf2[LCD_BUF_SIZE];
      
      if (s_utcShowFirmware) {
        buildUtcFirmwareLine(buf2);
      } else {
        uint8_t batPercent = batteryGetPercentage();
        if (syncSearching) {
          uint16_t remain = gpsSyncGetRemainingSeconds();
          buildUtcSyncSearchLine(buf2, remain, batPercent);
        } else if (syncResult == GPS_SYNC_RESULT_OK) {
          buildUtcSyncOkLine(buf2, syncAge, batPercent);
        } else if (syncResult == GPS_SYNC_RESULT_TIMEOUT) {
          buildUtcSyncTimeoutLine(buf2, batPercent);
        } else {
          buildUtcSyncNoneLine(buf2, batPercent);
        }
      }
      lcd.print(buf2);
      
      lastSec = s;
      lastTimeValid = timeValid;
      lastSyncSearching = syncSearching;
      lastSyncElapsed = syncElapsed;
      lastSyncResult = syncResult;
      lastSyncAge = syncAge;
      lastShowFirmware = s_utcShowFirmware;
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

  if (offsetEditMode) {
    // ===== OFFSET EDIT DISPLAY =====
    TimeEdit_t utcTime = mcuTimeGetCurrent();
    int8_t currentOffset = offsetEditGetValue();
    bool shouldShowOffset = offsetEditShouldFlash();
    TimeEdit_t localTime = calculateLocalTimeWithOffset(utcTime, currentOffset);
    
    // Line 1: UTC time (MM-DD HH:MM:SS UTC)
    lcd.setCursor(0, 0);
    char buf1[LCD_BUF_SIZE];
    formatMmDdHhMmSsTail3(buf1, &utcTime, "UTC");
    lcd.print(buf1);
    
    // Line 2: Local time with blinking offset
    lcd.setCursor(0, 1);
    char buf2[LCD_BUF_SIZE];
    char offsetStr[5];
    formatUtcOffset3(offsetStr, currentOffset, !shouldShowOffset);
    formatMmDdHhMmSsTail3(buf2, &localTime, offsetStr);
    lcd.print(buf2);
    
  } else {
    // ===== NORMAL DISPLAY =====
    int8_t offset = getUTCOffset();

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
      formatMmDdHhMmSsTail3(buf1, &utcTime, "UTC");
      lcd.print(buf1);
      
      // Line 2: Local time (MM-DD HH:MM:SS +/-offset)
      lcd.setCursor(0, 1);
      char buf2[LCD_BUF_SIZE];
      char offsetStr[4];
      formatUtcOffset3(offsetStr, offset, false);
      formatMmDdHhMmSsTail3(buf2, &localTime, offsetStr);
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
  out[0] = prefix;
  formatUint2(&out[1], number);
  out[3] = ' ';
  formatUint2(&out[4], shown.month);
  out[6] = '-';
  formatUint2(&out[7], shown.day);
  out[9] = ' ';
  formatUint2(&out[10], shown.hour);
  out[12] = ':';
  formatUint2(&out[13], shown.minute);
  out[15] = ':';
  formatUint2(&out[16], shown.second);
  out[18] = tzSuffix;
  out[19] = navArrow;
  out[20] = '\0';
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
    markerVisible = ((crystalTimeGetMillis() / (uint32_t)kTsScrollBlinkMs) % 2UL) == 0UL;
  }

  bool deleteAnimActive = (int32_t)(s_tsDeleteAnimUntil - crystalTimeGetMillis()) > 0;

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
    lcd.print(F(" Delete ALL stamps? "));
    lcd.setCursor(0, 1);
    lcd.print(F(" L:Cancel R:Delete "));
    return;
  }

  if (!hasSelected) {
    char marker = markerVisible ? '>' : ' ';
    lcd.setCursor(0, 0);
    char line1[LCD_BUF_SIZE];
    formatTimestampEmptySelectedLine(line1, marker);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(F(" 00 -- -- --:--:-- "));
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
    formatTimestampEmptySecondLine(line2, s_tsShowLocal ? 'L' : 'Z', downArrow);
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
    memcpy(line1, "STOPWATCH        ", 17);
    memcpy(&line1[17], running ? "RUN" : "STP", 3);
    line1[20] = '\0';
    lcd.print(line1);

    lcd.setCursor(0, 1);
    char line2[LCD_BUF_SIZE];
    memcpy(line2, "     ", 5);
    appendUint2(&line2[5], h);
    line2[7] = ':';
    appendUint2(&line2[8], m);
    line2[10] = ':';
    appendUint2(&line2[11], s);
    line2[13] = '.';
    line2[14] = (char)('0' + t);
    memcpy(&line2[15], "     ", 5);
    line2[20] = '\0';
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
    memcpy(head, "FUEL TIMER       ", 17);
    memcpy(&head[17], state, 3);
    head[20] = '\0';
    lcd.print(head);

    if (editActive) {
      uint8_t eh = 0, em = 0, es = 0;
      timerEditGetPreview(&eh, &em, &es);
      bool show = editFlashShow;
      TimerEditField_t field = timerEditGetField();

      char hh[3], mm[3], ss[3];
      formatUint2(hh, eh);
      formatUint2(mm, em);
      formatUint2(ss, es);

      if (!show) {
        if (field == TIMER_EDIT_HOUR) { hh[0] = ' '; hh[1] = ' '; hh[2] = '\0'; }
        else if (field == TIMER_EDIT_MINUTE) { mm[0] = ' '; mm[1] = ' '; mm[2] = '\0'; }
        else if (field == TIMER_EDIT_SECOND) { ss[0] = ' '; ss[1] = ' '; ss[2] = '\0'; }
      }

      lcd.setCursor(0, 1);
      char line2[LCD_BUF_SIZE];
      memcpy(line2, "     - ", 7);
      memcpy(&line2[7], hh, 2);
      line2[9] = ':';
      memcpy(&line2[10], mm, 2);
      line2[12] = ':';
      memcpy(&line2[13], ss, 2);
      memcpy(&line2[15], "      ", 6);
      line2[20] = '\0';
      lcd.print(line2);
    } else {
      lcd.setCursor(0, 1);
      char line2[LCD_BUF_SIZE];
      memcpy(line2, "     ", 5);
      line2[5] = elapsed ? '+' : '-';
      line2[6] = ' ';
      appendUint2(&line2[7], h);
      line2[9] = ':';
      appendUint2(&line2[10], m);
      line2[12] = ':';
      appendUint2(&line2[13], s);
      memcpy(&line2[15], "     ", 5);
      line2[20] = '\0';
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
  char* datePtr = datePart;
  switch (s_deskDateFmt) {
    case 0:  // ISO:        2026-04-16
      datePtr = appendUint4(datePtr, t.year);
      *datePtr++ = '-';
      datePtr = appendUint2(datePtr, t.month);
      *datePtr++ = '-';
      datePtr = appendUint2(datePtr, t.day);
      break;
    case 1:  // ISO+day:    Thu 2026-04-16
      datePtr = appendText(datePtr, kDow[dow]);
      *datePtr++ = ' ';
      datePtr = appendUint4(datePtr, t.year);
      *datePtr++ = '-';
      datePtr = appendUint2(datePtr, t.month);
      *datePtr++ = '-';
      datePtr = appendUint2(datePtr, t.day);
      break;
    case 2:  // US:         Apr 16, 2026
      datePtr = appendText(datePtr, kMon[mo]);
      *datePtr++ = ' ';
      datePtr = appendUint2(datePtr, t.day);
      *datePtr++ = ',';
      *datePtr++ = ' ';
      datePtr = appendUint4(datePtr, t.year);
      break;
    case 3:  // US+day:     Thu Apr 16 2026
      datePtr = appendText(datePtr, kDow[dow]);
      *datePtr++ = ' ';
      datePtr = appendText(datePtr, kMon[mo]);
      *datePtr++ = ' ';
      datePtr = appendUint2(datePtr, t.day);
      *datePtr++ = ' ';
      datePtr = appendUint4(datePtr, t.year);
      break;
    case 4:  // EU+day:     Thu 16 Apr 2026
    default:
      datePtr = appendText(datePtr, kDow[dow]);
      *datePtr++ = ' ';
      datePtr = appendUint2(datePtr, t.day);
      *datePtr++ = ' ';
      datePtr = appendText(datePtr, kMon[mo]);
      *datePtr++ = ' ';
      datePtr = appendUint4(datePtr, t.year);
      break;
  }
  *datePtr = '\0';

  // Build time part string.
  char timePart[14];
  char* timePtr = timePart;
  if (s_deskIs12H) {
    uint8_t hh = t.hour % 12;
    if (hh == 0) hh = 12;
    timePtr = appendUint2(timePtr, hh);
    *timePtr++ = ':';
    timePtr = appendUint2(timePtr, t.minute);
    *timePtr++ = ':';
    timePtr = appendUint2(timePtr, t.second);
    *timePtr++ = ' ';
    *timePtr++ = (t.hour < 12) ? 'A' : 'P';
    *timePtr++ = 'M';
  } else {
    timePtr = appendUint2(timePtr, t.hour);
    *timePtr++ = ':';
    timePtr = appendUint2(timePtr, t.minute);
    *timePtr++ = ':';
    timePtr = appendUint2(timePtr, t.second);
  }
  *timePtr = '\0';

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
  bool timeReliable = isGPSTimeReliable();
  bool hasFreshFix = gpsHasFreshFixSincePowerOn();
  int sat = gps.satellites.value();
  if (sat < 0) sat = 0;
  if (sat > 99) sat = 99;
  char gpsStatus[3];
  int16_t altFeet = 0;
  bool altValid = gpsTryGetAltitudeFeet(&altFeet);
  bool has3d = altValid && gps.location.isValid() && (sat >= 4);
  buildGpsStatus(gpsStatus, timeReliable, sat, has3d);

  uint16_t gsTenths = 0;
  uint16_t headingDeg = 0;
  bool pdopValid = false;
  uint16_t pdopX10 = 0;

  // Hide stale parser values until a fresh fix is received after power-on.
  if (!hasFreshFix) {
    gsValid = false;
    hdgValid = false;
    altValid = false;
    pdopValid = false;
    sat = 0;
  }

  if (gsValid) {
    // TinyGPS++ speed.value() is knots * 100.
    uint32_t gsCentiKnots = gps.speed.value();
    if (gsCentiKnots > 9999U) gsCentiKnots = 9999U;
    gsTenths = (uint16_t)((gsCentiKnots + 5U) / 10U);
  }

  if (hdgValid) {
    // TinyGPS++ course.value() is degrees * 100.
    uint32_t courseCentiDeg = gps.course.value();
    headingDeg = (uint16_t)(((courseCentiDeg + 50U) / 100U) % 360U);
  }

  pdopValid = gpsTryGetPdopX10(&pdopX10);

  uint32_t sig = 0;
  sig |= (uint32_t)gsValid;
  sig |= (uint32_t)hdgValid << 1;
  sig |= (uint32_t)altValid << 2;
  sig ^= (uint32_t)(sat & 0x7F) << 6;
  sig ^= (uint32_t)((uint8_t)gpsStatus[0]) << 13;
  sig ^= (uint32_t)((uint8_t)gpsStatus[1]) << 21;
  sig ^= (uint32_t)gsTenths << 3;
  sig ^= (uint32_t)headingDeg << 14;
  sig ^= (uint32_t)((uint16_t)altFeet) << 23;
  sig ^= (uint32_t)pdopValid << 5;
  sig ^= (uint32_t)pdopX10 << 9;

  if (sig == lastSig) {
    return;
  }
  lastSig = sig;

  char gsStr[4] = "---";
  char hdgStr[4] = "---";
  char hdgCard[4] = "---";
  char altStr[6] = "-----";
  char gpsFixSat[5] = "NO--";
  char pdopTok[4] = "P--";

  if (gsValid) {
    uint16_t whole = (uint16_t)(gsTenths / 10U);
    formatUint3(gsStr, whole);
  }
  if (hdgValid) {
    formatUint3(hdgStr, headingDeg);
    headingDegToCardinal3(headingDeg, hdgCard);
  }
  if (altValid) {
    formatIntRightAligned5(altStr, altFeet);
  }
  if (hasFreshFix) {
    gpsFixSat[0] = gpsStatus[0];
    gpsFixSat[1] = gpsStatus[1];
    formatUint2(&gpsFixSat[2], (uint8_t)sat);
  } else {
    gpsFixSat[0] = '-';
    gpsFixSat[1] = '-';
    gpsFixSat[2] = '-';
    gpsFixSat[3] = '-';
    gpsFixSat[4] = '\0';
  }
  if (pdopValid) {
    if (pdopX10 > 99U) pdopX10 = 99U;
    pdopTok[0] = 'P';
    formatUint2(&pdopTok[1], (uint8_t)pdopX10);
  }

  lcd.setCursor(0, 0);
  char line1[LCD_BUF_SIZE];
  memcpy(line1, "ALT:", 4);
  memcpy(&line1[4], altStr, 5);
  memcpy(&line1[9], "FT GS:", 6);
  memcpy(&line1[15], gsStr, 3);
  memcpy(&line1[18], "KT", 2);
  line1[20] = '\0';
  lcd.print(line1);

  lcd.setCursor(0, 1);
  char line2[LCD_BUF_SIZE];
  memcpy(line2, "HDG:", 4);
  memcpy(&line2[4], hdgStr, 3);
  line2[7] = ' ';
  memcpy(&line2[8], hdgCard, 3);
  line2[11] = ' ';
  memcpy(&line2[12], gpsFixSat, 4);
  line2[16] = ' ';
  memcpy(&line2[17], pdopTok, 3);
  line2[20] = '\0';
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

  // Global GPS sync request (right long) - works from any mode unless editing.
  // Any button acknowledges active timer alarms and consumes the event.
  if (event != BUTTON_NONE && timerAnyAlarmActive()) {
    timerAcknowledgeAllAlarms();
    return;
  }

  if (mode == MODE_UTC_ONLY) {
    if (event == BUTTON_ENC_LONG) {
      TimeEdit_t currentTime = mcuTimeGetCurrent();
      timeEditStart(&currentTime);
    } else if (event == BUTTON_RIGHT_LONG && !timeEditIsActive()) {
      gpsSyncRequest();
    } else if (event == BUTTON_LEFT_LONG) {
      s_utcShowFirmware = !s_utcShowFirmware;
      g_modeEpoch++;
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
        s_tsDeleteAnimUntil = crystalTimeGetMillis() + (uint32_t)kTsDeleteCueMs;
        g_modeEpoch++;
      }
    } else if (event == BUTTON_LEFT_LONG) {
      s_tsConfirmDeleteAll = true;
      g_modeEpoch++;
    }
  } else if (mode == MODE_STOPWATCH) {
    if (event == BUTTON_ENC_SHORT || event == BUTTON_RIGHT_SHORT) {
      stopwatchStartStopToggle(0);
      buzzControlStartStop();
    } else if (event == BUTTON_LEFT_SHORT) {
      stopwatchReset(0);
      buzzControlResetEdit();
    }
  } else if (mode == MODE_TIMER) {
    if (event == BUTTON_ENC_LONG) {
      bool wasEditing = timerEditIsActive();
      timerEditStart(0);
      if (!wasEditing && timerEditIsActive()) {
        buzzControlResetEdit();
      }
    } else if (event == BUTTON_RIGHT_SHORT && timerEditIsActive()) {
      timerEditFinish();
      buzzControlResetEdit();
    } else if (event == BUTTON_ENC_SHORT && timerEditIsActive()) {
      timerEditButtonPress();
      buzzControlResetEdit();
    } else if (event == BUTTON_ENC_SHORT) {
      timerStartStopToggle(0);
      buzzControlStartStop();
    } else if (event == BUTTON_RIGHT_SHORT) {
      timerStartStopToggle(0);
      buzzControlStartStop();
    } else if (event == BUTTON_LEFT_SHORT) {
      timerReset(0);
      buzzControlResetEdit();
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
