#include "backlight.h"
#include <Arduino.h>
#include "timer_mode.h"

// ===== Backlight Configuration =====
#define BACKLIGHT_BLINK_MS 200  // Half-period of blink (200ms on, 200ms off = 400ms cycle)
#define TIMESTAMP_BLINK_DURATION_MS 2000  // Blink for 2 seconds after timestamp

// ===== Module State =====
static uint8_t g_backlightPin = 0;
static uint32_t g_timestampBlinkEndTime = 0;

// ===== Private Helper: Check if timer alarm is active =====
extern bool timerAnyAlarmActive();

// ===== Initialize backlight on specified pin =====
void backlightInit(uint8_t pin) {
  g_backlightPin = pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);  // Start with backlight off
  g_timestampBlinkEndTime = 0;
}

// ===== Update backlight state with blinking logic =====
void backlightUpdate() {
  if (g_backlightPin == 0) return;  // Not initialized

  uint32_t now = millis();
  bool shouldBlink = false;
  
  // Check if timer alarm is active
  if (timerAnyAlarmActive()) {
    shouldBlink = true;
  }
  
  // Check if we're still in timestamp blink window
  if (now < g_timestampBlinkEndTime) {
    shouldBlink = true;
  }
  
  // Apply blink pattern if we should blink
  if (shouldBlink) {
    uint32_t blinkCycle = now % (2 * BACKLIGHT_BLINK_MS);
    bool isOn = (blinkCycle < BACKLIGHT_BLINK_MS);
    digitalWrite(g_backlightPin, isOn ? HIGH : LOW);
  } else {
    // Not blinking, turn off
    digitalWrite(g_backlightPin, LOW);
  }
}

// ===== Trigger timestamp blink (call when timestamp is captured) =====
void backlightTriggerTimestamp() {
  if (g_backlightPin == 0) return;  // Not initialized
  uint32_t now = millis();
  g_timestampBlinkEndTime = now + TIMESTAMP_BLINK_DURATION_MS;
}

// ===== Query if backlight is currently active =====
bool backlightIsActive() {
  uint32_t now = millis();
  return (timerAnyAlarmActive() || now < g_timestampBlinkEndTime);
}
