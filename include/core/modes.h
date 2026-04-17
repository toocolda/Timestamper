#pragma once

/**
 * @file modes.h
 * @brief UI mode dispatcher and state management
 *
 * Manages the 6 display modes:
 * - UTC Only: GPS time display
 * - UTC/Local: Dual timezone display
 * - Timestamp Review: Browse & manage saved timestamps
 * - Stopwatch: Two independent stopwatches
 * - Timer: Two independent countdowntimers
 * - Local Only (Desk Mode): Formatted calendar/clock display
 */

#include <stdint.h>
#include "hardware/buttons.h"

// ===== UI Mode Dispatcher =====
/**
 * Update display for current mode
 * @param mode The mode to display (see MODE_* defines in config.h)
 */
void updateDisplay(uint8_t mode);

/**
 * Handle button/encoder events for current mode
 * @param mode Current UI mode
 * @param event Button event to process
 */
void handleModeEvent(uint8_t mode, ButtonEvent_t event);

// ===== Mode State =====
extern uint8_t g_currentMode;
extern uint32_t g_modeEpoch;  // Incremented when display needs refresh

void buzzOnce(uint16_t durationMs);

// ===== Timestamp Mode Helpers =====
bool timestampModeIsScrollActive(void);
void timestampModeScrollBy(int32_t delta);
