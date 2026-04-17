#pragma once

/**
 * @file timer.h
 * @brief Dual independent countdown timers with alarm
 *
 * Two timer channels with features:
 * - 0-59:59:59 countdown range
 * - Configurable initial value
 * - Automatic alarm trigger + audio alert
 * - Elapsed time counter after alarm
 * - Per-channel start/stop control
 */

#include <stdint.h>
#include "../time/time_datatypes.h"

/**
 * Update timer state - call frequently from loop
 * Handles countdown, alarm triggers, and sound patterns
 */
void timerModeUpdate(void);

// ===== Timer State Queries =====

bool timerIsRunning(uint8_t channel);
bool timerAlarmActive(uint8_t channel);
bool timerAnyAlarmActive(void);
bool timerAnyRunning(void);

void timerAcknowledgeAllAlarms(void);

// ===== Display State =====

/**
 * Get display values for a timer channel
 * @param channel 0 or 1
 * @param hour, minute, second Output time display values
 * @param elapsed true if elapsed (post-alarm)
 * @param running true if countdown active
 * @param alarm true if alarm currently active
 */
void timerGetDisplay(uint8_t channel, uint8_t* hour, uint8_t* minute, uint8_t* second,
                      bool* elapsed, bool* running, bool* alarm);

uint8_t timerGetSelected(void);
void timerToggleSelected(void);

// ===== Timer Control =====

void timerStartStopToggle(uint8_t channel);
void timerReset(uint8_t channel);

// ===== Edit Mode =====

void timerEditStart(uint8_t channel);
bool timerEditIsActive(void);
void timerEditButtonPress(void);
void timerEditRotaryInput(int32_t delta);
void timerEditGetPreview(uint8_t* hour, uint8_t* minute, uint8_t* second);
uint8_t timerEditGetIndex(void);
bool timerEditShouldFlash(void);

typedef enum { TIMER_EDIT_NONE = 0, TIMER_EDIT_HOUR, TIMER_EDIT_MINUTE, TIMER_EDIT_SECOND } TimerEditField_t;
TimerEditField_t timerEditGetField(void);
