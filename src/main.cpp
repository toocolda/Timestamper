#include <Arduino.h>
#include <Wire.h>
#include "display/st7036.h"
#include "core/config.h"
#include "core/settings.h"
#include "core/modes.h"
#include "hardware/buttons.h"
#include "hardware/backlight.h"
#include "hardware/battery.h"
#include "hardware/buzzer.h"
#include "hardware/gps_service.h"
#include "hardware/power_sleep.h"
#include "time/time_edit.h"
#include "time/local_time.h"
#include "time/crystal_time.h"
#include "features/stopwatch.h"
#include "features/timer.h"

// Runtime architecture (main loop):
// 1) Run background engines (timer/backlight/battery)
// 2) Poll GPS service (UART, sync, auto-sync, power policy)
// 3) Route encoder to active context (editor, timestamp scroll, or mode select)
// 4) Render display at adaptive refresh rate
// 5) Sleep (desk deep-sleep or idle) to cut current
//
// GPS sync/power lives in gps_service.cpp; low-power policy and sleep handling
// live in power_sleep.cpp.

// ===== LCD =====
ST7036 lcd(LCD_ADDR);

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

void handleEncoder() {
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  if (state != lastState) {
    // Keep desk mode awake for slow turns even when this edge is not a full step.
    powerDeskNoteWake();
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
  settingsInit();

  // Early low-power hardware gating (WDT off, comparator off, SPI gate, pull-ups).
  powerEarlyInit();

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
  lcd.setContrast(settingsGetLcdContrastValue());
  crystalTimeInit();

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_TOP, INPUT_PULLUP);

  // GPS UART/power/enable/PPS pins; leaves the GPS module powered off.
  gpsServiceInit();

  buzzerInit(PIN_BUZZER);
  buzzerSetMode(settingsGetBuzzerMode());
  backlightInit(PIN_BACKLIGHT_BLUE, PIN_BACKLIGHT_RED, PIN_BACKLIGHT_GREEN);
  backlightSetManualTimeoutMs(settingsGetBacklightAutoOffMs());
  batteryInit(PIN_BATTERY);
  timerApplyDefaultPreset();

  powerAdcApplyModePolicy();

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
  powerAdcApplyModePolicy();
  // Battery is only shown in UTC-only mode, so avoid unnecessary ADC reads elsewhere.
  if (g_currentMode == MODE_UTC_ONLY) {
    batteryUpdate();
  }

  // ===== GPS Service (UART pump, sync state machine, auto-sync, PPS) =====
  gpsServicePoll();

  // ===== Handle Button Presses =====
  ButtonEvent_t buttonEvent = handleButtons();

  // Keep desk mode awake while user is actively pressing buttons.
  if (g_currentMode == MODE_LOCAL_ONLY && buttonEvent != BUTTON_NONE) {
    powerDeskNoteButtonActivity();
  }

  // Timestamp capture feedback (buzzer + blink) is driven in the main loop.
  // If we re-enter desk sleep immediately, that feedback appears interrupted.
  if (g_currentMode == MODE_LOCAL_ONLY && buttonEvent == BUTTON_TOP_LONG) {
    powerDeskNoteTimestampFeedback();
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
  bool isUtcSettingsMenu = (g_currentMode == MODE_UTC_ONLY) && utcSettingsIsActive();
  bool anyEditing = isEditing || isEditingOffset || isEditingTimer || isTimestampScroll || isUtcSettingsMenu;
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
  } else if (isUtcSettingsMenu) {
    // In UTC settings menu: encoder navigates menu items.
    int32_t encDelta = (encoderValue / ENC_DIVISOR) - lastEncoderForEdit;
    if (encDelta != 0) {
      utcSettingsScrollBy(encDelta);
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

  // ===== Sleep (desk deep-sleep + non-desk idle sleep) =====
  powerSleepUpdate();
}
