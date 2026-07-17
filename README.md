# GPSDO FreeRTOS v0.95

A GPS-disciplined 10 MHz OCXO on an STM32 BlackPill (F411CE), running FreeRTOS.
Eleven disciplining algorithms, a time-interval counter with sub-nanosecond
phase readout, TFT/OLED/LED displays, a serial CLI, and wear-levelled flash
storage for learned state.

Project by **J. M. Niewiński** — based on **GPSDO v0.06c** by André Balsa
([STM32-GPSDO](https://github.com/AndrewBCN/STM32-GPSDO)), with the FreeRTOS
port and algorithms 3–10 by the author, Claude AI as programming assistant, and
PCB design by Scrachi (EEVBlog forum).

---

## Documentation

The full documentation lives in [`doc/`](doc/). Everything is maintained in
three languages:

| | English | Polski | Español |
|---|---|---|---|
| **Manual** — hardware, wiring, algorithms, CLI, display | [README_EN](doc/README_EN.md) | [README_PL](doc/README_PL.md) | [README_ES](doc/README_ES.md) |
| **Changelog** — what changed and why | [CHANGELOG_EN](doc/CHANGELOG_EN.md) | [CHANGELOG_PL](doc/CHANGELOG_PL.md) | [CHANGELOG_ES](doc/CHANGELOG_ES.md) |
| **Flash ring bring-up** — first-time setup of the flash ring buffer | [BRINGUP_EN](doc/FLASH_RING_BRINGUP_EN.md) | [BRINGUP_PL](doc/FLASH_RING_BRINGUP_PL.md) | [BRINGUP_ES](doc/FLASH_RING_BRINGUP_ES.md) |

New here? Start with the manual in your language — it covers the build, the
wiring and the first calibration run.

---

## What's new in v0.95

**Timezones with DST, anywhere in the world.** `TZ Adelaide` is now enough:

```
TZ Adelaide     → UTC+9:30, and UTC+10:30 when DST is active
TZ Warsaw       → UTC+1 / UTC+2
TZ Kolkata      → UTC+5:30, no DST
```

407 zones are built in, and city names are unique worldwide so the region is
optional. `H TZ` explains the format, including how to type a raw POSIX rule
when a government changes the rules before this firmware catches up.

Also fixed: the frequency reading no longer jumps on the 320×240 panel, the
frame rails meet the header line again, `CT` counts down instead of showing
"Tune 0s", and the survey-in notice is yellow rather than a white word that
happened to blink.

See the [changelog](doc/CHANGELOG_EN.md) for the full list and the reasoning.

---

## Quick start

1. **Build** — Arduino IDE with the STM32duino core, board *Generic STM32F4 →
   BlackPill F411CE*. Display and peripherals are selected in
   `gpsdo_config.h`.
2. **Flash** — via ST-Link or DFU.
3. **Connect** — serial at 115200. `H` lists every command.
4. **Calibrate** — `CT` derives the PWM/frequency coefficient and tunes the
   PID for all algorithms (~3 min). With an LTIC detector fitted, follow with
   `LC` to calibrate the phase detector.
5. **Save** — `ES` writes the settings to EEPROM.

The [manual](doc/README_EN.md) covers each of these properly.

---

## Repository layout

```
GPSDO_FreeRTOS.ino      entry point: init, task creation, scheduler start
gpsdo_config.h          display/peripheral selection, pins, constants
gpsdo_tasks.cpp         FreeRTOS tasks: display, sensors, uptime
gpsdo_control.cpp       control loop, calibration (C/CT/LC)
GPSDO_algorithms.cpp    disciplining algorithms 3–10
gpsdo_freq.cpp          frequency counting and averaging windows
gpsdo_gps.cpp           NMEA/UBX parsing, survey-in
gpsdo_isr.cpp           interrupt handlers: 1PPS capture, timer overflow
ubx_timtp.cpp           UBX-TIM-TP sawtooth (qErr) decoding
gpsdo_tz.cpp            timezone resolution (POSIX TZ rules, named zones)
gpsdo_cli.cpp           serial command interface
gpsdo_state.cpp         shared state, EEPROM save/recall
flash_ring.cpp          wear-levelled flash storage for live data
live_store.cpp          what goes in the ring and when
TM1637Display.cpp       vendored driver, patched for FreeRTOS
tz_table.h              generated: 407 zones, 88 rules (see tools/)
tools/gen_tz_table.py   regenerates tz_table.h from system tzdata
doc/                    manual, changelog and bring-up guide (EN/PL/ES)
```

---

## Links

- **Repository** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
- **Original project** — [STM32-GPSDO](https://github.com/AndrewBCN/STM32-GPSDO)
  by André Balsa
- **Discussion** — the GPSDO thread on the EEVBlog forum
