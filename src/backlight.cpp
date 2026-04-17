#include "hardware/backlight.h"
#include <Arduino.h>
#include "features/timer.h"

// ===== Backlight Configuration =====
static const uint16_t kBacklightBlinkHalfPeriodMs = 200;   // 200ms on, 200ms off
static const uint16_t kTimestampBlinkDurationMs    = 2000;  // 2s confirmation blink
static const uint32_t kManualAutoOffMs             = 30000; // 30s manual auto-off

// ===== Module State =====
static uint8_t  s_backlightPin           = 0;
static uint32_t s_timestampBlinkUntilMs  = 0;
static bool     s_manualOn               = false;
static uint32_t s_manualOnUntilMs        = 0;

// ===== Initialize backlight on specified pin =====
void backlightInit(uint8_t pin) {
  s_backlightPin = pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);  // Start with backlight off
  s_timestampBlinkUntilMs = 0;
}

// ===== Update backlight state with blinking logic =====
void backlightUpdate() {
  if (s_backlightPin == 0) return;  // Not initialized

  uint32_t now = millis();
  bool shouldBlink = false;
  
  // Check if timer alarm is active
  if (timerAnyAlarmActive()) {
    shouldBlink = true;
  }
  
  // Check if we're still in timestamp blink window
  if (now < s_timestampBlinkUntilMs) {
    shouldBlink = true;
  }
  
  // Expire manual-on if auto-off time has passed
  if (s_manualOn && now >= s_manualOnUntilMs) {
    s_manualOn = false;
  }

  // Apply blink pattern if we should blink (alarm / timestamp take priority)
  if (shouldBlink) {
    uint32_t blinkCycle = now % (2UL * kBacklightBlinkHalfPeriodMs);
    bool isOn = (blinkCycle < kBacklightBlinkHalfPeriodMs);
    digitalWrite(s_backlightPin, isOn ? HIGH : LOW);
  } else if (s_manualOn) {
    digitalWrite(s_backlightPin, HIGH);
  } else {
    digitalWrite(s_backlightPin, LOW);
  }
}

// ===== Trigger timestamp blink (call when timestamp is captured) =====
void backlightTriggerTimestamp() {
  if (s_backlightPin == 0) return;  // Not initialized
  uint32_t now = millis();
  s_timestampBlinkUntilMs = now + kTimestampBlinkDurationMs;
}

// ===== Toggle manual backlight on/off (with 30s auto-off when turned on) =====
void backlightToggle() {
  if (s_backlightPin == 0) return;
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
