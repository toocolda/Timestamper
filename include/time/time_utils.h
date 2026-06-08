#pragma once

#include <stdint.h>

bool timeIsLeapYear(uint16_t year);
uint8_t timeDaysInMonth(uint16_t year, uint8_t month);

// Shared UI flash-state toggle helper used by multiple edit modes.
bool timeFlashToggle(uint32_t* lastToggleMs, bool* showState, uint16_t intervalMs);