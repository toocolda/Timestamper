#pragma once
#include <Arduino.h>
#include <Wire.h>

class ST7036 {
public:
    ST7036(uint8_t addr = 0x3C);

    void begin();
    void clear();
    void setCursor(uint8_t col, uint8_t row);
    void print(const char* str);

private:
    uint8_t _addr;

    void cmd(uint8_t c);
    void data(uint8_t d);
};