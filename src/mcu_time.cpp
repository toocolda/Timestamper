#include <Arduino.h>
#include "time/mcu_time.h"

// ===== MCU Time State =====
static TimeEdit_t g_mcuCurrentTime = {2020, 1, 1, 0, 0, 0};  // Cache current calculated time
static uint32_t g_mcuTimeBaseSeconds = 0;  // Unix-like seconds counter
static uint32_t g_mcuTimeLastSyncMs = 0;   // Last sync time from GPS (milliseconds)

// ===== Manual Time Storage =====
static TimeEdit_t g_manualTime = {2020, 1, 1, 12, 0, 0};
static bool g_hasManualTime = false;
static uint32_t g_manualTimeSetAt = 0;  // Timestamp when manual time was set (prevent GPS overwrite)

// ===== Sync MCU Clock to Known Time =====
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

// ===== Get Current Time with Elapsed Calculation =====
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

// ===== Save Manual Time =====
void setManualTime(TimeEdit_t* timeData) {
  if (timeData) {
    g_manualTime = *timeData;
    g_hasManualTime = true;
    g_manualTimeSetAt = millis();  // Record when manual time was set
    mcuTimeSync(timeData);  // Initialize MCU clock to start ticking immediately
  }
}

// ===== Check if GPS sync should be skipped (30s grace period after manual set) =====
bool shouldSkipGPSSync() {
  if (!g_hasManualTime) return false;
  uint32_t elapsed = millis() - g_manualTimeSetAt;
  return elapsed < 30000;  // Skip GPS sync for 30 seconds after manual time set (give user time to verify)
}

// ===== Get Saved Manual Time =====
TimeEdit_t getManualTime() {
  return g_manualTime;
}

// ===== Check if Manual Time Set =====
bool hasManualTime() {
  return g_hasManualTime;
}
