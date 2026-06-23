#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "hardware/power_sleep.h"
#include "hardware/gps_service.h"
#include "hardware/backlight.h"
#include "core/config.h"
#include "core/modes.h"
#include "time/crystal_time.h"
#include "time/time_edit.h"
#include "time/local_time.h"
#include "features/timer.h"
#include "features/stopwatch.h"

// ===== Desk-Mode Sleep Control =====
static volatile bool s_deskInputWake = false;
static bool s_deskSleepArmed = false;
static uint32_t s_deskEnterMs = 0;  // Crystal-ms when Timer2 sleep was first armed
static uint32_t s_deskStayAwakeUntilMs = 0;
static uint32_t s_deskLastDisplaySecond = 0xFFFFFFFFUL;
static const uint32_t kDeskSleepDelayMs = 1500;  // Grace period before first sleep
static const uint32_t kDeskInputAwakeMs = 700;   // Keep loop awake after input wake for debounce/long-press
static const uint32_t kDeskTimestampFeedbackAwakeMs = 2300;  // Let long-press feedback finish before sleeping

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

void powerAdcApplyModePolicy(void) {
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
  bool noManualEdit = !timeEditIsActive() && !offsetEditIsActive() && !timerEditIsActive() && !utcSettingsIsActive();
  bool noTimerActivity = !timerAnyRunning() && !timerAnyAlarmActive();
  bool noStopwatchActivity = !stopwatchAnyRunning();
  bool gpsOff = !gpsPowerIsOn();

  // Blue backlight fading needs 10ms updates + a running Timer3. Deep sleep
  // gates Timer3 off and only wakes at 1Hz, which would strand the PWM pin and
  // stretch the fade into a visible per-second blink. Stay awake until settled.
  bool noBacklightFade = !backlightFadeTransitionActive();

  // Enter desk sleep only when all low-power conditions are satisfied.
  return (g_currentMode == MODE_LOCAL_ONLY) && gpsOff && noTimerActivity && noStopwatchActivity && noManualEdit && noBacklightFade;
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

// ===== Public API =====
void powerEarlyInit(void) {
  // Datasheet §13.10.5 (Watchdog Timer): make sure the WDT is off after reset so
  // it can neither add running current nor reset us out of desk-mode power-save.
  MCUSR &= (uint8_t)~_BV(WDRF);
  wdt_disable();

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

#if defined(ACSR) && defined(ACD)
  // Comparator is unused in this firmware; keep it off for lower baseline current.
  ACSR |= _BV(ACD);
#endif
}

void powerSleepUpdate(void) {
  // In desk mode, sleep between 1 Hz async Timer2 wake-ups.
  deskSleepMaybeRunOneCycle();

  // Outside desk deep-sleep conditions, avoid full-speed busy looping.
  nonDeskIdleSleepOneCycle();
}

void powerDeskNoteWake(void) {
  s_deskInputWake = true;
}

void powerDeskNoteButtonActivity(void) {
  s_deskStayAwakeUntilMs = crystalTimeGetMillis() + kDeskInputAwakeMs;
}

void powerDeskNoteTimestampFeedback(void) {
  s_deskStayAwakeUntilMs = crystalTimeGetMillis() + kDeskTimestampFeedbackAwakeMs;
}
