# Changelog — GPSDO FreeRTOS

**English** | [Polski](CHANGELOG_PL.md) | [Español](CHANGELOG_ES.md)

📖 [Project home](../README.md) · Back to [README](README_EN.md) · Manual: [MD](MANUAL_EN.md) · [PDF](MANUAL_EN.pdf)

All notable changes to this project are documented here.

Project by **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Based on **GPSDO v0.06c** by André Balsa
(<https://github.com/AndrewBCN/STM32-GPSDO>), FreeRTOS port and algorithms
3–10 by the author, with **Claude Opus 5** (Anthropic), **GLM-5.3 Max** (Z.ai)
and **Qwen3.8-Max** as programming assistants, and PCB design by Scrachi
(EEVBlog forum).

From build 57 a build names itself `GPSDO vX.YY.NNrt`: the release, `NN` the
build number (`BUILD_SERIAL` in `gpsdo_build_id.h`), and `rt` for the FreeRTOS
port lineage. Build 56, the first to carry its number, spelled it
`GPSDO v1.07-rt56`. Releases before that carried the suffix `-rtos` —
`v1.06-rtos` was build 42 — and keep the names they were released under.

---

## [v1.07] — released 2026-09-24 (build 57)

Released as build 57, the first release to carry its build in its name:
`GPSDO v1.07.57rt`. As before, entries landed here as they were measured, not
as they were written.

### Added
- **`SPAN` — two `CT` calibrations, one per EFC span, chosen by the span
  jumper on PB14 (build 55).** A board with the EFC level shifter runs its
  oscillator over the full control span or a reduced one, and the two are
  different plants: on Dan Wiering's V3 prototype `CT` measured 8209 LSB/Hz on
  the full span and 40873–46711 on the reduced one, 5.0–5.7 times more — though
  the full-span figure predates his swap from a 5 V to a 4.096 V reference, and
  on one reference the two positions differ by the divider, 4.1–4.7×. Every
  loop coefficient is derived from that number, so until now a board whose
  jumper moved ran on the other position's calibration until someone re-ran
  `CT`. The jumper's second pole now goes to **PB14** (fitted = to ground =
  REDUCED; open, or not wired at all = FULL, so a board without the wire keeps
  its one calibration as before) and the firmware keeps one K per position.
  When the jumper moves — debounced, half a second:

  - every coefficient is re-derived from that position's K — the same set `CT`
    derives, now in one function, `algo_coeffs_from_k()`, that both call — and
    the learned LSB-denominated state (LRN feed-forward, algorithm 9's tempco)
    is rescaled by the ratio of the two;
  - **the control code is remapped so the EFC pin keeps its voltage**, and the
    frequency before the move is the frequency after it:
    `c_B = p_B + (K_A/K_B)·(c_A − p_A)`, where `(p_A, p_B)` is one pair of codes
    known to give the same voltage in both positions. The two paths' offsets
    are set by the reference, the divider and the trimmer, none of which the
    firmware can see, so the pair is measured rather than modelled — the old
    position's code at a move made while locked, paired with the new
    position's once its own loop has earned a lock, or `CT`'s null — and
    re-anchored at every move, so that a K error pivots about where the loop
    actually is;
  - the loop restarts, as on an algorithm change.

  A position with no calibration of its own runs on the other's coefficients,
  as a one-calibration board always did, while `CT` starts by itself — with a
  GPS fix, never in holdover, and up to three more times, 10, 20 and 40 minutes
  after a failure; then it stops and says so. A jumper moved while the board
  was off is handled at power-on, before the loop starts. The first
  boot of this build files an existing calibration under the position the pin
  reads; if the other position's `CT` later measures the same plant (within
  1.5×), the older one is taken to have been measured in the wrong place — the
  typical case is a board calibrated on REDUCED before PB14 was wired — and is
  forgotten. `SPAN` shows the state, `SPAN CLR FULL|REDUCED` forgets one
  position, and the `DAC` report prints it under the plant line. Manual gains
  in LSB (`LG`, `MG`, `LTK`) are the operator's and are not rescaled; a move
  warns about each one that is set.

  **Simulated before it shipped.** `tools/spansim` (new) compiles this module,
  the settings store, the algorithms and the health unit unmodified and drives
  them with a model of the V3 board — 4.083:1 divider, 1.5967 Hz/V,
  1.7e-10/day aging — under algorithm 11 at LTC 60, each power cycle a separate
  process sharing a flash image. Every scenario is run again on a build without
  span sensing, which is the firmware as it was:

  | | span sensing | one calibration |
  |---|---|---|
  | move between two calibrated positions: lands at | 3e-12 … 1.8e-11 | 3e-8 … 1.2e-7 |
  | phase excursion / relock | 13–49 ns / 399 s | 3.5–11.5 µs / 665–877 s |
  | jumper moved while off: boots at | 8.1e-12 | 1.2e-7 |
  | cold start, 3e-8 of retrace, then REDUCED: lands at | 5.6e-10 | 3.3e-8 |

  The cold-start figure is the map's own error: the two `CT`s were 2 % and
  10 % off in opposite directions, and the loop was 417 codes from the pair.

  Persisted as 12 bytes appended to the settings block (380 → 392 bytes, every
  earlier field at its old offset, checked with `offsetof` under
  arm-none-eabi). An older record reads the new fields back as 0, which means
  "never recorded"; build 54 reading a build-55 record takes the first 380
  bytes, so going back is safe too.
- **`VS` — what the PA0 divider is measuring (build 49).** `VS VCC` or
  `VS VREF`, auto-saved with the output-path group. From the V3 board a jumper
  on the top of that 4k7 + 4k7 divider selects the voltage **reference** in
  place of the 5 V rail: same pin, same divider, same scaling, different
  subject. The firmware cannot see a jumper, so it is told — the same
  arrangement as `DV`, `AV` and the DAC path, and for the same reason: the only
  evidence a jumper leaves is a voltage that is not where the firmware expected
  it, and that helps only if the firmware was told what to expect.

  Being told buys the check. On `VREF` the `DAC` report prints the reading
  against what `DV` claims and complains past 5 %, which catches a reference
  that is missing, sagging, or simply not the part that was fitted. That last
  one is not a subtle failure: a 5.000 V device where `DV` says 4.096 makes
  every frequency figure the board prints a fifth too small, in silence. The
  TFT label follows the jumper too — calling a 4.1 V reference "Vcc" would read
  as a supply that had half collapsed.

  Why a jumper at all rather than a pin of its own: **there is no free ADC
  channel**. The converter reaches PA0–PA7, PB0 and PB1 on this package, and
  with SPI1 carrying the display every one of the ten is taken. Freeing one
  would mean moving the AD5680's clock off PB0, which buys a pin at the cost of
  breaking the boards already built.
- **`BL` — TFT backlight dimming (build 48).** 30..100 %, auto-saved with the
  display flags. Drives a P-channel MOSFET on **PB5** (TIM3 CH2, 20 kHz)
  between the 3.3 V rail and the panel's LED anode: gate through ~47 Ω, a
  100 kΩ pull-down so the state is defined while the pin is high-impedance at
  reset, and 10-47 µF of bulk at the drain.

  The stage **inverts** — a low gate is full brightness — so the firmware
  writes the complement, which makes `BL 100` a compare of zero: the pin sits
  statically low and the stage does not switch at all. That is the point.
  3.3 V is the rail that feeds VDDA and therefore the ADC reference, the phase
  reading and the Vctl monitor, so full brightness is the *quietest* setting,
  not the loudest; dimming trades a little rail noise for load and for heat in
  an enclosure thermally coupled to the OCXO.

  The floor is 30 % for a reason that is not electrical: below about a third
  the panel stops being readable rather than becoming usefully dim, so a
  mistyped `3` would leave the operator unable to tell a dim screen from a dead
  board. Compiled in with any TFT, and it **yields PB5 to the 2 kHz test
  generator** when that has been explicitly enabled — a default must not take
  a pin away from a deliberate choice. `TIM3` was free: several comments
  claimed the 1 PPS capture lived there, but that is TIM2 channel 3 on PB10.

  Persisted in the **last padding byte of the settings block**, offset 319,
  beside `dac_path` at 318 — `sizeof(SettingsBlock_t)` is 376 before and after
  and every later field keeps its offset, verified with `offsetof` under
  arm-none-eabi rather than by eye. No `SETTINGS_VER` bump, because recall
  requires an exact version *and* size match and a bump would throw away
  everyone's PID, LC and timezone for one byte. Zero means unset, so a record
  written before the field existed asks for the default rather than for a dark
  screen.
- `DV` — volts at full code for the `DAC` report's "commanded" (2.50..5.50 V,
  default 3.30 = the PWM model). On a 5 V external DAC the old hardcoded 3.3 V
  model under-stated commanded by 1.5× and the report flagged MISMATCH on
  every reading. Auto-saved.
- `AV` — divide ratio of the divider before the ADC pin (1.00..10.00, default
  direct). Scales "measured" and every Vctl display (telemetry line, CSV,
  TFT). Auto-saved. With a sense divider at the DAC output (10k+10k on the
  AD5680 boards), `DV 5.00` + `AV 2.00` make the MISMATCH check exact.
- `SETTINGS_VER` 6: the two scales above persist with the ALGO block; v5
  records migrate automatically with the new fields at their defaults.

### Changed
- **The build number moves inside the version: `GPSDO v1.07.57rt` (build
  57).** The release, the build as a third number, then `rt` for the FreeRTOS
  lineage, in place of build 56's `GPSDO v1.07-rt56`. Only the spelling changes:
  the name is still `g_fw_version`, still joined in the sketch alone, printed
  by the same places and exactly as long, so every display that had room for
  `-rt56` has room for `.57rt`. The source headers follow (`Part of GPSDO
  v1.07.57rt`) — respelling them was an edit, so for now every one says 57 —
  and so do the document titles, the v1.06 README in the sketch folder
  included (`v1.06.42rt`). The tuner reads all three spellings, `v1.06-rtos`,
  `v1.07-rt56` and `v1.07.57rt`, compares only the release, and leaves the
  separate `build N` off its status line whenever the name already carries it.
- **The firmware's name carries its build: `GPSDO v1.07-rt56` (build 56).**
  Replaces `GPSDO v1.07-rtos`. `rt` still marks the FreeRTOS lineage; the
  number is `BUILD_SERIAL`, so a bump renames the firmware by itself, and a
  capture, a photo of the display or a line from the tuner says which build it
  came from. Joined once in the sketch as `g_fw_version` — the sketch is the
  only unit that includes `gpsdo_build_id.h`, so a bump still recompiles only
  the sketch — and printed from that one string by the banner, `V`, the `H`
  header, the TFT header and the OLED and LCD splash screens, all of which have
  room for it up to build 999. The compile stamp still ends in `build 56`,
  which tuners older than this one read. `PROGRAM_VERSION` is now the release
  alone, `v1.07`. Every source file's header reads `Part of GPSDO v1.07-rt56`,
  the number being the build that last changed that file
  (`gpsdo_flash_ring_core.c` still said v1.06). Build 57 moved the number
  inside the version — see above.
- **The tuner takes both names and states the build once (build 56).** Its
  version pattern already accepted any suffix after the release, so
  `v1.07-rt56` and `v1.06-rtos` both parse and only `1.07` is compared; the
  status line drops the separate `build N` when the name already carries it.
- **Time-zone table regenerated from IANA 2026d, and the generator now checks
  its own work (build 53).** 503 zones, 88 rules, ~3.34 KB of flash — the same
  footprint as the 2026c table it replaces.

  **The three "broken rows" reported against the previous table were a false
  alarm, and the reason is worth recording.** The rule for each zone is the
  POSIX string at the end of its TZif file, which is the rule that applies
  *after the last transition the file stores* — not necessarily the rule in
  force on the day the table is generated. For a zone with a legislated change
  ahead of it those differ, and whether that is a fault depends entirely on
  which question is asked.

  Asked as *"is this rule right for calendar year 2026?"*, six zones look
  broken: British Columbia, Alberta and the Northwest Territories stop
  observing DST on **1 November 2026**, so `America/Vancouver` carries `MST7`
  and `America/Edmonton` carries `CST6` while the first ten months of 2026 still
  had PDT and MDT. Asked as *"is it right for the years this table will sit in
  flash?"*, all six are correct, and the footer is exactly the right choice —
  a table generated today should carry the era it is generated *into*, not the
  one it is leaving. Africa/Casablanca is the same story eight days out: IANA
  models Morocco as permanently UTC+0 from 20 September 2026, which is what
  `<+00>0` says.

  **Measured, with the firmware's own evaluator rather than by argument.** Every
  transition of every zone in the table, from today to the end of 2028, probed
  two minutes before and one minute after: **778 transitions across 484 zones,
  1556 probes, and the firmware agrees on every one** except the final two
  minutes of Morocco's current era. That is also the strongest test the build-52
  DST fix has had.

  The generator no longer relies on anyone asking the right question. After
  writing the table it verifies every rule against the machine's own tzdata **a
  full two years out** — the era the table will actually live in — and prints
  either "all agree" or the zones that do not. The check compares offsets rather
  than re-implementing the firmware's rule engine, on the grounds that a second
  implementation can be wrong in the same place and agree with itself. It reads
  BOTH offsets of a rule, not just the standard one, because Ireland writes its
  zone as `IST-1GMT0` — IST is the standard and GMT is a negative summer time,
  which is backwards from everywhere else and entirely legal. A check that reads
  only the first offset calls Dublin broken on every run, and a check that cries
  wolf on a correct zone is worse than no check: it is how a real complaint gets
  scrolled past. Verified both ways — silent on the table as generated, and it
  catches a wrong offset or a missing DST rule when one is planted.
- **Uniform `gpsdo_` filenames, and a repository layout a clone can read
  (build 48).** Seventeen files renamed — `dac_ext`, `flash_ring`,
  `flash_ring_core`, `live_store`, `settings_store`, `tz_table`, `ubx_timtp`,
  `TeeSerial`, `build_id` and `GPSDO_algorithms` — so every source belonging to
  this project carries a lower-case `gpsdo_` prefix and a directory listing
  separates it from what it sits on. Include guards follow the filenames.

  **Four files deliberately keep their names**, each because the cost of
  renaming them is silence rather than a compile error: `GPSDO_FreeRTOS.ino`
  (Arduino requires the sketch file to match its folder), `build_opt.h` (the
  builder looks for that name to pick up compiler flags — renamed, the flags
  are dropped and it still builds, just differently), `STM32FreeRTOSConfig.h`
  (the library includes it by name) and `TM1637Display.*` (vendored; upstream's
  filename is what keeps a future diff readable).

  `doc/`, `tools/` and `README.md` move out of the sketch folder to the
  repository root, so the sketch folder holds code and nothing else, and a
  `.gitignore` arrives with them. It excludes the build output, the Python
  caches and the PDF scratch — and, deliberately, the working material in
  `doc/`: the correspondence, audits and to-do lists name people and quote
  private mail, and a careless `git add .` should not publish them.

  `tools/gpsdo_restructure.py` does all of it. Dry run by default, idempotent,
  and it **refuses to move `tools/` while any harness still locates the sources
  by counting directories** — `hostcheck.sh`, `loopsim/run.sh` and
  `algoswitch/run.sh` said "two levels up", which is true only while `tools/`
  lives inside the sketch folder; afterwards it points at the root, and
  hostcheck would have compiled an empty tree and reported success. All three
  now walk up to the `.ino` instead, which is correct in both layouts.

  Verified by running the script on a v1.06 tree, then against the v1.07 tree
  that hostcheck already passes 14/14 — where it reports nothing left to do,
  which is the statement that its output and the verified tree are the same
  thing. One bug found doing it: the script walks `tools/`, lives in `tools/`,
  and carries every old filename in its own rename table, so the first run
  rewrote that table into a list of identities. It now excludes itself.
- **The control voltage no longer passes through `analogWrite()` (build 48).**
  Both output paths now own TIM4 through registers — `pwm24_begin()` with the
  DMA replay, the new `pwm16_begin()` without. The carrier and the resolution
  are deliberately **unchanged**: 100 MHz / 50 000 = 2.000 kHz, the same figure
  `analogWriteFrequency(2000)` produced, so no board's CT, filter or plant gain
  moves because of this.

  Two things go away with it. The core's `analogWrite()` calls `pwm_start()`,
  which recomputes the prescaler and the auto-reload from the requested
  frequency **on every call** — the control voltage is written once a second
  for the life of the board, so that was the timer rebuilt once a second to
  change one compare value, an opportunity to glitch the output every second
  bought for nothing. And `analogWriteFrequency()` is a **global** in the core:
  it applies to whichever pin is written next, so the moment a second PWM
  exists — the backlight above — the two silently fight over one setting.

  The plain path also now maps **full scale to full scale**, the same rule the
  dither table and the external DAC already follow: code 65535 is a compare
  equal to the period. The core divided by 65 536 instead, leaving the top LSB
  unreachable; the 15 ppm difference mid-range is far below what CT resolves.
- CT is two-pass on gentle plants: when the first-pass K is below
  0.12 mHz/LSB, a second three-point pass re-measures over a wider code
  spread (targeting ~0.8 Hz of swing, centred on the fitted 10 MHz code,
  never narrower than the first pass, and never closer than 1000 LSB to
  either rail). On such boards the fixed 20 480-LSB
  sweep swings the oscillator well under a hertz and the slope fit scattered
  10-50% high against DMM ground truth. The plausibility floor for the final
  K moves 0.02 → 0.01 mHz/LSB — defensible now that a coarse gate catches
  no-signal garbage before any K is believed.

### Fixed
- **`CT` and `C` restart the loop in every build (build 57).** Build 55 made
  `CT` restart the loop after centring it, but put the restart in the span
  module's `CT` hook, which a build without `GPSDO_SPAN_SENSE` compiles empty —
  so there the loop's own copy of the code, still the one from before the
  sweep, went on steering back toward it. The restart is now in `CT` itself,
  and `C`, the two-point calibration, gets the same line, because it too
  writes a new code and leaves the loop the same stale copy. In `spansim`'s
  one-calibration build (`GPSDO_SPAN_SENSE` off), a `CT` right after the span
  jumper moved: without the restart the loop steered back toward the code `CT`
  had just left, 3.1e-8 off at worst, relocked 1416 s after the move, and ran
  at 1.1e-9 RMS over the two hours after; with it, 5.0e-9 at worst, 1190 s and
  2.2e-10. A build with span sensing on behaves exactly as before: the span
  half of the `spansim` output is identical to the byte. `tools/spansim` makes
  the call where `CT` now makes it.
- **`DV` lost its third decimal at every restart (build 57).** It was stored in
  centivolts, so `DV 4.096` — the ADR4540 on Dan Wiering's V3 — came back from
  a restart as 4.10, a 0.1 % bias on the `DAC` report's commanded column; and
  the query and the echo printed two decimals, so `DV` showed 4.10 whether it
  held 4.096 or 4.10. `DV` is now also stored in millivolts, in a field
  appended to the settings block (392 → 396 bytes, no version change), and
  printed with three decimals by `DV` and the `DAC` report. The centivolt
  field is still written from the same value, so an older build reading a
  newer record keeps the two-decimal `DV` it always had, and a record from an
  older build recalls exactly as before. Tested on the host against the real
  settings store: every `DV` from 2.500 to 5.500 in 0.1 mV steps comes back to
  the millivolt; a 392-byte record falls back to the centivolt value; build
  56's store reads the 396-byte record (4.10), and build 57 reads back what
  build 56 then saved.
- **`CT` restarts the loop after centring it (build 55).** `CT` puts a new
  code on the pin, but every loop keeps its own copy of where the pin should
  be — algorithm 11's integrator, algorithm 10's absolute target — and that
  copy still held the code from before the sweep, so the loop's first steps
  after `CT` steered back toward it and undid the centring. `CT` now requests
  the same restart an algorithm change does. Measured in `spansim` with a `CT`
  that succeeds while the loop has been running: without the restart the code
  sat 42 LSB off `CT`'s null twenty minutes later, at 1.0e-10, and the next
  span move, mapped from there, landed 1.1e-10 off; with it, 1.2e-11 and
  3.7e-12. The restart lives in the span module's `CT` hook, so a build
  compiled without `GPSDO_SPAN_SENSE` — off only by choice, the shipped config
  has it on — keeps the old behaviour. Build 57 moved it into `CT` itself, for
  every build — see above.
- **`tools/loopsim/run.sh` printed the tracking sd under a phase-sd heading
  (build 55).** The report line has carried a second `sd` since the tracking
  statistics were added (`track sd 0.71 LSB`), and the table's greedy `.*sd`
  picked that one: every table the script has printed since then was the
  tracking sd in LSB under a heading promising phase sd in ns, and nothing
  looked wrong because both are small positive numbers. Anchored on the field
  before it, the README's table reproduces again (algo-12 window, `MG 0`:
  3.70 / 2.93 ns against the 3.57 / 2.95 recorded, the tree having moved
  since). Conclusions drawn from `run.sh` tables since the tracking figures
  appeared are worth a second look; the loopsim program itself was always
  right.
- **`hostcheck` could not see a missing `span_`, `algo_` or `health_`
  function (build 55).** Its undefined-symbol check looks only at names with
  the firmware's own prefixes, and those three were not on the list: with the
  definition of `span_poll` renamed away, every row still said "clean". Added
  — together with a `DEFAULT_ON` list for switches the shipped config has on
  (`GPSDO_SPAN_SENSE`), so the existing rows keep building the boards they are
  named after, and two new rows that build without it. 18 rows, all clean
  under arm-none-eabi.
- **Algorithm 11 hammered the EFC for five minutes after it was already home
  (build 54).** `s_locked` gated the phase pre-filter — `if (!s_locked) filt =
  1u;` — and `s_locked` is not a statement about the phase. It is a stopwatch:
  the lock test needs phase AND frequency inside their windows *continuously*
  for `LPF × LTC` seconds, five minutes on the defaults, so a loop that is
  already home stays formally unlocked for five more minutes and spent every one
  of them in fast, unsmoothed mode, answering each sample of detector noise at
  full gain.

  Found in the 11.09 bench capture, at the moment algorithm 13 handed over to
  11 with the phase sitting at 2.4 ns: **299 seconds of `PLL` during which the
  filtered phase never left ±6.7 ns of a 100 ns window — and the PWM moved by an
  average of 4.5 LSB per second, by as much as 17 in one second, and by more
  than 4 on 145 of those 298 seconds.** Seventeen LSB is 5.4e-10 on this board.
  The same loop, locked, over 14.6 hours: 0 or 1 LSB every second, 2 LSB forty
  times, never more. An order of magnitude of output noise out of a flag that
  only ever meant "how long has this been good for".

  The pre-filter now follows the phase instead of the stopwatch: fast while the
  phase is outside the window, smoothed once it is inside **and has stayed
  there for as long as the filter is about to average over**. That second half
  is not a spare condition — the window test alone made things worse, because a
  phase on its way *out* passes through the window too and smoothing it there is
  how a loop learns about a disturbance late (the switch harness measured a
  600 s excursion ending at 340 ns instead of 241). The dwell is `filt` itself,
  which is the only self-consistent choice: averaging over N seconds is
  meaningful exactly when the last N seconds described the same thing.

  **Replayed on the measured 26.08 plant with the board's own settings** (LTC
  60, LFD 3, LPL 100, LPF 5, LG 2.130), over the 300 s the loop spends formally
  unlocked with the phase already home:

  | | mean step | worst second | seconds moving > 4 LSB |
  |---|---|---|---|
  | before | 5.88 LSB | 22 LSB | 159 of 300 |
  | after, whole window | 0.49 LSB | 13 LSB | 7 of 300 |
  | after, once the dwell qualifies | **0.21 LSB** | **1 LSB** | **0** |

  The first twenty seconds keep the old fast behaviour by construction: the loop
  stays quick until it has the evidence to afford smoothing. Everything else is
  unchanged and was checked rather than assumed — acquisition times identical in
  all four scenarios (settled, 800 ns out, 1500 ns out, detector railed), phase
  sd identical across five noise seeds, algorithm 12 bit-for-bit, and the
  algorithm-switch harness back to the figures it printed before the change.

- **Algorithm 13 kicked the DAC one second after every restart (build 54).**
  Measured on the bench at the 11.09 handover from algorithm 11 — PWM 40835 →
  40978 → 40871, a **143 LSB single-second step, 4.6e-9 of output**, on a board
  that had been locked to a nanosecond a second earlier. Traced in the simulator
  to its actual cause, which was not the obvious one:

  The filter's first frequency measurement after a reset is an EMA of the
  **whole-hertz one-second counter**, and `P[1][1]` is still at the wide
  cold-start prior, so the frequency state moves 61 % of the way to a number
  that is mostly EMA ripple — 3.79 ns/s, which is 119 LSB of correction on this
  board. The control applied it in full; the next second it took most of it
  back. `lim_lsb` is the whole detector band and never binds on a settled loop,
  so the clamp watched it happen.

  Two changes, and the measurement says both earn their place. **The short
  control horizon no longer latches on the first in-band reading** — after one
  sample the estimate *is* that sample — but waits until the filter has had a
  horizon's worth of measurements to average, which is the loop's own timescale
  rather than a new constant. And **the correction is now slew-limited** as well
  as clamped: a clamp bounds how far it may go, this bounds how fast it may
  change, at the detector band spread over one control horizon. On a settled
  board `du` changes by far less than an LSB per second, so it never binds; the
  remainder goes into the same carry the sub-LSB path uses, so a limited second
  is delayed rather than lost.

  Worst single-second step in the first minute, 03.09 night plant, five noise
  seeds:

  | | s1 | s2 | s3 | s4 | s5 |
  |---|---|---|---|---|---|
  | before | 48 | 78 | **175** | 120 | 16 |
  | slew limit only | 33 | 52 | 28 | 63 | 16 |
  | both | **13** | **27** | **9** | **19** | **10** |

  Phase sd, dPWM, the run-long worst step and the ADEV at every tau are
  unchanged.

- **The `AP` note recommended algorithms that cannot do what it asks (build
  54).** It said "use a PLL algorithm (LA 4/5/7) to keep phase locked
  long-term" — advice that predates algorithms 10–13. Those three steer on the
  counter and have no phase detector at all, so they cannot hold the divider
  where `AP` puts it; the LTIC loops can, and they re-arm by themselves when the
  detector says the divider has lost sync. Dan Wiering picked algorithm 7 for an
  overnight run and then wondered why he was seeing arm events — algorithms 0–9
  never arm at all, and this line is the likeliest reason he was there.

- **`loopsim` learned to report what the loop did to the pin (build 54).** The
  actuator statistic was an RMS over the whole run, which averages away a
  transient that lasts one second — and a one-second step is exactly what a
  phase analyser draws as a spike. It now also prints the worst single second
  and the worst second of the first minute, where restart transients live. The
  algorithm-11 knobs `LTC`, `LFD`, `LPL`, `LPF` and `LG` are exposed as
  environment overrides so a capture can be replayed with the settings the board
  actually had, and the dump carries the applied control beside the phase.
  Every number in the two entries above came out of those additions.
- **Regenerating the time-zone table put back the pre-rename include guard
  (build 53).** The file became `gpsdo_tz_table.h` in the v1.07 restructure and
  its guard became `GPSDO_TZ_TABLE_H`, but the generator still wrote
  `TZ_TABLE_H` — so every press of the button quietly undid it, and
  `TZ_TABLE_H` is exactly the kind of generic name a second library picks too.
  Fixed in the generator and in the delivered table.
- **Every DST transition fired at the wrong moment, by the size of the offset
  itself (build 52).** `tz_offset_now()` compared the zone's transition times —
  which POSIX writes in LOCAL time — against UTC, and a comment called the
  difference immaterial for a wall clock. It is not immaterial, and it is not
  the "hour or so" the comment claimed: the error is exactly the offset in
  force at the boundary. Measured against the real 2026 instants, before:

  | zone | spring | autumn |
  |---|---|---|
  | Europe/London | exact | **1 h late** |
  | Europe/Berlin, Warsaw | 1 h late | 2 h late |
  | Europe/Athens | 2 h late | 3 h late |
  | America/New_York | **5 h EARLY** | 4 h early |
  | America/Denver | 7 h early | 6 h early |

  London's spring change came out exact only because GMT *is* UTC, and that
  coincidence is what kept this hidden: the zone the author would have tested
  is the one zone where half the fault cancels. New York is the one nobody
  could have missed — the clock jumped forward at 21:00 on the Saturday
  evening, five hours before the country did.

  There was never a chicken-and-egg to avoid, which is what the old comment
  feared. A POSIX start rule is written in local STANDARD time and an end rule
  in local DST time; each offset is known before the comparison, so each
  boundary converts to UTC by subtracting its own. No iteration, no guess. The
  comparison now runs on a day-of-year ordinal instead of a packed date,
  because the subtraction can cross midnight — Australia/Sydney starts at
  02:00 AEST, which is 16:00 UTC on the *previous* day — and the boundaries
  wrap into the year.

  **Verified against this machine's IANA data, not by argument.** Every zone in
  the built-in table was scanned minute by minute across 2026 and its
  transitions compared with Python's `zoneinfo`: **416 zones now agree exactly**,
  where before the change the same test failed on the boundary instants
  everywhere outside UTC. The five that still differ are the three bad table
  rows described in the next entry, plus Casablanca and El Aaiun, whose DST
  follows Ramadan and cannot be written as a POSIX rule at all — which the
  firmware already says out loud when you select them.

  Found because Dave (Solder_Junkie) on EEVblog asked whether `TZ London` would
  switch by itself at the end of October. It did, an hour late, and the question
  was enough to make somebody finally measure it.

- **TM1637 brightness was set to 1 of 7 under a comment claiming 5/7 (build
  52).** A bare `setBrightness(1)` buried in the display task, with no way for
  a builder to know whether a dim module was the firmware or the part. It is
  now `TM1637_BRIGHTNESS` in `gpsdo_config.h`, default 4, sitting next to
  `HT16K33_BRIGHTNESS` where somebody would look for it, and documented in the
  manual's LED-clock section.

  **The two TM1637 configurations are also compiled by `hostcheck` now, and
  never were.** That is how this survived, and it is not the only thing that
  survived in that block: the six-digit colon mask still carries a comment
  saying the value the code uses lights no colons at all. A configuration
  nobody builds is a configuration nobody reads. Sixteen configurations now,
  from fourteen. (The 20x4 LCD still has no row — it needs `hd44780` stubs that
  do not exist yet. An honest gap rather than a silent one.)

- **The tuner drew a phase estimate for algorithm 13 only; now every LTIC loop
  that has one draws it (build 52).** Algorithm 11 maintains a filtered phase
  (`s_phase_filt`, an exponential smoother whose constant is
  `time_const/filter_div` once locked and 1 while acquiring) and has printed it
  as `phase=` all along — the tuner simply never plotted it, because
  algorithms 10 and 11 shared one plot family and the family is what selects
  the overlay. They are separate families now.

  Algorithm 12's pane was worse than empty: it showed `ph`, which *looks* like
  the right field and is the raw detector reading cast to `int16_t` — the
  measurement quantised to whole nanoseconds, labelled "phase error". The top
  pane now shows `dph`, the same measurement with its decimal, and the estimate
  goes over it.

  That estimate had to be exposed, because the algorithm never published one:
  `mlacc_stats_t` gains `est_ns`, the accumulator's own answer — `last_phase`
  normalised the way the correction itself normalises it — printed on the Learn
  line as `est=`, appended at the END of the algorithm-12 fields so no existing
  regex moves. Checked on the 26.08 replay rather than assumed: across 35
  corrections the estimate tracks the true phase with correlation 0.992 and a
  best-fit slope of **1.048** (a level error would have read 0.5 or 2.0), and
  its RMS departure from the truth is 0.70 ns against 2.45 ns of detector
  noise. That ratio is the algorithm's whole claim, and it is what the two
  traces will show as a gap.

  **Algorithm 10 gets no overlay, deliberately.** The three-stage loop works on
  the raw reading and keeps no filtered phase anywhere; its smoothing lives in
  the PID integrator, which is a control state, not an estimate of anything.
  Drawing it would be inventing an estimator the algorithm does not have.

  The overlay lag is 1 sample for all three. For algorithm 13 that was measured
  by cross-correlation over a 7.5 h capture; for 11 and 12 it is taken by
  structure — the same producer, the same consumer, the same race — and says so
  in the code. A few minutes of `RH` telemetry under each would confirm it.
- **Two indicators read uninitialised memory, and a third passed a null pointer
  to `strncpy` (build 51).** Both faults were reachable on an ordinary board;
  neither needed an unusual switch combination to find.

  `set_trend(0)` — algorithm 11 and algorithm 13 both refuse to run without the
  TIC calibration, and both said so by calling `set_trend(0)`, which is
  `strncpy(dest, NULL, 4)`. That is undefined behaviour rather than a blank
  indicator, and the path reaching it is the ordinary one: it is what every new
  board does on its first run, before `LC` has ever been executed. Both now set
  `NoCT`, the word algorithm 13 already used one line further down for the
  other missing coefficient — so the state has a name, and the operator is told
  *why* the loop is holding instead of watching an indicator that may or may
  not be a residue of the last thing written there.

  `snap_c` — the display task snapshots three shared structures under a 5 ms
  mutex timeout. Two were cleared first; the control snapshot was not. A missed
  timeout is not an error — it happens whenever the control task is part-way
  through its own update — and when it happened, every consumer below read an
  uninitialised stack frame: the yellow LED took its holdover state from it,
  the status bar took the algorithm number and the lock verdict, and
  `trendstr` reached the serial report and the LCD **with no terminator at
  all**, so the report's string append copied whatever followed it on the stack
  until it happened to meet a zero byte. It now keeps the last good copy and
  falls back to that, which is the only answer that is both safe and true —
  zeroing it would be safe and would still assert algorithm 0, PWM 0, no
  holdover and an empty trend, at random, which is the kind of wrong that gets
  believed.

- **Algorithm 13 had no lock verdict at all, so the screen judged a Kalman
  filter by a frequency average (build 51).** `tft_loop_locked()` lists the
  algorithms that publish a live lock state and asks them; 13 was not on the
  list, so it fell through to the branch written for algorithms 0–9, which
  tests the 10 000 s (or 1000 s) frequency average — precisely what that
  function was extracted to stop the bar doing. Listing it would not have
  helped either: **the Kalman loop never emits `LOCK`.** Its whole vocabulary
  is `KAL` / `REJ` / `NOPH` / `ARM` / `WAIT` / `NoCT` / `NoPL`, so a trend test
  could not have matched.

  The verdict now comes from the filter, computed where the filter knows it,
  from three terms that no other algorithm here can offer:

  - **the detector spoke this second** (`s_kf_holdover == 0`, which a rejected
    reading also satisfies — a `REJ` is the innovation gate doing its job, not
    the loop losing its input). Without this a filter flywheeling perfectly on
    its own model reads as locked indefinitely, which is the difference between
    steering and coasting;
  - **the estimated phase is inside the band** — the estimate, not this
    second's reading, which is the whole point of having a filter. A loop
    pulling in from 400 ns says `KAL` every second while it does so;
  - **and the filter knows that** — `sqrt(P[0][0])` inside the same band. This
    is what makes the verdict honest at the two moments that matter and costs
    nothing at any other: at a cold start `P[0][0]` is seeded at `(range/2)²`,
    and after a picDIV arm it is deliberately reset to the same value because
    the zero the estimate referred to no longer exists.

  The band is `LAT` (`g_ltic.acq_threshold_ns`), which `LC` measures and which
  this algorithm already uses for `s_kf_ctl_fast`, for its tracking test and
  for the arm patience gate. Nothing is invented and nothing is fixed at build
  time: a board whose detector resolves better gets a smaller `LAT` from `LC`
  and this verdict tightens with it.

  **Replayed, six scenarios, the 03.09 night plant (28 680 s, `LAT` 200 ns,
  2.45 ns detector noise).** The new verdict never once claimed lock while the
  true phase was outside the band — in any scenario. What it changes:

  | scenario | old rule says locked | new verdict says locked | false green |
  |---|---|---|---|
  | settled from the start | t+1000 s | **t+10 s** | 0 s / 0 s |
  | cold start, 800 ns out | t+1000 s | t+193 s (still pulling in until then) | 0 s / 0 s |
  | detector railed | t+1000 s | t+201 s | 0 s / 0 s |
  | detector frozen at +1295 ns | t+1189 s | **never** | **14 459 s** / 0 s |

  The last row is the fault this was worth doing for, and it is not
  hypothetical — a detector frozen at +1295 ns is the failure the arm logic
  exists to catch, taken from a real capture. The frequency average cannot see
  it, because the oscillator's *frequency* is fine: it is the phase reference
  that died. For four hours of a single night the bar would have shown
  `DISCIPLINED  FIX OK` in lock green with the phase detector stuck 1.3 µs out.
  The new verdict does not light once.

  `LOOPSIM_TRACE13` now prints both verdicts side by side (`lock` and `ofrq`),
  so the next change to either can be counted rather than argued about.

- **The yellow LED went dark in manual holdover once the fix was lost (build
  51).** The OFF test read `(!fix && !hold_auto)`, which catches the one
  combination that must never be dark: the operator froze the output with `MH`,
  the LED was pulsing slowly to say so, and the moment the fix dropped it went
  out — indistinguishable from a board that had never seen a satellite, at
  exactly the moment the frozen output is the only thing holding the
  oscillator. Manual holdover outranks the fix here, as it already does in the
  status bar. Exactly one of the eight input combinations changes behaviour;
  the other seven were checked and do not move.

- **`LPOL 0` was described as "auto" in three places, and it is the opposite
  (build 51).** All three LTIC loops treat polarity 0 as *refuse to run and
  hold*, and print a line asking for it to be set. Calling that auto told the
  operator the firmware would work it out, which is the one thing it will not
  do — the "auto" that exists is `LC`, and it has to be run. `LPOL`, `LL` and
  the help text now say `not set - loop holds`.

- **Corrections were never counted on algorithms 12 and 13, and `CS` gave a
  false reason (build 51).** `counting_now()` knew how to ask algorithms 10 and
  11 whether they were locked; 12 and 13 fell through the `default:` with 0–9,
  so on the two newest loops — the two most likely to be running on a board
  whose owner cares about this number — the statistics counted nothing at all,
  and `CS` explained the blank by saying the board was "running an algorithm
  below 10", which for 12 and 13 is not true.

  Both algorithms could always answer; neither was asked. They now publish the
  verdict where they decide it (`mlacc_locked()`, `kf_locked()`), and `CS`
  names the algorithms that genuinely have no lock state — 0–9. Algorithm 12's
  flag deliberately survives a `CORR` second (a correction made by a settled
  loop is exactly what the statistic measures) and deliberately drops for the
  one second of a `ZC` jump, which cancels a slew the algorithm applied on
  purpose and is a command rather than a correction.

- **`LL` printed algorithm 10's state under every algorithm (build 51).** The
  three-stage `state=ACQ|DPLL|LOCK` is persisted, so under algorithms 11, 12 or
  13 the line reported where the three-stage loop stopped the last time it ran
  — possibly in a previous session, since the value is recalled from flash —
  printed without qualification among the live LTIC parameters. It now prints
  only under algorithm 10 and says `state=- (algo N running…)` otherwise. The
  ternary behind it was the second half of the fault: every value that was
  neither `ACQ` nor `DPLL` printed as `LOCK`, so a byte that had never been
  written claimed the most reassuring of the three states rather than the least.

- **The tip printed after `CT` named a trend that cannot appear (build 51).**
  "arm picDIV (AP) after the loop locks (trend `hit`)" — `hit` is emitted only
  by algorithms 0 and 3–8, never by 10–13, and a builder who has just run `CT`
  is running one of the latter. Whoever followed it literally waited for a word
  that could not arrive. The tip now says *once the loop reports lock* and
  leaves the indication to the display and to `CS`, which know per algorithm
  what that means.

- **Algorithm 13's holdover trend was renamed `HOLD` → `NOPH` (build 51).**
  Three unrelated states shared one word on a single screen: this one (the
  detector said nothing *this second*), the operator's or the automatic
  holdover mode printed as `[HOLDOVER]` beside it, and `SW`'s "holdover — MCU
  crystal" for a system clock running without PPS. Algorithm 12 already called
  this `NOPH`; there was no reason for 13 to name it differently and every
  reason not to. The manual's trend-word list never contained `HOLD`, so the
  documentation gets more accurate rather than less.

- **The tab report printed 0.0 for averages that did not exist yet (build
  51).** `gpsdo_calc_averages()` computes each window only once it has filled;
  before that the fields hold their initial 0.0. The human-readable report has
  always gated them on the same flags — the tab report did not, so the first
  10, 100, 1000, 10 000 and 20 000 seconds of every log carried a hard `0.0` in
  the corresponding column. Plotted, that is not a gap: it is an oscillator
  reading zero hertz, and it rescales the axis so everything after it is a flat
  line.

  Those fields are now **left empty** until their window fills. The separators
  still go down, so the column count is unchanged (22 fields, verified) and
  anything already parsing the file keeps working; gnuplot, pandas and every
  spreadsheet read an empty field between two tabs as missing data, which is
  what it is. A sentinel — `nan`, `-1`, `99999` — would have been one more
  value to explain to whoever plots it next.
- **The status bar claimed a lock it had no way of knowing about (build 50).**
  It read: a position fix present and not in holdover, therefore
  `DISCIPLINED  FIX OK`, in lock green. That is a statement about the GPS
  receiver wearing the colours of a statement about the loop. A board warming
  up, a board running `CT` with its output being swept on purpose, and a board
  thirty seconds into acquisition with microseconds of phase error all showed
  the same green bar as one that had been inside a nanosecond for an hour —
  the largest, most prominent thing on the screen was the one thing that could
  not be wrong, and was.

  The verdict it needed already existed. `tft_loop_locked()` — extracted from
  the frequency digits, where its thresholds were measured over a three-hour
  run rather than guessed — is now asked by both, so the band and the digits
  cannot disagree: green here means green there, by construction rather than
  by the same rule being written out twice.

  Four states the bar could not previously express: `ACQUIRING  FIX OK` (fix
  good, loop not yet converged), `OCXO WARMUP`, `CALIBRATING`, and the existing
  green now meaning what it says. Holdover still outranks everything, because
  a frozen output is the more important fact.

  **Both directions are damped, asymmetrically.** The lock verdict is a
  one-second thing that can flicker on a single count of counter wobble — the
  reason the digits' own threshold was widened to 0.15 Hz. Instant response was
  tried first and is wrong here for the same reason: one count of jitter is not
  a fault, and a band alternating between green and amber is worse than either
  colour. Five consecutive seconds of lock before the bar turns green, two of
  loss before it gives it up. `tools/gpsdo_statusbar.py` replays the machine
  against a scenario from cold start to unplugged antenna; on that scenario the
  old rule claimed `DISCIPLINED` for 18 of 33 seconds when it was not true.
- **A new settings field no longer costs everyone their settings (build 49).**
  `settings_recall()` required the stored record to be exactly the current size,
  so appending one byte meant a `SETTINGS_VER` bump, and a bump means the
  record is rejected wholesale — PID, LC, timezone, all of it, to gain a byte.
  The file worked around this twice by carving fields out of alignment padding
  and twice more with a hand-written `else if` per version, each tied to its own
  `offsetof`.

  It now accepts any record from `SETTINGS_V6_BYTES` (376, the size at which the
  layout was frozen) up to the current `sizeof`. The struct is zeroed before the
  read, so every field appended since reads back as 0 — which each of them
  already defines as "unset". `VS` is the first field to use it, and there are
  three alignment bytes behind it for the next two. The partial-save path takes
  the same rule, since seeding from a short record is now safe for the same
  reason.

  Verified against the target compiler rather than by eye: `dac_path` still at
  318, `bl_pct` 319, `a12_gain` 320, `dac_vref_cv` 372, `adc_vdiv_h` 374,
  `vsense_src` at 376, `sizeof` 380. A record written by build 48 loads, and
  its missing byte reads as the 5 V rail — which is what build 48 did.
- **`DV` and `AV` did not auto-save, although everything written about them
  said they did (build 48).** The v1.07 changelog, all three manuals and the
  notes sent to the people building AD5680 boards all promised it; the code
  called `cli_manual_save("ES ALGO")`. A `DV 5.00` typed and not followed by an
  `ES` came back as 3.30 after a reset, and the MISMATCH warning returned with
  it — which reads as a hardware fault rather than as a lost setting. These two
  describe the board's output stage and are typed once when it is built, so the
  promise was the right one and the code has caught up to it.

- **The `DAC` report overstated the plain-PWM path by a third (build 48).**
  `gpsdo_dac_output_bits()` returned 16 for that path, but on a build with no
  dither engine the timer resolves **50 000** duty values, not 65 536 — the
  carrier is 2 kHz from a 100 MHz clock and 50 000 is not a power of two. The
  accessor is now `gpsdo_dac_output_steps()` and returns a count; the report
  prints `N-bit` where that is exactly true and `N steps` where it is not.
  Same class of defect as the 0.094 µHz the report once offered on an 18-bit
  part, and the same fix: say what the hardware does.
- **The CT second pass could run to full code (build 47, correcting the
  second pass added earlier in this version).** The bottom of that sweep
  always kept a 1000 LSB guard; the top kept none — the high clamp stopped
  the centre from going *past* `65535 - half` rather than from reaching it,
  so a high fitted null put the top measurement point exactly on the rail.
  Dan Wiering's AD5680 board did it on 2026-09-10: null at 45 577, half-span
  20 654, centre pulled down to `65535 - half`, third point at 65 535. It
  measured cleanly there — his three points were linear to the digit — but a
  converter's output buffer is at its least linear against its own rail, and
  the calibration that sets the plant gain for every algorithm is the last
  place to spend the final LSB of range.

  `CT_RAIL_GUARD` (1000 LSB) now applies at **both** ends; on that board the
  sweep becomes 23 227 / 43 881 / 64 535. It costs nothing in swing — the
  span is still `CT_TARGET_SWING / K` and only the centring moves — and the
  60 000 span cap is what guarantees the two clamps can never collide (they
  would only meet past `65535 - 2·CT_RAIL_GUARD` = 63 535). The pass-2 line
  now reports which of the two happened, instead of claiming "centred on the
  fitted 10 MHz code" in the one case where the clamp had just moved it.
  Verified over 271 076 combinations of K and fitted null: closest approach
  to a rail 0 LSB before, 1000 after, span unchanged in every case.

- **The `DAC` report quoted a step the converter cannot take (build 46).** It
  printed a 16-bit and a 24-bit figure on every path, and there are three
  converters of three different widths behind that jumper. On Dan Wiering's
  AD5680 board both lines were wrong at once and in opposite directions: it
  offered **0.094 µHz** as a step, when 64 counts of the control value make one
  move at an 18-bit pin and the smallest real one is **5.99 µHz** — while the
  other line quoted a 16-bit figure four times coarser than the part can do.

  The control value is 24-bit on every path; what the OUTPUT moves in one write
  is the live driver's own width, and `gpsdo_dac_output_bits()` now says which:
  **24** on DITH (the dither table averages the 24-bit value exactly, by
  construction), **18** on the AD5680, **16** on plain PWM, which is the same
  property `gpsdo_dac_fine_available()` already reported. The report prints both
  numbers and names them:

  ```
    step: control 24-bit 1 LSB = 0.094 uHz = 9.36e-15
          output 18-bit 1 LSB = 5.987 uHz = 5.99e-13
          (EXT resolves 18 bits; finer requests reach the pin as a
           time average, not as one step)
  ```

  The fractional-frequency column had to learn an exponent to do it. A fixed
  `e-15` was fine while the report quoted two hard-coded widths; across 16, 18
  and 24 bits the same line carries anything from 2.4e-12 to 9.4e-15, and
  "2394.9e-15" is not a number anybody reads. `cli_frac_exp()` normalises the
  mantissa — this file prints no floats through `printf`, because Float printf
  has to be enabled in the IDE and emits "?" when it is not.

- **A calibration slope the ramp cannot have, and the eight arms it cost
  (build 45).** `LC` produces two ns/V numbers and one of them bounds the other,
  which nothing checked. `range_ns/span` is the **average** dφ/dV over the swept
  band; the anchor fit measures the **local** dφ/dV at 0.632·Vsat. On the ramp
  this detector is — `V = Vsat(1 − e^(−φ/τ))` — `dφ/dV = (τ/Vsat)·e^(φ/τ)` rises
  with φ, so an average taken over a transit that reaches past the anchor is
  evaluated above it and is therefore **larger**. For this board's geometry
  (Vsat 3.29 V, transit 0.80…3.22 V) the anchor value should be about **0.55×**
  the average. A local slope above the average is not a slope the ramp can have.

  Two calibrations of the same detector, three days apart:

  | | LNV | LZO | LRN | LNV ÷ whole-transit average |
  |---|---|---|---|---|
  | builds 16–41 | 1252.0 | 2.0809 | 3000.00 | **1.01** |
  | build 42 | 1837.7 | 2.0797 | 2958.75 | **1.50** |

  LZO agrees to 1.2 mV and LRN to 1.4 %, so the ramp did not move — only the
  slope, and only the one number fitted from the handful of points inside
  `±LTIC_ANCHOR_WIN_V`. It was measured while the phase was unstable.

  **What it broke was not the slope.** It was the saturation guard in
  `ltic_phase_error_ns()`, which sizes the usable band as
  `range_ns / ns_per_volt` — mixing the whole-transit numerator with the local
  denominator. The band shrank from **±1.318 V to ±0.886 V** about the zero.
  The picDIV lands this board's phase **1.06…1.28 V below the zero**, a fixed
  physical offset that did not move, and it was now outside the band: every
  landing read as railed. Algorithm 11's phase-capture bridge then re-armed the
  divider every 20 s — 15 s hold-off plus 5 s of stranding — eight times, at
  t = 122, 142, 162, 178, 198, 218, 238, 258 s, until one landing happened to
  fall only 0.67 V out and be accepted. The loop went PLL immediately and was
  locked thirty seconds later. **Eight interruptions of the 1 PPS output for a
  calibration artefact.**

  LC now compares the two before storing either: an anchor slope above the
  whole-transit average is reported and replaced by the average. The anchor
  itself is kept — the Vsat fit runs over the whole transit and is robust, which
  is exactly what the 1.2 mV agreement in LZO shows. On the known-good
  calibration the guard moves LNV by 1 % (1252.0 → 1239); on the bad one by
  33 %, which is the whole of the fault.

  Not fixed here: the guard in `ltic_phase_error_ns()` still sizes its band from
  `range_ns / ns_per_volt`, two quantities measured on different definitions.
  The band belongs to the anchor — `Vsat = LZO / 0.63212` gave 3.290 V and
  3.292 V across the two calibrations, i.e. the same number before and after —
  and moving it there needs the simulator taught that the ramp is exponential
  rather than linear before any constant is chosen.

- **The log printed a phase the panel was refusing to show (build 44).** Both
  display paths derive dph from the same latched voltage, but each did it down
  its own copy of the arithmetic, and they had already drifted apart once over
  the sawtooth. This time it was the BAND.

  `ns_per_volt` is a *local* slope: LC measures it in a narrow window around the
  anchor it places at 0.632·Vsat, because the ramp is `V = Vsat(1 − e^(−t/τ))`
  and an exponential has no single slope. Outside the 15–85 % window the curve
  has flattened and a linear reading is wrong. The panel has refused to print
  there for some time — it shows `ovf` — and the serial report went on printing
  a number.

  **Measured on the 04.09 11:41 capture**, which was taken for an entirely
  different question. Switched from algorithm 13 to algorithm 7, the phase
  parked at what the log called **+1085 ns** with Vphase at **2.946 V**, above
  the 2.798 V top of this detector's band. The panel had been saying `ovf` for
  most of an hour while the log said +1085 ns — and the log was not merely near
  a rail. Comparing the same board's first differences at the two positions:

  | where the reading sat | Vphase | white floor | p99 of \|Δ\| |
  |---|---|---|---|
  | mid-band (algorithm 13) | 2.08 V | **2.6 ns** | 5.2 ns |
  | parked near the top (algorithm 7) | 2.94 V | **7.7 ns** | 20.6 ns |

  **Three times the noise**, from nothing but position on the ramp — and the
  bias is in the direction that flatters, because compression means the true
  phase was *larger* than the number printed. Nothing in the log said so.

  Both paths now call one function, `ltic_display_phase()`, which carries the
  zero, the measured slope, the latched sawtooth and the band together. Out of
  band the serial line prints `dph:ovf`, the same word the panel uses. Every
  script that reads these logs matches `dph:` followed by digits, so out of band
  they now find no reading — which is the truth — instead of a plausible number
  that is wrong in a known direction. The raw `Vphase:` sits immediately to its
  left and says which end it ran out of.

  The same failure is on record twice from the other end: a rock-steady
  "+1561 ns" and a rock-steady "+1295 ns", both read as good, both costing a
  measurement before anyone noticed. **A reading that is wrong is recoverable; a
  reading that is wrong and looks calm is not.**

  Not fixed here, and worth its own look: the loop's own guard (`railed_now`)
  tests a hard-coded 3.28 V, so on a detector saturating near 2.9 V it never
  fires and the filter still acts on readings the displays now call out of band.

- Manual: the `GPSDO_DAC_EXT` row in the build-options table still claimed the
  define was mutually exclusive with `GPSDO_PWM_DITHER` — stale wording from
  before the runtime path selection existed. They compile together; the `DAC`
  command picks the live path and the jumper routes the signal.

### Rejected
- **Holding the loop output while a span position has no calibration of its
  own.** The first version of the span module did this, on the theory that the
  other position's gain — 5× off — was the greater evil. Measured instead of
  assumed (`loopsim` with the new `LOOPSIM_KALL`, three plants, five seeds
  each, algorithms 10–13, at LTC 100 and 60): a fifth of the right gain made
  every loop 2.2–6.4× worse in phase sd; with 4–5.7× the right gain algorithm
  11 did 3.6–6× *better*, 13 between 0.6× and 2.2× with 100–250 ns peaks, 12
  three to seven times worse, and 10 fine at 4× with 500–870 ns excursions
  in two runs of fifteen at 5.7×. None diverged. And `spansim` showed what the
  hold costs when `CT` cannot succeed: moved to REDUCED at power-on with `CT`
  failing for three hours, the held board sat at 3e-8 for over five hours and
  walked 570 µs of phase, where the same board left on FULL's coefficients
  locked in 58 minutes. In the normal case the two are identical — `CT` starts
  at once and the loop does not run while it sweeps — so the hold only ever
  mattered in the case where it did harm. Algorithms 3–7 cannot be driven by
  loopsim and were not measured; the module's header says so.

- **Raising the plain 16-bit PWM carrier to the dither's 12.2 kHz.** It would
  let the filter's corner rise with it — six times, two poles — and on a
  span-reduced board that is worth having. But it is already true where it
  matters: with `GPSDO_PWM_DITHER` compiled in, `dac_emit()` routes the plain
  path through `pwm24_write(code24 & 0x00FFFF00)`, so both paths already share
  one TIM4 carrier at 12.2 kHz and switching between them changes only the
  table. The 2 kHz `analogWrite()` survived only in the no-dither build — and
  that is exactly the configuration where the trade is wrong, because there the
  PWM's own width **is** the whole output: 15.6 → 13 bits takes the step from
  5.0e-11 to 3.0e-10 on a 3.3 V board. Wrong direction. Recorded at the code
  site in `gpsdo_dac.cpp` so it is not proposed again; `tools/carrier.py` has
  the numbers for both boards and all three carriers, including the dither
  table's own 6-18 Hz lines, which become the binding case above a ~16 Hz
  corner and do not move when the carrier does.

- **Arming the picDIV under algorithms 3–9, to show dph there.** Most of it
  already works — `ltic_read_fast()` runs on every pulse regardless of the
  algorithm, and both display paths are gated on LC rather than on the loop —
  so the question was only whether to arm the divider and re-arm it as the phase
  walks out. The 04.09 capture answers it, and the answer is no.

  Locked in algorithm 13, then switched to 7: the phase left ±100 ns within ten
  minutes, ran at up to **2.2 ns/s (2.2e-9)** while the LRN feed-forward was
  still gathering and the PWM swung 353 LSB, and then **parked at +1085 ns and
  stayed there**. Over the last 27 minutes its drift was `+3.1e-13 ± 1.5e-12` —
  algorithm 7 holds frequency superbly and has no mechanism whatever for the
  phase offset a transient leaves behind.

  So the cadence is not set by drift: from a fresh landing at 3e-13 the ramp
  would last weeks. It is set by *events* — every switch, re-learn or
  disturbance can spend a third of the band in minutes — and each re-arm stops
  the picPPS output for `PICDIV_ARM_MS` and brings it back displaced by the
  landing offset, which is −900…−1650 ns on this board.

  That is the argument that settles it: **the phase a frequency-only loop parks
  at is a memory of the last upset, not a property of the oscillator, and a
  monitor that re-arms to keep it on screen would make it a memory of the last
  arm instead.** It would be changing the very output whose phase it claims to
  report. Under 10/11/13 that is paid because the loop owns the phase; under
  3–9 nothing owns it.

  The diagnostic value is real and can be had without any of this: lock in 13,
  switch, log, and read the slope. It took 52 minutes and it measured what no
  counter on this board can — algorithm 7's steady-state error is three decades
  below the 1 ks average's own quantum, and at the end of that capture the 10 ks
  average still read −0.0041 Hz because it was carrying a transient from forty
  minutes earlier.

### Documentation
- Manual: new "Three paths, one node" block in the Output section — how
  `PWM`/`DITH`/`EXT` coexist, the single 24-bit command with the 16-bit view,
  and how the AD5680's 18 bits relate to the 24-bit SPI frame
  (`code18 = code24 × 262143 / 16777215`, scaling so coefficients carry over
  between paths). Prompted by build-time questions from Dan Wiering.
- Manual: `SPAN` in the command reference, `GPSDO_SPAN_SENSE` in the switch
  table, and a "Two spans, two calibrations" block in the Output section.
  `tools/loopsim`: `LOOPSIM_KALL`, the whole CT-derived set from a wrong K.
- Tuner **Help** tab: `SPAN` and `SPAN CLR` (build 56), and `SPAN` among the
  board commands in README_TUNER. The new name in the manual's banner example,
  the TFT header sketch and the tuner's status-line example.
- Manual and tuner **Help**: `DV` keeps three decimals (build 57). The
  `v1.07.57rt` spelling in the manual's banner example, the TFT header sketch,
  the tuner's status-line example and the document titles.

## [v1.06-rtos] — released 2026-09-05 (build 42)

Released as build 42. Entries landed here as they were measured, not as
they were written.

### Added

- **Manuals: Appendix D — the Kalman filter in plain words; KC documented
  (all three languages).** A new no-formulas appendix explains algorithm 13
  by intuition: the three beliefs and their uncertainty pencils, the two
  witnesses (detector and TIM2), KT-vs-KC as understanding-versus-hands,
  the scepticism machinery (4σ gate, trust test, arm silence, post-restart
  TIM2 silence), what a good night looks like and when not to touch the
  knobs. Section 4.6a now describes the split control law (`phase/KC`,
  in force after the first settle), the measured R (~2.9 ns) with the
  separate ~2.6 ns/45 s zero-wander structure, the KL adaptation ratio and
  Q water marks, and the four deliberate HOLD seconds after an arm.
- **`KC` — the controller's horizon, separated from the estimator's.** One
  number was doing two jobs in algorithm 13: the Q ceiling `R/T³` sets how fast
  the **estimator** may run, and `x0/T` in the control law sets how fast the
  **controller** nulls a phase error it already knows about. The note by the `KT`
  command said in as many words that those are different things, and then said
  separating them "needs Sg measured from the oscillator, which needs a reference
  this board does not have." That was wrong. It needs a second variable.

  **What the measurement said first.** On the 03.09 14:17 capture, settled and in
  time mode, the filter's own estimate was correlated with the detector at
  **r = 0.822 with a lag of −1 s** — the measurement cadence, i.e. no lag at all
  — and subtracting the estimate left **2.52 ns**, which is the detector's white
  floor to within 1 %. So `dph = ph + white noise`: the filter *sees* the whole
  phase error, 3.27 ns of it. And that error is slow — the 100 s average of `dph`
  still has sd **3.06 ns**, 69 % of the amplitude surviving a full horizon of
  averaging. Nothing was being mis-estimated. The controller was simply choosing
  not to correct what the estimator had already found.

  **Why the split is free.** `x0` is an estimate, not a measurement. Its own
  error is `sqrt(P00)` ≈ 1.05 ns against a 3.3 ns signal on this board, so
  nulling it quickly does not amplify white noise — the filter removed that
  already. Only `Q/R` decides how much of the detector's slow lie gets believed,
  and `KC` does not touch `Q/R`. There is a second effect in the same direction:
  the control law **books its own correction into the frequency state**, so a
  horizon that tolerates a standing phase error for 100 s biases `x1` for 100 s.
  Nulling faster removes that bias, which is why the *frequency* tracking
  improves as much as the phase does.

  **Measured**, night plant reconstructed from the 03.09 8 h capture, eight
  seeds, `KT = 100` throughout, `KC = 100` (the old behaviour) against
  `KC = auto = KT/3 = 33 s`:

  | condition | phase sd | control-error sd | r | DAC motion | Q |
  |---|---|---|---|---|---|
  | clean detector | 24.72 → **4.46** | 2.20 → **0.78** | 0.637 → 0.913 | 0.619 → 0.675 | unchanged |
  | wander 2.8 ns / 60 s *(this board)* | 15.06 → **4.01** | 1.56 → **0.59** | 0.759 → 0.945 | 0.680 → 0.862 | 9.4e-6 → 8.5e-6 |
  | wander 8 ns / 60 s | 18.98 → **8.14** | 1.89 → **1.14** | 0.688 → 0.833 | 0.817 → 1.193 | unchanged |
  | wander 12 ns / 300 s | 23.35 → **12.24** | 2.15 → **1.47** | 0.655 → 0.764 | 0.756 → 1.042 | unchanged |
  | railed cold start | settled 266 → **108 s** | same arm count | | | |

  Better on both plants, at every level of detector wander, and in acquisition —
  including the 12 ns case where shortening `KT` itself measured *worse*, because
  that route speeds up the estimator too and the estimator is what copies the
  lie. Against shortening `KT` to 40 s, which reaches the same phase sd at the
  same DAC motion, `KC = 33` leaves **Q at 8.5e-06 instead of 1.64e-05** — half
  the process noise, so half the willingness to follow the detector — and leaves
  the long averaging and the aging state that pay for holdover exactly as they
  were.

  The default is `KT/3` rather than a constant, so it scales with the horizon and
  is not a number fitted to one oscillator. The measured knee is 20–30 s on a
  board whose detector wander has `tau ≈ 60 s`; `KT/3 = 33 s` lands there. `KC`
  takes 10–10000 s, or 0 for auto, and warns in both directions: above ~60 s it
  says the loop tolerates a standing phase error and books it into the frequency
  state; below ~15 s it says the phase stops improving while the DAC moves more.
  `KL` prints the value in force.

  The flash record grows from 12 to 14 bytes and its version from 1 to 2.
  **Version 1 records still load** — rejecting them would have been two lines
  shorter and would have silently reset an operator's `KR`/`KQ`/`KT` on the one
  upgrade that had no business touching them.

### Fixed

- **The blank was one cycle short, because the count starts at the request and
  the request is not the pin (build 42).** Three was the number of rail readings
  a capture *shows*. The loop sees one more. `ltic_arm_picdiv()` only sets an
  event bit; the control task picks it up on its next wake and pulls the pin,
  holds it for `PICDIV_ARM_MS` = 1001 ms — deliberately just past a second, so
  the release lands after the edge instead of racing it — and the divider then
  syncs on the following 1PPS. Only the ramp after that is a phase.

  Build 41 leaked the fourth reading at both of its arms:

  | arm at cycle | rails at | the loop consumed |
  |---|---|---|
  | A = 101 | A+2, A+3, A+4 | **1377.0 ns** |
  | A = 661 | A+3, A+4 | **1361.5 ns** |

  Note where the rail *starts*: A+2 in one, A+3 in the other, because the
  dispatch wake is where the jitter is. Note where it *ends*: **A+4 in both**,
  because the end is set by the 1001 ms hold and the resync, which are
  deterministic. Four is therefore not three plus a safety margin — it is the
  cycle the rail actually ends on, twice.

  **Measured.** The harness could not see this either, and for a reason worth
  writing down: its arm model decremented the settle counter and landed the
  phase in the same iteration, so `ARMSETTLE=n` produced `n−1` rail cycles and a
  model asking for four gave three — exactly what the build-41 blank already
  covered, so the leak reproduced as nothing. With that off-by-one fixed and the
  transient set to the four cycles the hardware loop actually sees, twenty-four
  seeds on the night plant:

  | | settle median | settle worst | arms worst | rejects median |
  |---|---|---|---|---|
  | build 41 | 240 s | **2154 s** | 6 | 12 |
  | build 42 | **220 s** | **347 s** | 3 | **2** |

  The single leaked reading is worth **+454 LSB** of commanded correction in the
  simulator — the hardware's ~430 — and the loop then rejects the real readings
  for as long as it takes the gate to reopen around a phase estimate 2570 ns
  from the truth.

  **And the cost of being one too many is nothing.** Run against a three-cycle
  transient, where build 42 blanks one reading it did not have to: settle median
  220 s either way, worst 300 s against 297 s. Three seconds in the worst seed
  of twelve. Being one short costs a 454 LSB command and, on one seed in
  twenty-four, the whole acquisition.

- **A reading taken while the divider is stopped is not a phase (build 41).**
  Arming the picDIV stops its output and waits for the next 1PPS edge. The LTIC
  ramp goes on being sampled throughout, and with nothing to stop it, it reads
  near its top. That reading is in band, quantised, and carries a nanosecond of
  jitter — there is nothing in it for the innovation gate or the filter to
  object to. It is simply not a phase. Six arms in the 03/04.09 overnight
  capture, the three seconds after each and then the fourth:

  | arm at | +1 | +2 | +3 | +4 |
  |---|---|---|---|---|
  | t+102 | 1425.4 | 1378.0 | 1378.0 | **−1453.4** |
  | t+445 | 1437.5 | 1381.0 | 1381.0 | **−947.1** |
  | t+510 | 1437.5 | 1381.0 | 1381.0 | **−1106.4** |
  | t+575 | 1437.5 | 1381.0 | 1381.0 | **−1740.9** |
  | t+1597 | 1444.7 | 1394.9 | 1397.0 | **−1326.9** |
  | t+2202 | −1357.5 | 1378.1 | 1380.3 | **−1320.8** |

  The same three numbers every time, because it is the rail and not a
  measurement — and then the landing, where the arm model says it should be.

  **What it cost.** On the first arm of that capture the filter had just been
  reset, so the gate was open on `P00 = (range/2)²` and it took all three rail
  readings as phase. It commanded **+1653 LSB in 105 s**. TIM2 said the board
  was within **0.01 Hz** when the loop started; it was **0.50 Hz** out when the
  loop finished with it — fifty times the arm gate's own band, put there by the
  loop itself. The phase then crossed the detector at about 100 ns/s, the ramp
  railed, and the trust test convicted it, rightly: it was not following. Three
  further arms could not help, because a convicted loop does not steer and so
  cannot undo the frequency error that keeps railing the detector. It took the
  30-minute expiry to get out — **2373 s of holdover on a board that had been
  locked when it was switched on.**

  The repair is the one GLM-5.3 Max wrote for TIM2 a build earlier, applied to
  the other measurement for the same reason: **no phase reading for three
  seconds after an arm.** `raw` false is the honest description — the detector
  said nothing, which is true, and every consumer downstream already knows what
  to do with it. The landing capture then catches the fourth reading, which is
  the one the arm gate has wanted all along and has never once been given.

  **Measured.** The simulator could not see any of this, because its arm landed
  instantly — the fourth flattering model found in that file, after the perfect
  TIM2, the wrong rail voltage and the landing at zero. With the transient
  modelled from the table above, twenty-four seeds on the night plant, phase
  starting where the capture started:

  | | settle median | settle worst | arms worst | phase sd worst | rejects median |
  |---|---|---|---|---|---|
  | build 40 | 514 s | **never (28680 s)** | 51 | **9495 ns** | 89 |
  | build 41 | **218 s** | **297 s** | 2 | **0.55 ns** | 2 |

  Ten of the twenty-four seeds failed to acquire at all before the change; none
  after. Outside acquisition the change is not merely small but **bit-identical**
  — same figures to every digit on five seeds with no arm, and on the 26.08
  plant — and the frozen-detector case still convicts (50 arms either way).

- **One quantum is not the whole of the reference's noise (build 40).** The
  quantum floor added a build earlier is right in kind and short in magnitude.
  Measured on the very run it was written for, the windows immediately before
  the false verdict carried `|aexp|` up to **83 ns** — two and a half quanta,
  because the gated average dipped to −0.04 Hz. At a 32 ns floor that window
  still convicts: `amov` 20.0 ns against `0.25·aexp` = 20.7. **The verdict the
  floor exists to stop is the one it lets through.**

  The floor now comes from the reference's own measured scatter instead of its
  display step. `Rf` is the variance of `z_f` and the filter already computes it
  for the TIM2 update; the thirty-two readings in a window come from a
  hundred-second boxcar and so share almost all their content, which makes the
  accumulated noise `W·σ` rather than `sqrt(W)·σ`. On this board that is
  32 × 2.9 = **93 ns**, clearing the 83 ns event by twelve per cent and
  following the antenna instead of being a constant.

  It is a one-sigma test and deliberately so: two sigma would be 186 ns and
  would blind the frozen-detector check this whole test exists for. The cost is
  stated — a frozen detector now needs a real offset of about 0.03 Hz before it
  can be convicted — and below that there is no phase motion to miss. The
  simulator is silent on the change (steady state 3.94 ns and dPWM 1.305 either
  way), because its TIM2 is clean and `aexp` never approaches any of the floors;
  this is a hardware-only fault and the measurement above is the evidence.

- **The trust test convicted healthy detectors on TIM2's own quantisation
  (build 39).** The window's expected phase motion comes from `z_f = -100 x
  avg100`, whose gated 100 s average moves in 0.01 Hz quanta - one quantum of
  bias is 32 ns of "expected motion" over a 32 s window, above the old floor
  of `4*sqrt(2R) ~ 16.5 ns`. An oscillator parked near a quantisation
  boundary with quiet GPS then opened the test on phantom motion, a locked
  loop moved nothing, and three windows convicted the detector: 30 minutes of
  HOLD with healthy in-band readings (03.09 20:11, and already on record at
  02.09 10:59). The floor now also covers one step of the reference's own
  resolution: `max(4*sqrt(2R), trust_ns, 100 * 0.01 Hz * KF_TRUST_W)`. The
  cost is stated: a frozen detector needs real offset above one quantum
  before the test can see it.
- **KC no longer acts during acquisition (build 39).** With the controller
  horizon split (KC = KT/3), a cold arm landing half a band out commanded
  1278/33 = 39 ns/s of nulling and the boot rode the limiter through six
  rail-bounces and 116 rejects. KC now waits on a one-way latch - the phase
  inside the acquisition band on a reading the filter is using - then keeps
  it for good; `LA n` restarts re-earn it. The R-EMA moving guard and the
  frozen-detector patience follow the horizon in force, so both revert to
  their pre-split (KT) behaviour during acquisition.
- **The first TIM2 update after a reset no longer believes the power-on
  transient (build 39).** `kf_reset()` seeds P11 wide open, so the first
  update gave a stale 100 s boxcar (still carrying the oscillator's pull to
  frequency) a gain of ~0.9 and wrote a frequency the board no longer had -
  f = -29513 ps/s one second after the 03.09 boot arm, un-said through the
  limiter for nine minutes. TIM2 updates are blanked for the first boxcar
  (100 s) after a reset; the arm gate and the trust test read `z_f` directly
  and are unaffected. Arms widen P00 only and do not re-trigger the blank.
- **The innovation average carried the pull-in for hours, and the Q adaptation
  acted on it.** An arm lands the phase a thousand nanoseconds out, so the
  innovations during acquisition are of that order and their *squares* are a
  million times the steady-state value. `s_kf_ms_innov` is an EMA with a 0.001
  constant, so it needs about three hours to forget one. Reconstructed from the
  03.09 18:35 capture it peaked at **3.0e+05** and, 5100 s later on a run that
  had been quiet since t+446, still read **1.6e+03** against a true value of
  **9.1**. The firmware's own `KL` reported `ratio 31.55` on a run whose last
  thousand seconds measure **1.7**.

  That is not a display fault. The adaptation *acts* on this ratio, so **Q was
  being driven to its ceiling for hours after every boot by innovations that
  belonged to the pull-in** — which is why almost every capture in this
  project's history has shown `[at ceiling]`, and why the only run that did not
  was the eight-hour one. It is the same shape as the water-mark bug two builds
  earlier, and it hid for longer because the number it corrupts is plausible.

  The EMA now starts when `tracking` does, on the same latch as the water marks,
  and is **seeded to `S`** rather than zeroed: `S` is what a consistent filter
  expects `y²` to be, so the adaptation opens at ratio 1 and moves only on
  evidence gathered while tracking. Measured across the harness — steady state,
  clean detector, 8 ns wander, railed cold start, −1300 ns start — no change
  beyond seed noise, because the simulator's arm model is milder than the
  hardware's and its acquisition transient was never the problem.

- **The R estimator's freeze guard divided by `KT` while the control asked for
  `x0/KC`.** The guard's own comment binds it to "exactly what `u` below will
  ask for"; since build 36 that is `x0/KC`, so left on `KT` it under-read the
  commanded nulling rate by `KT/KC` = 3 at the default split — motion it scored
  as 0.4 ns/s was really 1.2 and should have frozen the EMAs. Steady state is
  unaffected because `x0` is small, but every nulling transient fed R and
  `ms_diff1` at three times the intended rate, and `KC` makes those transients
  three times steeper. Found by GLM-5.3 Max reading build 36 against that
  comment.

  Shipped on the argument: the harness barely sees it (only a frequency-error
  start moves, rejects 6 → 4), because these plants contain cold starts rather
  than the episode recoveries real GPS produces. `tools/episode_r.py` measures
  the difference on hardware — how far R rises above its pre-episode mean — and
  the build-35 baseline is **median +0.074 ns, 90th percentile +0.143, worst
  +0.306** over sixteen episodes.

  `patience` moved to `KC` with it, because its comment names the control
  explicitly. The arm gate's `horiz` stays on `KT` deliberately: it asks how far
  drift carries a landing before the loop has authority, which is not a
  nulling-speed question, and `KC` there would admit *more* arms.

- **The phase measurement and the housekeeping voltmeter shared one ADC with no
  interlock, and the bench caught it on the harmless side first.** `PA1` (the
  LTIC ramp) is `ADC1_IN1` and `PIN_VCTL_ADC` (`PB1`) is `ADC1_IN9` — one ADC on
  this part — and the core's `analogRead()` reconfigures the channel on a shared
  handle and is not reentrant. `ltic_read_fast()` runs from the PPS-woken task;
  `ControlTask` reads Vctl/Vcc/Vdd every 200 ms. Nothing stood between them.

  The visible symptom was cosmetic and exact. In the 03.09 10:08 capture the
  displayed Vctl dropped from 1.800 V to **1.620 V** seven times, for about two
  seconds each, with the PWM unchanged. Vctl is a **ten-deep moving average**, so
  one conversion returning zero drops it by exactly one tenth: 1.800 × 0.9 =
  1.620, matched to four figures, seven times. That value feeds only the
  displays, so nothing was steered by it — but it is a direct measurement of an
  ADC conversion being destroyed by the other task, and the same collision on the
  other side destroys a **phase**.

  There was a second, independent reason the read had to be atomic: **the 50 µs
  settle is a deadline, not a delay.** The ramp decays with a ~5 ms leakage
  constant, so a task switch that pushes the read out by 1 ms lands it 20 % down
  the decay — a 20 % phase error with no outward sign at all.

  Both are now closed by suspending the scheduler around every ADC access: the
  whole settle-and-sixteen-conversions block in `ltic_read_fast()` (~350 µs) and
  the three conversions in `ControlTask` (~60 µs), plus the two calibration
  refreshes and the one throwaway read at startup. **Interrupts stay enabled
  throughout** — the PPS capture, the timers and SysTick are untouched — so
  nothing in the timing path changes; only other *tasks* are excluded.

  A mutex was considered and rejected. The only correct timeout on the phase side
  is zero, because it cannot afford to wait; and a zero-timeout take that fails
  leaves a choice between racing anyway and dropping a phase measurement, neither
  of which is an improvement. Suspending the scheduler makes the cheap side yield
  to the deadline instead, which is the right way round.

  The same capture also carried what the collision looks like on the phase side:
  a lost second in the log (03:18:24 → 03:18:26), CPU 38 % on the sample that
  followed, and a single detector reading of **+1369.1 ns** — a full ramp, i.e. a
  read served against the wrong reference edge. The LTIC gate should have held it
  (the jump is 1364 counts against a 743-count threshold) and did not, because
  its second-chance branch accepts a reading that matches the previously rejected
  one — and a task starved past a PPS boundary produces exactly a matching pair.
  Algorithm 13's innovation gate caught it regardless: `rej` went 135 → 136 and
  the phase estimate never moved off −3.7 ns. Defence in depth worked; the layer
  that should have stopped it did not.

  No new telemetry is needed to confirm the repair. If it worked, the Vctl ×0.9
  dips stop appearing.

- **A night's `KL` reported `Q since start: 5.022e-06 .. 4.982e-05` and neither
  number meant what the pass conditions needed.** The water marks added one build
  earlier — so that a single dump at the end of an unattended run could answer
  "did Q ever pass its ceiling" and "did it descend during quiet stretches" —
  spanned the whole run, and during pull-in `tracking` is false, the ceiling is
  not in force, and the wide `q_seed·1e3` rail applies instead. The high mark was
  therefore six times the tracking ceiling, entirely legal, and unreadable as
  either an excursion or its absence. An offline replay of the settled hours put
  the real range at `6.4e-06 .. 1.2e-05`. The marks now start at the first
  tracking second and are never reset afterwards — a momentary loss of tracking
  is part of the night, not a new one. `KL` says `Q while tracking` accordingly.

  It is worth naming what happened rather than just fixing it: the diagnostic
  added to stop a rail going unreported was itself unreadable on its first night
  out, in the same way and for the same reason. Two builds earlier the `[at
  ceiling]` line hid the floor; here the water marks hid the regime.

- **The Q adaptation was subtracting the wrong R, and the two noise EMAs were
  named so alike that a written analysis of this filter read them backwards.**
  `R` is deliberately the **lag-16** mean square — it carries the detector's slow
  wander as well as its white noise, so the innovation gate and the Kalman gain
  treat the detector as worth what it is across the horizon the loop steers over.
  That derivation is documented at its code site and it measured: dropping to the
  white floor there is what threw away 11% of readings on the 29.08 bench and, on
  the 27/28.08 run, steered the detector's wander into the oscillator.

  It is right for the gate and right for the gain. It was wrong for the third
  thing R had quietly been given to do. An innovation at a **one-second**
  prediction horizon can only carry the white floor plus whatever the filter
  failed to track — about 6.4 ns² on this board — so it can never reach the
  8.45 ns² the lag-16 estimator reports. `Pobs = ms_innov - R` is therefore
  negative in quiet GPS as a matter of arithmetic, and the adaptation was
  information-starved by construction rather than by accident. The hold added in
  build 30 made that survivable; it did not make it informative.

  Referenced to the lag-1 floor instead, the arithmetic closes:
  `E[y²] = P00 + σ_white²`, so `Pobs` estimates the true `P00` and `Ppred` is the
  filter's own — a covariance-consistency test that Q can actually move, because
  `P00` is exactly what Q is the lever on. The gate and the gains keep the lag-16
  `R` and every protection documented there.

  The bang-bang law went with it. `×1.02` above ratio 1.2 and `×0.98` below 0.8
  has no fixed point, only two deadband edges to chatter between, and it is
  violently asymmetric in time: at ratio 1.25 it compounds to **×2.7 per minute**,
  while coming back down needs the ratio under 0.8, which cannot happen until P
  has already grown. It is now a multiplicative stochastic approximation,
  `Q *= 1 + κ(ratio − 1)` with `κ = 0.001` matched to the innovation EMA and the
  step clipped at ±0.02 so the worst-case rate is no higher than before. Fixed
  point exactly at ratio 1, symmetric, and the same ratio 1.25 now e-folds Q in
  **about an hour instead of a minute** — a GPS episode cannot ratchet it. The
  adaptation also stands down for ten minutes after any picDIV arm and while the
  gate is rejecting above 5%, on the same grounds as the `moving` guard the R
  estimator already had.

  Because the lag-1 EMA now sets the adaptation's reference rather than only
  feeding `Sf`, its input is **Winsorized**: the squared difference is clamped
  rather than the sample skipped, so a Gaussian tail is barely touched while a
  20 ns GPS step is bounded. The clamp is **9×** the running mean square, not the
  3× that looks natural — the EMA holds `0.5·d1²`, whose mean is `σ²`, while `d1`
  itself has standard deviation `√2·σ`, so clamping at `m·σ²` clamps `|d1|` at
  `√m` standard deviations. At `m = 3` that is 1.73 σ: it fires on **8.4%** of
  samples and biases the floor **down by 14%** (measured on 400 000 Gaussian
  draws) — landing squarely on the quantity the change exists to measure
  accurately. At `m = 9` it is the intended 3 σ: 0.27% of samples, 0.5% bias.

  **Measured**, before against after, twelve noise seeds per condition. With a
  detector wander matched to this board's (5 ns / 300 s), phase sd **27.4 -> 19.3
  ns** mean and **44.6 -> 25.3** worst case, control-error sd 2.13 -> 1.58 LSB, DAC
  motion unchanged. At 8 ns of wander, **21.2 -> 17.5** mean and **35.4 -> 21.3**
  worst. A frequency error of 200 LSB: mean 29.8 -> 28.0, worst **51.3 -> 34.9**.
  Acquisition — eight railed cold starts at two horizons, and a -1300 ns start —
  is unchanged. Two conditions come out worse: a perfectly clean detector (mean
  23.1 -> 24.6, though worst case improves 33.7 -> 31.3) and wander at 12 ns,
  which is half again what this board shows (17.0 -> 18.6). That is the expected
  shape of the trade: the change lets Q rise to cover wander the lag-16 `R` was
  keeping out of the gain, which is right up to the point where the wander is
  large enough that it wants its own state instead.

  And the EMAs are renamed. `s_kf_ms_diff` is now `s_kf_ms_diff16`, with the lag
  in the name and a comment at the declaration, because the pair being read
  backwards is what turned a documented design decision into a phantom "30% error
  in R" and cost a day.

  Diagnosis and the two repairs are **GLM-5.3 Max's**, from an independent
  reading of the 02.09 four-hour capture; the two constants above are this
  project's corrections to them.

- **Algorithm 13's process noise had never once adapted — it was always pressed
  against a rail, and one of the two rails was invisible in `KL`.** Three
  captures on one board, 02.09, at three different horizons:

  | KT | Q in use | which rail |
  |---|---|---|
  | 100 s | 7.777e-06 | `R/T^3` — the ceiling |
  | 40 s | 9.936e-08 | `q_seed/1000` — the floor |
  | 20 s | 7.949e-07 | `q_seed/1000` — the floor |

  Each matches its rail to four significant figures. `KL` named only the
  ceiling, so two of the three runs looked like a healthy adaptation.

  Two faults, and they hid each other. First, the floor was `q_seed/1000` and
  `q_seed` is `r_seed/T^3` — **the same T as the ceiling**. Both rails moved
  together, so changing KT slid a fixed thousand-fold window up and down instead
  of giving the adaptation any more room. The KT sweep those captures were meant
  to be measuring was measuring where a rail happened to sit: KT 40 came out
  *worse* than KT 100 (fitted loop time constant 91 s against 40–65 s) because Q
  fell 78x when the floor moved under it and the estimator's own `(R/Q)^(1/3)`
  went to 462 s. A shorter horizon produced a slower loop.

  Second, the adaptation compared the innovation mean square against `S`, and
  `S = HPH' + R`. `S` can never fall below `R`, so whenever the innovations come
  out smaller than R alone the ratio is stuck under 0.8 whatever Q does, and the
  0.98-per-second decay runs until it hits something. **There is no lower fixed
  point.** The adaptation was being asked to repair an error in R by shrinking
  Q, which is not a thing Q can do — and on this board, since the sawtooth
  pairing was fixed, the innovations *are* smaller than R: measured R is 2.9–3.0
  ns while a structure-function fit of the same captures puts the white part at
  2.35–2.65. The ratio sits near 0.77. At KT 100 it measured 0.83 and Q froze on
  the ceiling where an earlier climb had left it; at KT 40 it measured 0.77 and
  Q walked to the floor. A four per cent change in R flipped the loop between
  two opposite failures.

  The floor is now a **numerical** rail with no T in it — the value the same
  formula gives at the longest horizon the firmware accepts, with a detector at
  its quantisation limit, about 1e-12 on this board and six decades under the
  seed — so it can keep the recursion off zero without taking part in the
  answer. The ceiling keeps its T, because "do not run faster than the horizon
  you were given" is what KT means. And the adaptation now subtracts R from both
  sides and compares `HPH'` predicted against `HPH'` implied: when the implied
  value is negative the innovations carry no information about Q, so Q is
  **held** and `KL` says so, pointing at R instead. `KL` also names the floor
  now, and the held state.

  **Measured**, before against after, same tree, one condition at a time.
  Steady state with a detector walk matched to the measured one: at KT 100 phase
  sd **42.1 -> 31.6 ns**, control-error sd **3.17 -> 2.56 LSB**, correlation with
  the true required control **0.046 -> 0.319**, DAC motion unchanged; at KT 40 Q
  leaves the floor (**1.2e-07 -> 2.9e-05**) and phase sd goes 6.00 -> 5.06. With
  a clean detector at KT 100 over twelve seeds, mean **24.26 -> 23.12** and worst
  case **53.42 -> 33.73**. With 12 ns of slow detector wander, **20.8/29.2 ->
  16.5/20.4**. Every acquisition case — eight railed cold starts at each of three
  horizons, a -1300 ns start, a frozen detector inside and outside the band — is
  **bit-identical**, which is what should happen: the adaptation stands back
  during pull-in and this change is entirely inside the part that tracks.

  The repair the previous comment in this file proposed — seeding Q from a TIM2
  measurement of the oscillator's frequency walk — was checked and is not
  possible: an integer one-second count quantises at 29 ns/s and a hundred-second
  average at 2.9, against a walk of order 1e-2 ns/s. That is item 4 of
  `doc/AUDIT_algo13_model_gaps.md`, already closed there as unmeasurable. The
  seed was never the problem; the floor was.

- **The loop could re-sync the divider while the detector was reporting a
  perfectly good phase, and throw away most of the acquisition band doing it.**
  The arm test asked `!have`, and `have` is `raw && trust` — two different
  faults collapsed into one question. A RAILED detector (`raw` false) is saying
  nothing at all, and re-syncing the picDIV is the only repair there is. A
  DISTRUSTED one (`raw` true, `trust` false) is still reporting a phase, and if
  that phase is inside the acquisition band then arming repairs nothing: it
  discards a usable reading and lands somewhere between -900 and -1650 ns.

  On the 02.09 10:59 capture that is exactly what happened. At t+593 s the
  detector read **-53 ns** at `Vphase 2.041 V` — healthy by any measure — but
  the trust test had dropped it eight seconds earlier and the loop was in
  holdover. The branch counted its five seconds and armed. The phase went to
  **-1222 ns** and the run needed another seven hundred seconds to come back:
  three arms and **1317 s** to settle, against one arm and **309 s** the night
  before on the same board.

  The branch now arms only when there is nothing to lose — no valid reading at
  all, or a valid one already outside the band, which is the frozen case where
  a re-sync *is* the repair. **Measured** against the same tree with the one
  condition removed: eight railed cold starts, steady state, a 12 ns/300 s
  detector drift, a -1300 ns start offset and a 200 LSB frequency error all come
  out bit-identical, and a detector frozen outside the band still arms fourteen
  times. Only a detector frozen *inside* the band changes — fourteen arms become
  none — which is the case the change is for.

- **The TIM2 measurement was lagged and over-trusted; fixing both halved the time
  to acquire.** Two faults in one place, and `loopsim.cpp` had described the
  second of them about its own plant model since 26.08 without anyone telling the
  filter.

  `Rf` was estimated from the differences of adjacent `avg100` readings. Those
  are boxcars sharing ninety-nine of their hundred samples, so their difference
  is about a hundredth of the single-sample noise and the estimator came out two
  orders of magnitude too small — the `Rf >= 1` floor was quietly doing all the
  work, and 1 (ns/s)² is itself about eight times too optimistic. It is now built
  from the ONE-SECOND counter's own measured scatter divided by the number of
  samples the average in use contains: no constant, and a noisier PPS or a worse
  antenna shows up in it directly.

  And a hundred-second boxcar is not a measurement of the frequency now — it sits
  about fifty seconds behind, which is exactly when the frequency was different if
  the loop has been correcting. The loop books every correction it makes, so it
  knows the commanded change over that window: the measurement is now predicted
  as `x1 - du/2` rather than `x1`. A correction, not an inflation, because the
  number is known rather than merely bounded.

  **Measured** over sixteen railed cold starts (four frequency offsets, four
  seeds): mean time to settle **1802 -> 923 s**, worst case **4615 -> 2727 s**,
  with far fewer picDIV arms. Frozen-detector tracking improves six-fold (track
  sd 978 -> 169 LSB). Steady state is a wash. This is item 3 of
  `doc/AUDIT_algo13_model_gaps.md`, which was ranked third of five and turned out
  to be the largest single improvement of them.

- **Algorithm 13 could fail to acquire at all, and the simulator could not see
  it because its picDIV model was flattering.** The 01.09 21:00 capture never
  locked: eleven minutes, four arms, 460 s of holdover, 94 s of gate rejections,
  eleven seconds of normal operation. `Sf` had nothing to do with it — `R` never
  left its seed, so `Sf` was identically zero the whole run.

  **Where an arm actually lands.** Seven arms across the three captures of 01.09,
  phase read in the second after each: `-1554 -1431 -899` (21:00), `-1641 -1441
  -943` (17:12), `-927` (10:49 — the one run that then worked). Every one
  negative, none near zero, -900 to -1650 ns against a band of ±1500. The
  simulator modelled the landing as `gauss(300)` — a few hundred nanoseconds
  either side of zero — so every simulated arm recovered and every acquisition
  test passed. That is the third flattering model found in `loopsim.cpp`, after
  the perfect TIM2 and the wrong rail voltage. It now lands where the hardware
  lands, and it can rail again afterwards, which the harness also never modelled.
  `LOOPSIM_ARMOFS` / `LOOPSIM_ARMSD` override the two numbers.

  **Why the old gate could not work.** It asked only that the frequency error not
  carry the phase across half the band during the 60 s hold-off — `arm_hz =
  range/(2·100·60)` = 0.25 Hz — which spends the entire budget on drift and
  leaves nothing for the landing offset, and the landing offset turns out to be
  most of the band. The 21:00 arms passed that gate at +0.24 and +0.17 Hz: 24 and
  17 ns of phase per second, enough to walk a -1450 landing off the ramp in
  seconds. The gate now asks the question that matters — *given where this
  divider lands, will the phase still be readable one horizon from now?* — with
  the landing measured from the last arm and the drift from TIM2, so neither term
  is a constant anyone has to guess. It also stops refusing a large frequency
  error that happens to push the phase back toward the middle. An escape after
  ten horizons blind keeps a gate that can refuse forever from doing so.

  **And the filter was treating its own actuator as exact.** `s_kf_x1 +=
  polarity*applied/lsb_per_ns` hands the filter the correction as fact, with no
  covariance attached, and `lsb_per_ns` comes from `CT`, which is a measurement
  like any other. A few per cent of error there accumulates in `x1` and nothing
  ever takes it back — and the bias has a stable home, because `u = -(x1 + x0/T)`
  commands exactly nothing whenever `x1 = -x0/T`. The loop parks at a constant
  phase offset with zero innovations and no measurement disagreeing with
  anything. Seen in the simulator parked at -100 ns for 1500 s with `x1` = +1.0
  ns/s — a 0.01 Hz error, exactly TIM2's resolution, so the second measurement
  cannot see it either — and on hardware as the 01.09 run that took 3600 s to
  bring 21 ns down to 7.5 ns while reporting "already nulling" (TODO 80), and as
  every standing offset this loop has ever shown. Five per cent of the applied
  correction, squared, now goes into `P11`: the filter keeps enough doubt about
  its own frequency for the phase measurement to pull it back.

  **Measured**, sixteen railed cold starts (four frequency offsets, four seeds),
  on the honest arm model: mean time to settle **2316 -> 1802 s**, worst case
  **7127 -> 4615 s**, and far fewer arms in every case that changed. The locked
  loop is unchanged to two decimal places on every steady-state figure across two
  plants, five seeds and three levels of detector wander — both repairs are inert
  once the loop is in.

  `loopsim` now reports **time to settle**, which is the metric acquisition
  changes actually move; phase sd over a whole run scores a loop that arms early
  and badly the same as one that waits and arms well.


- **Algorithm 13 was running half a clock model, and that half was the ratchet.**
  The two-state clock model every timing text uses carries *two* process-noise
  densities — `Sf`, the white frequency noise (`h0`) which appears as a random
  walk in phase, and `Sg`, the frequency random walk (`h_-2`):

  ```
         | Sf*t + Sg*t^3/3   Sg*t^2/2 |
    Q =  |                            |
         |    Sg*t^2/2        Sg*t    |
  ```

  This filter injected `Q/3`, `Q/2`, `Q`, which at `t` = 1 s is `Sg`'s three
  terms exactly, with **`Sf` identically zero** — not a knob set to zero: it had
  no name and nothing measured it. The consequence is the whole story of the last
  week. With no `Sf`, the only way to explain *"the phase moved more this second
  than I predicted"* is to raise `Sg`, that is, to conclude the OSCILLATOR'S
  FREQUENCY is wandering fast. Short-term phase noise, a wandering detector zero,
  a lagged counter reading: all of it was booked as frequency random walk, which
  raises `K1`, the gain that writes the frequency state, which is what the DAC
  follows. The `Q` ratchet was not a bug in the adaptation. It was the adaptation
  doing the only thing the model left it.

  **Measuring `Sf` needs two lags, and the second is nearly free.** Over a lag of
  `k` seconds the phase differences carry `0.5*E[dp^2] = sigma_R^2 + Sf*k/2` —
  white noise, which does not grow with `k`, plus a walk, which does. The history
  for the lag-16 `R` estimator was already there; a lag-1 estimator alongside it
  separates the two. It is the same `floor` / `slow` split `tools/logab.py` has
  been printing all along and the filter was never given.

  **`Sf` is measured, not adapted**, so unlike `Sg` it cannot ratchet — that was
  the point.

  The first version of this clamped the difference at zero and was wrong, in a
  way worth recording: both estimators are EMAs with `alpha` = 0.002 of a squared
  Gaussian, so each carries about 4.5% standard error and their difference about
  6.3%, and clamping a noisy signed quantity at zero rectifies it into a positive
  bias of roughly 0.4 sigma. On a perfectly white simulated detector, where the
  honest answer is zero, `Sf` came out 1.5e-2 ns²/s — the predicted bias almost
  exactly — and it cost 60% on phase sd and a factor of two on ADEV at tau 1024,
  because a fictitious `Sf` explains the innovations, the adaptation then starves
  `Sg`, and `Sg` is what lets the filter follow a drifting oscillator. The
  difference must now clear two sigma of its own noise (0.126 of the estimate)
  before it counts as one, and that threshold is derived from the EMA constant
  rather than chosen.

  **Measured**, two plants, five seeds. With a clean detector, where `Sf`
  correctly reads zero, everything is unchanged; the railed, frozen, cold-start
  and `KT`-sweep cases are unchanged to the last digit. With 12 ns of detector
  zero wander — which is the condition both real boards are actually in, `R` 6.3
  ns of which about 5.9 ns is wander — short-tau ADEV improves **1.7x**
  (3.11e-11 -> 1.81e-11 at tau 16) and the loop tracks the oscillator's true
  trajectory materially better (track sd 1.26 -> 1.14 LSB, applied-against-
  required correlation 0.913 -> 0.928; on the second plant 1.29 -> 1.15 and
  0.461 -> 0.529). The costs are 33% more DAC motion — still an order of
  magnitude below where this started, and it is useful motion, since short-tau
  ADEV improved with it — and about 8% on ADEV at tau 1024.

  `KL` prints `Sf` next to `R` and `Q`. Zero there means the two lags agree,
  which on a clean detector is the right answer.

  This is item 1 of `doc/AUDIT_algo13_model_gaps.md`. Items 2 to 4 — the detector
  zero as a state, the lagged and over-trusted TIM2 measurement, and a `KT` that
  the rubidium-referenced data says is five to ten times too short — are still
  open, and `KT` still cannot be raised until the `Sg` seed stops scaling as
  `1/KT^3`.


- **The `Q` ceiling stood back during acquisition, because otherwise it was
  tightest exactly when the filter needed room.** `R` starts *at* its seed
  (`kf_reset` seeds the difference estimator with `r_seed`) and `q_seed` is
  `r_seed/KT³`, so on the first second `R/KT³` **is** `q_seed`: the new ceiling
  landed on the seed and left the adaptation no upward room at all until `R` had
  been measured. Acquisition is where a wide `Q` earns its keep — a phase a
  thousand nanoseconds out needs a filter that can move.

  The 01.09 17:12 capture is what raised it: a picDIV arm landed the phase at
  -2124 ns, outside the detector band, and the run took five arms, 897 railed
  seconds and 3300 s to settle while throwing away up to **70% of its readings**
  at the innovation gate — against one arm and 900 s the run before. The
  simulator does not reproduce that start (arming lands cleanly there), so how
  much was this and how much was the arm's own dice roll is not settled; a
  ceiling that collapses onto the seed at boot is wrong either way.

  So the horizon bound now applies only while the loop is tracking: the phase
  inside the acquisition band **and** `R` with at least one EMA time constant of
  real measurement behind it. Until both hold, the adaptation runs against the
  wide sanity rail as before. Nothing is lost — the ratchet this bound exists to
  stop is a steady-state fault that needs hours of quiet tracking to develop.
  Steady state is unchanged to the last digit across two plants, five seeds and
  three levels of detector wander; the frozen-detector case improves sharply
  (phase sd 347k -> 55k ns, end frequency -0.50 -> +0.31 Hz, gate rejections
  11.2% -> 0.8%).

  Standing back is not the same as letting go, and the first version of this got
  that wrong: with the tight ceiling suspended and no unconditional one behind
  it, a 1.02-per-second ratchet reached `Q` = 5.4e+18 in under two hours on the
  frozen-detector plant — the loop never reads "tracking", so the only bound left
  has to be an unconditional one. The wide rail is now applied in both branches.


- **The CPU reading in the TFT header is back on the centre line of the bar.**
  Fixing the clipped `%` moved it: the field was centred in the gap between the
  product name and the *padding* the LMT clock reserves, and `HDR_LMT_PAD` is far
  wider than the clock's glyphs, so the whole string sat about 23 px left of
  centre on the 480 panel. The padding was never the obstacle — both draw paths
  put CPU down last, so nothing can erase it afterwards, and the only thing it
  must not touch is the clock's actual glyphs. Measured against those instead,
  the free space is symmetric (the product name and the clock are both sixteen
  characters), so the centre line fits with about 30 px to spare on each side and
  that is where it goes. The erase band is now sized to the widest reading the
  field can show (`CPU 100%`) rather than to the whole gap — sizing it to the gap
  was what forced the text off-centre in the first place. The centre is used only
  when the measured widths say it fits; otherwise the gap centre is kept as the
  fallback, and if even that is too small nothing is drawn, so no arrangement of
  fonts or panels can bring the clipping back.


- **`KL` now says so when `KQ` is pinned.** The header line already distinguished
  the two by omitting `(adapt)` after the value, and that was not enough. The
  01.09 capture showed the loop moving the DAC six times less than the week
  before, which read as the new ceiling on the `Q` adaptation doing its work —
  and it was not: `KQ` had been pinned at the seed during an earlier experiment
  and recalled from the flash ring on every boot since, so the adaptation had
  not run at all. A setting that survives reboots and changes the meaning of
  every other number in the report needs a line of its own, so it has one.

- **Algorithm 13 over-actuated: `Q` ratcheted up on correlated detector noise
  until the filter was five times faster than its own horizon.** The bench had
  been showing the loop moving the DAC 1.80 LSB per second against algorithm
  11's 0.49 on the same night, with no better phase for it — and split into fast
  and slow components it was per-second jitter, 3.7x, not slow wander.

  The cause is in the `Q` adaptation, and it is a one-way ratchet. The filter
  raises `Q` whenever its innovations come out bigger than the covariance
  predicted. When the detector's error is *correlated* — a zero wandering over
  minutes — they do, for a reason that has nothing to do with the oscillator, so
  `Q` climbs at 1.02 per second until `P` and `S` have grown enough to explain
  them. It stops, but it stops high: `KL` on 30.08 read `Q=1.238e-03` against a
  `6.36e-06` seed. 195x in `Q` is `sqrt(195)` = 14x in the gain that writes the
  frequency state, and that gain is what the DAC follows.

  A SECOND BOARD SETTLED IT. Dan Wiering ran the same firmware on his own board
  with nothing touched beyond `CT`, `LC`, `SAW 1` and `ES LTIC`, and it went the
  whole way: `Q` was against the old 1000x rail at `4.468e-03` within 2h37m of
  boot and still there nine hours later, the DAC moving **11.05 LSB/s** and
  applying a **249 LSB span where the oscillator needed 19.8**. Measured against
  a rubidium standard — the independent reference this project does not have —
  ADEV at 20 s was **8.6e-11 against 3.1e-12** for algorithm 11 on the same board
  and the same reference, a hump peaking, as it must, at the filter's own time
  constant. Same defect, four times the size, on hardware that had never been
  tuned by hand.

  `(R/Q)^(1/3)` has the units of time and is the filter's own time constant, so
  `Q = R/KT³` says exactly *run as fast as the horizon you were given*. The
  adaptation is now bounded there: **the filter may not run faster than `KT`**,
  with `R` taken as measured rather than as seeded. That last part is not a
  detail. The first version of this bound was eight times the seed, and it
  worked — but only by luck, because `q_seed` comes from the a-priori
  2.5-quantum guess and how far a real detector sits above that is a property of
  the board: this one measures 2.5x its seed and Dan's 6.8x, so the same
  multiplier meant tau >= 92 s here and >= 95 s there, and would have meant
  >= 50 s on a board whose detector matched its seed. Taken from the `R` in use
  it says the same thing everywhere, and it follows a re-run of `LC` within a
  second. Measured over two plants, five noise seeds and three levels of detector
  wander: short-tau ADEV twice better (7.99e-12 against 1.54e-11 at tau 16), DAC
  motion 2.4x lower, against 7% on phase sd and 25% on ADEV at tau 1024 — and the
  phase sd is measured against the detector, which is the instrument that lies
  here, while a rubidium is not. Pinning `Q = 1.238e-03` by hand reproduces the
  bench figure in the simulator, 1.07 LSB/s, which is the confirmation the log
  alone could not give.

  Nothing is lost by refusing to let `Q` absorb correlated detector error: `R`
  already carries it, being measured from differences taken near the horizon. A
  value pinned with `KQ` still passes as given; the bound is on the adaptation.
  A faster loop is now asked for the honest way — shorten `KT`.

  AND THAT CUTS BOTH WAYS, which is worth stating plainly. All of the above is at
  the default `KT` of 100 s. At `KT` 300 and 1000 the loop runs into this ceiling
  and stays against it (phase sd 0.64 -> 2.82 ns at 300, 3.04 -> 47.6 ns at 1000),
  because `Q_seed = R/KT³` falls as the cube of the horizon while the
  oscillator's real walk does not move at all — past a few hundred seconds the
  seed stops estimating anything, and the old 1000x rail was quietly correcting
  it. That is now visible rather than hidden: `KL` prints `[at ceiling]` beside
  `Q`, and a loop sitting there for a whole run is saying *your `KT` is longer
  than this oscillator supports*. The repair, when it comes, is to seed `Q` from
  the oscillator instead of from the horizon — TIM2 can measure the frequency
  walk directly and its noise is independent of the detector's zero. Until then
  `KT 100` is the measured configuration. `KL` gained the `[at ceiling]` flag in
  the first place because this fault was found by grepping a capture for that
  number, which is not a diagnostic anyone should need twice.

- **Algorithm 13's frequency gate could latch shut, and it took holdover with
  it.** Found while checking the above against a frozen detector: the trust test
  does its job and drops the detector, and from there TIM2 is the only
  measurement left — but by then the oscillator is a hertz out, the innovation
  is 100 ns/s and the gate's limit is 4 ns/s, because `P11` has nothing but `Q`
  to grow on. Every reading refused, the frequency state stuck at zero, and the
  loop riding a model that says all is well while the phase runs off. The one
  case this measurement exists for was the one case it could not act in.

  The phase gate's escape — widen the covariance by the refused innovation and
  let the next one in — was tried here first and measured far worse: it puts the
  gain at nearly one, so the state snaps to a measurement that is a 100 s boxcar
  and therefore fifty seconds behind, and the loop chases its own lag (+10.2 Hz
  and 7795 LSB/s, against -1.04 Hz and 0.06 LSB/s for refusing outright). That
  works for the phase because the phase is instantaneous; this is not. So the
  gate now **clips** instead of opening: after ten refusals in a row the reading
  is accepted, but only four sigma of it, and the state walks toward the truth
  at a bounded rate. Frozen detector: -0.49 Hz and 1.9 LSB/s, the best of the
  three. Every healthy case — railed detector, 400 LSB cold start, clean run,
  both plants, all seeds — is bit-for-bit unchanged.

### Changed

- **The tuner's status bar now shows the whole firmware identity, and `V`
  answers with it.** It used to say `connected — firmware v1.06` and stop there,
  which names the protocol but not the binary. It now reads

  ```
  connected — firmware v1.06-rtos  build 27  2026-09-02 09:46  CRC 78B08D26
  ```

  The compile stamp and build serial existed already but only in the boot
  banner, which has usually scrolled past by the time anyone connects. The
  sketch now composes that stamp once into a shared string — it has to be the
  sketch, since `__DATE__` is baked into whichever translation unit mentions it
  and the sketch is the only one `build_id.h` forces to recompile — and both the
  banner and `V` print the same characters rather than two that can drift apart.

  All three facts are there because they answer different questions: the version
  says which protocol the tuner is talking, the build and timestamp say which
  source tree it came from, and the CRC says which binary is actually running.
  Only the last cannot be stale, which is the whole reason it exists: on 26.08
  two captures from two different builds carried the same timestamp and cost an
  hour of arguing with a log that was right.

  Everything past the version is optional in the parser, so an older firmware
  that answers `V` with the name alone still connects and the line simply says
  less.


- **`KT` above 200 s now warns, because the loop cannot serve both ends.** `Sg`
  is seeded and capped at `R/KT³`, so a long horizon forces the *estimator* to be
  as slow as the *controller* — and those are different things. Removing the cap
  fixes `KT` 1000 completely (phase sd 50.2 -> 2.92 ns, settling immediately
  instead of after 11120 s) and brings the ratchet straight back at `KT` 100
  (`Q` to 1.12e-3, ADEV at tau 16 from 8.05e-12 to 4.08e-11). Freezing the
  adaptation while the loop is commanding — the guard that works for `R` — does
  not help either: the ratchet develops in the quiet state too. Separating the
  two needs `Sg` measured from the oscillator, and `Sg` is 6.4e-6 (ns/s)²/s
  against a TIM2 noise floor of 8 (ns/s)² — six orders of magnitude below what
  this board can see. So the cap stays, `KT 100` remains the measured
  configuration, and the firmware says so when you set anything longer.

- **The detector zero as a fourth state was built, measured, and left out of the
  tree** — kept whole in `doc/algo13-zero-state.patch`. It does what it was
  designed to do: with 12 ns of zero wander the DAC moves 32% less and short-tau
  ADEV improves 1.6x. It also holds the true phase worse (sd 12.80 -> 13.90,
  track sd 1.13 -> 1.24, `r` 0.929 -> 0.917, ADEV at 1024 +17%), because `x0` and
  the zero are nearly degenerate — the phase measurement sees only their sum, and
  the only thing separating them is TIM2 at about 8 (ns/s)². Time constants of
  1x, 2x, 5x and 10x `KT` were tried. Both losing metrics are measured against
  the phase reading, which is the instrument the change is about, so this
  simulator cannot settle it; an independent reference measuring the output would,
  in one night.



- **Algorithm 10, LOCK: the deadband is gone, replaced by algorithm 12's own
  construction.** The old LOCK treated any phase error below `range_ns/40` as
  zero — 47 ns on this board — with a soft knee above it, and the loop duly
  parked at a standing +76 ns for ninety minutes. That offset was not a fault in
  the loop; it was the specification. Removing it and acting on the plain mean
  of the interval was worse (phase swept ±500 ns, both detector rails), because
  a mean over H seconds is the phase as it was H/2 ago and this stage already
  updates only every H.

  Algorithm 12 does not average. Its test `(a+b) + 2*(b-a)` keeps two adjacent
  half-windows and EXTRAPOLATES to the end of the pair, so the averaging and the
  lag cancel by construction — and the same pair yields the slope, which is a
  measurement of the frequency error that LOCK otherwise cannot see at all
  (`Kp` is 0 here, so the TIM2 term is identically zero). LOCK now keeps that
  pair, gates the phase on the standard error of the newer mean and the slope on
  the standard error of a difference of two means, and feeds the slope into the
  integrator as an absolute PWM correction. No threshold is assumed anywhere:
  sigma comes from first differences of the detector, the same estimator
  algorithm 12 uses.

  Measured on 21.08 at `LIV 30`, 25 min in LOCK with no drop: phase RMS
  **6.9 ns** after settling (mean **+2.2 ns**, −14.3…+19.1 ns), 31 corrections,
  median step 2 LSB, largest 9. Overlapping ADEV matches the 23 h algorithm 12
  record to within a few percent at every tau out to 128 s
  (3.5e-9 at 1 s, 1.0e-10 at 128 s) — which is the point of the change: the
  three-stage loop now holds phase as well as the accumulator does, on the same
  hardware, and with a gentler control effort.

- **LOCK cadence is bounded by the drift the loop has just measured.** Never let
  the phase travel more than two sigma of its own noise between updates, using
  the slope the pair already yields. This only ever SHORTENS the interval, and a
  board with no resolvable drift keeps the full `lock_interval_s` it was given,
  because the bound is a division by a slope that reads zero. Swept in
  simulation: at `LIV 300` and the drift measured on this hardware, phase RMS is
  90 ns at eight sigma, 50 at four and 34 at two, against 100 for the old
  deadband; the loop that looks more often has less to undo each time, so the
  individual PWM steps get SMALLER, not larger (about 10 LSB at two sigma
  against 17 at eight). `LIV 30` remains the best setting measured and needs no
  bound at all.

- **Bumpless DPLL↔LOCK transfer.** The integrator is the absolute PWM target in
  this loop (`u = integ - pwm`), so on a stage change it is now re-seeded to what
  was just written. Measured across the transition on 21.08: 40853 → 40854, a
  one-LSB step where the phase had previously been jerked by wherever `integ`
  happened to have wound.

- **Vcc is shown to three decimals** and the `dph` row is labelled `dp:` on the
  320×240 panel; `qE:` is expanded to `qEr:`. All three touched rows now tile
  the 168..314 px span exactly.

- **Algorithm 10's ACQ was unconditionally unstable — the gain was five times
  the stability boundary.** ACQ updates every 5 s but steers on `avg100`, a
  SLIDING 100 s boxcar, so a correction cannot reach the measurement for up to
  100 s while the loop keeps acting twenty times inside that window. The gain
  was half the plant (`acq.Kp = 0.5 * lsb_per_hz`), so it applied about ten
  times what was needed before the measurement could answer.

  Measured 25.08 21:06, a cold ACQ entry with the OCXO already inside 0.02 Hz:
  PWM swung **31229..51512** — twenty thousand LSB — Vctl 1.38..2.15 V, the
  runaway guard fired twice, and the run ended parked at **+2.53 Hz** with the
  PWM frozen for its last 527 s. The simulator reproduces it from the shipped
  constants alone (PWM ±13319, ending at 2.79 Hz) and diverges even from a
  one-LSB start, which is what "unconditionally" means here.

  The bound is `Kp * K < 2 * period / window` = 0.10. Swept in simulation from
  a 0.02 Hz start: 0.50 diverges, 0.25 crawls, 0.10 is the edge (334 s), 0.05
  settles in 167 s. `acq.Kp` is now **0.05 × lsb_per_hz**, a factor of two
  inside the boundary, and it converges monotonically from 1 LSB, 0.02 Hz,
  1 Hz and 3 Hz starts with no overshoot at all (peak excursion 0.95× the
  starting offset, settled within 603 s in the worst case). It scales with the
  board's own measured K, so it carries to any OCXO.

  This only ever bit a COLD ACQ. `g_ltic.state` is persisted, so a warm start
  resumes in DPLL or LOCK and never runs the path — which is why an algorithm
  shipped in v1.04 took until now to show it.

- **ACQ centring is gated on "not railed", not on "in band" — and the error is
  clamped to the band instead of being discarded with it.** One term, two
  opposite failures. Gating it on the full band test stopped the 20.08 shove
  (Vphase 3.187 V against a band of 0.818..2.865 V, err_v +1.35 V, 690 LSB
  swept), but it also switches the pull-in off whenever the band LC recorded is
  narrower than the detector really is — and on this hardware it is a fifth of
  it. Measured 25.08 with the ACQ gain fixed: after the picDIV arm the phase
  parked at -1320 ns, i.e. 1.583 V against a recorded band of 1.729..2.433 V.
  Outside the band, so no centring; frequency already on target, so no
  frequency term; `u = 0`, PWM frozen, and ACQ→DPLL needs |phase| ≤ 200 ns. A
  permanent stall with every guard quiet, because nothing was wrong except that
  the loop had switched itself off.

  What is meaningless outside the band is the MAGNITUDE of `V - centre`, not
  its sign: the ramp is monotonic up to the rails, so the reading still says
  which way home is, and that is all ACQ needs — it is a bounded proportional
  nudge that integrates nothing. It now steers whenever the reading is off the
  rails, with the error clamped to the band edge: unchanged in band, a
  band-edge pull outside it rather than a 1.35 V shove, and holding at the
  rails as before. DPLL and LOCK keep the strict test — they integrate the
  phase, which is what the gate exists for.

- **The tuner showed a stale loop state on the algorithms with their own
  vocabulary.** `parse_state()` scanned the whole line for a fixed list of
  words, and the list could not keep up: the firmware emits `SYNC`, `FLL`, `ZC`,
  `HYB`, `NoPL`, `hit` and ten direction words like `uf+` that were never in it.
  On those seconds it returned nothing and the label went on showing whatever it
  had last recognised — under algorithm 12 the panel read **LOCK through every
  CORR and ZC second**. `[HOLDOVER]` never matched either, so holdover was
  invisible. The search was not anchored, so any of those words anywhere in any
  line could set the state.

  The trend is a FIELD, not a vocabulary: it is the last token of the `PWM:`
  line, and in holdover the firmware replaces it outright. Taken by position, a
  word the firmware invents tomorrow is displayed verbatim instead of being
  dropped, and nothing else on the wire can be mistaken for it. `___` now clears
  the label instead of leaving the previous word standing — the caller tested
  truthiness where it had to test `is not None`, which was the same stale-display
  bug one level up. Fifteen cases checked against the firmware's actual output.

- **The panel's `dph` gained the decimal the log has always had.** The serial
  report prints one decimal and the panel printed whole nanoseconds; invisible
  while readings were hundreds of ns, glaring once the loop settled to single
  digits — the log said −5.2 and the panel said −5. The decimal could not simply
  be added: both phase fields are sized to the string `+0000ns`, and the 320
  panel's own comment warns that a five-digit reading would overrun the label.
  So one decimal below 100 ns, where the widest form `-99.9ns` is exactly the
  seven characters the field was measured for, and whole numbers above. Checked
  across the whole detector range: the widest string either format can produce
  is seven characters, so no pad needed re-cutting. `dtostrf`, not `%.1f` —
  this file uses no float conversions in `snprintf` anywhere, deliberately,
  because they print `?` without Float printf enabled in the IDE.

- **The tuner's Help tab caught up with the firmware.** `DAC` now documents the
  path argument; `LTO`/`LTR` say volts; `MR`'s default is 7, not 9; `AQI`/`AQD`
  are marked stored-but-inert with a pointer to `ACG`; the algorithm-12 trend
  list covers the whole vocabulary rather than three of it; `SAW` describes the
  pairing counters; and `FA`/`FAD`/`FAL` are documented at all, having been
  missing since they were added. Checked mechanically against the verb list
  extracted from `gpsdo_cli.cpp` — nothing the firmware answers to is now
  missing from the tab.

- **`DAC PWM|DITH|EXT` — the control-voltage path is chosen at runtime.** All
  three output paths now compile together and the command picks which one the
  firmware drives. The SIGNAL is switched by jumpers on the board; there is no
  soft multiplexer and there must not be one, because two drivers fighting over
  the control voltage is a hardware fault rather than a mode. What the firmware
  needs to know is which path it is steering, so that the step size, the
  telemetry and the fine-path arithmetic describe the thing that is connected.

  `GPSDO_PWM_DITHER` and `GPSDO_DAC_EXT` were mutually exclusive at compile time
  and are not any more — the pins never collided (PB9/TIM4 against PB4/PB0/PB2),
  only the assumption that one binary drove one output. One binary now serves
  either wiring, and a dither-on against dither-off comparison is a command
  instead of a reflash.

  PWM and DITH share PB9/TIM4 CH4, so selecting PWM on a board that has the
  dither engine does **not** tear the DMA down and hand the pin back to
  `analogWrite`. It writes the same 24-bit code with the low eight bits cleared:
  every table entry identical, constant duty cycle, bit-for-bit the voltage
  plain PWM produced — the same output reached without a reconfiguration that
  could fail halfway. `gpsdo_dac_fine_available()` becomes a property of the
  ACTIVE path rather than of the build, so a loop steering in fractions is told
  when the whole-LSB path is about to throw them away.

  Stored in one byte carved from the alignment padding between `tz_str` and
  `a12_gain` — verified against the compiler, not by eye — so the layout, the
  size and `SETTINGS_VER` are all unchanged and a block written by an older
  build still loads. Zero means UNSET and asks for the default, which is why the
  encoding starts at 1: an old record reads back as "use the default" rather
  than as "path 0". **The default is DITH**, resolved at bring-up against what
  is actually compiled in, and never resolving to a path that cannot drive the
  pin. On a build with the external DAC but no dither engine an unset value
  gives PWM, not EXT — the external part is a deliberate hardware choice and
  should have to be asked for.

  The report now prints the commanded voltage beside the measured one and warns
  when they differ by more than half a volt. The firmware cannot see the jumper;
  the only evidence that the setting and the wiring disagree is that the control
  voltage is not where it was told to go. Half a volt is deliberately loose —
  the ADC divider and reference are good to a few percent at best — because what
  this has to catch is "commanded 1.80 V, measured 0.00 V", not a scale error.

- **`LTO` and `LTR` take volts, like everything else on this detector.** The
  same physical point — the detector's phase zero — was held in two units that
  did not agree: `LZO` = 2.0809 V for algorithm 10, `LTO` = 2620 ADC counts for
  algorithm 11, which is 2.1104 V. Thirty-seven counts apart, about 37 ns once
  the scale was corrected, and invisible because nobody compares 2.0809 with
  2620 by eye. Both commands now take and print volts; the settings block still
  holds ADC counts, exactly the split `MLP` uses for nanoseconds, so no
  `SETTINGS_VER` bump and no migration. Both print the count alongside, since
  that is what a flash dump shows. A value between 3.3 and 4095 is refused with
  the conversion spelled out — "2620 counts = 2.1104 V — type that" — because
  that is the one mistake anyone will actually make. In the tuner, `LTO` and
  `LTR` become volt boxes, EVERY parameter label now carries its unit (or says
  "(x)" for a dimensionless one), and the algo-11 readback pattern stopped
  being anchored at end of line, which would otherwise have silently dropped
  every `tic_offset=2.1104 V (2620 counts)` reply and left the box empty while
  the board answered perfectly well.

- **`AQI` and `AQD` did nothing, and said nothing about it.** ACQ reads
  `pid->Kp` and nothing else; the centring pull is `g_ltic_acq_centre_gain`,
  set by `ACG`, a separate global with its own units. `acq.I_LIMIT` IS live —
  it is the step limiter — so the inert pair is exactly Ki and Kd. They were
  set by autotune, printed by `LL`, settable by `AQI`/`AQD`, persisted, and
  offered as editable boxes in the tuner: five ways to be told a knob works
  when turning it changes nothing. Every read and write of them now says so,
  `LL` carries the same note on the ACQ row, and the tuner greys both boxes —
  the readback still shows what the board holds, it just cannot be turned. The
  verbs still accept a value, because the tuner sends all four as one group on
  Apply and a refusal there would look like a fault in the tuner. Removing the
  fields means touching the settings block, which is a separate job.

- **The TFT phase row was subtracting the NEXT pulse's sawtooth.** Both display
  paths asked `ubx_timtp_correction_ns()` for the correction at the moment they
  were drawn, and that returns whatever qErr was decoded most recently. The
  serial report is gated on a change of `ppscount`, so it runs on the first wake
  after the pulse and gets the right one. The TFT redraws on EVERY wake of
  `vDisplayTask` — and one of those wakes comes from the GPS parser, after the
  receiver's serial burst has been consumed. TIM-TP is inside that burst, so by
  then `g_qerr_ns` has advanced to the next pulse while `g_ltic_voltage` is
  still this pulse's ramp. The panel was subtracting qErr(N+1) from phase(N).

  Noticed as a panel reading visibly larger than the log line printed in the
  same second, and larger is exactly right: on this receiver successive qErr
  values are ANTI-correlated (corr −0.30, period 2–3 s), so the neighbouring
  pulse's correction adds the sawtooth instead of removing it. Measured on the
  25.08 record, subtracting the neighbour takes the residual from 11.46 ns to
  13.6 ns — worse than applying no correction at all. Alan Cashin had raised
  exactly this failure mode for the loop the same day; it was in the display.

  `ltic_read_fast()` runs ~50 µs after the PPS edge, before the burst carrying
  the next frame, so the qErr visible there still belongs to this pulse. It is
  now latched once and every reader uses the latch, so the report, the panel and
  the loop agree by construction whenever they happen to be drawn.

- **Algorithm 10 overnight, 26.08: 9.7 h in LOCK, no drop-outs.** ACQ took
  225 s, DPLL 47 s, and after t = 410 s the state machine never moved again.
  Settled over 9.29 h: phase mean **+0.09 ns**, sd 7.42 ns, drift **−0.10 ns/h**,
  PWM inside a 32 LSB band with one correction every 53 s. Overlapping ADEV
  4.4e-9 at 1 s, 1.0e-10 at 128 s, 1.2e-11 at 1024 s, 1.7e-12 at 8192 s. The
  residual correlation against qErr held at +0.021 over the whole night, so the
  sawtooth cancellation is not a short-run artefact.

  What the run also shows is where the remaining error now lives. Splitting the
  phase into per-sample noise (from first differences) and everything slower:

  ```
                            sd      noise floor   slow structure
    algo 11, 5.2 h        3.11 ns      2.56 ns        1.76 ns
    algo 10, same window  7.76 ns      2.53 ns        7.34 ns
  ```

  The detector floor is identical — same board, same receiver — so the
  difference is entirely in the loop. Algorithm 11 corrects every second with a
  60 s time constant; algorithm 10's LOCK corrects once every 53 s with a phase
  integral whose restoring time constant is nearer 800 s, and the phase wanders
  ±20 ns at periods of 250–2400 s because the loop is slower than whatever is
  moving it. LOCK is now the limit, not the detector.

- **`ltic_autotune()` recomputes only when its inputs changed.** Every gain it
  derives is a pure function of `lsb_per_hz` (from CT) and `range_ns` (from
  LC), yet it ran on every transition into ACQ and silently threw away anything
  the operator had typed. Hit twice in one evening while testing the ACQ gain
  by hand: `AQP` is set, the loop drops to ACQ, autotune puts the old value
  straight back, and the next observation is of the tuning you thought you had
  replaced — with nothing in the log to say so. It now runs once per boot and
  again whenever CT or LC moves the measured constants, which is what "no
  per-board hand tuning is ever required" actually needs.

- **The runaway guard could never release.** Freezing set `u = 0`, which leaves
  the OCXO parked wherever the escape put it; the phase then races through the
  detector forever, `railed_now` never clears, `|e_freq|` never falls below
  0.25, and the release condition cannot be met. The 25.08 run sat in exactly
  that state for its last 527 s. The guard now walks PWM back toward
  `start_pwm` — the last code held while genuinely healthy — at up to 50 LSB
  per cycle instead of stopping on the spot, so a 20 000 LSB escape unwinds in
  about half an hour and the guard releases the moment the frequency comes
  back inside 0.25 Hz on the way.

- **`LNV` / `LZO` / `LRN` set by hand did not survive a reset.** The detector
  calibration lives in two places and the other one wins at boot: `LC` writes
  it to the live-store slot, `ES LTIC` writes it to the settings block, and
  `setup()` applies the settings block first and the live slot after it.
  Measured 25.08: `LNV 1252` was set, saved, and the board came back up on
  2649.3914 without a word. `ES LTIC` now refreshes the live slot as well, so
  the two agree and the load order stops mattering. Reordering the loads was
  the other candidate, but `LC` saves ONLY to the live slot, so making the
  settings block authoritative would have stopped an ordinary `LC` surviving a
  reboot.

- **The zero-crossing test is armed only by a LIMIT correction (Alan's rule).**
  One flag was doing two jobs: suppressing a second correction while the first
  one's deliberate slew is still walking the phase home — our addition, after
  the 14.08 +3800 LSB overshoot — and arming the zero-cross cancellation. Only
  the second is Alan's, and his rule is narrower than the port made it: a
  scheduled correction (the `MR` run level) fires on a timer with the phase
  wherever it happens to be, so there is no known slew for a crossing to cancel
  and no reason to expect a crossing at all, and a ZC does not re-arm itself
  because it *is* the cancellation. Arming in either case let an unrelated
  crossing minutes later pull a step out of a stale slope. The two jobs are now
  two flags: the settling suppression still follows every correction, the
  arming follows only the limit path.

- **`MR` default is 7 (256 s), not 9 (1024 s)** — Alan's own value, and the one
  that makes the scheduled correction the workhorse it is meant to be rather
  than a once-every-17-minutes backstop. The code changed with the algorithm-12
  work; the three manuals said 9 in two places each until now.

- **`MLP` and `ML` speak nanoseconds.** The limit table is stored in accumulator
  units — a level holds 2^(level+1) samples of `2*phase + 1`, so the stored
  number is the phase shifted left by `level+2` — and both commands printed that
  raw number while calling it "ns". Tuning a quantity nobody can relate to an
  oscilloscope is tuning blind. `MLP <n>` now prints both (`lim[6]=126ns (32350
  units over 128s)`), `MLP <n> <ns>` takes nanoseconds, `ML` tabulates ns beside
  units and span, and the tuner's limit boxes are nanoseconds throughout. The
  storage format is unchanged, so the settings block and existing saves are
  untouched.

- **Nothing claims the phase detector is present when it cannot know.** Dave
  Solder_Junkie built without one, and every layer told him it was fine: the
  boot banner printed `HW: LTIC phase input OK (PA1 analog)`, `LA 12` was
  accepted, and the loop disciplined the OCXO against ADC noise on a floating
  pin. Three changes, none of which can detect the hardware — because nothing
  can — but which stop pretending otherwise. The banner now reads
  `enabled (PA1) - needs the ramp detector hw`. The `GPSDO_LTIC` comment in
  `gpsdo_config.h` states the requirement in full instead of describing the
  circuit. And `LA 10`, `LA 11` and `LA 12` check whether `LC` has EVER run: if
  the TIC slope *and* the detector range are both still at build defaults, the
  warning is no longer the mild "uncalibrated" but "no detector calibrated,
  phase may be floating — is the hardware really there?".

- **ACQ centring holds on an invalid detector reading**, the way algorithm 12
  already did. The centring term steers on the raw voltage, and a saturated
  detector reports a voltage that no longer tracks phase; the drift gate did not
  catch it, because a railed reading is flat and its drift reads zero. Measured
  on 20.08 19:42, three seconds after a switch into algorithm 10: Vphase
  3.187 V against a band of 0.818..2.865 V, the centring term saturating its own
  cap and pushing for as long as the detector stayed out of band, 690 LSB of PWM
  swept before it settled. Holding costs nothing — the frequency path works from
  TIM2, which sees the offset whatever the detector is doing, and that is
  exactly what algorithm 12 falls back on.

### Added
- **AD5680 external DAC: the driver exists.** `dac_ext.cpp` leaves stub-hood —
  bit-banged GPIO on CS/SCK/MOSI = PB4/PB0/PB2 (Dan Wiering's PCB routing;
  PB2 doubles as BOOT1, so the MOSI trace must stay pull-up free), 24-bit
  word MSB-first (`code << 2`, zero command bits = write & normal mode), DIN
  clocked on SCLK's falling edge, register latched on SYNC's rising edge,
  shift-out under ~20 us of masked interrupts. Enable with `GPSDO_DAC_EXT`.
  Alongside: TM1637 and the 2 kHz test generator are OFF by default per the
  v1.06 policy (historical options) and yield their pins automatically when
  the external DAC is enabled — no more #error, the build simply omits them.

  It now builds on a real toolchain: `hostcheck` gained two AD5680 rows (with
  and without the phase detector) and both compile and link for cortex-m4 under
  `arm-none-eabi-g++`. That closes the "reviewed only" caveat the driver
  shipped with — it does not close the hardware test, which is Dan's to run.
### Added
- **Algorithm 13 — a three-state Kalman filter (phase, frequency, aging).** Every
  loop in this firmware until now has had a bandwidth chosen once and lived
  with: algorithm 10 switches between three, algorithm 11 has a time constant,
  algorithm 12 picks a level from a threshold table. All three answer the same
  question — how much of this second's reading to believe — with a number
  decided in advance. This one answers it from the variances, and re-answers it
  every second.

  **It costs nothing worth counting.** Three states and a SCALAR measurement, so
  the matrix inversion everyone worries about is one division: about 140
  multiply-adds and 36 bytes of state, once a second, roughly a microsecond of a
  100 MHz M4F. The received wisdom that a Kalman filter needs a Cortex-A or an
  FPGA is about twenty-state GNSS filters, not about this.

  **Both noise figures are measured, not set.** R comes from the detector's own
  first differences, the same estimator algorithm 12 uses for its sigma. Q is
  adapted from the innovation sequence: the filter predicts how big its own
  surprises should be, and when they are consistently bigger the process noise
  is too small. `KR` and `KQ` pin either one for an experiment; zero means
  measure. Seeded with the figures the 26/27.08 night gave — 2.64 ns and
  2e-6 (ns/s)²/s, the latter consistent across 600, 1800 and 3600 s windows,
  which is what random-walk FM looks like — so the filter is sane from its first
  second rather than after the estimators converge.

  **Holdover needs no code of its own.** The state carries frequency AND aging
  with their covariances, so losing the phase is not a special case: stop
  updating, keep predicting, keep steering. The trend shows `HOLD` and `KL`
  reports how long it has been running on the model.

  Measured with `tools/loopsim`, replaying the oscillator reconstructed from the
  26/27.08 logs, five noise seeds, against the loops that already exist:

  ```
                     phase sd [ns]        ADEV @ 1024 s
                  algo-12 win / algo-11   algo-12 win / algo-11
    algorithm 11     3.62 / 7.39           7.7e-12 / 1.5e-11
    algorithm 12     3.07 / 4.20           4.1e-12 / 7.6e-12
    algorithm 13     1.20 / 1.25           1.4e-12 / 1.8e-12
  ```

  The second column is the interesting one. The other two loops lose ground on
  the plant that drifts more and this one does not, which is the adaptive
  bandwidth doing its job rather than a better constant.

  Two guards, both put there by the simulator rather than by taste. **A gate
  that never opens is a broken filter**: the innovation gate rejects a reading
  more than four sigma from prediction — the 26/27.08 night had two such spikes
  at ±40 ns — but ten consecutive rejections mean the STATE is wrong rather than
  the data, so the filter widens its own belief by the size of what it keeps
  refusing. Without that, a start 1500 ns out never recovered. And **a phase
  still beyond the ACQ window after five horizons is a reading that is not
  moving with the oscillator**, which on this board means a picDIV that never
  synced: arm it once and start the filter again.

  New commands `KR` / `KQ` / `KT` / `KL`, saved on entry in their own flash-ring
  record (`REC_A13`) rather than in the settings block, which has no padding
  left — growing it would have forced a `SETTINGS_VER` bump and thrown away
  everyone's PID, LC and timezone for three numbers.

- **TAB or ESC pauses and resumes the telemetry, one key.** Alan Cashin's
  suggestion, and the right one: `RP` and `RR` already do this, but typing a
  command while the reports scroll past is exactly the thing that is hard, and
  the fix should not itself need a clear moment to type into. A bare ESC toggles
  like TAB; ESC followed by `[` or `O` is an arrow or function key and is
  swallowed, so reaching for shell history no longer stops the reports. A
  half-typed line is abandoned on the toggle rather than being left to join the
  next one.

- **CPU load on the telemetry line.** `CPU:7%`, appended to the sensor line.
  Measured rather than modelled and with no timer of its own:
  `vApplicationIdleHook()` increments a counter, and once a second the count
  becomes a percentage against the highest per-second count ever seen, which is
  by definition an idle second. `configGENERATE_RUN_TIME_STATS` was the
  alternative and it wants a spare timer and a base clock to produce per-task
  figures nobody asked for.

  What it sees: everything the idle task did not get, interrupt time included,
  because an ISR steals from whatever is running. What it cannot see: a board
  that has never once been near idle, where the reference is an underestimate
  and the figure therefore flatters. The reference leaks by about 0.02% a second
  so it follows the board instead of being pinned forever by one lucky second at
  boot. It is appended at the END of the line on purpose — every reader searches
  for its own field rather than anchoring, but adding one in the middle is still
  how you silently break somebody's regex.

### Measured
- **The "27% worse than algorithm 11" figure was wrong, and it is withdrawn.**
  It came from comparing two different nights, and both algorithm-13 runs behind
  it were misconfigured: one had `KR` pinned at 2.5 ns, which cost eleven per
  cent of its readings at the innovation gate, and the 29.08 12:31 session had
  the R estimator running away to **47.70** during pull-ins — the very failure
  the freeze-while-moving guard was written for, on a build that predates it.
  Neither run should ever have been used to judge the loop.

  The 28.08 capture switches algorithms mid-session, which settles in one
  afternoon what cross-night comparison cannot settle at all. Algorithm 13 ran
  4.24 h and algorithm 11 the next 3.53 h, back to back on the same board:

  ```
    algo   dph sd  floor   slow   track sd    r    req span  applied
     13     3.67    2.72   2.46     0.54    0.996    20.7      59.0
     11     3.22    2.70   1.76     0.48    0.959    10.0      23.0
     12     6.18    2.67   5.57     0.87    0.959    20.0      28.0
  ```

  Thirteen per cent apart on tracking error, fourteen on phase sd — and
  algorithm 13 had the harder half, against a required control span of 20.7 LSB
  where algorithm 11 faced 10.0. Both are far ahead of algorithm 12 in the same
  session. That is a different loop from the one the cross-night number
  described.

- **And the earlier verdict on the R estimator is withdrawn with it.** The
  replay said the 16 s lag measured marginally worse than 1 s; the bench says R
  reached 47.70 on the build without the guard, against a white floor of 6.4.
  `loopsim` never reproduces that because its plants carry no pull-in transient
  large enough — the runaway happens while the loop is moving the phase, which
  is exactly the state the guard freezes the estimator through. The change
  stays, and the replay result stands as what it is: a measurement of a
  situation the replay does not contain.

- **One behavioural difference survives every session: algorithm 13 works the
  actuator far harder than it needs to.** 59 LSB applied for 20.7 required on
  28.08, and 111 for 8.0 on the bad 29.08 run, against algorithm 11's 23 for
  10.0 and 17 for 6.1. It is not under-correcting — that suspicion is closed —
  it is over-actuating, consistently, and that is the thing left to attack.

### Added
- **`tools/logab.py` — compare the algorithms against each other inside one
  capture.** Per contiguous stretch of one algorithm it prints the phase sd, the
  detector floor, the slow structure the loop is responsible for, and the
  tracking error against the control the oscillator actually needed
  (reconstructed the way `loopsim` builds its plants). Two algorithms on two
  nights are two experiments; one capture that switches between them is a
  comparison.

  It also fixes a trap that cost a day: the Learn line has a different shape per
  algorithm family, so a regex written for one of them silently keeps the
  previous value for the others — which turned a four-algorithm session into a
  single 20 h "algorithm 13" segment on the first attempt. Only `algo=` comes
  from the Learn line; everything else comes from lines every algorithm prints.

### Measured
- **Three bench runs on algorithm 13, and the one certain finding is that a
  pinned `KR` cost eleven per cent of the readings.** The 29/30.08 run threw
  away **4904 of 45275** samples at the innovation gate — `rej` went 98 to 5021
  — and nothing said so: the Learn line carries a cumulative counter nobody
  differentiates while it scrolls, and the phase sd looked ordinary at 6.07 ns.
  `KR` was pinned at 2.5 ns, which fixes R below the innovations the detector
  actually produces and leaves the 4-sigma gate too tight. The two short runs of
  30.08, with R measured, rejected **none at all** — and neither does the
  simulator. So: leave `KR` at 0 unless pinning it is the experiment.

- **The loop is not under-correcting**, which was the standing suspicion from
  the 57-against-93 LSB observation. Reconstructing what the oscillator actually
  needed from each log: the required control spanned **17.6 LSB** on the
  algorithm-13 night and **18.6 LSB** on the algorithm-11 night — the two nights
  were as comparable as one could ask — while the loops applied 61 and 93 LSB
  respectively. Both move three to five times more than required; algorithm 11
  moves the MORE of the two and still holds the phase better.

- **A metric that discriminates, and a sharper statement of where the simulator
  fails.** Smoothed over 300 s, the correlation between the control applied and
  the control required is **0.992 for algorithm 11 and 0.942 for algorithm 13**,
  with tracking errors of 0.70 and 0.81 LSB. `loopsim` now reports the same two
  numbers, and on the replayed 26.08 oscillator it gives algorithm 11
  **0.71 LSB** against the bench's 0.70 — the plant reconstruction is sound and
  the metric is right. For algorithm 13 it gives 0.20 LSB against the bench's
  0.81. The divergence is specific to that one loop, not to the plant.

  Calibrating the simulator's correlated detector error against the bench does
  not close it: algorithm 13 matches at about 12 ns of zero wander (0.96 LSB,
  r 0.944), and algorithm 11 matches at zero (0.71, r 0.971). There is no
  setting at which both are right. The same detector behaves as though it were
  12 ns noisier to one loop than to the other.

### Rejected
- **Making R bigger does not fix it.** The R estimator's lag was swept at the
  bench-matching operating point: 1 s gives 0.96 LSB / r 0.945, 16 s gives
  1.01 / 0.939, and 64, 128 and 256 s get monotonically worse to 1.49 / 0.886.
  Pinning Q lower — the other way to filter harder — is worse again (1.30 LSB at
  1e-7 against 1.01 adaptive); pinning it higher is marginally better. Every
  lever tried so far moves the wrong way or does nothing: the R lag, the Q,
  the horizon from 50 to 800 s, feeding the aging state forward, and an
  innovation whiteness test. The mechanism is real — correlated detector error
  degrades this loop five times faster than algorithm 11 — but inflating R is
  not its cure, because a slower filter tracks the real drift worse.

  The A/B on ONE night is still the experiment that settles it, and until it is
  run every further change to this loop is a guess.

### Fixed
- **The CPU reading on the TFT header lost the tail of its `%`, and the 320x240
  build did not compile at all.** Two separate things, found together because
  the same check now exists for both.

  The header's CPU field was centred at `TFT_W / 2`. The LMT clock beside it is
  right-anchored with a padding of `TFT_S(130)`, and TFT_eSPI's padding erase is
  a rectangle of that width ending at the anchor — 276..471 on the 480 panel.
  `CPU 66%` in FreeSans9pt is about 79 px, so centred at 240 its right edge
  lands near 279, three pixels inside that band, and the clock (drawn after it)
  wipes the last glyph. On the 320 panel the same sum leaves about one pixel,
  which is not a margin but a coincidence: a three-digit reading clips there
  too. The field is now centred in the gap the other two fields actually leave,
  with both widths taken from `textWidth()` rather than assumed — the panels do
  not even use the same face — and it is drawn after the clock, so no erase can
  reach it whatever the widths turn out to be. If the gap cannot hold the
  string it draws nothing: a clipped number is worse than no number. The
  padding constant now has one name, `HDR_LMT_PAD`, because two copies of a
  number that a one-pixel margin depends on is how this happened.

- **The 320x240 TFT build had a variable declared in the 480 branch and used in
  the 320 one** (`phs`, the formatted phase). It has presumably been broken
  since that row was split, and nobody could have noticed: **no check compiled a
  single line of the display code.** Every panel switch was off in
  `tools/hostcheck` for want of the vendor library, and the display block is the
  largest thing in `gpsdo_tasks.cpp`. A configuration nobody can build is not a
  configuration, it is a rumour.

  `tools/hostcheck/stub/TFT_eSPI.h` is now enough of that API to compile
  against, and hostcheck grew three rows: both panels with the detector, and the
  small panel without it. Fourteen configurations. It draws nothing and cannot
  catch a clipped glyph — only the panel can — but it catches the typo, the
  wrong type, the datum that does not exist and the variable out of scope, which
  is the class of error that actually happens here.

### Measured
- **Algorithm 13 on the bench, 11.8 h overnight: the standing offset is gone and
  the loop is 27% worse than algorithm 11.** Both facts matter and the second
  one is not yet explained.

  What worked. The +18.7 ns standing offset of the previous run is **+0.06 ns**
  — the output-stage bookkeeping fix did exactly what it was supposed to. The
  CT/LC derivation printed `res 1.01ns R0 2.52ns Q0 6.36e-6 P0 1500ns lim 750LSB
  arm<0.25Hz` and the measured R settled at 6.28 against a seeded 6.35, so the
  seed was right to within noise. **163 innovation rejections in twelve hours**
  against 281 in the previous fifty-three minutes, and **zero** picDIV re-arms.
  The frequency estimate averaged +0.0007 ns/s: no bias.

  What did not. Phase sd **5.94 ns against algorithm 11's 4.68 ns** over a
  comparable 11.0 h night on the same board — same detector floor (2.74 against
  2.64 ns), same thermal swing (2.4 against 2.7 °C), so it is the loop and not
  the room. ADEV 1.0e-11 at 1024 s against 7.8e-12, and 2.7e-12 at 4096 s
  against 2.0e-12; identical at 1 s and 16 s, so the short end is detector-
  limited for both and the whole difference sits at mid tau.

  **The simulator says the opposite, by a factor of three.** Four attempts to
  make it say otherwise all failed — correlated detector noise, a realistic
  TIM2, the horizon from 50 to 800 s, and feeding the aging state forward into
  the control. The one clue nobody has explained: over the night this loop moved
  the PWM **57 LSB where algorithm 11 moved 93** on a comparable night. That is
  the signature of UNDER-correction, not of chasing noise. The two runs were
  different nights, so the honest next step is A/B on ONE night — a couple of
  hours of each, alternating — which takes the environment out of the comparison
  entirely.

### Fixed
- **Two simulator models were flattering, and one of them was an outright bug.**
  `loopsim` handed the firmware the exact, instantaneous, noiseless frequency
  every second. The real TIM2 gates whole cycles for one second, so the 1 s
  figure is an INTEGER number of hertz, the 100 s average is the mean of a
  hundred of those — which is where its 0.01 Hz resolution comes from — and it
  is a boxcar that lags by fifty seconds. That did not matter while the
  frequency was a minor term; it mattered the moment algorithm 13 took it as a
  Kalman measurement. The plant now counts whole cycles and averages them the
  way the counter does.

  The detector noise was white, and a ramp TIC read through a 12-bit ADC is not.
  This is not a detail: any loop that estimates its measurement noise from FIRST
  DIFFERENCES — algorithm 13's R, algorithm 12's sigma — measures only the white
  part and is blind to the rest by construction. `LOOPSIM_DNOISE=<ns>` now adds
  a stationary slow error, so the question has a number for an answer. It
  degrades algorithm 13 five times faster than algorithm 11 (1.22 → 7.23 ns
  against 7.45 → 10.07 at 8 ns of zero wander), which is the right shape — and
  still not enough to reverse the bench result.

### Rejected
- **A whiteness test on the innovations**, written to explain the bench result
  and measured worse at every level, including a clean detector (1.22 → 1.72 ns)
  and 8 ns of correlated noise (7.23 → 8.28). The reasoning was textbook: an
  optimal filter's innovations are white, so a positive lag-1 correlation means
  the MEASUREMENT noise is correlated and the right response is to inflate R
  rather than widen Q. The flaw is that this filter's innovations were never
  going to be white — its control writes its own frequency state every second —
  so the test fires for reasons that have nothing to do with the detector and
  the inflation only makes the loop sluggish. Not in the tree; written up at its
  site so it does not get proposed again.

### Added
- **CPU load per task, measured on the cycle counter, with a true 100 s
  window.** `SW` prints it once, busiest first, beside the stack marks; `TL 1`
  puts the same numbers on the telemetry line and `TL 0` takes them off again.
  Not stored, and off after every reset: it is a bench diagnostic that costs a
  line of telemetry a second, and a setting that survives a reboot is one nobody
  remembers turning on.

  It is not sampled. FreeRTOS calls `traceTASK_SWITCHED_IN()` on every context
  switch and the Cortex-M4 has a free-running cycle counter in the DWT block, so
  the interval between two switches is known exactly and belongs, exactly, to
  the task that was running — a handful of cycles per switch and no timer
  consumed. A tick-driven sampling profiler was the obvious alternative and
  would have been blind to any task that starts on a tick boundary and finishes
  before the next one, which on this firmware is most of them.

  The window is a hundred one-second buckets rather than an exponential average:
  asked for a 100 s mean, an EWMA with a 100 s time constant still carries a
  fifth of its weight from five minutes ago. Shares are of each second's own
  total switched time, so no clock rate enters the arithmetic and the columns
  add to 100. Each bucket closes under a critical section that also charges the
  slice of the task running at that instant — without that the idle task, which
  routinely holds the processor for most of a second uninterrupted, would
  contribute nothing to the bucket it dominated.

  What it attributes where is worth stating: interrupt time lands on whichever
  task was interrupted, because an ISR does not switch context. The reading is
  "the processor spent this long with this task current", which is honest and
  not quite the same as "this task used this much".

### Changed
- **The whole-CPU figure now comes from the same accounting: 100% minus the idle
  task's share.** It used to be an idle-hook spin counter with the highest count
  ever seen taken as 0% load — which works, and has a flaw that cannot be
  measured from inside it: a board that has never once been near idle has an
  underestimated reference and therefore an optimistic reading, for ever. The
  cycle counter has no reference to be wrong about. The idle hook and
  `configUSE_IDLE_HOOK` are gone with it.

  It is also no longer ticked from the telemetry line, which meant TAB (pause
  telemetry) silently paused the load measurement too. It is driven from the
  uptime task now, and rolls on elapsed milliseconds rather than on being called
  exactly once a second, so a caller that skips or doubles cannot shorten or
  stretch a bucket.

### Changed
- **Algorithm 13 takes its scale from CT and LC instead of from one board.** It
  shipped with three numbers measured on the author's bench — R seeded at
  (2.64 ns)^2, Q at 2e-6, and a cold-start phase covariance of (100 ns)^2 — and
  on any other OCXO or detector they are simply wrong. A board with a 300 ns
  detector would start with a prior four times wider than its whole band; one
  with a 10 000 ns detector far tighter than the truth, rejecting good readings
  for its first ten minutes. The 27.08 run shows the second failure on the very
  board the constants came from: **281 rejections and 500 s** to pull in from
  1300 ns.

  Nothing in `kf_scale()` is now a constant. CT gives the counts that null a
  nanosecond in a second; LC gives ns per volt and the usable range; the part
  fixes the ADC at 12 bits over 3.3 V, so the detector's own quantum is
  `ns_per_volt * 3.3/4096` — 1.01 ns on this board, against a measured noise of
  2.6 ns, which is two and a half quanta and is where the R seed comes from.
  From those:

  | was | now | on this board |
  |---|---|---|
  | R seed (2.64 ns)^2 | (2.5 quanta)^2 | 6.4 ns^2 (measured: 6.6) |
  | R floor 0.25 ns^2 | one quantum squared | 1.02 ns^2 |
  | Q seed 2e-6 | R / KT^3 | 6.6e-6 (measured: 2e-6) |
  | P00 (100 ns)^2 | (half the band)^2 | (1500 ns)^2 |
  | P11 (1 ns/s)^2 | (band crossed in one horizon)^2 | (15 ns/s)^2 |
  | aging Q 1e-12 | Q / (100 KT^2) | 6.6e-12 |
  | arm gate 0.5 Hz | band / (2 x 100 x hold-off) | 0.25 Hz |
  | trust floor 8 ns | two quanta | 2.0 ns |

  Recomputed every second rather than cached, so re-running CT or LC takes
  effect without restarting the loop, and printed once as a single `KAL: from
  CT/LC ...` line when the filter starts — a derived constant nobody can read is
  a constant nobody can check. Q's seed deserves a note: there is no measurement
  of an oscillator's random walk at CT or LC time, but the filter adapts Q from
  its own innovations within minutes, so the seed only has to set a sane
  bandwidth for those minutes. `Q = R/KT^3` has units of (ns/s)^2 per second
  exactly and lands within a factor of three of what the night run measured —
  two unrelated routes to the same number, which is as much as a seed can be
  asked for.

  Measured, five seeds: phase sd **1.19 -> 0.81 ns** on the 26.08 algo-12 plant
  and 1.24 -> 1.20 on the algo-11 plant.

### Fixed
- **The loop booked corrections the pin never received, and that is the standing
  phase offset in the 27.08 log.** +18.7 ns held for thirty-eight minutes with
  the PWM motionless and the filter reporting a slew of -0.19 ns/s it was not
  applying. The clamp was accounted for; the ROUNDING was not.

  Once the phase is home the corrections are a fraction of an LSB. Booking the
  requested `du` into the frequency state tells the filter it is already slewing
  at `-x0/T`; the next second the control computes `-(x1 + x0/T) = 0` and asks
  for nothing. If that fraction never reached the pin, the filter is now certain
  it is fixing a phase error that nothing is fixing — and the loop parks, at any
  offset, for ever, because the state that would notice is written by the
  control rather than estimated from the data.

  The fix is not to round more carefully. It is to book the **difference between
  the DAC values**, which covers the clamp, the rounding and the sub-LSB path
  together and cannot be fooled by a future output stage either, and to carry
  the remainder into the next second so a sub-LSB request is delayed rather than
  discarded — a one-second sigma-delta, so a board without the fine path asks
  again until a whole count moves. Same lesson as algorithm 12's zero-crossing,
  one layer further down: a state updated with an unapplied control is a state
  that lies.

  Simulated on a 16-bit board (`LOOPSIM_FINE=0`), standing phase offset:
  +0.29 -> +0.02 ns on a quiet plant, +0.16 -> +0.04 on the 26.08 plant, with
  sd 0.65 -> 0.55.

- **The detector-trust verdict could latch, and did.** Two faults, both found by
  measurement rather than by reading. First, the test ran against the 1 s
  frequency EMA when the gated 100 s average was not yet available; that EMA
  lags by fifty seconds, so during a pull-in it says the phase should be moving
  at a rate that was true a minute ago and convicts a perfectly good detector.
  It now runs only when `have100` is true. Second, the verdict could never be
  revisited: a distrusted detector leaves the loop steering on TIM2, which holds
  the frequency well, so nothing is expected to move, no window can conclude,
  and the conviction stands for ever on evidence that has expired. Thirty
  minutes without a single conclusive window now returns the benefit of the
  doubt — and the arm bridge no longer clears that clock, which re-created the
  same deadlock through the 600 s back-off. A detector that has already been
  caught is convicted again on one failing window instead of three, which halves
  what the periodic re-hearing costs on a genuinely frozen one.

  With the railed detector modelled properly (`LOOPSIM_RAIL` used to sit at
  3.27 V, which is INSIDE the band for a 3000 ns LRN — it was a frozen-detector
  test wearing the wrong name), the recovery is now one arm at t+106 s followed
  by a run indistinguishable from a healthy one: phase sd 0.55 ns, frequency
  0.0001 Hz. A permanently frozen detector holds frequency to 0.0002 Hz.

### Added
- **A build identity that cannot be stale: a CRC-32 of the flash image, computed
  at boot from the flash itself.** The banner and the `V` command now print it
  beside the compile time.

  The compile stamp was believed and on 26.08 it lied — two captures from two
  different builds carried the same one, and an hour went into arguing with a
  log that was right all along. Nobody had done anything wrong. `__DATE__` is
  baked into whichever translation unit mentions it, which is the sketch, and
  the Arduino builder does not recompile a translation unit whose sources have
  not changed. Edit `GPSDO_algorithms.cpp`, upload, and the sketch's object file
  is reused with last week's timestamp inside it. The stamp is honest about when
  the SKETCH was compiled and says nothing about the rest of the firmware.

  A CRC of the image has none of that: nothing computes it until the board is
  running, so it cannot come out of a build cache, and it changes if any byte of
  any translation unit changed. Two boards flashed with the same binary print
  the same number. When a log and a memory disagree, that is the one to trust.

  It covers the vector table, code, read-only data and the `.data` initialisers
  — every byte the programmer wrote — bounded by the linker's `_sidata`,
  `_sdata` and `_edata`. Those are **weak** references: a toolchain that names
  them differently reports "unavailable" instead of failing to link, because an
  identity that is missing is a nuisance and one that stops the firmware
  building is a fault. About 2.5 ms once at boot, and 64 bytes of table.

- **`build_id.h` and `tools/bumpbuild.py`, so the timestamp is fresh too.** The
  sketch includes `build_id.h` for no reason except its existence: touching it
  is what makes the builder recompile the sketch, and nothing else — a fraction
  of a second, against the full rebuild the same trick would cost through
  `build_opt.h`, where a changed flag invalidates everything. `bumpbuild.py`
  increments the serial; any edit at all does the same job, since the builder
  keys on the file rather than its contents. The serial is printed in the
  banner. If it is never bumped nothing breaks and nothing lies — the CRC is the
  guarantee, this is the convenience.

### Changed
- **The tuner plots algorithm 13's phase estimate instead of a series it never
  sends.** Algorithm 13 fell through to the PID family, so the top pane was
  labelled "Learned drift (LSB) — LRN feed-forward" over an empty graph. It now
  shows the filter's phase estimate, with Vphase and its band guides underneath,
  because the question this loop most often raises is whether the detector is
  alive at all. The guides now follow whichever pane is showing Vphase rather
  than a hard-coded family. `ARM` joins the trend words it explains, and the
  Learn line carries `arm=` once the divider has been re-armed.

### Fixed
- **Algorithm 13 had no LTIC bridge at all, and with the detector railed it
  simply let the oscillator walk.** Reported from the bench as the frequency
  climbing steadily under algorithm 13. The cause was structural rather than
  subtle: the filter had ONE measurement, the phase. With the picDIV unsynced
  there is no valid phase, so it had no measurement at all — it predicted from a
  state still at zero, applied nothing, and reported HOLD while the OCXO drifted.
  Algorithms 11 and 12 both have the TIM2 frequency for exactly this reason, and
  algorithm 12 says it in as many words: while the phase detector is blind, the
  frequency still reads true. Worse, the one thing that could have repaired it —
  the stall watch that re-arms the picDIV — cleared its own counter whenever the
  phase was invalid, which is the case it existed for.

  **TIM2 is now the filter's second measurement**, as one more scalar update on
  a state it already carries: no matrix inversion, no new concepts, about thirty
  more multiply-adds. Holdover, pull-in from far off frequency and a dead
  detector all stop being special cases.

  **And it makes the detector checkable.** Phase and frequency are the same
  quantity differentiated, so over a window the phase MUST move by the summed
  frequency error. A detector that does not move when TIM2 says it must is not
  measuring anything — which is the 26.08 21:47 fault, where a pinned 3.116 V
  read as a perfectly valid +1295 ns that never changed, and a filter has no
  defence against that alone: a constant reading is a consistent reading, the
  innovations go to zero and it believes it MORE the longer it lies. This test
  is algorithm 12's stall watch with the guesswork removed — that one predicted
  the motion from the slew it had just commanded, which is the loop marking its
  own homework; this compares against a second instrument. An untrusted detector
  is then treated exactly as a railed one, and trust is NOT handed back on a
  re-arm: doing that re-admitted a detector already proven dead and cost 1.35 Hz
  in simulation where staying blind cost 0.01.

  The picDIV arm bridge is algorithm 12's, gate and hold-off and all — arm only
  once the frequency is close, because an arm lands the phase at a quantised
  offset and with the frequency still off it rails again within seconds. On an
  arm the filter widens its PHASE belief rather than resetting: nothing it
  learned about frequency or aging came through the divider, and those are the
  expensive ones to relearn. Algorithm 12 has to throw its whole accumulator
  away here; this is what carrying a covariance buys.

  Measured in `tools/loopsim` against the oscillator replayed from the 26.08 log,
  with the detector failing the way the hardware fails (new `LOOPSIM_RAIL` and
  the existing `LOOPSIM_STUCK`), before and after:

  ```
                                       BEFORE      AFTER
    railed picDIV, 300 LSB off        -5.06 Hz    -0.0002 Hz (1 arm)
    frozen at +1295 ns, 300 LSB off   -4.26 Hz    +0.0004 Hz
    frozen at +1295 ns, on frequency  -4.17 Hz    -0.0005 Hz
    healthy detector, 1500 ns out    sd 209.80    sd 209.77 ns
    healthy detector (5 seeds)       sd  1.23     sd  1.19 ns
  ```

  Loop quality on a working detector is unchanged, which is the point: none of
  this is in the control law.

- **The simulator's frequency channel had the wrong sign**, and it went unnoticed
  because nothing used both channels at once until this filter took the frequency
  as a measurement. `loopsim` drove the detector and TIM2 from the same control
  error with the frequency FALLING as the PWM rose, which makes the phase rate
  and the frequency error the same sign; on this board they are opposite. Two
  loops that run on the bench say so independently — algorithm 11 lowers the PWM
  when the oscillator reads fast, and algorithm 12's TIM2 trim walked `f100` home
  with negative steps on the 16.08 storm — and both need frequency to rise with
  PWM, while the phase reconstruction has it fall. No earlier measurement moves:
  algorithm 12's TIM2 term is gated well above the frequency errors these plants
  produce and never fired.

- **The sketch would not compile, and eleven clean configurations said nothing
  about it.** Two errors reached the IDE together: `vApplicationIdleHook()` was
  written with `extern "C"` on the line above the function rather than on it,
  and `setup()` called `kf_store_load()` without the header that declares it.

  The first is worth stating plainly, because it is not obvious. The Arduino
  builder inserts a C++ prototype for every function a sketch defines, and its
  ctags pass is LINE ORIENTED: `extern "C"` on the preceding line is not seen,
  so the prototype is generated, and the definition below it — which does have C
  linkage — conflicts with it. FreeRTOS itself declares no prototype for the
  hook (it is called from C), so the generated one is the first declaration and
  the compiler has nothing to prefer it to.

  Neither error was visible to `tools/hostcheck`, which compiled every `.cpp` in
  eleven configurations and never compiled the `.ino`. **It does now**:
  `tools/hostcheck/ino2cpp.py` reproduces both things the Arduino builder does
  to a sketch — prepend `<Arduino.h>`, insert the line-oriented prototypes — and
  the sketch is compiled and linked alongside the rest in every configuration.
  Reverting either fix now fails the run with the same wording the IDE gave.

- **Algorithm 12 was three times worse than it needed to be, and the cause was
  one CLI entry.** The 26.08 run: 2.25 h on algorithm 12 at phase sd 41 ns in a
  ±70 ns limit cycle with a 2.3 hour period, against algorithm 11's 3.0 ns on
  the same board the same afternoon. The detector noise floor was identical in
  both segments — 2.47 ns against 2.45 — so the difference was entirely the
  loop, which is what the slow-structure metric exists to say.

  `MG` had been set to **2.130 LSB/ns**. That is algorithm 11's `LG`, its own
  VCO gain; algorithm 12's `MG` is the counts needed to null one nanosecond of
  phase in one second, and CT had measured **31.3**. Both are printed "LSB per
  ns" and they are not the same quantity. Every correction was therefore 14.7×
  too small — visible in the log as thirteen corrections moving the PWM a total
  of four LSB while the oscillator's required control wandered 3.3.

  The same entry did a second thing nobody asked for. `MF 0` meant "follow MG",
  so typing a gain also swapped the limit table from the noise formula to the
  stored one, whose level-6 limit is ~126 ns. The loop then corrected weakly AND
  far too late.

  Three changes, each measured rather than argued:

  - **The limit table no longer follows the gain.** `MF 0` is the noise formula
    now, whatever `MG` says; the hand-edited `MLP` table is asked for with
    `MF 1`. The welding was already argued to be wrong when `MF` was introduced
    — the gain belongs to the oscillator, the limits to the site's phase noise —
    and only compatibility kept it. That compatibility is what this cost.
  - **The zero-crossing test arms on both correction paths.** It armed only on
    the limit path, on the argument that a scheduled correction has "no known
    slew for the crossing to cancel". Both paths compute the same
    `slew_lsb = -(p_ns/span)*lsb_per_ns` a few lines earlier and both store it,
    so that argument describes Alan's loop rather than this one.
  - **The board now says so when a hand-set `MG` cannot be a tuning choice.**
    Beyond a factor of four from what CT measured it is an entry error, not
    tuning. The value is still used — a deliberate experiment has to stay
    possible — but `MG`, and the loop on first use after a settings recall, both
    print the measured figure beside it. The recall path matters: that is the
    one nobody tests.

  New tool `tools/loopsim/` did the measuring, and it is the third of its kind:
  `hostcheck` asks whether the tree compiles, `algoswitch` whether a loop
  restarts, `loopsim` whether it holds the phase. It replays an oscillator
  reconstructed from a log rather than one invented for the occasion — given the
  PWM applied and the phase reported, the control the oscillator needed each
  second is algebra — and builds the real algorithm twice, with and without a
  change, so a column comparison is the change and nothing else. Phase sd, mean
  of five noise seeds:

  ```
                          BEFORE   AFTER    algo 11
    26.08 algo-12 window
      MG 0  (derived)      3.57     2.95      3.64
      MG 2.130 (as set)   53.81    21.06
      MG 31.3 (CT)         6.63     2.95
    26.08 algo-11 window
      MG 0  (derived)      4.62     4.01      7.39
      MG 2.130 (as set)   66.04    61.96
      MG 31.3 (CT)        16.41     4.01
  ```

  With the gain right, algorithm 12 now holds better than algorithm 11 on both
  windows — 2.95 against 3.64 on the shorter, 4.01 against 7.39 on the longer,
  which carries four times the drift.

  **Two repairs were written and rejected, and both are recorded at their
  sites.** The zero-crossing test fired zero times in that log because the phase
  took 810 to 3283 seconds to change sign after each correction, against a 300 s
  give-up window; every return outlived it. Replacing that window with "hold the
  arm while the phase is still coming home" is the obvious repair and measures
  *worse* on the longer plant (4.01 → 6.76 ns), because on a drifting board a
  phase creeping toward zero is often the oscillator rather than this
  correction's slew. The long returns were the symptom of the 14.7× gain error,
  not a fault in the constant. A second guard — arm only when the accumulator's
  extrapolated phase agrees in sign with the phase on the pin — measured as
  buying nothing and is not in the tree either. A rejected repair that leaves no
  trace gets proposed again.

  Existing installations: if `MG` is non-zero, check it against `CT`'s figure —
  `ML` prints both — and `MG 0` if in doubt. If you deliberately hand-edited the
  `MLP` limits, add `MF 1` to keep them, then `ES ALGO12`.
- **The zero-crossing test was paying back a slew that never reached the pin.**
  This is the one that made algorithm 12 unusable whenever the phase was far
  out, and the 26.08 22:41 log is a clean recording of it: after the stall watch
  re-armed the divider the phase came back honestly, reached −50 ns, and the
  loop then threw the PWM 1038 counts in one second. It spent the next seventeen
  minutes crossing the whole detector band — PWM 40348 to 41410, phase −1600 to
  +20, five re-arms — and never once got inside ±200 ns.

  A correction computes a deliberate slew and then clamps the total against the
  detector band. The crossing later removes the slew, so that the oscillator is
  left at the right frequency AND no phase error. But the value it removed was
  the slew as COMPUTED, recorded before the clamp — and whenever the clamp bites,
  which is exactly when the phase is far out, that is a different number. At
  −1500 ns on this board the computed slew is 734 LSB and only 500 reach the
  pin; the crossing then hands back 734, and the extra 234 is a fresh frequency
  error pointing the other way. The phase sets off in the opposite direction,
  hits the clamp at the far rail, and the loop paces the band forever.

  It now records what survived the clamp — the clamped total minus the frequency
  term, which is not a deliberate slew and is not cancelled. Pull-in from a large
  offset, replayed against the oscillator from that day, mean over five noise
  seeds:

  ```
     starting phase     before        after
        ±500 ns          0.6 ns       1.1 ns
       ±1000 ns          0.7 ns       1.1 ns
       ±1500 ns      4.6e6 ns         1.0 ns
  ```

  Below about 1000 ns the clamp rarely bites and both behave; at 1500 the old
  code diverges every time and the new one settles inside 2 ns. Holding is
  unchanged (3.07 ns against algorithm 11's 3.62 on the same plant), and the
  hand-set-gain case improves as well, 53.8 → 16.7 ns.

- **A detector that has stopped tracking now gets noticed.** Second half of the
  26.08 evening. The board reset straight into algorithm 12 with the picDIV
  unsynced: the ramp sat near its top rail, Vphase flat at 3.116 V to ±5 mV for
  the whole 5.5 minute capture. With `LRN` 3000 the usable band is ±1650 ns, so
  that voltage is comfortably INSIDE it — the detector reported a perfectly
  valid +1295 ns that never changed. The loop believed it had a phase, so it
  never armed (`arm=0` throughout), fired one level-0 correction that saturated
  the ±500 LSB clamp, and sat there.

  **The test that ships is a prediction, not a threshold.** The loop knows the
  slew it commanded, so it knows how far the phase should travel in a window:
  `|slew_lsb| / lsb_per_ns` nanoseconds per second. Compare that with what the
  phase actually did — the difference of two half-window means, whose noise is
  `sigma*sqrt(2/W)` rather than `sigma`, so 1.8 ns here instead of 7. When the
  prediction is large enough to be measurable and the phase delivers less than a
  quarter of it, three 32-second windows running, the reading is not connected to
  the oscillator any more. Then arm the picDIV and say so. Replayed against that
  day's oscillator with the detector frozen at +1320 ns, it fires 229 s in.

  **Three versions of this were written and two are not in the tree.** The first
  compared single samples against a reference and reset on any four-sigma
  excursion — which the per-second noise does almost every second, so it never
  counted past one and never fired on the board it was written for. The second
  added a gate that refused to act on a phase not yet shown to be moving, and
  deadlocked: the gate blocked the correction whose job was to move it. Diagnose,
  do not restrain.

  **A fresh-entry arm was also tried and rejected**, on the model of algorithm
  10's boot question. It fires when the detector is FINE and the phase merely
  happens to be far out, and the arm then throws away a good measurement — the
  divider re-syncs to a quantised offset of a few hundred nanoseconds. It is also
  unnecessary: replayed from +1295 ns with a working detector and a real
  frequency error, algorithm 12 pulls in by itself every time, 17 to 19
  zero-crossings, back inside ±17 ns, without arming once. Arming is not a free
  action, "the phase is far out" is not evidence that the divider is lost, and
  the loop does not need help when the detector is honest.

- **The accumulator is thrown away whenever the picDIV is armed.** Arming
  re-syncs the divider to the 1PPS edge, so every phase already in the hierarchy
  was measured against an alignment that no longer exists; keeping them mixes two
  different zeros. Simulated, the first correction after an arm came out at
  −436 LSB from a level-3 test that was half pre-jump and half post-jump. The
  existing `s_mla_post_arm` does not cover this — it keeps the landing transient
  OUT of the accumulator, but the accumulator was already full.
- **Changing the algorithm now restarts the loop you change to.** Nothing did,
  and it had never been asked to: every loop keeps its state in function
  statics, and a static does not know that the operator typed `LA 12`. Reported
  on the bench — algorithm 11 in LOCK, switch to 12, and 12 sat at level 0 with
  zero corrections and the picDIV unarmed.

  Three separate mechanisms, one cause. **Algorithm 12** was left holding
  `s_mla_returning`, the flag a correction raises while its deliberate slew
  walks the phase home; it gates BOTH correction paths (the per-level limit test
  and the `MR` schedule) and the 300-second timeout that releases it only ticks
  while algorithm 12 is the running loop — so leaving mid-slew and coming back
  suppressed every correction until that timeout finally expired, with
  `level=0 corr=0` in the telemetry and nothing to say why. Its accumulator
  hierarchy also still held phase sums from before the switch, so the first
  correction that did fire acted on minutes-old evidence. **Algorithm 10** keeps
  `integ` as an ABSOLUTE PWM target, which is a control voltage chosen for
  conditions that may be hours old, and `prev_state` still reading LOCK means
  the entry transition never fires — so `ltic_autotune()` never runs and the
  picDIV is never armed. **Algorithms 3–9** carried their PID integrators
  across, which is a step correction nobody asked for on the first update after
  the switch.

  The hook lives in `adjustVctlPWM()` rather than in the `LA` handler, because
  that is the one path every change goes through: the CLI, a settings recall at
  boot, and anything else that ever writes `gCtrl.active_algo`. A hook on the
  command would have missed the recall — exactly the case nobody tests. Each
  loop takes the flag once and clears its own state; the legacy loops reuse the
  ring-buffer flush they already had.

  What deliberately SURVIVES a restart is everything the loops have MEASURED
  about the board — algorithm 12's LSB-per-ns, its detector noise floor and its
  measured thresholds, and algorithm 10's noise estimate. Those describe the
  hardware, not the previous run, and rebuilding them would cost minutes of
  running blind on every switch. The per-session counters are cleared for the
  opposite reason: "zero corrections since the switch" is only a readable fact
  if the count starts at zero.

  One thing that was NOT a fault: algorithm 12 not arming the picDIV on entry
  from a locked algorithm 11. It arms only when the detector is blind and the
  frequency is close (`!have_phase && |f| < 0.5 Hz`); arriving from a lock the
  phase is valid, so there is nothing to arm and disturbing the divider would
  only throw the phase to a quantised offset. `arm=0` there is the loop working.

  Verified before shipping, not after the next log. New tool
  `tools/algoswitch/run.sh` builds `GPSDO_algorithms.cpp` for the PC twice —
  once as it stands, once with `algo_take_restart()` forced false, which is the
  code as it was — and runs both against a simulated board (319.5 µHz/LSB, the
  detector at 1252 ns/V, LPOL −1). Same switch sequence, leaving algorithm 12
  mid-slew: before, 30 minutes after the switch back it was still churning at
  level 2 with 308 ns of phase error; after, it settles to level 7 at 4 ns.
  Entering algorithm 10 with the phase 1200 ns off the ramp: before, the
  persisted LOCK is believed and kept; after, it is re-checked, demoted to ACQ
  and the divider re-armed. A centred lock is preserved in both — the point is
  to re-check the claim, not to disturb a good one.
- **Manual errata + clone-receiver support.** The DFU section now covers the
  current WeAct v3.1 boards, which have a BOOT0 *button* instead of a jumper
  (hold BOOT0 while plugging USB, then release — the hold-and-tap-NRST
  recipe found elsewhere does not work), and Part 3 gained a GNSS
  module-choice paragraph: Chinese u-blox clones ignore the binary
  configuration, and on some the unanswered config frames derail the
  auto-baud until a `T` tunnel re-probe rescues them (field report, Solder
  Junkie). New compile switch `GPSDO_FAKE_UBLOX` (off by default): baud
  probe only, zero UBX configuration — nothing the loop needs is lost, the
  PPS never cared about UBX; gone are NMEA silencing, stationary mode,
  survey-in/Time Mode and `qErr`.
- **Algorithms 11 and 12 no longer guess the EFC polarity.** They used to treat an unset `LPOL` as +1 and steer — on an inverted-EFC board every correction pushed away. They now hold with a one-line warning until `LPOL ±1` is set (existing 11/12 installations: set `LPOL` once, then `ES LTIC`).
- **The `ES` help hid the `ALGO12` group.** Both help lines (the `H` main
  list and the `H TZ` page) read `obj: TZ/PID/LTIC/FLAGS/ALGO/PO`, yet the
  parser has accepted `ES ALGO12` since the algo-12 block exists — a user
  following the help would never find the group that persists MG/MR/MF/MFT
  and the per-level limit table, while the `[not saved — run 'ES ALGO12']`
  hints pointed at a command the help did not acknowledge. Both lines now
  list `ALGO12`. The tuner's embedded help already had `ES` right but still
  described `FR 0|1` as an on/off toggle and carried the unreachable
  `LRN 0|1|R` line in the PID section — both corrected to match the firmware.
- **The non-blocking report write silenced the 1 Hz telemetry on USB CDC.** The
  guard added for the "USB in, display freezes" fault asked `availableForWrite()`
  once and dropped the WHOLE report when it returned less than the report size.
  On USB CDC that is every report: `USBSerial`'s transmit queue is
  `USB_FS_MAX_PACKET_SIZE * CDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER` = 64 × 2 =
  **128 bytes** (stm32duino 2.12.0 defaults) and the human report is 400+. Boot
  output and the CLI kept working — short lines, different path — so a board
  that had just logged fine over a UART (512-byte TX buffer via `build_opt.h`,
  where the report happened to fit) went silent the moment logging moved to
  USB, which is why the failure looked configuration-dependent.

  The report is now written in CHUNKS no larger than the room the port reports,
  with a 25 ms budget: a draining host takes the whole report in a few
  iterations (it empties the queue in well under a millisecond of polling); a
  host that is not reading has its tail dropped after the budget and the
  display keeps living, which was the point of the guard. `room == 0` is read
  as "full, wait" and never as a licence to blind-write the rest — on a full
  CDC queue `USBSerial::write()` loops for as long as the host stays connected,
  which is precisely the freeze the guard exists to prevent.

  The CDC transmit queue is also enlarged to 1 KB
  (`-DCDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER=16` in `build_opt.h`; the macro
  is `#ifndef`-guarded in the USBDevice library, so the flag reaches it from
  the sketch — effective on cores where that guard exists, verified on 2.12.0).
  A healthy host now has ~2.5 s of slack and nothing is dropped at all; the
  chunked write remains as the backstop for a connected-but-not-reading host,
  which no buffer size saves.
- **The link failed with `GPSDO_LTIC` off, even after the header fix above.**
  `g_freq_damp_win_dpll` and `g_freq_damp_win_lock` — the FA / FAD / FAL damping
  windows — were defined inside the `GPSDO_LTIC` block in `gpsdo_tasks.cpp`, but
  they are part of the persisted settings block and the CLI prints and sets them
  unconditionally, so `settings_store.cpp` and `gpsdo_cli.cpp` reference them
  whatever the configuration. Four bytes of RAM against a firmware that cannot
  be built without a phase detector is not a trade worth making; the definitions
  moved out of the guard.
- **The firmware would not build with `GPSDO_LTIC` off.** `GPSDO_algorithms.cpp`
  defines the algorithm-12 state, globals and accessors *outside* its own
  `#ifdef GPSDO_LTIC` block, and `gpsdo_cli.cpp` reads them unconditionally —
  but every one of those declarations lived *inside* the `#ifdef GPSDO_LTIC`
  block in `GPSDO_algorithms.h`. Turning the detector off produced fourteen "was
  not declared in this scope" errors from `GPSDO_algorithms.cpp:1437` and the
  whole `ML`/`MLP`/`MG`/`MF` group in the CLI. Separately, the `#endif` closing
  the guard around `multi_level_accum()` sat one line *above* that function's
  closing brace, so with the guard off the brace was orphaned. Nobody had hit
  either, because every board of this design has the detector — the first person
  to build without one was Dave (Solder_Junkie) on EEVblog, whose M8N board has
  no TIC front end. A declaration costs nothing when the definition is absent,
  so the guard now covers the two functions that genuinely need the hardware and
  nothing else. Verified both ways: the tree compiles and links clean with
  `GPSDO_LTIC` on and with it off, and no symbol defined only under the guard is
  referenced from outside one.
- **A blocking USB CDC write froze the display.** `vDisplayTask` drives the
  OLED, LCD, TM1637 and TFT *and* writes the 1 Hz telemetry report, and
  STM32duino's `USBSerial::write()` spins while the endpoint is busy for as long
  as the host is connected. A host that had enumerated the port but was not
  draining it therefore stopped that task dead, inside the write, on the far
  side of the 30 ms serial-mutex timeout — so the visible symptom was a frozen
  display, which reads as a crashed board and was nothing of the kind: the
  frequency and control tasks never touch serial and went on disciplining the
  oscillator throughout. There was no `availableForWrite()` guard anywhere in
  the firmware. The report now asks for room first and drops the whole line if
  it will not fit; telemetry is a live stream, not a log, and the next one is a
  second away. `TeeSerial` gained its own `availableForWrite()`, returning the
  smaller of its two ports, because `Stream`'s default returns 0 and would
  otherwise silence a `GPSDO_BLUETOOTH_PARALLEL` build completely. Reported by
  Dave (Solder_Junkie) on EEVblog.
- **Uptime was counted on a free-running MCU timer, in a GPS-disciplined
  clock.** Measured over thirteen captures on two boards, 1 to 21 hours each:
  the printed uptime gained on the printed UTC by +129 to +169 ppm, best
  estimate **+159 ppm** from the three longest records (+12 s in 20.8 h, +13 s
  in 22.9 h, +7 s in 11.9 h). Both boards agree, so this is not a crystal
  tolerance — the 2 Hz tick that drove the counter simply is not 2 Hz.

  That rate error also produced the ±1 s jitter, which is what a reader notices
  first: the report is printed on the PPS and the counter advanced on TIM9, so
  the two edges slid across each other once every ~6530 s (= 1/159 ppm) and
  each crossing threw a burst of repeated and skipped seconds before settling.
  The bursts in the 19.08 record begin at 325, 6854, 13383 and 19907 s —
  spacing 6529, 6529, 6524.

  Uptime is now advanced by the validated PPS, at the same point where
  `ppscount` is incremented — which is also the event that triggers the report,
  so the printed value cannot be a beat between two clocks. `vUptimeTask` keeps
  the clock running in holdover only, after the PPS has been silent for over
  1.5 s. Simulated against a 159-ppm-fast crystal: exactly 86 400 s over 24 h
  locked, exactly 3600 over an hour of pure holdover, and zero error across 20
  dropout cycles and an hour with 2 % of pulses missing (`tools/uptime_test.cpp`).

- **Two silent drops in the old uptime path.** It took the uptime mutex with a
  5 ms timeout and `continue`d on failure, discarding that second with no
  record that one was owed; and it counted half-second ticks by parity, which
  a binary semaphore's dropped give inverts. Neither exists now: there is no
  mutex, and what is counted is elapsed milliseconds, so a missed, late or
  doubled tick all come out the same and a task stalled for a minute makes the
  minute up.

- **The LOCK update tick used `ppscount % period` while the period could
  change.** The modulo form is correct only for a fixed period, and the cadence
  bound above makes it anything but: with `LIV 300` and a bound of ~110 s the
  tick fired on multiples of whatever `period` happened to be that second, which
  in a 6 h simulation gave intervals from 7 s to 551 s and a MEDIAN of 300 — the
  bound computed the right number every second and almost never got to use it.
  LOCK now gates on elapsed time; ACQ and DPLL keep the modulo form, their
  periods being fixed. Confirmed on hardware: every correction interval in the
  21.08 run is an exact multiple of the 30 s setting, bar one 12 s interval
  during settling where the bound legitimately fired.

- **The LOCK step cap is per UPDATE, not per second.** The 4 mHz cap granted the
  loop a thirtieth of the authority at a 300 s cadence that it had at 30 s, with
  the same drift to cancel; the integrator sat against the cap for update after
  update and overshot when it caught up (simulated phase RMS 174 ns at
  `LIV 300`, against 34 with this fixed). The cap now has a floor at four times
  the frequency step the slope test has just computed, and is unchanged on a
  board with no resolvable slope.

- **Both window lengths were assumed to be `lock_interval_s`.** They are not,
  once the cadence bound can shorten the interval: on a board asking for 300 s
  and running at 73 the slope came out four times too small and the
  extrapolation reached four times too far. Both lengths are now read off the
  PPS counter.

- **The window roll sat inside the pair test, so LOCK would have done nothing at
  all.** After the sample reset at DPLL→LOCK the older window is empty, so the
  test did not run, so the roll did not happen, so the older window stayed
  empty. It survived simulation only because the simulator seeded the first
  window by hand — a difference between model and firmware that the model
  existed to rule out. The roll is now unconditional once there is a newer
  window.

- **The picDIV re-arm was fed straight into the PI.** The divider lands the
  phase at a quantised offset — about ±3 µs on this build — and that jump is not
  a phase error the loop should answer. Algorithm 12 has skipped it since v1.05;
  the three-stage loop armed at three separate places and skipped nothing. All
  three now blank the detector for 5 s.

- **The frequency digits changed colour on `LOCK` only**, so algorithm 12 —
  which reports `CORR` and `ZC` — showed an unlocked colour while locked. The
  stale-echo guard was also ±0.050 Hz against a 10 s average quantised to
  0.1 Hz, so a single legitimate count read as a stale echo; it is now ±0.150.

- **Every plot in the tuner has been squashed 4:1 along the time axis.** The
  shared time buffer was appended to whenever *any* field parsed out of a line —
  which is four of the six lines in a telemetry block — while each individual
  series was appended to once per second. The two therefore filled at different
  rates, and the drawing code paired the newest N samples of a series with the
  newest N *timestamps*, which covered only the last quarter of the span those
  samples occupied. Measured on the 21.08 capture: 1725 telemetry seconds,
  6872 time samples. The "30 h" buffer was 30 h of phase against 7.5 h of clock,
  and the "seconds held" readout was four times the truth. The tick is now the
  `Up:` line, printed exactly once a second by every algorithm, and every series
  is padded forward to it so the lengths match by construction rather than by
  coincidence.
- **`HDOP:TIME` was dropped as unparseable.** A LEA-T that has completed
  survey-in reports its fix mode in the HDOP field, and that flag is the more
  useful of the two facts — it is the mode in which the 1PPS is worth trusting.
  It now passes through as written.

### Added
- **`tools/hostcheck/` — compile and link every switch combination on a PC.**
  Three build failures in a row came out of one person building without a phase
  detector, each hiding the next, and only the last was a LINK error that no
  amount of reading a single file would have caught. This runs the host compiler
  over seven configurations in about thirty seconds and reports any symbol that
  exists in one and not another. It is not a substitute for an Arduino build —
  the stubs are only deep enough to satisfy the includes and the ARM target is
  not exercised — but the class of bug it catches is exactly the one that cost
  three round trips.

- **Measured per-level thresholds for algorithm 12 (`MF`, `MFT`).** The `auto`
  table was not adaptive: sigma pins at its 5 ns floor on every board of this
  design, so the table came out identical everywhere, and its level scaling
  assumes white phase noise (exponent 0.5) where both boards here measure
  0.95–1.03. `MF 3` replaces the assumption with a per-level EMA of the test
  statistic, a least-squares fit of the exponent, and a quantile at the target
  interval set by `MFT`. `ML` reports which source is in use, the fitted
  exponent and the level count. Verified against the firmware's own `ML` output
  and against two hardware records offline; **not yet flown in a closed loop** —
  that measurement is outstanding, and on the quieter of the two boards the
  measured table replayed WORSE than the assumed one (median 13-min RMS 11.6 ns
  against 5.5), because the two tables ask different questions.
- **GPS position redaction in the tuner.** A checkbox, latched when the capture
  file is opened and greyed out while logging, replaces Lat/Lon/Alt in the
  captured log and writes a two-line provenance header. Sat count and HDOP are
  kept.
- **`tools/lock_sim_algo10.py`** — the LOCK-stage simulator the numbers above
  came from, so they can be checked independently.
- **The tuner writes CSV as well as the raw log, and holds a week.** It was
  built as a tuning tool and says so, but it is being used as a logger, and a
  stability run that ends because the buffer wrapped is a run that has to be
  repeated. The plot history goes from 30 h to 604 800 samples — one week at the
  1 Hz telemetry rate — and a dropdown beside **Start logging** chooses *Full
  log*, *CSV only* or *Both*, latched for the life of the file like the
  redaction checkbox.

  The CSV is one row per telemetry second and carries what every analysis of
  these logs has actually needed rather than everything the firmware prints:
  `utc, up_s, algo, state, dph_ns, qerr_ns, vphase_v, pwm, f10, f100, ph_ns,
  level, corr, sig_ns, zc, bmp_c, sat, hdop`. About 65 MB a week against 217 for
  the full log. `Vctl` is left out because it is `pwm` through an RC network and
  `pwm` is the exact number; humidity, pressure and the INA rails are left out
  because across every capture so far they have never moved enough to explain
  anything. There are no position columns at all, so a CSV is redacted by
  construction whatever the checkbox says.

  The row builder assumes nothing about the order of the six lines in a
  telemetry block, because that order is not fixed — the sensor line is printed
  by a different task from the loop line, and a capture from 20.08 has it last
  where one from 21.08 has it first. A row is closed the moment a line tries to
  write a field that is already set, which can only mean the next second has
  begun.

- **Three manual sections that answering the same questions twice earned.**
  *10.1 How to report a problem* asks for the four things every diagnosis here
  has needed — boot log as text, the compiled `gpsdo_config.h`, which GNSS
  module (genuine timing, genuine navigation, or clone), and where the antenna
  sees sky — plus the one check worth doing first: a banner without a
  `compiled <date>` line means a build from before v1.05, and several reports
  have turned out to be bugs already fixed. *Making a survey permanent* (Part
  3.2) covers doing the survey once in u-center V8.29 and saving it to the
  module's battery-backed configuration, which beats the firmware's 300 s / 5 m
  compromise and survives power cycles — Alan Cashin's recommendation, and the
  cheapest accuracy in the build. And *Appendix C — Glossary* defines the
  twenty-one terms this project uses in a particular sense, ADEV through ZC,
  because half of them mean something else elsewhere.

---

## [v1.05-rtos] — 2026-08-20

Algorithm 12 made to work. It was shipped in v1.04 with the arithmetic right and
five separate faults in the machinery around it, each of which hid the next. The
loop now holds phase to 5–8 ns RMS over a 23-hour run with a single picDIV
re-arm, against 10–23 ns for the best previous reference — and every fix below
was simulated before it was flashed, because the two changes in this project that
went out on reasoning alone were both wrong.

### Fixed
- **The noise estimator could only ever fall.** The outlier gate was
  `dp_lim = 5*sigma`, read from the estimate it was feeding: once sigma was
  small, every difference large enough to raise it was rejected as an outlier.
  Measured on 14.08 — `sig` read exactly 2 ns for all 1020 samples of a run,
  and with the per-level limits derived from it the hierarchy pinned at the
  100-unit floor: 79 of 80 corrections fired at level 0. A multi-level
  accumulator that never leaves level 0 is not one. The gate is now absolute
  (300 ns), and the genuine outliers it existed to catch — differences taken
  across a NOPH/SYNC/re-arm gap — are excluded structurally by a contiguity flag
  instead of statistically. Sigma is floored at 5 ns, below which this detector
  cannot honestly resolve.
- **The frequency term had the wrong sign.** It carried `+polarity`, copied from
  algorithm 11's frequency branch — but that branch reads TIM2, and this
  firmware's own algo-11 comment records the hardware finding that TIM2 and the
  LTIC detector have opposite orientation on this wiring. The slope `f_nss` is
  not a TIM2 reading: it is the derivative of the same accumulator values that
  produce the phase term, from the same sensor. A quantity and its own time
  derivative, measured by one sensor, cannot need opposite feedback signs.
  Alan's `cvPWM` agrees — it pushes phase and slope through one conversion and
  adds them. With the plant measured rather than assumed (+319.5 µHz/LSB, from
  regressing the 100 s mean of PWM against the printed 100 s frequency average,
  correlation 0.999 at zero lag) the old sign worked out to `d(phase_rate) =
  +0.4*f_ns`. That is positive feedback.
- **`s_mla_wait` was never reset.** It appeared exactly twice in the file, at its
  declaration and at the `++` in the give-up test, and never went back to zero.
  So about five minutes into any run it passed 300 and the give-up test fired on
  the same second the flag was raised — which killed both things that flag gates:
  the zero-crossing correction and the suppression of new corrections while a
  slew is still walking the phase home. The run that found it: `zc` = 7 in 76
  minutes, all inside the first five, and 1072 of 1174 corrections exactly two
  seconds apart, which is the bare level-0 cadence with nothing holding it back.
  The zero-crossing mechanism had therefore never worked beyond the opening
  minutes of any run since it was introduced.
- **The FLL rescue was a bang-bang loop and could not have been anything else.**
  Its step was `-f*lsb_per_hz*0.10` clamped to ±64, which saturates at
  |f| = 0.256 Hz, while the gate below it only opened at 0.3 Hz — so the
  proportional part could never act. Driven once a second from a 100 s average,
  about 50 s of lag, that gives 64 LSB/s × 50 s = 3200 LSB of travel before the
  measurement responds: 1.0 Hz of overshoot. Both numbers are in the logs
  (462 of 655 consecutive steps exactly ±64; PWM sweeping 12 845 LSB; f100
  swinging −1.29 to +1.55 Hz). It now applies the whole computed correction once
  and holds off for as long as the average it came from needs to refresh, at two
  speeds: the 10 s average while the error is large, the 100 s average once it is
  small, with the hold-off always matching the window in use. The gate moved from
  0.3 Hz to 0.05 Hz, because above 0.147 Hz the phase crosses the whole ±940 ns
  detector band inside one 64 s horizon — the old gate left a dead zone from
  0.147 to 0.3 Hz in which the phase loop could not get a long enough look and
  the FLL considered its work done.
- **`instant_offset` wrapped.** `FREQ_LOWER`/`FREQ_UPPER` admit ±500 Hz and the
  field was `int8_t`, so anything past ±127 fed garbage to every gate that read
  it. Now `int16_t`, in all three files that touch it — the struct, the cast that
  fills it, and the snapshot that copies it. Fixing only one would have compiled
  cleanly and left the wrap in place.
- **The frequency and FLL branches took their sign from a hardcoded minus.**
  Correct only because `LPOL` is −1 on this board; positive feedback on an
  `LPOL +1` board. Both now take the board's `polarity`, which evaluates
  identically here and correctly elsewhere.
- **The dither wrote only one of its two DMA tables.** Double buffering
  alternates every pass, so the other table — still holding the previous code —
  replayed until the next write, and the output flipped between old and new code
  at about 3 Hz (one pass is 2^(24−N) carrier periods = 167.8 ms however N is
  chosen). A 0.8 Hz two-pole filter attenuates 3 Hz by only 14×. Both tables are
  now filled, under a mutex, because `pwm24_write()` is reached from both
  ControlTask and CliTask and two concurrent fills of one table interleave into a
  torn 168 ms replay. Writing the table the DMA is reading is safe by overtaking:
  the fill writes an entry every few microseconds where the DMA consumes one
  every 81.9 µs.
- **`PO` and `AO` could not be set to zero.** The range check was
  `v >= −3000 && v <= 3000 && v != 0.0f`, so the one value a user is most likely
  to want was the one value rejected. Ranges corrected to ±5000 Pa and ±3000 m,
  and both now carry their units in the help and in the echo.

- **The board did not always come up from cold, and the 3.3 V rail was never the
  reason.** `ubx_poll_svin_nav()` called `vTaskDelay()` unconditionally. Its
  sibling `ubx_poll_svin()` carries the guard and the comment explaining it —
  *"before vTaskStartScheduler() this must not be vTaskDelay(): calling it with
  no scheduler hangs the system"* — and the fix went into one of the two and not
  the other.

  Before the scheduler exists, `vTaskDelay()` writes through `pxCurrentTCB`,
  which is still NULL, so the board takes a hard fault and the default handler
  spins with interrupts off: no output, no watchdog, nothing but the reset
  button. The FreeRTOS failure hooks added in v1.04 cannot catch it — they need
  a running kernel.

  It hid behind the call order in `gpsdo_gps_init()`: NAV-SVIN is polled only
  when TIM-SVIN did not answer inside its 500 ms window. A receiver that is
  already running answers, and the board boots — which is the case after a
  reset, because the receiver keeps its own power. One still starting up does
  not, and the board stops dead. That is the cold power-on case, and that
  asymmetry is why this looked for a long time like a supply sagging as the rail
  came up.

  Found in a log with four consecutive boots, each ending after
  `UBX: CFG-NAV5 ACK` and before `LEA-T: starting survey-in`, with the reset
  cause reading `PIN/NRST` every time — the operator's button. The decoder
  prints `POWER-ON/BROWN-OUT` and a "check the 3V3 rail" line when the supply is
  at fault, and it never appeared once. A supply fault does not stop at the same
  source line four times.
- **The frequency digits did not follow algorithm 12's lock.** The colour logic
  has an authoritative branch for loops that publish a live state, and algorithm
  12 was not in the list — so it fell through to the frequency-average branch,
  which is precisely what the comment above that branch says green must not be.

  Measured over a 2.99 h run: the loop and the colour disagreed on **15.0%** of
  samples, and every one of those was the loop LOCKED with the digits WHITE,
  never the reverse. The loop reached LOCK at 108 s and the digits went green at
  1041 s — fifteen minutes of a disciplined oscillator looking undisciplined, on
  every run, because until the 1000 s average fills there is nothing for that
  branch to judge by and `locked` is false by construction.

  Algorithm 12 now takes its colour from its own trend, like 10 and 11. `CORR`
  and `ZC` count as locked: they are one-second states meaning the loop is doing
  its job, the same reasoning that keeps them from resetting `s_mla_quiet`.
  Without that the digits would blink white once per correction — sixteen times
  in the three hours measured. Agreement is now 100%.
- **The stale-echo guard was set to half a count.** It withdraws a long-average
  lock when the 10 s average has drifted, and the threshold was ±50 mHz. But the
  10 s average is a cycle count over ten seconds, so its granularity is 0.1 Hz:
  across three hours it took exactly three values — −100, 0 and +100 mHz — and
  nothing between. A 50 mHz threshold therefore did not mean "within 50 mHz", it
  meant "the counter must read exactly 10 000 000", and one count either way
  killed the green. That is 8.3% of settled samples and 46% of the disagreement
  above. Now ±0.15 Hz: one full count plus half a count of margin, so a
  single-count wobble passes and a real loss of discipline — many counts, which
  is what the guard exists for — still fails it. Algorithms 0-9 carry the same
  guard and get the same fix.

### Added
- **A level gate on the frequency term.** The slope's own noise is
  `sd(f_nss) = sigma * 2^((1−3L)/2)`, so at level 0 it is 1.41·sigma of pure
  noise scaled by 12.5 LSB per ns/s, against the phase term's 0.39 LSB per ns —
  a 32:1 noise-to-signal advantage for the wrong quantity. The log showed what it
  bought: 46% of corrections slammed into the ±470 clamp, one of them with the
  phase reading exactly 0 ns and the correction at full scale. The term is now
  used from level 3 upward, where the same estimate is averaged over 16 s pairs
  and is a measurement again.
- **A TIM2 frequency trim.** When the 100 s average shows more than 0.03 Hz, the
  correction's frequency component is taken from that measurement instead of the
  accumulator slope. It is dormant in steady state — in a 10 h simulation it
  never fired — and that is the point: it catches the frequency excursions that
  would otherwise ramp the phase out of the detector band, so the loop never has
  to re-acquire. The 23 h run that established the numbers above recorded a
  single picDIV re-arm, against 121 for the same settings without it.
- **`configUSE_MUTEXES` and `INCLUDE_xTaskGetSchedulerState`**, set explicitly.
  The dither lock needs both, neither was set by this project, and whether the
  library default enables them is not a thing to leave to chance: one missing
  macro is a build error rather than a runtime surprise.
- **The dither's low 8 bits now reach the loop.** v1.04 shipped the 24-bit
  output and said, in this changelog, that it did not yet give the loop finer
  steps: every algorithm called `gpsdo_dac_write16()`, which left-shifted into
  the top 16 bits so existing settings kept their voltage, and the low byte was
  always zero. It is not any more.

  The fraction is owned by `gpsdo_dac.cpp`, not by the control loop, and that is
  the whole design. The control value is written from 21 places — the `CT` and
  `LC` sweeps, the acquisition ramps, holdover steering, `SP`, and the loop
  itself — and twenty of them are deliberately coarse: a sweep that lands on
  30720.4 instead of 30720 is not a better sweep, it is one whose reference point
  nobody can state. Every coarse write clears the fraction as a side effect of
  arriving at `gpsdo_dac_write16()`, so no caller has to remember to. Keeping the
  fraction in the loop instead would have meant twenty places that each had to
  know to reset it, which is the same class of bug the single write point was
  introduced to prevent.

  What it buys, on the plant measured here: one 16-bit step is about 320 µHz,
  which is 3.2e-11 of 10 MHz — coarser than the 4e-12 the loop was measured
  holding over 10 000 s. It reached that by dithering between adjacent codes from
  one correction to the next, which works but leaves the control voltage hunting.
  With the fraction kept, a correction smaller than one step is applied instead
  of being truncated away, and the step becomes 1.25e-13.

  The truncation it removes was also biased: `(int32_t)` rounds toward zero, so
  every correction lost part of itself in the same direction — which reads to the
  loop as a gain error of up to a sixth at the 6-LSB corrections seen in normal
  operation.

  Nothing above the DAC layer changed. `gpsdo_dac_last16()` still returns a plain
  `uint16_t`, so the displays, the telemetry line and the flash ring see exactly
  what they saw before, and the settings block still stores 16 bits: a restore
  starts with a zero fraction and gives up at most 1.25e-13, which is below
  anything this hardware can show.
- **`DAC` — a command that says what the control voltage actually is.** The
  output path, and for the dither its carrier frequency and table RAM; the code
  in three views — 24-bit, the rounded 16-bit the displays and the flash ring
  use, and the exact fractional value with its difference from the rounded one;
  the measured Vctl; and the step size at both widths, in µHz and as a fraction
  of 10 MHz. A 24-bit code that is not a multiple of 256 is the proof that the
  fine path is driving the pin, which is why all three views are printed rather
  than one, and the command says in as many words whether the fine path is
  active or the output is rounding it away.

  The step figures need the plant gain, which only `CT` can supply. Without it
  the command says so, rather than printing a number derived from a default.
  Listed in the tuner's Help tab as well.

- **`MF` and `MFT` — the per-level limits get their own source, chosen
  independently of the gain.** The two shared one `if`, so `MG 0` meant "gain
  from CT **and** limits from the noise formula" and `MG > 0` meant "gain by
  hand **and** limits by hand". There is no reason they should be welded: the
  gain belongs to the OSCILLATOR — it is LSB per ns, and a different OCXO has a
  different Vctl sensitivity — while the limits belong to the PHASE NOISE the
  board sees, which is a property of the site and the receiver. "Measured gain,
  hand-set limits", which is what a noisy installation wants, could not be
  expressed at all.

  `MF 0` follows `MG` as before and is the default, so nothing moves until
  someone asks. `MF 1` holds the stored table, `MF 2` the noise formula, `MF 3`
  the measured table below. Both settings live in three padding bytes the
  algo-12 block already had, so the layout, the size and `SETTINGS_VER` are all
  unchanged and an older stored block still loads — reading back as 0/0, which
  is exactly the behaviour that build had.
- **`MF 3` — per-level limits measured instead of extrapolated.** The formula is
  `thr[L] = 8·σ·√(2^L)·√10`, and `√(2^L)` says the phase is WHITE, so that
  averaging 2^L samples reduces the test by 2^(L/2). Measured on two boards of
  this design — same PCB, same OCXO, different rooms — the exponent is **0.95
  and 1.03**, not 0.50. Averaging buys almost nothing here, because what matters
  is a slow wander (autocorrelation 0.96 at 60 s, 0.64 at 300 s) and not
  sample-to-sample noise. The error compounds with level: the formula understates
  the real spread about 5x at level 0 and over 100x at level 10, so its table
  falls 32x across the hierarchy where the phase itself falls by 1.3x.

  So the exponent is measured. Each level keeps the mean square of its own test
  statistic, a least-squares fit of log2(sd) against level gives amplitude and
  exponent together, and the table is built from the fit. Fitting ACROSS levels
  rather than trusting each alone is what makes it usable early — level 8 is
  evaluated once every 512 s and would need half a day to have a variance of its
  own, but the low levels populate in minutes and the fit extrapolates.

  Five hard-coded numbers leave with it: the 0.5 exponent, the `8.0` multiplier
  (now the normal quantile for the false-fire rate `MFT` states, which is the job
  the 8 was doing by hand — the hierarchy tests level 0 a thousand times more
  often than level 10), the `√10` white-noise propagation, the 5 ns σ floor —
  a property of this detector, not of the arithmetic — and the 100-unit floor.
  What is left is one number with a physical meaning: how long between
  corrections that noise alone triggered.

  The exponent is clamped to [0.5, 1.0] and that is physics rather than taste.
  Below 0.5 would mean averaging removes more than white noise allows; above 1.0
  the spread is growing faster than flat-in-ns, which is a phase RAMP and not a
  noisier board — and letting a ramp raise the threshold is the failure already
  recorded in this file, where sigma climbed 165 → 746 ns and the loop froze.

  Verified by replaying the firmware's own arithmetic over both boards' records:
  the workshop table comes out at 74 ns falling to 14, which is where that board
  was set by hand after auto proved unstable, and the home board reproduces its
  own settled behaviour. On a three-hour run the home board fitted **α = 1.00**
  and corrected at levels 5 to 9 — the first time this hierarchy has used more
  than one or two of its levels.

  **It is not automatically better.** On the home board, where the formula's
  much tighter table happened to suit a quiet site, the measured table doubles
  the phase RMS (11.6 ns median against 5.5 ns over the same window length)
  because it corrects a third as often. The two tables ask different questions —
  the formula asks whether a deviation is above the measurement noise, the
  measured table whether it is unusual for this board — and which one is right
  depends on the site. That is what `MF` is for.

### Changed
- `LOCK` in the trend field now means the hierarchy is quiet **and** the TIM2
  frequency is within 0.05 Hz, counted on consecutive quiet seconds rather than
  on `s_mla_count`, which resets at every correction and was a poor proxy for
  "how long since anything happened".
- **`GPSDO_PWM_DITHER` is on in the shipped configuration.** It went out in
  v1.04 switched off, while the output path was still being proven; with the
  fine path closed and a 23-hour run behind it, off is no longer the honest
  default. Commenting it out still falls back to the plain 16-bit PWM, and the
  pin, filter and wiring are the same either way.
- **The 320×240 panel's field arrangement now matches the 480×320's.** This
  manual has said since v0.93 that the operating screen is authored once and
  scaled, and that was true of the geometry and not of the content: the two
  panels had drifted apart field by field. qErr moved up to the Alt row, beside
  the fix data it belongs to; AHT and the phase field swapped columns, so the
  environmental sensors share the left column and the electrical ones the right;
  Vcc and Vdd now share the row that freed up. The small panel shows everything
  the large one does.

  Every field that combined a label with a varying-width value was split in two.
  A single right-anchored string pins the unit and drags the label sideways as
  the digits change width — visible on qErr as a label that moved once a second.
  Label and value are now separate slots with separate padding: the label holds
  the column's left edge, the value keeps the right-hand anchor, and only the gap
  between them changes. Same for `dph` and for the INA current.
- **Font 2 is proportional, and this layout had been arithmetic'd at 8 px per
  character.** Checked against the library's own width table, that overstates the
  small panel's strings by about a fifth — `Vph:1.951V` measures 70 px, not 80.
  The error was not academic: it is what had cost the `dph` label, and what had
  kept Vcc at two decimals where the 480 shows three. Both are back. The
  right-hand fields now share one alignment line at x=314 — the one Vdd was
  already anchored to — so qErr, dph, the INA current and Vdd form a column
  instead of four near-misses. Each field's padding is now the measured width of
  its own widest form rather than of today's reading, and the paddings in a row
  tile it exactly, so no background fill can erase a neighbour's edge.

### Credits
- **Alan Cashin** (MIS42N on the EEVBlog forum) is now credited where the work is
  his: in `V`, in the help header, on the tuner's About screen, and in the
  credits table of all three manuals. Algorithm 12, the zero-crossing
  correction, the dithered PWM and the `CS` self-assessment idea all come from
  his Budget GPSDO. He had been thanked for "dither / DAC discussion", which
  understated it considerably.

### Measured
Twenty-three hours, automatic thresholds, `MR 9`, dither at 13 bits:

| | this run | best previous |
|---|---|---|
| phase RMS, settled | **5–8 ns** | 10–23 ns |
| \|phase\| < 10 ns | **86.7%** of samples | — |
| picDIV re-arms | **1** | 121 |
| correction levels reached | **5–6 typical, up to 8** | 0 |
| 10 000 s frequency | **4e-12** | 1.4e-11 |
| interval between corrections | 254 s | 130 s |

`NOPH` three times in 82 572 samples; `FLL` once. Ambient pressure fell 4 hPa
across the run and the loop did not react.

---

## [v1.04-rtos] — 2026-08-11

### Added
- **`GPSDO_PWM_DITHER` — 24-bit control voltage from a dithered short PWM.**
  Idea from Alan Cashin (MIS42N): run the PWM at fewer bits than you need and
  vary the duty from period to period so the average carries the rest.

  The gain is the CARRIER, not the extra bits. Ripple has to be filtered below one
  output step, and how hard that is depends on the gap between carrier and filter
  corner: the 16-bit PWM at 2 kHz allows a 0.7 Hz corner and a 230 ms time
  constant, while 13-bit dithering at 12.2 kHz allows 4.2 Hz and 38 ms. Filter
  delay goes straight into the loop as phase lag, so a six-fold shorter filter is
  worth more than the resolution.

  Alan dithers in a timer interrupt because a PIC has no DMA. That would be 12 000
  interrupts a second here, competing with the 1PPS capture — the one interrupt
  that must not be delayed. But the pattern for a constant value is periodic, so
  it is computed once into a table and replayed by DMA into the compare register:
  0.012% CPU at 13 bits, and none of it in an interrupt. The average is exact by
  construction — the table holds exactly Y entries of X+1 among 2^(24-N).

  Same pin as before (PB9, TIM4 CH4), so the existing filter and wiring are
  unchanged. TIM4_UP drives DMA1 Stream 6 Channel 2; the 2 Hz tick is on TIM9 and
  the 1PPS chain on TIM2/TIM3, so nothing else is disturbed. Two buffers in
  hardware double-buffer mode mean a value change never glitches the pin.

  Off by default. Costs 8 KB of RAM at 13 bits, 16 KB at 12.

  **What this does not yet do** is give the loop finer steps: every algorithm
  calls `gpsdo_dac_write16()`, which left-shifts into the top 16 bits so existing
  settings keep their voltage. The low 8 bits wait for a loop that calls
  `gpsdo_dac_write24()`.
- **Zero-crossing correction, from Alan's flowchart.** After a limit correction
  changes the frequency, the phase keeps moving in the direction it was already
  going: it sweeps through zero, out the other side, and usually fails the limit
  again — so the loop corrects, overshoots, corrects back, and settles slowly.

  The instant the phase crosses zero is special. The phase error is nil, but the
  frequency error that carried it there is still present; cancelling the frequency
  error exactly then leaves the oscillator with the right frequency AND no phase
  error, rather than a state the loop has to iterate towards.

  Measured against Alan's own logs from the same design: his loop corrects every
  506 seconds where this one corrected every 130. Most of that gap is this test,
  which he describes as essential and which was missing here.

  Reported as `zc=` in telemetry, trend shows `ZC` at the moment it fires.
- **FreeRTOS failure hooks and a project-local `STM32FreeRTOSConfig.h`.**
  `configCHECK_FOR_STACK_OVERFLOW` and `configUSE_MALLOC_FAILED_HOOK` both
  default to 0, so a blown task stack silently corrupts a neighbour and
  `configASSERT` traps in a `for(;;)` with interrupts off — a dead white panel
  with nothing on the console. That is exactly how the last three faults
  presented: a CLI stack too small for the flash ring write, a NULL event group
  read before the scheduler started, and a struct declared in a dead branch that
  still grew the display task's frame.

  The override turns both hooks on and redefines `configASSERT` to print the file
  and line before trapping. The hooks name the offending task on the USB console
  and blink the LED, so the next one identifies itself in seconds rather than
  hours. Bare `Serial`, not `OUT_SERIAL`: a hook must not touch a mutex or a
  Bluetooth stream that may itself be the thing that failed.

  Credit to GLM-5.2 for writing this while the white-screen fault was being
  chased; it is adopted here essentially as written.
- **Algorithm 12 — multi-level accumulator.** After Alan Cashin's (MIS42N on
  EEVblog) Budget GPSDO. Every other loop here has one time constant, and that is
  a compromise nobody wins: measured against a rubidium reference, `LTC 60` is up
  to 1.58x better past 800 s while `LTC 240` is up to 1.44x better between 10 and
  400 s. This one does not choose. Readings accumulate into levels — level n
  covering 2^n seconds — and a correction fires at the **lowest** level whose
  error exceeds its limit, so a large error acts within two seconds and a small
  one waits for a longer average. There is no `LTC` to set.

  The levels come out of the bit pattern of the seconds counter rather than an
  array of buffers: eleven levels, 2 s to 2048 s, for 22 bytes.

  **Input is phase in nanoseconds from the LTIC detector.** The first attempt fed
  the TIM2 count error in whole hertz and was blind — a disciplined oscillator
  sits far below 1 Hz, so the field read zero in 83% and 95% of samples across two
  runs and nothing ever accumulated. Phase integrates where a one-second frequency
  count does not. Alan asked why 100 ns had been quoted when a TIC resolves 1 ns;
  he was right, that was the counter's resolution, not the detector's. Boards with
  no detector integrate the count into a phase estimate themselves.

  **The frequency test is gone**, on Alan's own advice: *"It was an experiment...
  what we want is a stable system where the tests always pass. So the frequency
  test is unnecessary."*

  New commands `MG`, `MR`, `MLP` and `ML`, saved with `ES ALGO12`. The per-level
  limits are runtime-editable and persisted because only **one** was ever derived
  — 125 ns at 128 s, from the original 10 MHz ±0.01 Hz specification. Alan calls
  the rest arbitrary, so there is nothing to reproduce faithfully beyond that
  anchor, and every board will want its own.
- **Reset-cause reporting at boot.** `RCC->CSR` is read and decoded before
  anything else runs, so an intermittent restart no longer looks identical whether
  it came from a brown-out, the reset pin or a software reset. Added after a board
  rebooted repeatedly at the same point in GPS configuration with no way to tell
  which.

### Fixed
- **Algorithm 12's thresholds are now measured, not inherited.** They were taken
  from Alan's design and scaled by the ratio of counter step sizes, which is the
  wrong quantity: what a threshold must clear is the NOISE on the phase
  measurement, and that differs between builds for reasons a step size does not
  capture. Measured on this board: mean phase -1 ns with a standard deviation of
  462 ns — the oscillator was correctly tuned and all of that spread was noise,
  while the level-0 threshold sat at 462 ns. 41% of samples crossed it. 620
  corrections in 1685 seconds, the hierarchy resetting every 2.7 s and never
  reaching level 2.

  The firmware now estimates the phase noise continuously and sets each level's
  threshold from it. A second error surfaced doing so: the threshold applies to
  the test expression |3b - a|, whose deviation is sigma*sqrt(2^L)*sqrt(10), not
  to the mean phase, whose deviation is sigma/sqrt(N). Using the latter made the
  threshold 4.5x too low at level 0 and worse above. Six sigma on the correct
  quantity brings the correction interval to about a minute, against the 256 s
  Alan's design settles into.

  `ML` reports the measured noise and whether the limits are following it.
  Telemetry carries it as `sig=`. Setting `MG` above zero holds the stored table
  instead, for anyone who would rather tune by hand.
- **The tuner stopped reading anything back from the board.** `STATE_HINT` was
  added to `TelemetryParser` but referenced as `self.STATE_HINT` from
  `GpsdoTuner` — a different class. Every telemetry line then raised
  `AttributeError` inside the line handler, so no reply ever reached its absorber
  and the LL calibration fields and the algo-12 limit table both stayed empty.
  Two symptoms, one crash.

  The handler is now wrapped: a parse failure costs one line and prints to the
  monitor, rather than silently killing reception. That the fault presented as two
  unrelated parsing bugs, and was only found because the console traceback was
  quoted, is the argument for the wrapper.

- **`MG` and `LG` both answered `gain=`.** The Lars absorber runs first in the
  line handler and matched the algo-12 reply, returning before it could reach the
  algo-12 boxes. The firmware now answers `m_gain=` and `m_run_level=`, so the two
  command sets no longer share field names.
- **The tuner's algo-12 limit table showed zeros.** It was never read back: the
  parameter query asked for the scalars only, so the eleven spin boxes sat at zero
  until somebody typed in them — and pressing Send would then have written eleven
  zeros over a table the firmware had defaults for. A control that displays a
  value the device does not hold is worse than one that displays nothing.

  The table is now read on connect, `MLP n` with no value prints its setting, and
  Send refuses while any row is still zero. `MLP` and `ML` also print the phase
  threshold each limit represents alongside the raw accumulator value — the raw
  number is what you set, the nanoseconds are what it means, and only the latter
  can be compared against a scope.
- **Algorithm 12 ignored the detector polarity and confused nanoseconds with
  hertz.** Two faults in the same conversion, found together from one log.

  `LPOL -1` was not applied at all — algorithm 11 multiplies its phase term by
  `-polarity` and this did not — so on such a board every correction went the
  wrong way. And the average phase, in nanoseconds, was multiplied by LSB-per-hertz
  as though the two were the same quantity: nulling P ns over T seconds needs
  P/(100*T) Hz at 10 MHz, so the correction came out 100*T times too large, from
  200x at level 0 to 102400x at level 9. Every correction hit the +/-2000 clamp.

  Positive feedback four orders of magnitude too strong is a fair description of
  what the log showed: 6000 counts of PWM swing over 148 corrections.

- **`MG` and `MR` were accepted and stored but never read.** The commands worked,
  the tuner sent them, `ML` listed them back, and the algorithm used neither — a
  hand-set gain did nothing and the forced-correction level did not exist. Both
  are now wired: `MG` overrides the CT-derived scale in LSB per ns, and `MR`
  forces a correction once its level is reached, whatever the limits say, which is
  what stops a drift slow enough to stay under every limit from accumulating
  forever.
- **Algorithm 12 now requires the LTIC detector, and holds instead of guessing.**
  It was written to fall back to integrating the TIM2 count error on boards with
  no detector. That fallback was actively destructive: the count is quantised to
  whole hertz and reads zero on a disciplined oscillator, so integrating it
  produced a random walk of quantisation noise rather than phase. The walk crossed
  a level limit, a correction fired and hit the clamp, the oscillator was thrown
  far enough to rail the detector, and railing kept the fallback running.
  Measured: 6000 counts of PWM swing across 148 corrections with the detector
  railed 58% of the time and the reported phase stuck at 0 throughout.

  `LA 12` now refuses without `GPSDO_LTIC`, and when the detector is fitted but
  not reading — railed, saturated or not yet armed — the algorithm holds and lets
  the picDIV bridge do its work. A silent fallback that destroys the lock is worse
  than refusing to run.
- **Algorithm 12 now arms the picDIV.** It did not, and the failure was silent:
  with the ramp railed the detector never returns a valid reading, so the code
  fell through to integrating the count error and the algorithm was blind again —
  the very thing moving to the detector was meant to fix, with nothing in the
  telemetry to say so. Same hold-off bridge as algorithm 11.
- **Algorithm 12 could be selected but never persisted.** `LA` gained a branch for
  12, but `settings_recall` still clamped the stored value to `<= 11`, so the
  setting saved correctly and was silently dropped on the next boot — which looks
  like the flash ring failing rather than a stale constant in the recall path.
- **White screen at boot.** The algorithm-12 telemetry declared a stats struct
  inside `print_human_report()`, which runs in the display task. A stack frame is
  sized at compile time, so a struct in a branch that never executes still
  reserves its space on every call; twenty bytes took the display task over its
  stack and it died before `tft.init()`. Now static.

### Changed
- **`SETTINGS_VER` 4 → 5** for the algo-12 block, **with migration**. A v4 block is
  accepted, its fields applied, and the algo-12 values left at defaults. Rejecting
  it outright would have discarded a working PID, LC and timezone because a new
  algorithm was added.

## [v1.03-rtos] — 2026-08-01

Built on v1.01. The v1.02 experiments — a sigma-delta DAC on PB5 and support for
STM32duino core 3.0.0 — are not carried forward: the first was measured and found
not to deliver what it promised, the second locked the board up on hardware.
v1.01 remains the tested base, with two additions.

### Fixed
- **A warm reboot no longer restarts a finished survey-in.** The receiver keeps
  its own power and its own state across `RB`, so a survey completed before the
  reset is still valid: the position it established has not moved. The firmware
  previously commanded a fresh survey regardless, discarding a result that took
  minutes to reach and dropping the module out of Time Mode while it repeated work
  already done. `gpsdo_gps_init()` now polls TIM-SVIN first and skips the start
  when the receiver reports valid=1 with active=0 — Time Mode with a finished
  survey behind it. Reported as *already in Time Mode from an earlier survey*.

  This required making `ubx_poll_svin()` safe to call before the scheduler: it
  yielded with `vTaskDelay()` unconditionally, which hangs the system when no
  scheduler is running. It now uses `delay()` in that case, the same pattern the
  ACK reader already used.

- **The board would not start: no LED, no console, nothing.** `setup()` writes the
  DAC three times — the initial 127, the recalled PWM and the default — before
  `xEventGroupCreate()` has run. The new correction statistics hang off the DAC
  write path, and their gate read `xSysEvents`, which was still NULL at that
  point. Passing NULL to `xEventGroupGetBits()` trips `configASSERT` and halts the
  processor, so the failure happened before the first blink and left nothing on
  the console to explain it. The gate now checks for NULL first; those early
  writes are commands rather than corrections, so excluding them is correct as
  well as safe.

### Added
- **`CS` — correction statistics, the loop assessing itself.** Algorithm 11 was
  validated against a rubidium standard on someone else's bench; almost nobody who
  builds this has one, and without it there is the author's word and a lock
  indicator. The correction the loop applies is the error it just observed, so the
  size of those corrections says whether the discipline is working — and GPS is
  the reference, so nothing better exists to compare frequency against. The
  firmware already computed these numbers and threw them away.

  Reports RMS correction over the last **100, 1 000, 10 000 and 100 000
  corrections**, in DAC counts and — once `CT` has measured the oscillator slope —
  in fractional frequency, directly comparable to an ADEV figure. Also the steady
  bias, non-zero when the loop is tracking real drift rather than noise.

  The windows count corrections rather than seconds because the correction rate
  depends on the algorithm: algorithm 11 steers once a second, algorithm 10 once
  per `LIV`. Labelling them in minutes would have meant one thing under one
  algorithm and sixty times that under the other — the same number describing two
  different spans. `CS` measures the actual interval and prints what the windows
  currently cover in wall-clock time, so the reader does not have to work it out.
  At one correction per second, 100 000 spans about 28 hours.

  These are exponential weights, not hard windows: roughly 63% of the weight falls
  inside N corrections and 95% inside 3N. That costs four multiply-adds per
  correction and no memory, where a buffer holding 100 000 samples would take most
  of the RAM budget to answer the same question no better.

  Counted only while locked and with no calibration running: the acquisition ramp,
  the three jumps `CT` makes and the `LC` sweep are commands, not corrections, and
  one of them would dominate the hour-long average long after it ended.
  Algorithms 0-9 have no lock state to gate on and are excluded, which `CS` says
  rather than reporting a number with no defined meaning.

  **The caveat is in the output, the header and the README:** it measures whether
  the LOOP IS SETTLED, not whether the OUTPUT IS GOOD. A noisy detector makes the
  loop chase noise; the corrections grow, `CS` reports them faithfully, and the
  oscillator was fine until the loop made it worse. Nothing measured from inside
  the loop can see that.

  The idea is Alan's (MIS42N on EEVblog), whose own design relies on exactly this
  and therefore needs no secondary standard.
- **`GPSDO_DAC_EXT` — external SPI DAC, planned, not implemented.** Enabling it is
  a compile error by design: `dac_ext.cpp` is a stub with no device chosen. The
  16-bit PWM gives about 50 uV per step at 3.3 V, near 2.7e-11 fractional on a
  5.3 Hz/V oscillator; an 18-bit part with a reference designed for the job reaches
  roughly 17 uV, near 9e-12, with no filter delay in the loop.

  No hardware SPI is needed or available — SPI1 belongs to the TFT and every SPI2
  pin on this package is taken — but the DAC is written once per second, so
  bit-banging costs microseconds. Suggested pins PB0, PB2, PB4, chosen to avoid
  PB6/PB7: those look free but are the default I2C1 pins that `Wire.begin()`
  claims, and a DAC there would break the sensors and the clock display.

### Changed
- **All 23 `analogWrite(PIN_VCTL_PWM, ...)` call sites now go through
  `gpsdo_dac_write16()`.** Adding a second output path by editing each of them
  would have invited a missed one, and a missed call site is the worst kind of bug
  here: the loop would steer correctly almost always and jump whenever the stale
  path was taken. Adding a DAC now means filling in one function.

## [v1.01-rtos] — 2026-07-29

> **Build with STM32duino core 2.12.0 or earlier.** Core 3.0.0 (23 July 2026)
> deploys ArduinoCore-API, which removes `ltoa()` and turns `HardwareSerial`
> into an abstract interface — both used here — and, more importantly, leaves
> TFT_eSPI unable to initialise the panel (white screen, CLI unaffected). The
> first two are small and could be made version-conditional; the third lives in
> the library. See the README for detail.

Milestone release: merges the flash-ring persistence branch with the algorithm 11
(LTIC-Lars) branch. Algorithm 11 is based on the original continuous-PI GPSDO
controller by the late **Lars Walenius**, shared with the time-nuts community; it
is extended here with the auto-calibration and acquisition work below, in his
memory.

### Added
- **Algorithm 11 "LTIC-Lars"** — a single continuous PI loop (no ACQ/DPLL/LOCK
  state machine), disciplining the OCXO from the hardware TIC phase. Selectable
  with `LA 11`; trend LFQ (freq-led) / LPH (phase) / LLK (locked). Tuned live
  with LG/LD/LTC/LFD/LTO/LPL/LPF/LTK/LTR.
- **CT auto-calibration for algorithm 11.** gain defaults to 0 = auto: the loop
  derives its frequency scale from the CT-measured K (Hz per PWM LSB), the same
  constant algo 10 uses, so one CT calibrates the Lars loop too. A non-zero LG
  overrides with a manual scale.
- **Frequency-led acquisition** with a dominant self-braking proportional term,
  a step clamp and anti-windup, so a cold start pulls in without the runaway or
  the ±2 Hz oscillation seen during development.
- **picDIV phase-capture bridge**: once the frequency is settled but the phase is
  still railed, the picDIV is re-armed once to bring the phase into the detector
  window, where the phase branch completes the lock.
- **Tuner: versioned and matched to the firmware.** The tools now carry a
  TOOL_VERSION tracking the firmware release, and the tuner reads the board's own
  version on connect: a mismatch is reported in the status bar and the monitor
  rather than left to show up as fields that read oddly. The main window now opens
  maximised with the splash over it, and on Windows the console spawned by
  double-clicking the script is minimised to the taskbar (only when the tuner owns
  it — a terminal the operator opened is left alone).
- **Tuner: Help tab and boot splash.** The tuner gained a Help tab with the full
  command reference grouped by topic, and a three-second splash animating two
  phase-shifted sine waves converging into one — the same lock metaphor as the
  firmware's TFT boot screen (click to skip). It also reads every parameter group
  on connect (LTIC, FA, PID 3-9, Lars) instead of just two, and algorithm 11 has
  its own tab beside algorithm 10.
- **Flash-ring persistence for algorithm 11.** All g_lars parameters are stored
  in the flash ring alongside the LTIC settings (SETTINGS_VER 2); `ES LTIC` saves
  both. No EEPROM anywhere — persistence is 100% flash ring.

### Changed
- **`LC` warns when run before `CT`.** The two are not independent: `LC` needs the
  Hz-per-LSB slope that `CT` measures, and without it falls back to a generic
  value. The failure is quiet rather than obvious — one board reported
  ns_per_volt 1592.8 before `CT` and 921.2 after, a factor of 1.7, with nothing in
  the first run to suggest it was suspect. `LC` now says so up front and continues
  anyway, and the README states the order explicitly.
- **Every setting now says whether it was saved.** Preferences that do not touch
  the control loop — timezone (`TZ`/`TO`/`LT`), sensor offsets (`PO`/`AO`) and the
  boot/survey flags (`WU`/`SPL`/`SV`) — save themselves, and the reply names the
  group that was written. Loop tuning stays manual, and its reply names the exact
  command that would keep it, e.g. `[not saved — run 'ES LTIC' to keep it]`, so
  the group never has to be guessed. `SET_FLAGS` carries `SAW` and `LRN` alongside
  the boot flags, so an auto-save there commits them too; the message lists the
  whole group rather than hiding it. A rejected value is reported as such —
  `[not saved — value out of range; accepted range shown above]` — rather than
  offering an `ES` command for a change that never happened.
- **`LT` is now persistent.** The command was implemented but had no field in the
  settings block, so the UTC/local choice did not survive a reboot. Added to the
  timezone group (SETTINGS_VER 4).
- **`CT` auto-saves its result.** Like `LC`, the three-minute calibration now
  writes its coefficients to the flash ring on success instead of asking the
  operator to remember `ES PID`. Only the PID group is written, so live loop
  tuning in progress elsewhere is untouched.
- **Algorithm 11 trend labels renamed** to ACQ / PLL / LOCK, matching algorithm
  10's vocabulary so the displays, CLI and tuner read consistently. Algo 11 shows
  PLL where algo 10 shows DPLL, which still tells the two apart in a log.
- **Learn telemetry reports what actually steers each loop**: algo 11 shows gain
  mode / scale / filtered phase, algo 10 shows its state machine, algos 3-9 keep
  the LRN figures. qErr stays on every line (shared by both LTIC branches).
- **CT message** now states it tunes algos 3-9 plus LTIC 10 & 11.
- **Persistence wrappers renamed** eeprom_* → persist_* to reflect that storage
  is the flash ring, not EEPROM; the names no longer mislead.

### Fixed
- **Survey-in never went to the background after a reset.** The patience timer
  ran from the host's own boot, but a timing receiver keeps surveying across an
  MCU reset — it has its own power and state, and reports its own elapsed time.
  So every reflash restarted the clock and granted the survey another full cap in
  the foreground, indefinitely. Observed on the bench: the receiver reporting
  4450 s of survey against a host uptime of 7 minutes, with the timeout message
  never printed once. The deadline now expires when EITHER clock passes the cap,
  and the message says which one ran out.
- **Algorithm 10 could freeze during a healthy pull-in.** The runaway guard fired
  on a railed detector plus a large frequency error alone — but that is the normal
  state of a cold or far-off OCXO at the start of acquisition, and freezing there
  removes the only path back, since the frequency term is exactly what pulls the
  oscillator into the detector window. One observed run travelled 3855 LSB during
  a perfectly healthy DPLL pull-in and was frozen mid-recovery. Both guards now
  additionally require the error to have stopped improving for several cycles
  (LTIC_RUNAWAY_STALL), which a genuine wrong-polarity runaway trips and a healthy
  acquisition never does. The rail threshold is also taken from the LC calibration
  again instead of a fixed 3.28 V that only suited one board.
- **`LIV` was capped at 30 s.** Both the CLI and the loop clamped the LOCK
  correction interval to 30, and the loop snapped an out-of-range value to 5 s —
  so asking for a slower loop silently gave you the fastest one. Restored to
  1..600 s, clamping to the nearest bound. This mattered immediately: a tester
  comparing LIV 30 against LIV 60 would have had the 60 rejected.
- **Settings were never actually persisted.** The ring's slot header stored the
  payload length in a single byte, so anything over 255 B wrapped: a 324-byte
  settings block was recorded as 68. The data itself was written correctly and the
  CRC covered it, so nothing looked wrong — but every read returned a truncated
  length, leaving the tail of the recalled block as whatever was on the stack.
  That is where the stray `temp_coeff=-1` came from, and once the length was
  checked strictly the recall rejected the record outright and the board came up
  on defaults. The length field is now 16-bit (slot header 4 B → 6 B, payload
  506 B → 504 B) and the ring magic is bumped so an older ring reformats itself
  instead of decoding as garbage. This affected the flash-ring branch from the
  start — GML's own block was already 292 B, likewise over the limit.
- **Stack overflow when writing the flash ring.** Saving settings needs about
  1.4 KB of stack — `fr_write()` builds a 512-byte slot image plus a 512-byte
  read-back copy, and `settings_store` adds a ~324-byte block on top — but the CLI
  task had 1 KB and the control task 1.5 KB. The CLI task overflowed into its
  neighbour, and the board printed its save confirmation and then hung with the
  display frozen. Both stacks are raised with head-room (CLI 1 KB → 3 KB, control
  1.5 KB → 3.25 KB; 4 KB more RAM out of 128 KB). The hazard predates the
  auto-save work — `ES` was equally exposed — but auto-save made it easy to reach.
- **`EW` reported the wrong flash sector.** The ring has always lived in sector 7
  (0x08060000, the last sector, so firmware keeps the maximum contiguous space
  below it), but the `EW` message hardcoded "sector 6, 0x08040000" — the one place
  an operator looks was the one place that lied. The message now reads the address
  from the implementation via new `flash_ring_sector_no()` / `flash_ring_base_addr()`
  accessors, so it cannot drift again. The bring-up documents carried the same
  stale figures and are corrected in all three languages: the firmware ceiling is
  393216 B (384 KB), not 262144 B, and the J-Link erase range for wiping the ring
  is 0x08060000-0x0807FFFF, not the sector-6 range that would have left the ring
  untouched.
- **LC no longer throws away its own convergence.** The rate-nulling loop stopped
  after three tries and, if it had not yet landed in the acceptance band, fell
  back to `saved_pwm + offset` — which assumes the saved PWM sits at the lock
  point. Run before the oscillator is near 10 MHz that assumption is badly wrong:
  an observed run converged -244, -57, -16 ns/s (one step from the band), then
  discarded that and sampled at a PWM running at -244 ns/s, where the phase
  crosses the whole detector window between publications. Every picDIV arm landed
  on the rail and the calibration aborted. The loop now gets six tries, and when
  it runs out it keeps the steered PWM instead of jumping back.
- **FA / FAD / FAL restored.** The per-state damping-average window (and the
  `damp_e_freq` term it feeds in algorithm 10) was present in v0.97 but absent
  from the flash-ring branch, so it was lost in the merge. Restored, and now
  stored in the flash ring rather than EEPROM.
- **Settings records are length-checked.** `settings_recall` and
  `settings_save_partial` accepted any record of two bytes or more into a
  stack-allocated block, so a record shorter than the current struct left the
  tail as stack garbage — and a partial save would write that garbage back. Both
  now zero the block first and require the exact size.
- **settings_store.cpp now compiles.** It read three globals it could not see —
  g_pressure_offset, g_altitude_offset (defined in gpsdo_control.cpp, with no
  header of their own) and g_qerr_enable (declared in ubx_timtp.h, which was not
  included). Added the include and the two local externs, following the pattern
  the rest of the project uses.
- **LT command implemented.** The help had always documented `LT 0|1` and the
  display and report paths had always read g_show_local_time, but the CLI handler
  was never written, so the verb silently did nothing. It now toggles and reports
  UTC vs local time as documented.
- **Serial dph now matches the panel.** The TFT row subtracted the receiver
  sawtooth but the serial report did not, so the same instant read differently on
  the two — a whole sawtooth apart (~±10 ns on a LEA-6T, more on an M8T). The
  serial path now subtracts it as well, as its own comment already claimed.
- **CR (cold restart) now really erases the ring.** persist_erase() calls the new
  flash_ring_wipe(), which physically erases and reformats the ring sector, so a
  cold restart genuinely returns to defaults instead of merely marking state stale.

## [v0.95-rtos] — 2026-07-16

### Added
- **Timezones with DST, anywhere in the world.** `TZ Adelaide` is now enough to
  get the clock right, including its half-hour offset and its
  southern-hemisphere DST. City names are accepted on their own — they are
  unique across the entire IANA database, so the region is optional
  (`TZ Australia/Adelaide` works too), and case is ignored.

  The rule can also be typed in full: `TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3`. That
  form matters when a government changes the rules and this firmware hasn't
  caught up — the user can fix it from the CLI rather than waiting for a
  release.

  407 zones and 88 rules are built in, generated from the system tzdata by
  `tools/gen_tz_table.py`. The full IANA database is ~2 MB, four times this
  MCU's entire flash, and its real value is being updated several times a year
  — which a GPSDO with no internet cannot benefit from anyway. The POSIX TZ
  string each zone reduces to is 4–44 bytes and captures the same present-day
  behaviour, so that is what is stored. Cost: ~7 KB of flash.
- **`H TZ`** — the first per-command help page. `TZ` takes two quite different
  arguments and the difference matters, so it gets a page of its own rather
  than a cramped line in the main list.
- **`TO` now accepts minutes**: `TO 9:30`, `TO -3:30`, `TO 5:45`. Plain hours
  still work.
- **Vcc on screen (480×320).** Requested by Dan Wiering alongside Vdd. The 5 V
  rail was already measured but had nowhere to go — every cell in both columns
  was spoken for. The `Alt` cell had ~134 px of slack after the altitude, so it
  gives up its right half, and the fields were regrouped to earn their keep:
  `qErr` moves up next to `Alt` (it is the receiver's own report on its 1PPS, so
  it belongs with the fix data) and `Vcc` takes the space `qErr` left beside
  `Vdd`, so the supplies sit together. `Vdd` regains its second decimal, which
  it only ever gave up to make room for `qErr`.

  Both are 480-only. At 320 `Alt` and `qErr` want ~168 px and the cell is 148,
  so that panel keeps the old arrangement.

### Fixed
- **Reported by Dan Wiering: auto timezone missed DST in South Australia.**
  Two separate bugs, only one of which was visible. `TO A` guesses the zone
  from longitude and applies the EU DST rule, so outside Europe it gave no DST
  at all — that was the reported symptom. But it also returned whole hours,
  and Adelaide is UTC+9:30, so the clock was half an hour out even in winter
  with DST fixed. India (+5:30), Nepal (+5:45), Newfoundland (−3:30) and
  Chatham (+12:45) had the same silent error.

  `TZ <zone>` resolves both. `TO A` is unchanged and still there — it is right
  across most of Europe and needs no configuration — but it now says plainly
  what it cannot do.
- **The frequency reading jumped sideways on the 320×240 panel.** v0.94 removed
  the `dtostrf` field width on the theory that a fixed-width font already keeps
  the digits in columns. It does — but a string that loses a character still
  gets re-centred, moving every glyph half a character. The field width is what
  makes the *string* a constant length, and it is back, as it has been since
  v0.89. The 480×320 panel is untouched: it anchors the reading by its right
  edge instead, which is verified on-panel.
- **The side rails vanished beside the frequency.** The frequency sprite clears
  its whole band before drawing, and drew only the separator line above itself
  — so the rails from the initial layout were wiped from that band on the very
  first update, and the frame appeared not to meet the header line. The sprite
  now carries the rails too. Both panels.
- **Vdd was only shown on an LTIC build.** It sits in the phase row, and the
  whole row was behind `#ifdef GPSDO_LTIC` — so a board without the TIC could
  not see its own 3.3 V rail, for no better reason than where the field happened
  to be written. The rails now sit outside that guard: `Vcc` and `Vdd` show
  whatever the panel, and the phase field alone stays LTIC-gated, leaving the
  row's left half empty without it. `qErr` stays gated too, since it only ever
  appears under algo 10.
- **`CT` displayed "Tune 0s" for its entire run.** It set the calibration flags
  but never seeded the countdown, unlike `C` and `LC`. Three points at
  `OCXO_CALIB_SECS` each, so 185 s.
- **`qErr` shifted about on the 480×320 panel.** Left-anchored, the field grew
  rightwards as the value changed width and "ns" walked back and forth.
  Right-anchoring the whole string fixed the unit but then dragged the `qErr:`
  label along with the digits instead. The label and the value are now two
  fields: the label is pinned to the slot's left edge, the value keeps a right
  anchor so the unit stays put, and only the gap between them changes — which is
  how the `Vph`/`dph` row has always behaved.

### Changed
- **`dph` printed a confident number long after the detector had stopped
  measuring.** `ns_per_volt` is a local slope, read around the anchor LC places
  at 0.632·Vsat; the ramp itself is `V = Vsat·(1 − e^(−φ/τ))`, so away from that
  anchor the curve flattens and the linear reading understates the phase. Past
  Vsat there is no reading at all — the stop pulse has missed its window and the
  capacitor charges on to the supply rail. The display reported that state twice
  as a rock-steady "+1561 ns", and both times it cost a measurement before
  anyone thought to check the voltage next to it. `dph` now reads `ovf` outside
  15–85% of Vsat, with `Vph` beside it saying which end it ran out of.

  Vsat is not stored — LC fits it, places the anchor and discards it — but the
  anchor is 0.632·Vsat by construction, so `zero_offset` recovers it. On this
  board that gives 2.91 V, which matches the 2.93 V the calibration comments
  quote for it.

  Worth noting separately: the loop's own runaway guard (`railed_now`) tests a
  hard-coded 3.28 V. On a detector saturating near 2.9 V it cannot fire, so the
  band between ~2.9 V and 3.28 V is saturated as far as the hardware is
  concerned and healthy as far as the loop is concerned. That is not addressed
  here.
- **`dph` on screen never had the sawtooth removed.** The display computed the
  phase down its own path — voltage, centre, `ns_per_volt` — and skipped the
  correction the loop applies in `ltic_phase_error_ns()`. So algo 10 steered on a
  corrected phase while showing an uncorrected one, and the two differed by the
  whole receiver sawtooth: ~14 ns of 1-sigma scatter on an otherwise flat
  reading, measured on air. The display now subtracts it too.

  This matters most away from algo 10. Algorithms 3–9 never call the loop's phase
  path, so `dph` was their only view of true phase and it was the noisy one —
  which is precisely the case where the TIC is worth having, since it resolves a
  frequency offset to ~5e-11 in 100 s where the cycle counter needs 1000 s to
  reach 1e-10. The `qErr` field and the `qErr=` figure in the serial report are
  no longer gated on algo 10 either: what was subtracted has to be visible, or
  the number cannot be checked after the fact.
- **Each column has one right-hand alignment line (480×320).** The left column
  ends where "hPa" does on the BMP row, the right where "ns" does on the phase
  row — those being the widest, most stable strings in each. `Vct`, `% rH` and
  the INA current are now anchored to those lines instead of each stopping
  wherever its own text ran out, which left the column edges as three near-misses
  a few pixels apart. `PWM:` and `INA:` keep their labels at the column's left
  edge, so both rows had to become two fields rather than one string.

  The lines are measured with `textWidth()` on first use rather than written in
  as constants: every value in these rows is fixed-width, so each edge is a
  constant — but it is a constant of the font's glyph metrics, which are not the
  sort of thing to guess at. The paddings are derived from the measurement too,
  so the fields tile the row whatever it turns out to be.
- **The sensor rows are grouped by column rather than by sensor (480×320).**
  BMP and AHT now fill the left column and the electrical fields the right —
  the phase readout on top, the supply rails directly beneath it. `AHT` and
  `Vph`/`dph` swapped places to get there. Moving the phase field into the
  narrower right column cost it one space before `dph:`; its padding is sized to
  the widest string it can produce, not to the column, so a shrinking value
  cannot leave a tail behind.
- **`dPh:` is now `dph:`**, matching `Vph:` next to it. Changed on the TFT and
  in the serial report together — the two labels were meant to agree, and only
  changing one would have made that worse rather than better.
- **The survey-in notice moved out of the header and onto the status bar.** It
  used to pulse between the product name and the clock, where on the 480 panel
  it never appeared at all — a failure that survived every reading of the code
  and several confident wrong diagnoses. Rather than keep hunting it, the notice
  now appends itself to whatever the status bar already says:
  `DISCIPLINED  FIX OK SURVEY`, or `SV` at 320 where the full word would overrun
  the band.

  The bar is the better home regardless of the bug. It repaints its entire
  background before drawing, so the word cannot be clipped by a neighbour's
  padding the way a header slot could; it is the one place on screen the eye
  already goes for state; and sitting there it does not need to blink to be
  noticed, so the pulse is gone too.

  The condition is unchanged, because it was never the problem: the notice
  appears once the survey-in monitor times out with the receiver still
  surveying, and clears the moment Time Mode arrives.
- `g_time_offset` (int8, hours) is now `g_time_offset_min` (int16, minutes),
  with a single writer. `g_tz_auto` (bool) became `g_tz_mode` (manual /
  auto-EU / POSIX): every command sets the mode, so there is no half-state
  where one mechanism is configured and another is quietly overriding it.

### EEPROM
- The timezone block moved to `[234..284]`: mode, manual offset in minutes, and
  the POSIX rule as text.
- **Existing settings are migrated automatically — no factory reset.** A
  pre-v0.95 EEPROM has never been written above `[233]`, so the block reads
  back as erased flash; that is the marker, and the old `[9]`/`[142]` pair is
  carried forward (hours × 60 is exactly what it meant). The signature is
  unchanged.
- **Downgrading is one-way, though.** The legacy `[9]` and `[142]` bytes are
  still written, so v0.94 flashed onto a v0.95 board reads a sane whole-hour
  offset — but a `TZ` rule cannot be represented there and will be lost.

### Documentation
- Moved to [`doc/`](../doc/), with the English files gaining an `_EN` suffix so
  all three languages are named alike. The root `README.md` is now a short index
  — GitHub renders it on the project page, and it points into `doc/` from there.
- The flash-ring bring-up guides were orphans: nothing linked to them and they
  linked nowhere. They now carry the same language nav as everything else.
- Their flash-budget figure was five versions stale (~170 KB at v0.90). It reads
  216976 B (212 KB) at v0.95, ~44 KB of head-room below the ring at
  0x08040000 — measured, not estimated. That number is the whole point of the
  check, so it should not be left to rot. The guide now also warns that the
  IDE's percentage counts against the full 512 KB and reads far rosier than
  the truth: "41%" is really 83% of what firmware may use.

### Notes
- `tz_table.h` is generated. Re-run `tools/gen_tz_table.py` when tzdata is
  updated; a rule saved in EEPROM survives the regeneration.
- Africa/Casablanca and Africa/El_Aaiun degrade to their standard offset with a
  warning: their DST follows Ramadan, which the POSIX format cannot express at
  all. Every other zone in current tzdata resolves fully.

---

## [v0.94-rtos] — 2026-07-15

### Fixed
- **The 320×240 frequency field was still drawn with the GFX fonts.** v0.93
  moved the small panel back to the classic fonts, but the fix only reached the
  direct-draw path — and that path never runs, because the sprites are created
  on *both* panels, not just the 480×320 one. The sprite branch still had
  `GF_FREQ`/`GF_STATUS` hard-coded, so the reading (and `no signal`) kept
  rendering in FreeMono. It now goes through the same `TFT_FONT_*` macros as
  everything else.
- **The frequency twitched sideways on the 480×320 panel.** The reading was
  centred, so any change in string length moved every character: the averaging
  window changes the decimals, and 10000000.0000 → 9999999.9999 drops a
  character outright, with centring splitting that difference across both ends.
  The reading is now anchored by its right edge at x=464, chosen so the nominal
  `10000000.0000 Hz` (16 chars x 28 px fixed-width = 448 px) still lands dead
  centre, leaving 16 px of air on each side. "Hz" no longer moves; only the
  digits do. Busy messages stay centred — they use the proportional status
  font, where there are no columns to align.

### Changed
- **The frame is white on both panels.** Besides matching the big panel, this
  is what lets the 1-bit data sprite carry the frame itself: that sprite has
  exactly two colours (white and background), so the navy frame could not be
  drawn *into* it and had to be repainted on the panel after every push. White
  means frame and text now go out together in one atomic transfer, on both
  sizes. The header separator moved into the frequency sprite for the same
  reason (its 4-bit palette already holds white).
- **The splash no longer uses the GFX fonts.** It was the last GFX holdout on
  the small panel, which meant anyone upgrading from v0.92 had to add
  `LOAD_GFXFF` to `User_Setup.h` or watch the subtitle collapse to a lone "p" —
  an obscure failure for a cosmetic gain. The subtitle now uses classic font 4
  (which carries the full alphabet — fonts 6/8 are the letterless ones) and the
  credits use font 1 on both panels. **A 320×240 build now needs only
  `LOAD_GLCD`, `LOAD_FONT2` and `LOAD_FONT4`**; `LOAD_GFXFF` is required for
  the 480×320 build alone. The orphaned `GF_TITLE`/`GF_SUB`/`GF_CREDIT` macros
  and the dead 320 branch of the `GF_*` block are gone with it.
- Status bar labels sit 2 px lower on the 320×240 panel. They are all-caps, so
  the descender space at the bottom of the glyph box is empty and geometric
  centring reads high; the nudge centres what the eye actually sees. The
  480×320 panel is unchanged.
- Version bump to v0.94-rtos, including the per-file headers (which still read
  v0.92).

## [v0.93-rtos] — 2026-07-14

### Fixed
- **Countdowns ran slower than the clock.** The OCXO warmup and the
  calibrations timed their seconds with `vTaskDelay(1000)`, which sleeps *for*
  a second rather than *until* the next one — so the ADC reads, the serial
  prints and any preemption were all added on top, and the displayed figure
  drifted behind real time (worse the busier the system). Both now use
  `vTaskDelayUntil`, which absorbs the work time and keeps each step a true
  second. The calibration countdown also stopped at 1 instead of reaching 0.
- **A survey-in that outlives its monitor window is no longer invisible.** When
  the safety timeout fires, the firmware stops polling but the receiver keeps
  surveying ("continuing anyway" in the log) — and with the frequency band back
  to showing the frequency, nothing on screen said so. A slow-pulsing `SURVEY`
  now sits in the header between the version and the clock, and clears itself
  when the receiver reports Time Mode (`HDOP: TIME`), which is the survey's real
  completion signal.
- **`qErr` left stale characters on the ILI9488 panel** (shown as
  `qErr: -1.6 nsss`). The field's text padding was 55 authored units (~82 px)
  while the widest value, `qErr: -21.3ns`, needs ~104 px in FreeSans 9pt —
  TFT_eSPI only repaints the background under the padding, so the tail of the
  previous, longer string survived. Padding widened to 75 authored units
  (~112 px), which covers the text and still clears the right-anchored `Vdd`
  field.
- **Vctl / Vcc / Vdd read 0.000 V for the whole OCXO warmup.** Those ADC
  averages are sampled in the control task's main loop, but `do_warmup()` runs
  *before* that loop is entered and only slept — so nothing ever filled them.
  The warmup countdown now samples the same three channels every second, the
  way `wait_secs_pwm()` already does during calibration.
- **Frequency readout sat right of centre and jumped sideways.** The value was
  formatted with `dtostrf(..., 14, ...)`, left-padding it to 14 characters;
  `MC_DATUM` then centred the string *including* those invisible spaces, so the
  visible digits sat ~40 px right of centre — and because the pad count varies
  with the averaging window (1–4 spaces), the readout shifted whenever
  precision changed. The width is dropped: `GF_FREQ` is FreeMonoBold, which
  already holds the digits in fixed columns, so the padding bought nothing. The
  480-panel trailing-space workaround is gone with it.

### Changed
- **The 320×240 panels go back to the classic fonts for the operating screen.**
  v0.92 moved every panel to the GFX free fonts; on 480×320 that was a clear
  win, but at 320×240 the proportional faces are too wide for a layout authored
  around the numeric ones — values ran past their columns into the neighbour and
  the centre divider cut through the overflow. There was no smaller face to fall
  back on either (FreeSans starts at 9 pt; below it is only the unreadable
  3×5 TomThumb). The small panel now uses font 2 for the header and grid, font 4
  for the status bar, and font 1 ×3 (fixed-width) for the frequency, while the
  splash keeps GFX on both panels. The `TFT_FONT_*` macros pick this at compile
  time — still one layout, not two. The centre column divider is now 480-only
  (no room for it at 320), and the frame reverts to navy on the small panel.
- **The live display regions are double-buffered as sprites.** The header, the
  frequency band and the data area are each rendered into a `TFT_eSprite` in RAM
  and pushed to the panel in one continuous SPI transfer, instead of erasing the
  panel with `setTextPadding` and then drawing on top of it. That erase-then-draw
  was visible as a once-a-second flicker, especially on the 480×320 panel where
  it wipes 2.4x the pixels. Palettes keep it cheap (4-bit header/freq, 1-bit
  data; ~25 KB total on the big panel). If `createSprite()` fails on a
  fragmented heap, each band falls back to direct drawing — the old flicker
  returns but nothing breaks; the boot log reports which path is live.
- **Status messages now spell themselves out, and name which calibration is
  running.** `WARMUP 285s` → `OCXO warmup 285s`, `SVIN 120s 5m` →
  `Survey 120s +/-5m`, and the ambiguous `CAL 245s` becomes `Calibrate`, `Tune`
  or `LTIC cal` — C, CT and LC take very different times, so a bare countdown
  told the operator little. Both panels. Note the two figures differ in kind:
  warmup and the calibrations count down, while survey-in counts up (the
  receiver reports elapsed time, and completion also depends on accuracy, so a
  "remaining" figure would be a guess).
- **`SPI_FREQUENCY 40000000` is now the documented setting** (was 27 MHz in the
  README while `gpsdo_config.h` already said 40). The F411's SPI1 tops out at
  50 MHz, so 40 leaves headroom; it matters most on the 480×320 panel, where a
  sprite push is a single transfer whose duration scales with the clock. Drop
  to 27 MHz if long jumper leads misbehave.
- **Splash credit lines get more leading on the 480×320 panel.** The authored
  12-unit gap scales to only ~16 px there, and the credits are FreeSans 9pt
  (~13 px tall), so the two lines merged visually. The large panel now uses a
  16-unit gap (~21 px, ~1.6x leading); the 320×240 panel keeps 12, which suits
  its 6x8 font1.
- `dPh:` and `qErr:` on the ILI9488 drop the space before their `ns` unit.
- Version bump to v0.93-rtos.

## [v0.92-rtos] — 2026-07-12


### Changed
- **Splash simplified and operating-screen proportions refined.** The large
  green "GPSDO" title was removed from the boot splash; the "GPS Disciplined
  OCXO" subtitle now sits raised at the top, matching the original 320×240
  layout. On the operating screen the header text dropped to the data-font size,
  the bottom status bar was halved in height with a smaller status font, and the
  reclaimed space went into wider line spacing between the telemetry rows (row
  pitch 17→20 authored) so the grid breathes. Data font stays at FreeSans 9pt.
- **All TFT text migrated to Adafruit GFX free fonts (GFXFF).** The header, big
  frequency readout, data grid, status bar and splash title/subtitle now render
  in FreeSans / FreeMono instead of the classic numeric GLCD fonts. This fixes a
  long-standing bug where lettered strings drawn in the numeric fonts (6/8,
  which contain only `0-9 . : - a p m`) collapsed to a single glyph — most
  visibly the splash subtitle "GPS Disciplined OCXO" rendering as a lone "p", and
  the status-bar label appearing blank on a coloured bar. A per-role, per-panel
  font layer (`GF_DATA` / `GF_HEAD` / `GF_STATUS` / `GF_TITLE` / `GF_SUB` /
  `GF_FREQ` in `gpsdo_config.h`) picks FreeSans 9/12 pt, FreeSansBold 12/18/24 pt
  and FreeMonoBold 18/24 pt automatically for the 320×240 and 480×320 panels, so
  the same layout code serves both. The big frequency uses FreeMonoBold so its
  digits stay fixed-width and don't shuffle as the value changes.
- **Requires `#define LOAD_GFXFF` in `User_Setup.h`** (see README). The old
  `LOAD_FONT2/4/6/8` lines are no longer needed; `LOAD_GLCD` is retained only
  for the two fine-print splash credit lines.
- **Operating-screen layout re-geometried for 480×320.** Band boundaries (freq,
  grid, sensors, status) recomputed so the taller free-font rows never cross a
  separator on either panel, the two data columns fill the full width with a
  faint centre divider, and the status bar fills the whole band to the screen
  bottom (no dead colour strip below the label). Grid values are right-datum
  anchored so changing widths stay pinned instead of drifting. Verified on the
  ILI9486 480×320 panel.

### Fixed
- **Stale "not yet implemented / phase A" wording removed from CLI and
  telemetry.** `LA` with a bad value said "0..9 (10=LTIC, not yet available)",
  `LL` printed "(loop not yet implemented — phase A)", and the help/comments
  still described algo 10 as an unimplemented preview. Algorithm 10 has
  disciplined the loop for many releases; all these now describe the live
  3-stage ACQ→DPLL→LOCK phase loop. (`Vdd:` on the TFT also gained a space
  before its value to match the other labels.)
- **LED spinner animations (warmup / survey-in / calibration) ran ~5x too slow
  and looked choppy.** The display task wakes on the 1 Hz PPS notification, but
  the spinners step their frame every 200 ms — so at the 1100 ms wake cadence
  they advanced only once a second. The task now wakes ~every 150 ms while an
  animation is active (and keeps the slow 1100 ms cadence otherwise, since the
  clock only changes once a second). To stop the faster wake from re-pushing
  identical segments over the software-bit-banged TM1637 (~5–8 ms per write), a
  small write cache (`tm_set`) skips the transfer when the pattern is unchanged.
  This is a scheduling/caching fix — no DMA involved; DMA remains a separate
  future step for the TFT SPI path.
- **A raised damping floor did not take effect after reflash — lock oscillated
  LOCK↔DPLL.** The damping multiplier is persisted in the flash ring (live
  data) and restored on boot. Flash written by a build with the old 0.30 floor
  therefore reloaded damp = 0.30 even after the floor was raised to 0.45, and
  since damp only adapts on limit-cycle crossings it stayed stuck there — the
  loop ran at 30 % correction authority, the phase climbed past the lock
  threshold, and the loop hunted LOCK↔DPLL every ~30 s (seen on air). The
  restored damp is now clamped into the current legal band on load (both flash
  ring and EEPROM), so a reflash takes effect immediately. The damp bounds
  moved to the shared header so storage and the learner agree.
- **TFT now shows qErr, and phase gets a `dPh:` label.** On algo 10 with SAW
  active, the right sensor row leads with the receiver sawtooth `qErr:…ns` and
  `Vdd:` shortened to 1 decimal. The two are drawn separately — qErr
  left-aligned, Vdd anchored to the right screen edge — so Vdd no longer drifts
  sideways as qErr changes width. With SAW off, Vdd alone is shown at full
  precision, still right-anchored. The LTIC phase on the left is labelled
  `dPh:±…ns` (no space after `Vph:`) for a clearer, consistent readout; the
  serial report uses the same `dPh:` label after `Vphase:` so the two match.
  Both
  qErr and dPh use a fixed-width signed field (sign always shown, magnitude
  right-justified), so the digits and units stay put instead of jumping
  sideways when the value crosses zero or changes digit count.
### Fixed
- **LOCK could lose lock on a drifting OCXO — now carries a gentle frequency
  term.** In the normal LOCK branch the frequency path was disabled
  (freq_term = 0), so the only defence against a real OCXO drift was the slow
  drift feed-forward. On warm hardware that lagged badly and the phase walked
  out of the lock window (11 → −425 ns in 51 s, then LOCK→DPLL→ACQ). LOCK now
  applies a light 0.1×Kp frequency term — enough to cancel the live drift each
  update, gentle enough not to inject TIM2 noise into a quiet lock. Combines
  with the faster drift feed-forward (below). Root-cause analysis: GML-5.2.
- **ACQ limit cycle (±150 LSB PWM hunting) removed.** Algorithm 10 took its
  frequency error from avg10 (0.1 Hz quantisation); times Kp (~1550 LSB/Hz)
  that produced ±150 LSB PWM jumps on a ~10 s cycle, which kept the phase from
  settling under the lock threshold and slowed acquisition. It now uses avg100
  (0.01 Hz), 10× finer, and acquisition settles cleanly. Analysis: GML-5.2.
- **Drift feed-forward now bootstraps after lock.** Its first learning window
  was a slow 30 s, so a fast post-lock drift escaped before it moved. It now
  runs three fast 8 s windows at a larger step right after lock (absorbing the
  drift in ~10–20 s) then relaxes to the quiet 30 s regime.
- **Damping floor raised 0.30 → 0.45.** The learner could damp so hard the
  loop had only 30 % correction authority and couldn't follow drift; 0.45 still
  damps a limit cycle but keeps enough authority to track.

### Changed
- **LC calibration anchor is now universal — 0.632·Vsat, derived per board.**
  The detector is an RC charge ramp V(φ) = Vsat·(1 − e^(−φ/τ)); the point
  φ = τ, where V = 0.632·Vsat, is the same fractional height on every
  exponential detector regardless of Vsat. LC now recovers Vsat with a 1-D fit
  (linearise −ln(1 − V/Vsat) vs t, pick the lowest-residual Vsat) and anchors
  there. The previous hard-coded 1.85 V only worked because Marek's and Dan
  Wiering's detectors both happen to have Vsat ≈ 2.93 V; a detector with a
  different Vsat would have missed the band. LC is now self-adapting per board
  with no configuration, and `LTIC_ZERO_ANCHOR_V` is retired. Verified on
  logged runs: Vsat recovered to ~0.3 %, anchors agree ~0.8 % run to run.
  Physics and derivation: GML-5.2.
- **Splash subtitle** now reads `GPS Disciplined OCXO` (space, not hyphen).

### Credits
- Loop-anomaly diagnosis and the universal-anchor derivation in this release
  were contributed by **GML-5.2**, cross-checked here against the logged data
  and hardware behaviour. Field logs and testing: **danieljw** (Rb reference)
  and **lucido**.

---

## [v0.91-rtos] — 2026-07-11

### Added
- **LC calibration — anchored operating point + local-slope ns/V (Option D).**
  The ramp phase detector is exponential (1k/1n, τ≈1 µs), so ns/V is not
  constant along the ramp and a whole-transit average (range/span) drifted
  ~15–20 % between runs depending on where the picDIV arm parked the phase.
  ns/V is now taken from the LOCAL slope dV/dt in a window around a fixed
  operating point (LTIC_ZERO_ANCHOR_V = 1.85 V). zero_offset is anchored to
  that point — the repeatable middle of the ramp, clear of the detector dead
  zones Dan Wiering measured (the Schottky drop + pull-down below ~0.05 V, and
  the ADC rail/wraparound near 3.3 V). If a sweep never crosses the anchor band
  the code falls back to the previous range/span average and says so.

  Bench findings across several 1 s-resolved LC runs:
  * The anchor is exact — back-to-back runs land zero_offset on 1.8500 V every
    time.
  * Run-to-run ns/V spread fell from ~15–20 % (old range/span average) to a few
    percent. With both runs swept at the SAME rate it is ~2.8 %; the residual is
    dominated by the sweep-rate quantisation, not the slope fit — avg100 resolves
    the rate to 1 ns/s, so a "−5" vs "−6" label carries ±0.5 ns/s and the two
    ns/V confidence bands overlap. This does not hurt LOCK: the loop uses the
    exact ns/V it measured, at the voltage where it actually works.
  * The fit window was widened to ±0.20 V (LTIC_ANCHOR_WIN_V): more points in
    the band (~70 vs ~35) average down the ADC noise, taking the same-rate
    spread from ~5.9 % at ±0.10 V to ~2.8 %.
- **LC per-second diagnostic log.** During the sampling sweep LC now prints one
  `t=/V=/n=` line per second, making the whole ramp visible in a capture (used
  to derive Option D).

### Fixed
- **Serial report printed twice per second in RD/RH when GPS had a fix.**
  vDisplayTask is notified by two ~1 Hz sources — the frequency relay (per PPS)
  and the GPS parser (per time sentence) — so with a fix it woke twice a second
  and emitted two report lines. The serial line is now gated on a change of the
  PPS counter, so exactly one line prints per second; the on-screen display
  still refreshes on every notification. Reported by Dan Wiering.
- **Credit spelling.** "Wieringa" → "Wiering" in the acknowledgements.
- **TFT `Vph` phase readout was dead code, and wrong if enabled.** It gated on
  the compile-time `LTIC_NS_PER_VOLT` (default 0, so the ns figure never showed
  once the detector was calibrated) and, had the constant been set, computed
  `V × ns_per_volt` from 0 V instead of relative to `zero_offset`. It now uses
  the MEASURED `g_ltic.ns_per_volt` and `zero_offset` from LC, showing a signed
  phase `(V − zero_offset) × ns/V` that matches the loop's own error, or just
  the volts when uncalibrated.
- **CT rejected narrow-span (better) OCXOs.** The plant-gain sanity check
  floored K at 0.1 mHz/LSB, but a narrow EFC span is desirable — smaller Hz/LSB
  means finer resolution and is one route to E-12. Dan Wiering's build measures
  0.048 mHz/LSB (~1.05 V EFC span) and was wrongly rejected. Floor lowered to
  0.02 mHz/LSB; only genuine noise/no-GPS runs are now refused.
- **ILI9488 (480×320) layout fixes — from user photos, not yet verified on a
  panel.** Early adopters Dan Wiering and lucido sent photos of their 480×320
  builds. Several issues were addressed from those images without an ILI9488 on
  hand: (1) the body font was over-scaled — TFT_F mapped font 2→4 (growing
  1.63× while rows scale only 1.33×), so lines overran vertically and the
  status bar was pushed off-screen; the body font is now kept at 2. (2) The
  BMP sensor row was trimmed (temperature and pressure to 1 decimal) so the
  wider scaled glyphs don't overrun the AHT column. (3) The `User_Setup.h`
  font instructions were missing `LOAD_FONT8`, which the frequency readout
  needs — without it that line stays blank. These are best-effort fixes from
  photographs; a final geometry pass will follow once an ILI9488 panel is in
  hand. Small 320×240 panels are unaffected (TFT_F is identity there).
- **LOCK could lose lock on a drifting OCXO (LOCK→DPLL→ACQ bounce).** With a
  real frequency drift (measured ~8.5 ns/s on warm hardware), the phase walked
  out of the lock window — 11 → −425 ns in 51 s — while the correction was too
  weak to follow: the damping learner had floored at 0.30 (correction at 30 %
  authority) and the drift feed-forward was still gathering its first 30 s
  window, so it never moved before lock was lost. Two changes: the damping
  floor was raised 0.30 → 0.45 (keeps enough authority to track drift while
  still damping a limit cycle), and the feed-forward now BOOTSTRAPS after lock
  — three fast 8 s windows at a larger step absorb the drift within ~10–20 s,
  then it relaxes to the slow, quiet 30 s regime. Simulated on the logged
  drift, phase now holds near −125 ns instead of running away. Stable, low-drift
  setups (e.g. a Rb-referenced build) are unaffected — the bootstrap converges
  immediately and the higher floor is still net damping.
- **Phase in ns added to the serial report**, after `Vphase:`, once LC has
  calibrated the detector — `(V − zero_offset) × ns/V`, the same convention as
  the loop and the TFT row.
- **LC anchor is now the measured ramp midpoint, not a fixed 1.85 V.** The
  local-slope anchor was hard-coded to Marek's detector band; a build whose
  ramp sweeps a different range (Dan's runs lower, ~1.3 V) missed the anchor
  window entirely and fell back to the coarse range/span average ("weak"
  result). The anchor is now `vlow + span/2` from the actual sweep, with the
  config `LTIC_ZERO_ANCHOR_V` used only when it genuinely falls inside the
  swept band. LC is now self-adapting per board.
- **TFT pressure could overrun into the AHT column.** BMP pressure was printed
  with 2 decimals (`1013.25hPa`), which at 4-digit pressures ran past the left
  column. Reduced to 1 decimal (`1013.2hPa`), matching the serial report.
- **Warm-boot LOCK bounce (wasted ~1 min of the ~8 min boot-to-lock).** A
  persisted LOCK/DPLL was resumed as long as the phase read was valid (on the
  ramp), even when it sat far from zero_offset — e.g. Vphase ≈2.09 V against a
  1.85 V anchor (~260 ns off). LOCK then engaged, DPLL judged the phase too far
  a minute later and dropped all the way to ACQ, so the full pull-in ran anyway
  after a needless detour. The boot guard now demotes a persisted LOCK/DPLL to
  ACQ unless the phase is valid AND within the ACQ window of zero_offset. Cold
  boot is unaffected (state defaults to ACQ); a genuinely centred warm boot
  still resumes LOCK immediately.

---

## [v0.90-rtos]

### Added
- **Wear-levelled flash ring buffer for "live" data.** Learned drift/damping,
  LC calibration and last PWM are now auto-saved to a dedicated flash sector
  (sector 6, 0x08040000, 128 KB) as a ring of 32-byte slots. Each save
  programs the next empty slot; the sector is erased only when the ring wraps
  (once per 4095 saves), so at 100 saves/day the flash lasts on the order of a
  thousand years. Each slot carries a CRC and a sequence number; a half-written
  slot (power loss) fails CRC and the previous good slot is used. A signature +
  format-version header makes the firmware robust to full-chip-erase,
  sector-only programming, first boot and leftover flash junk alike (a foreign
  or blank sector is detected and re-initialised).
- **Auto-save with hysteresis.** Live data is written only when it has settled
  on a new level: drift changed by > 8 LSB or damping by > 0.03, AND at least
  20 min since the last save. A successful `LC` calibration saves immediately.
- **`FR 0|1` command** (saved with `ES`, default on) toggles the ring buffer at
  runtime — no compile flag, so no build-cache surprises. `FR 0` stops all
  flash-ring activity.
- **`EW` command** shows flash-wear diagnostics: erase cycles and slots used.
- **Sawtooth (qErr) correction for LTIC (`SAW 0|1`).** u-blox timing receivers
  generate 1PPS by dividing an internal clock, so each pulse lands up to one
  clock period off true GPS time — a per-pulse quantization error the receiver
  reports as `qErr` in UBX-TIM-TP. A passive sniffer parses that message
  (qErr is a signed 32-bit picosecond field at the same offset on LEA-6T,
  LEA/NEO-M8T and ZED-F9T, so one parser serves all) and the TIC phase path
  subtracts it, removing the receiver's granularity sawtooth and leaving the
  OCXO's own error. On a LEA-6T (21 ns granularity) this is the dominant
  short-term phase term. TIM-TP is enabled automatically at GPS init; `SAW`
  toggles the correction (saved with `ES`, default off) and shows live qErr.

### Changed
- **`ES` no longer overwrites learned/calibration values when the ring is on.**
  With `FR 1`, calibration (ns_per_volt, zero_offset, range_ns, centre_v) and
  learned drift/damp are owned solely by the ring; `ES` writes only genuine
  settings (PID gains, thresholds, flags). With `FR 0`, `ES` still saves those
  live values to EEPROM as a fallback, and `eeprom_recall()` seeds them at boot
  so migrating an older EEPROM keeps its calibration.

### Fixed
- **`LC` no longer fights the discipline loop.** Running `LC` while algorithm
  10 was actively disciplining let the loop move PWM at the same time as the
  calibration sweep, so the two corrupted each other — the measured sweep rate
  came out at ±1 ns/s and the range as absurd values (1502 / 3518 ns), which
  the physics gate correctly rejected. The control loop is now suppressed
  whenever a calibration is active (`g_calib_active`), so `LC` can be run at
  any time, including under `LA 10`.
- **Calibration-safe PWM paths.** The same guard now also covers the algorithm
  9 thermal-holdover steering and the manual PWM commands (`up1`/`up10`/`dp1`/
  `dp10`/`SP`), which are refused with a clear message while `LC`/`CT` runs, so
  no path can perturb a sweep in progress.
- **`LC` no wrap is no longer flagged as a failure.** A detector that does not
  wrap within the sweep now passes with a good slope/centre/span and is
  auto-saved; only a genuinely weak result (tiny span or off-band centre) is
  called out, with the specific reason. Messages no longer tell the user to run
  `ES` after `LC` — a passing `LC` auto-saves to the flash ring (this is live
  data). `CT` still prompts for `ES`, since it tunes PID settings.

### Credits
- Attribution refined: André Balsa credited as author of v0.06c, the
  inspiration for the RTOS port. Repository link corrected.

---

## [v0.89-rtos]

### Added
- **Self-learning loop aid (`LRN`), shared by algorithm 7 and LTIC.** Two slow,
  passive learners — informed by Dan Wiering's overnight Rb-referenced traces
  (a ~9000 s ±80 ns phase sawtooth, an ADEV bump at the loop time constant, and
  8E-12/day drift): (1) a **drift feed-forward** that estimates the OCXO's mean
  phase slope over 30 s windows and adds a PWM term to cancel it, so the loop
  stops chasing a moving target and the phase goes flat; (2) a **damping
  adaption** that watches phase-error zero-crossings and eases the correction
  gain down on overshoot, up when sluggish — flattening the ADEV bump at the
  loop time constant. Both run ONLY when locked, update at most every 30 s, and
  are hard-clamped (feed-forward ±400 LSB, damping 0.5–1.5) so a bad estimate
  cannot destabilise the loop; neither injects any excitation. `LRN 1|0` enables
  /disables (default on), `LRN R` resets to theory, `LRN` alone prints live
  state; learned values are saved by `ES` (EEPROM 222–230) and recalled at
  boot. The serial report shows a live `Learn:` line (drift, slope, damping,
  observed limit-cycle period/amplitude).
- **Learning now covers every disciplining algorithm (3–10), not just 7/8.**
  A single `lrn_apply()` wrapper feeds each loop's own phase accumulator and
  frequency error to the learners; the NN (algo 9), having no explicit phase
  accumulator, uses damping only. `LRN` state is shared across algorithms.

### UI / Display
- **Colour TFT reworked for clarity and a little life.** Consistent single-space
  label formatting throughout (`Alt: 144m`, `PWM:...`, `Uptime: ...`); the value
  fields align optically in the proportional font. A navy frame (matching the
  header) now boxes the data area, with the three separators joined by side
  rails. The frequency turns green on lock. `DATE:` label added.
- **Boot splash refined**: title at the frequency's height, two oscillator waves
  that fade in out of phase, drift into agreement and merge into one green wave
  with a swelling-then-fading halo, followed by a scrolling hardware-detection
  list (fixed-height window, credits stay put).
- **`SPL 0|1` command** (saved with `ES`, default 1) toggles the boot animation.
  `SPL 0` shows just the title and credits for two seconds — for the
  art-indifferent.

---

## [v0.88-rtos]

### Fixed
- **TFT frequency field no longer keeps digit slivers after CAL/WARMUP/SVIN
  messages.** The busy messages and the big frequency use different text
  heights, so text padding wiped only the current font's band; the whole
  field is now cleared on every busy↔normal transition.

### Removed
- **SPI→T6963C bridge support removed** (an experiment): `T6963C_Bridge.h`,
  its display task section, config block and cross-references are gone.

### Docs
- READMEs (EN/PL/ES) updated with the LTIC v0.5x–v0.88 feature set (LC
  auto-calibration, autotuned gains, ADC median path, runaway guard, WU,
  LED animations, trustworthy lock colour) and a new section on colour TFT
  support: any TFT_eSPI panel at 320×240 or 480×320 with setup steps.

---

## [v0.87-rtos]

### Fixed
- **Zero dead time before sampling — the prep was eating the whole band.**
  The ADC keeps up fine (1 sample/s ≈ 8 mV/step at 9 ns/s); what failed was
  the ~60 s of settling and d1/d2 reads between commanding the ramp and the
  first sample. A fixed offset lands on top of whatever df the saved PWM
  already has (measured +9 ns/s on air), so the phase flew 0.061→2.62 V
  through the entire band BEFORE sampling began and the fit saw only
  saturation. LC now re-arms picDIV (deterministic bottom start), commands
  the offset and starts sampling within ~3 s; the exact rate is read AFTER
  the pass from clean avg100. If saturation still arrives before 10 fit
  points, the offset is halved, picDIV re-armed and the pass retried once.
  The pre-sweep d1/d2 measurement and the adaptive reduce/increase machinery
  are removed — the physics gate and the post-pass precise rate make them
  redundant.

---

## [v0.86-rtos]

### Changed
- **LC redesigned as a single bottom-to-top pass — no direction probing, no
  flipping, no wraps needed.** Field logs proved the picDIV arm parks the
  phase DETERMINISTICALLY ~60 ns above the sync point (Vphase ≈0.061 V after
  every re-arm), that the negative side below that point is DEAD (edge order
  inverts, the pulse vanishes — avg100 showed a real −3 ns/s drift while the
  voltage stood still), and that the positive side runs the whole band up
  into soft saturation. LC now exploits this: after arming it COMMANDS a
  positive ~+4 ns/s sweep (offset from the measured K), samples the entire
  band in one pass, and treats sustained upper saturation as the natural END
  of the measurement rather than a fault. The precise avg100 read-back
  (v0.85) scales ns/V exactly. The in-sweep direction flip and its restart
  machinery are removed.

---

## [v0.85-rtos]

### Fixed
- **The direction flip now COMMANDS a sweep rate instead of trusting a blind
  read — and the phase no longer parks at the band's edge.** On air the flip
  iteration stopped at a nominal "−1 ns/s" that was really ≈0: avg10
  quantises at 0.1 Hz (d1=0.1000, d2=0.0000 in the log), so below 0.1 Hz the
  read is noise. With df≈0 the phase sat wherever the picDIV re-arm dropped
  it (Vphase 0.061 V — the band's lower edge, where too-narrow pulses barely
  charge the RC), the sweep covered 5 mV, and the physics gate had to abort.
  Now, when the sign flips between iterations, LC interpolates the 10 MHz
  point P0 from the last two offsets and sets the ramp to P0 − 0.06 Hz·(LSB/Hz)
  — a COMMANDED −6 ns/s derived from the measured K, independent of the
  quantised read. At the end of the sweep (PWM constant throughout, so avg100
  is clean at 0.01 Hz resolution) the true rate is read back and replaces the
  commanded one before ns/V is computed, so the fit scale is exact.

---

## [v0.84-rtos]

### Fixed
- **The in-sweep direction flip now re-measures the rate and FORCES the sign
  to change.** v0.83's defences all fired correctly on air (soft-saturation →
  flip → clean restart → bad result rejected), but the flip itself had two
  defects: (1) the fit's ns/V divides by phase_rate, and the pre-flip rate was
  reused after the flip — a guaranteed wrong scale (ns/V=9.09e6 rejected by
  the guard); (2) mirroring the offset around saved_pwm does not change the
  drift sign when saved_pwm sits far from the true 10 MHz point (+70 gave
  +0.100 Hz, −70 still +0.054 Hz — the railing side, just slower). After the
  flip LC now re-measures df, and if the sign has not flipped it pushes the
  offset further by −2·df·(LSB/Hz) from the measured K and re-checks (≤3
  iterations); the glitch-rejection window is rescaled to the new rate.
  Simulated on the exact on-air numbers: one push lands at −0.054 Hz
  (−5.4 ns/s), the wrapping side at an ideal sweep speed.

---

## [v0.83-rtos]

### Fixed
- **`LC` can no longer be fooled by soft RC saturation.** A run with a fast
  initial offset (10 ns/s) let the phase drift into the RC's soft-saturation
  region (2.9-3.27 V — below the 3.28 V rail threshold, so "live"): the linear
  fit ingested flat saturation points (ns/V ×74 too big), the later drop out
  of saturation (a 2.57 V "jump") was accepted as a wrap, and the result
  (range=209204 ns, zero_offset=1.34 V — outside the detector band) even
  PASSED the volt-vs-volt self-consistency. Three band-relative gates close
  this class: (1) **physics gate** — the committed range cannot exceed what
  the sweep could physically cover (~rate × window × 1.5), else params
  unchanged; (2) **wrap-jump endpoints** must lie within the clean fitted
  band ±50%, so a drop out of saturation is not a wrap; (3) **soft-saturation
  skip** — once the fit has shape, samples far outside its band are treated
  like railed ones (skipped; they feed the in-sweep direction-flip logic).
  All three scale from the run's own observations — full-swing 3.3 V
  detectors are unaffected.

### Added
- **Survey-in animation on the LED displays.** An upper-'o' spinner (segments
  A→B→G→F chasing around the digit's top loop), phase-shifted per digit into a
  wave — visually distinct from the warmup's lower-'o' wave.

---

## [v0.82-rtos]

### Fixed
- **ACQ parked the phase half a range away from the handover point — permanent
  ACQ (1401 cycles on air with Δf≈0).** The ACQ pull target was computed as
  `zero_offset + span/2`, a relic from before v0.66 when zero_offset was the
  band's floor; since then zero_offset IS the band middle, so the loop held the
  phase at its own "centre" while the ACQ→DPLL threshold (measured against
  zero_offset) could never be satisfied. One point of truth now: ACQ pulls
  exactly to zero_offset. A fresh `LC` also clears any old `LCV` override
  (which could silently re-introduce the same stalemate from EEPROM).

### Added
- **Warmup animation on the LED displays.** During OCXO warmup every digit of
  the TM1637/HT16K33 shows the lowercase-'o' chasing-segment spinner,
  phase-shifted per digit so the pattern travels across the display like a
  wave (survey-in keeps the dashes).

### Note
- After upgrading, re-run `LC` once: the previous calibration was taken
  through the old 10-second-averaged ADC path and its zero_offset/range are
  blurred; the rebuilt burst-median path (v0.79) gives a sharper measurement.

---

## [v0.81-rtos]

### Fixed
- **Build fix:** `p_eff` was used by the DPLL/LOCK integrator before its
  declaration (v0.79/v0.80 did not compile). The deadband/soft-knee block is
  now computed first, so both the integrator and the phase term see it.
- **Calibration countdown shows the REAL total time.** The counter used to
  restart for every internal wait segment (30 s, 20 s…), so the display never
  reflected the whole procedure. `LC`/`CT` now preload a realistic total and
  adaptive phases (ramp increase, rail-backoff, direction flip, sweep restart)
  top it up as they occur; every exit path clears it.
- **OCXO warmup restored and made a saved setting.** Warmup was silently
  skipped whenever the EEPROM was valid — so it "disappeared" once a
  configuration was saved, and a cold-started OCXO was disciplined while still
  drifting thermally. Warmup now runs by default on every boot and can be
  disabled with the new `WU 0` command (`WU 1` re-enables; state saved by `ES`
  in EEPROM byte 221, fresh-flash default: on).

### Added
- **LED "CAL" + spinner during every calibration.** TM1637 and HT16K33 show
  CAL on the first three digits and, on the fourth, a chasing-segment
  animation (G→C→D→E) tracing a lowercase 'o' — a clear "working" cue.

---

## [v0.80-rtos]

### Fixed
- **The green frequency colour now means a trustworthy, CURRENT lock.** After
  LTIC dropped from LOCK to ACQ, the display stayed green because the 1000-s
  average still read ~10 MHz — an echo of the past, not the present. Rules now:
  for algorithm 10 green comes ONLY from the loop's live LOCK state (no
  average fallback); for algorithms 0-9 the long-window criterion remains but
  must be backed by the fast 10-s average still within ±50 mHz of 10 MHz, so a
  loss of discipline kills the green in ~10 s instead of minutes.

---

## [v0.79-rtos]

### Fixed
- **LTIC ADC path rebuilt — the 10-second moving average was poisoning the
  loop.** The old path took ONE raw ADC read per PPS through a 10-sample
  (=10 s) moving average: ~5 s group delay (the loop corrected on stale data)
  and, worse, pre- and post-wrap voltages blended into phantom mid-levels — the
  loop saw a smooth ~30 ns/s drift that did not physically exist and kicked the
  real phase (LOCK steps up to 152 LSB, LOCK↔DPLL bouncing). Now each PPS slot
  takes a 16-read burst (~1 ms) and its MEDIAN — no cross-second memory, no
  lag, no wrap blending, single-read glitches fall out — plus an outlier gate:
  a jump >25% of the calibrated span must repeat in the next read to be
  believed (real wraps persist; glitches don't). Note: reading the ADC more
  often would add nothing — the detector charges the capacitor once per PPS,
  so phase information is inherently 1 Hz; the burst maximises the quality of
  that one sample.
- **LOCK is gentle by design: deadband + soft knee + step cap.** Inside a
  deadband (range/40, ≥6 ns — the ADC noise floor) the phase error counts as
  zero and the integrator holds; outside, the error ramps from zero (soft
  knee); the final LOCK step is hard-capped at ≈4 mHz (from measured K). Small
  offsets now get proportionally small pushes instead of full-gain kicks.

---

## [v0.78-rtos]

### Fixed
- **First confirmed on-air LOCK with the LTIC three-stage loop.** Two follow-ups:
  (1) the TFT frequency readout now turns green on LTIC LOCK — it only
  recognised the legacy "hit" trend, so the colour would have waited for the
  1000/10000-s averages to reach mHz; (2) the EEPROM recall guard rejected
  algorithm 10 (`algo > 9 → 0`), so a saved LTIC configuration silently
  reverted to algorithm 0 on reboot — now `> 10`. With this, `ES` fully
  preserves the LTIC setup: algorithm 10, the LC calibration and polarity are
  stored, and the loop gains are re-derived by autotune from the stored
  measurements on every entry, so a reboot comes back locked-capable with no
  manual steps.

---

## [v0.77-rtos]

### Fixed
- **State transitions no longer bounce on the stepped detector read.** With the
  frequency finally held (−0.02 Hz), the loop still ping-ponged ACQ↔DPLL: the
  ADC updates the phase voltage in steps, and each step produced a phantom
  50-100 ns/s "slope" that tripped the V-derived slope gates (entry to DPLL
  blocked for 183 cycles; DPLL demoted after 6). All frequency-quality gates in
  the transitions now use TIM2's Δf (immune to the stepping) — ACQ→DPLL at
  |Δf|≤0.05 Hz, DPLL→LOCK at ≤0.03 Hz, demotions at Δf>0.30 / 0.10 Hz — while
  the voltage is used only for phase POSITION. DPLL demotion also gained the
  same 3-strike persistence LOCK already had, so a single stepped read cannot
  demote. Simulated with stepped reads: no false demotions, clean promotion to
  LOCK.

---

## [v0.76-rtos]

### Added
- **Full LTIC auto-tuning — no hand-set coefficients.** `ltic_autotune()`
  derives EVERY loop gain from the two measured hardware constants: K (Hz/LSB
  from CT) and ns/V + range (from LC). Freq loop cancels ~50% of Δf per step;
  phase loop pulls with τ≈20 s; LOCK is 4× gentler; the ACQ threshold becomes a
  quarter of the measured detector range. Runs automatically after each
  successful LC and on entry to algorithm 10, and prints the derived values.

### Fixed
- **ACQ now drives the TIM2 frequency error, not the voltage-derived drift.**
  The stepped detector read goes flat at a band edge (on air: phase parked at
  0.336 V while a real −0.3 Hz offset persisted, with ACQ↔DPLL bouncing) — a
  V-derived slope is blind there; TIM2 is not.
- **Board polarity no longer inverts the frequency path.** K is positive on
  every board (+PWM → +f), so frequency terms take no `pol`; only the phase
  (Vphase) path does. Routing e_freq through pol=−1 had been inverting a
  correct frequency correction in DPLL — a co-cause of the state bouncing.

---

## [v0.75-rtos]

### Fixed
- **ACQ oscillated (±1 Hz swings, twice frozen by the runaway guard) once the
  calibration was finally CORRECT.** The drift gain used a guessed fixed
  multiplier (×60) that had been implicitly tuned against the old, wrongly
  scaled calibration; with the true ns/V the numeric drift grew ~2.3× and the
  loop over-corrected ~1.8× per step — a textbook overshoot oscillation. The
  gain is now derived from the MEASURED OCXO sensitivity (CT stores 0.40/K in
  g_pid[7].Kp, so LSB-per-Hz is recovered as Kp7/0.40) with a 0.5 damping
  factor: ~60% of the error cancelled per step, unconditionally stable on any
  unit, no per-board tuning. The DPLL frequency term (fixed ×1000, ~6× too weak
  on this unit) is scaled from measured K the same way.

---

## [v0.74-rtos]

### Fixed
- **Wrap-jump quality gate — closes the last known way LC could go wrong.** The
  stepped ADC can report a wrap mid-step, yielding a PARTIAL jump; one was
  accepted as the full span (0.122 V on a ~0.33 V detector), which parked
  zero_offset near the floor (0.09 V) and sent the loop chasing a false centre
  until the frequency ran 3 Hz away. A jump now counts only if it starts from a
  live (un-railed) sample AND is ≥80% of the min–max band actually observed;
  partial jumps are named in the log and the observed band (or time
  cross-check) is used instead. `zero_offset` is now ALWAYS the middle of the
  observed band, never derived from the jump position.
- **Operator verdict line.** LC ends with an explicit "PASSED checks — review
  LL, then 'ES'" or "MARGINAL result — prefer re-running LC before 'ES'", so a
  weak calibration is hard to save by accident.

---

## [v0.73-rtos]

### Fixed
- **Runaway guard rebuilt after a real 3 Hz escape reached PWM 63500 — the old
  guard had three false assumptions.** (1) Its baseline re-anchored on every
  un-railed sample, but during a runaway the phase periodically WRAPS (briefly
  un-railed), so the baseline chased the escape and the 6000-LSB trip never
  fired. It now re-baselines only when genuinely healthy (un-railed AND
  |Δf| < 0.25 Hz). (2) An LSB threshold silently assumes the OCXO's Hz/LSB
  sensitivity; the primary criterion is now the measured frequency error
  itself: phase railed AND |Δf| > 0.5 Hz → freeze (a 2000-LSB backstop
  remains). (3) Freezing the step left the DPLL/LOCK integrator winding up,
  ready to slam PWM on recovery — it is re-seeded to the held PWM while
  frozen. Behavioural test: old guard let the simulated escape reach 6.15 Hz;
  the new one freezes at 0.51 Hz.

---

## [v0.72-rtos]

### Fixed
- **Direction flip now happens IN the sweep, where the rail actually shows.**
  v0.71's 8 s pre-check could not catch the wrong direction: in the rail-prone
  direction the phase exits the sync window only after ~a full range of drift —
  tens of seconds into the sweep (the pre-check passed, then 137 samples
  railed). LC now counts consecutive railed samples during the sweep itself;
  a sustained run (≥15 s) is the direction verdict: it flips the offset sign
  (mirrored around the saved PWM), re-arms picDIV, wipes every accumulator and
  restarts the sweep once. Verified in simulation: wrong side rails at 40 s →
  flip at ~54 s → clean sweep from the good side with the full-span wrap jump
  captured. If both directions rail, the existing mostly-railed abort still
  reports it.

---

## [v0.71-rtos]

### Fixed
- **`LC` auto-detects the ramp DIRECTION — the root cause of every railed
  calibration.** Comparing all field runs revealed the pattern: every failed
  cal had a positive df (ramp pushing the frequency above 10 MHz) and the single
  clean one (range=318) had a negative df. On this detector family the phase
  wraps sawtooth-style only when drifting one way; the other way the pulse just
  widens until the RC pins at the 3.3 V rail and stays. The good direction is
  board-dependent, so LC now probes it: after settling it watches the phase for
  ~8 s and, if pinned to a rail, flips the offset sign, re-arms picDIV and
  settles again (aborting cleanly only if BOTH directions rail). The adaptive
  ramp keeps the detected direction. Also verified: algorithm 7 does NOT run
  during LC (the calibration blocks the control task), so loop interference is
  ruled out.

---

## [v0.70-rtos]

### Changed
- **`LC` is fully self-contained: it ignores the previous calibration.** Per a
  good operator principle — you recalibrate precisely because the stored values
  may be wrong — LC no longer inherits anything from EEPROM/g_ltic: the ramp
  target, wrap threshold, glitch window and prep criterion all start from
  neutral assumptions and everything is measured fresh. This ends the poisoning
  cascade where one bad cal (range=6035) mis-steered the next three runs.
- **Single-wrap range measurement.** The voltage JUMP at a wrap (sawtooth top →
  bottom in one sample) IS the full detector span, so one wrap suffices:
  range = |jump| × ns/V. The ramp target drops to one wrap in the window, i.e. a
  much gentler sweep that no longer pushes the phase out of the picDIV sync
  window onto a rail (the failure seen at 9-22 ns/s). Two wraps, when they occur
  naturally, still enable the independent time cross-check.
- **Prep criterion is universal:** waits for a valid, un-railed, steady phase —
  no assumed centre voltage (detector bands legitimately differ between builds).

---

## [v0.69-rtos]

### Fixed
- **`LC` adaptive ramp is now hardware-agnostic and self-limiting.** The v0.68
  log showed a cascade: a poisoned prior cal (range_ns=6035 from a noise fit)
  set an absurd ramp-speed target, the adaptive increase chased it (offset up to
  1120, 15 ns/s), and the fast ramp pushed the phase out of the picDIV sync
  window entirely — the detector pulse went wide and the voltage pinned at the
  rail for the whole sweep ("180 railed samples"). Three hardware-agnostic
  defences (no detector band is assumed; different builds range from ~0.3 V to
  full 3.3 V swings): (1) the stored range only *guides* the ramp target through
  a wide anti-garbage clamp (20..5000 ns); (2) **rail-backoff** — after each
  ramp increase LC watches ~8 s and, if the phase pins to a rail, halves the
  offset back, re-arms picDIV to regain sync, and proceeds at the speed the
  hardware allows; (3) **self-consistency gate** — results are committed only if
  range ÷ slope implies a physically possible voltage span (≤3.3 V), otherwise
  the previous calibration is left untouched (a bad LC can no longer poison the
  next one).

---

## [v0.68-rtos]

### Fixed
- **`LC` no longer produces garbage when the ramp lands near the OCXO's 10 MHz
  point.** A +70 LSB offset can barely detune the OCXO (df=0.01 Hz → 1 ns/s), so
  no real wrap could occur in the window — yet read glitches (the phase voltage
  updates in steps) exceeded the wrap threshold and produced fake "2 wraps", a
  noise-only fit, and absurd results (ns_per_volt=38615, range_ns=6035). Three
  defences added: (1) **adaptive ramp increase** — if the drift is too slow for
  two wraps in the window, the offset is doubled (capped ±4000) and re-settled;
  (2) **time-validated wraps** — a jump sooner than ~half the expected crossing
  time after the previous wrap is a glitch and is ignored; (3) **volt/time
  range cross-check** — the time between two wraps × phase rate gives an
  independent range measure; if it disagrees >2× with the voltage-span measure,
  the slope is suspect and the TIME range wins (ns/V rescaled to match).

---

## [v0.67-rtos]

### Added
- **`LC` now auto-preps before ramping (operator convenience).** Running `LC`
  used to require a manual `LA 7` / `AP` / "wait for the phase to reach centre"
  sequence first; starting with the phase against a rail was the main cause of
  poor calibrations. `LC` now, on its own: (1) arms picDIV to sync to 1PPS if a
  GPS fix is present, then (2) waits up to ~60 s for the phase voltage to settle
  inside the central band of the detector (centre ± ¼ range, held a few seconds)
  before starting the ramp. It prints each step and proceeds with a clear note
  if the phase can't be centred in time. Just run `LC` — no manual prep needed.

---

## [v0.66-rtos]

### Fixed
- **`LC` now measures the FULL detector range (was a fraction, e.g. <75 ns).**
  Two bugs collapsed `range_ns` on a narrow detector: (1) the wrap threshold was
  a fixed 0.5 V — larger than the whole ~0.33 V detector range — so wraps were
  never detected; (2) `range_ns` was taken from the small slice the phase
  happened to sweep during the ramp, not the detector's full unambiguous span.
  `LC` now sweeps until it has seen **two wraps** (one full cycle), tracks the
  true min/max across wraps for the range, and still fits the slope (ns/V) on
  the clean pre-wrap segment. The wrap threshold is now relative to the detector
  span. Ramp/window retuned (offset 70 LSB, 180 s) so both a long clean slope
  segment and two wraps fit. `LC` reports whether it saw 0/1/2 wraps so you know
  if the range is exact, approximate, or a lower bound.

---

## [v0.65-rtos]

### Fixed
- **DPLL corrected too infrequently for a narrow detector (looked "frozen").**
  DPLL only adjusted PWM every 10 s and LOCK every `lock_interval_s`; on a
  narrow detector the phase sweeps its whole range in ~10-15 s of residual
  drift, so between corrections the phase wandered and wrapped while PWM sat
  still (seen as PWM pinned at one value for 114 samples). DPLL now corrects
  every 2 s. This is *not* a schematic error: in every state PWM (via the RC
  filter → EFC) drives the OCXO — Vphase is only the ADC feedback measurement,
  so there is correctly no analog Vphase→EFC path.
- **LOCK interval clamped to a sane range (1..30 s).** A corrupted
  `lock_interval_s` (e.g. the 50373 seen in a log) would have made LOCK correct
  roughly once every 14 hours; it is now bounded at runtime and in the `LIV`
  command so LOCK keeps tracking.

---

## [v0.64-rtos]

### Changed
- **Removed the unreliable polarity auto-probe; polarity is now set manually.**
  The single-cycle probe could not separate the PWM effect from the phase's own
  drift on a narrow, drifting detector, so it repeatedly detected the wrong sign
  (+1 where the board is −1). ACQ now holds and prints a reminder to run
  `LPOL -1` (or `+1`) then `ES` when polarity is unset, and DPLL/LOCK already
  hold when polarity is unknown. Once `LPOL` is set and saved, all three stages
  use it consistently — this is reliable where the probe was not.

---

## [v0.63-rtos]

### Fixed
- **Detected polarity is now shared by all three stages.** The auto-detected
  sign lived in a static local inside ACQ, invisible to DPLL/LOCK, which then
  fell back to +1 and — on a reversed board with polarity unsaved — drove the
  phase to the ceiling rail with PWM climbing and frequency walking away from
  10 MHz. ACQ now writes the detected sign into `g_ltic.polarity`, so every
  stage uses it (and it prints a reminder to `ES`).
- **DPLL/LOCK hold instead of guessing when polarity is unknown.** With no
  established sign they now output zero correction and let the machine fall back
  to ACQ (which probes), rather than assuming +1 and running away.
- **Runaway guard.** If the phase is pinned to a rail while PWM is pushed more
  than ~6000 LSB from where the loop started, the loop freezes and warns once
  ("check LPOL / re-centre") instead of sliding PWM to an extreme and
  undisciplining the OCXO.

### Note
- Save your polarity: after the loop prints "detected …polarity -1", run `ES`
  so it survives a reboot (this was the root cause of the last runaway — the
  sign was set but never saved, so it reverted to auto/one).

---

## [v0.62-rtos]

### Fixed
- **DPLL and LOCK now apply the board polarity (was ACQ-only).** ACQ used the
  detected/forced `LPOL` sign, but DPLL and LOCK did not — so on a reversed
  board they drove the phase the wrong way, shoving Vphase onto the floor rail
  and dropping straight back to ACQ (the phase would centre in ACQ, hand over to
  DPLL, then get pushed to ~0 V and fall back). All three stages now share the
  same polarity, so DPLL/LOCK pull the phase toward centre instead of into a
  rail. With ACQ handover already working (v0.61), this is what lets DPLL hold
  and progress to LOCK.

---

## [v0.61-rtos]

### Fixed
- **ACQ now nulls the phase drift instead of chasing phase position.** With the
  polarity correct (`LPOL -1`) PWM stopped running away, but the phase still
  swept the whole detector and wrapped, so ACQ never met the "in-window + low
  slope" exit. The residual frequency offset (~-0.26 Hz) drove the phase at
  ~26 ns/s across a 318 ns detector — far too fast. ACQ's dominant term now acts
  on the phase DRIFT (dPhase/dt), driving the frequency offset to zero so the
  phase stops moving; a weak centring term parks it mid-range only once the
  drift is already small. Wrap-induced drift spikes (phase jumping >½ range in a
  step) are rejected so they don't corrupt the drift estimate or the
  slope-gated transitions.

---

## [v0.60-rtos]

### Fixed
- **ACQ ran PWM away when the board polarity was reversed.** ACQ walked PWM in a
  fixed direction toward `zero_offset`; on hardware where increasing PWM lowers
  the phase voltage (opposite sign), that drove PWM ever downward while the
  phase wrapped chaotically, so ACQ never settled (observed as a long ACQ hang
  with PWM sliding from ~41000 to ~17000). ACQ now **auto-detects the PWM→phase
  polarity** with a small probe step, then drives toward the target with the
  correct sign. A new `LPOL -1/0/1` command forces the sign (0 = auto).
- **ACQ now centres on the middle of the detector range, not `zero_offset`.**
  On a narrow low-band detector `zero_offset` can sit near the floor (e.g.
  0.097 V), so targeting it kept the phase against the rail (risking latch-up /
  wrap, per Dan's note about choosing mid-scale). ACQ now aims at the range
  middle, overridable with `LCV <volts>`.

### Added
- `LPOL` (PWM→phase polarity) and `LCV` (ACQ centring target) CLI commands,
  both persisted to EEPROM and shown by `LL`.

---

## [v0.59-rtos]

### Changed
- **Phase-slope gating on state transitions (algorithm 10).** On advice from
  Dan (time-nuts), both LTIC state transitions now check the phase SLOPE
  (dPhase/dt), not just the phase magnitude. Since frequency is the first
  derivative of phase, a small slope means the frequency is already close to
  10 MHz — so ACQ→DPLL now requires a wide slope window and DPLL→LOCK a ~5×
  tighter one, preventing a handover while the phase is merely sweeping through
  centre at speed (which would lock the wrong frequency). LOCK also drops back
  to DPLL if the slope grows. This is what makes the frequency land very close
  to nominal at each handover.

---

## [v0.58-rtos]

### Fixed
- **`LC` ramp far too fast for a narrow detector.** On hardware whose detector
  spans only a fraction of the ADC (e.g. ~0.33 V per unambiguous period), the
  old +2000 LSB ramp drove the phase across the whole detector every ~1-2 s, so
  every sample railed or wrapped and `LC` aborted with "mostly railed". The
  default ramp offset is now a gentle 60 LSB (≈4-5 ns/s on a typical OCXO), and
  `LC` adaptively steps the offset down further if the measured drift would
  cross the detector in under ~15 s. The frequency-measurement fix from v0.56
  is confirmed working (real df now reported, e.g. 1.4-2.0 Hz, not the old
  hard-coded 0.6).

---

## [v0.57-rtos]

### Fixed
- **ACQ now actively centres the phase (was frequency-only).** The ACQ stage
  previously corrected only the TIM2 frequency error; once the OCXO was already
  near 10 MHz nothing drove the phase, so it could sit stuck against a detector
  rail forever and never satisfy the ACQ→DPLL exit test (observed as an
  overnight hang with Vphase parked low). ACQ now walks PWM toward the detector
  centre when the reading is railed, and drives proportionally to the phase
  error once it is in-window.
- **Phase centre taken from calibration, not a hard-coded 1.65 V.** Real
  hardware can have a narrow detector band far from mid-ADC (e.g. 0..0.45 V), so
  the loop now centres on the calibrated `zero_offset` (with a coarse 0.22 V
  fallback) instead of assuming 1.65 V. Run `LC` so `zero_offset`/`ns_per_volt`
  reflect the real band.

---

## [v0.56-rtos]

### Fixed
- **`LC` frequency measurement.** The calibration read the 10 s frequency
  average once, immediately after a 10 s settle — on real hardware that window
  had not yet caught up to the forced ramp, so the ramp rate (and therefore
  `ns_per_volt`) came out wrong. `LC` now settles 30 s, then samples the 100 s
  average (steadier, with a 10 s fallback) twice ~5 s apart and averages them.
- **`LC` rail handling.** Samples where the TIC voltage sits at the ADC ceiling
  or floor (phase outside the detector window) are now skipped rather than
  flattening the least-squares fit, and `LC` aborts with a clear message if the
  ramp is mostly railed (telling you to centre Vphase near mid-rail first).
- **Build fix:** removed a duplicate `g_ltic_voltage` extern in
  GPSDO_algorithms.cpp that conflicted with the `gpsdo_state.h` declaration.

---

## [v0.55-rtos]

### Added
- **Algorithm 10 (LTIC three-stage PLL) — the loop is now implemented.**
  `LA 10` disciplines the OCXO from the hardware TIC phase (PA1) through a
  hybrid ACQ → DPLL → LOCK state machine. ACQ is frequency-led (TIM2) to pull
  the OCXO close to 10 MHz so the phase ramps slowly enough to catch; DPLL adds
  the LTIC phase term for fast centring; LOCK is phase-led with slow updates
  every `lock_interval_s` and a hysteresis band for dropping back to DPLL. The
  picDIV is armed automatically on entering ACQ. The loop works in nanoseconds
  when the TIC is calibrated (`LC`), and falls back to a nominal volt-based
  phase with a one-time warning when it is not. The state persists in
  `g_ltic.state`, so a warm reboot (`RB`) resumes mid-sequence rather than
  restarting from ACQ. The trend field shows `ACQ` / `DPLL` / `LOCK`.
- **Third PID set (ACQ).** `LticParams_t` gained an `acq` PID alongside `dpll`
  and `lock`, with its own CLI verbs `AQP` / `AQI` / `AQD` / `AQL` and EEPROM
  storage. `LL` now lists all three sets.

### Changed
- **EEPROM layout extended to 216 bytes (reserved to 224).** The ACQ PID block
  [200..215] was appended under the same `GPSD2` signature with the usual
  NaN/`0xFF` guards, so older saves still load with the ACQ gains defaulting.

---

## [v0.54-rtos]

### Added
- **`LC` — LTIC self-calibration.** Automatically measures the TIC's
  voltage→time slope without any external reference. `LC` forces a small PWM
  offset so the phase ramps linearly, derives the ramp rate from the TIM2
  frequency error (`phase_rate = df / BASE_FREQ × 1e9` ns/s), least-squares
  fits the TIC voltage against time to get `dV/dt`, and computes
  `ns_per_volt = phase_rate / (dV/dt)`. It also records the swept voltage span
  as `range_ns` and a mid-scale `zero_offset`, detecting one wrap to keep a
  single clean ramp segment. Runs in the control task like `CT`, with the same
  safety pattern (PWM saved and restored, range-guarded results, abort on
  no-GPS / too-few-points / singular or flat fit — params left unchanged on
  any failure). Results go to the live LTIC params; review with `LL`, then
  `ES` to save. New config constants `LTIC_CAL_PWM_OFFSET`, `LTIC_CAL_SECS`,
  `LTIC_CAL_MIN_POINTS`. This fills the calibration fields that the phase-A
  loop will need; the loop itself is still not implemented.

---

## [v0.53-rtos]

### Added
- **Warm/cold restart commands `RB` and `CR`.** `RB` does a warm reboot
  (`NVIC_SystemReset()`) keeping the EEPROM, so the still-warm OCXO recalls its
  disciplined state. `CR YES` does a cold restart: erases the EEPROM (back to
  factory defaults — PWM, model, calibration, LTIC params all reset) then
  reboots; the `YES` confirmation is required because it discards the learned
  OCXO model.
- **Algorithm 10 (LTIC) infrastructure — parameters, CLI and EEPROM.** Full
  parameter set, CLI editing and EEPROM persistence for the planned LTIC
  three-stage PLL (ACQ→DPLL→LOCK), so the configuration is ready before the
  loop itself is written ("phase A"). New `LticParams_t` holds TIC calibration
  (ns/V, zero offset, range), two PID sets (wide-band DPLL + narrow-band LOCK),
  state-transition thresholds, the LOCK interval, and the resumable state.
  Fifteen CLI commands set/show these (`LL`, `LNV/LZO/LRN`, `DPP/DPI/DPD/DPL`,
  `LKP/LKI/LKD/LKL`, `LAT/LDT/LIV`). `LA 10` is accepted by the parser but
  reports "not implemented yet" and refuses to select, so the OCXO is never
  left undisciplined. The loop itself is not implemented — that is phase A,
  pending the LTIC hardware.

### Changed
- **EEPROM layout extended to 200 bytes (reserved to 208).** The LTIC block
  [144..207] was added under the **same `GPSD2` signature**; every new field is
  NaN/`0xFF`-guarded, so EEPROM images saved by older firmware load cleanly with
  the LTIC parameters defaulting until set. No migration or re-init needed.

---

## [v0.52-rtos]

### Added
- **LTIC (Lars' TIC) phase-voltage preview.** The TIC voltage on PA1 was
  already sampled and sent over serial telemetry, but had no on-screen
  presence. Added (all gated by `GPSDO_LTIC`, so zero effect on builds without
  the TIC):
  - a **TFT row** showing `Vph:x.xxxV` (and `… NNNns` once calibrated);
  - an **LTIC entry in the boot-splash hardware checklist** (`[x] LTIC phase
    (PA1)` — shown when compiled in, like the TM1637/TFT, since the TIC is
    read-only and cannot be probed);
  - a **`LTIC_NS_PER_VOLT` calibration constant** in the config (0 =
    uncalibrated → volts only). When set to the measured ramp slope, the
    display and the planned phase-discipline algorithm convert volts to ns.
  This is a **preview/telemetry layer only** — the control loop does not yet
  discipline the OCXO from the TIC (planned as a separate phase, a new
  LTIC-based algorithm). OLED/LCD were intentionally left unchanged (their
  layouts are full); Vphase remains available there via serial logging, which
  is what characterising the TIC needs at this stage.

---

## [v0.52-rtos]

### Added
- **LTIC (Lars' TIC) phase-voltage preview layer.** When `GPSDO_LTIC` is
  compiled in, the latched TIC voltage (`g_ltic_voltage`, already sampled on
  PA1 and discharged each PPS) is now surfaced as a preview: a dedicated
  `Vph:` row on the TFT (below the sensor row, shown only with LTIC built in),
  and an `LTIC phase (PA1)` entry in the boot checklist. Serial telemetry
  already carried Vphase. A new `LTIC_NS_PER_VOLT` calibration constant lets a
  future build convert the voltage to a phase in nanoseconds: while it is 0
  (default, uncalibrated) the displays show volts only; once set, the TFT row
  also shows `<n>ns`. This is preview/telemetry only — the control loop does
  not yet discipline on LTIC; that is a planned separate algorithm. OLED/LCD
  layouts are unchanged (both are full); Vphase will be added there when LTIC
  becomes an operational loop input.

---

## [v0.51-rtos]

### Added
- **CLI commands are now case-insensitive.** The command dispatcher compared
  verbs with `strcmp()`, so `LA` worked but `la` did not. Command matching now
  uses a small case-insensitive helper (`cli_ieq`), so any letter case is
  accepted (`LA` / `la` / `La` are equivalent), including the lowercase verbs
  (`up1`, `dp10`, …) and the `KP`/`KI`/`KD`/`IL` family (whose parameter
  letter is also matched case-insensitively). Command arguments are unchanged;
  `TO A` already accepted either case.

### Changed
- **ZED-F9T (Gen9) support is no longer experimental.** The CFG-VALSET
  survey-in path and the NAV-SVIN monitor fallback were tested on real
  hardware by EEVblog user danieljw, so the "experimental / untested" markings
  have been removed from the code, config and READMEs. No code change to the
  F9T path itself — only its status.

---

## [v0.50-rtos]

### Added
- **ZED-F9T (Gen9) timing-receiver support — experimental, untested.** A third
  survey-in path was added alongside the proven LEA-6T / LEA-M8T ones.
  `ubx_start_survey_in()` now also sends a `CFG-VALSET` (0x06 0x8A) frame
  setting the Gen9 configuration keys `CFG-TMODE-MODE` (survey-in),
  `CFG-TMODE-SVIN_MIN_DUR` and `CFG-TMODE-SVIN_ACC_LIMIT` (the latter converted
  from mm to the F9T's 0.1 mm unit). The survey-in monitor gained a parallel
  `NAV-SVIN` (0x01 0x3B) parser and falls back to it when `TIM-SVIN` does not
  answer, since the F9 generation reports survey-in through NAV-SVIN. ⚠️
  Written from u-blox documentation/ubxtool with no F9T on hand — key IDs, the
  0.1 mm unit and the NAV-SVIN payload offsets are NOT verified on hardware.
  The legacy `CFG-NAV5` stationary frame may NAK on an F9T (harmless; the
  survey-in path is independent). The two tested receivers are unaffected:
  TIM-SVIN is still tried first, so LEA-6T / LEA-M8T / NEO-M8T behaviour is
  unchanged. Documented as experimental in the README and config.

### Changed
- **LCD 20×4 splash subtitle** changed from `GPS-Disciplined Osc.` to
  `GPS-Disciplined OCXO`, matching the TFT splash (both 20 chars, full width).

### Notes
- **NEO-M8T** confirmed (by datasheet analysis) fully compatible with the
  existing LEA-M8T path — same M8 silicon + FW3, same CFG-TMODE2 / TIM-SVIN —
  no code change required. Documented in the timing-receiver section.

---

## [v0.49-rtos]

### Fixed
- **Config macro ordering: `OUT_SERIAL` now respects `GPSDO_BLUETOOTH`.** The
  `OUT_SERIAL` routing macro was evaluated near the top of `gpsdo_config.h`,
  *before* `GPSDO_BLUETOOTH` (and several other feature switches) were defined
  further down. As a result `OUT_SERIAL` always resolved to USB `Serial` even
  when Bluetooth was enabled, and a build with `GPSDO_BLUETOOTH` commented out
  could fail to compile depending on what referenced it. All feature switches
  are now grouped together near the top of the file, and macros derived from
  them (`OUT_SERIAL`) are evaluated afterwards in a dedicated "Derived macros"
  section. No functional change to any enabled feature beyond Bluetooth output
  now actually going to Serial2. A scan of the other source files found no
  further define-after-use ordering issues.

### Changed
- **HT16K33 startup pattern unified with the TM1637.** At power-up the
  HT16K33 now shows `----` (segment-G dashes) instead of `oooo`, matching the
  TM1637's startup pattern — both LED clocks signal "alive, waiting for GPS"
  the same way. The `oooo` indicator is retained for the no-fix-during-
  operation case (where the TM1637 also shows `oooo`), so the two displays now
  behave identically in every state.
- **TFT splash credit line** changed from `jmnlabs + with Claude (Anthropic)`
  to `jmnlabs with Claude (Anthropic)` (dropped the `+`).

---

## [v0.48-rtos]

### Added
- **ILI9488 480×320 SPI TFT support (`GPSDO_TFT_ILI9488`).** ⚠️ Untested — no
  panel on hand yet. The existing 320×240 ILI9341/ST7789 operating screen and
  animated splash are shared and auto-scaled to 480×320 at compile time:
  width ×1.5 and height ×1.33 via independent `TFT_SX`/`TFT_SY` macros (the
  panel aspect differs from a pure 1.5×), and TFT_eSPI fonts mapped up one
  size via `TFT_F`. Geometry verified to fit the panel; not yet run on real
  hardware. Set `ILI9488_DRIVER` + `TFT_WIDTH 320`/`TFT_HEIGHT 480` (+
  `LOAD_FONT6`) in TFT_eSPI `User_Setup.h`.
- **SPI→T6963C bridge as a new display backend (`GPSDO_T6963C`).**
  ⚠️ Experimental / untested — backend is complete and compiles, but the link
  is not yet validated on clean hardware (long-wire bring-up showed ringing
  and spurious CS edges; same on the reference master → a signal-integrity
  issue, not firmware). Disabled by default; leave off until tested on
  short, point-to-point wiring.
  Drives a PowerTip PG240128 (240×128 mono) panel through the external
  `T6963C_SPI_bridge` over SPI1 using high-level drawing commands
  (`T6963C_Bridge.h`). Selectable in the config like the other displays;
  mutually exclusive with the TFT (shared SPI1 pins / display slot).
  - Reuses the TFT's SPI1 pins: `SCK PA5`, `MOSI PA7`, `CS PB13`,
    `READY PB12`; frees `PB15` (was TFT_RST).
  - Condensed 240×128 layout mirroring the TFT screen: header (title + LMT
    time), large frequency (LOGISOSO fonts), status row, value rows
    (PWM/Vctl, INA219, sensors) and a survey-in progress bar.
  - Monochrome panel → the lock/holdover colour cue becomes an inverted
    (filled) box around the status word (`LOCK` / `HOLD` / `H-LOST` /
    `NOFIX`).
  - One batched SPI transaction per refresh (single READY wait), with the
    bridge library's auto-split as a safety net; per-field change-cache to
    skip redundant redraws.
  - Static boot splash (logo + subtitle + hardware checklist); no wave
    animation, since batched SPI rendering would make it costly on a small
    mono panel.

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
