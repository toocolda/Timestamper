#pragma once

// ===== Mode Display Function Declarations =====
void displayModeUTCOnly();
void displayModeUTCLocal();
void displayModeTimestampReview();
void displayModeStopwatch();
void displayModeTimer();
void displayModeLocalOnly();
void updateDisplay(uint8_t mode);
void buzzOnce(uint16_t durationMs);

// ===== External globals =====
extern uint8_t g_currentMode;
extern uint32_t g_modeEpoch;
