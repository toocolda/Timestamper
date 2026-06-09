#include <Arduino.h>
#include <avr/io.h>
#include "hardware/buzzer.h"

static uint8_t s_buzzerPin = 255;
static const uint8_t kBuzzerDutyPercent = 18;

static void buzzerSetTimer1Enabled(bool enabled) {
#if POWER_GATE_TIMER1_WITH_BUZZER
#if defined(PRR)
#if defined(PRTIM1)
  if (enabled) PRR &= (uint8_t)~_BV(PRTIM1);
  else PRR |= _BV(PRTIM1);
#endif
#elif defined(PRR0)
#if defined(PRTIM1)
  if (enabled) PRR0 &= (uint8_t)~_BV(PRTIM1);
  else PRR0 |= _BV(PRTIM1);
#endif
#endif
#else
  (void)enabled;
#endif
}

static uint16_t computeTop(uint16_t frequencyHz, uint16_t prescaler) {
  if (frequencyHz == 0 || prescaler == 0) return 0xFFFF;

  // Timer1 phase/frequency-correct PWM: f = F_CPU / (2 * prescaler * TOP)
  uint32_t denom = 2UL * (uint32_t)prescaler * (uint32_t)frequencyHz;
  if (denom == 0) return 0xFFFF;

  uint32_t top = (F_CPU + (denom / 2UL)) / denom;
  if (top < 2UL) top = 2UL;

  if (top > 0xFFFFUL) return 0xFFFF;
  return (uint16_t)top;
}

void buzzerInit(uint8_t pin) {
  s_buzzerPin = pin;
  pinMode(s_buzzerPin, OUTPUT);
  digitalWrite(s_buzzerPin, LOW);
  buzzerStop();
}

void buzzerStart(uint16_t frequencyHz) {
  buzzerStartWithDuty(frequencyHz, kBuzzerDutyPercent);
}

void buzzerStartWithDuty(uint16_t frequencyHz, uint8_t dutyPercent) {
  if (s_buzzerPin == 255 || frequencyHz == 0) {
    buzzerStop();
    return;
  }

  buzzerSetTimer1Enabled(true);

  if (dutyPercent < 1U) dutyPercent = 1U;
  if (dutyPercent > 95U) dutyPercent = 95U;

  uint8_t csBits = 0;
  uint16_t top = computeTop(frequencyHz, 1);

  if (top <= 0xFFFF) {
    csBits = _BV(CS10);
  }
  if (top > 0xFFFF - 1) {
    top = computeTop(frequencyHz, 8);
    csBits = _BV(CS11);
  }
  if (top > 0xFFFF - 1) {
    top = computeTop(frequencyHz, 64);
    csBits = _BV(CS11) | _BV(CS10);
  }
  if (top > 0xFFFF - 1) {
    top = computeTop(frequencyHz, 256);
    csBits = _BV(CS12);
  }
  if (top > 0xFFFF - 1) {
    top = computeTop(frequencyHz, 1024);
    csBits = _BV(CS12) | _BV(CS10);
  }

  uint32_t compare = ((uint32_t)top * (uint32_t)dutyPercent) / 100UL;
  if (compare < 1UL) compare = 1UL;
  if (compare >= top) compare = (uint32_t)top - 1UL;

  noInterrupts();
  // Phase/frequency-correct PWM (mode 8), TOP=ICR1, output on OC1A.
  TCCR1A = _BV(COM1A1);
  TCCR1B = _BV(WGM13) | csBits;
  ICR1 = top;
  OCR1A = (uint16_t)compare;
  interrupts();
}

void buzzerStop(void) {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  interrupts();

  buzzerSetTimer1Enabled(false);

  if (s_buzzerPin != 255) {
    digitalWrite(s_buzzerPin, LOW);
  }
}
