#pragma once

/**
 * @file time_datatypes.h
 * @brief Shared time data structures and types
 *
 * Common definitions used across all time-related modules
 */

#include <stdint.h>

// ===== Time Structure =====
/**
 * Universal time representation
 * Used for: current time, edited time, stored timestamps
 */
typedef struct {
  uint16_t year;     // 2000-2255
  uint8_t month;     // 1-12
  uint8_t day;       // 1-31
  uint8_t hour;      // 0-23 (24-hour)
  uint8_t minute;    // 0-59
  uint8_t second;    // 0-59
} TimeEdit_t;

// ===== Edit Field Enumeration =====
/**
 * Field being edited in time picker
 */
typedef enum {
  EDIT_FIELD_NONE = 0,
  EDIT_FIELD_YEAR,
  EDIT_FIELD_MONTH,
  EDIT_FIELD_DAY,
  EDIT_FIELD_HOUR,
  EDIT_FIELD_MINUTE,
  EDIT_FIELD_SECOND,
  EDIT_FIELD_DONE
} EditField_t;

/**
 * Validate date components
 * @param year 4-digit year
 * @param month 1-12
 * @param day 1-31
 * @return true if valid date
 */
bool isValidDate(uint16_t year, uint8_t month, uint8_t day);
