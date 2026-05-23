#pragma once

/**
 * @file config.h
 * @brief Central configuration for all pins, LCD, GPS, and system constants
 *
 * This file contains all hardware-specific settings:
 * - Pin assignments (buttons, encoders, displays)
 * - LCD (ST7036) configuration
 * - GPS serial parameters
 * - Display update timing
 * - Mode definitions
 */

// ===== LCD Configuration (ST7036 I2C) =====
#define LCD_ADDR 0x3C
#define LCD_ROWS 2
#define LCD_COLS 20
#define LCD_BUF_SIZE (LCD_COLS + 1)

// ===== LCD Commands (ST7036) =====
#define LCD_CMD_FUNC_SET 0x38
#define LCD_CMD_FUNC_SET_EXT 0x39
#define LCD_CMD_INTERNAL_OSC 0x14
#define LCD_CMD_CONTRAST_MAX (0x70 | 0x0F)
#define LCD_CMD_POWER_CONTRAST 0x56
#define LCD_CMD_FOLLOWER 0x6C
#define LCD_CMD_DISPLAY_ON 0x0C
#define LCD_CMD_CLEAR 0x01
#define LCD_ADDR_ROW0 0x80
#define LCD_ADDR_ROW1 0xC0

// ===== LCD Timing (ms) =====
#define LCD_INIT_DELAY_1 50
#define LCD_INIT_DELAY_2 200
#define LCD_CLEAR_DELAY 2
#define LCD_RESET_PULSE_MS 2
#define LCD_RESET_READY_MS 10

// ===== I2C Communication =====
#define LCD_I2C_CMD_MODE 0x00
#define LCD_I2C_DATA_MODE 0x40

// ===== GPIO Pin Assignments =====
// Encoder & buttons
#define PIN_ENC_A PIN_PE0
#define PIN_ENC_B PIN_PE1
#define PIN_ENC_BTN PIN_PD4
#define PIN_BTN_LEFT PIN_PD5
#define PIN_BTN_RIGHT PIN_PD6
#define PIN_BTN_TOP PIN_PD3

// Outputs
#define PIN_GPS_POWER PIN_PB0    // Active LOW (AO3407 high-side GPS power)
#define PIN_BUZZER PIN_PB1
#define PIN_LCD_RESET PIN_PB2
#define PIN_BACKLIGHT_RED PIN_PC1
#define PIN_BACKLIGHT_GREEN PIN_PC2
#define PIN_BACKLIGHT_BLUE PIN_PC3
#define PIN_GPS_ENABLE PIN_PD7   // GPS module EN pin
#define PIN_GPS_PPS PIN_PD2

// ===== ADC Pin Assignments =====
#define PIN_BATTERY PIN_PC0

// ===== GPS Serial =====
#define GPS_BAUD 9600
#define GPS_UART_ENABLED 1

// ===== Encoder Quadrature =====
#define ENC_DIVISOR 2  // Count per half-step

// ===== Display Refresh =====
#define DISPLAY_UPDATE_MS 200

// ===== Time Display Constants =====
#define MAX_AGE_MS 1000000UL
#define MAX_AGE_DISPLAY 9999

// ===== UI Mode Definitions =====
#define MODE_UTC_ONLY 0
#define MODE_UTC_LOCAL 1
#define MODE_TIMESTAMP_REVIEW 2
#define MODE_STOPWATCH 3
#define MODE_TIMER 4
#define MODE_LOCAL_ONLY 5
#define MODE_GPS_INFO 6
#define NUM_MODES 7
