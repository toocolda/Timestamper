#include <Arduino.h>
#include "st7036.h"
#include "config.h"
#include <TinyGPS++.h>
#include "time_edit.h"
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
  
  // If entering/exiting edit mode, clear display
  if (editMode != lastEditMode) {
    lcd.clear();
    lastEditMode = editMode;
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
    
    snprintf(buf, LCD_BUF_SIZE, "%s-%s-%s %s:%s:%s",
             year_str, month_str, day_str, hour_str, min_str, sec_str);
    lcd.print(buf);
    
    lcd.setCursor(0, 1);
    const char* fieldName = "";
    switch (field) {
      case EDIT_FIELD_YEAR: fieldName = "Year"; break;
      case EDIT_FIELD_MONTH: fieldName = "Month"; break;
      case EDIT_FIELD_DAY: fieldName = "Day"; break;
      case EDIT_FIELD_HOUR: fieldName = "Hour"; break;
      case EDIT_FIELD_MINUTE: fieldName = "Min"; break;
      case EDIT_FIELD_SECOND: fieldName = "Sec"; break;
      default: fieldName = "Done"; break;
    }
    snprintf(buf, LCD_BUF_SIZE, "Editing: %s    ", fieldName);
    lcd.print(buf);
    
  } else {
    // ===== NORMAL DISPLAY =====
    bool timeValid = isGPSTimeReliable();
    int sat = gps.satellites.value();
    int h = gps.time.hour();
    int m = gps.time.minute();
    int s = gps.time.second();
    
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
      if (timeValid) {
        int y = gps.date.year();
        int mo = gps.date.month();
        int d = gps.date.day();
        snprintf(buf, LCD_BUF_SIZE,
                 "%04d-%02d-%02d %02d:%02d:%02dZ",
                 y, mo, d, h, m, s);
      } else if (hasManualTime()) {
        // Use manual time as fallback
        TimeEdit_t manual = getManualTime();
        snprintf(buf, LCD_BUF_SIZE,
                 "%04d-%02d-%02d %02d:%02d:%02dZ",
                 manual.year, manual.month, manual.day,
                 manual.hour, manual.minute, manual.second);
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
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UTC&Local Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
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
      // Start time edit mode
      TimeEdit_t currentTime;
      if (isGPSTimeReliable()) {
        currentTime.year = gps.date.year();
        currentTime.month = gps.date.month();
        currentTime.day = gps.date.day();
        currentTime.hour = gps.time.hour();
        currentTime.minute = gps.time.minute();
        currentTime.second = gps.time.second();
      } else {
        currentTime.year = 2020;
        currentTime.month = 1;
        currentTime.day = 1;
        currentTime.hour = 0;
        currentTime.minute = 0;
        currentTime.second = 0;
      }
      timeEditStart(&currentTime);
    } else if (event == BUTTON_ENC_SHORT && timeEditIsActive()) {
      // Move to next field in edit mode
      timeEditButtonPress();
    }
  }
}
