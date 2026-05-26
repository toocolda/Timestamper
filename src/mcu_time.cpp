#include <Arduino.h>
#include "time/mcu_time.h"
#include "time/crystal_time.h"

// ===== MCU Time State =====
static TimeEdit_t g_mcuCurrentTime = {2020, 1, 1, 0, 0, 0};  // Cache current calculated time
static uint32_t g_mcuTimeBaseSeconds = 0;  // Unix-like seconds counter
static uint32_t g_mcuTimeSyncCrystalTicks256 = 0;  // Crystal tick snapshot at last sync
static int16_t g_mcuDriftPpm = 0;  // Clock correction in parts per million
static bool g_mcuHasSync = false;

// ===== Manual Time Storage =====
static TimeEdit_t g_manualTime = {2020, 1, 1, 12, 0, 0};
static bool g_hasManualTime = false;

// ===== Sync MCU Clock to Known Time =====
void mcuTimeSync(TimeEdit_t* timeData) {
  if (timeData) {
    // Store the complete date/time for quick access
    g_mcuCurrentTime = *timeData;
    // Convert full time to seconds for elapsed calculation
    // Must cast to uint32_t first to prevent 16-bit overflow during intermediate calculations
    g_mcuTimeBaseSeconds = ((uint32_t)timeData->hour * 3600) + ((uint32_t)timeData->minute * 60) + timeData->second;
    g_mcuTimeSyncCrystalTicks256 = crystalTimeGetTicks256();
    g_mcuHasSync = true;
  }
}

// ===== Get Current Time with Elapsed Calculation =====
TimeEdit_t mcuTimeGetCurrent() {
  // Always use elapsed calculation for smooth ticking, regardless of grace period
  
  TimeEdit_t current = g_mcuCurrentTime;  // Start with cached value
  
  if (!g_mcuHasSync) {
    // No sync yet, return manual time or default
    return (g_hasManualTime) ? g_manualTime : g_mcuCurrentTime;
  }
  
  // Calculate elapsed ticks since last sync and apply ppm correction.
  uint32_t elapsedTicks = crystalTimeGetTicks256() - g_mcuTimeSyncCrystalTicks256;
  // Approximate 1e6 with 2^20 (1048576) to avoid expensive 64-bit division code.
  int64_t corrTermQ20 = ((int64_t)elapsedTicks * (int64_t)g_mcuDriftPpm);
  if (corrTermQ20 >= 0) corrTermQ20 += 524288LL;
  else corrTermQ20 -= 524288LL;
  int32_t correctionTicks = (int32_t)(corrTermQ20 >> 20);

  int64_t correctedTicks = (int64_t)elapsedTicks + (int64_t)correctionTicks;
  if (correctedTicks < 0) correctedTicks = 0;
  uint32_t elapsedSeconds = (uint32_t)(correctedTicks >> 8);
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

bool mcuTimeHasSync() {
  return g_mcuHasSync;
}

void mcuTimeSetDriftPpm(int16_t ppm) {
  g_mcuDriftPpm = ppm;
}

int16_t mcuTimeGetDriftPpm(void) {
  return g_mcuDriftPpm;
}

// ===== Save Manual Time =====
void setManualTime(TimeEdit_t* timeData) {
  if (timeData) {
    g_manualTime = *timeData;
    g_hasManualTime = true;
    mcuTimeSync(timeData);  // Initialize MCU clock to start ticking immediately
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

