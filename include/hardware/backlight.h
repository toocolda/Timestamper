#pragma once

/**
 * @file backlight.h
 * @brief LCD backlight control with smart blinking patterns
 *
 * Manages backlight on/off with two blinking triggers:
 * - Timer alarm active: continuous 200ms blink pattern
 * - Timestamp capture: 2-second blink pattern
 */

#include <stdint.h>

/**
 * Initialize backlight control on specified GPIO pin
 * @param pin Digital output pin (e.g., PIN_BACKLIGHT)
 */
void backlightInit(uint8_t pin);

/**
 * Update backlight state - call every loop iteration
 * Updates blink patterns based on timer alarms and timestamp capture
 */
void backlightUpdate(void);

/**
 * Trigger 2-second backlight blink for timestamp capture feedback
 */
void backlightTriggerTimestamp(void);

/**
 * Toggle backlight on/off manually.
 * When toggled on, auto-off fires after 30 seconds of no further toggle.
 */
void backlightToggle(void);

/**
 * Check if backlight is currently active/blinking
 * @return true if timer alarm or timestamp blink is active
 */
bool backlightIsActive(void);
