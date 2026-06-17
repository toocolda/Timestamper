#pragma once

/**
 * @file timer.h
 * @brief Single fuel countdown timer with alarm
 *
 * Fuel timer features:
 * - 00:00:00 to 99:59:59 range
 * - Configurable initial value
 * - Automatic alarm trigger + audio alert
 * - Elapsed time counter after alarm
 * - Start/stop/reset control
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
bool timerAlarmToneOn(void);

void timerAcknowledgeAllAlarms(void);

// ===== Display State =====

/**
 * Get display values for the timer
 * @param channel Compatibility parameter (ignored in single-timer mode)
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
void timerApplyDefaultPreset(void);

// ===== Edit Mode =====

void timerEditStart(uint8_t channel);
bool timerEditIsActive(void);
void timerEditButtonPress(void);
void timerEditFinish(void);
void timerEditRotaryInput(int32_t delta);
void timerEditGetPreview(uint8_t* hour, uint8_t* minute, uint8_t* second);
uint8_t timerEditGetIndex(void);
bool timerEditShouldFlash(void);

typedef enum { TIMER_EDIT_NONE = 0, TIMER_EDIT_HOUR, TIMER_EDIT_MINUTE, TIMER_EDIT_SECOND } TimerEditField_t;
TimerEditField_t timerEditGetField(void);
