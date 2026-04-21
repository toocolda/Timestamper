#pragma once

/**
 * @file stopwatch.h
 * @brief Single stopwatch
 *
 * Stopwatch features:
 * - Continuous elapsed time counting (0 to ~99:59:59)
 * - Start/stop/reset control
 * - Display in MM:SS.T format (tenths of seconds)
 */

#include <stdint.h>

/**
 * Get elapsed time in tenths of seconds
 * @param channel Compatibility parameter (ignored in single-stopwatch mode)
 * @return Total elapsed tenths (0-9999999)
 */
uint32_t stopwatchGetTenths(uint8_t channel);

/**
 * Check if stopwatch is currently running
 * @param channel Compatibility parameter (ignored in single-stopwatch mode)
 * @return true if counting
 */
bool stopwatchIsRunning(uint8_t channel);

/**
 * Get current selected stopwatch
 * @return Always 0 in single-stopwatch mode
 */
uint8_t stopwatchGetSelected(void);

/**
 * No-op in single-stopwatch mode (retained for compatibility)
 */
void stopwatchToggleSelected(void);

/**
 * Start/stop toggle
 * @param channel Compatibility parameter (ignored in single-stopwatch mode)
 */
void stopwatchStartStopToggle(uint8_t channel);

/**
 * Reset stopwatch to 0:00:00
 * @param channel Compatibility parameter (ignored in single-stopwatch mode)
 */
void stopwatchReset(uint8_t channel);

/**
 * Get display values (hours, minutes, seconds, tenths)
 * @param channel Compatibility parameter (ignored in single-stopwatch mode)
 * @param h, m, s, t Output display values
 */
void stopwatchGetDisplay(uint8_t channel, uint8_t* h, uint8_t* m, uint8_t* s, uint8_t* t);

/**
 * Check if any stopwatch is currently running
 * @return true if at least one stopwatch active
 */
bool stopwatchAnyRunning(void);
