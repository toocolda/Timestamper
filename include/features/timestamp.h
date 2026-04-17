#pragma once

/**
 * @file timestamp.h
 * @brief GPS timestamp capture and storage
 *
 * Stores up to 99 GPS timestamps (when TOP button long-pressed):
 * - EEPROM-backed ring buffer (6 bytes per record)
 * - Auto-wrap oldest record when full
 * - Persistent across power cycles
 * - Browse, delete, and UTC/local view in Review mode
 * - Automatic LED/backlight feedback on capture
 */

#include <stdint.h>
#include "../time/time_datatypes.h"
#define TIMESTAMP_STORE_MAX 99
/**
 * Add new timestamp to storage
 * If already 99 records, overwrites oldest
 * @param stamp Current time to store
 */
void timestampStoreAdd(const TimeEdit_t* stamp);

/**
 * Get total number of stored timestamps
 * @return 0-99
 */
uint8_t timestampStoreCount(void);

/**
 * Retrieve timestamp by newest-first index
 * @param newestIndex 0=newest, count-1=oldest
 * @param out Output time struct
 * @return true if record exists
 */
bool timestampStoreGetByNewest(uint8_t newestIndex, TimeEdit_t* out);

/**
 * Delete timestamp by newest-first index
 * @param newestIndex Record to remove
 * @return true if deletion successful
 */
bool timestampStoreDeleteByNewest(uint8_t newestIndex);

/**
 * Clear all timestamps
 */
void timestampStoreClearAll(void);

// ===== Timestamp Review Mode State =====

/**
 * Check if scroll mode is active in Review
 * @return true if browsing list
 */
bool timestampModeIsScrollActive(void);

/**
 * Scroll selected timestamp position
 * @param delta Positive=newer, negative=older
 */
void timestampModeScrollBy(int32_t delta);
