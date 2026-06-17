#include <EEPROM.h>

#include "core/settings.h"
#include "features/timestamp.h"

static const uint8_t kSettingsMagic0 = 0x46;  // 'F'
static const uint8_t kSettingsMagic1 = 0x57;  // 'W'
static const uint8_t kSettingsVersion = 2;
static const int kSettingsBaseAddr = 2 + ((int)TIMESTAMP_STORE_MAX * 6);
static const int kAddrMagic0 = kSettingsBaseAddr + 0;
static const int kAddrMagic1 = kSettingsBaseAddr + 1;
static const int kAddrVersion = kSettingsBaseAddr + 2;
static const int kAddrBacklight = kSettingsBaseAddr + 3;
static const int kAddrBuzzer = kSettingsBaseAddr + 4;
static const int kAddrTimerPreset = kSettingsBaseAddr + 5;
static const int kAddrGpsAutoSync = kSettingsBaseAddr + 6;

struct SystemSettings {
  BacklightAutoOffSetting backlightAutoOff;
  BuzzerModeSetting buzzerMode;
  TimerPresetSetting timerPreset;
  GpsAutoSyncSetting gpsAutoSync;
};

static bool s_loaded = false;
static SystemSettings s_settings;

static void settingsSetDefaults(void) {
  s_settings.backlightAutoOff = BACKLIGHT_AUTO_OFF_30S;
  s_settings.buzzerMode = BUZZER_MODE_ALL;
  s_settings.timerPreset = TIMER_PRESET_00_00_00;
  s_settings.gpsAutoSync = GPS_AUTO_SYNC_OFF;
}

static bool settingsValuesValid(void) {
  return s_settings.backlightAutoOff < BACKLIGHT_AUTO_OFF_COUNT &&
         s_settings.buzzerMode < BUZZER_MODE_COUNT &&
         s_settings.timerPreset < TIMER_PRESET_COUNT &&
         s_settings.gpsAutoSync < GPS_AUTO_SYNC_COUNT;
}

static void settingsSave(void) {
  EEPROM.update(kAddrMagic0, kSettingsMagic0);
  EEPROM.update(kAddrMagic1, kSettingsMagic1);
  EEPROM.update(kAddrVersion, kSettingsVersion);
  EEPROM.update(kAddrBacklight, (uint8_t)s_settings.backlightAutoOff);
  EEPROM.update(kAddrBuzzer, (uint8_t)s_settings.buzzerMode);
  EEPROM.update(kAddrTimerPreset, (uint8_t)s_settings.timerPreset);
  EEPROM.update(kAddrGpsAutoSync, (uint8_t)s_settings.gpsAutoSync);
}

void settingsInit(void) {
  if (s_loaded) return;

  if (EEPROM.read(kAddrMagic0) == kSettingsMagic0 &&
      EEPROM.read(kAddrMagic1) == kSettingsMagic1 &&
      EEPROM.read(kAddrVersion) == kSettingsVersion) {
    s_settings.backlightAutoOff = (BacklightAutoOffSetting)EEPROM.read(kAddrBacklight);
    s_settings.buzzerMode = (BuzzerModeSetting)EEPROM.read(kAddrBuzzer);
    s_settings.timerPreset = (TimerPresetSetting)EEPROM.read(kAddrTimerPreset);
    s_settings.gpsAutoSync = (GpsAutoSyncSetting)EEPROM.read(kAddrGpsAutoSync);

    if (!settingsValuesValid()) {
      settingsSetDefaults();
      settingsSave();
    }
  } else {
    settingsSetDefaults();
    settingsSave();
  }

  s_loaded = true;
}

BacklightAutoOffSetting settingsGetBacklightAutoOff(void) {
  settingsInit();
  return s_settings.backlightAutoOff;
}

void settingsCycleBacklightAutoOff(void) {
  settingsInit();
  s_settings.backlightAutoOff = (BacklightAutoOffSetting)((s_settings.backlightAutoOff + 1U) % BACKLIGHT_AUTO_OFF_COUNT);
  settingsSave();
}

uint32_t settingsGetBacklightAutoOffMs(void) {
  switch (settingsGetBacklightAutoOff()) {
    case BACKLIGHT_AUTO_OFF_OFF:
      return 0;
    case BACKLIGHT_AUTO_OFF_10S:
      return 10000UL;
    case BACKLIGHT_AUTO_OFF_30S:
      return 30000UL;
    case BACKLIGHT_AUTO_OFF_60S:
      return 60000UL;
    default:
      return 30000UL;
  }
}

BuzzerModeSetting settingsGetBuzzerMode(void) {
  settingsInit();
  return s_settings.buzzerMode;
}

void settingsSetBuzzerMode(BuzzerModeSetting mode) {
  settingsInit();
  if (mode >= BUZZER_MODE_COUNT) return;
  s_settings.buzzerMode = mode;
  settingsSave();
}

void settingsCycleBuzzerMode(void) {
  settingsInit();
  s_settings.buzzerMode = (BuzzerModeSetting)((s_settings.buzzerMode + 1U) % BUZZER_MODE_COUNT);
  settingsSave();
}

TimerPresetSetting settingsGetTimerPreset(void) {
  settingsInit();
  return s_settings.timerPreset;
}

void settingsCycleTimerPreset(void) {
  settingsInit();
  s_settings.timerPreset = (TimerPresetSetting)((s_settings.timerPreset + 1U) % TIMER_PRESET_COUNT);
  settingsSave();
}

uint32_t settingsGetTimerPresetSeconds(void) {
  switch (settingsGetTimerPreset()) {
    case TIMER_PRESET_00_00_00:
      return 0UL;
    case TIMER_PRESET_00_30_00:
      return 30UL * 60UL;
    case TIMER_PRESET_01_00_00:
      return 60UL * 60UL;
    default:
      return 0UL;
  }
}

GpsAutoSyncSetting settingsGetGpsAutoSync(void) {
  settingsInit();
  return s_settings.gpsAutoSync;
}

void settingsCycleGpsAutoSync(void) {
  settingsInit();
  s_settings.gpsAutoSync = (GpsAutoSyncSetting)((s_settings.gpsAutoSync + 1U) % GPS_AUTO_SYNC_COUNT);
  settingsSave();
}

uint32_t settingsGetGpsAutoSyncIntervalMs(void) {
  switch (settingsGetGpsAutoSync()) {
    case GPS_AUTO_SYNC_OFF:
      return 0UL;
    case GPS_AUTO_SYNC_12H:
      return 12UL * 60UL * 60UL * 1000UL;
    case GPS_AUTO_SYNC_24H:
      return 24UL * 60UL * 60UL * 1000UL;
    case GPS_AUTO_SYNC_WEEK:
      return 7UL * 24UL * 60UL * 60UL * 1000UL;
    default:
      return 0UL;
  }
}