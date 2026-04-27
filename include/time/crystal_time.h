#pragma once

#include <stdint.h>

// Initialize asynchronous Timer2 from 32.768 kHz crystal with 1 Hz overflow.
void crystalTimeInit(void);

// Monotonic second counter from Timer2 overflow ISR.
uint32_t crystalTimeGetSeconds(void);

// Monotonic 1/256-second ticks (seconds*256 + TCNT2).
uint32_t crystalTimeGetTicks256(void);
