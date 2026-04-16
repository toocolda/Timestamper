#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include "st7036.h"
#include "config.h"
#include "modes.h"
#include "buttons.h"
#include "time_edit.h"
#include "mcu_time.h"
#include "local_time.h"
#include "stopwatch.h"

// ===== LCD =====
ST7036 lcd(LCD_ADDR);

// ===== GPS =====
TinyGPSPlus gps;

// ===== Pins =====
#define ENC_A PIN_ENC_A
#define ENC_B PIN_ENC_B
#define ENC_BTN PIN_ENC_BTN
#define BTN_LEFT PIN_BTN_LEFT
#define BTN_RIGHT PIN_BTN_RIGHT
#define BTN_TOP PIN_BTN_TOP
#define BUZZER PIN_BUZZER

// ===== Encoder =====
volatile int32_t encoderCount = 0;
volatile uint8_t lastState = 0;

const int8_t enc_table[16] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

void handleEncoder() {
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  uint8_t index = (lastState << 2) | state;

  encoderCount += enc_table[index];
  lastState = state;
}

// ===== Setup =====
void setup() {
  Wire.begin();
  lcd.begin();

  Serial.begin(GPS_BAUD);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_TOP, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);

  lastState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);

  attachInterrupt(digitalPinToInterrupt(ENC_A), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), handleEncoder, CHANGE);

  initButtons();

  lcd.setCursor(0, 0);
  lcd.print("Init...");
}

// ===== Loop =====
void loop() {
  // ===== Read GPS =====
  while (Serial.available()) {
    gps.encode(Serial.read());
  }

  // ===== Handle Button Presses =====
  ButtonEvent_t buttonEvent = handleButtons();
  
  // Send button events to modes for handling
  handleModeEvent(g_currentMode, buttonEvent);

  static uint32_t lastUpdate = 0;
  static uint8_t lastDisplayedMode = 255;
  static int32_t lastEncoderForEdit = 0;
  static bool wasEditing = false;

  // ===== Read Encoder =====
  int32_t encoderValue;
  noInterrupts();
  encoderValue = encoderCount;
  interrupts();

  bool isEditing = timeEditIsActive();
  bool isEditingOffset = offsetEditIsActive();
  bool anyEditing = isEditing || isEditingOffset;
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
  } else {
    // Not editing anymore - handle transition and normal mode
    if (wasEditing) {
      // Just exited edit mode - anchor encoder to current mode to prevent jump.
      int32_t anchoredCount = (int32_t)g_currentMode * ENC_DIVISOR;
      noInterrupts();
      encoderCount = anchoredCount;
      interrupts();
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
      }
    } else {
      // Skip mode change this iteration
      skipModeChangeOnce = false;
    }
  }

  // ===== Update Display (adaptive throttle) =====
  // Stopwatch needs 0.1s visual resolution.
  uint16_t refreshMs = 200;
  if (g_currentMode == MODE_STOPWATCH || stopwatchAnyRunning()) {
    refreshMs = 100;
  }

  if (millis() - lastUpdate >= refreshMs) {
    lastUpdate = millis();
    updateDisplay(g_currentMode);
  }
}