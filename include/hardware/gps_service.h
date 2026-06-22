#pragma once

/**
 * @file gps_service.h
 * @brief GPS power management, UTC sync state machine, optional PPS discipline.
 *
 * Owns the TinyGPSPlus instance, the GPS module power/enable pins, the UART
 * power gating, and the sync/auto-sync scheduling. The public sync-result API
 * (gpsSyncRequest(), gpsSyncIsSearching(), GpsSyncResult, ...) lives in
 * core/modes.h since the UI layer consumes it.
 */

#include <stdint.h>

// One-time hardware bring-up from setup(): GPS UART/power/enable/PPS pins,
// leaves the GPS module powered off.
void gpsServiceInit(void);

// Per-loop service: GPS config retries, UART byte pump, sync state machine,
// auto-sync scheduling, and (when enabled) PPS discipline.
void gpsServicePoll(void);

// Apply the GPS power on/off policy derived from the current mode + sync state.
void gpsApplyPowerPolicy(void);

// True while the GPS module is powered (used by the sleep policy).
bool gpsPowerIsOn(void);
