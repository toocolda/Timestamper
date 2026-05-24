#include "hardware/battery.h"
#include <Arduino.h>
#include "time/crystal_time.h"

// ===== Battery Configuration =====
// Voltage divider: 27K (positive) + 100K (to GND) = 127K total
// V_out = V_in * (100K / 127K) ≈ V_in * 0.787
// 18650 battery range: 2.5V (0%) to 4.2V (100%)
static const uint16_t kBatteryUpdatePeriodMs = 1000;
static const uint16_t kBatteryMinMv = 2500;
static const uint16_t kBatteryMaxMv = 4200;

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
  
  // Convert battery voltage to percentage and clamp to 0..99 for 2-digit UI.
  if (batteryMv <= kBatteryMinMv) {
    s_batteryPercent = 0;
  } else if (batteryMv >= kBatteryMaxMv) {
    s_batteryPercent = 99;
  } else {
    uint16_t rangeMv = kBatteryMaxMv - kBatteryMinMv;
    uint16_t offsetMv = batteryMv - kBatteryMinMv;
    s_batteryPercent = (uint8_t)((100UL * (uint32_t)offsetMv) / (uint32_t)rangeMv);
    if (s_batteryPercent > 99) s_batteryPercent = 99;
  }
}

// ===== Get current battery percentage =====
uint8_t batteryGetPercentage() {
  return s_batteryPercent;
}
