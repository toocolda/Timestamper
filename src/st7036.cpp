#include "display/st7036.h"

ST7036::ST7036(uint8_t addr) : _addr(addr) {}

void ST7036::cmd(uint8_t c) {
    Wire.beginTransmission(_addr);
    Wire.write(LCD_I2C_CMD_MODE);
    Wire.write(c);
    Wire.endTransmission();
}

void ST7036::data(uint8_t d) {
    Wire.beginTransmission(_addr);
    Wire.write(LCD_I2C_DATA_MODE);
    Wire.write(d);
    Wire.endTransmission();
}

void ST7036::begin() {
    delay(LCD_INIT_DELAY_1);

    cmd(LCD_CMD_FUNC_SET);
    cmd(LCD_CMD_FUNC_SET_EXT);
    cmd(LCD_CMD_INTERNAL_OSC);
    cmd(LCD_CMD_CONTRAST_MAX);
    cmd(LCD_CMD_POWER_CONTRAST);
    cmd(LCD_CMD_FOLLOWER);
    delay(LCD_INIT_DELAY_2);

    cmd(LCD_CMD_FUNC_SET);
    cmd(LCD_CMD_DISPLAY_ON);
    cmd(LCD_CMD_CLEAR);
    delay(LCD_CLEAR_DELAY);
}

void ST7036::clear() {
    cmd(LCD_CMD_CLEAR);
    delay(LCD_CLEAR_DELAY);
}

void ST7036::setCursor(uint8_t col, uint8_t row) {
    uint8_t addr = (row == 0) ? LCD_ADDR_ROW0 : LCD_ADDR_ROW1;
    cmd(addr + col);
}

void ST7036::print(const char* str) {
    while (*str) {
        data(*str++);
    }
}

void ST7036::print(const __FlashStringHelper* str) {
    PGM_P p = reinterpret_cast<PGM_P>(str);
    char c = pgm_read_byte(p++);
    while (c != '\0') {
        data((uint8_t)c);
        c = pgm_read_byte(p++);
    }
}
