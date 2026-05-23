#include <Arduino.h>
#include <avr/interrupt.h>
#include "time/crystal_time.h"

static volatile uint32_t s_crystalSeconds = 0;

ISR(TIMER2_OVF_vect) {
  s_crystalSeconds++;
}

void crystalTimeInit(void) {
  cli();

  ASSR |= _BV(AS2);  // Async Timer2 clock source = external 32.768 kHz crystal
  TCCR2A = 0;        // Normal mode
  TCCR2B = _BV(CS22) | _BV(CS20);  // Prescaler 128 => 256 Hz counter, 1 Hz overflow
  TCNT2 = 0;

  // Wait for async register transfers to complete.
  while (ASSR & (_BV(TCR2AUB) | _BV(TCR2BUB) | _BV(TCN2UB))) {
  }

  TIFR2 = _BV(TOV2);     // Clear stale overflow flag
  TIMSK2 |= _BV(TOIE2);  // Enable overflow interrupt
  s_crystalSeconds = 0;

  sei();
}

uint32_t crystalTimeGetSeconds(void) {
  uint32_t sec;
  noInterrupts();
  sec = s_crystalSeconds;
  interrupts();
  return sec;
}

uint32_t crystalTimeGetTicks256(void) {
  uint32_t sec;
  uint8_t tcnt;
  uint8_t tov;

  noInterrupts();
  sec = s_crystalSeconds;
  tcnt = TCNT2;
  tov = (uint8_t)(TIFR2 & _BV(TOV2));

  // If overflow happened but ISR has not run yet, account for one pending second.
  if (tov != 0 && tcnt < 255U) {
    sec++;
    tcnt = TCNT2;
  }
  interrupts();

  return (sec << 8) | (uint32_t)tcnt;
}

uint32_t crystalTimeGetMillis(void) {
  uint32_t ticks256 = crystalTimeGetTicks256();
  // Convert 1/256 s ticks to ms with truncation for monotonic timing.
  return (ticks256 * 1000UL) / 256UL;
}

bool crystalTimeElapsedMs(uint32_t sinceMs, uint32_t intervalMs) {
  uint32_t nowMs = crystalTimeGetMillis();
  return (uint32_t)(nowMs - sinceMs) >= intervalMs;
}
