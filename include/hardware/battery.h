#pragma once

/**
 * @file battery.h
 * @brief Battery voltage monitoring with percentage calculation
 *
 * Reads voltage divider on A0 from 18650 battery:
 * - Divider: 27K (positive) + 100K (GND)
 * - Updates battery percentage (0-99) once per second
 * - Only reads ADC when in UTC display mode to save power
 */

#include <stdint.h>

/**
 * Initialize battery monitoring on specified ADC pin
 * @param adcPin Analog input pin for battery voltage (e.g., PIN_BATTERY)
 */
void batteryInit(uint8_t adcPin);

/**
 * Update battery percentage estimate - call every loop
 * Internally throttled to 1-second updates; reads ADC only in UTC mode
 */
void batteryUpdate(void);

/**
 * Get current battery percentage (0-99)
 * @return Battery percentage, capped at 99 for 2-digit display
 */
uint8_t batteryGetPercentage(void);
