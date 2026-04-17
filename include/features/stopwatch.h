#pragma once

/**
 * @file stopwatch.h
 * @brief Dual independent stopwatches
 *
 * Two stopwatch channels with features:
 * - Continuous elapsed time counting (0 to ~99:59:59)
 * - Start/stop/reset per channel
 * - Display in MM:SS.T format (tenths of seconds)
 * - Channel selection and independent control
 */

#include <stdint.h>

/**
 * Get elapsed time in tenths of seconds for a channel
 * @param channel 0 or 1
 * @return Total elapsed tenths (0-9999999)
 */
uint32_t stopwatchGetTenths(uint8_t channel);

/**
 * Check if stopwatch is currently running
 * @param channel 0 or 1
 * @return true if counting
 */
bool stopwatchIsRunning(uint8_t channel);

/**
 * Get current selected stopwatch
 * @return 0 or 1
 */
uint8_t stopwatchGetSelected(void);

/**
 * Toggle selected stopwatch (0 <-> 1)
 */
void stopwatchToggleSelected(void);

/**
 * Start/stop toggle for specified channel
 * @param channel 0 or 1
 */
void stopwatchStartStopToggle(uint8_t channel);

/**
 * Reset stopwatch to 0:00:00
 * @param channel 0 or 1
 */
void stopwatchReset(uint8_t channel);

/**
 * Get display values (hours, minutes, seconds, tenths)
 * @param channel 0 or 1
 * @param h, m, s, t Output display values
 */
void stopwatchGetDisplay(uint8_t channel, uint8_t* h, uint8_t* m, uint8_t* s, uint8_t* t);

/**
 * Check if any stopwatch is currently running
 * @return true if at least one stopwatch active
 */
bool stopwatchAnyRunning(void);
