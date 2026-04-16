#include <Arduino.h>
#include "stopwatch.h"

// 99:59:59.9 in 0.1s units
static const uint32_t kMaxTenths = 3599999UL;

struct StopwatchState {
  bool running;
  uint32_t accumulatedTenths;
  uint32_t runStartMs;
};

static StopwatchState g_sw[2] = {
  {false, 0, 0},
  {false, 0, 0}
};

static uint8_t g_selected = 0;  // 0 => SW1, 1 => SW2

static uint8_t clampIndex(uint8_t index) {
  return (index > 1) ? 1 : index;
}

static uint32_t stopwatchComputeTenths(uint8_t index) {
  uint8_t i = clampIndex(index);

  if (!g_sw[i].running) {
    return g_sw[i].accumulatedTenths;
  }

  uint32_t elapsedMs = millis() - g_sw[i].runStartMs;
  uint32_t elapsedTenths = elapsedMs / 100;
  uint32_t totalTenths = g_sw[i].accumulatedTenths + elapsedTenths;

  // Saturate and auto-stop at max range.
  if (totalTenths >= kMaxTenths) {
    g_sw[i].accumulatedTenths = kMaxTenths;
    g_sw[i].running = false;
    return kMaxTenths;
  }

  return totalTenths;
}

void stopwatchStartStopToggle(uint8_t index) {
  uint8_t i = clampIndex(index);

  if (g_sw[i].running) {
    g_sw[i].accumulatedTenths = stopwatchComputeTenths(i);
    g_sw[i].running = false;
    return;
  }

  if (g_sw[i].accumulatedTenths >= kMaxTenths) {
    return;  // Already maxed; must reset before running again.
  }

  g_sw[i].runStartMs = millis();
  g_sw[i].running = true;
}

void stopwatchReset(uint8_t index) {
  uint8_t i = clampIndex(index);
  g_sw[i].running = false;
  g_sw[i].accumulatedTenths = 0;
  g_sw[i].runStartMs = 0;
}

bool stopwatchIsRunning(uint8_t index) {
  uint8_t i = clampIndex(index);
  return g_sw[i].running;
}

bool stopwatchAnyRunning() {
  return g_sw[0].running || g_sw[1].running;
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
  return g_selected;
}

void stopwatchToggleSelected() {
  g_selected = (g_selected == 0) ? 1 : 0;
}
