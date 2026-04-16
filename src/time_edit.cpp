#include <Arduino.h>
#include "time_edit.h"

// ===== Time Edit State =====
static TimeEdit_t g_editData;
static EditField_t g_currentField = EDIT_FIELD_NONE;
static bool g_isEditing = false;
static uint32_t g_lastInputTime = 0;
static uint32_t g_flashToggleTime = 0;
static bool g_showFlash = true;

// ===== Manual Time Storage =====
static TimeEdit_t g_manualTime = {2020, 1, 1, 12, 0, 0};  // Include initial hour:12 for testing
static bool g_hasManualTime = false;
static uint32_t g_manualTimeSetAt = 0;  // Timestamp when manual time was set (prevent GPS overwrite for 5 seconds)

// ===== MCU Time Tracking (independent of GPS) =====
static TimeEdit_t g_mcuCurrentTime = {2020, 1, 1, 0, 0, 0};  // Cache current calculated time
static uint32_t g_mcuTimeBaseSeconds = 0;  // Unix-like seconds counter
static uint32_t g_mcuTimeLastSyncMs = 0;   // Last sync time from GPS (milliseconds)

#define INACTIVITY_TIMEOUT_MS 10000
#define FLASH_INTERVAL_MS 300

// ===== Start Time Edit Mode =====
void timeEditStart(TimeEdit_t* timeData) {
  if (timeData) {
    g_editData = *timeData;
  } else {
    // Default values
    g_editData.year = 2020;
    g_editData.month = 1;
    g_editData.day = 1;
    g_editData.hour = 0;
    g_editData.minute = 0;
    g_editData.second = 0;
  }
  
  g_isEditing = true;
  g_currentField = EDIT_FIELD_YEAR;
  g_lastInputTime = millis();
  g_flashToggleTime = millis();
  g_showFlash = true;
}

// ===== Stop Time Edit Mode and Save =====
void timeEditStop() {
  if (g_isEditing && g_currentField != EDIT_FIELD_NONE) {
    setManualTime(&g_editData);
    // Don't sync MCU time - let manual time display directly without elapsed calculation
  }
  g_isEditing = false;
  g_currentField = EDIT_FIELD_NONE;
}

// ===== Check if Editing =====
bool timeEditIsActive() {
  if (!g_isEditing) return false;
  
  // Check for inactivity timeout
  if (millis() - g_lastInputTime > INACTIVITY_TIMEOUT_MS) {
    timeEditStop();
    return false;
  }
  
  return true;
}

// ===== Handle Rotary Input =====
void timeEditRotaryInput(int32_t delta) {
  if (!g_isEditing) return;
  if (delta == 0) return;
  
  g_lastInputTime = millis();
  int direction = (delta > 0) ? 1 : -1;
  
  switch (g_currentField) {
    case EDIT_FIELD_NONE:
      break;  // Do nothing if no field selected
    case EDIT_FIELD_YEAR:
      g_editData.year += direction;
      if (g_editData.year < 2000) g_editData.year = 2000;
      if (g_editData.year > 2099) g_editData.year = 2099;
      break;
    case EDIT_FIELD_MONTH:
      g_editData.month += direction;
      if (g_editData.month < 1) g_editData.month = 12;
      if (g_editData.month > 12) g_editData.month = 1;
      // Validate day after month change
      if (!isValidDate(g_editData.year, g_editData.month, g_editData.day)) {
        g_editData.day = 28;  // Safe default
      }
      break;
    case EDIT_FIELD_DAY: {
      // Use signed int to handle negative wrapping correctly
      int8_t d = (int8_t)g_editData.day + direction;
      // Wrapping with validation - get max day for month
      uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      bool isLeap = (g_editData.year % 400 == 0) || ((g_editData.year % 4 == 0) && (g_editData.year % 100 != 0));
      uint8_t maxDay = daysInMonth[g_editData.month - 1];
      if (isLeap && g_editData.month == 2) maxDay = 29;
      
      // Wrap at boundaries
      if (d < 1) d = maxDay;
      if (d > maxDay) d = 1;
      g_editData.day = (uint8_t)d;
      break;
    }
    case EDIT_FIELD_HOUR: {
      // Use signed int to handle negative wrapping correctly
      int8_t h = (int8_t)g_editData.hour + direction;
      if (h < 0) h = 23;
      if (h > 23) h = 0;
      g_editData.hour = (uint8_t)h;
      break;
    }
    case EDIT_FIELD_MINUTE: {
      // Use signed int to handle negative wrapping correctly
      int8_t m = (int8_t)g_editData.minute + direction;
      if (m < 0) m = 59;
      if (m > 59) m = 0;
      g_editData.minute = (uint8_t)m;
      break;
    }
    case EDIT_FIELD_SECOND: {
      // Use signed int to handle negative wrapping correctly
      int8_t s = (int8_t)g_editData.second + direction;
      if (s < 0) s = 59;
      if (s > 59) s = 0;
      g_editData.second = (uint8_t)s;
      break;
    }
    default:
      break;
  }
}

// ===== Handle Button Press (Move to Next Field) =====
void timeEditButtonPress() {
  if (!g_isEditing) return;
  
  g_lastInputTime = millis();
  g_showFlash = true;
  g_flashToggleTime = millis();
  
  switch (g_currentField) {
    case EDIT_FIELD_YEAR:
      g_currentField = EDIT_FIELD_MONTH;
      break;
    case EDIT_FIELD_MONTH:
      g_currentField = EDIT_FIELD_DAY;
      break;
    case EDIT_FIELD_DAY:
      g_currentField = EDIT_FIELD_HOUR;
      break;
    case EDIT_FIELD_HOUR:
      g_currentField = EDIT_FIELD_MINUTE;
      break;
    case EDIT_FIELD_MINUTE:
      g_currentField = EDIT_FIELD_SECOND;
      break;
    case EDIT_FIELD_SECOND:
      g_currentField = EDIT_FIELD_DONE;
      timeEditStop();
      break;
    default:
      break;
  }
}

// ===== Get Current Time Data =====
TimeEdit_t timeEditGetData() {
  return g_editData;
}

// ===== Get Current Field =====
EditField_t timeEditGetCurrentField() {
  return g_currentField;
}

// ===== Flash Control for Visual Feedback =====
bool timeEditShouldFlash() {
  if (!g_isEditing) return true;
  
  if (millis() - g_flashToggleTime > FLASH_INTERVAL_MS) {
    g_showFlash = !g_showFlash;
    g_flashToggleTime = millis();
  }
  
  return g_showFlash;
}

// ===== Save Manual Time =====
void setManualTime(TimeEdit_t* timeData) {
  if (timeData) {
    g_manualTime = *timeData;
    g_hasManualTime = true;
    g_manualTimeSetAt = millis();  // Record when manual time was set
    mcuTimeSync(timeData);  // Initialize MCU clock to start ticking immediately
  }
}

// ===== Check if GPS sync should be skipped (5s grace period after manual set) =====
bool shouldSkipGPSSync() {
  if (!g_hasManualTime) return false;
  uint32_t elapsed = millis() - g_manualTimeSetAt;
  return elapsed < 5000;  // Skip GPS sync for 5 seconds after manual time set
}

// ===== Get Saved Manual Time =====
TimeEdit_t getManualTime() {
  return g_manualTime;
}

// ===== Check if Manual Time Set =====
bool hasManualTime() {
  return g_hasManualTime;
}

// ===== Date Validation =====
bool isValidDate(uint16_t year, uint8_t month, uint8_t day) {
  // Validate month
  if (month < 1 || month > 12) return false;
  
  // Validate day - check if day is in valid range for the month
  uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
  // Check for leap year
  bool isLeapYear = (year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0));
  if (isLeapYear) daysInMonth[1] = 29;  // February has 29 days in leap year
  
  if (day < 1 || day > daysInMonth[month - 1]) return false;
  
  return true;
}

// ===== MCU Time Tracking =====
void mcuTimeSync(TimeEdit_t* timeData) {
  if (timeData) {
    // Store the complete date/time for quick access
    g_mcuCurrentTime = *timeData;
    // Convert full time to seconds for elapsed calculation
    // Must cast to uint32_t first to prevent 16-bit overflow during intermediate calculations
    g_mcuTimeBaseSeconds = ((uint32_t)timeData->hour * 3600) + ((uint32_t)timeData->minute * 60) + timeData->second;
    g_mcuTimeLastSyncMs = millis();
  }
}

TimeEdit_t mcuTimeGetCurrent() {
  // Always use elapsed calculation for smooth ticking, regardless of grace period
  
  TimeEdit_t current = g_mcuCurrentTime;  // Start with cached value
  
  if (g_mcuTimeLastSyncMs == 0) {
    // No sync yet, return manual time or default
    return (g_hasManualTime) ? g_manualTime : g_mcuCurrentTime;
  }
  
  // Calculate elapsed time since last sync
  uint32_t elapsedMs = millis() - g_mcuTimeLastSyncMs;
  uint32_t elapsedSeconds = elapsedMs / 1000;
  uint32_t currentTotalSeconds = g_mcuTimeBaseSeconds + elapsedSeconds;
  
  // Handle day wraparound (86400 seconds per day)
  uint32_t secondsPerDay = 86400;
  uint32_t daysElapsed = currentTotalSeconds / secondsPerDay;
  if (daysElapsed > 0) {
    // Days wrapped - increment date
    currentTotalSeconds = currentTotalSeconds % secondsPerDay;
    
    // Add daysElapsed to current date
    for (uint32_t i = 0; i < daysElapsed; i++) {
      // Get max day for current month
      uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      bool isLeap = (current.year % 400 == 0) || ((current.year % 4 == 0) && (current.year % 100 != 0));
      uint8_t maxDay = daysInMonth[current.month - 1];
      if (isLeap && current.month == 2) maxDay = 29;
      
      // Increment day
      current.day++;
      if (current.day > maxDay) {
        current.day = 1;
        current.month++;
        if (current.month > 12) {
          current.month = 1;
          current.year++;
        }
      }
    }
  }
  
  // Convert seconds back to HH:MM:SS
  current.hour = currentTotalSeconds / 3600;
  current.minute = (currentTotalSeconds % 3600) / 60;
  current.second = currentTotalSeconds % 60;
  
  return current;
}
