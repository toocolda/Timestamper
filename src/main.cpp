#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include "display/st7036.h"
#include "core/config.h"
#include "core/modes.h"
#include "hardware/buttons.h"
#include "time/time_edit.h"
#include "time/mcu_time.h"
#include "time/crystal_time.h"
#include "time/local_time.h"
#include "features/stopwatch.h"
#include "features/timer.h"
#include "hardware/backlight.h"
#include "hardware/battery.h"
#include "hardware/buzzer.h"

// Runtime architecture (main loop):
// 1) Run background engines (timer/backlight/battery)
// 2) Poll GPS stream and button events
// 3) Route encoder to active context (editor, timestamp scroll, or mode select)
// 4) Render display at adaptive refresh rate

// ===== LCD =====
ST7036 lcd(LCD_ADDR);

// ===== GPS =====
TinyGPSPlus gps;

enum GpsSyncState : uint8_t {
  GPS_SYNC_IDLE = 0,
  GPS_SYNC_SEARCHING
};

static bool s_gpsPowerOn = false;
static bool s_gpsPowerPinsInitialized = false;
static uint32_t s_gpsPowerOnStartFixSentences = 0;
static uint32_t s_gpsInfoPowerHoldUntilMs = 0;
static GpsSyncState s_gpsSyncState = GPS_SYNC_IDLE;
static uint32_t s_gpsSyncStartedMs = 0;
static uint32_t s_gpsSyncStartFixSentences = 0;
static GpsSyncResult s_gpsSyncLastResult = GPS_SYNC_RESULT_NONE;
static uint32_t s_gpsSyncLastResultMs = 0;
static const uint32_t kGpsSyncTimeoutMs = 120000;
static const uint32_t kGpsInfoPowerHoldMs = 30000;
static bool s_gpsInfoFirstSyncDoneSincePowerOn = false;

#if GPS_PPS_DISCIPLINE_ENABLED
static bool s_syncAwaitingUpdatedTimeAfterPps = false;
static uint32_t s_syncPpsArmedMs = 0;
static const uint32_t kSyncPpsUpdatedWaitMs = 1500;
#endif

#if GPS_PPS_DISCIPLINE_ENABLED
static volatile uint8_t s_gpsPpsEdgeCount = 0;
static bool s_ppsHavePrevTick = false;
static uint32_t s_ppsPrevTick256 = 0;
static int32_t s_ppsAccumErrorTicks = 0;
static uint8_t s_ppsSampleCount = 0;
static int32_t s_ppsFilteredPpm = 0;
static uint32_t s_lastPpsCommitDate = 0;
static uint32_t s_lastPpsCommitTime = 0;
static uint32_t s_lastGpsInfoSyncFixSentences = 0;
static uint32_t s_lastGpsInfoSyncMs = 0;
static bool s_gpsInfoAwaitingUpdatedTimeAfterPps = false;
static uint32_t s_gpsInfoPpsArmedMs = 0;
static const int16_t kPpsPpmPerErrorTick =
  (int16_t)((1000000L + ((256L * GPS_PPS_DISCIPLINE_WINDOW) / 2L)) /
        (256L * GPS_PPS_DISCIPLINE_WINDOW));

void gpsPpsIsr() {
  if (s_gpsPpsEdgeCount < 255U) {
    s_gpsPpsEdgeCount++;
  }
}

static void gpsTimeAddOneSecond(TimeEdit_t* t) {
  if (t == nullptr) return;

  if (++t->second < 60) return;
  t->second = 0;
  if (++t->minute < 60) return;
  t->minute = 0;
  if (++t->hour < 24) return;
  t->hour = 0;

  uint8_t maxDay = 31;
  if (t->month == 4 || t->month == 6 || t->month == 9 || t->month == 11) {
    maxDay = 30;
  } else if (t->month == 2) {
    maxDay = 28;
  }
  if ((t->month == 2) && ((t->year % 4) == 0)) {
    maxDay = 29;
  }
  if (++t->day <= maxDay) return;
  t->day = 1;
  if (++t->month <= 12) return;
  t->month = 1;
  t->year++;
}

static void gpsPpsDisciplineReset() {
  s_ppsHavePrevTick = false;
  s_ppsPrevTick256 = 0;
  s_ppsAccumErrorTicks = 0;
  s_ppsSampleCount = 0;
  s_lastPpsCommitDate = 0;
  s_lastPpsCommitTime = 0;
  s_gpsInfoAwaitingUpdatedTimeAfterPps = false;
  s_gpsInfoPpsArmedMs = 0;
}

static bool gpsPpsPopEdge() {
  bool hadEdge = false;
  noInterrupts();
  if (s_gpsPpsEdgeCount > 0) {
    s_gpsPpsEdgeCount--;
    hadEdge = true;
  }
  interrupts();
  return hadEdge;
}

static bool gpsTryCommitFromPpsUpdatedSample() {
  if (!gps.time.isUpdated() || !isGPSTimeReliable()) {
    return false;
  }

  uint32_t rawDate = gps.date.value();
  uint32_t rawTime = gps.time.value();
  if (rawDate == s_lastPpsCommitDate && rawTime == s_lastPpsCommitTime) {
    return false;
  }

  TimeEdit_t gpsTime;
  gpsTime.year = gps.date.year();
  gpsTime.month = gps.date.month();
  gpsTime.day = gps.date.day();
  gpsTime.hour = gps.time.hour();
  gpsTime.minute = gps.time.minute();
  gpsTime.second = gps.time.second();

  // ATGM336H PPS rising edge is UTC-aligned; NMEA time payload can trail by
  // one second at parse time, so align commit phase to PPS boundary.
  gpsTimeAddOneSecond(&gpsTime);

  mcuTimeSync(&gpsTime);
  s_lastPpsCommitDate = rawDate;
  s_lastPpsCommitTime = rawTime;
  return true;
}

static void gpsInfoPpsDisciplineAndAutoSyncUpdate() {
  if (!s_gpsPowerOn) {
    gpsPpsDisciplineReset();
    return;
  }
  if (g_currentMode != MODE_GPS_INFO) {
    return;
  }

  while (gpsPpsPopEdge()) {
    uint32_t nowTicks = crystalTimeGetTicks256();

    if (s_ppsHavePrevTick) {
      uint32_t deltaTicks = nowTicks - s_ppsPrevTick256;

      // Reject obvious glitches and only accumulate near-1s intervals.
      if (deltaTicks >= 200U && deltaTicks <= 312U) {
        s_ppsAccumErrorTicks += (int32_t)deltaTicks - 256;
        s_ppsSampleCount++;

        if (s_ppsSampleCount >= GPS_PPS_DISCIPLINE_WINDOW) {
          int32_t samplePpm = s_ppsAccumErrorTicks * (int32_t)kPpsPpmPerErrorTick;

          // Low-pass filter to avoid overreacting to quantization jitter.
          s_ppsFilteredPpm = (s_ppsFilteredPpm * 7 + samplePpm) / 8;
          if (s_ppsFilteredPpm > 2000) s_ppsFilteredPpm = 2000;
          if (s_ppsFilteredPpm < -2000) s_ppsFilteredPpm = -2000;
          mcuTimeSetDriftPpm((int16_t)s_ppsFilteredPpm);

          s_ppsAccumErrorTicks = 0;
          s_ppsSampleCount = 0;
        }
      } else {
        gpsPpsDisciplineReset();
      }
    }

    s_ppsPrevTick256 = nowTicks;
    s_ppsHavePrevTick = true;

    // Arm UTC sync from the first updated GPS time after PPS edge.
    s_gpsInfoAwaitingUpdatedTimeAfterPps = true;
    s_gpsInfoPpsArmedMs = crystalTimeGetMillis();
  }

  if (s_gpsInfoAwaitingUpdatedTimeAfterPps) {
    bool hasFreshFixSincePowerOn = gps.sentencesWithFix() > s_gpsPowerOnStartFixSentences;
    bool hasFixSinceLastGpsInfoSync = gps.sentencesWithFix() > s_lastGpsInfoSyncFixSentences;
    bool allowSyncByInterval =
      !s_gpsInfoFirstSyncDoneSincePowerOn ||
      crystalTimeElapsedMs(s_lastGpsInfoSyncMs, GPS_INFO_AUTO_SYNC_MIN_MS);
    if (
        hasFreshFixSincePowerOn &&
        hasFixSinceLastGpsInfoSync &&
        allowSyncByInterval &&
        gpsTryCommitFromPpsUpdatedSample()) {
      uint32_t nowMs = crystalTimeGetMillis();
      s_lastGpsInfoSyncFixSentences = gps.sentencesWithFix();
      s_lastGpsInfoSyncMs = nowMs;
      s_gpsInfoFirstSyncDoneSincePowerOn = true;
      s_gpsSyncLastResult = GPS_SYNC_RESULT_OK;
      s_gpsSyncLastResultMs = nowMs;
      s_gpsSyncState = GPS_SYNC_IDLE;
      g_modeEpoch++;
      s_gpsInfoAwaitingUpdatedTimeAfterPps = false;
      s_gpsInfoPpsArmedMs = 0;
    } else if (crystalTimeElapsedMs(s_gpsInfoPpsArmedMs, kSyncPpsUpdatedWaitMs)) {
      s_gpsInfoAwaitingUpdatedTimeAfterPps = false;
      s_gpsInfoPpsArmedMs = 0;
    }
  }
}
#endif

static uint8_t s_gpsCfgAttempts = 0;
static uint32_t s_gpsCfgNextAttemptMs = 1000;

static void spiSetEnabled(bool enabled) {
#if POWER_GATE_SPI_UNUSED
#if defined(PRR)
#if defined(PRSPI)
  if (enabled) PRR &= (uint8_t)~_BV(PRSPI);
  else PRR |= _BV(PRSPI);
#endif
#elif defined(PRR0)
#if defined(PRSPI0)
  if (enabled) PRR0 &= (uint8_t)~_BV(PRSPI0);
  else PRR0 |= _BV(PRSPI0);
#elif defined(PRSPI)
  if (enabled) PRR0 &= (uint8_t)~_BV(PRSPI);
  else PRR0 |= _BV(PRSPI);
#endif
#endif
#else
  (void)enabled;
#endif
}

static void usart0SetEnabled(bool enabled) {
#if POWER_GATE_USART0_WITH_GPS
#if defined(PRR)
#if defined(PRUSART0)
  if (enabled) PRR &= (uint8_t)~_BV(PRUSART0);
  else PRR |= _BV(PRUSART0);
#endif
#elif defined(PRR0)
#if defined(PRUSART0)
  if (enabled) PRR0 &= (uint8_t)~_BV(PRUSART0);
  else PRR0 |= _BV(PRUSART0);
#endif
#endif
#else
  (void)enabled;
#endif
}

static void gpsSetUartEnabled(bool enabled) {
#if GPS_UART_ENABLED
  static bool s_uartEnabled = false;
  if (enabled == s_uartEnabled) return;

  if (enabled) {
    usart0SetEnabled(true);
    Serial.begin(GPS_BAUD);
  } else {
    // GPS is power-gated in desk mode; keep UART pins high-Z so TXD does not
    // source current into an unpowered GPS module through IO protection paths.
    Serial.end();
    pinMode(0, INPUT);
    pinMode(1, INPUT);
    usart0SetEnabled(false);
  }

  s_uartEnabled = enabled;
#else
  (void)enabled;
#endif
}

static void gpsSetPower(bool on) {
  bool stateChanged = (on != s_gpsPowerOn);
  if (!stateChanged && s_gpsPowerPinsInitialized) return;

  s_gpsPowerOn = on;
  if (on && stateChanged) {
    s_gpsPowerOnStartFixSentences = gps.sentencesWithFix();
#if GPS_PPS_DISCIPLINE_ENABLED
    s_lastGpsInfoSyncFixSentences = s_gpsPowerOnStartFixSentences;
    s_lastGpsInfoSyncMs = 0;
#endif
    s_gpsInfoFirstSyncDoneSincePowerOn = false;
  }

  digitalWrite(PIN_GPS_POWER, on ? LOW : HIGH);
  digitalWrite(PIN_GPS_ENABLE, on ? HIGH : LOW);
  gpsSetUartEnabled(on);

  s_gpsPowerPinsInitialized = true;

  // Re-run output config sequence on each power-up.
  if (stateChanged || on) {
    s_gpsCfgAttempts = 0;
    s_gpsCfgNextAttemptMs = crystalTimeGetMillis() + 1000;
  }
}

void gpsSyncRequest(void) {
  s_gpsSyncState = GPS_SYNC_SEARCHING;
  s_gpsSyncStartedMs = crystalTimeGetMillis();
  s_gpsSyncStartFixSentences = gps.sentencesWithFix();
#if GPS_PPS_DISCIPLINE_ENABLED
  s_syncAwaitingUpdatedTimeAfterPps = false;
  s_syncPpsArmedMs = 0;
#endif
  gpsSetPower(true);
  g_modeEpoch++;
}

bool gpsSyncIsSearching(void) {
  return s_gpsSyncState == GPS_SYNC_SEARCHING;
}

bool gpsHasFreshFixSincePowerOn(void) {
  return s_gpsPowerOn && (gps.sentencesWithFix() > s_gpsPowerOnStartFixSentences);
}

uint16_t gpsSyncGetRemainingSeconds(void) {
  if (s_gpsSyncState != GPS_SYNC_SEARCHING) return 0;
  uint32_t elapsed = crystalTimeGetMillis() - s_gpsSyncStartedMs;
  if (elapsed >= kGpsSyncTimeoutMs) return 0;
  return (uint16_t)((kGpsSyncTimeoutMs - elapsed + 999UL) / 1000UL);
}

uint16_t gpsSyncGetElapsedSeconds(void) {
  if (s_gpsSyncState != GPS_SYNC_SEARCHING) return 0;
  uint32_t elapsed = crystalTimeGetMillis() - s_gpsSyncStartedMs;
  return (uint16_t)(elapsed / 1000UL);
}

GpsSyncResult gpsSyncGetLastResult(void) {
  return s_gpsSyncLastResult;
}

uint16_t gpsSyncGetLastResultAgeSeconds(void) {
  if (s_gpsSyncLastResult == GPS_SYNC_RESULT_NONE) return 0;
  uint32_t ageMs = crystalTimeGetMillis() - s_gpsSyncLastResultMs;
  uint32_t ageSec = ageMs / 1000UL;
  if (ageSec > 999U) ageSec = 999U;
  return (uint16_t)ageSec;
}

void gpsSyncClearLastResult(void) {
  s_gpsSyncLastResult = GPS_SYNC_RESULT_NONE;
  s_gpsSyncLastResultMs = 0;
  g_modeEpoch++;
}

static void gpsSyncUpdate(void) {
  if (s_gpsSyncState != GPS_SYNC_SEARCHING) return;

  // Only accept GPS time after at least one fresh fix sentence arrives
  // in this sync session (prevents immediate reuse of stale parser data).
  bool hasFreshFixThisSession = gps.sentencesWithFix() > s_gpsSyncStartFixSentences;

#if GPS_PPS_DISCIPLINE_ENABLED
  if (hasFreshFixThisSession) {
    if (gpsPpsPopEdge()) {
      s_syncAwaitingUpdatedTimeAfterPps = true;
      s_syncPpsArmedMs = crystalTimeGetMillis();
    }

    if (s_syncAwaitingUpdatedTimeAfterPps && gpsTryCommitFromPpsUpdatedSample()) {
      s_syncAwaitingUpdatedTimeAfterPps = false;
      s_syncPpsArmedMs = 0;

      s_gpsSyncLastResult = GPS_SYNC_RESULT_OK;
      s_gpsSyncLastResultMs = crystalTimeGetMillis();
      s_gpsSyncState = GPS_SYNC_IDLE;
      g_modeEpoch++;
      return;
    }

    if (s_syncAwaitingUpdatedTimeAfterPps &&
        crystalTimeElapsedMs(s_syncPpsArmedMs, kSyncPpsUpdatedWaitMs)) {
      s_syncAwaitingUpdatedTimeAfterPps = false;
      s_syncPpsArmedMs = 0;
    }

    if (crystalTimeElapsedMs(s_gpsSyncStartedMs, 5000UL) &&
        gpsTryCommitFromPpsUpdatedSample()) {
      s_syncAwaitingUpdatedTimeAfterPps = false;
      s_syncPpsArmedMs = 0;

      s_gpsSyncLastResult = GPS_SYNC_RESULT_OK;
      s_gpsSyncLastResultMs = crystalTimeGetMillis();
      s_gpsSyncState = GPS_SYNC_IDLE;
      g_modeEpoch++;
      return;
    }
  }
#else
  if (hasFreshFixThisSession && isGPSTimeReliable()) {
    TimeEdit_t gpsTime;
    gpsTime.year = gps.date.year();
    gpsTime.month = gps.date.month();
    gpsTime.day = gps.date.day();
    gpsTime.hour = gps.time.hour();
    gpsTime.minute = gps.time.minute();
    gpsTime.second = gps.time.second();
    mcuTimeSync(&gpsTime);

    s_gpsSyncLastResult = GPS_SYNC_RESULT_OK;
    s_gpsSyncLastResultMs = crystalTimeGetMillis();
    s_gpsSyncState = GPS_SYNC_IDLE;
    g_modeEpoch++;
    return;
  }
#endif

  if (crystalTimeElapsedMs(s_gpsSyncStartedMs, kGpsSyncTimeoutMs)) {
    s_gpsSyncLastResult = GPS_SYNC_RESULT_TIMEOUT;
    s_gpsSyncLastResultMs = crystalTimeGetMillis();
    s_gpsSyncState = GPS_SYNC_IDLE;
    g_modeEpoch++;
  }
}

static void gpsApplyPowerPolicy(void) {
  uint32_t nowMs = crystalTimeGetMillis();

  // Keep GPS alive briefly after leaving GPS Info mode so quick mode switches
  // do not require a cold restart/reacquire each time.
  if (g_currentMode == MODE_GPS_INFO) {
    s_gpsInfoPowerHoldUntilMs = nowMs + kGpsInfoPowerHoldMs;
  }
  bool gpsInfoHoldActive = (int32_t)(nowMs - s_gpsInfoPowerHoldUntilMs) < 0;

  // Keep GPS off globally except:
  // 1) Active sync sessions (boot/manual), and
  // 2) GPS Info mode, and
  // 3) 30s grace period after leaving GPS Info mode.
  bool shouldBeOn = gpsSyncIsSearching() || (g_currentMode == MODE_GPS_INFO) || gpsInfoHoldActive;
  gpsSetPower(shouldBeOn);
}

static void gpsSendCommand(const char* command) {
#if GPS_UART_ENABLED
  if (command == nullptr) return;
  Serial.print(command);
  Serial.print("\r\n");
  Serial.flush();
#else
  (void)command;
#endif
}

static void gpsConfigureOutput() {
#if GPS_UART_ENABLED
  // Keep only the sentences used by the watch:
  // GGA (altitude, satellites), GSA (PDOP), and RMC (speed, course, UTC date/time).
  // Drop GLL/GSV/VTG/ZDA/TXT to reduce UART load at 9600 baud.
  gpsSendCommand("$PCAS03,1,0,1,0,1,0,0,0*03");
#endif
}

static void gpsConfigureOutputMaybeRetry() {
#if GPS_UART_ENABLED
  if (!s_gpsPowerOn) return;
  if (s_gpsCfgAttempts >= 3) return;

  uint32_t now = crystalTimeGetMillis();
  if ((int32_t)(now - s_gpsCfgNextAttemptMs) < 0) return;

  gpsConfigureOutput();
  s_gpsCfgAttempts++;
  s_gpsCfgNextAttemptMs += 1000;
#endif
}

// ===== Pins =====
#define ENC_A PIN_ENC_A
#define ENC_B PIN_ENC_B
#define ENC_BTN PIN_ENC_BTN
#define BTN_LEFT PIN_BTN_LEFT
#define BTN_RIGHT PIN_BTN_RIGHT
#define BTN_TOP PIN_BTN_TOP

// ===== Encoder =====
int32_t encoderCount = 0;
uint8_t lastState = 0;

const int8_t enc_table[16] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

// ===== Desk-Mode Sleep Control =====
static volatile bool s_deskInputWake = false;
static bool s_deskSleepArmed = false;
static uint32_t s_deskEnterMs = 0;  // Crystal-ms when Timer2 sleep was first armed
static uint32_t s_deskStayAwakeUntilMs = 0;
static uint32_t s_deskLastDisplaySecond = 0xFFFFFFFFUL;
static const uint32_t kDeskSleepDelayMs = 1500;  // Grace period before first sleep
static const uint32_t kDeskInputAwakeMs = 700;   // Keep loop awake after input wake for debounce/long-press
static const uint32_t kDeskTimestampFeedbackAwakeMs = 2300;  // Let long-press feedback finish before sleeping

static void adcSetEnabled(bool enabled) {
  if (enabled) {
#if defined(PRR)
#if defined(PRADC)
    PRR &= (uint8_t)~_BV(PRADC);
#endif
#elif defined(PRR0)
#if defined(PRADC)
    PRR0 &= (uint8_t)~_BV(PRADC);
#endif
#endif
    ADCSRA |= _BV(ADEN);
  } else {
    ADCSRA &= (uint8_t)~_BV(ADEN);
#if defined(PRR)
#if defined(PRADC)
    PRR |= _BV(PRADC);
#endif
#elif defined(PRR0)
#if defined(PRADC)
    PRR0 |= _BV(PRADC);
#endif
#endif
  }
}

static void adcApplyModePolicy() {
  // Battery ADC is only used in UTC-only mode.
  adcSetEnabled(g_currentMode == MODE_UTC_ONLY);
}

// PCINT2 ISR — intentionally empty; fires on any change of pins 4-7 (buttons)
// to wake the MCU from PWR_SAVE sleep so button press is serviced immediately.
ISR(PCINT2_vect) {
  s_deskInputWake = true;
}

// Encoder A/B pin-change wake source while sleeping in desk mode.
// ATmega328PB can map PE0/PE1 in PCINT3; some variants use PCINT1.
#if defined(PCINT3_vect)
ISR(PCINT3_vect) {
  s_deskInputWake = true;
}
#endif

#if defined(PCINT1_vect)
ISR(PCINT1_vect) {
  s_deskInputWake = true;
}
#endif

static void deskEnableWakePinChange() {
  // Buttons on PD3-PD6.
  PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21) | (1 << PCINT22);
  PCICR  |= (1 << PCIE2);

#if defined(PCMSK3) && defined(PCIE3) && defined(PCINT24) && defined(PCINT25)
  // ATmega328PB: encoder on PE0/PE1 => PCINT24/PCINT25.
  PCMSK3 |= (1 << PCINT24) | (1 << PCINT25);
  PCICR  |= (1 << PCIE3);
#elif defined(PCMSK1) && defined(PCIE1) && defined(PCINT8) && defined(PCINT9)
  // Fallback mapping for variants exposing encoder on PCINT8/PCINT9.
  PCMSK1 |= (1 << PCINT8) | (1 << PCINT9);
  PCICR  |= (1 << PCIE1);
#endif
}

static void deskDisableWakePinChange() {
  PCICR  &= (uint8_t)~(1 << PCIE2);
  PCMSK2 &= (uint8_t)~((1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21) | (1 << PCINT22));

#if defined(PCMSK3) && defined(PCIE3) && defined(PCINT24) && defined(PCINT25)
  PCICR  &= (uint8_t)~(1 << PCIE3);
  PCMSK3 &= (uint8_t)~((1 << PCINT24) | (1 << PCINT25));
#elif defined(PCMSK1) && defined(PCIE1) && defined(PCINT8) && defined(PCINT9)
  PCICR  &= (uint8_t)~(1 << PCIE1);
  PCMSK1 &= (uint8_t)~((1 << PCINT8) | (1 << PCINT9));
#endif
}

static bool deskSleepShouldRun() {
  bool noManualEdit = !timeEditIsActive() && !offsetEditIsActive() && !timerEditIsActive();
  bool noTimerActivity = !timerAnyRunning() && !timerAnyAlarmActive();
  bool noStopwatchActivity = !stopwatchAnyRunning();
  bool gpsOff = !s_gpsPowerOn;

  // Enter desk sleep only when all low-power conditions are satisfied.
  return (g_currentMode == MODE_LOCAL_ONLY) && gpsOff && noTimerActivity && noStopwatchActivity && noManualEdit;
}

static void deskSleepResetState() {
  s_deskSleepArmed = false;
  s_deskInputWake = false;
  s_deskEnterMs = 0;
  s_deskStayAwakeUntilMs = 0;
  s_deskLastDisplaySecond = 0xFFFFFFFFUL;
}

static void deskSleepMaybeRunOneCycle() {
  if (!deskSleepShouldRun()) {
    deskSleepResetState();
    return;
  }

  if (!s_deskSleepArmed) {
    s_deskSleepArmed = true;
    s_deskEnterMs = crystalTimeGetMillis();
    s_deskLastDisplaySecond = crystalTimeGetSeconds();
  }

  // 5-second grace period: let the normal loop run freely after entering Desk Mode
  // so the display draws fully and mode-change buzz completes before sleeping.
  if (!crystalTimeElapsedMs(s_deskEnterMs, kDeskSleepDelayMs)) {
    return;
  }

  // Keep CPU awake briefly after input wake so button debounce and long-press
  // logic (millis-based) can run before sleeping again.
  if (s_deskInputWake) {
    s_deskInputWake = false;
    s_deskStayAwakeUntilMs = crystalTimeGetMillis() + kDeskInputAwakeMs;
    return;
  }
  if ((int32_t)(crystalTimeGetMillis() - s_deskStayAwakeUntilMs) < 0) {
    return;
  }

  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();

  deskEnableWakePinChange();

  uint8_t adcsraPrev = ADCSRA;
  ADCSRA &= (uint8_t)~_BV(ADEN);

#if defined(PRR)
  uint8_t prrPrev = PRR;
  uint8_t prrMask = 0;
#if defined(PRADC)
  prrMask |= _BV(PRADC);
#endif
#if defined(PRUSART0)
  prrMask |= _BV(PRUSART0);
#endif
#if defined(PRSPI)
  prrMask |= _BV(PRSPI);
#endif
#if defined(PRTIM0)
  prrMask |= _BV(PRTIM0);
#endif
#if defined(PRTIM1)
  prrMask |= _BV(PRTIM1);
#endif
#if defined(PRTWI)
  prrMask |= _BV(PRTWI);
#endif
  PRR |= prrMask;
#elif defined(PRR0)
  uint8_t prr0Prev = PRR0;
  uint8_t prr0Mask = 0;
#if defined(PRADC)
  prr0Mask |= _BV(PRADC);
#endif
#if defined(PRUSART0)
  prr0Mask |= _BV(PRUSART0);
#endif
#if defined(PRSPI0)
  prr0Mask |= _BV(PRSPI0);
#elif defined(PRSPI)
  prr0Mask |= _BV(PRSPI);
#endif
#if defined(PRTIM0)
  prr0Mask |= _BV(PRTIM0);
#endif
#if defined(PRTIM1)
  prr0Mask |= _BV(PRTIM1);
#endif
#if defined(PRTWI0)
  prr0Mask |= _BV(PRTWI0);
#elif defined(PRTWI)
  prr0Mask |= _BV(PRTWI);
#endif
  PRR0 |= prr0Mask;
#if defined(PRR1)
  uint8_t prr1Prev = PRR1;
  uint8_t prr1Mask = 0;
#if defined(PRUSART1)
  prr1Mask |= _BV(PRUSART1);
#endif
#if defined(PRSPI1)
  prr1Mask |= _BV(PRSPI1);
#endif
#if defined(PRTIM3)
  prr1Mask |= _BV(PRTIM3);
#endif
#if defined(PRTIM4)
  prr1Mask |= _BV(PRTIM4);
#endif
#if defined(PRTWI1)
  prr1Mask |= _BV(PRTWI1);
#endif
  PRR1 |= prr1Mask;
#endif
#endif

  noInterrupts();
  bool stillDesk = deskSleepShouldRun();
  if (stillDesk) {
#if defined(BODS) && defined(BODSE)
    sleep_bod_disable();
#endif
    interrupts();
    sleep_cpu();
  } else {
    interrupts();
  }
  sleep_disable();

#if defined(PRR)
  PRR = prrPrev;
#elif defined(PRR0)
  PRR0 = prr0Prev;
#if defined(PRR1)
  PRR1 = prr1Prev;
#endif
#endif

  ADCSRA = adcsraPrev;

  // Disable pin-change wake sources immediately after wakeup so they don't fire outside sleep.
  deskDisableWakePinChange();

  // Check second after waking (race-free: Timer2 ISR has already run and
  // incremented s_crystalSeconds before we reach here). Doing this check
  // before sleep_cpu() had a race: if Timer2 fired between the check and
  // sleep_cpu(), the ISR consumed the overflow flag and the MCU slept until
  // the NEXT second, causing a 2-second display gap.
  {
    uint32_t nowSecond = crystalTimeGetSeconds();
    if (nowSecond != s_deskLastDisplaySecond) {
      s_deskLastDisplaySecond = nowSecond;
      // Do NOT increment g_modeEpoch — that would clear the LCD every second.
      // displayModeLocalOnly() has its own signature cache and only writes changed content.
      updateDisplay(g_currentMode);
    }
  }
}

static void nonDeskIdleSleepOneCycle() {
  // In non-desk modes, run IDLE sleep between loop iterations.
  // Timer0 interrupt (Arduino timebase) wakes every ~1ms, so UI and button
  // responsiveness remain effectively unchanged while cutting busy-spin current.
  if (deskSleepShouldRun()) {
    return;
  }

  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();
  noInterrupts();
#if defined(BODS) && defined(BODSE)
  sleep_bod_disable();
#endif
  interrupts();
  sleep_cpu();
  sleep_disable();
}

void handleEncoder() {
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  if (state != lastState) {
    // Keep desk mode awake for slow turns even when this edge is not a full step.
    s_deskInputWake = true;
  }
  uint8_t index = (lastState << 2) | state;

  int8_t step = enc_table[index];
  if (step != 0) {
    encoderCount += step;
  }
  lastState = state;
}

// ===== Setup =====
void setup() {
  // Datasheet §13.10.5 (Watchdog Timer): make sure the WDT is off after reset so
  // it can neither add running current nor reset us out of desk-mode power-save.
  MCUSR &= (uint8_t)~_BV(WDRF);
  wdt_disable();

  // Hardware-reset the LCD (active-low) on PB2 before I2C init commands.
  pinMode(PIN_LCD_RESET, OUTPUT);
  digitalWrite(PIN_LCD_RESET, HIGH);
  delay(1);
  digitalWrite(PIN_LCD_RESET, LOW);
  delay(2);
  digitalWrite(PIN_LCD_RESET, HIGH);
  delay(10);

  Wire.begin();
  lcd.begin();
  crystalTimeInit();

#if GPS_UART_ENABLED
  // GPS UART is dynamically enabled only while the GPS module is powered.
  usart0SetEnabled(false);
  pinMode(0, INPUT);
  pinMode(1, INPUT);
#else
  Serial.end();
  pinMode(0, INPUT);
  pinMode(1, INPUT);
#endif

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_TOP, INPUT_PULLUP);

  // GPS power controls are managed dynamically by mode/sync policy.
  pinMode(PIN_GPS_POWER, OUTPUT);
  pinMode(PIN_GPS_ENABLE, OUTPUT);
#if GPS_PPS_DISCIPLINE_ENABLED
  pinMode(PIN_GPS_PPS, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), gpsPpsIsr, RISING);
#else
  // Datasheet §13.10.6 (Port Pins): when PPS discipline is disabled this pin is
  // otherwise never configured, leaving a floating CMOS input. A floating input
  // near VCC/2 makes the digital input buffer draw excess current, so pull it up.
  pinMode(PIN_GPS_PPS, INPUT_PULLUP);
#endif
  gpsSetPower(false);

  spiSetEnabled(false);

  // Datasheet §13.10.6 / "Minimizing Power Consumption": any digital input left
  // floating near VCC/2 keeps its input buffer in the linear region and draws
  // shoot-through current. Across several pins this can total ~1 mA, which shows
  // up directly as excess PWR_SAVE (desk-mode) current. Tie every otherwise
  // unused pin to a defined level via the internal pull-ups.
  //
  // PB3/PB4/PB5 = SPI MOSI/MISO/SCK: only wired to the ISP header, unused at
  // runtime (the LCD is on I2C). PB6/PB7 are the 32.768 kHz TOSC crystal and
  // must NOT be touched.
  DDRB  &= (uint8_t)~(_BV(3) | _BV(4) | _BV(5));
  PORTB |= (uint8_t)(_BV(PORTB3) | _BV(PORTB4) | _BV(PORTB5));
#if defined(PORTE)
  // PE2/PE3 = unused ATmega328PB Port-E pins (PE0/PE1 are the encoder).
  DDRE  &= (uint8_t)~(_BV(2) | _BV(3));
  PORTE |= (uint8_t)(_BV(PORTE2) | _BV(PORTE3));
#endif
  // Keep PD0/PD1 untouched here. They are managed by gpsSetUartEnabled():
  // high-Z when GPS power is off, UART-owned when GPS is on.

  buzzerInit(PIN_BUZZER);
  backlightInit(PIN_BACKLIGHT_BLUE, PIN_BACKLIGHT_RED, PIN_BACKLIGHT_GREEN);
  batteryInit(PIN_BATTERY);

#if defined(ACSR) && defined(ACD)
  // Comparator is unused in this firmware; keep it off for lower baseline current.
  ACSR |= _BV(ACD);
#endif

  adcApplyModePolicy();

  lastState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);

  initButtons();

  // Boot behavior: search for GPS time for up to 2 minutes, then power off.
  gpsSyncRequest();

  updateDisplay(g_currentMode);  // Force initial display immediately
}

// ===== Loop =====
void loop() {
  // Encoder test mode: poll quadrature in main loop instead of interrupts.
  handleEncoder();

  // ===== Background Engines =====
  timerModeUpdate();
  modeAudioUpdate();
  backlightUpdate();
  gpsApplyPowerPolicy();
  adcApplyModePolicy();
  gpsConfigureOutputMaybeRetry();
  // Battery is only shown in UTC-only mode, so avoid unnecessary ADC reads elsewhere.
  if (g_currentMode == MODE_UTC_ONLY) {
    batteryUpdate();
  }

  // ===== Read GPS =====
#if GPS_UART_ENABLED
  if (s_gpsPowerOn) {
    while (Serial.available()) {
      gps.encode(Serial.read());
    }
  }
#endif

  gpsSyncUpdate();

#if GPS_PPS_DISCIPLINE_ENABLED
  gpsInfoPpsDisciplineAndAutoSyncUpdate();
#endif

  // ===== Handle Button Presses =====
  ButtonEvent_t buttonEvent = handleButtons();

  // Keep desk mode awake while user is actively pressing buttons.
  if (g_currentMode == MODE_LOCAL_ONLY && buttonEvent != BUTTON_NONE) {
    s_deskStayAwakeUntilMs = crystalTimeGetMillis() + kDeskInputAwakeMs;
  }

  // Timestamp capture feedback (buzzer + blink) is driven in the main loop.
  // If we re-enter desk sleep immediately, that feedback appears interrupted.
  if (g_currentMode == MODE_LOCAL_ONLY && buttonEvent == BUTTON_TOP_LONG) {
    s_deskStayAwakeUntilMs = crystalTimeGetMillis() + kDeskTimestampFeedbackAwakeMs;
  }
  
  // Send button events to modes for handling
  handleModeEvent(g_currentMode, buttonEvent);

  static uint32_t lastUpdate = 0;
  static uint8_t lastDisplayedMode = 0;  // Must match g_currentMode at startup for encoder to work
  static int32_t lastEncoderForEdit = 0;
  static bool wasEditing = false;
  static bool firstLoop = true;

  // Force refresh on first loop iteration (in case g_modeEpoch incremented before reaching here)
  if (firstLoop) {
    firstLoop = false;
    lastUpdate = crystalTimeGetMillis();  // Sync after initial setup() display
  }

  // ===== Read Encoder =====
  int32_t encoderValue = encoderCount;

  // Any encoder movement acknowledges active timer alarms.
  static int32_t lastEncoderForAlarmAck = 0;
  if (encoderValue != lastEncoderForAlarmAck) {
    if (timerAnyAlarmActive()) {
      timerAcknowledgeAllAlarms();
    }
    lastEncoderForAlarmAck = encoderValue;
  }

  bool isEditing = timeEditIsActive();
  bool isEditingOffset = offsetEditIsActive();
  bool isEditingTimer = timerEditIsActive();
  bool isTimestampScroll = (g_currentMode == MODE_TIMESTAMP_REVIEW) && timestampModeIsScrollActive();
  bool anyEditing = isEditing || isEditingOffset || isEditingTimer || isTimestampScroll;
  static bool skipModeChangeOnce = false;  // Skip one mode change iteration after edit exit

  // On entry to any edit mode, sync encoder baseline so first delta is zero.
  if (anyEditing && !wasEditing) {
    lastEncoderForEdit = encoderValue / ENC_DIVISOR;
  }

  // ===== Handle Encoder Input =====
  if (isEditing) {
    // In time edit mode: use encoder to adjust current field
    int32_t encDelta = (encoderValue / ENC_DIVISOR) - lastEncoderForEdit;
    if (encDelta != 0) {
      timeEditRotaryInput(encDelta);
      lastEncoderForEdit = encoderValue / ENC_DIVISOR;
    }
    wasEditing = true;
    skipModeChangeOnce = false;
  } else if (isEditingOffset) {
    // In offset edit mode: use encoder to adjust offset
    int32_t encDelta = (encoderValue / ENC_DIVISOR) - lastEncoderForEdit;
    if (encDelta != 0) {
      offsetEditRotaryInput(encDelta);
      lastEncoderForEdit = encoderValue / ENC_DIVISOR;
    }
    wasEditing = true;
    skipModeChangeOnce = false;
  } else if (isEditingTimer) {
    // In timer edit mode: use encoder to adjust HH/MM/SS field
    int32_t encDelta = (encoderValue / ENC_DIVISOR) - lastEncoderForEdit;
    if (encDelta != 0) {
      timerEditRotaryInput(encDelta);
      lastEncoderForEdit = encoderValue / ENC_DIVISOR;
    }
    wasEditing = true;
    skipModeChangeOnce = false;
  } else if (isTimestampScroll) {
    // In timestamp mode scroll state: encoder navigates stored records.
    int32_t encDelta = (encoderValue / ENC_DIVISOR) - lastEncoderForEdit;
    if (encDelta != 0) {
      timestampModeScrollBy(encDelta);
      lastEncoderForEdit = encoderValue / ENC_DIVISOR;
    }
    wasEditing = true;
    skipModeChangeOnce = false;
  } else {
    // Not editing anymore - handle transition and normal mode
    if (wasEditing) {
      // Just exited edit mode - anchor encoder to current mode to prevent jump.
      int32_t anchoredCount = (int32_t)g_currentMode * ENC_DIVISOR;
      encoderCount = anchoredCount;
      encoderValue = anchoredCount;
      lastEncoderForEdit = anchoredCount / ENC_DIVISOR;
      lastDisplayedMode = g_currentMode;  // Preserve current mode
      wasEditing = false;
      skipModeChangeOnce = true;  // Skip mode change this iteration
    }
    
    // Normal mode: use encoder to change modes (but skip first iteration after edit)
    if (!skipModeChangeOnce) {
      int32_t currentModeValue = encoderValue / ENC_DIVISOR;
      
      // Wrap mode to 0-5
      while (currentModeValue < 0) currentModeValue += NUM_MODES;
      currentModeValue = currentModeValue % NUM_MODES;
      
      uint8_t newMode = (uint8_t)currentModeValue;

      // Mode changed - increment epoch to signal all display functions
      if (newMode != lastDisplayedMode) {
        lastDisplayedMode = newMode;
        g_currentMode = newMode;

        g_modeEpoch++;  // Signal mode change to all display functions
        buzzOnce(70);  // Softer, shorter cue on mode change

        // Apply power policy immediately on mode transition.
        gpsApplyPowerPolicy();
      }
    } else {
      // Skip mode change this iteration
      skipModeChangeOnce = false;
    }
  }

  // ===== Display Update (adaptive throttle) =====
  // Stopwatch/timer activity gets faster refresh for smoother motion.
  uint16_t refreshMs = 200;
  if (g_currentMode == MODE_STOPWATCH || stopwatchAnyRunning() ||
      g_currentMode == MODE_TIMER || timerAnyRunning() || timerAnyAlarmActive()) {
    refreshMs = 100;
  }

  if (crystalTimeElapsedMs(lastUpdate, refreshMs)) {
    lastUpdate = crystalTimeGetMillis();
    updateDisplay(g_currentMode);
  }

  // In desk mode, sleep between 1 Hz async Timer2 wake-ups.
  deskSleepMaybeRunOneCycle();

  // Outside desk deep-sleep conditions, avoid full-speed busy looping.
  nonDeskIdleSleepOneCycle();
}