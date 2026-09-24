# GPSDO v1.06.42rt

A GPS-disciplined 10 MHz OCXO on an STM32 BlackPill (F411CE), running FreeRTOS.
Fourteen disciplining algorithms (0–13), a time-interval counter with
sub-nanosecond phase readout, TFT/OLED/LED displays, a serial CLI, an external
18-bit DAC path, and wear-levelled flash storage for learned state.

Project by **J. M. Niewiński** — based on **GPSDO v0.06c** by André Balsa
([STM32-GPSDO](https://github.com/AndrewBCN/STM32-GPSDO)), with the FreeRTOS
port and algorithms 3–9 by the author, Claude Opus 5, GLM-5.3 Max and
Qwen3.8-Max as programming assistants, and PCB design by Scrachi (EEVBlog
forum). Algorithm 11 (continuous-PI loop) is based on the original GPSDO
controller design by the late **Lars Walenius**, extended here with CT
auto-calibration, a frequency-led acquisition branch and a picDIV
phase-capture bridge.

Algorithm 12 (multi-level accumulator) is based on the Budget GPSDO by
**Alan Cashin** (MIS42N on the EEVBlog forum), which is also the origin of the
zero-crossing correction, the dithered PWM that reaches 24 bits from a short
one, and the `CS` self-assessment idea. It is implemented here on the LTIC
phase detector rather than a counter.

Algorithm 13 (Kalman filter) is original to this project. The algorithm-10
(LTIC 3-stage) concept, and the measurement and field testing of algorithms
10–12, by **Dan Wiering**, whose rubidium-referenced ADEV runs shaped both
loops.

---


## Documentation

The full documentation lives in [`doc/`](doc/). Everything is maintained in
three languages:

| | English | Polski | Español |
|---|---|---|---|
| **Step-by-step user manual** — build, flash, calibrate, every command, troubleshooting; written for first-time users | [MANUAL_EN](doc/MANUAL_EN.md) | [MANUAL_PL](doc/MANUAL_PL.md) | [MANUAL_ES](doc/MANUAL_ES.md) |
| **Manual** — hardware, wiring, algorithms, CLI, display | [README_EN](doc/README_EN.md) | [README_PL](doc/README_PL.md) | [README_ES](doc/README_ES.md) |
| **Changelog** — what changed and why | [CHANGELOG_EN](doc/CHANGELOG_EN.md) | [CHANGELOG_PL](doc/CHANGELOG_PL.md) | [CHANGELOG_ES](doc/CHANGELOG_ES.md) |
| **Flash ring bring-up** — first-time setup of the flash ring buffer | [BRINGUP_EN](doc/FLASH_RING_BRINGUP_EN.md) | [BRINGUP_PL](doc/FLASH_RING_BRINGUP_PL.md) | [BRINGUP_ES](doc/FLASH_RING_BRINGUP_ES.md) |

New here? Start with the [step-by-step user manual](doc/MANUAL_EN.md) — it
walks through the build, the flash, the first calibration and every command
assuming no prior knowledge. The reference manual in your language covers the
same ground in more depth.

---

## What's new in v1.06 (build 42)

**Algorithm 13 — a Kalman filter.** Three states (phase, frequency, aging)
with scalar updates; it *measures* its own detector noise (R, from two lags of
differences) and the oscillator's frequency walk (Q, adapted, bounded at
R/KT³), so the GPS-vs-oscillator weighting moves with the hardware it lands
on. Honest verdict: on a detector-referenced comparison algorithm 11 still
wins — the filter refuses to chase the detector's slow zero wander (~2.6 ns
over ~45 s on this bench), which is mathematically right and metrically
invisible. Documented in manual section 4.6 and Appendix D (no formulas).

**Controller/estimator split (`KC`).** The filter's bandwidth (KT) and the
phase-nulling pace (KC, default KT/3, in force from the loop's first settle)
are separate knobs — they are genuinely different things. Measured ~23% better
ADEV at tau 256–4096 on a 20-hour run, at a stated dither cost.

**AD5680 external DAC** (`GPSDO_DAC_EXT`) — an 18-bit control voltage (the
24-bit frame word is trimmed to the part), sub-LSB remainders carried to the
next second; selected against the dithered PWM by a `DAC` command.

**Algorithm 12 re-derived** from Alan Cashin's original: faithful tree logic,
measured thresholds (`MF 3`), runtime-tunable limits, arming and persistence.

**Robustness:** the trust test can no longer convict a healthy detector on
TIM2's own quantisation; picDIV arm no longer feeds rail readings to the loops
(four seconds of deliberate phase silence — `HOLD` right after `ARM` is
health); TIM2 ignores its warm-up boxcar after a reset; telemetry write is
non-blocking over USB CDC; a stack-overflow hook names the guilty task.
`KL` now prints the adaptation ratio and Q water marks; the TFT header shows
total CPU load (`TL 1` / `SW` for per-task).

A subset of the stability and USB fixes has been **backported to the
v1.05-rtos line** — if you stay on 1.05, take the newest fix package; settings
migrate automatically. The full history, with the reasoning and the ideas that
were tried and abandoned, is in the [changelog](doc/CHANGELOG_EN.md).

## Quick start

1. **Build** — Arduino IDE with the STM32duino core, board *Generic STM32F4 →
   BlackPill F411CE*. Display and peripherals are selected in
   `gpsdo_config.h`.
2. **Flash** — via ST-Link or DFU.
3. **Connect** — serial at 115200. `H` lists every command.
4. **Calibrate** — `CT` first (~3 min): it measures the oscillator's Hz-per-LSB
   slope and tunes every algorithm from it. Then `LC` to calibrate the phase
   detector. **That order matters** — `LC` without a measured slope falls back to
   a generic value and comes out quietly wrong.
5. **Choose the loop** — `LA 11` for the continuous-PI loop (recommended),
   `LA 10` for the three-stage one, `LA 12` for the multi-level accumulator,
   or `LA 13` for the Kalman filter. Gain is derived automatically from `CT`;
   the Kalman measures its own R and Q from your hardware.
6. **Save** — `ES` writes the settings to the flash ring. Preferences save
   themselves; loop tuning needs an explicit `ES` and the reply says which.

The [manual](doc/README_EN.md) covers each of these properly.

---

## Repository layout

```
GPSDO_FreeRTOS/     the Arduino sketch — source files and nothing else
doc/                manual, changelog, tuner guide, bring-up (EN / PL / ES)
tools/              harnesses and analysers: hostcheck, loopsim, the tuner
README.md           this file
.gitignore
```

Arduino requires the sketch file and the folder holding it to share a name,
which is why the repository root and the sketch folder are both called
`GPSDO_FreeRTOS`. Open `GPSDO_FreeRTOS/GPSDO_FreeRTOS.ino` in the IDE; `doc/`
and `tools/` sit outside it so that a clone reads as code, documentation and
tools rather than as one folder with all three mixed together.

`doc/` also holds working material — correspondence, audits, to-do lists —
which `.gitignore` keeps out of the repository. It names people and quotes
private mail, so it is excluded by pattern rather than by memory. Move a file
into the tracked set deliberately, once you have read it and decided it belongs
in public.

### Naming

Every source file belonging to this project carries a lower-case `gpsdo_`
prefix, so a directory listing separates the project from the things it sits
on. Four files deliberately do not, and each for a reason whose failure mode is
silence rather than a compile error:

| file | why it keeps its name |
|---|---|
| `GPSDO_FreeRTOS.ino` | Arduino requires the sketch file to carry the same name as its folder — rename one without the other and the IDE stops seeing a sketch |
| `build_opt.h` | the Arduino builder looks for this name to pick up extra compiler flags; renamed, the flags are silently dropped and it still builds, just differently |
| `STM32FreeRTOSConfig.h` | the STM32FreeRTOS library includes it by that exact name |
| `TM1637Display.cpp` / `.h` | vendored from upstream; keeping upstream's filename is what makes a future diff against upstream readable |

`tools/gpsdo_restructure.py` applies the naming and the layout to an older
tree. It prints what it would do and changes nothing until `--apply`, and it is
safe to re-run: on a tree already laid out this way it reports no actions.

### Inside the sketch folder

```
GPSDO_FreeRTOS.ino          entry point: init, task creation, scheduler start
gpsdo_config.h              display/peripheral selection, pins, constants
gpsdo_tasks.cpp             FreeRTOS tasks: display, sensors, uptime
gpsdo_control.cpp           control loop, calibration (C/CT/LC)
gpsdo_algorithms.cpp        disciplining algorithms 0-13
gpsdo_freq.cpp              frequency counting and averaging windows
gpsdo_gps.cpp               NMEA/UBX parsing, survey-in
gpsdo_isr.cpp               interrupt handlers: 1PPS capture, timer overflow
gpsdo_ubx_timtp.cpp         UBX-TIM-TP sawtooth (qErr) decoding
gpsdo_tz.cpp                timezone resolution (POSIX TZ rules, named zones)
gpsdo_cli.cpp               serial command interface
gpsdo_state.cpp             shared state and persistence wrappers
gpsdo_dac.cpp               control-voltage output: PWM or external DAC
gpsdo_dac_ext.cpp           external SPI DAC: AD5680 (18-bit), bit-banged
gpsdo_pwm24.cpp             24-bit control voltage from dithered PWM
gpsdo_backlight.cpp         TFT backlight dimming on PB5 (the BL command)
gpsdo_health.cpp            correction statistics behind the CS command
gpsdo_flash_ring.cpp        wear-levelled flash storage, settings and live data
gpsdo_settings_store.cpp    the settings block: what is saved and in which group
gpsdo_live_store.cpp        learned drift and damping, saved as they change
gpsdo_build.cpp             firmware CRC and banner identity
gpsdo_build_id.h            one number, bumped to force the sketch to recompile
gpsdo_tee_serial.h          mirrors one stream onto USB and Bluetooth together
gpsdo_tz_table.h            generated: 503 zones, 88 rules (see tools/)
TM1637Display.cpp           vendored driver, patched for FreeRTOS
```

---

## Links

- **Repository** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
- **Original project** — [STM32-GPSDO](https://github.com/AndrewBCN/STM32-GPSDO)
  by André Balsa
- **Discussion** — the GPSDO thread on the EEVBlog forum
