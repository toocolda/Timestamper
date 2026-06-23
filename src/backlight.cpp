#include "hardware/backlight.h"
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "features/timer.h"
#include "time/crystal_time.h"

// ===== Backlight Configuration =====
static const uint16_t kTimestampBlinkHalfPeriodMs = 200;   // 200ms on, 200ms off
static const uint16_t kTimestampBlinkDurationMs   = 2000;  // 2s confirmation blink
static const uint8_t  kBlueFadeLevels             = 24;     // Blue fade steps (0..24)
static const uint16_t kBlueFadeStepMs             = 10;     // ~240ms full fade-in/out

// Blue channel software PWM on Timer3 (a spare 16-bit timer on the 328PB).
// PB0 has no hardware PWM output, so two compare-match ISRs toggle the pin like
// the buzzer does on Timer1. Driving the PWM from a timer ISR makes brightness
// independent of main-loop timing, so long tasks (LCD I2C, GPS) can no longer
// stretch a duty phase and cause visible flicker. CTC mode, TOP = OCR3A.
#if defined(TCCR3A) && defined(TCCR3B) && defined(OCR3A) && defined(OCR3B) && defined(TIMSK3)
#define BLUE_PWM_TIMER3 1
static const uint16_t kBluePwmTop = 2499;  // 1 MHz / (2499+1) = 400 Hz carrier
#else
#define BLUE_PWM_TIMER3 0
#endif

// ===== Module State =====
static uint8_t  s_bluePin                = 0;
static uint8_t  s_redPin                 = 0;
static uint8_t  s_greenPin               = 0;
static uint32_t s_timestampBlinkStartMs  = 0;
static bool     s_timestampBlinkActive   = false;
static bool     s_manualOn               = false;
static uint32_t s_manualOnStartMs        = 0;
static uint32_t s_manualAutoOffMs        = 30000;

// Blue fade engine state.
static uint8_t  s_blueLevel              = 0;   // Current brightness step
static uint8_t  s_blueTarget             = 0;   // Target brightness step
static uint32_t s_blueFadeLastStepMs     = 0;

static void redGreenWrite(bool redOn, bool greenOn) {
  // Low-side AO3400 drivers: drive HIGH to turn channel on.
  digitalWrite(s_redPin, redOn ? HIGH : LOW);
  digitalWrite(s_greenPin, greenOn ? HIGH : LOW);
}

#if BLUE_PWM_TIMER3
static bool s_blueTimerOn = false;

static void blueTimer3SetPower(bool on) {
#if defined(PRR1) && defined(PRTIM3)
  if (on) PRR1 &= (uint8_t)~_BV(PRTIM3);
  else    PRR1 |= _BV(PRTIM3);
#else
  (void)on;
#endif
}

static void blueTimer3Stop() {
  if (!s_blueTimerOn) return;
  noInterrupts();
  TIMSK3 = 0;
  TCCR3A = 0;
  TCCR3B = 0;
  interrupts();
  blueTimer3SetPower(false);
  s_blueTimerOn = false;
}

static void blueTimer3SetDuty(uint16_t dutyTicks) {
  if (s_blueTimerOn) {
    // OCR3B is NOT double-buffered in CTC mode: a write lands immediately. When
    // dimming we lower the duty, and if TCNT3 has already passed the new value
    // the COMPB "turn-off" match won't fire again until next period, stranding
    // the pin HIGH for a whole period (a bright flash on a dimming ramp). Snap
    // the pin to the level it should hold for the current counter position so
    // neither raising nor lowering the duty produces a one-period glitch.
    noInterrupts();
    OCR3B = dutyTicks;
    if (TCNT3 < dutyTicks) {
      digitalWrite(s_bluePin, HIGH);
    } else {
      digitalWrite(s_bluePin, LOW);
    }
    interrupts();
    return;
  }
  blueTimer3SetPower(true);
  noInterrupts();
  TCCR3A = 0;                        // CTC, no hardware compare-output pins.
  TCCR3B = _BV(WGM32) | _BV(CS31);   // CTC (TOP = OCR3A), prescaler 8 => 1 MHz.
  OCR3A = kBluePwmTop;
  OCR3B = dutyTicks;
  TCNT3 = 0;
  TIMSK3 = _BV(OCIE3A) | _BV(OCIE3B);
  interrupts();
  s_blueTimerOn = true;
}

ISR(TIMER3_COMPA_vect) {  // Counter wrapped to a new period -> blue LED on.
  digitalWrite(s_bluePin, HIGH);
}

ISR(TIMER3_COMPB_vect) {  // Reached the duty cutoff -> blue LED off.
  digitalWrite(s_bluePin, LOW);
}
#endif  // BLUE_PWM_TIMER3

// Apply a blue brightness step. 0 = off, full = solid on (timer idle), and any
// intermediate step runs the Timer3 PWM. Steady states keep the timer stopped
// so it costs nothing when not fading and behaves correctly across sleep.
static void blueApplyLevel(uint8_t level) {
  if (level == 0) {
#if BLUE_PWM_TIMER3
    blueTimer3Stop();
#endif
    digitalWrite(s_bluePin, LOW);
    return;
  }
  if (level >= kBlueFadeLevels) {
#if BLUE_PWM_TIMER3
    blueTimer3Stop();
#endif
    digitalWrite(s_bluePin, HIGH);  // Solid full brightness.
    return;
  }
#if BLUE_PWM_TIMER3
  // Gamma (~2.0) correction: duty = level^2 so the perceived fade is even.
  uint32_t duty = ((uint32_t)level * (uint32_t)level * (uint32_t)(kBluePwmTop + 1U))
                  / ((uint32_t)kBlueFadeLevels * (uint32_t)kBlueFadeLevels);
  if (duty < 1UL) duty = 1UL;
  if (duty >= (uint32_t)kBluePwmTop) duty = (uint32_t)kBluePwmTop - 1UL;
  blueTimer3SetDuty((uint16_t)duty);
#else
  digitalWrite(s_bluePin, HIGH);  // No spare timer: snap on without a fade.
#endif
}

// ===== Initialize backlight on specified RGB pins =====
void backlightInit(uint8_t bluePin, uint8_t redPin, uint8_t greenPin) {
  s_bluePin = bluePin;
  s_redPin = redPin;
  s_greenPin = greenPin;

  pinMode(s_bluePin, OUTPUT);
  pinMode(s_redPin, OUTPUT);
  pinMode(s_greenPin, OUTPUT);
  redGreenWrite(false, false);  // Start with red/green off
  s_blueLevel = 0;
  s_blueTarget = 0;
  blueApplyLevel(0);            // Blue off, timer idle
  s_timestampBlinkStartMs = 0;
  s_timestampBlinkActive = false;
  s_blueFadeLastStepMs = crystalTimeGetMillis();
}

// ===== Update backlight state with blinking logic =====
void backlightUpdate() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;  // Not initialized

  uint32_t now = crystalTimeGetMillis();
  bool alarmBlink = timerAnyAlarmActive();
  bool alarmOn = timerAlarmToneOn();
  bool timestampBlink = false;

  if (s_timestampBlinkActive) {
    if (crystalTimeElapsedMs(s_timestampBlinkStartMs, kTimestampBlinkDurationMs)) {
      s_timestampBlinkActive = false;
    } else {
      timestampBlink = true;
    }
  }
  
  // Expire manual-on if auto-off time has passed (wrap-safe elapsed check)
  if (s_manualOn && s_manualAutoOffMs != 0 &&
      crystalTimeElapsedMs(s_manualOnStartMs, s_manualAutoOffMs)) {
    s_manualOn = false;
  }

  // Priority: alarm red blink > timestamp green blink > manual blue fade.
  bool redOn = false;
  bool greenOn = false;
  bool blueWanted = false;

  if (alarmBlink) {
    redOn = alarmOn;
  } else if (timestampBlink) {
    uint32_t blinkCycle = (now - s_timestampBlinkStartMs) % (2UL * kTimestampBlinkHalfPeriodMs);
    greenOn = (blinkCycle < kTimestampBlinkHalfPeriodMs);
  } else if (s_manualOn) {
    blueWanted = true;
  }

  // Red/green are crisp on/off blinks (never PWM'd) so the alarm and timestamp
  // signals stay unambiguous regardless of the blue fade engine.
  redGreenWrite(redOn, greenOn);

  // Blue fades smoothly in/out via the Timer3 PWM, but snaps fully off the
  // instant a higher-priority red/green blink takes over so they stay clean.
  if (alarmBlink || timestampBlink) {
    s_blueTarget = 0;
    if (s_blueLevel != 0) {
      s_blueLevel = 0;
      blueApplyLevel(0);
    }
    s_blueFadeLastStepMs = now;
  } else {
    s_blueTarget = blueWanted ? kBlueFadeLevels : 0;
    if ((uint32_t)(now - s_blueFadeLastStepMs) >= kBlueFadeStepMs) {
      s_blueFadeLastStepMs = now;
      if (s_blueLevel < s_blueTarget) {
        s_blueLevel++;
        blueApplyLevel(s_blueLevel);
      } else if (s_blueLevel > s_blueTarget) {
        s_blueLevel--;
        blueApplyLevel(s_blueLevel);
      }
    }
  }
}

// ===== Trigger timestamp blink (call when timestamp is captured) =====
void backlightTriggerTimestamp() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;  // Not initialized
  s_timestampBlinkStartMs = crystalTimeGetMillis();
  s_timestampBlinkActive = true;
}

// ===== Toggle manual backlight on/off (with 30s auto-off when turned on) =====
void backlightToggle() {
  if (s_bluePin == 0 || s_redPin == 0 || s_greenPin == 0) return;
  if (s_manualOn) {
    s_manualOn = false;
  } else {
    s_manualOn = true;
    s_manualOnStartMs = crystalTimeGetMillis();
  }
}

void backlightSetManualTimeoutMs(uint32_t timeoutMs) {
  s_manualAutoOffMs = timeoutMs;
  if (s_manualOn) {
    s_manualOnStartMs = crystalTimeGetMillis();
  }
}

uint32_t backlightGetManualTimeoutMs(void) {
  return s_manualAutoOffMs;
}

// ===== Query if backlight is currently active =====
bool backlightIsActive() {
  return (s_manualOn || s_blueLevel > 0 || timerAnyAlarmActive() || s_timestampBlinkActive);
}

// ===== Query if the blue fade is mid-transition =====
// True while the fade engine still needs frequent (10ms) updates to step the
// blue level toward its target. Desk-mode deep sleep stops Timer3 and only
// wakes at 1Hz, which would strand the PWM pin and stretch the fade into a
// visible per-second blink, so sleep must wait until the fade settles.
bool backlightFadeTransitionActive() {
  return (s_blueLevel != s_blueTarget);
}
