#include <Arduino.h>
#include "features/stopwatch.h"

// 99:59:59.9 in 0.1s units
static const uint32_t kMaxTenths = 3599999UL;

struct StopwatchState {
  bool running;
  uint32_t accumulatedTenths;
  uint32_t runStartMs;
};

static StopwatchState g_sw = {false, 0, 0};

static uint32_t stopwatchComputeTenths(uint8_t index) {
  (void)index;

  if (!g_sw.running) {
    return g_sw.accumulatedTenths;
  }

  uint32_t elapsedMs = millis() - g_sw.runStartMs;
  uint32_t elapsedTenths = elapsedMs / 100;
  uint32_t totalTenths = g_sw.accumulatedTenths + elapsedTenths;

  // Saturate and auto-stop at max range.
  if (totalTenths >= kMaxTenths) {
    g_sw.accumulatedTenths = kMaxTenths;
    g_sw.running = false;
    return kMaxTenths;
  }

  return totalTenths;
}

void stopwatchStartStopToggle(uint8_t index) {
  (void)index;

  if (g_sw.running) {
    g_sw.accumulatedTenths = stopwatchComputeTenths(0);
    g_sw.running = false;
    return;
  }

  if (g_sw.accumulatedTenths >= kMaxTenths) {
    return;  // Already maxed; must reset before running again.
  }

  g_sw.runStartMs = millis();
  g_sw.running = true;
}

void stopwatchReset(uint8_t index) {
  (void)index;
  g_sw.running = false;
  g_sw.accumulatedTenths = 0;
  g_sw.runStartMs = 0;
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
