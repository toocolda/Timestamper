#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include "st7036.h"

// ===== LCD =====
ST7036 lcd(0x3C);

// ===== GPS =====
TinyGPSPlus gps;

// ===== Pins =====
#define ENC_A 2
#define ENC_B 3
#define ENC_BTN 4
#define BTN_LEFT 5
#define BTN_RIGHT 6

// ===== Encoder =====
volatile int32_t encoderCount = 0;
volatile uint8_t lastState = 0;

const int8_t enc_table[16] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

void handleEncoder() {
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  uint8_t index = (lastState << 2) | state;

  encoderCount += enc_table[index];
  lastState = state;
}

// ===== Setup =====
void setup() {
  Wire.begin();
  lcd.begin();

  Serial.begin(9600);  // GPS

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  lastState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);

  attachInterrupt(digitalPinToInterrupt(ENC_A), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), handleEncoder, CHANGE);

  lcd.setCursor(0, 0);
  lcd.print("Init...");
}

// ===== Loop =====
void loop() {
  // ===== Read GPS =====
  while (Serial.available()) {
    gps.encode(Serial.read());
  }

  static uint32_t lastUpdate = 0;

  // Cache last values (for no-flicker updates)
  static int lastSec = -1;
  static int lastSat = -1;
  static int32_t lastCount = -9999;
  static uint32_t lastAge = 9999;
  static bool lastL = false, lastR = false, lastP = false;

  if (millis() - lastUpdate > 200) {
    lastUpdate = millis();

    // ===== GPS data =====
    bool fix = gps.location.isValid();
    int sat = gps.satellites.value();

    int h = gps.time.hour();
    int m = gps.time.minute();
    int s = gps.time.second();

    // ===== Age =====
    uint32_t age_ms = gps.location.age();
    uint32_t age_s;

    if (age_ms > 1000000UL) {
      age_s = 9999;
    } else {
      age_s = age_ms / 1000;
      if (age_s > 9999) age_s = 9999;
    }

    // ===== Encoder =====
    int32_t count;
    noInterrupts();
    count = encoderCount;
    interrupts();
    count /= 2;

    // ===== Buttons =====
    bool L = !digitalRead(BTN_LEFT);
    bool R = !digitalRead(BTN_RIGHT);
    bool P = !digitalRead(ENC_BTN);

    // ===== Line 1: Time + Satellites =====
    if (s != lastSec || sat != lastSat) {
      lcd.setCursor(0, 0);

      char buf[21];
      snprintf(buf, sizeof(buf),
               "%s %02d:%02d:%02d S:%02d  ",
               fix ? "F" : "N",
               h, m, s, sat);

      lcd.print(buf);

      lastSec = s;
      lastSat = sat;
    }

    // ===== Line 2: Encoder + Age + Buttons =====
    if (count != lastCount || age_s != lastAge || L != lastL || R != lastR || P != lastP) {

      lcd.setCursor(0, 1);

      char buf2[21];
      snprintf(buf2, sizeof(buf2),
               "C:%-5ld A:%04lu %c%c%c   ",
               count,
               age_s,
               L ? 'L' : ' ',
               R ? 'R' : ' ',
               P ? 'P' : ' ');

      lcd.print(buf2);

      lastCount = count;
      lastAge = age_s;
      lastL = L;
      lastR = R;
      lastP = P;
    }
  }
}