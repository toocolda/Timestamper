# Timestamper

A compact GPS-capable watch/clock designed for pilot flight training. Might be also useful in other use cases. 

This repository is a PlatformIO project targeting Arduino framework on ATmega328PB at 8 MHz 3.3V.

Current firmware: v0.03 (build 2026-06-21)

More info on timestamper.ca

## Features

- 7 UI modes controlled by rotary encoder
- UTC time display with manual edit
- UTC + Local time display with editable UTC offset
- EEPROM-backed timestamp capture/review (up to 99 records)
- Stopwatch mode (single channel)
- Fuel timer mode (countdown + alarm + elapsed)
- Desk clock mode with low-power sleep behavior
- GPS info mode (altitude, ground speed, track, fix/sat status, PDOP)
- Power-aware GPS/UART gating and sleep policies

## Hardware Assumptions

Configured in include/core/config.h:

- MCU: ATmega328PB
- CPU clock: 8 MHz
- Display: ST7036-compatible 20x2 over I2C (address 0x3C)
- GPS UART: 9600 baud
- Build/upload toolchain: PlatformIO + USBtiny ISP + Logic Level Convertor (5V-3.3V)

### Hardware Main Blocks/Components

- MCU: ATMEGA328PB-AU
- GPS: ATGM336H-5N31
- Display: NHD-C0220BIZ-FS(RGB)-FBW-3VM
- User Inputs:
    Rotary Encoder
    Push buttons

### Wiring Table

| Subsystem | Signal | MCU Pin | Direction | Notes |
|---|---|---|---|---|
| Encoder | ENC_A | PE1 | Input | Quadrature A |
| Encoder | ENC_B | PE0 | Input | Quadrature B |
| Encoder | ENC_BTN | PD4 | Input | Encoder push button |
| Buttons | BTN_LEFT | PD6 | Input | Left button |
| Buttons | BTN_RIGHT | PD5 | Input | Right button |
| Buttons | BTN_TOP | PD3 | Input | Top button |
| GPS | GPS_POWER | PC1 | Output | Active-low high-side power control |
| GPS | GPS_ENABLE | PC2 | Output | GPS EN pin |
| GPS | GPS_PPS | PD2 | Input | PPS pulse input |
| LCD | LCD_RESET | PC3 | Output | Hardware reset for LCD |
| Backlight | BACKLIGHT_RED | PB2 | Output | PWM-capable output |
| Backlight | BACKLIGHT_GREEN | PB1 | Output | PWM-capable output |
| Backlight | BACKLIGHT_BLUE | PB0 | Output | PWM-capable output |
| Buzzer | BUZZER | PD7 | Output | Timer1-driven tone output |
| Battery | BATTERY_ADC | PC0 | Analog input | Battery voltage measurement |

### Quick Wiring Notes

- LCD data path is I2C; only reset is mapped in firmware pin config.
- GPS serial uses USART0 at 9600 baud and is power-gated by mode/sync policy.
- Buttons and encoder are configured with internal pull-ups in firmware.

## Build And Upload

From repository root:

```bash
platformio run
platformio run --target upload
```

Current PlatformIO environment:

- env: atmega328_8mhz
- platform: atmelavr
- board: ATmega328PB
- framework: arduino
- upload_protocol: usbtiny

## Controls

### Global Controls

- Top short: toggle backlight
- Top long: capture timestamp (stores current time to EEPROM)

### Encoder

- Rotate (normal state): switch UI mode
- Rotate (inside edit/scroll state): adjust current value or list selection
- Encoder short/long press: mode-specific actions

### Buttons

- Left/Right short and long press are mode-specific
- If timer alarm is active, any button/encoder movement acknowledges alarm first

## Mode Guide

Mode index order:

1. UTC Only
2. UTC + Local
3. Timestamp Review
4. Stopwatch
5. Timer
6. Local Only (Desk Mode)
7. GPS Info

### 1) UTC Only

Display:

- Line 1: UTC date/time (or placeholder if no valid time)
- Line 2: sync status and battery

Controls:

- Encoder long: start manual UTC time edit
- Encoder short (while editing): move to next field
- Right short (while editing): save and exit edit
- Right long (not editing): request GPS sync
- Left long: open the UTC settings menu

UTC settings menu:

- Rotate encoder: move up/down the menu
- Encoder short or right short: change the selected setting
- Left long: exit settings and return to UTC Only

Current UTC settings:

- Backlight auto-off: `OFF`, `10s`, `30s`, `60s`
- Buzzer: `OFF`, `ALRMS ONLY`, `ALL`
- Timer preset: `00:00:00`, `00:30:00`, `01:00:00`
- GPS auto sync: `OFF`, `12H`, `24H`, `WEEK`
- LCD contrast: `1`..`5` (applied live)
- Firmware version display
- Build date display

Notes:

- Manual UTC edit writes time and clears last GPS sync status
- Inactivity timeout exists while editing (configured in src/time_edit.cpp)

### 2) UTC + Local

Display:

- Line 1: UTC time
- Line 2: local time (using UTC offset)

Controls:

- Encoder long: start UTC offset edit
- Rotate: change offset
- Right short or encoder short (while editing): save offset and exit

### 3) Timestamp Review

Display:

- Browse saved timestamps (newest first)
- Toggle UTC/local rendering for entries
- Empty-state placeholders shown when no records exist

Controls:

- Encoder short: toggle scroll mode
- Rotate (in scroll mode): move selection
- Right short: toggle UTC/local view
- Left short (in scroll mode): delete selected record
- Left long: enter delete-all confirmation
- In confirmation:
  - Left short: cancel
  - Right short: delete all records

Storage details:

- Ring buffer in EEPROM, capacity 99 records
- Records persist across power cycles
- When full, newest write overwrites oldest

### 4) Stopwatch

Display:

- Line 1: state header (RUN/STP)
- Line 2: HH:MM:SS.t

Controls:

- Encoder short or right short: start/stop
- Left short: reset to zero

### 5) Timer (Fuel Timer)

Display:

- Line 1: FUEL TIMER + state (RUN/STP/ALM)
- Line 2: signed timer display
  - - HH:MM:SS for countdown
  - + HH:MM:SS for elapsed after zero crossing

Controls:

- Encoder long: enter timer edit (when stopped)
- Rotate in edit: adjust current field (HH, MM, SS)
- Encoder short in edit: next field
- Right short in edit: finish edit
- Finishing edit auto-starts timer
- Encoder short or right short (not editing): start/stop
- Left short: reset

Alarm behavior:

- Alarm activates at countdown completion
- Alarm auto-clears after a short timeout
- Any button or encoder movement acknowledges alarm
- Reset returns to the configured timer preset

### 6) Local Only (Desk Mode)

Display:

- Centered date and time presentation
- Multiple date format styles
- 12h/24h toggle
- UTC/local toggle for displayed time source

Controls:

- Left short: cycle date format
- Right short: toggle 12h/24h
- Encoder short: toggle UTC/local source

Power behavior:

- Uses low-power sleep policy when idle in this mode
- Wakes periodically for display and on user input events

### 7) GPS Info

Display:

- Line 1: ALT:xxxxxFT GS:xxxKT
- Line 2: TRK:ddd CCC XXnn Ppp

Where:

- TRK is track/course over ground (degrees)
- CCC is 3-char cardinal token
- XXnn is fix/status + satellites
- Ppp is PDOP token

GPS freshness policy:

- Values are hidden until a fresh fix is received after GPS power-on

## Time Sources And Sync

- A crystal-timed software clock is used as the running time base
- On boot, firmware requests GPS sync automatically
- Manual sync can be requested from UTC Only mode (right long)
- GPS sync timeout is currently 120 seconds
- Optional automatic GPS resync can be set to `12H`, `24H`, `WEEK`, or `OFF`

## Power Strategy (High Level)

- GPS power is enabled only when needed (sync, GPS Info, short hold)
- USART0 can be power-gated with GPS
- ADC is mainly used in UTC mode for battery reporting
- Desk mode has dedicated sleep behavior for reduced idle draw

## Repository Layout

- src: firmware implementation
- include: headers and module interfaces
- lib/TinyGPSPlus: bundled TinyGPS++
- datasheet: hardware reference docs
- pcb: hardware design files
- test: test scaffold

## Known Practical Notes

- Timestamp capture uses the current maintained clock value, not raw GPS parser timestamp
- Battery status is surfaced as a coarse level in UTC Only mode
- GPS PPS discipline is currently disabled by default in config
- Settings are stored in EEPROM and persist across power cycles

## License

This project is licensed under the MIT License. See the LICENSE file for details.
