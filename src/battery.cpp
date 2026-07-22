#include "hardware/battery.h"
#include <Arduino.h>
#include <avr/io.h>
#include "time/crystal_time.h"
#include "core/config.h"

// ===== Battery Configuration =====
// Voltage divider: 1M (positive) + 1M (to GND) = 2M total
// V_out = V_in * (1M / 2M) = V_in * 0.5
// Battery state changes slowly enough that 15 s updates are effectively
// real-time while reducing repeated ADC/reference wakeups in UTC mode.
static const uint16_t kBatteryUpdatePeriodMs = 15000;

#if BATTERY_MEASURE_VIA_VCC
static uint16_t batteryReadVccMv() {
  uint8_t admuxPrev = ADMUX;
#if defined(ADCSRB)
  uint8_t adcsrbPrev = ADCSRB;
#endif

  // Measure internal 1.1V bandgap using AVcc as reference.
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#if defined(ADCSRB) && defined(MUX5)
  ADCSRB &= (uint8_t)~_BV(MUX5);
#endif
  delay(2);

  ADCSRA |= _BV(ADSC);
  while ((ADCSRA & _BV(ADSC)) != 0) {
  }

  uint16_t adc = ADC;

  ADMUX = admuxPrev;
#if defined(ADCSRB)
  ADCSRB = adcsrbPrev;
#endif

  if (adc == 0) return 0;
  // 1.1V * 1023 * 1000 = 1125300.
  return (uint16_t)(1125300UL / (uint32_t)adc);
}
#endif

// ===== Module State =====
static uint8_t s_batteryAdcPin = 0;
static uint32_t s_lastBatterySampleMs = 0;
static BatteryLevel_t s_batteryLevel = BATTERY_LEVEL_NONE;

static BatteryLevel_t batteryLevelFromMv(uint16_t batteryMv, BatteryLevel_t prev) {
  // USB-only power (no battery pack) leaves the divider input near 0 mV.
  // Use hysteresis so intermittent noise does not flap between NO and LOW.
  static const uint16_t kNoBatteryEnterMv = 1500U;
  static const uint16_t kNoBatteryExitMv = 1650U;
  if (prev == BATTERY_LEVEL_NONE) {
    if (batteryMv < kNoBatteryExitMv) return BATTERY_LEVEL_NONE;
  } else {
    if (batteryMv < kNoBatteryEnterMv) return BATTERY_LEVEL_NONE;
  }

  // 3S NiMH nominal: ~3.6V plateau. Use hysteresis to prevent flicker near thresholds.
  switch (prev) {
    case BATTERY_LEVEL_NONE:
      // Re-enter from NO only after hysteresis is satisfied above.
      return BATTERY_LEVEL_LOW;
    case BATTERY_LEVEL_HIGH:
      if (batteryMv < 3690U) return BATTERY_LEVEL_MID;
      return BATTERY_LEVEL_HIGH;
    case BATTERY_LEVEL_MID:
      if (batteryMv >= 3750U) return BATTERY_LEVEL_HIGH;
      if (batteryMv < 3480U) return BATTERY_LEVEL_LOW;
      return BATTERY_LEVEL_MID;
    case BATTERY_LEVEL_LOW:
    default:
      if (batteryMv >= 3540U) return BATTERY_LEVEL_MID;
      return BATTERY_LEVEL_LOW;
  }
}

// ===== Initialize battery monitoring on specified ADC pin =====
void batteryInit(uint8_t adcPin) {
  s_batteryAdcPin = adcPin;
  pinMode(adcPin, INPUT);
#if defined(DIDR0) && defined(ADC0D)
  // Disable digital input buffer on ADC0 (PIN_PC0) to reduce analog pin leakage.
  DIDR0 |= _BV(ADC0D);
#endif
  // Force immediate first read without waiting for the first period.
  s_lastBatterySampleMs = crystalTimeGetMillis() - kBatteryUpdatePeriodMs;
  batteryUpdate();  // Initial read
}

// ===== Update battery state (internally throttled to 1 second) =====
void batteryUpdate() {
  if (s_batteryAdcPin == 0) return;  // Not initialized

  uint32_t now = crystalTimeGetMillis();
  if ((uint32_t)(now - s_lastBatterySampleMs) < kBatteryUpdatePeriodMs) {
    return;  // Not time to update yet
  }
  s_lastBatterySampleMs = now;

  // ADC may be power-gated by mode policy; re-enable before sampling.
#if defined(PRR)
#if defined(PRADC)
  PRR &= (uint8_t)~_BV(PRADC);
#endif
#elif defined(PRR0)
#if defined(PRADC)
  PRR0 &= (uint8_t)~_BV(PRADC);
#endif
#endif
  ADCSRA |= _BV(ADEN);

  uint16_t batteryMv = 0;

#if BATTERY_MEASURE_VIA_VCC
  // Pack directly powers MCU: AVcc tracks battery, so estimate AVcc directly.
  batteryMv = batteryReadVccMv();
#else
  // Divider mode: AVcc is regulated at a known voltage, so use DEFAULT reference.
  analogReference(DEFAULT);
  delay(2);

  // Throw away first sample after reference switch, then take a smoothed read.
  (void)analogRead(s_batteryAdcPin);
  uint16_t adcValue = 0;
  adcValue += analogRead(s_batteryAdcPin);
  adcValue += analogRead(s_batteryAdcPin);
  adcValue += analogRead(s_batteryAdcPin);
  adcValue += analogRead(s_batteryAdcPin);
  adcValue = (uint16_t)((adcValue + 2U) / 4U);

  // Convert ADC to voltage at A0 (in mV) using regulated AVcc reference.
  uint16_t sensedMv = (uint16_t)(((uint32_t)adcValue * (uint32_t)BATTERY_ADC_REF_MV + 511UL) / 1023UL);

  // Correct for voltage divider to get actual battery voltage (in mV)
  // V_in = V_out * 2 for a 1M/1M divider.
  batteryMv = (uint16_t)((uint32_t)sensedMv * 2UL);
#endif

  s_batteryLevel = batteryLevelFromMv(batteryMv, s_batteryLevel);
}

BatteryLevel_t batteryGetLevel() {
  return s_batteryLevel;
}
