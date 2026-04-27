#include <Arduino.h>
#include "core/config.h"
#include "features/timer.h"
#include "hardware/buzzer.h"
#include "time/crystal_time.h"

static const int32_t kMaxSeconds = 99L * 3600L + 59L * 60L + 59L;
static const int32_t kDefaultPresetSeconds = 0L;  // FUEL TIMER default = 00:00:00
static const uint32_t kAlarmAutoClearMs = 5000;

struct TimerChannel {
  bool running;
  int32_t pausedSignedSeconds;   // >0 countdown remaining, <=0 elapsed magnitude encoded as negative
  int32_t runStartSignedSeconds;
  uint32_t runStartTicks256;
  bool alarmActive;
  bool alarmTriggered;
  uint32_t alarmStartedMs;
};

static TimerChannel g_timer = {false, kDefaultPresetSeconds, kDefaultPresetSeconds, 0, false, false, 0};

// Timer edit state (for selected channel): HH -> MM -> SS
static bool g_timerEditActive = false;
static uint8_t g_timerEditIndex = 0;
static TimerEditField_t g_timerEditField = TIMER_EDIT_NONE;
static int32_t g_timerEditSeconds = 0;
static uint32_t g_timerEditFlashMs = 0;
static bool g_timerEditShow = true;
static const uint32_t kEditFlashIntervalMs = 300;

// Alarm sound pattern (distinct and longer than mode-change buzz)
static bool g_alarmToneOn = false;
static uint32_t g_alarmLastStepMs = 0;
static uint8_t g_alarmStep = 0;

static uint8_t clampIndex(uint8_t index) {
  (void)index;
  return 0;
}

static int32_t clampSignedSeconds(int32_t signedSeconds) {
  if (signedSeconds > kMaxSeconds) return kMaxSeconds;
  if (signedSeconds < -kMaxSeconds) return -kMaxSeconds;
  return signedSeconds;
}

static int32_t timerCurrentSignedSeconds(uint8_t index) {
  (void)index;
  const TimerChannel& ch = g_timer;

  if (!ch.running) {
    return ch.pausedSignedSeconds;
  }

  uint32_t nowTicks = crystalTimeGetTicks256();
  uint32_t elapsedTicks = nowTicks - ch.runStartTicks256;
  int32_t elapsedSec = (int32_t)(elapsedTicks / 256UL);
  int32_t currentSigned = ch.runStartSignedSeconds - elapsedSec;
  return clampSignedSeconds(currentSigned);
}

static void timerAlarmPatternUpdate(bool alarmWanted) {
  if (!alarmWanted) {
    if (g_alarmToneOn) {
      buzzerStop();
      g_alarmToneOn = false;
    }
    g_alarmStep = 0;
    g_alarmLastStepMs = millis();
    return;
  }

  uint32_t now = millis();
  uint32_t stepDurMs = 0;

  switch (g_alarmStep) {
    case 0: stepDurMs = 220; break;  // Tone A
    case 1: stepDurMs = 90;  break;  // silence
    case 2: stepDurMs = 220; break;  // Tone B
    case 3: stepDurMs = 140; break;  // silence
    case 4: stepDurMs = 260; break;  // Tone A long
    case 5: stepDurMs = 420; break;  // gap
    default: stepDurMs = 220; break;
  }

  if (now - g_alarmLastStepMs < stepDurMs) {
    return;
  }

  g_alarmLastStepMs = now;
  g_alarmStep = (uint8_t)((g_alarmStep + 1) % 6);

  // Even steps with tones, odd steps silent.
  if (g_alarmStep == 0 || g_alarmStep == 4) {
    buzzerStart(1400);
    g_alarmToneOn = true;
  } else if (g_alarmStep == 2) {
    buzzerStart(900);
    g_alarmToneOn = true;
  } else {
    buzzerStop();
    g_alarmToneOn = false;
  }
}

static void splitHms(int32_t totalSeconds, uint8_t* h, uint8_t* m, uint8_t* s) {
  if (totalSeconds < 0) totalSeconds = 0;
  if (totalSeconds > kMaxSeconds) totalSeconds = kMaxSeconds;
  if (h) *h = (uint8_t)(totalSeconds / 3600L);
  if (m) *m = (uint8_t)((totalSeconds % 3600L) / 60L);
  if (s) *s = (uint8_t)(totalSeconds % 60L);
}

static int32_t joinHms(uint8_t h, uint8_t m, uint8_t s) {
  int32_t v = (int32_t)h * 3600L + (int32_t)m * 60L + (int32_t)s;
  if (v > kMaxSeconds) v = kMaxSeconds;
  return v;
}

void timerModeUpdate() {
  int32_t nowSigned = timerCurrentSignedSeconds(0);

  // One-shot latch alarm only for countdowns that started above zero.
  if (g_timer.running && g_timer.runStartSignedSeconds > 0 && nowSigned <= 0 && !g_timer.alarmTriggered) {
    g_timer.alarmActive = true;
    g_timer.alarmTriggered = true;
    g_timer.alarmStartedMs = millis();
  }

  // Auto-clear alarm after timeout if user takes no action.
  if (g_timer.alarmActive && (millis() - g_timer.alarmStartedMs >= kAlarmAutoClearMs)) {
    g_timer.alarmActive = false;
  }

  // Auto-stop at max elapsed bound.
  if (g_timer.running && nowSigned <= -kMaxSeconds) {
    g_timer.pausedSignedSeconds = -kMaxSeconds;
    g_timer.running = false;
  }

  timerAlarmPatternUpdate(g_timer.alarmActive);
}

void timerStartStopToggle(uint8_t index) {
  uint8_t i = clampIndex(index);

  // Do not allow start/stop while this channel is being edited.
  if (g_timerEditActive && g_timerEditIndex == i) {
    return;
  }

  if (g_timer.running) {
    g_timer.pausedSignedSeconds = timerCurrentSignedSeconds(i);
    g_timer.running = false;
    return;
  }

  if (g_timer.pausedSignedSeconds > 0) {
    // Starting a new countdown cycle clears prior alarm one-shot state.
    g_timer.alarmTriggered = false;
    g_timer.alarmActive = false;
  }

  g_timer.runStartSignedSeconds = g_timer.pausedSignedSeconds;
  g_timer.runStartTicks256 = crystalTimeGetTicks256();
  g_timer.running = true;
}

void timerReset(uint8_t index) {
  uint8_t i = clampIndex(index);
  if (g_timerEditActive && g_timerEditIndex == i) {
    g_timerEditActive = false;
    g_timerEditField = TIMER_EDIT_NONE;
  }
  g_timer.running = false;
  g_timer.pausedSignedSeconds = kDefaultPresetSeconds;
  g_timer.runStartSignedSeconds = kDefaultPresetSeconds;
  g_timer.runStartTicks256 = 0;
  g_timer.alarmActive = false;
  g_timer.alarmTriggered = false;
  g_timer.alarmStartedMs = 0;
}

bool timerIsRunning(uint8_t index) {
  return g_timer.running;
}

bool timerAnyRunning() {
  return g_timer.running;
}

bool timerAlarmActive(uint8_t index) {
  return g_timer.alarmActive;
}

bool timerAnyAlarmActive() {
  return g_timer.alarmActive;
}

void timerAcknowledgeAllAlarms() {
  g_timer.alarmActive = false;
  timerAlarmPatternUpdate(false);
}

uint8_t timerGetSelected() {
  return 0;
}

void timerToggleSelected() {
  // Single timer mode: no secondary channel to select.
}

void timerEditStart(uint8_t index) {
  uint8_t i = clampIndex(index);
  if (g_timer.running) {
    return;  // Edit only when stopped to keep UX predictable.
  }

  int32_t preset = g_timer.pausedSignedSeconds;
  if (preset < 0) preset = 0;  // If showing elapsed, start edit from 00:00:00.

  g_timerEditActive = true;
  g_timerEditIndex = i;
  g_timerEditField = TIMER_EDIT_HOUR;
  g_timerEditSeconds = preset;
  g_timerEditFlashMs = millis();
  g_timerEditShow = true;
}

bool timerEditIsActive() {
  return g_timerEditActive;
}

void timerEditRotaryInput(int32_t delta) {
  if (!g_timerEditActive || delta == 0) return;

  int direction = (delta > 0) ? 1 : -1;
  uint8_t h = 0, m = 0, s = 0;
  splitHms(g_timerEditSeconds, &h, &m, &s);

  if (g_timerEditField == TIMER_EDIT_HOUR) {
    int16_t nh = (int16_t)h + direction;
    if (nh < 0) nh = 99;
    if (nh > 99) nh = 0;
    h = (uint8_t)nh;
  } else if (g_timerEditField == TIMER_EDIT_MINUTE) {
    int16_t nm = (int16_t)m + direction;
    if (nm < 0) nm = 59;
    if (nm > 59) nm = 0;
    m = (uint8_t)nm;
  } else if (g_timerEditField == TIMER_EDIT_SECOND) {
    int16_t ns = (int16_t)s + direction;
    if (ns < 0) ns = 59;
    if (ns > 59) ns = 0;
    s = (uint8_t)ns;
  }

  g_timerEditSeconds = joinHms(h, m, s);
}

void timerEditButtonPress() {
  if (!g_timerEditActive) return;

  g_timerEditShow = true;
  g_timerEditFlashMs = millis();

  if (g_timerEditField == TIMER_EDIT_HOUR) {
    g_timerEditField = TIMER_EDIT_MINUTE;
    return;
  }
  if (g_timerEditField == TIMER_EDIT_MINUTE) {
    g_timerEditField = TIMER_EDIT_SECOND;
    return;
  }

  // Finish edit on SECOND -> save and auto-start.
  uint8_t i = g_timerEditIndex;
  (void)i;
  g_timer.pausedSignedSeconds = g_timerEditSeconds;
  g_timer.runStartSignedSeconds = g_timerEditSeconds;
  g_timer.runStartTicks256 = crystalTimeGetTicks256();
  g_timer.running = true;
  g_timer.alarmActive = false;
  g_timer.alarmTriggered = false;
  g_timer.alarmStartedMs = 0;

  g_timerEditActive = false;
  g_timerEditField = TIMER_EDIT_NONE;
}

bool timerEditShouldFlash() {
  if (!g_timerEditActive) return true;
  if (millis() - g_timerEditFlashMs > kEditFlashIntervalMs) {
    g_timerEditShow = !g_timerEditShow;
    g_timerEditFlashMs = millis();
  }
  return g_timerEditShow;
}

uint8_t timerEditGetIndex() {
  return g_timerEditIndex;
}

TimerEditField_t timerEditGetField() {
  return g_timerEditField;
}

void timerEditGetPreview(uint8_t* hours, uint8_t* minutes, uint8_t* seconds) {
  splitHms(g_timerEditSeconds, hours, minutes, seconds);
}

void timerGetDisplay(uint8_t index,
                     uint8_t* hours,
                     uint8_t* minutes,
                     uint8_t* seconds,
                     bool* isElapsed,
                     bool* running,
                     bool* alarm) {
  uint8_t i = clampIndex(index);
  int32_t signedSec = timerCurrentSignedSeconds(i);

  bool elapsed = (signedSec <= 0);
  int32_t absSec = elapsed ? -signedSec : signedSec;
  if (absSec > kMaxSeconds) absSec = kMaxSeconds;

  if (hours) {
    *hours = (uint8_t)(absSec / 3600L);
  }
  if (minutes) {
    *minutes = (uint8_t)((absSec % 3600L) / 60L);
  }
  if (seconds) {
    *seconds = (uint8_t)(absSec % 60L);
  }
  if (isElapsed) {
    *isElapsed = elapsed;
  }
  if (running) {
    *running = g_timer.running;
  }
  if (alarm) {
    *alarm = g_timer.alarmActive;
  }
}
