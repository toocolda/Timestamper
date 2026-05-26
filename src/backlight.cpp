#include "hardware/backlight.h"
#include <Arduino.h>
#include "features/timer.h"
#include "time/crystal_time.h"

// ===== Backlight Configuration =====
static const uint16_t kTimestampBlinkHalfPeriodMs = 200;   // 200ms on, 200ms off
static const uint16_t kTimestampBlinkDurationMs   = 2000;  // 2s confirmation blink
static const uint32_t kManualAutoOffMs            = 30000; // 30s manual auto-off
static const uint8_t  kBluePwmLevels              = 4;      // Software PWM levels (0..4)
static const uint16_t kBlueFadeStepMs             = 16;     // ~160ms full fade

enum BacklightColor : uint8_t {
  BL_COLOR_NONE = 0,
  BL_COLOR_BLUE,
  BL_COLOR_RED,
  BL_COLOR_GREEN
};

// ===== Module State =====
static uint8_t  s_bluePin                = 0;
static uint8_t  s_redPin                 = 0;
static uint8_t  s_greenPin               = 0;
static uint32_t s_timestampBlinkStartMs  = 0;
static uint32_t s_timestampBlinkUntilMs  = 0;
static bool     s_manualOn               = false;
static uint32_t s_manualOnUntilMs        = 0;

static BacklightColor s_colorCurrent     = BL_COLOR_NONE;
static uint8_t  s_level                  = 0;
static uint8_t  s_targetLevel            = 0;
static uint32_t s_fadeLastStepMs         = 0;

static void writeRgb(bool blueOn, bool redOn, bool greenOn) {
  // Low-side AO3400 drivers: drive HIGH to turn channel on.
  digitalWrite(s_bluePin, blueOn ? HIGH : LOW);
  digitalWrite(s_redPin, redOn ? HIGH : LOW);
  digitalWrite(s_greenPin, greenOn ? HIGH : LOW);
}

static bool levelPwmOnNow(uint8_t level) {
  if (level == 0) return false;
  if (level >= kBluePwmLevels) return true;
  uint8_t phase = (uint8_t)(crystalTimeGetTicks256() & (kBluePwmLevels - 1));
  return phase < level;
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
  s_timestampBlinkStartMs = 0;
  s_timestampBlinkUntilMs = 0;
  s_colorCurrent = BL_COLOR_NONE;
  s_level = 0;
  s_targetLevel = 0;
  s_fadeLastStepMs = crystalTimeGetMillis();
}

// ===== Update backlight state with blinking logic =====
void backlightUpdate() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;  // Not initialized

  uint32_t now = crystalTimeGetMillis();
  bool alarmBlink = timerAnyAlarmActive();
  bool alarmOn = timerAlarmToneOn();
  bool timestampBlink = (now < s_timestampBlinkUntilMs);
  
  // Expire manual-on if auto-off time has passed
  if (s_manualOn && now >= s_manualOnUntilMs) {
    s_manualOn = false;
  }

  // Priority: alarm red blink > timestamp green blink > manual blue steady.
  BacklightColor desiredColor = BL_COLOR_NONE;
  bool desiredOn = false;

  if (alarmBlink) {
    desiredColor = BL_COLOR_RED;
    desiredOn = alarmOn;
  } else if (timestampBlink) {
    uint32_t blinkCycle = (now - s_timestampBlinkStartMs) % (2UL * kTimestampBlinkHalfPeriodMs);
    desiredColor = BL_COLOR_GREEN;
    desiredOn = (blinkCycle < kTimestampBlinkHalfPeriodMs);
  } else if (s_manualOn) {
    desiredColor = BL_COLOR_BLUE;
    desiredOn = true;
  }

  s_targetLevel = desiredOn ? kBluePwmLevels : 0;

  if ((uint32_t)(now - s_fadeLastStepMs) >= kBlueFadeStepMs) {
    s_fadeLastStepMs = now;
    if (s_level < s_targetLevel) {
      s_level++;
    } else if (s_level > s_targetLevel) {
      s_level--;
    }
  }

  if (desiredColor != BL_COLOR_NONE) {
    s_colorCurrent = desiredColor;
  } else if (s_level == 0) {
    s_colorCurrent = BL_COLOR_NONE;
  }

  bool on = levelPwmOnNow(s_level);
  writeRgb(on && s_colorCurrent == BL_COLOR_BLUE,
           on && s_colorCurrent == BL_COLOR_RED,
           on && s_colorCurrent == BL_COLOR_GREEN);
}

// ===== Trigger timestamp blink (call when timestamp is captured) =====
void backlightTriggerTimestamp() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;  // Not initialized
  uint32_t now = crystalTimeGetMillis();
  s_timestampBlinkStartMs = now;
  s_timestampBlinkUntilMs = now + kTimestampBlinkDurationMs;
}

// ===== Toggle manual backlight on/off (with 30s auto-off when turned on) =====
void backlightToggle() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;
  if (s_manualOn) {
    s_manualOn = false;
  } else {
    s_manualOn = true;
    s_manualOnUntilMs = crystalTimeGetMillis() + kManualAutoOffMs;
  }
}

// ===== Query if backlight is currently active =====
bool backlightIsActive() {
  uint32_t now = crystalTimeGetMillis();
  return (s_manualOn || s_level > 0 || timerAnyAlarmActive() || now < s_timestampBlinkUntilMs);
}
