#include <Arduino.h>
#include "st7036.h"
#include "config.h"
#include <TinyGPS++.h>

// Forward declarations from main.cpp
extern ST7036 lcd;
extern TinyGPSPlus gps;

// ===== Buzzer Pin =====
#define BUZZER PIN_BUZZER

// ===== Global Mode Variables =====
uint8_t g_currentMode = MODE_UTC_ONLY;
uint32_t g_modeEpoch = 0;  // Increments every mode change

// ===== Buzzer Control =====
void buzzOnce(uint16_t durationMs) {
  tone(BUZZER, 1000);  // 1000 Hz tone
  delay(durationMs);
  noTone(BUZZER);
}

// ===== GPS Validation Helper =====
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

// ===== Mode: UTC Only =====
void displayModeUTCOnly() {
  static int lastSec = -1;
  static int lastSat = -1;
  static bool lastTimeValid = false;
  static uint32_t lastEpoch = 0;
  
  // Reset cache if mode changed
  if (lastEpoch != g_modeEpoch) {
    lastSec = -1;
    lastSat = -1;
    lastTimeValid = false;
    lastEpoch = g_modeEpoch;
    lcd.clear();
  }
  
  bool timeValid = isGPSTimeReliable();
  int sat = gps.satellites.value();
  int h = gps.time.hour();
  int m = gps.time.minute();
  int s = gps.time.second();
  
  char gpsStatus[3];
  if (!isGPSTimeReliable()) {
    strcpy(gpsStatus, "NO");
  } else if (gps.location.isValid()) {
    strcpy(gpsStatus, "FX");
  } else if (sat > 0) {
    strcpy(gpsStatus, "AC");
  } else {
    strcpy(gpsStatus, "TI");
  }
  
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
    
    lcd.setCursor(0, 1);
    char buf2[LCD_BUF_SIZE];
    snprintf(buf2, LCD_BUF_SIZE,
             "GPS:%s SAT:%02d BAT:XX",
             gpsStatus, sat);
    lcd.print(buf2);
    
    lastSec = s;
    lastSat = sat;
    lastTimeValid = timeValid;
  }
}

// ===== Mode: UTC & Local =====
void displayModeUTCLocal() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UTC&Local Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Timestamp Review =====
void displayModeTimestampReview() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Timestamp Review");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Stopwatch =====
void displayModeStopwatch() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Stopwatch Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Timer =====
void displayModeTimer() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Timer Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode: Local Only =====
void displayModeLocalOnly() {
  static uint32_t lastEpoch = 0;
  if (lastEpoch != g_modeEpoch) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Local Only Mode");
    lcd.setCursor(0, 1);
    lcd.print("Coming Soon...");
    lastEpoch = g_modeEpoch;
  }
}

// ===== Mode Dispatcher =====
void updateDisplay(uint8_t mode) {
  switch (mode) {
    case MODE_UTC_ONLY:
      displayModeUTCOnly();
      break;
    case MODE_UTC_LOCAL:
      displayModeUTCLocal();
      break;
    case MODE_TIMESTAMP_REVIEW:
      displayModeTimestampReview();
      break;
    case MODE_STOPWATCH:
      displayModeStopwatch();
      break;
    case MODE_TIMER:
      displayModeTimer();
      break;
    case MODE_LOCAL_ONLY:
      displayModeLocalOnly();
      break;
    default:
      break;
  }
}
