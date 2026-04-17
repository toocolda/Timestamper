#include "battery.h"
#include <Arduino.h>

// ===== Battery Configuration =====
// Voltage divider: 27K (positive) + 100K (to GND) = 127K total
// V_out = V_in * (100K / 127K) ≈ V_in * 0.787
// 18650 battery range: 2.5V (0%) to 4.2V (100%)
#define BATTERY_UPDATE_MS 1000  // Update every 1 second (no need to update too often)
#define V_MIN_MV 2500           // Minimum voltage in mV (0%)
#define V_MAX_MV 4200           // Maximum voltage in mV (100%)
#define DIVIDER_RATIO (127.0 / 100.0)  // Voltage divider ratio correction

// ===== Module State =====
static uint8_t g_batteryPin = 0;
static uint32_t g_lastBatteryUpdate = 0;
static uint8_t g_batteryPercent = 0;

// ===== Initialize battery monitoring on specified ADC pin =====
void batteryInit(uint8_t adcPin) {
  g_batteryPin = adcPin;
  pinMode(adcPin, INPUT);
  g_lastBatteryUpdate = millis() - BATTERY_UPDATE_MS;  // Force immediate first read
  batteryUpdate();  // Initial read
}

// ===== Update battery percentage (internally throttled to 1 second) =====
void batteryUpdate() {
  if (g_batteryPin == 0) return;  // Not initialized
  
  uint32_t now = millis();
  if (now - g_lastBatteryUpdate < BATTERY_UPDATE_MS) {
    return;  // Not time to update yet
  }
  g_lastBatteryUpdate = now;
  
  // Read ADC (0-1023)
  uint16_t adcValue = analogRead(g_batteryPin);
  
  // Convert ADC to voltage at A0 (in mV)
  // ADC range 0-1023 maps to 0-5000mV
  uint16_t v_out_mv = (uint32_t)adcValue * 5000UL / 1023UL;
  
  // Correct for voltage divider to get actual battery voltage (in mV)
  // V_in = V_out * (127/100)
  uint16_t v_battery_mv = (uint32_t)v_out_mv * 127UL / 100UL;
  
  // Convert voltage to percentage (0-100)
  // percent = (V - V_min) / (V_max - V_min) * 100
  if (v_battery_mv <= V_MIN_MV) {
    g_batteryPercent = 0;
  } else if (v_battery_mv >= V_MAX_MV) {
    g_batteryPercent = 99;  // Cap at 99 (2-digit display space)
  } else {
    uint16_t v_range = V_MAX_MV - V_MIN_MV;  // 1700 mV
    uint16_t v_offset = v_battery_mv - V_MIN_MV;
    g_batteryPercent = (uint8_t)((100UL * (uint32_t)v_offset) / (uint32_t)v_range);
    if (g_batteryPercent > 99) g_batteryPercent = 99;
  }
}

// ===== Get current battery percentage =====
uint8_t batteryGetPercentage() {
  return g_batteryPercent;
}
