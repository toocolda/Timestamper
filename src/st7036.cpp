#include "display/st7036.h"

static void st7036SetTwiEnabled(bool enabled) {
#if POWER_GATE_TWI_BETWEEN_LCD_WRITES
#if defined(PRR)
#if defined(PRTWI)
    if (enabled) PRR &= (uint8_t)~_BV(PRTWI);
    else PRR |= _BV(PRTWI);
#endif
#elif defined(PRR0)
#if defined(PRTWI0)
    if (enabled) PRR0 &= (uint8_t)~_BV(PRTWI0);
    else PRR0 |= _BV(PRTWI0);
#elif defined(PRTWI)
    if (enabled) PRR0 &= (uint8_t)~_BV(PRTWI);
    else PRR0 |= _BV(PRTWI);
#endif
#endif
#else
    (void)enabled;
#endif
}

ST7036::ST7036(uint8_t addr) : _addr(addr) {}

void ST7036::busAcquire() {
        st7036SetTwiEnabled(true);
}

void ST7036::busRelease() {
        st7036SetTwiEnabled(false);
}

void ST7036::cmd(uint8_t c) {
        busAcquire();
    Wire.beginTransmission(_addr);
    Wire.write(LCD_I2C_CMD_MODE);
    Wire.write(c);
    Wire.endTransmission();
        busRelease();
}

void ST7036::data(uint8_t d) {
        busAcquire();
    Wire.beginTransmission(_addr);
    Wire.write(LCD_I2C_DATA_MODE);
    Wire.write(d);
    Wire.endTransmission();
        busRelease();
}

void ST7036::begin() {
        busAcquire();
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
    busRelease();
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

void ST7036::setContrast(uint8_t value) {
    // 6-bit contrast (C5..C0). Programmed via the extended instruction set:
    // Contrast Set carries C3..C0; Power/ICON/Contrast carries C5..C4 (Bon=1).
    if (value > 0x3F) value = 0x3F;
    busAcquire();
    cmd(LCD_CMD_FUNC_SET_EXT);  // IS=1: extended instruction set
    cmd(LCD_CMD_CONTRAST_SET_BASE | (value & 0x0F));
    cmd(LCD_CMD_POWER_CONTRAST_BASE | ((value >> 4) & 0x03));
    cmd(LCD_CMD_FUNC_SET);      // IS=0: back to normal instruction set
    busRelease();
}
