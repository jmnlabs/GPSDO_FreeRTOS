# GPSDO v1.08.71rt

<!-- Kept twice: README.md at the repository root and a copy in GPSDO_FreeRTOS/,
     the sketch folder, which the release zip also carries. The copy differs
     only in its links, one level up (doc/ -> ../doc/). Refresh both with every
     build. -->

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
loops. Build 68's PPS-qualified holdover, `LC`'s arm-and-wait, the per-port
telemetry and the faster GPS tunnel — and the idea of the LM75 — come from the
fork of **Zsolt Zsiros** (ZsZs73), who found each of them on his own board.

---


## Documentation

The full documentation lives in [`doc/`](../doc/). Everything is maintained in
three languages:

| | English | Polski | Español |
|---|---|---|---|
| **Step-by-step user manual** — build, flash, calibrate, every command, troubleshooting; written for first-time users | [MANUAL_EN](../doc/MANUAL_EN.md) | [MANUAL_PL](../doc/MANUAL_PL.md) | [MANUAL_ES](../doc/MANUAL_ES.md) |
| **Manual** — hardware, wiring, algorithms, CLI, display | [README_EN](../doc/README_EN.md) | [README_PL](../doc/README_PL.md) | [README_ES](../doc/README_ES.md) |
| **Changelog** — what changed and why | [CHANGELOG_EN](../doc/CHANGELOG_EN.md) | [CHANGELOG_PL](../doc/CHANGELOG_PL.md) | [CHANGELOG_ES](../doc/CHANGELOG_ES.md) |
| **Flash ring bring-up** — first-time setup of the flash ring buffer | [BRINGUP_EN](../doc/FLASH_RING_BRINGUP_EN.md) | [BRINGUP_PL](../doc/FLASH_RING_BRINGUP_PL.md) | [BRINGUP_ES](../doc/FLASH_RING_BRINGUP_ES.md) |

New here? Start with the [step-by-step user manual](../doc/MANUAL_EN.md) — it
walks through the build, the flash, the first calibration and every command
assuming no prior knowledge. The reference manual in your language covers the
same ground in more depth.

---

## What's new in v1.08 (builds 58-71, released 2026-11-04)

Fourteen builds since v1.07, newest first. In short: algorithm 13, the
Kalman filter, now beats algorithm 11 on the author's board and hurries
while a disturbance lasts; the phase detector is read at the PPS edge, from a
line through the whole burst, and leaves out glitched conversions and a
10 MHz pickup; a start far from the operating point settles; and four fixes
and the LM75 thermometer come from Zsolt Zsiros's fork. The
[changelog](../doc/CHANGELOG_EN.md) has every build as it was measured.

**Build 71: a third of the burst left out whole, and a typical bend that
follows the level.** On some bursts every third conversion meets the 10 MHz at
the same point of its period, and a detector that picks up its edges has that
third off by the same few codes — 17–30 on Dan's board, where such a burst
read several codes off or was refused. Now that third is left out whole first:
his four such bursts read on a line through the other two, and every other
burst reads exactly as before. On the author's board a burst's bend follows
its level, so a step that takes the phase 60–100 ns away had its bursts
refused as bent (131 in 16 minutes on 27.10): the board's typical bend now
moves at once to the median of the last 15 when that is more than 3.5 codes
away, and a whole step on the board then refused ~13. `KL` and `KT` no longer
read a Q at its ceiling as a reason to shorten `KT`: on Dan's board `KT 1000`
sat there and beat `KT 500`. Algorithm 13's gate is build 70's: giving way at
the fourth refusal of a step, as build 71 first did, made it give way seven
times in one step on the board, and the versions that gave way once did worse
than build 70 elsewhere (see the changelog).

**Build 70: the detector's fit may leave out twenty conversions, and the notes
stop blaming the PWM carrier for Dan's glitches.** They come in pairs about
21 µs apart, four or five to a burst, from something outside the GPSDO
switching at roughly 23 kHz: they stayed with the PWM pin held low and the
backlight at `BL 100`, and came and went with the time of day. With twelve
drops a burst with six pairs or more was refused and the last reading kept —
with the other refusals, a fifth of the seconds on his board. With twenty, the
bursts he sent that had been refused for them read within 0.2 codes of a
robust line, and a clean burst, which leaves out nothing, reads exactly as
before.

**Build 69: algorithm 13's controller hurries while a disturbance lasts, and
the PWM pin is held low while the AD5680 drives the EFC.**
`KC` is a choice about noise; a step of the oscillator's frequency is not
noise. On 21.10 a three-minute sag of the author's 5 V rail moved the OCXO by
2.7·10⁻⁹, and at `KC 33` the phase went to +75 ns, then −51 ns, and took 12
minutes to come back inside ±10 ns. The new `KF` (16 s, `KF 0` = off) takes
over from `KC` while the filter's phase is plainly out — past 10 times the
board's calm noise, or the gate giving way — and hands back after two calm
minutes: replayed, the same event goes to +41/−29 ns and 5½ minutes. Calm
operation is untouched, to the last bit of the PWM in the simulator. And up to
build 68 a board on `EXT` kept the dither engine running on its PWM pin — the
code written at boot, before the stored path was recalled, replayed for good.
Dan's scope found it on the PWM section of his V3, at 11.71 kHz. The first
write to `EXT` now holds the pin low; a board on `DITH` or `PWM` is
untouched.

**Build 68: the GPS reference is a pulse as well as a fix, `LC` waits for the
phase instead of re-arming six times, the report reaches USB and Bluetooth
separately, the GPS tunnel keeps up, an LM75 is logged, and `KC` is `KT/3`
again.** Four of these, and the idea of the LM75, come from the fork of Zsolt
Zsiros (ZsZs73), each found on his own board. A timing receiver in Time Mode
reports its fixed position as valid with no signal, so holdover now begins when
the fix goes or the pulse has been missing for 10 s, and ends only when the fix
is back with 10 s of unbroken pulses; a manual holdover stays manual through
such a loss, and the picDIV — `LC`'s arms included — is armed only with a pulse
present. `LC` arms once and lets its own sweep carry a low landing into the
detector's band — a re-arm lands where the last one did, so the six tries
failed alike on a board whose landings sit below it. In parallel mode the
report goes to each port on its own, so a connected USB host that stops reading
can no longer stall it through the tee, and the GPS tunnel empties the
receiver's buffer on every pass instead of moving a byte a millisecond. An
LM75-family thermometer (`GPSDO_LM75`, 0x48) is detected at boot and logged
after `CPU:` on the `RH` sensor line. `KC` 0 = auto is `KT/3` again: on the
author's board, against `KC 16`, half the PWM motion and 1.7 times lower
frequency noise at 4–16 s, the same by 256 s, for a fifth more logged phase.
And build 67's notes blamed Dan's glitches on the EFC PWM's carrier: they are a
23.4 kHz square wave — the 12-bit dither's carrier, which his board does not
build — from a source not yet found, and the notes now say so.

**Build 67: the LED spinners stop doubling the CPU load, a burst with glitches
on it is read, and a long `KT` no longer slows a cold start.** On the author's
board the CPU figure ran at 55–56 % through survey-in against 27 % afterwards:
during warm-up, survey-in and `CT` the display task wakes every 150 ms to step
the LED spinners, and every one of those wakes rebuilt the TFT header and
redrew the OLED and LCD as well. Now a wake that no PPS or GPS sentence caused
steps the LED digits only, the screens are redrawn on the PPS as at any other
time, and an HT16K33 frame goes out only when it changes. Dan's `LB` listings
showed a pair of glitches at each edge of a 23.4 kHz square wave in every burst
(not the EFC PWM's carrier, as this first said), and a third of his bursts
refused as off the line; the fit may now leave out twelve conversions instead
of eight, which reads both refused bursts and changes nothing on a clean board.
Algorithm 13's pull-in uses `KT` only up to 100 s, so the 300–1000 s a quiet
OCXO wants no longer costs a cold start half an hour (in the model of Dan's
board: 6–12 minutes at `KT 1000` against 28–41); at 100 s and below it runs bit
for bit as build 66. `DAC` now reads the timer clock — 96 MHz on the BlackPill,
not the 100 MHz it assumed — and prints the real carrier; the `KT` note above
200 s says what to watch rather than calling a long horizon wrong; and the
tuner's position redaction also catches a line the serial link has damaged.

**Build 66: algorithm 13 reads the ramp below the window, trusts the edges
less, and takes no phase from a repeated reading.** Both of build 65's starts
of 07.10 began on frequency, from the operating point the live store had kept,
and still took 17 and 22 minutes to lock. At 11:49 the divider landed below
the detector's window, and since a synced divider can move its edge only by
whole steps of its own clock, every re-arm landed in the same place: four arms
and 800 s without a phase. At 17:33 it landed near the edge, two readings
19 ns apart passed for a slope of 17 ns/s and the loop chased it off the ramp,
after which the arm gate refused to re-arm for 1000 s. And a third of each
pull-in's seconds republished the last reading, which the filter took as new
and then refused when it jumped. Algorithm 13 now reads the RC ramp below the
window, down to 0.9 of `LC`'s span, as fifteen times noisier than a reading at
the zero; takes a reading as noisier the further it sits from the zero, from a
quarter of the band out (never inside `LAT`); skips a repeated reading, which
the trend shows as `KEPT`; and arms by the range it can read. `tools/loopsim`
now models this board's divider, ramp and reads: build 65 left 46 of 264 cold
starts unsettled, with up to 78 arms, and build 66 settles every one with a
single arm, worst 587 s. In the bench the locked loop and the restart kick are
unchanged to the digit. The price is a slower pull-in on frequency (263–277 s
against 217–226), from a rail that reads in band (the 3000 ns harness board,
up to 70 s more at the median) and with `CT`'s K over the plant's (up to 1.7
times as long).

**Build 65: algorithm 13 pulls in from far out, the board's bend is learned
only on the ramp, the live store keeps the operating point and its span, a
full flash ring keeps the settings, a loop going rail to rail says so, and the
detector's capacitor is discharged with either detector.** Build 64's first
start came up 0.47 Hz off the operating point with the picDIV unsynced, and
`LA 13`, typed during the survey-in, swung the PWM from rail to rail for an
hour and a half: the limit on how fast its correction may change held braking
as well as speeding up, so a PWM ramped to a few hundred LSB a second could
not stop. The limit now holds only growth. Build 65's own first start (07.10,
a reset into algorithm 13 at the same stale code) showed what else was wrong:
with the detector railed the filter steered on a phase built from its own
estimate and drove the oscillator 0.8 Hz the other way, armed on a 100 s
average still 0.77 Hz behind, and then convicted the healthy detector on that
same average. Now it keeps no phase until the detector reads, carries the
average to the present with the corrections the loop has booked since, and
hands trust back when an arm moves the reading; replayed as the board runs the
loop (`tools/loopsim/coldstart.sh`), every railed cold start up to ±6000 LSB
settles in 149–482 s with a single arm, where build 64 failed from ±800, and
build 54's restart-kick scenario comes out the same as before. In the same
railed start, bursts bent +70 codes taught the bend gate +20, and it then
refused the board's own bursts, freezing the reading for seconds at a time;
the bend is now learned only from bursts that read inside `LC`'s band. The
0.47 Hz was the live store's: it saved the code only with the drift model,
which algorithms 11–13 never move; now the code is saved on its own after
20 minutes of lock, and the slot says which span it is in, so a power cut
between the span record's save and the code's no longer starts the oscillator
~1e-7 off (`tools/spansim`). Saving it showed up an older fault: a full flash
ring erased the sector and kept only the slot being written, so the settings
and the Kalman record went with it and the next boot came up on defaults; a
wrap now carries the newest record of each type across the erase
(`tools/flashring`). A second review then found two older faults that the new
saves made worse: two tasks could write the same slot at once — the ring now
takes a mutex, and a boot carries on after the last slot written — and `LC`
left the loop starting from its sweep code, which the save after it kept; `LC`
now puts the code back. The rest of its list is in too: a settings save is one
read and one write, `CR` erases once and says so, a wrap tries a failed flash
operation once more, and the save threshold is 0.02 Hz through `CT`'s K. A
loop that takes the PWM from one end of the DAC to the other with no lock
between now prints `LOOP: rail to rail …` and shows `RAILS` for the trend. And
`GPSDO_LTIC_ACTIVE_RESET`, on in the shipped configuration, is now documented
as right for both detector variants: the discharge comes after the read, and
without it the cap relies on a leak that slows several-fold when cold. Build
64's first night on the board was the best yet: phase sd 1.64 ns, white floor
0.94 ns, ADEV 2.8e-12 at 1024 s, one kept reading in a thousand seconds.

**Build 64: the bend gate counts from the board's own bend, and the command
line keeps quiet at the edge.** Build 63's first night on the board had none
of build 62's sawtooth spikes (the largest one-second step 6.5 ns, no reading
rejected by the Kalman filter), but one second in nine published a kept
reading, and `LB`'s new counts put nearly all of them on the bend gate. Every
burst on the board bends by a few codes, the same way each second (each
conversion takes ~0.12 % of the capacitor's charge, so a burst falls ~180
codes), and the gate counted from none, so it refused the tail of the board's
own bends. It now counts from an average of the board's bend that the
firmware keeps (−3.1 codes in 70 bursts on 04.10), and refuses only a burst
bent 3.5 codes or more unlike the rest. And the reading after an `LB` came
out 8–20 ns high 30 times in 70: its ~3 kB were still going out on the
Bluetooth UART at the edge. The command line now holds its output from 150 ms
before each edge until 2 ms after it.

**Build 63: a kept reading keeps its own sawtooth correction.** Build 62's
first night on the board took the detector's white floor to 1.14 ns (1.64
with build 61) and the phase's sd to 1.74 ns (2.65), and the 233.24 s spikes
were gone. What was left were single spikes of up to ~20 ns, 55 in the night:
a second whose reading was kept (about 1.3 % of them — the edge not caught,
the line refused, or a jump held back) was paired with the next pulse's qErr,
so when the receiver's sawtooth jumped, the phase jumped with it. Now the
reading and its qErr stay together. `LB` also counts, since boot, what became
of every second — the edge caught or not, the line refused and why, the
reading kept — so where the kept seconds come from can be read off the board.

**Build 62: the phase detector is read at the PPS edge itself, on a quiet
chip.** Build 61's line took the detector's white floor on the board from 2.06
to 1.61 ns overnight — and showed a one-second +10…16 ns spike every 233.24 s,
the beat of the GPS second against the 1 ms RTOS tick, which once a beat
landed on the charge pulse. Now the firmware predicts each edge from the last
two, masks every interrupt from 10 µs before it to 5 µs after it and polls the
capture itself, so nothing runs on the chip during the pulse; the converter is
driven through its registers instead of `analogRead()` (54.5 µs a call): 64
conversions from 5 µs after the edge, during which the serial ports and USB
keep running, a VREFINT conversion before it so the sampling capacitor starts
the same every second, and a line through them read at the edge itself,
whatever the leak rate. Modelled: 0.4–0.5 LSB per reading against the median's
1.2–1.6. `LRM 0` still brings back build 60's median for an A/B; run `LC` once
after updating, since the two modes read the leak at different moments.

**Build 61: build 60's defaults undone where the board showed them wrong, and
algorithm 13 now beats algorithm 11.** Build 60 chose them on a replay without
the oscillator's own fast noise; the same week's logs put algorithms 12 and 13
at about twice algorithm 11's phase noise. Algorithm 13 is back at `KT` 100 s,
with `KC` = `KT/6` (16 s; `KT/3` again since build 68): on the board, a night
at 2.63 ns logged phase sd against algorithm 11's 2.73, ADEV at 1024 s 4.4e-12
against 4.7e-12, with the PWM moving about 2.5 times as often. Algorithm 12's calm mode no longer holds
the short levels back (×3 limits) and takes the frequency term whole from a
fixed 128 s level rather than one tied to `MR`; it still does not reach
algorithm 11. The loop simulator is now calibrated against the board
(`bench.py run --hw` and a new algorithm-11 night plant): it reproduces that
week's four runs within 20 % and predicted the algorithm 13 result before the
board ran it. And the phase detector is read from a line through the whole
burst of sixteen conversions, timestamped on TIM2 and read at a fixed 200 µs
after the edge, instead of from its median — which was a single conversion,
sitting on the ADC's uneven codes (a ~3 ns staircase in the logs) at whatever
moment the task woke. `LRM 0` brings the median back for an A/B, `LB` shows
the last burst.

**Build 60: algorithms 12 and 13 calmer by default** (build 61 undid the
algorithm-13 defaults and the ×3 limits). Algorithm 12's new calm
mode (`MM 1`; `MM 0` is the original path) keeps the fast levels for large
errors, takes the long-window frequency term whole and ramps every correction
at 1 LSB/s: in replay its 9–18 LSB steps become 1 LSB ones and ADEV at 10–30 s
halves. Algorithm 13 now runs `KT` 200 s with `KC` = `KT` (was 100 and
`KT/3`): with the bench's measured detector wander it matches algorithm 11's
ADEV at 10–100 s, beats it from 300 s up, keeps a smaller true phase error and
moves the DAC a fifth as much. The tuner plots the commanded control voltage
(`Vcmd`, from the PWM code) instead of the noisy ADC read-back. And a fault in
the loop simulator — a `float`/`double` mismatch that corrupted every fine-DAC
correction of algorithms 12 and 13 — is fixed; the instability it showed at
long `KT`/`KC` was never the loop's.

**Build 59 changes no code.** The source comments were read against the
code: history went to the [changelog](../doc/CHANGELOG_EN.md), statements the
code contradicted were corrected, and configuration macros that no code reads
— `LTIC_NS_PER_VOLT`, `LTIC_CAL_PWM_OFFSET` and a few more that looked like
settings — were removed. Every unit preprocesses to the same tokens as build
58 in 39 configurations, so the program is the same.

**The span pair believes only nulls.** A pair of codes learned at a jumper
move is what keeps the EFC voltage across it, and build 57 learned one from
any code the loop called locked. On the V3 prototype both false pairs came
from a hand-wired bench where the shifter went in or out before PB14 followed
— the loop then locks on the other position's plant, and the next move lands
up to 1.4e-7 off — and a move in holdover or on a code typed with `SP` would
have recorded one too. Now no pair is taken in holdover or after a code set by
hand, and each position keeps the code `CT` found 10 MHz at: a locked code
counts as a null only near its own position's and not nearer the other's.
In four of five simulated cases the next move lands under 1e-11, where build
57 put it 3e-8 to 1.2e-7 off; in the fifth both land alike, and build 58 just
stores no false pair.

**`CT` resolves gentle plants to about 1 %.** The second pass now reads
100-second averages (0.01 Hz steps instead of 0.1 Hz) over the same 0.8 Hz of
swing in either span position, so the two positions' K are measured on the
same stretch of the oscillator's curve. About five minutes more per `CT` on
such boards. A `CT` that fails now puts the loop's code back instead of
leaving the oscillator on its last sweep point.

**`LRN` reads its whole argument** — `LRN 1e3` no longer switches learning
on, nor `LRN ON` off; `LRN R` saves at once. Details in the
[changelog](../doc/CHANGELOG_EN.md).

## What's new in v1.07 (builds 43-57, released 2026-09-24)

**The output path tells the truth.** `DV` and `AV` state what full code means
at the output and what divider sits before the ADC pin, so the `DAC` report's
commanded-against-measured check — which flagged MISMATCH on every reading of
a 5 V board — becomes exact; `VS` says what the PA0 divider is jumpered to
(V3 boards: the 5 V rail or the voltage reference, checked against `DV`). The
report also names what the converter can really do: an 18-bit step on the
AD5680, 24 on the dithered PWM, 50 000 steps on plain PWM in a build without
the dither engine — not one 16/24-bit figure for all three.

**CT handles gentle plants.** A second three-point pass, wider and centred on
the fitted 10 MHz code, runs when K is small (span-reduced EFC, external
DAC) — on such boards the fixed sweep moved the oscillator under a hertz and
the slope fit scattered 10-50% high against DMM ground truth. Both sweep ends
keep a rail guard, and the plausibility floor drops to 0.01 mHz/LSB.

**Indicators stopped bluffing.** The status bar's lock verdict comes from the
loops themselves (`tft_loop_locked()` with hysteresis; four states instead of
one green lie), algorithm 13 publishes a real verdict computed in the filter
(detector spoke, estimate inside the band, uncertainty inside the band — a
detector frozen 1.3 µs out no longer reads as locked for four hours), the
yellow LED stays lit in manual holdover when the fix drops, and `LPOL 0`
says "not set" instead of the "auto" it never was.

**Readings that look calm but aren't are gone.** The serial phase report now
prints `dph:ovf` outside the detector's band instead of a plausible number
measured at three times the noise; `LC` rejects an anchor slope the ramp
cannot have (one such calibration cost eight picDIV re-arms); the tab report
leaves unfilled averages empty rather than printing 0.0 Hz; algorithms 12 and
13 finally count corrections in `CS`.

**Under the hood:** the control voltage no longer passes through
`analogWrite()` (TIM4 driven through registers, carrier unchanged); an
uninitialised read (`snap_c`) and a null pointer (`set_trend(0)` → `NoCT`)
fixed;
settings records grow without version bumps; `BL` dims the TFT backlight
(30-100 %, and 100 % is the electrically quietest setting); files renamed to
a uniform `gpsdo_` prefix and the repository re-laid-out so a clone reads.

**Two EFC spans, two calibrations (`SPAN`, build 55).** A board with the EFC
level shifter runs two different plants — on the V3 prototype K differs about
3.5 times between the jumper positions (3.3–3.6, measured on one reference) —
and every coefficient is derived from K. A
second pole of that jumper now reaches PB14, and the firmware keeps one
calibration per position: on a move it re-derives every coefficient, rescales
the learned state, and **remaps the control code so the EFC pin keeps its
voltage** — through a pair of codes *measured* on the board, not modelled.
Simulated over full power cycles against a build without the sensing: a move
between calibrated positions lands at 1e-11 instead of 1e-7. A board without
the wire behaves exactly as before.

**The DST bug every zone had (build 52).** Transition instants are written in
local time and were compared against UTC — an error of exactly the zone's
offset at the boundary (New York switched five hours early, on the Saturday
evening; London's spring change looked right only because GMT is UTC). The
comparison now runs on day-of-year ordinals with each boundary converted by
its own offset; verified minute-by-minute against IANA data across 416 zones.
The zone table is regenerated from IANA 2026d and its generator now checks
its own work two years ahead — including Dublin, which writes its rule
backwards.

**The loops behave at the moments that were never tested (builds 54-57),
measured on real captures.** Algorithm 11 spent five minutes answering
detector noise at full gain after the phase was already home (replayed on a
recorded plant, the mean output step falls from 5.9 to 0.5 LSB, and to 0.2
once settled); algorithm 13 kicked the DAC 143 LSB one second after every
restart (the short control horizon now waits for a horizon's worth of
measurements, and the correction is slew-limited); `CT` restarts the loop
after centring it, so its first steps stop undoing the calibration. And the
build number moved into the firmware's name — `v1.07.57rt` — so a capture, a
photo or a tuner line says which build it came from; `DV` keeps its third
decimal (the ADR4540 boards run 4.096 V).

**Upgrading from v1.06:** settings carry over — PID, LC, timezone and the
rest — with the new fields at their defaults; no factory reset.

The full history, with the reasoning and the ideas that were tried and
abandoned, is in the [changelog](../doc/CHANGELOG_EN.md).

## Previously — v1.06 (build 42)

**Algorithm 13 — a Kalman filter.** Three states (phase, frequency, aging)
with scalar updates; it *measures* its own detector noise (R, from two lags of
differences) and the oscillator's frequency walk (Q, adapted, bounded at
R/KT³), so the GPS-vs-oscillator weighting moves with the hardware it lands
on. Honest verdict: on a detector-referenced comparison algorithm 11 still
wins — the filter refuses to chase the detector's slow zero wander (~2.6 ns
over ~45 s on this bench), which is mathematically right and metrically
invisible. Documented in manual section 4.6 and Appendix D (no formulas).

**Controller/estimator split (`KC`).** The filter's bandwidth (KT) and the
phase-nulling pace (KC, default = KT/3, in force from the loop's first settle)
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
were tried and abandoned, is in the [changelog](../doc/CHANGELOG_EN.md).

## Quick start

1. **Build** — Arduino IDE with the STM32duino core, board *Generic STM32F4 →
   BlackPill F411CE*. Display and peripherals are selected in
   `gpsdo_config.h`.
2. **Flash** — via ST-Link or DFU.
3. **Connect** — serial at 115200. `H` lists every command.
4. **Calibrate** — `CT` first (~3 min; ~11 on a gentle plant): it measures the oscillator's Hz-per-LSB
   slope and tunes every algorithm from it. Then `LC` to calibrate the phase
   detector. **That order matters** — `LC` without a measured slope falls back to
   a generic value and comes out quietly wrong.
5. **Choose the loop** — `LA 11` for the continuous-PI loop (recommended),
   `LA 10` for the three-stage one, `LA 12` for the multi-level accumulator,
   or `LA 13` for the Kalman filter. Gain is derived automatically from `CT`;
   the Kalman measures its own R and Q from your hardware.
6. **Save** — `ES` writes the settings to the flash ring. Preferences save
   themselves; loop tuning needs an explicit `ES` and the reply says which.

The [manual](../doc/README_EN.md) covers each of these properly.

---

## Repository layout

```
GPSDO_FreeRTOS/     the Arduino sketch — source files and nothing else
doc/                manual, changelog, tuner guide, bring-up (EN / PL / ES)
tools/              harnesses and analysers: hostcheck, loopsim, lticsim, spansim, livestore, railwatch, flashring, hosttests, the tuner
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
gpsdo_span.cpp              EFC span jumper on PB14: one CT calibration per position
gpsdo_backlight.cpp         TFT backlight dimming on PB5 (the BL command)
gpsdo_health.cpp            correction statistics behind the CS command; the rail-to-rail watch
gpsdo_flash_ring.cpp        wear-levelled flash storage, settings and live data (a wrap keeps every record)
gpsdo_settings_store.cpp    the settings block: what is saved and in which group
gpsdo_live_store.cpp        learned drift and damping, LC and the operating point, saved as they change
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
