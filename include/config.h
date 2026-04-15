#pragma once

// ===== LCD Configuration =====
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

// ===== LCD Timing =====
#define LCD_INIT_DELAY_1 50    // ms
#define LCD_INIT_DELAY_2 200   // ms
#define LCD_CLEAR_DELAY 2      // ms

// ===== I2C =====
#define LCD_I2C_CMD_MODE 0x00
#define LCD_I2C_DATA_MODE 0x40

// ===== GPIO Pins =====
#define PIN_ENC_A 2
#define PIN_ENC_B 3
#define PIN_ENC_BTN 4
#define PIN_BTN_LEFT 5
#define PIN_BTN_RIGHT 6

// ===== GPS =====
#define GPS_BAUD 9600

// ===== Encoder =====
#define ENC_DIVISOR 2  // Quadrature: count is per half-step

// ===== Display Update =====
#define DISPLAY_UPDATE_MS 200

// ===== Age Calculation =====
#define MAX_AGE_MS 1000000UL  // Max milliseconds before clamping to 9999
#define MAX_AGE_DISPLAY 9999   // Max value to display
