#include <Arduino.h>
#include "core/config.h"
#include "hardware/buttons.h"
#include "time/crystal_time.h"

// ===== Button Configuration =====
#define LONG_PRESS_TIME_MS 500  // Time to consider a press as "long"
#define DEBOUNCE_TIME_MS 20     // Debounce time

// ===== Button Pin Definitions =====
#define BTN_ENC PIN_ENC_BTN
#define BTN_L PIN_BTN_LEFT
#define BTN_R PIN_BTN_RIGHT
#define BTN_T PIN_BTN_TOP

// ===== Button State Tracking =====
struct ButtonState {
  uint8_t pin;
  bool lastPressed;
  uint32_t pressStartTime;
  bool longPressReported;
};

static ButtonState buttons[4] = {
  {BTN_ENC, false, 0, false},
  {BTN_L, false, 0, false},
  {BTN_R, false, 0, false},
  {BTN_T, false, 0, false}
};

// ===== Initialize Buttons =====
void initButtons() {
  // Pins are already set to INPUT_PULLUP in main.cpp setup
}

// ===== Handle Button Presses =====
ButtonEvent_t handleButtons() {
  static uint32_t lastDebounceTime = 0;
  // Use the crystal-backed clock here so debounce timing remains correct while
  // the MCU is in PWR_SAVE sleep and the CPU millis() counter is paused.
  uint32_t nowMs = crystalTimeGetMillis();
  
  // Debounce check
  if (nowMs - lastDebounceTime < DEBOUNCE_TIME_MS) {
    return BUTTON_NONE;
  }
  
  for (int i = 0; i < 4; i++) {
    bool isPressed = !digitalRead(buttons[i].pin);  // Pins are pulled up, so pressed = LOW
    
    // Transition from not pressed to pressed
    if (isPressed && !buttons[i].lastPressed) {
      buttons[i].lastPressed = true;
      buttons[i].pressStartTime = nowMs;
      buttons[i].longPressReported = false;
      lastDebounceTime = nowMs;
    }
    
    // Button is being held
    if (isPressed && buttons[i].lastPressed) {
      // Check for long press if not already reported
      if (!buttons[i].longPressReported && 
          (nowMs - buttons[i].pressStartTime) >= LONG_PRESS_TIME_MS) {
        buttons[i].longPressReported = true;
        lastDebounceTime = nowMs;
        
        // Return long press event
        switch (i) {
          case 0: return BUTTON_ENC_LONG;
          case 1: return BUTTON_LEFT_LONG;
          case 2: return BUTTON_RIGHT_LONG;
          case 3: return BUTTON_TOP_LONG;
        }
      }
    }
    
    // Transition from pressed to not pressed
    if (!isPressed && buttons[i].lastPressed) {
      buttons[i].lastPressed = false;
      lastDebounceTime = nowMs;
      
      uint32_t pressDuration = nowMs - buttons[i].pressStartTime;
      
      // If it was a short press (released before long press time)
      if (!buttons[i].longPressReported && pressDuration < LONG_PRESS_TIME_MS) {
        // Return short press event
        switch (i) {
          case 0: return BUTTON_ENC_SHORT;
          case 1: return BUTTON_LEFT_SHORT;
          case 2: return BUTTON_RIGHT_SHORT;
          case 3: return BUTTON_TOP_SHORT;
        }
      }
    }
  }
  
  return BUTTON_NONE;
}
