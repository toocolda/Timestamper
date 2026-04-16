#include <Arduino.h>
#include "local_time.h"

// ===== UTC Offset Storage =====
static int8_t g_utcOffset = 0;  // Default: UTC+00

// ===== Offset Edit State =====
static int8_t g_offsetEditValue = 0;           // Value being edited
static OffsetEditState_t g_offsetEditState = OFFSET_EDIT_NONE;
static uint32_t g_offsetFlashToggleTime = 0;
static bool g_offsetShowFlash = true;

#define OFFSET_FLASH_INTERVAL_MS 300

// ===== Get Current UTC Offset =====
int8_t getUTCOffset() {
  return g_utcOffset;
}

// ===== Set UTC Offset =====
void setUTCOffset(int8_t offset) {
  if (offset >= -12 && offset <= 14) {
    g_utcOffset = offset;
  }
}

// ===== Calculate Local Time from UTC =====
TimeEdit_t calculateLocalTime(TimeEdit_t utcTime) {
  return calculateLocalTimeWithOffset(utcTime, g_utcOffset);
}

TimeEdit_t calculateLocalTimeWithOffset(TimeEdit_t utcTime, int8_t offset) {
  TimeEdit_t localTime = utcTime;
  int16_t totalHour = (int16_t)localTime.hour + (int16_t)offset;
  
  // Handle day wraparound
  if (totalHour < 0) {
    // Previous day
    localTime.day--;
    if (localTime.day < 1) {
      // Previous month
      localTime.month--;
      if (localTime.month < 1) {
        // Previous year
        localTime.year--;
        localTime.month = 12;
      }
      // Get days in previous month
      uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      bool isLeap = (localTime.year % 400 == 0) || ((localTime.year % 4 == 0) && (localTime.year % 100 != 0));
      uint8_t maxDay = daysInMonth[localTime.month - 1];
      if (isLeap && localTime.month == 2) maxDay = 29;
      localTime.day = maxDay;
    }
    totalHour += 24;
  } else if (totalHour >= 24) {
    // Next day
    uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool isLeap = (localTime.year % 400 == 0) || ((localTime.year % 4 == 0) && (localTime.year % 100 != 0));
    uint8_t maxDay = daysInMonth[localTime.month - 1];
    if (isLeap && localTime.month == 2) maxDay = 29;
    
    localTime.day++;
    if (localTime.day > maxDay) {
      localTime.day = 1;
      localTime.month++;
      if (localTime.month > 12) {
        localTime.month = 1;
        localTime.year++;
      }
    }
    totalHour -= 24;
  }
  
  localTime.hour = (uint8_t)totalHour;
  return localTime;
}

// ===== Start Offset Edit Mode =====
void offsetEditStart(int8_t currentOffset) {
  g_offsetEditValue = currentOffset;
  g_offsetEditState = OFFSET_EDIT_ACTIVE;
  g_offsetFlashToggleTime = millis();
  g_offsetShowFlash = true;
}

// ===== Stop Offset Edit Mode and Save =====
void offsetEditStop() {
  if (g_offsetEditState == OFFSET_EDIT_ACTIVE) {
    setUTCOffset(g_offsetEditValue);  // Save the edited offset
  }
  g_offsetEditState = OFFSET_EDIT_NONE;
}

// ===== Check if Editing Offset =====
bool offsetEditIsActive() {
  return g_offsetEditState == OFFSET_EDIT_ACTIVE;
}

// ===== Adjust Offset with Encoder =====
void offsetEditRotaryInput(int32_t delta) {
  if (g_offsetEditState != OFFSET_EDIT_ACTIVE) return;
  if (delta == 0) return;
  
  int direction = (delta > 0) ? 1 : -1;
  int16_t newOffset = (int16_t)g_offsetEditValue + direction;
  
  // Clamp to valid range -12 to +14
  if (newOffset < -12) newOffset = -12;
  if (newOffset > 14) newOffset = 14;
  
  g_offsetEditValue = (int8_t)newOffset;
}

// ===== Get Current Edited Offset Value =====
int8_t offsetEditGetValue() {
  return g_offsetEditValue;
}

// ===== Get Edit State =====
OffsetEditState_t offsetEditGetState() {
  return g_offsetEditState;
}

// ===== Flash Control for Visual Feedback =====
bool offsetEditShouldFlash() {
  if (g_offsetEditState != OFFSET_EDIT_ACTIVE) return true;
  
  if (millis() - g_offsetFlashToggleTime > OFFSET_FLASH_INTERVAL_MS) {
    g_offsetShowFlash = !g_offsetShowFlash;
    g_offsetFlashToggleTime = millis();
  }
  
  return g_offsetShowFlash;
}
