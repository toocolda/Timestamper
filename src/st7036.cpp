#include "st7036.h"

ST7036::ST7036(uint8_t addr) : _addr(addr) {}

void ST7036::cmd(uint8_t c) {
    Wire.beginTransmission(_addr);
    Wire.write(0x00); // command mode
    Wire.write(c);
    Wire.endTransmission();
}

void ST7036::data(uint8_t d) {
    Wire.beginTransmission(_addr);
    Wire.write(0x40); // data mode
    Wire.write(d);
    Wire.endTransmission();
}

void ST7036::begin() {
    delay(50);

    cmd(0x38); // function set
    cmd(0x39); // function set (extended)
    cmd(0x14); // internal osc
    cmd(0x70 | 0x0F);  // max contrast
    cmd(0x56); // power/contrast
    cmd(0x6C); // follower control
    delay(200);

    cmd(0x38); // function set
    cmd(0x0C); // display ON
    cmd(0x01); // clear
    delay(2);
}

void ST7036::clear() {
    cmd(0x01);
    delay(2);
}

void ST7036::setCursor(uint8_t col, uint8_t row) {
    uint8_t addr = (row == 0) ? 0x80 : 0xC0;
    cmd(addr + col);
}

void ST7036::print(const char* str) {
    while (*str) {
        data(*str++);
    }
}