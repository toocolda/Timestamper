#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "hardware/buzzer.h"

static uint8_t s_buzzerPin = 255;
static const uint8_t kBuzzerDutyPercent = 18;
static BuzzerModeSetting s_buzzerMode = BUZZER_MODE_ALL;

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

// Timer1 CTC mode: f = F_CPU / (prescaler * (OCR1A + 1))
// Returns OCR1A value for the desired frequency.
static uint16_t computeTop(uint16_t frequencyHz, uint16_t prescaler) {
  if (frequencyHz == 0 || prescaler == 0) return 0xFFFF;

  uint32_t denom = (uint32_t)prescaler * (uint32_t)frequencyHz;
  // Round to nearest, then subtract 1 for OCR1A register value.
  uint32_t top = (F_CPU + (denom / 2UL)) / denom;
  if (top < 2UL) top = 2UL;
  top -= 1UL;  // OCR1A = F_CPU/(prescaler*freq) - 1

  if (top > 0xFFFFUL) return 0xFFFF;
  return (uint16_t)top;
}

// Timer1 CTC ISRs: software-toggle the buzzer pin on PD7 (no hardware PWM output).
// COMPA fires when the counter resets (start of new period) → pin HIGH.
// COMPB fires at the duty-cycle cutoff → pin LOW.
ISR(TIMER1_COMPA_vect) {
  if (s_buzzerPin != 255) digitalWrite(s_buzzerPin, HIGH);
}

ISR(TIMER1_COMPB_vect) {
  if (s_buzzerPin != 255) digitalWrite(s_buzzerPin, LOW);
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

  if (top < 0xFFFF) {
    csBits = _BV(CS10);
  }
  if (top >= 0xFFFF) {
    top = computeTop(frequencyHz, 8);
    csBits = _BV(CS11);
  }
  if (top >= 0xFFFF) {
    top = computeTop(frequencyHz, 64);
    csBits = _BV(CS11) | _BV(CS10);
  }
  if (top >= 0xFFFF) {
    top = computeTop(frequencyHz, 256);
    csBits = _BV(CS12);
  }
  if (top >= 0xFFFF) {
    top = computeTop(frequencyHz, 1024);
    csBits = _BV(CS12) | _BV(CS10);
  }

  uint32_t compare = ((uint32_t)top * (uint32_t)dutyPercent) / 100UL;
  if (compare < 1UL) compare = 1UL;
  if (compare >= (uint32_t)top) compare = (uint32_t)top - 1UL;

  noInterrupts();
  // CTC mode (WGM12), no hardware PWM outputs — pin toggled in ISRs above.
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | csBits;
  OCR1A = top;
  OCR1B = (uint16_t)compare;
  TIMSK1 = _BV(OCIE1A) | _BV(OCIE1B);
  interrupts();
}

void buzzerStop(void) {
  noInterrupts();
  TIMSK1 = 0;
  TCCR1A = 0;
  TCCR1B = 0;
  interrupts();

  buzzerSetTimer1Enabled(false);

  if (s_buzzerPin != 255) {
    digitalWrite(s_buzzerPin, LOW);
  }
}

void buzzerSetMode(BuzzerModeSetting mode) {
  if (mode >= BUZZER_MODE_COUNT) return;
  if (mode != s_buzzerMode) {
    buzzerStop();
  }
  s_buzzerMode = mode;
}

BuzzerModeSetting buzzerGetMode(void) {
  return s_buzzerMode;
}

bool buzzerAllowsUi(void) {
  return s_buzzerMode == BUZZER_MODE_ALL;
}

bool buzzerAllowsAlarm(void) {
  return s_buzzerMode == BUZZER_MODE_ALARMS_ONLY || s_buzzerMode == BUZZER_MODE_ALL;
}
