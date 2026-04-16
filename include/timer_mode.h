#pragma once

#include <stdint.h>

// ===== Dual Timer Mode (Background Runner) =====
// Two independent channels with countdown -> alarm -> elapsed count-up.
// Each channel range: 00:00:00 .. 99:59:59

// Call frequently from loop() to update channel state and alarm sound pattern.
void timerModeUpdate();

// Index: 0 = T1, 1 = T2
void timerStartStopToggle(uint8_t index);
void timerReset(uint8_t index);

bool timerIsRunning(uint8_t index);
bool timerAnyRunning();

// Alarm state
bool timerAlarmActive(uint8_t index);
bool timerAnyAlarmActive();
void timerAcknowledgeAllAlarms();

// Selection for UI/control
uint8_t timerGetSelected();
void timerToggleSelected();

// Timer preset edit mode (HH -> MM -> SS).
typedef enum {
    TIMER_EDIT_NONE = 0,
    TIMER_EDIT_HOUR,
    TIMER_EDIT_MINUTE,
    TIMER_EDIT_SECOND,
} TimerEditField_t;

void timerEditStart(uint8_t index);       // Long press ENC in timer mode
bool timerEditIsActive();
void timerEditRotaryInput(int32_t delta); // Encoder adjusts current field
void timerEditButtonPress();              // ENC short: advance field
bool timerEditShouldFlash();
uint8_t timerEditGetIndex();
TimerEditField_t timerEditGetField();
void timerEditGetPreview(uint8_t* hours, uint8_t* minutes, uint8_t* seconds);

// Display helpers
// isElapsed: false => countdown display, true => elapsed-after-zero display
void timerGetDisplay(uint8_t index,
                     uint8_t* hours,
                     uint8_t* minutes,
                     uint8_t* seconds,
                     bool* isElapsed,
                     bool* running,
                     bool* alarm);
