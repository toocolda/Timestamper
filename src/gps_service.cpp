#include <Arduino.h>
#include <TinyGPS++.h>
#include <avr/interrupt.h>

#include "hardware/gps_service.h"
#include "core/config.h"
#include "core/modes.h"
#include "core/settings.h"
#include "time/time_edit.h"
#include "time/local_time.h"
#include "time/mcu_time.h"
#include "time/crystal_time.h"
#include "features/timer.h"

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
    bool isLeap = ((t->year % 400) == 0) ||
                  (((t->year % 4) == 0) && ((t->year % 100) != 0));
    maxDay = isLeap ? 29 : 28;
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

static void gpsAutoSyncMaybeRequest() {
  uint32_t intervalMs = settingsGetGpsAutoSyncIntervalMs();
  if (intervalMs == 0UL) return;
  if (gpsSyncIsSearching()) return;
  if (timeEditIsActive() || offsetEditIsActive() || timerEditIsActive() || utcSettingsIsActive()) return;
  if (s_gpsSyncLastResult == GPS_SYNC_RESULT_NONE) return;

  if (crystalTimeElapsedMs(s_gpsSyncLastResultMs, intervalMs)) {
    gpsSyncRequest();
  }
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

uint16_t gpsSyncGetLastResultAgeDays(void) {
  if (s_gpsSyncLastResult == GPS_SYNC_RESULT_NONE) return 0;
  uint32_t ageMs = crystalTimeGetMillis() - s_gpsSyncLastResultMs;
  uint32_t ageDays = ageMs / 86400000UL;
  if (ageDays > 999U) ageDays = 999U;
  return (uint16_t)ageDays;
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

void gpsApplyPowerPolicy(void) {
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

// ===== Public service API =====
void gpsServiceInit(void) {
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

  // GPS power controls are managed dynamically by mode/sync policy.
  pinMode(PIN_GPS_POWER, OUTPUT);
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
}

void gpsServicePoll(void) {
  gpsConfigureOutputMaybeRetry();

#if GPS_UART_ENABLED
  if (s_gpsPowerOn) {
    while (Serial.available()) {
      gps.encode(Serial.read());
    }
  }
#endif

  gpsSyncUpdate();
  gpsAutoSyncMaybeRequest();

#if GPS_PPS_DISCIPLINE_ENABLED
  gpsInfoPpsDisciplineAndAutoSyncUpdate();
#endif
}

bool gpsPowerIsOn(void) {
  return s_gpsPowerOn;
}
