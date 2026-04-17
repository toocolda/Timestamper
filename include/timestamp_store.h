#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "time_edit.h"

// Maximum number of timestamp records retained in EEPROM.
#define TIMESTAMP_STORE_MAX 99

void timestampStoreAdd(const TimeEdit_t* stamp);
uint8_t timestampStoreCount();
bool timestampStoreGetByNewest(uint8_t newestIndex, TimeEdit_t* outStamp);
bool timestampStoreDeleteByNewest(uint8_t newestIndex);
void timestampStoreClearAll();
