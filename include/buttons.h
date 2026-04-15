#pragma once

// ===== Button Types =====
typedef enum {
  BUTTON_NONE = 0,
  BUTTON_ENC_SHORT,
  BUTTON_ENC_LONG,
  BUTTON_LEFT_SHORT,
  BUTTON_LEFT_LONG,
  BUTTON_RIGHT_SHORT,
  BUTTON_RIGHT_LONG,
  BUTTON_TOP_SHORT,
  BUTTON_TOP_LONG,
} ButtonEvent_t;

// ===== Button Handler Functions =====
void initButtons();
ButtonEvent_t handleButtons();
