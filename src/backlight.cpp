#include "hardware/backlight.h"
#include <Arduino.h>
#include "features/timer.h"

// ===== Backlight Configuration =====
static const uint16_t kBacklightBlinkHalfPeriodMs = 200;   // 200ms on, 200ms off
static const uint16_t kTimestampBlinkDurationMs    = 2000;  // 2s confirmation blink
static const uint32_t kManualAutoOffMs             = 30000; // 30s manual auto-off

// ===== Module State =====
static uint8_t  s_bluePin                = 0;
static uint8_t  s_redPin                 = 0;
static uint8_t  s_greenPin               = 0;
static uint32_t s_timestampBlinkUntilMs  = 0;
static bool     s_manualOn               = false;
static uint32_t s_manualOnUntilMs        = 0;

static void writeRgb(bool blueOn, bool redOn, bool greenOn) {
  // Low-side AO3400 drivers: drive HIGH to turn channel on.
  digitalWrite(s_bluePin, blueOn ? HIGH : LOW);
  digitalWrite(s_redPin, redOn ? HIGH : LOW);
  digitalWrite(s_greenPin, greenOn ? HIGH : LOW);
}

// ===== Initialize backlight on specified RGB pins =====
void backlightInit(uint8_t bluePin, uint8_t redPin, uint8_t greenPin) {
  s_bluePin = bluePin;
  s_redPin = redPin;
  s_greenPin = greenPin;

  pinMode(s_bluePin, OUTPUT);
  pinMode(s_redPin, OUTPUT);
  pinMode(s_greenPin, OUTPUT);
  writeRgb(false, false, false);  // Start with backlight off
  s_timestampBlinkUntilMs = 0;
}

// ===== Update backlight state with blinking logic =====
void backlightUpdate() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;  // Not initialized

  uint32_t now = millis();
  bool alarmBlink = timerAnyAlarmActive();
  bool timestampBlink = (now < s_timestampBlinkUntilMs);
  
  // Expire manual-on if auto-off time has passed
  if (s_manualOn && now >= s_manualOnUntilMs) {
    s_manualOn = false;
  }

  // Priority: alarm red blink > timestamp green blink > manual blue steady.
  if (alarmBlink) {
    uint32_t blinkCycle = now % (2UL * kBacklightBlinkHalfPeriodMs);
    bool isOn = (blinkCycle < kBacklightBlinkHalfPeriodMs);
    writeRgb(false, isOn, false);
  } else if (timestampBlink) {
    uint32_t blinkCycle = now % (2UL * kBacklightBlinkHalfPeriodMs);
    bool isOn = (blinkCycle < kBacklightBlinkHalfPeriodMs);
    writeRgb(false, false, isOn);
  } else if (s_manualOn) {
    writeRgb(true, false, false);
  } else {
    writeRgb(false, false, false);
  }
}

// ===== Trigger timestamp blink (call when timestamp is captured) =====
void backlightTriggerTimestamp() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;  // Not initialized
  uint32_t now = millis();
  s_timestampBlinkUntilMs = now + kTimestampBlinkDurationMs;
}

// ===== Toggle manual backlight on/off (with 30s auto-off when turned on) =====
void backlightToggle() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;
  if (s_manualOn) {
    s_manualOn = false;
  } else {
    s_manualOn = true;
    s_manualOnUntilMs = millis() + kManualAutoOffMs;
  }
}

// ===== Query if backlight is currently active =====
bool backlightIsActive() {
  uint32_t now = millis();
  return (s_manualOn || timerAnyAlarmActive() || now < s_timestampBlinkUntilMs);
}
