#pragma once

/**
 * @file time_edit.h
 * @brief Interactive time picker dialog
 *
 * Allows user to edit time via rotary encoder:
 * - Field-by-field editing (year, month, day, hour, minute, second)
 * - Blinking field indicates current edit target
 * - Encoder adjusts value, buttons navigate fields
 */

#include <stdint.h>
#include "time_datatypes.h"

/**
 * Enter edit mode with initial time
 * @param initialTime Starting time for editor
 */
void timeEditStart(TimeEdit_t* initialTime);

/**
 * Exit edit mode and save changes
 */
void timeEditStop(void);

/**
 * Check if edit mode is currently active
 * @return true if in time picker
 */
bool timeEditIsActive(void);

/**
 * Get current edited time
 * @return Time being edited
 */
TimeEdit_t timeEditGetData(void);

/**
 * Get currently editing field
 * @return Field enumeration (EDIT_FIELD_*)
 */
EditField_t timeEditGetCurrentField(void);

/**
 * Check if field should flash (for visibility during edit)
 * @return true if visible (on phase of blink)
 */
bool timeEditShouldFlash(void);

/**
 * Handle button press while editing
 * Advances to next field
 */
void timeEditButtonPress(void);

/**
 * Handle rotary encoder input while editing
 * @param delta Encoder movement (positive/negative)
 */
void timeEditRotaryInput(int32_t delta);
