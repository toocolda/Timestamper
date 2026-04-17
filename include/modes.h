#pragma once

#include <stdint.h>
#include "buttons.h"

// ===== Mode Display Function Declarations =====
void displayModeUTCOnly();
void displayModeUTCLocal();
void displayModeTimestampReview();
void displayModeStopwatch();
void displayModeTimer();
void displayModeLocalOnly();
void updateDisplay(uint8_t mode);
void buzzOnce(uint16_t durationMs);
void handleModeEvent(uint8_t mode, ButtonEvent_t event);
bool timestampModeIsScrollActive();
void timestampModeScrollBy(int32_t delta);

// ===== External globals =====
extern uint8_t g_currentMode;
extern uint32_t g_modeEpoch;
