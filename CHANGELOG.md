# Changelog — GPSDO FreeRTOS

All notable changes to this project are documented here.

Project by **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Based on **GPSDO v0.06c** by André Balsa
(<https://github.com/AndrewBCN/STM32-GPSDO>), FreeRTOS port and algorithms
3–9 by the author, with Claude AI as programming assistant and PCB design
by Scrachi (EEVBlog forum).

The version suffix `-rtos` marks the FreeRTOS port lineage.

---

## [v0.47-rtos]

### Added
- **`SV` CLI command** — enable/disable survey-in (Time Mode) on a timing
  receiver at runtime, stored in EEPROM (byte 143). `SV` shows state, `SV 0`
  disables (stay in nav mode — handy for bench testing), `SV 1` enables;
  `ES` saves, applied at next boot. Defaults to enabled on fresh EEPROM.

### Fixed
- **Survey-in polling no longer stalls the displays.** `ubx_poll_svin()`
  waited up to 1000 ms with a busy `delay()`, starving the higher-priority
  GPS task's siblings — the display task visibly lagged (worst on the
  slower-responding LEA-6T). The poll now uses a ~500 ms window that yields
  with `vTaskDelay()` between reads, so the display task runs normally while
  still reliably catching the module's TIM-SVIN reply (100-200 ms latency).
  NMEA bytes seen while scanning are forwarded to TinyGPS++ so the fix is
  not disrupted. Once a survey has replied, occasional missed polls no
  longer abort the monitor (the survey is in progress); gaps in the
  `svin dur=` sequence are gone.
- **Survey-in now exits reliably when its criteria are met.** Completion is
  declared when EITHER the receiver flags the mean position valid, OR the
  user criteria are met (accuracy ≤ limit AND duration ≥ minimum) — some
  receivers (notably the LEA-6T) reached ~0.45 m well past the minimum but
  left the survey "active", so the old `valid && !active` test never fired.
  The safety backstop is now `3 × SVIN_MIN` (min 600 s) so a slow-converging
  survey on a weak antenna gets a fair chance.
- TIM-SVIN early-survey accuracy of `0xFFFFFFFF` ("no estimate") is clamped
  to 65535 mm instead of overflowing.

### Changed
- **TFT precision**: INA219 now shows bus voltage to 3 decimals and current
  to 2 decimals; the PWM control voltage (Vctl) shows 3 decimals.

### Documentation
- README (EN/PL) notes that survey-in needs a good outdoor antenna with a
  full sky view, and records the field observation that the LEA-6T is more
  sensitive than the LEA-M8T in marginal conditions. Both modules were
  verified completing survey-in and entering Time Mode on a professional
  outdoor (survey-grade) antenna. Corrected a couple of stale source
  comments (EEPROM size 144 B, TIM-SVIN vs NAV-SVIN).

---

## [v0.46-rtos]

### Removed
- **Compile-time OCXO selection (CTI / Vectron) dropped entirely.** The `CT`
  command measures the plant gain and derives all coefficients for whatever
  oscillator is fitted, so per-OCXO defines, PID tables and the
  `DEFAULT_PWM` switch are no longer needed. The loop starts from a
  universal mid-range PWM (32767 ≈ 1.65 V) before the first `CT`.

### Added
- **Multi-variant survey-in start.** The LEA-6T and LEA-M8T accept
  different Time Mode commands (both verified in u-center), so the firmware
  tries each in turn and stops at the first ACK: `CFG-TMODE2` 0x06 0x3D
  (LEA-M8T), then the classic `CFG-TMODE` 0x06 0x1D (LEA-6T, u-blox 6). This
  auto-adapts to either module. If neither is ACKed the module is assumed to
  be already timing and is monitored anyway.

### Fixed
- **TIM-SVIN accuracy was nonsense (showed ~467 km).** The `meanV` field is
  a position *variance* in mm², not a distance — the firmware now takes its
  square root to report a 1-sigma accuracy in mm (verified against u-center:
  18113534 mm² → ~4.3 m). Survey-in duration/accuracy now read sensibly.
- **Boot hang when survey-in actually started (LEA-M8T).** The survey-in
  progress loop ran inside `gpsdo_gps_init()` — before the scheduler — and
  used `vTaskDelay()`, which hangs the system when called before
  `vTaskStartScheduler()`. It never showed on the LEA-6T because that unit
  NAKed the start and skipped the loop; the M8T ACKs it, entered the loop,
  and froze (blue LED stuck). Survey-in now only *starts* in init; progress
  is polled non-blocking from `vGpsTask` after the scheduler runs.
- **Intermittent boot hang / black displays** — `STACK_DISPLAY` raised from
  768 to 1024 words. Font scaling and the OLED clear loop had made 768
  marginal; with no stack-overflow hook this showed as a silent, sometimes-
  boots hang.
- **LEA-M8T timing module now works.** It was stuck in a 3D nav fix
  (HDOP ≈ 1) because the firmware sent it `CFG-TMODE3`, which its firmware
  (TIM 1.10, PROTVER 22) does not support. u-center confirmed the LEA-M8T
  uses the **same** `CFG-TMODE2` / `TIM-SVIN` messages as the LEA-6T. The
  timing path is unified to a single TMODE2 implementation; the separate
  `GPSDO_GPS_LEA6T` / `GPSDO_GPS_LEA8T` options are replaced by one
  `GPSDO_GPS_TIMING`, and the TMODE3 / NAV-SVIN branch is removed.
- **OLED**: the lower half of the big `GPSDO` splash (drawn with a two-row
  font) lingered behind the LMT clock — the panel is now cleared, every row
  blanked, the 2x2 font reset and the row cache invalidated when the splash
  ends. `GPSDO` and the version line are centred; footer uses
  `jmnlabs+Claude`.
- **LCD 20x4**: title/version line shifted right (two leading spaces) so the
  `-rtos` suffix is no longer truncated.
- EEPROM layout header comment corrected (143 bytes, was mislabelled 134).

### Changed
- **TFT**: the white frequency value uses a fixed-width font (font 1,
  size 3) so its digits keep a constant column position; subtitle enlarged
  and changed to `GPS-Disciplined OCXO`; logo, subtitle and the
  converging-wave animation raised; hardware checklist reveals more slowly
  with a lead-in pause so the first items are not missed; footer credit
  uses `+`. Sensor values (BMP/AHT temperature, pressure, humidity) now show
  two decimal places.

---

## [v0.45-rtos]

### Changed
- **TFT splash reworked again** to a phase-lock metaphor: the credits are
  drawn first and persist; two 2px sine waves (blue above, amber below)
  start with a visible phase offset and small vertical gap, then slowly
  converge until they coincide and merge into a single 4px green wave,
  held ~1.8 s. The hardware checklist follows.
- Serial human-readable report now shows `HDOP:TIME` in Time Mode (the
  tab-delimited machine format keeps the numeric value for plotting).

### Removed
- Redundant `SERIAL_*_BUFFER_SIZE` defines in `gpsdo_config.h` (they never
  reached the core anyway). The buffer sizes live solely in `build_opt.h`
  (`RX=256, TX=512`).

---

## [v0.44-rtos]

### Added
- **`build_opt.h`** enlarging the serial RX/TX buffers to 256 bytes
  (`-DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=256`). STM32duino
  applies these to the whole build including the core, which a sketch-level
  `#define` cannot reach. This prevents NMEA sentences being dropped or
  merged at 38400 baud when the GPS task is briefly preempted (the cause of
  the garbled NMEA seen on the LEA-6T).

### Changed
- **TFT boot splash reworked**: two sine waves of different colours (blue
  from the left, amber from the right) converge to the centre and merge
  into a single green 10 MHz wave — a synchronism metaphor — with the
  GPSDO logo and hardware checklist below. Timings stretched for
  readability.

### Notes
- Only GGA + RMC NMEA sentences are kept (GLL/GSA/GSV/VTG disabled), which
  together with the larger buffer keeps the bus well within budget.

---

## [v0.43-rtos]

### Added
- **Time Mode detection / `HDOP:TIME`.** A timing receiver in time-only
  mode keeps a frozen valid position but reports HDOP ≈ 99.99. Instead of
  showing that meaningless number, the displays now show `HDOP:TIME` when a
  valid position coincides with a non-meaningful HDOP (≥ 50.00). New
  `gGps.time_mode` flag.

### Changed
- **Survey-in NAK is handled gracefully.** Some timing modules (e.g.
  surplus units with a stored Time-Mode config) NAK `CFG-TMODE2/3`. The
  firmware no longer treats this as an error — it logs that the module may
  already be timing and continues; runtime Time Mode detection then reports
  the real state.
- Boot splash durations lengthened (TFT ~7 s, OLED/LCD ~4.5 s) so the
  welcome screen can actually be read.

### Fixed
- OLED splash footer no longer clips the last character (`jmnlabs/Claude`,
  spaces around the slash removed to fit the 16-column width).

---

## [v0.42-rtos]

### Fixed
- **Build error in the survey-in code** (`get_ubx_ack` called with
  class/id/timeout instead of the message-buffer pointer it expects). Both
  `ubx_start_survey_in` branches now pass the frame buffer, matching the
  function signature. LEA timing builds compile again.

### Notes
- The u-blox M8 timing module (**LEA-M8T**) is the same generation as the
  8T and uses CFG-TMODE3 / NAV-SVIN — enable `GPSDO_GPS_LEA8T` for it.

---

## [v0.41-rtos]

### Added
- **Animated boot splash on TFT**: a sweeping 10 MHz sine, the GPSDO logo,
  and a hardware checklist reconstructed from the real detection flags
  (modules show `[x]` / `[ ]`), with a discreet `jmnlabs · with Claude
  (Anthropic)` footer. Plays once, then the operating screen is drawn.
- **Boot splash on OLED** (character mode, U8x8): double-size `GPSDO`,
  version, accent line and footer.
- **Boot splash on LCD 20x4**: four-line welcome with title, subtitle and
  footer.

### Fixed
- **TFT did not update PWM / Vctl during calibration.** The display
  returned early after drawing the countdown, freezing the info grid. It
  now falls through so the PWM/Vctl cell keeps updating live during
  `C` / `CT` — matching the OLED behaviour.

---

## [v0.40-rtos]

### Added
- **LEA-6T / LEA-8T timing receiver support** (`GPSDO_GPS_LEA6T` /
  `GPSDO_GPS_LEA8T`). On these modules the firmware runs a survey-in at
  every power-up (CFG-TMODE2 on the 6T, CFG-TMODE3 on the 8T), then the
  receiver switches to a fixed-position time-only solution with a much
  cleaner 1PPS. Survey-in ends when either the minimum duration
  (`GPSDO_SVIN_MIN_SECS`, default 120 s) or the accuracy limit
  (`GPSDO_SVIN_ACC_LIMIT`, default 2000 mm) is met.
- Survey-in progress is shown on every display (`SVIN nnns nnm` on
  OLED/LCD/TFT, dashes on the LED clocks), via the new `g_svin_*` state.
- Position keeps streaming in NMEA throughout Time Mode, so location
  display and automatic timezone (`TO A`) continue to work — using the
  averaged, frozen survey-in position.
- `CHANGELOG.md` and `CHANGELOG_PL.md` are now included in the project archive.

### Notes
- NEO-6M / NEO-8M behaviour is unchanged (neither LEA option defined).

---

## [v0.39-rtos]

### Added
- OCXO warmup is now shown on every display with a live countdown
  (`WARMUP nnn s` on OLED/LCD/TFT, dashes on TM1637/HT16K33), driven by the
  new `g_warmup_active` / `g_warmup_remaining` state.

---

## [v0.38-rtos]

### Fixed
- **Steady-state PWM dither on the phase-locked algorithms (4, 5, 7, 8).**
  The dead-zone now tests the accumulated phase as well as the frequency
  error: when `|e| < 1 mHz` and `|phase| < 5 Hz·s` (≈500 ns) the loop holds
  the PWM and reports `hit`, so a locked oscillator stops being nudged by
  GPS noise every period. Small phase noise is held; real drift is still
  corrected.
- All phase algorithms now actually emit the `hit` trend on lock; FLL
  algorithms (3, 6) gained an equivalent frequency-only lock hold.
- PWM and Vctl readings on the displays now update live **during** `C` /
  `CT` calibration (a new `wait_secs_pwm` publishes PWM and samples the
  Vctl ADC each second while the main loop is busy).

---

## [v0.37-rtos]

### Changed
- `LP 8` and `LP 9` now show where those algorithms actually read their
  gains: algo 8 (hybrid) uses `g_pid[6]` (FLL branch) + `g_pid[7]` (PLL
  branch); algo 9 (NN) uses fixed network weights, so only `NS` / `IL`
  apply. Prevents the empty `g_pid[8]/[9]` from looking "untuned" after
  `CT`.

---

## [v0.36-rtos]

### Added
- Calibration progress shown on all displays: `CAL nnn s` countdown in the
  frequency field (OLED/LCD/TFT) and `CAL` on the LED clocks (TM1637 /
  HT16K33), via `g_calib_active` / `g_calib_remaining`.

---

## [v0.35-rtos]

### Added
- **`CT` (Calibrate & Tune) command.** Measures the plant gain `K` from a
  three-point PWM sweep (1.5 / 2.0 / 2.5 V) with a least-squares fit, finds
  the PWM for exactly 10 MHz, and derives PID coefficients for all
  algorithms from `K` (PLL: `Kp = 0.40/K`; FLL: `Kp = 0.35/K`,
  `Ki = Kp/300`, `Kd = Kp·73`; NN: `max_step = 0.05/K`). Sanity-checked,
  non-destructive; `ES` saves the result.

---

## [v0.34-rtos]

### Changed
- **Two-timescale PLL tuning for "fast capture, gentle phase-hold".** The
  dominant term acts on the frequency error (`Kp ≈ 0.4/K`) for quick,
  overshoot-free capture; small phase terms remove slow drift. A shared
  output stage adds a slew-rate limit (≈12 LSB/step for the PLLs, 40 for
  the hybrid) and a near-lock dead-zone, so a large overnight phase drift
  is spread over several periods instead of one big PWM jump.

---

## [v0.33-rtos]

### Fixed
- **Algorithm 9 (NN) ran away upward.** The previous "trained" weights had a
  large output bias (≈ −0.96 at zero error → constant PWM ramp). Replaced
  with an analytically constructed, bias-free, odd-symmetric network: zero
  input gives exactly zero output.
- **Algorithms 4 / 5 / 7 and the PLL branch of 8 drifted.** They used a
  rolling-window average as a stand-in for phase, which lagged the 10 s
  update by 500–1000 s and wound the integrator up. Replaced with true
  phase accumulation (`phase += (avg10 − 10 MHz)·10 s`, the exact cycle
  count), feeding back with a 10 s lag.
- The `GPS fix acquired` message now distinguishes the first fix after boot
  from a genuine recovery after fix loss.

### Added
- **Automatic timezone (`TO A`).** Local time follows the GPS position: a
  compact European civil-zone rule set plus the EU DST rule, or a solar
  `round(lon/15)` zone elsewhere. `TO <n>` keeps the manual mode. The mode
  is saved to EEPROM (byte 142, now 143 bytes total) and restored at boot.

---

## [v0.32-rtos]

### Fixed
- **Hardware detection report.** Added a robust dual-verification I2C probe
  (address ACK + 1-byte read-back). OLED and HT16K33 were previously
  reported `OK` unconditionally / on an unreliable ACK; they now report
  real presence. TM1637 and TFT are marked `enabled (write-only — not
  verifiable)`.
- **TFT frequency colour.** The green "locked" colour is now derived from
  the actual deviation from 10 MHz (≤1 mHz on the 10000 s window or ≤10 mHz
  on 1000 s), independent of the algorithm — so a locked algo 8 turns green
  too, rather than only on the rarely-emitted `hit` trend.

---

## [v0.31-rtos]

### Added
- **HT16K33 4-digit clock support** (I2C 0x70): a self-contained driver
  (HH:MM with blinking colon, `oooo` when searching), shareable with the
  LCD on the same bus — no extra pins. TM1637 retained.
- Unified startup hardware report: every optional device reports `OK` or
  `not found` in a consistent `HW:` format.
- New hardware architecture diagram in both READMEs (TFT + HT16K33).

---

## [v0.30-rtos]

### Added
- **TFT 240×320 support (ILI9341 / ST7789)** via TFT_eSPI on hardware SPI1
  (SCK PA5, MOSI PA7, RES PB15, DC PB12, CS PB13). Landscape layout: header
  bar, large colour-coded frequency, two-column info grid, sensor row, and
  a colour-coded status bar. Selective per-cell redraw keeps SPI traffic
  low. DisplayTask stack raised to 768 words when the TFT is enabled.
  Both controllers tested on hardware.

---

## [v0.29-rtos]

### Fixed
- **picDIV synchronisation.** Arming is now deferred until a GPS fix is
  present (a stopped divider with no 1PPS on Sync would otherwise hang
  dead); a dedicated flag replaces the millis-timestamp guard (wrap-safe);
  auto-arm after calibration was removed (the loop hasn't converged yet).
  Added clear serial feedback. README documents FLL phase random-walk vs
  PLL phase-lock for long-term 1PPS alignment.

---

## [v0.28-rtos]

### Fixed
- **PWM range with 3.3 V DAC.** The STM32 PWM reaches only 0–3.3 V of the
  0–4 V EFC input (82.5 %), so the accessible tuning is −10…+14.75 Hz (CTI)
  and −20…+13 Hz (Vectron). Default PWM corrected per-OCXO: 32767 (CTI,
  1.65 V midpoint) and 39718 (Vectron, 2.0 V nominal).

---

## [v0.27-rtos]

### Fixed
- **Vectron C4550A1-0213 parameters.** Corrected to its real operating
  point: 5 V supply, 0–4 V EFC, Kv = 10 Hz/V (0.504 mHz/LSB), scale factor
  1.333 vs CTI (gains × 0.75), shared default PWM.

### Changed
- `README_EN.md` renamed to `README.md` (GitHub default); `README_PL.md`
  unchanged.

---

## [v0.26-rtos]

### Added
- **OCXO selection** in `gpsdo_config.h` (`GPSDO_OCXO_CTI_OSC5A2B02` /
  `GPSDO_OCXO_VECTRON_C4550`), with per-OCXO compile-time PID defaults and
  default PWM. Falls back to CTI values if none is selected.
- `SP`, `F`, `C`, `T` documented in the help text and READMEs.

---

## [v0.25-rtos]

### Added
- `g_pressure_offset` (`PO`) and `g_altitude_offset` (`AO`) now saved to and
  restored from EEPROM (bytes 134–141, 142 bytes total).
- `V` command expanded with full author/credit information and GitHub links.

---

## [v0.24-rtos]

### Fixed
- **Bluetooth output.** All runtime messages route through an `OUT_SERIAL`
  macro (Serial2 when `GPSDO_BLUETOOTH` is defined, else USB Serial).

### Added
- Report pause/resume (`RP` / `RR`) to quiet the data stream during
  configuration.
- Algorithm PID parameters saved to EEPROM (signature `GPSD2`).
- Professional file-header documentation across all source files; README
  rewritten from scratch (project description, hardware principle, software
  architecture) in Polish and English; GitHub URL added to every file and
  to the serial banner.

---

## [v0.23-rtos]

### Added
- **Runtime PID tuning over CLI** — `LP`, `KP`, `KI`, `KD`, `IL` for
  algorithms 3–7, `BC` / `BS` for the algo 8 blend, `NS` for the algo 9 NN
  step. Coefficients moved to a global `g_pid[10]` array.

---

## [v0.22-rtos]

### Added
- Yellow LED 4-state machine (off / on / slow pulse = manual holdover /
  fast pulse = auto-holdover) and automatic holdover on GPS fix loss with
  `H` / `A` indicators on OLED and LCD.

---

## [v0.21-rtos]

### Added
- OLED row-0 clock (local time + day of week) after the version splash;
  LCD line-2 date/day rotating view. Day-of-week (Zeller) and local-time
  offset helpers.

---

## [v0.20-rtos]

### Changed
- Unified 4-character trend strings; corrected OLED/LCD frequency
  formatting; build-time guard against LCD + TM1637 together; fixed the
  André Balsa source URL.

---

## [v0.19-rtos]

- First tracked FreeRTOS port baseline: STM32F411CE BlackPill, frequency
  measurement via TIM2 ETR + TIM3 1PPS capture, ring-buffer averaging,
  PWM-DAC discipline loop, GPS/NMEA parsing, OLED / LCD / TM1637 displays,
  optional AHT/BMP/INA sensors, and the initial control algorithms.
