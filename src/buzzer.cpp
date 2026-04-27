#include <Arduino.h>
#include "hardware/buzzer.h"

static uint8_t s_buzzerPin = 255;

static uint16_t computeOcr1a(uint16_t frequencyHz, uint16_t prescaler) {
  if (frequencyHz == 0 || prescaler == 0) return 0xFFFF;

  // Timer1 CTC toggle: f = F_CPU / (2 * prescaler * (1 + OCR1A))
  uint32_t denom = 2UL * (uint32_t)prescaler * (uint32_t)frequencyHz;
  if (denom == 0) return 0xFFFF;

  uint32_t rounded = (F_CPU + (denom / 2UL)) / denom;
  if (rounded == 0) rounded = 1;

  uint32_t ocr = rounded - 1UL;
  if (ocr > 0xFFFFUL) return 0xFFFF;
  return (uint16_t)ocr;
}

void buzzerInit(uint8_t pin) {
  s_buzzerPin = pin;
  pinMode(s_buzzerPin, OUTPUT);
  digitalWrite(s_buzzerPin, LOW);
  buzzerStop();
}

void buzzerStart(uint16_t frequencyHz) {
  if (s_buzzerPin == 255 || frequencyHz == 0) {
    buzzerStop();
    return;
  }

  uint8_t csBits = 0;
  uint16_t ocr = computeOcr1a(frequencyHz, 1);

  if (ocr <= 0xFFFF) {
    csBits = _BV(CS10);
  }
  if (ocr > 0xFFFF - 1) {
    ocr = computeOcr1a(frequencyHz, 8);
    csBits = _BV(CS11);
  }
  if (ocr > 0xFFFF - 1) {
    ocr = computeOcr1a(frequencyHz, 64);
    csBits = _BV(CS11) | _BV(CS10);
  }
  if (ocr > 0xFFFF - 1) {
    ocr = computeOcr1a(frequencyHz, 256);
    csBits = _BV(CS12);
  }
  if (ocr > 0xFFFF - 1) {
    ocr = computeOcr1a(frequencyHz, 1024);
    csBits = _BV(CS12) | _BV(CS10);
  }

  noInterrupts();
  // CTC mode with OCR1A top, toggle OC1A on compare match.
  TCCR1A = _BV(COM1A0);
  TCCR1B = _BV(WGM12) | csBits;
  OCR1A = ocr;
  interrupts();
}

void buzzerStop(void) {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  interrupts();

  if (s_buzzerPin != 255) {
    digitalWrite(s_buzzerPin, LOW);
  }
}
