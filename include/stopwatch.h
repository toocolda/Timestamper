#pragma once

#include <stdint.h>

// ===== Stopwatch (Background Runner) =====
// Resolution: 0.1s
// Range: 00:00:00.0 .. 99:59:59.9

// Index: 0 = SW1, 1 = SW2
void stopwatchStartStopToggle(uint8_t index);
void stopwatchReset(uint8_t index);
bool stopwatchIsRunning(uint8_t index);

// Returns true if at least one stopwatch is running.
bool stopwatchAnyRunning();

// Returns current elapsed time in tenths of a second.
uint32_t stopwatchGetTenths(uint8_t index);

// Convenience split for display formatting.
void stopwatchGetDisplay(uint8_t index, uint8_t* hours, uint8_t* minutes, uint8_t* seconds, uint8_t* tenths);

// Selected stopwatch for UI control in Stopwatch mode.
uint8_t stopwatchGetSelected();
void stopwatchToggleSelected();
