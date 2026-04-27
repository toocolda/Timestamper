#pragma once

#include <stdint.h>

// Timer1-based buzzer driver on OC1A (ATmega328P pin 9).
// Keeps Timer2 free for asynchronous 32.768 kHz crystal timekeeping.
void buzzerInit(uint8_t pin);
void buzzerStart(uint16_t frequencyHz);
void buzzerStop(void);
