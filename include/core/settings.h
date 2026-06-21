#pragma once

#include <stdint.h>

enum BacklightAutoOffSetting : uint8_t {
  BACKLIGHT_AUTO_OFF_OFF = 0,
  BACKLIGHT_AUTO_OFF_10S,
  BACKLIGHT_AUTO_OFF_30S,
  BACKLIGHT_AUTO_OFF_60S,
  BACKLIGHT_AUTO_OFF_COUNT
};

enum BuzzerModeSetting : uint8_t {
  BUZZER_MODE_OFF = 0,
  BUZZER_MODE_ALARMS_ONLY,
  BUZZER_MODE_ALL,
  BUZZER_MODE_COUNT
};

enum TimerPresetSetting : uint8_t {
  TIMER_PRESET_00_00_00 = 0,
  TIMER_PRESET_00_30_00,
  TIMER_PRESET_01_00_00,
  TIMER_PRESET_COUNT
};

enum GpsAutoSyncSetting : uint8_t {
  GPS_AUTO_SYNC_OFF = 0,
  GPS_AUTO_SYNC_12H,
  GPS_AUTO_SYNC_24H,
  GPS_AUTO_SYNC_WEEK,
  GPS_AUTO_SYNC_COUNT
};

enum LcdContrastSetting : uint8_t {
  LCD_CONTRAST_1 = 0,
  LCD_CONTRAST_2,
  LCD_CONTRAST_3,
  LCD_CONTRAST_4,
  LCD_CONTRAST_5,
  LCD_CONTRAST_COUNT
};

void settingsInit(void);

BacklightAutoOffSetting settingsGetBacklightAutoOff(void);
void settingsCycleBacklightAutoOff(void);
uint32_t settingsGetBacklightAutoOffMs(void);

BuzzerModeSetting settingsGetBuzzerMode(void);
void settingsSetBuzzerMode(BuzzerModeSetting mode);
void settingsCycleBuzzerMode(void);

TimerPresetSetting settingsGetTimerPreset(void);
void settingsCycleTimerPreset(void);
uint32_t settingsGetTimerPresetSeconds(void);

GpsAutoSyncSetting settingsGetGpsAutoSync(void);
void settingsCycleGpsAutoSync(void);
uint32_t settingsGetGpsAutoSyncIntervalMs(void);

LcdContrastSetting settingsGetLcdContrast(void);
void settingsCycleLcdContrast(void);
uint8_t settingsGetLcdContrastValue(void);