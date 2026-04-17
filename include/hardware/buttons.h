#pragma once

/**
 * @file buttons.h
 * @brief Button and encoder event handling
 *
 * Manages all user input:
 * - Rotary encoder (mode selection & parameter editing)
 * - 4 push buttons: ENC (encoder), LEFT, RIGHT, TOP
 * - Short-press and long-press detection
 * - Debouncing and edge detection
 */

#include <stdint.h>

// ===== Button Event Types =====
#define BUTTON_NONE 0
#define BUTTON_ENC_SHORT 1
#define BUTTON_ENC_LONG 2
#define BUTTON_LEFT_SHORT 3
#define BUTTON_LEFT_LONG 4
#define BUTTON_RIGHT_SHORT 5
#define BUTTON_RIGHT_LONG 6
#define BUTTON_TOP_SHORT 7
#define BUTTON_TOP_LONG 8

typedef uint8_t ButtonEvent_t;

/**
 * Initialize button/encoder hardware and interrupts
 */
void initButtons(void);

/**
 * Poll buttons for events - call every loop
 * @return Button event code (BUTTON_* defines) or BUTTON_NONE
 */
ButtonEvent_t handleButtons(void);
