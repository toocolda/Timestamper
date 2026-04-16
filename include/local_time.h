#pragma once

#include <stdint.h>
#include "time_edit.h"  // For TimeEdit_t structure

// ===== UTC Offset Management (for UTC&Local Mode) =====
// Stores timezone offset and calculates local time from UTC

// Get current UTC offset (-12 to +14 hours)
int8_t getUTCOffset();

// Set UTC offset (-12 to +14 hours)
void setUTCOffset(int8_t offset);

// Calculate local time from UTC time
// Handles date wraparound when hour goes negative or >= 24
TimeEdit_t calculateLocalTime(TimeEdit_t utcTime);

// Calculate local time from UTC time using a provided offset value
// Useful while editing offset so display reflects live (unsaved) value.
TimeEdit_t calculateLocalTimeWithOffset(TimeEdit_t utcTime, int8_t offset);

// ===== Offset Editing State Machine =====
// Similar to time_edit, but for editing the timezone offset

typedef enum {
  OFFSET_EDIT_NONE = 0,
  OFFSET_EDIT_ACTIVE,
  OFFSET_EDIT_DONE,
} OffsetEditState_t;

void offsetEditStart(int8_t currentOffset);     // Enter offset edit mode
void offsetEditStop();                          // Exit offset edit mode
bool offsetEditIsActive();                      // Check if currently editing offset
void offsetEditRotaryInput(int32_t delta);      // Adjust offset with encoder
int8_t offsetEditGetValue();                    // Get current edited offset value
OffsetEditState_t offsetEditGetState();         // Get current edit state
bool offsetEditShouldFlash();                   // For blinking offset display
