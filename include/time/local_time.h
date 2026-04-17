#pragma once

/**
 * @file local_time.h
 * @brief Local timezone calculations
 *
 * Converts UTC time to local time using configurable offset:
 * - ±14 hour offset support
 * - UTC to local conversion
 * - Local to UTC conversion (reverse)
 * - Persistent offset storage in EEPROM
 */

#include <stdint.h>
#include "time_datatypes.h"

/**
 * Get current UTC offset in hours
 * @return Offset: -12 to +14, 0 = UTC
 */
int8_t time_getUTCOffset(void);

/**
 * Set UTC offset and save to EEPROM
 * @param offset Hours: -12 to +14
 */
void time_setUTCOffset(int8_t offset);

/**
 * Convert UTC time to local time
 * @param utcTime UTC time struct
 * @return Local time at configured offset
 */
TimeEdit_t time_calculateLocal(const TimeEdit_t* utcTime);

/**
 * Convert UTC time to local with specific offset
 * @param utcTime UTC time
 * @param offset Hours offset (-12 to +14)
 * @return Local time at specified offset
 */
TimeEdit_t time_calculateLocalWithOffset(const TimeEdit_t* utcTime, int8_t offset);

/**
 * Convert local time back to UTC
 * @param localTime Local time
 * @return UTC time
 */
TimeEdit_t time_calculateUTC(const TimeEdit_t* localTime);

/**
 * Get day of week (Sakamoto algorithm)
 * @param year Full 4-digit year
 * @param month Month (1-12)
 * @param day Day (1-31)
 * @return Day of week: 0=Sunday ... 6=Saturday
 */
uint8_t time_getDayOfWeek(uint16_t year, uint8_t month, uint8_t day);

// ===== UTC Offset Editor (for manual UTC offset configuration) =====
/**
 * Enter UTC offset edit mode
 * @param initialOffset Starting offset (-12 to +14)
 */
void offsetEditStart(int8_t initialOffset);

/**
 * Exit UTC offset edit mode
 */
void offsetEditStop(void);

/**
 * Check if offset editing is active
 * @return true if in offset picker
 */
bool offsetEditIsActive(void);

/**
 * Get current edited offset value
 * @return Offset hours (-12 to +14)
 */
int8_t offsetEditGetValue(void);

/**
 * Handle rotary encoder during offset edit
 * @param delta Encoder movement
 */
void offsetEditRotaryInput(int32_t delta);

/**
 * Check if offset should flash (for visibility)
 * @return true if visible (on phase of blink)
 */
bool offsetEditShouldFlash(void);

// ===== Legacy API (current firmware usage) =====
int8_t getUTCOffset(void);
void setUTCOffset(int8_t offset);
TimeEdit_t calculateLocalTime(TimeEdit_t utcTime);
TimeEdit_t calculateLocalTimeWithOffset(TimeEdit_t utcTime, int8_t offset);

typedef enum {
	OFFSET_EDIT_NONE = 0,
	OFFSET_EDIT_ACTIVE,
	OFFSET_EDIT_DONE,
} OffsetEditState_t;

OffsetEditState_t offsetEditGetState(void);
