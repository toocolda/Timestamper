#pragma once

#include <stdint.h>

// Initialize battery monitoring on ADC pin (e.g., A0)
void batteryInit(uint8_t adcPin);

// Update battery percentage (call periodically from loop, internally throttled)
void batteryUpdate();

// Get current battery percentage (0-99)
uint8_t batteryGetPercentage();
