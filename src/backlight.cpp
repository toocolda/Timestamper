#include "hardware/backlight.h"
#include <Arduino.h>
#include "features/timer.h"

// ===== Backlight Configuration =====
static const uint16_t kBacklightBlinkHalfPeriodMs = 200;   // 200ms on, 200ms off
static const uint16_t kTimestampBlinkDurationMs = 2000;    // 2s confirmation blink

// ===== Module State =====
static uint8_t s_backlightPin = 0;
static uint32_t s_timestampBlinkUntilMs = 0;

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
  
  // Apply blink pattern if we should blink
  if (shouldBlink) {
    uint32_t blinkCycle = now % (2UL * kBacklightBlinkHalfPeriodMs);
    bool isOn = (blinkCycle < kBacklightBlinkHalfPeriodMs);
    digitalWrite(s_backlightPin, isOn ? HIGH : LOW);
  } else {
    // Not blinking, turn off
    digitalWrite(s_backlightPin, LOW);
  }
}

// ===== Trigger timestamp blink (call when timestamp is captured) =====
void backlightTriggerTimestamp() {
  if (s_backlightPin == 0) return;  // Not initialized
  uint32_t now = millis();
  s_timestampBlinkUntilMs = now + kTimestampBlinkDurationMs;
}

// ===== Query if backlight is currently active =====
bool backlightIsActive() {
  uint32_t now = millis();
  return (timerAnyAlarmActive() || now < s_timestampBlinkUntilMs);
}
