#pragma once

#include <stdint.h>
#include "time_edit.h"  // For TimeEdit_t structure

// ===== MCU Time Tracking =====
// Maintains independent MCU clock that ticks based on elapsed milliseconds
// Can be synced from GPS or manually set time
// Handles date/time advancement with full calendar logic

void mcuTimeSync(TimeEdit_t* timeData);      // Sync MCU clock to a known time
TimeEdit_t mcuTimeGetCurrent();              // Get current time with elapsed calculation
bool shouldSkipGPSSync();                    // Check if manual time grace period is active

// ===== Manual Time Storage =====
void setManualTime(TimeEdit_t* timeData);    // Store manually-set time
TimeEdit_t getManualTime();                  // Retrieve manually-set time
bool hasManualTime();                        // Check if manual time is available
