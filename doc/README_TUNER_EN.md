# GPSDO Tuner

**English** | [Polski](README_TUNER_PL.md) | [Español](README_TUNER_ES.md)

📖 [Project home](../README.md) · [README](README_EN.md) · Manual: [MD](MANUAL_EN.md) · [PDF](MANUAL_EN.pdf)

A desktop console for tuning the loop live and watching what it does: three
scrolling plots, one tab per parameter group, and a manual command box for
anything the tabs do not cover.

It is a **tuning aid**, not a measurement instrument — see *Limitations* below
before drawing conclusions from what it shows.

---

## Requirements

**Python** 3.9 or newer, plus four packages:

```
pip install PySide6 pyqtgraph pyserial tzdata
```

| Package | Used for |
|---------|----------|
| PySide6 | the Qt user interface |
| pyqtgraph | the live plots |
| pyserial | talking to the board |
| tzdata | the **Generate gpsdo_tz_table.h** button only |

`tzdata` is optional if you never press that button, and on Linux or macOS the
system already provides the same zone data. On Windows it is the only source,
since no IANA database ships with the OS. Refresh it later with
`pip install -U tzdata`.

Run with `python gpsdo_tuner.py`, or double-click it on Windows: the console
window that opens is minimised to the taskbar automatically, and stays
available in case a traceback needs reading.

---

## Version matching

The tuner carries a `TOOL_VERSION` tracking the firmware release it was written
for. On connect it reads the board's own version and compares:

- **match** — the status bar shows the board's whole identity, as far as it
  offered one:

  ```
  connected — firmware v1.07.57rt  2026-09-23 11:02  CRC 5A41C90B
  ```

  The three facts answer different questions. The **version** says which
  protocol the tuner is talking. The **build** — in the name from build 56 on,
  `.57rt` (build 56 itself wrote `-rt56`); older firmware (`v1.07-rtos`) sends
  it separately and the line shows `build N` — and the compile time say which source tree it came from. The **CRC** says which binary is actually running, and it
  is the only one that cannot be stale — the board computes it from its own
  flash at boot, so it stays honest even when the Arduino builder reuses an
  object file and the timestamp does not. Everything past the version is
  optional: an older firmware answers `V` with the name alone and the line
  simply says less.
- **mismatch** — the status bar and the Raw monitor both say so

A mismatch is not fatal and the tuner will still talk to the board, but expect
fields to read oddly or commands to be rejected: an older tuner does not know
about newer telemetry, and a newer one may send verbs the board has never heard
of. Use the pair that shipped together.

---

## Tabs

| Tab | Purpose |
|-----|---------|
| **PID algo 3-9** | Kp / Ki / Kd / I_LIMIT for the frequency-domain algorithms |
| **LTIC (algo 10)** | Per-stage PID for the three-stage phase loop, plus the detector calibration and the damping-average window (`FAD` / `FAL`, per stage) |
| **LTIC-Lars (algo 11)** | The continuous-PI parameters (`LG`, `LD`, `LTC`, …) |
| **LTIC-MLA (algo 12)** | The two scalars (`MG`, `MR`) and the eleven per-level phase limits |
| **Calibration** | `LC`, `CT` and the detector constants |
| **Raw monitor** | Everything the board sends, unparsed |
| **Help** | The full firmware command reference, in the algorithms' own order |

Every parameter group is read on connect, so the panels start populated rather
than empty. The tabs follow the algorithm numbers. Commands that describe the
board rather than a loop — the output-path scales (`DV`, `AV`, `VS`), the EFC
span jumper (`SPAN`), the backlight (`BL`) — have no tab of their own: they
go through the command box, and the **Help** tab documents them with the rest.

---

## Plots

Three panes, updated once per second. What the upper two show depends on which
algorithm the board reports:

| | Algorithms 10 / 11 (LTIC) | Algorithm 12 | Algorithm 13 | Algorithms 0-9 |
|---|---|---|---|---|
| Top | Phase `dph` (ns) | Phase error `ph` (ns) | Phase **estimate** `ph` (ns) | Learned drift (LSB) |
| Middle | Detector `Vphase` (V), with band guides | Control voltage `Vctl` (V) | Detector `Vphase` (V), with band guides | Control voltage `Vctl` (V) |
| Bottom | Frequency error (Hz) | Frequency error (Hz) | Frequency error (Hz) | Frequency error (Hz) |

Only the LTIC loops have a phase detector, so under any other algorithm those
two panes would sit empty for the entire session. They are repointed instead,
and the titles follow automatically — no setting to change.

Algorithm 12 gets its own pairing rather than borrowing either of the others: it
does not use the self-learning feed-forward, so the drift trace would be flat,
and its phase comes straight from the detector rather than through a loop
filter, so it is not the same quantity `dph` plots. The detector band guides
come down whenever the middle pane is showing a control voltage instead.

Algorithm 13 plots what the Kalman filter BELIEVES the phase to be rather than
this second's reading — that estimate is the whole point of having a filter —
and puts `Vphase` underneath it, because the question this loop most often
raises is whether the detector is alive at all. It fell through to the 0-9
pairing at first, so the top pane was labelled "Learned drift" over a series
algorithm 13 never sends.

### Span and Follow

**Span** sets how much history is visible: 1 min, 5 min, 15 min, 1 h, or *all*.
With a span selected the trace scrolls leftwards at a constant scale instead of
the axis stretching to cover the whole buffer.

**Follow live** keeps the window pinned to the newest sample. Drag or wheel any
plot and it unticks itself, handing the axis to the mouse so the whole buffer
can be explored; tick it again — or change the Span — to jump back to live.

**Clear plots** discards every buffered sample and restarts the time axis at
zero — useful after a false start, and quicker than restarting the tool and
losing the connection with it. It clears the buffers as well as the traces, so
nothing scrolls back into view afterwards.

**About** replays the boot animation, for no reason beyond the pleasure of it.

---

## Limitations

**History is capped at one week.** The tuner holds 604 800 samples at the
1 Hz telemetry rate. Anything older is discarded as new data arrives and cannot
be recovered; nothing on the plots is written to disk. The buffers are arrays of
doubles rather than lists of Python floats, so a full week of every series costs
about 82 MB of host RAM instead of 406 — and nothing is preallocated, so a
five-minute session still costs kilobytes.

**The plots are not a logger.** Plotted data lives in memory only and is lost
when the window closes. Use **Start logging** (Raw monitor tab) for anything you
intend to keep — see below.

### What logging writes

The dropdown beside **Start logging** chooses the format, and it is fixed for
the life of the file:

| Setting | Writes | About a week |
|---|---|---|
| **Full log** | every received line, exactly as printed, to `gpsdo_YYYY-MM-DD_HH-MM-SS.log` | ~217 MB |
| **CSV only** | one row per telemetry second, analysis columns only, to `…​.csv` | ~65 MB |
| **Both** | the same capture written to both files | ~282 MB |

Both are opened line-buffered next to the script, so a run that ends badly
leaves usable data rather than an empty file of unflushed buffers.

The **full log** is the raw telemetry text — everything the board said,
including CLI replies and boot banners. It is what to send someone who is going
to look at the run rather than compute from it, and it is the only format that
preserves anything the CSV has no column for.

The **CSV** is for computation: `pandas.read_csv` and `numpy.loadtxt` both read
it with default settings, since the two provenance lines start with `#`. The
columns are what every analysis of these logs has actually needed, not
everything the firmware prints:

```
utc, up_s, algo, state, dph_ns, qerr_ns, vphase_v, pwm, f10, f100,
ph_ns, level, corr, sig_ns, zc, bmp_c, sat, hdop
```

`ph_ns`, `level`, `corr`, `sig_ns` and `zc` are the algorithm-12 diagnostics and
stay empty under any other algorithm; `f100` is empty until the 100 s window has
filled. An empty cell always means *that field was absent from that second's
telemetry*, never zero.

Three columns deserve a note:

- **`up_s` is trustworthy only from firmware v1.06 onwards.** Measured over
  75 055 blocks captured under v1.05: UTC advanced by exactly one second every
  single time, while the uptime counter repeated or skipped a second 118 times
  (0.16 %) and gained 12 s over 20.8 h. It was counted from a free-running MCU
  timer that runs about 159 ppm fast; v1.06 counts it from the PPS instead. The
  tuner writes both columns exactly as received and repairs neither — a logger
  that quietly fixes its input is not one you can use to find this sort of
  thing — so for a capture from v1.05 or earlier, use `utc`.
- **`hdop` is not always a number.** A LEA-T that has completed survey-in prints
  `HDOP:TIME`, and that flag is the more useful of the two facts — it is the
  mode in which the 1PPS is worth trusting. Parse the column with
  `errors="coerce"` if you want it numeric.
- **`vphase_v`** is the raw detector ramp voltage, and the only column that
  reveals a railed phase detector. `dph_ns` derived from a railed ramp looks
  like an ordinary number.

Left out on purpose: `Vctl` (it is `pwm` through an RC network, and `pwm` is the
exact figure), humidity, pressure and the INA rails (across every capture so far
they have never moved enough to explain anything). **There are no position
columns at all**, so a CSV is redacted by construction whatever the checkbox
says.

**Redact position** (beside the logging button, on by default) applies to the
**full log** — the CSV has no position columns to redact. It replaces the
receiver's `Lat` / `Lon` / `Alt` with placeholders **in the saved file only** —
the Raw monitor and the plots keep showing your real fix. Satellite count, HDOP
and the TIME flag stay: they are diagnostic and say nothing about where you are.

A telemetry log is the thing that ends up on a forum or in the hands of whoever
offered to measure your board, and every second of it carries a fix to six
decimal places — about ten centimetres. Scrubbing it afterwards works but
depends on remembering, and the once it is forgotten is the once the file has
already been sent. The setting is fixed when the file opens and the box greys
out until logging stops, so a log is wholly redacted or wholly not; a file
redacted in parts reads as safe at a glance and is not. Either way the log says
which it is, on its second line.

**Generate gpsdo_tz_table.h** rebuilds the firmware's timezone table from this
machine's IANA data and writes `gpsdo_tz_table.h` next to the script. It replaces the
old `gen_tz_table.py`, so the tuner is now the only script to keep.

Zone data comes from the system database on Linux/macOS, or from the `tzdata`
Python package — which is how it works on Windows, where no IANA database ships
with the OS. If the button reports no data, run `pip install tzdata`; to refresh
it later, `pip install -U tzdata`. The generated header records which IANA
release it came from, where that can be established.

> IANA itself publishes *source* that needs the `zic` compiler, so downloading
> from them directly would not help — the `tzdata` package is the same data
> already compiled.

**The Raw monitor pane holds only the last ~2500 lines** (under five minutes at
the telemetry rate). That is a display limit, not a logging one: once logging is
started the file gets everything regardless of what the pane still shows.

**Resolution is the telemetry rate.** One sample per second, so anything faster
than about 2 s is invisible: a fast limit cycle or per-PPS jitter will not show
up, and what you see has already been averaged inside the firmware.

**One connection at a time.** The serial port is exclusive. Close any other
terminal on the same port first, and remember the tuner holds it while open.

**The plots trust the board.** Values are parsed from the telemetry text as
sent. If the firmware reports a stale or wrong figure, the tuner draws it
faithfully — it does not cross-check anything.

**Writes are not persistent.** Setting a parameter changes it in RAM only.
Loop-tuning parameters need an explicit `ES` (the reply names the exact
command); preferences save themselves and say so.

---

*Part of GPSDO FreeRTOS — [firmware manual](README_EN.md) · [changelog](CHANGELOG_EN.md) · [repository](https://github.com/jmnlabs/GPSDO_FreeRTOS)*
