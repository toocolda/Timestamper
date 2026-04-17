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
#define PIN_BTN_TOP 7
#define PIN_BUZZER 9
#define PIN_BACKLIGHT 10

// ===== GPS =====
#define GPS_BAUD 9600

// ===== Encoder =====
#define ENC_DIVISOR 2  // Quadrature: count is per half-step

// ===== Display Update =====
#define DISPLAY_UPDATE_MS 200

// ===== Age Calculation =====
#define MAX_AGE_MS 1000000UL  // Max milliseconds before clamping to 9999
#define MAX_AGE_DISPLAY 9999   // Max value to display

// ===== Display Modes =====
#define MODE_UTC_ONLY 0
#define MODE_UTC_LOCAL 1
#define MODE_TIMESTAMP_REVIEW 2
#define MODE_STOPWATCH 3
#define MODE_TIMER 4
#define MODE_LOCAL_ONLY 5
#define NUM_MODES 6

// ===== Mode Names =====
#define MODE_NAME_0 "UTC Only"
#define MODE_NAME_1 "UTC&Local"
#define MODE_NAME_2 "Timestamp"
#define MODE_NAME_3 "Stopwatch"
#define MODE_NAME_4 "Timer"
#define MODE_NAME_5 "Desk Mode"
