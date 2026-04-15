#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include "st7036.h"
#include "config.h"

// ===== LCD =====
ST7036 lcd(LCD_ADDR);

// ===== GPS =====
TinyGPSPlus gps;

// ===== Pins =====
#define ENC_A PIN_ENC_A
#define ENC_B PIN_ENC_B
#define ENC_BTN PIN_ENC_BTN
#define BTN_LEFT PIN_BTN_LEFT
#define BTN_RIGHT PIN_BTN_RIGHT

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

// ===== GPS Validation =====
bool isGPSTimeReliable() {
  if (!gps.time.isValid() || !gps.date.isValid()) {
    return false;
  }
  
  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();
  
  // Sanity check: year should be reasonable (>= 2020)
  if (year < 2020) return false;
  
  // Month should be 1-12
  if (month < 1 || month > 12) return false;
  
  // Day should be 1-31
  if (day < 1 || day > 31) return false;
  
  return true;
}

// ===== Setup =====
void setup() {
  Wire.begin();
  lcd.begin();

  Serial.begin(GPS_BAUD);

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
  static bool lastTimeValid = false;

  if (millis() - lastUpdate > DISPLAY_UPDATE_MS) {
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

    if (age_ms > MAX_AGE_MS) {
      age_s = MAX_AGE_DISPLAY;
    } else {
      age_s = age_ms / 1000;
      if (age_s > MAX_AGE_DISPLAY) age_s = MAX_AGE_DISPLAY;
    }

    // ===== Encoder =====
    int32_t count;
    noInterrupts();
    count = encoderCount;
    interrupts();
    count /= ENC_DIVISOR;

    // ===== Buttons =====
    bool L = !digitalRead(BTN_LEFT);
    bool R = !digitalRead(BTN_RIGHT);
    bool P = !digitalRead(ENC_BTN);

    // ===== GPS State =====
    char gpsState[3];
    if (!gps.time.isValid()) {
      strcpy(gpsState, "NO");
    }
    else if (gps.location.isValid()) {
      strcpy(gpsState, "FX");
    }
    else if (gps.satellites.value() > 0) {
      strcpy(gpsState, "AC");
    }
    else {
      strcpy(gpsState, "TI");
    }

    // ===== Line 1: Date + Time =====
    bool timeValid = isGPSTimeReliable();
    
    if (s != lastSec || sat != lastSat || timeValid != lastTimeValid) {
      lcd.setCursor(0, 0);

      char buf[LCD_BUF_SIZE];
      
      if (timeValid) {
        int y = gps.date.year();
        int mo = gps.date.month();
        int d = gps.date.day();
        snprintf(buf, LCD_BUF_SIZE,
                 "%04d-%02d-%02d %02d:%02d:%02dZ",
                 y, mo, d, h, m, s);
      } else {
        snprintf(buf, LCD_BUF_SIZE,
                 "YYYY-MM-DD HH:MM:SSZ");
      }

      lcd.print(buf);

      lastSec = s;
      lastSat = sat;
    }

    // ===== Line 2: GPS Status + Satellites + Battery =====
    if (count != lastCount || age_s != lastAge || L != lastL || R != lastR || P != lastP || timeValid != lastTimeValid) {

      lcd.setCursor(0, 1);

      char buf2[LCD_BUF_SIZE];
      snprintf(buf2, LCD_BUF_SIZE,
               "GPS:%s SAT:%02d BAT:XX",
               gpsState,
               sat);

      lcd.print(buf2);

      lastCount = count;
      lastAge = age_s;
      lastL = L;
      lastR = R;
      lastP = P;
      lastTimeValid = timeValid;
    }
  }
}