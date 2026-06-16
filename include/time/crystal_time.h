#pragma once

#include <stdint.h>

// Initialize asynchronous Timer2 from 32.768 kHz crystal with 1 Hz overflow.
void crystalTimeInit(void);

// Monotonic second counter from Timer2 overflow ISR.
uint32_t crystalTimeGetSeconds(void);

// Monotonic 1/256-second ticks (seconds*256 + TCNT2).
uint32_t crystalTimeGetTicks256(void);

// Monotonic milliseconds derived from 256 Hz ticks.
uint32_t crystalTimeGetMillis(void);

// Elapsed check equivalent to: (now - since) >= intervalMs, wrap-safe for uint32_t.
bool crystalTimeElapsedMs(uint32_t sinceMs, uint32_t intervalMs);
