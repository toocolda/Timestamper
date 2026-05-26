#pragma once

/**
 * @file mcu_time.h
 * @brief MCU timekeeping synchronized to GPS when available
 *
 * Maintains system time via:
 * - Crystal-backed tick counter (crystalTimeGetSeconds/Millis)
 * - Periodic GPS sync to correct drift
 * - Manual time override capability
 * - Continues independently when GPS signal lost
 */

#include <stdint.h>
#include "time_datatypes.h"

void mcuTimeSync(TimeEdit_t* timeData);
TimeEdit_t mcuTimeGetCurrent(void);
/**
 * Check whether the clock has been initialized by any trusted source.
 * This becomes true after GPS sync or manual time entry.
 */
bool mcuTimeHasSync(void);

// Set/get software clock drift correction in parts-per-million.
void mcuTimeSetDriftPpm(int16_t ppm);
int16_t mcuTimeGetDriftPpm(void);

void setManualTime(TimeEdit_t* timeData);
TimeEdit_t getManualTime(void);
bool hasManualTime(void);
