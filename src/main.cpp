#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
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
static GpsSyncState s_gpsSyncState = GPS_SYNC_IDLE;
static uint32_t s_gpsSyncStartedMs = 0;
static uint32_t s_gpsSyncStartFixSentences = 0;
static GpsSyncResult s_gpsSyncLastResult = GPS_SYNC_RESULT_NONE;
static uint32_t s_gpsSyncLastResultMs = 0;
static const uint32_t kGpsSyncTimeoutMs = 120000;

static uint8_t s_gpsCfgAttempts = 0;
static uint32_t s_gpsCfgNextAttemptMs = 1000;

static void gpsSetPower(bool on) {
  if (on == s_gpsPowerOn) return;

  s_gpsPowerOn = on;
  digitalWrite(PIN_GPS_POWER, on ? LOW : HIGH);
  digitalWrite(PIN_GPS_ENABLE, on ? HIGH : LOW);

  // Re-run output config sequence on each power-up.
  s_gpsCfgAttempts = 0;
  s_gpsCfgNextAttemptMs = crystalTimeGetMillis() + 1000;
}

void gpsSyncRequest(void) {
  s_gpsSyncState = GPS_SYNC_SEARCHING;
  s_gpsSyncStartedMs = crystalTimeGetMillis();
  s_gpsSyncStartFixSentences = gps.sentencesWithFix();
  gpsSetPower(true);
  g_modeEpoch++;
}

bool gpsSyncIsSearching(void) {
  return s_gpsSyncState == GPS_SYNC_SEARCHING;
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

  if (crystalTimeElapsedMs(s_gpsSyncStartedMs, kGpsSyncTimeoutMs)) {
    s_gpsSyncLastResult = GPS_SYNC_RESULT_TIMEOUT;
    s_gpsSyncLastResultMs = crystalTimeGetMillis();
    s_gpsSyncState = GPS_SYNC_IDLE;
    g_modeEpoch++;
  }
}

static void gpsApplyPowerPolicy(void) {
  // Keep GPS off globally except:
  // 1) Active sync sessions (boot/manual), and
  // 2) GPS Info mode.
  bool shouldBeOn = gpsSyncIsSearching() || (g_currentMode == MODE_GPS_INFO);
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
static const uint32_t kDeskSleepDelayMs = 5000;  // Grace period before first sleep
static const uint32_t kDeskInputAwakeMs = 700;   // Keep loop awake after input wake for debounce/long-press

// PCINT2 ISR — intentionally empty; fires on any change of pins 4-7 (buttons)
// to wake the MCU from PWR_SAVE sleep so button press is serviced immediately.
ISR(PCINT2_vect) {
  s_deskInputWake = true;
}

static bool deskSleepShouldRun() {
  // Keep sleep restricted to desk mode, and avoid conflict with active timers/alarm tone.
  return (g_currentMode == MODE_LOCAL_ONLY) && !timerAnyRunning() && !timerAnyAlarmActive();
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

  uint32_t nowSecond = crystalTimeGetSeconds();
  if (nowSecond != s_deskLastDisplaySecond) {
    s_deskLastDisplaySecond = nowSecond;
    // Do NOT increment g_modeEpoch — that would clear the LCD every second.
    // displayModeLocalOnly() has its own signature cache and only writes changed content.
    updateDisplay(g_currentMode);
  }

  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();

  // Enable PCINT2 so button pins PD3-PD6 (TOP, ENC_BTN, LEFT, RIGHT) wake the MCU.
  PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21) | (1 << PCINT22);
  PCICR  |= (1 << PCIE2);

  noInterrupts();
  bool stillDesk = deskSleepShouldRun();
  interrupts();

  if (stillDesk) {
    sleep_cpu();
  }
  sleep_disable();

  // Disable PCINT2 immediately after wakeup so it doesn't fire outside sleep.
  PCICR  &= (uint8_t)~(1 << PCIE2);
  PCMSK2 &= (uint8_t)~((1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21) | (1 << PCINT22));
}

void handleEncoder() {
  s_deskInputWake = true;
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  uint8_t index = (lastState << 2) | state;

  encoderCount += enc_table[index];
  lastState = state;
}

// ===== Setup =====
void setup() {
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
  Serial.begin(GPS_BAUD);
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
  gpsSetPower(false);

  buzzerInit(PIN_BUZZER);
  backlightInit(PIN_BACKLIGHT_BLUE, PIN_BACKLIGHT_RED, PIN_BACKLIGHT_GREEN);
  batteryInit(PIN_BATTERY);

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
  backlightUpdate();
  gpsApplyPowerPolicy();
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

  // ===== Handle Button Presses =====
  ButtonEvent_t buttonEvent = handleButtons();

  // Keep desk mode awake while user is actively pressing buttons.
  if (g_currentMode == MODE_LOCAL_ONLY && buttonEvent != BUTTON_NONE) {
    s_deskStayAwakeUntilMs = crystalTimeGetMillis() + kDeskInputAwakeMs;
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
        buzzOnce(100);  // Buzz for 100ms on mode change

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
}