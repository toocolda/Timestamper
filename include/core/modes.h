#pragma once

/**
 * @file modes.h
 * @brief UI mode dispatcher and state management
 *
 * Manages the 7 display modes:
 * - UTC Only: UTC display with optional GPS time sync actions
 * - UTC/Local: Dual timezone display
 * - Timestamp Review: Browse & manage saved timestamps
 * - Stopwatch: Single stopwatch
 * - Timer: Single fuel countdown/elapsed timer
 * - Local Only (Desk Mode): Formatted calendar/clock display
 * - GPS Info: GS (knots), heading, altitude (feet)
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

// ===== GPS Sync Coordination =====
enum GpsSyncResult : uint8_t {
	GPS_SYNC_RESULT_NONE = 0,
	GPS_SYNC_RESULT_OK,
	GPS_SYNC_RESULT_TIMEOUT
};

bool isGPSTimeReliable(void);
void gpsSyncRequest(void);
bool gpsSyncIsSearching(void);
uint16_t gpsSyncGetRemainingSeconds(void);
uint16_t gpsSyncGetElapsedSeconds(void);
GpsSyncResult gpsSyncGetLastResult(void);
uint16_t gpsSyncGetLastResultAgeSeconds(void);
void gpsSyncClearLastResult(void);

// ===== Mode State =====
extern uint8_t g_currentMode;
extern uint32_t g_modeEpoch;  // Incremented when display needs refresh

void buzzOnce(uint16_t durationMs);

// ===== Timestamp Mode Helpers =====
bool timestampModeIsScrollActive(void);
void timestampModeScrollBy(int32_t delta);
