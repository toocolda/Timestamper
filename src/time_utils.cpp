#include "time/time_utils.h"
#include "time/crystal_time.h"

bool timeIsLeapYear(uint16_t year) {
  return (year % 400U == 0U) || ((year % 4U == 0U) && (year % 100U != 0U));
}

uint8_t timeDaysInMonth(uint16_t year, uint8_t month) {
  switch (month) {
    case 2:
      return timeIsLeapYear(year) ? 29U : 28U;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30U;
    default:
      return 31U;
  }
}

bool timeFlashToggle(uint32_t* lastToggleMs, bool* showState, uint16_t intervalMs) {
  if (lastToggleMs == nullptr || showState == nullptr) {
    return true;
  }

  if (crystalTimeElapsedMs(*lastToggleMs, intervalMs)) {
    *showState = !(*showState);
    *lastToggleMs = crystalTimeGetMillis();
  }

  return *showState;
}