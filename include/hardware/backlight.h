#pragma once

/**
 * @file backlight.h
 * @brief RGB LCD backlight control with smart blinking patterns
 *
 * Manages RGB backlight on/off with two blinking triggers:
 * - Timer alarm active: red 200ms blink pattern
 * - Timestamp capture: green 2-second blink pattern
 * - Manual toggle: blue steady-on (30s auto-off)
 */

#include <stdint.h>

/**
 * Initialize RGB backlight control on specified GPIO pins
 * @param bluePin Blue channel pin
 * @param redPin Red channel pin
 * @param greenPin Green channel pin
 */
void backlightInit(uint8_t bluePin, uint8_t redPin, uint8_t greenPin);

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
