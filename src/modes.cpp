#include <Arduino.h>
#include "st7036.h"
#include "config.h"
#include <TinyGPS++.h>
#include "time_edit.h"
#include "mcu_time.h"
#include "local_time.h"
#include "buttons.h"

// Forward declarations from main.cpp
extern ST7036 lcd;
extern TinyGPSPlus gps;

// ===== Buzzer Pin =====
#define BUZZER PIN_BUZZER

// ===== Global Mode Variables =====
uint8_t g_currentMode = MODE_UTC_ONLY;
uint32_t g_modeEpoch = 0;  // Increments every mode change

// ===== Buzzer Control =====
void buzzOnce(uint16_t durationMs) {
  tone(BUZZER, 1000);  // 1000 Hz tone
  delay(durationMs);
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
void displayModeTimestampReview() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Timestamp Review");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Stopwatch =====
void displayModeStopwatch() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Stopwatch Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Timer =====
void displayModeTimer() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Timer Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Local Only =====
void displayModeLocalOnly() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Local Only Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
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
  if (mode == MODE_UTC_ONLY) {
    if (event == BUTTON_ENC_LONG) {
      // Start time edit mode - use current MCU time (which is GPS-synced or manually set)
      TimeEdit_t currentTime = mcuTimeGetCurrent();
      timeEditStart(&currentTime);
    } else if (event == BUTTON_ENC_SHORT && timeEditIsActive()) {
      // Move to next field in edit mode
      timeEditButtonPress();
    }
  } else if (mode == MODE_UTC_LOCAL) {
    if (event == BUTTON_ENC_LONG) {
      // Start offset edit mode
      int8_t currentOffset = getUTCOffset();
      offsetEditStart(currentOffset);
    } else if (event == BUTTON_ENC_SHORT && offsetEditIsActive()) {
      // Exit offset edit mode and save
      offsetEditStop();
    }
  }
}
