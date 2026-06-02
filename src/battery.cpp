#include "hardware/battery.h"
#include <Arduino.h>
#include "time/crystal_time.h"

// ===== Battery Configuration =====
// Voltage divider: 27K (positive) + 100K (to GND) = 127K total
// V_out = V_in * (100K / 127K) ≈ V_in * 0.787
static const uint16_t kBatteryUpdatePeriodMs = 1000;

struct BatteryCurvePoint {
  uint16_t mv;
  uint8_t pct;
};

// 3xAAA alkaline pack (series) discharge approximation under light/moderate load.
// Tuned so ~3.3V is effectively empty for this project.
static const BatteryCurvePoint kBatteryCurve[] = {
  {3300, 0},
  {3400, 3},
  {3500, 8},
  {3600, 16},
  {3700, 28},
  {3800, 42},
  {3900, 58},
  {4000, 74},
  {4200, 90},
  {4500, 99}
};

static uint8_t batteryPercentFromMv(uint16_t batteryMv) {
  const uint8_t pointCount = (uint8_t)(sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]));
  if (batteryMv <= kBatteryCurve[0].mv) {
    return kBatteryCurve[0].pct;
  }
  if (batteryMv >= kBatteryCurve[pointCount - 1].mv) {
    return kBatteryCurve[pointCount - 1].pct;
  }

  for (uint8_t i = 1; i < pointCount; i++) {
    uint16_t mvHi = kBatteryCurve[i].mv;
    if (batteryMv > mvHi) continue;

    uint16_t mvLo = kBatteryCurve[i - 1].mv;
    uint8_t pctLo = kBatteryCurve[i - 1].pct;
    uint8_t pctHi = kBatteryCurve[i].pct;

    uint16_t spanMv = mvHi - mvLo;
    uint16_t posMv = batteryMv - mvLo;
    uint16_t spanPct = (uint16_t)pctHi - (uint16_t)pctLo;
    uint16_t interp = (uint16_t)((((uint32_t)posMv * (uint32_t)spanPct) + (spanMv / 2U)) / spanMv);
    uint8_t pct = (uint8_t)((uint16_t)pctLo + interp);
    if (pct > 99U) pct = 99U;
    return pct;
  }

  return 0;
}

// ===== Module State =====
static uint8_t s_batteryAdcPin = 0;
static uint32_t s_lastBatterySampleMs = 0;
static uint8_t s_batteryPercent = 0;

// ===== Initialize battery monitoring on specified ADC pin =====
void batteryInit(uint8_t adcPin) {
  s_batteryAdcPin = adcPin;
  pinMode(adcPin, INPUT);
  // Force immediate first read without waiting for the first period.
  s_lastBatterySampleMs = crystalTimeGetMillis() - kBatteryUpdatePeriodMs;
  batteryUpdate();  // Initial read
}

// ===== Update battery percentage (internally throttled to 1 second) =====
void batteryUpdate() {
  if (s_batteryAdcPin == 0) return;  // Not initialized
  
  uint32_t now = crystalTimeGetMillis();
  if (now - s_lastBatterySampleMs < kBatteryUpdatePeriodMs) {
    return;  // Not time to update yet
  }
  s_lastBatterySampleMs = now;
  
  // Read ADC (0-1023)
  uint16_t adcValue = analogRead(s_batteryAdcPin);
  
  // Convert ADC to voltage at A0 (in mV)
  // ADC range 0-1023 maps to 0-5000mV
  uint16_t sensedMv = (uint32_t)adcValue * 5000UL / 1023UL;
  
  // Correct for voltage divider to get actual battery voltage (in mV)
  // V_in = V_out * (127/100)
  uint16_t batteryMv = (uint32_t)sensedMv * 127UL / 100UL;
  
  // Convert battery voltage using a nonlinear 3xAAA curve and clamp to 0..99 for 2-digit UI.
  s_batteryPercent = batteryPercentFromMv(batteryMv);
}

// ===== Get current battery percentage =====
uint8_t batteryGetPercentage() {
  return s_batteryPercent;
}
