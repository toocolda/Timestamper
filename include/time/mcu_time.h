#pragma once

/**
 * @file mcu_time.h
 * @brief MCU timekeeping synchronized to GPS when available
 *
 * Maintains system time via:
 * - Millisecond tick counter (from Arduino millis())
 * - Periodic GPS sync to correct drift
 * - Manual time override capability
 * - Continues independently when GPS signal lost
 */

#include <stdint.h>
#include "time_datatypes.h"

/**
 * Synchronize MCU time to GPS-provided time
 * @param gpsTime Time structure from GPS module
 */
void time_syncToGPS(const TimeEdit_t* gpsTime);

/**
 * Override MCU time with manual input
 * @param newTime User-entered time
 */
void time_setManual(const TimeEdit_t* newTime);

/**
 * Get current MCU time
 * @return Current time (UTC)
 */
TimeEdit_t time_getCurrent(void);

/**
 * Check if manual time was recently set (grace period for no GPS sync)
 * @return true if manual time set within last 5 seconds
 */
bool time_hasManualOverride(void);

/**
 * Check if GPS sync should be skipped (grace period after manual set)
 * @return true if recent manual override active
 */
bool time_shouldSkipGPSSync(void);

// ===== Legacy API (current firmware usage) =====
void mcuTimeSync(TimeEdit_t* timeData);
TimeEdit_t mcuTimeGetCurrent(void);
bool shouldSkipGPSSync(void);

void setManualTime(TimeEdit_t* timeData);
TimeEdit_t getManualTime(void);
bool hasManualTime(void);
