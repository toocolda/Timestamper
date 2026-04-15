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
static TimeEdit_t g_manualTime = {2020, 1, 1, 0, 0, 0};
static bool g_hasManualTime = false;

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
    case EDIT_FIELD_YEAR:
      g_editData.year += direction;
      if (g_editData.year < 2000) g_editData.year = 2000;
      if (g_editData.year > 2099) g_editData.year = 2099;
      break;
    case EDIT_FIELD_MONTH:
      g_editData.month += direction;
      if (g_editData.month < 1) g_editData.month = 12;
      if (g_editData.month > 12) g_editData.month = 1;
      break;
    case EDIT_FIELD_DAY:
      g_editData.day += direction;
      if (g_editData.day < 1) g_editData.day = 31;
      if (g_editData.day > 31) g_editData.day = 1;
      break;
    case EDIT_FIELD_HOUR:
      g_editData.hour += direction;
      if (g_editData.hour > 23) g_editData.hour = 0;
      if (g_editData.hour == 255) g_editData.hour = 23;
      break;
    case EDIT_FIELD_MINUTE:
      g_editData.minute += direction;
      if (g_editData.minute > 59) g_editData.minute = 0;
      if (g_editData.minute == 255) g_editData.minute = 59;
      break;
    case EDIT_FIELD_SECOND:
      g_editData.second += direction;
      if (g_editData.second > 59) g_editData.second = 0;
      if (g_editData.second == 255) g_editData.second = 59;
      break;
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
  }
}

// ===== Get Saved Manual Time =====
TimeEdit_t getManualTime() {
  return g_manualTime;
}

// ===== Check if Manual Time Set =====
bool hasManualTime() {
  return g_hasManualTime;
}
