#pragma once

/**
 * @file power_sleep.h
 * @brief Low-power policy: early peripheral gating, ADC mode policy, and the
 *        desk-mode deep sleep / non-desk idle sleep loop handling.
 */

#include <stdint.h>

// One-time early hardware power setup in setup(): watchdog off, analog
// comparator off, SPI power-gate, and pull-ups on otherwise-floating pins.
void powerEarlyInit(void);

// Enable/disable the battery ADC based on the current mode (UTC-only uses it).
void powerAdcApplyModePolicy(void);

// End-of-loop sleep handling (desk-mode deep sleep + non-desk idle sleep).
void powerSleepUpdate(void);

// Desk-mode wake bookkeeping, called from input sources / event handling.
void powerDeskNoteWake(void);                 // an input edge occurred
void powerDeskNoteButtonActivity(void);       // keep awake for button debounce
void powerDeskNoteTimestampFeedback(void);    // keep awake for capture feedback
