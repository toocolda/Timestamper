#pragma once

#include <stdint.h>

// Initialize backlight control on the specified pin
void backlightInit(uint8_t pin);

// Update backlight blink state (call frequently from main loop)
void backlightUpdate();

// Trigger backlight blink on timestamp capture
void backlightTriggerTimestamp();

// Query if backlight should blink (for testing/debug)
bool backlightIsActive();
