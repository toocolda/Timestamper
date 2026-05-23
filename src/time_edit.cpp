#include <Arduino.h>
#include "time/time_edit.h"
#include "time/mcu_time.h"
#include "time/crystal_time.h"
#include "core/modes.h"

// ===== Time Edit State =====
static TimeEdit_t g_editData;
static EditField_t g_currentField = EDIT_FIELD_NONE;
static bool g_isEditing = false;
static uint32_t g_lastInputTime = 0;
static uint32_t g_flashToggleTime = 0;
static bool g_showFlash = true;

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
  g_lastInputTime = crystalTimeGetMillis();
  g_flashToggleTime = crystalTimeGetMillis();
  g_showFlash = true;
}

// ===== Stop Time Edit Mode and Save =====
void timeEditStop() {
  if (g_isEditing && g_currentField != EDIT_FIELD_NONE) {
    setManualTime(&g_editData);
    // Manual edits intentionally clear the previous GPS result so UTC mode
    // shows that the clock is user-set rather than freshly GPS-verified.
    gpsSyncClearLastResult();
    // Don't sync MCU time - let manual time display directly without elapsed calculation
  }
  g_isEditing = false;
  g_currentField = EDIT_FIELD_NONE;
}

// ===== Check if Editing =====
bool timeEditIsActive() {
  if (!g_isEditing) return false;
  
  // Check for inactivity timeout
  if (crystalTimeElapsedMs(g_lastInputTime, INACTIVITY_TIMEOUT_MS)) {
    timeEditStop();
    return false;
  }
  
  return true;
}

// ===== Handle Rotary Input =====
void timeEditRotaryInput(int32_t delta) {
  if (!g_isEditing) return;
  if (delta == 0) return;
  
  g_lastInputTime = crystalTimeGetMillis();
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
  
  g_lastInputTime = crystalTimeGetMillis();
  g_showFlash = true;
  g_flashToggleTime = crystalTimeGetMillis();
  
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
  
  if (crystalTimeElapsedMs(g_flashToggleTime, FLASH_INTERVAL_MS)) {
    g_showFlash = !g_showFlash;
    g_flashToggleTime = crystalTimeGetMillis();
  }
  
  return g_showFlash;
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
