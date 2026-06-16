#include "hardware/battery.h"
#include <Arduino.h>
#include <avr/io.h>
#include "time/crystal_time.h"
#include "core/config.h"

// ===== Battery Configuration =====
// Voltage divider: 100K (positive) + 100K (to GND) = 200K total
// V_out = V_in * (100K / 200K) = V_in * 0.5
// Battery percentage changes slowly enough that 5 s updates are effectively
// real-time to the user while cutting repeated ADC/reference wakeups in UTC mode.
static const uint16_t kBatteryUpdatePeriodMs = 5000;

struct BatteryCurvePoint {
  uint16_t mv;
  uint8_t pct;
};

// 3xAAA alkaline pack (series) discharge approximation under light/moderate load.
// Tuned so ~3.3V is effectively empty for this project.
static const BatteryCurvePoint kBatteryCurve[] = {
  {3000, 0},
  {3200, 5},
  {3400, 10},
  {3500, 15},
  {3600, 16},
  {3700, 28},
  {3800, 42},
  {3900, 58},
  {4000, 74},
  {4200, 90},
  {4500, 99}
};

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
#if defined(DIDR0) && defined(ADC0D)
  // Disable digital input buffer on ADC0 (PIN_PC0) to reduce analog pin leakage.
  DIDR0 |= _BV(ADC0D);
#endif
  // Force immediate first read without waiting for the first period.
  s_lastBatterySampleMs = crystalTimeGetMillis() - kBatteryUpdatePeriodMs;
  batteryUpdate();  // Initial read
}

// ===== Update battery percentage (internally throttled to 1 second) =====
void batteryUpdate() {
  if (s_batteryAdcPin == 0) return;  // Not initialized

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
  
  uint32_t now = crystalTimeGetMillis();
  if (now - s_lastBatterySampleMs < kBatteryUpdatePeriodMs) {
    return;  // Not time to update yet
  }
  s_lastBatterySampleMs = now;
  
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
  // V_in = V_out * 2 for a 100K/100K divider.
  batteryMv = (uint16_t)((uint32_t)sensedMv * 2UL);
#endif
  
  // Convert battery voltage using a nonlinear 3xAAA curve and clamp to 0..99 for 2-digit UI.
  s_batteryPercent = batteryPercentFromMv(batteryMv);
}

// ===== Get current battery percentage =====
uint8_t batteryGetPercentage() {
  return s_batteryPercent;
}
