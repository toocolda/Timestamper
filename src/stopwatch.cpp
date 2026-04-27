#include <Arduino.h>
#include "features/stopwatch.h"
#include "time/crystal_time.h"

// 99:59:59.9 in 0.1s units
static const uint32_t kMaxTenths = 3599999UL;
static const uint32_t kMaxTicks256 = ((kMaxTenths * 256UL) + 9UL) / 10UL;

struct StopwatchState {
  bool running;
  uint32_t accumulatedTicks256;
  uint32_t runStartTicks256;
};

static StopwatchState g_sw = {false, 0, 0};

static uint32_t stopwatchTicksToTenths(uint32_t ticks256, bool running) {
  uint32_t tenthsTimes256 = ticks256 * 10UL;

  // Hybrid smoothing: while running, round to nearest tenth for a steadier
  // visual cadence on the LCD. When stopped, use floor so stored elapsed time
  // never over-reports.
  uint32_t tenths = running ? ((tenthsTimes256 + 128UL) / 256UL)
                            : (tenthsTimes256 / 256UL);
  if (tenths > kMaxTenths) tenths = kMaxTenths;
  return tenths;
}

static uint32_t stopwatchComputeTenths(uint8_t index) {
  (void)index;

  if (!g_sw.running) {
    return stopwatchTicksToTenths(g_sw.accumulatedTicks256, false);
  }

  uint32_t nowTicks = crystalTimeGetTicks256();
  uint32_t elapsedTicks = nowTicks - g_sw.runStartTicks256;
  uint32_t totalTicks = g_sw.accumulatedTicks256 + elapsedTicks;
  uint32_t totalTenths = stopwatchTicksToTenths(totalTicks, true);

  // Saturate and auto-stop at max range.
  if (totalTicks >= kMaxTicks256 || totalTenths >= kMaxTenths) {
    g_sw.accumulatedTicks256 = kMaxTicks256;
    g_sw.running = false;
    return kMaxTenths;
  }

  return totalTenths;
}

void stopwatchStartStopToggle(uint8_t index) {
  (void)index;

  if (g_sw.running) {
    uint32_t nowTicks = crystalTimeGetTicks256();
    uint32_t elapsedTicks = nowTicks - g_sw.runStartTicks256;
    uint32_t totalTicks = g_sw.accumulatedTicks256 + elapsedTicks;
    if (totalTicks >= kMaxTicks256) totalTicks = kMaxTicks256;
    g_sw.accumulatedTicks256 = totalTicks;
    g_sw.running = false;
    return;
  }

  if (g_sw.accumulatedTicks256 >= kMaxTicks256) {
    return;  // Already maxed; must reset before running again.
  }

  g_sw.runStartTicks256 = crystalTimeGetTicks256();
  g_sw.running = true;
}

void stopwatchReset(uint8_t index) {
  (void)index;
  g_sw.running = false;
  g_sw.accumulatedTicks256 = 0;
  g_sw.runStartTicks256 = 0;
}

bool stopwatchIsRunning(uint8_t index) {
  (void)index;
  return g_sw.running;
}

bool stopwatchAnyRunning() {
  return g_sw.running;
}

uint32_t stopwatchGetTenths(uint8_t index) {
  return stopwatchComputeTenths(index);
}

void stopwatchGetDisplay(uint8_t index, uint8_t* hours, uint8_t* minutes, uint8_t* seconds, uint8_t* tenths) {
  uint32_t totalTenths = stopwatchGetTenths(index);

  if (hours) {
    *hours = (uint8_t)(totalTenths / 36000UL);
  }
  if (minutes) {
    *minutes = (uint8_t)((totalTenths % 36000UL) / 600UL);
  }
  if (seconds) {
    *seconds = (uint8_t)((totalTenths % 600UL) / 10UL);
  }
  if (tenths) {
    *tenths = (uint8_t)(totalTenths % 10UL);
  }
}

uint8_t stopwatchGetSelected() {
  return 0;
}

void stopwatchToggleSelected() {
  // Single stopwatch mode: no secondary channel to select.
}
