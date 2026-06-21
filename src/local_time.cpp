#include <Arduino.h>
#include <EEPROM.h>
#include "time/local_time.h"
#include "time/crystal_time.h"
#include "time/time_utils.h"

// ===== UTC Offset Storage =====
static int8_t g_utcOffset = 0;  // Default: UTC+00
static bool g_utcOffsetLoaded = false;

static const int kUtcOffsetSignatureAddr = E2END - 1;
static const int kUtcOffsetValueAddr = E2END - 2;
static const uint8_t kUtcOffsetSignature = 0xA5;

static void loadUTCOffset() {
  if (g_utcOffsetLoaded) return;

  if (EEPROM.read(kUtcOffsetSignatureAddr) == kUtcOffsetSignature) {
    uint8_t storedOffset = EEPROM.read(kUtcOffsetValueAddr);
    if (storedOffset <= 26) {
      g_utcOffset = (int8_t)storedOffset - 12;
    } else {
      g_utcOffset = 0;
    }
  } else {
    g_utcOffset = 0;
    EEPROM.update(kUtcOffsetValueAddr, (uint8_t)(g_utcOffset + 12));
    EEPROM.update(kUtcOffsetSignatureAddr, kUtcOffsetSignature);
  }

  g_utcOffsetLoaded = true;
}

static void saveUTCOffset() {
  EEPROM.update(kUtcOffsetValueAddr, (uint8_t)(g_utcOffset + 12));
  EEPROM.update(kUtcOffsetSignatureAddr, kUtcOffsetSignature);
}

// ===== Offset Edit State =====
static int8_t g_offsetEditValue = 0;           // Value being edited
static OffsetEditState_t g_offsetEditState = OFFSET_EDIT_NONE;
static uint32_t g_offsetFlashToggleTime = 0;
static bool g_offsetShowFlash = true;

#define OFFSET_FLASH_INTERVAL_MS 300

// ===== Get Current UTC Offset =====
int8_t getUTCOffset() {
  loadUTCOffset();
  return g_utcOffset;
}

// ===== Set UTC Offset =====
void setUTCOffset(int8_t offset) {
  if (offset >= -12 && offset <= 14) {
    loadUTCOffset();
    g_utcOffset = offset;
    saveUTCOffset();
  }
}

// ===== Calculate Local Time from UTC =====
TimeEdit_t calculateLocalTime(TimeEdit_t utcTime) {
  loadUTCOffset();  // Ensure offset is read from EEPROM before first use
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
      localTime.day = timeDaysInMonth(localTime.year, localTime.month);
    }
    totalHour += 24;
  } else if (totalHour >= 24) {
    // Next day
    uint8_t maxDay = timeDaysInMonth(localTime.year, localTime.month);
    
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
  g_offsetFlashToggleTime = crystalTimeGetMillis();
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

  return timeFlashToggle(&g_offsetFlashToggleTime, &g_offsetShowFlash, OFFSET_FLASH_INTERVAL_MS);
}
