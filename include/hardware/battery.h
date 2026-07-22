#pragma once

/**
 * @file battery.h
 * @brief Battery voltage monitoring with coarse level detection
 *
 * Reads voltage divider on A0 from a 3xAAA battery pack:
 * - Divider: 1M (positive) + 1M (GND)
 * - Default config measures divider pin using regulated AVcc ADC reference
 * - Optional AVcc estimation mode is controlled by BATTERY_MEASURE_VIA_VCC
 *   in core/config.h; divider-mode reference is BATTERY_ADC_REF_MV
 * - Reports a coarse battery level only
 * - Only reads ADC when in UTC display mode to save power
 */

#include <stdint.h>

typedef enum {
	BATTERY_LEVEL_NONE = 0,
	BATTERY_LEVEL_LOW,
	BATTERY_LEVEL_MID,
	BATTERY_LEVEL_HIGH
} BatteryLevel_t;

/**
 * Initialize battery monitoring on specified ADC pin
 * @param adcPin Analog input pin for battery voltage (e.g., PIN_BATTERY)
 */
void batteryInit(uint8_t adcPin);

/**
 * Update battery state estimate - call every loop
 * Internally throttled to 1-second updates; reads ADC only in UTC mode
 */
void batteryUpdate(void);

/**
 * Get coarse battery state for low-churn UI display.
 * @return BATTERY_LEVEL_LOW, BATTERY_LEVEL_MID, or BATTERY_LEVEL_HIGH
 */
BatteryLevel_t batteryGetLevel(void);
