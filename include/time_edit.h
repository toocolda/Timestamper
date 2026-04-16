#pragma once

#include <stdint.h>

// ===== Time Edit State =====
typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} TimeEdit_t;

// ===== Edit Field Types =====
typedef enum {
  EDIT_FIELD_NONE = 0,
  EDIT_FIELD_YEAR,
  EDIT_FIELD_MONTH,
  EDIT_FIELD_DAY,
  EDIT_FIELD_HOUR,
  EDIT_FIELD_MINUTE,
  EDIT_FIELD_SECOND,
  EDIT_FIELD_DONE,
} EditField_t;

// ===== Time Edit Interface =====
void timeEditStart(TimeEdit_t* timeData);
void timeEditStop();
bool timeEditIsActive();
void timeEditRotaryInput(int32_t delta);
void timeEditButtonPress();
TimeEdit_t timeEditGetData();
EditField_t timeEditGetCurrentField();
bool timeEditShouldFlash();  // For flashing display of current field

// ===== Manual Time Tracking =====
void setManualTime(TimeEdit_t* timeData);
TimeEdit_t getManualTime();
bool hasManualTime();
bool shouldSkipGPSSync();  // Check if manual time was recently set

// ===== Date Validation =====
bool isValidDate(uint16_t year, uint8_t month, uint8_t day);

// ===== MCU Time Tracking =====
void mcuTimeSync(TimeEdit_t* timeData);
TimeEdit_t mcuTimeGetCurrent();
