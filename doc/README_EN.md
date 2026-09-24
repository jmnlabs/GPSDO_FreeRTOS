# GPSDO v1.07.57rt

**English** | [Polski](README_PL.md) | [Español](README_ES.md)

📖 [Project home](../README.md) · Manual: [MD](MANUAL_EN.md) · [PDF](MANUAL_EN.pdf)

Real-time (FreeRTOS) firmware for a GPS-Disciplined Oscillator (GPSDO)
on the STM32 BlackPill platform (WeAct F411CE / F401CCU6).

📋 Version history: [Changelog](CHANGELOG_EN.md)

## Credits

| Role | Person / source |
|------|-----------------|
| FreeRTOS port author, algorithms 3–11 and 13 | **J. M. Niewiński** — [repository](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Programming assistants | **Claude Opus 5 (Anthropic) · GLM-5.3 Max (Z.ai) · Qwen3.8-Max** |
| Measurement and field testing, algorithms 10–12 | **Dan Wiering** — rubidium-referenced ADEV runs that found the algorithm-10 limit cycle, settled the `FA` damping question, reported the ACQ lock-up, and established the algorithm-11 comparison; now shaking down algorithm 12 |
| ILI9486 / ILI9488 support — the push to implement it; and the PC tuner grew out of a monitoring script he assembled with Claude | **lucido** (EEVBlog forum) |
| v0.06c author — inspiration for the RTOS port | **André Balsa** — [repository](https://github.com/AndrewBCN/STM32-GPSDO) |
| Continuous-PI loop (algorithm 11) — original design | **Lars Walenius** (in memoriam) — GPSDO controller shared on the [time-nuts](http://www.leapsecond.com/time-nuts.htm) community and EEVBlog. Extended here with CT auto-calibration, a frequency-led acquisition branch and a picDIV phase-capture bridge. |
| Multi-level accumulator (algorithm 12) — original design | **Alan Cashin** (MIS42N, EEVBlog forum) — [profile](https://www.eevblog.com/forum/profile/?u=121386) — his Budget GPSDO is the origin of algorithm 12, the zero-crossing correction, the dithered PWM that reaches 24 bits from a short one, and the `CS` self-assessment idea. Implemented here on the LTIC phase detector, with per-level limits made editable because only the 128 s one was ever derived from a specification. |
| Kalman filter (algorithm 13) — original design | **J. M. Niewiński** — three states on a scalar measurement, so the textbook matrix inversion is one division and the whole filter costs about a microsecond a second on the M4F. Every constant it would otherwise need is derived from `CT` and `LC`, so it carries no number measured on one particular board. |
| PCB design (prototype) | **Scrachi** (EEVBlog forum) — [post with files](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [profile](https://www.eevblog.com/forum/profile/?u=762266) |
| Project thread | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — EEVBlog Forum |

This firmware was written from scratch as a port of André Balsa's original
code to the FreeRTOS architecture, with a complete redesign of tasks,
synchronisation, and display handling. The hardware design is based on the
v0.06c schematic, using PCBs shared by the user Scrachi on the EEVBlog forum.

---


> **PC tuning console:** see [README_TUNER_EN](README_TUNER_EN.md) for the
> desktop tuner — live plots, parameter tabs and the `gpsdo_tz_table.h` generator.

---

## Project overview

A GPSDO (GPS-Disciplined Oscillator) is a precision 10 MHz frequency source
in which an oven-controlled crystal oscillator (OCXO) is disciplined by the
1PPS signal from a GPS receiver. This achieves long-term accuracy on the order
of 10⁻¹⁰–10⁻¹², while preserving the OCXO's short-term stability.

### Hardware principle of operation

```
                                            10 MHz
               ┌─────────────┐       ┌──────────────┐
   GPS Antenna ┤  u-blox     │       │    OCXO      ├── TIM2 ETR (PA15) ──┐
               │ NEO-6M/8M   │       │  10 MHz      │                     │
               └──┬──────┬───┘       └──────▲───────┘                     │
                  │      │                  │                             │
        NMEA      │  1PPS (PB10)      PWM (PB9)                           │
     (Serial1)    │      │            + RC filter                         │
                  │      │                  │                             │
               ┌──▼──────▼──────────────────┴───────┐                     │
               │           STM32 F411CE             │◄────────────────────┘
               │           BlackPill                │
               └───┬─────────┬─────────┬───────┬────┘
                   │         │         │       │
                I2C bus    SPI1     Serial2  GPIO
                   │         │         │       │
        ┌──────┬───┼───┬─────┤         │    TM1637
        │      │   │   │     │         │    (clock,
      OLED   LCD  HT16K33  TFT        BT     PA8/PB4)
     128x64  20x4 (clock)  │         HC-06
        │              ┌───┴────────┐
     Sensors:          │ ILI9341 /  │  320x240
   ┌────┼────┐         │ ST7789     │
  AHT  BMP  INA        │ ILI9488    │  480x320
                       └────────────┘
```

The arrangement above is the same on both panels. It has not always been — the
two branches drifted apart field by field until v1.05, when the small panel was
brought back into line: qErr sits on the Alt row beside the fix data, the
environmental sensors share the left column and the electrical ones the right,
and Vcc and Vdd share the bottom sensor row.

**If you edit the small panel's layout, measure — do not count characters.**
Font 2 is proportional. `Vph:1.951V` is 70 px, not the 80 that ten characters at
8 px suggests, and the overstatement runs to about a fifth across a row. Two
fields had already been cut on the strength of that arithmetic and both fitted
once measured. `tft_text_w()` asks the library, which is what the 480 branch does
throughout; where a constant is used instead, the comment beside it gives the
measured width it came from.

Each field's padding is the width of its **widest** form, not of today's reading:
`dtostrf()`'s width argument is a minimum, so a value gains a character when it
crosses a power of ten, and a pad sized for the usual case lets that character
land on the neighbour. The paddings in a row tile it exactly — no gaps, no
overlap — so no background fill can erase an adjacent field's edge. On the small
panel the right-hand column's fields are all anchored to x=314.

**The control loop** operates as follows:

1. The OCXO generates a 10 MHz signal fed into TIM2 ETR (PA15).
   The 32-bit TIM2 counter counts OCXO cycles continuously.
2. The GPS 1PPS signal triggers a capture interrupt on TIM3 (PB10).
   The ISR reads the current TIM2 value — the difference between two
   consecutive captures gives the OCXO cycle count in exactly one GPS second.
3. Measurements are averaged over 10 s, 100 s, 1000 s and 10000 s windows
   using a circular ring buffer (20000 samples).
4. The control algorithm (PID, step, or hybrid) computes a PWM correction.
5. A 16-bit PWM DAC (PB9) controls the Vctl voltage applied to the OCXO's
   EFC input through a dual RC filter (20 kΩ / 10 µF, τ ≈ 200 ms).

**Sensors** (optional, I2C):

- **AHT10/20** — enclosure temperature and humidity
- **BMP280** — temperature and atmospheric pressure
- **INA219** — supply voltage and OCXO current draw

**Displays** (optional):

- **OLED 128×64** I2C (SH1106 / SSD1306 / SSD1309)
- **LCD 20×4** I2C (HD44780 + PCF8574T)
- **TM1637** (4- or 6-digit clock display)
- **TFT 320×240** SPI (ILI9341 / ST7789, TFT_eSPI library)
- **TFT 480×320** SPI (ILI9488, TFT_eSPI library) — field-tested against a
  rubidium reference; the 320×240 layout is auto-scaled up
- **HT16K33** 4-digit 7-segment clock with colon, I2C addr 0x70 (HH:MM)

OLED and LCD can operate simultaneously (different I2C addresses).
LCD and TM1637 **cannot** operate simultaneously (bus conflict).

---

---

## Software architecture

The firmware runs under FreeRTOS with seven tasks at strictly defined
priority levels:

| Priority | Task | Stack | Role |
|----------|------|-------|------|
| Highest | `vFreqRelayTask` | 768 B | PPS processing, frequency ring buffer |
| High | `vControlTask` | 1.5 KB | OCXO warmup, calibration, PID algorithm, ADC |
| Medium-high | `vGpsTask` | 1.5 KB | NMEA parsing (TinyGPS++), UBX configuration |
| Medium | `vCliTask` | 1 KB | Serial / Bluetooth command parser |
| Medium-low | `vSensorTask` | 1.5 KB | AHT/BMP/INA readout every 2 s |
| Low | `vDisplayTask` | 4 KB | OLED, LCD, TM1637, serial report, LEDs |
| Lowest | `vUptimeTask` | 768 B | Uptime clock during holdover only — the PPS drives it otherwise |

**Shared state** is protected by FreeRTOS mutexes:

- `xFreqMutex` — frequency data (`gFreq`, `gFreqSnap`)
- `xGpsMutex` — GPS data (`gGps`)
- `xCtrlMutex` — control data (`gCtrl`: PWM, algorithm, holdover, trend)
- `xWireMutex` — I2C bus (shared by sensors and displays)
- `xSerialMutex` — serial / Bluetooth port

---

---

## Control algorithms

Eleven algorithms selectable via the `LA n` (0–10) command:

| Algo | Type | Input | Period | Description |
|------|------|-------|--------|-------------|
| 0 | Step | avg100/1k | ~429 s | Default — simple, robust |
| 1 | Drift | — | 1000 s | OCXO drift measurement only |
| 2 | Random | — | 5 s | Noise floor measurement — diagnostic |
| 3 | FLL PID | avg100 | 100 s | General purpose, conservative |
| 4 | PLL PI+D | true phase | 10 s | Low noise; Kd = frequency damping (required) |
| 5 | PLL PID | true phase | 10 s | Balanced: speed + noise |
| 6 | FLL PID (GA) | avg100 | 100 s | Genetically optimised coefficients |
| 7 | PLL PID (GA) | true phase | 10 s | Genetically optimised coefficients |
| 8 | Hybrid | FLL+PLL | 100 s | Automatic FLL↔PLL sigmoid blend |
| 9 | Neural net | e/∫e/de + temp | 10 s | 5-input MLP; learns oscillator tempco, thermally compensated holdover |
| 10 | LTIC | TIC phase + freq | staged | Three-stage ACQ→DPLL→LOCK; hardware phase detector, self-calibrating |
| 11 | LTIC-Lars | TIC phase | continuous | Single continuous PI, no state machine; gain auto-derived from `CT`. After Lars Walenius |
| 12 | Multi-level accumulator | LTIC phase [ns] | adaptive | No time constant to set: the error picks its own averaging span. After Alan Cashin (MIS42N). **Untuned.** |
| 13 | Kalman filter | LTIC phase [ns] + TIM2 frequency | adaptive | Three states — phase, frequency, aging — with covariances: the weight given to each reading comes from the measured variances rather than from a constant, and every scale is derived from `CT` and `LC`. Original to this project. |

PLL algorithms (4, 5, 7 and the PLL branch of 8) use a **two-timescale**
design tuned for "fast capture, gentle phase-hold":

- the dominant term acts on the **frequency error** (Kp ≈ 0.4/K), pulling
  the frequency to target quickly and without overshoot;
- small phase terms (Kd proportional, Ki integral on accumulated phase)
  remove slow drift with tiny steps.

Every correction passes through a shared output stage that applies a
**slew-rate limit** (max ~12 LSB/step for the PLLs, 40 for the hybrid) and a
**dead-zone** near lock. The slew limit spreads a large overnight phase
drift over several periods instead of one big PWM jump that would disturb
the OCXO; the dead-zone lets the PWM sit still in steady state so the OCXO
free-runs on its own short-term stability.

Algorithms 3–9 have runtime-tunable PID parameters (`Kp`, `Ki`, `Kd`,
`I_LIMIT`) configurable via CLI commands (`KP`, `KI`, `KD`, `IL`) —
no recompilation needed. Parameters are persisted to the flash ring with `ES`.

---

---

## OLED display layout (128×64 px, 16 chars × 8 rows)

For 2 seconds after boot, row 0 shows the firmware version.
Then it switches to a local-time clock. Two pages alternate every
`OLED_PAGE_SWITCH_SECS` seconds (default 10 s):

```
── Row 0 (shared): LMT:14:32:45 Mon  ← local time + day of week
── Row 1 (shared): F 9999999.9999Hz   ← frequency + Hz at positions 14-15
──── PAGE A (GPS) ───────────────────────────────────────────
Row 2: La  52.12345             ← latitude
Row 3: Lo  23.12345             ← longitude
Row 4: Al  175m Sat: 9          ← altitude + satellites
Row 5: Up 000d 00:00:00         ← uptime
Row 6: 12:34:56  23.4C          ← UTC + AHT temperature
──── PAGE B (sensors) ───────────────────────────────────────
Row 2: BM:23.4C 1013hPa        ← BMP280
Row 3: AH:22.1C 45.3%rH        ← AHT20
Row 4: IN:12.05V  250mA        ← INA219
Row 5: Sat:09 HDOP:0.90        ← GPS quality
Row 6: UTC:14:32:45 Mon        ← UTC time + day
──── Both pages ─────────────────────────────────────────────
Row 7: PWM:40908 hit H          ← PWM + trend + holdover (H/A blink)
```

Holdover indication on row 7: `H` (manual) or `A` (automatic — fix lost).

---

---

## LCD 20×4 display layout

Version splash for 2 seconds, then:

```
Line 0: F:  10000000.0000 Hz     ← frequency (20 chars, right-justified)
Line 1: UTC:14:32:45 Up 000d     ← UTC time + uptime days
Line 2: [rotating view]          ← see table below
Line 3: PWM:40908 V:1.65 hit     ← PWM + Vctl + trend / holdover
```

Line 2 rotates every `LCD_LINE2_SWITCH_SECS` seconds:

| Mode | Content | Example |
|------|---------|---------|
| 0 | GPS coordinates + sats | `La:52.123 Lo:23.123 S: 9` |
| 1 | Satellites + HDOP | `Sats: 9  HDOP:0.90` |
| 2 | Date + day + local time | `02/06/2026 Mon 14:32` |
| 3 | AHT20 | `AHT:22.1C  45.3%rH` |
| 4 | INA219 | `INA:12.05V   250mA` |
| 5 | BMP280 | `BMP:23.4C 1013.2hPa` |

Holdover on line 3: `[H]` (manual) or `[A]` (automatic) — 500 ms blink.

---

---

## TFT display layout (ILI9341 / ST7789 320×240, ILI9488 480×320, TFT_eSPI)

Cheap SPI TFT modules are supported in landscape orientation, driven over
hardware SPI1: **ILI9341** and **ST7789** at 320×240, and **ILI9488** at
480×320. All three share the same `User_Setup.h` wiring — switching panels
requires only changing the driver define and the width/height. The
`TFT_RGB_ORDER` / `TFT_INVERSION_OFF` lines are needed for correct colours on
ST7789 modules and are harmless on the others. Independent of the I2C
displays — OLED, LCD and TFT can all run simultaneously.

### The two panel sizes

The operating screen is authored once, at 320×240, and scaled to 480×320 at
compile time through `TFT_SX` / `TFT_SY` / `TFT_F`. The arrangement of fields is
therefore identical on both panels — nothing moves relative to anything else.
What differs is how the text is rendered, and that difference is large enough to
matter.

```
 ┌──────────────────────────────────────────────────┐
 │ GPSDO v1.07.57rt                LMT 14:32:45 Mon │   header: version + local time
 │                                                  │
 │          1 0 0 0 0 0 0 0 . 0 0 0 0  H z          │   frequency, large font
 │                                                  │
 │ UTC: 12:32:45 Mon        Sat: 11 HDOP: 0.77      │
 │ DATE: 28/07/2026         Lat:  52.229676         │
 │ Uptime: 000d 01:42:12    Lon:  21.012229         │
 │ Algo: 12 LOCK            Alt:118m     qEr:-4.2ns │   algorithm + trend  |  fix data + qErr
 │ PWM:40849 Vct:1.799V     INA:4.920V     182.00mA │   control output     |  supply monitor
 │ BMP: 51.1C 1006.7hPa     Vph:2.083V dp:     -5ns │   sensors            |  phase detector
 │ AHT: 24.60C 41.20%rH     Vcc:5.012V    Vdd:3.29V │                      |  supply rails
 │                                                  │
 │               DISCIPLINED  FIX OK                │   status bar (colour = state)
 └──────────────────────────────────────────────────┘
```

**320×240 (ILI9341, ST7789)** uses the classic GLCD numeric fonts for the
operating screen. They are already the right size at 1:1, they render fast, and
they need no `LOAD_GFXFF` in `User_Setup.h`. See *Why the small panel keeps the
classic fonts* below for the reasoning.

**480×320 (ILI9488, ILI9486)** uses Adafruit GFX free fonts through a per-role
abstraction (`GF_DATA`, `GF_HEAD`, `GF_STATUS` and so on in `gpsdo_config.h`).
Scaling the classic bitmap fonts by 1.5 would have produced visibly blocky
digits; the free fonts stay clean at the larger size. This build **does** need
`LOAD_GFXFF`.

The larger panel also moves 2.4x the pixels per redraw, so `SPI_FREQUENCY` should
not be reduced below 40 MHz on it — see the wiring notes below.

> **ILI9488 / ILI9486 480×320 support is verified on-panel (v0.93).** The
> 320×240 operating screen and splash are auto-scaled to 480×320 at compile time
> (width ×1.5, height ×1.33). The big panel draws all of its text with the
> Adafruit GFX free fonts, which fixed the symptoms seen in early adopters'
> photos (Dan Wiering, lucido) — the splash subtitle collapsing to a lone "p"
> and the status bar appearing blank were both the numeric GLCD fonts lacking
> letters, not a scaling problem. The band geometry (freq, grid, sensors,
> status) was recomputed against the taller free-font rows and checked on a live
> ILI9488 panel, so no row crosses a separator on either size.
>
> ILI9488/ILI9486 over SPI moves 2.4x the pixels of a 320×240 panel at 18-bit
> colour, which used to make every repaint visible. Since v0.93 the live regions
> are double-buffered as sprites and pushed in one transfer each, so the redraw
> no longer flickers — see [Sprites: why the display stopped
> flickering](#sprites-why-the-display-stopped-flickering). Run SPI at 40 MHz on
> this panel.
>
> The 320×240 panels keep the classic numeric fonts for the operating screen —
> the GFX faces are too wide for that layout. See [Why the small panel keeps the
> classic fonts](#why-the-small-panel-keeps-the-classic-fonts).

**Wiring (hardware SPI1):**

| TFT pin | STM32 pin |
|---------|-----------|
| SCK | PA5 (SPI1 SCLK) |
| SDI | PA7 (SPI1 MOSI) |
| RES | PB15 |
| D/C | PB12 |
| CS | PB13 |

**Screen layout:**

```
┌────────────────────────────────────────────┐
│ v1.03-rt      GPSDO      LMT 14:32:45 Thu   │ ← header bar (navy)
├────────────────────────────────────────────┤
│                                            │
│        10000000.0000 Hz                    │ ← frequency (large, colour-coded)
│                                            │
├────────────────────────────────────────────┤
│ UTC: 12:32:45 Thu    │ Sat:  9 HDOP: 0.90  │
│ DATE: 11/06/2026     │ Lat: 52.123456      │
│ Uptime: 000d 02:15:33│ Lon: 23.123456      │
│ Algo: 5 hit          │ Alt:  175m          │
│ PWM:44653 Vct:1.970V │ INA: 12.050V 250mA  │
├──────────────────────┼─────────────────────┤
│ BMP: 23.40C 1013.2hPa│ AHT: 22.10C 45.3%rH │
│ Vph: 2.615V +830ns   │ Vdd: 3.30V          │
├────────────────────────────────────────────┤
│          DISCIPLINED  FIX OK               │ ← status bar (colour-coded)
└────────────────────────────────────────────┘
```

**Colour coding:**

| Element | Colour | Meaning |
|---------|--------|---------|
| Frequency | green | locked — best average within 1e-10 (10000s) or 1e-9 (1000s) of 10 MHz |
| Frequency | white | adjusting |
| Frequency | orange | holdover |
| Frequency | red | no signal |
| Status bar | green | disciplined, fix OK |
| Status bar | orange | manual holdover |
| Status bar | red | auto-holdover (fix lost) / waiting for fix |

Updates are selective — each value cell caches its previous string and is
redrawn only on change, keeping SPI traffic minimal at the 1 Hz refresh.

**TFT_eSPI library configuration (required):**

TFT_eSPI is configured in the *library*, not the sketch. Edit
`Arduino/libraries/TFT_eSPI/User_Setup.h` to contain:

```c
#define ST7789_DRIVER          // or ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO PA6      // required on STM32 even if display has no MISO
#define TFT_MOSI PA7
#define TFT_SCLK PA5
#define TFT_CS   PB13
#define TFT_DC   PB12
#define TFT_RST  PB15
#define TFT_RGB_ORDER TFT_BGR   // colour order Blue-Green-Red
#define TFT_INVERSION_OFF       // fixes inverted colours on some ST7789 modules
#define LOAD_GLCD               // classic font 1 — frequency readout + splash credits
#define LOAD_FONT2              // classic font 2 — header + data grid
#define LOAD_FONT4              // classic font 4 — status bar, busy messages, splash subtitle
#define SPI_FREQUENCY 40000000  // F411 SPI1 tops out at 50 MHz; 40 leaves headroom
```

> **The 320×240 build needs no `LOAD_GFXFF`.** Everything on this panel —
> including the splash — is drawn with the classic numeric fonts, so the three
> `LOAD_` lines above are the whole story. This is deliberate: see [Why the
> small panel keeps the classic fonts](#why-the-small-panel-keeps-the-classic-fonts).
> If you are upgrading from an older build, your existing `User_Setup.h` almost
> certainly already has these.

For the **ILI9488 / ILI9486 (480×320)** panel, change the driver and dimensions,
and add `LOAD_GFXFF` — the big panel draws its header, grid, status bar and
frequency with the Adafruit GFX free fonts. The firmware picks the point sizes
automatically at compile time (see the `GF_*` macros in `gpsdo_config.h`):

```c
#define ILI9488_DRIVER          // works for both ILI9488 and ILI9486 panels
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
// ...same TFT_MISO/MOSI/SCLK/CS/DC/RST/RGB_ORDER lines as above...
#define LOAD_GLCD               // font 1 — splash credits
#define LOAD_FONT4              // font 4 — retained for the splash subtitle path
#define LOAD_GFXFF              // REQUIRED on this panel — GFX free fonts
#define SPI_FREQUENCY 40000000  // 480×320 pushes 2.4x the pixels — don't skimp here
```

> **SPI clock.** 40 MHz is the tested setting on the F411 (SPI1 peaks at
> 50 MHz, so this leaves headroom for wiring that isn't ideal). It matters most
> on the 480×320 panel: a full-screen redraw moves 2.4x the pixels of the small
> panel, and the sprite pushes (below) are single continuous transfers whose
> duration scales directly with the clock. If your panel shows artefacts, drop
> to 27 MHz — long jumper leads may not survive 40.

**Troubleshooting:** if the firmware freezes at the OLED version splash after
enabling TFT, check the serial output. The message `TFT: init start ...` is
printed immediately before `TFT_eSPI::init()` — if it is the last line, the
hang is inside the library: verify that `User_Setup.h` contains exactly the
pins above (including `TFT_MISO PA6`) and the correct driver define. The
DisplayTask stack is automatically raised to 768 words when `GPSDO_TFT` is
enabled — if you modified stack sizes manually, restore that value.

Then enable `GPSDO_TFT_ST7789`, `GPSDO_TFT_ILI9341` or `GPSDO_TFT_ILI9488` in `gpsdo_config.h`.

### Why the small panel keeps the classic fonts

v0.92 moved every screen to the Adafruit GFX free fonts. On the 480×320 panel
that was a clear win: the lettering is properly shaped, the layout has room to
breathe, and `FreeMonoBold` keeps the frequency digits in fixed columns.

On 320×240 the same change was tried and **reverted in v0.93**. The GFX faces
are proportional and noticeably wider than the numeric fonts the layout was
authored against, and 320 px simply has no room for the difference: values ran
past their columns into the neighbouring one (`Uptime: 000d 00:01:03n: ---`,
`PWM:44778 Vct:1.9INA: 4.888V 224.5`), and the centre divider cut through the
overflowing text. Shrinking the font wasn't an option either — 9 pt is the
*smallest* FreeSans that TFT_eSPI ships, and the only thing below it is
TomThumb (3×5 px), which is unreadable at arm's length.

So the small panel keeps what fits: classic font 2 for the header and grid,
font 4 for the status bar, font 1 at ×3 (18×24, fixed-width) for the frequency.
The splash follows suit — its subtitle uses font 4, which carries the full
alphabet (fonts 6/8 are the letterless ones that once turned "GPS Disciplined
OCXO" into a lone "p"), and the credits use font 1. That was the last GFX
holdout, and dropping it means **a 320×240 build needs no `LOAD_GFXFF` at
all** — anyone upgrading from an older version can leave `User_Setup.h` alone.
The `TFT_FONT_*` macros in `gpsdo_config.h` make the choice at compile time;
there is one layout, not two.

The **centre column divider** is likewise 480-only: at 320 px the columns run
right up to the middle and the line had nowhere to go that wasn't through text.

### Sprites: why the display stopped flickering

The panel is written over SPI, so anything drawn straight to it is *seen* being
drawn. The old code erased before it wrote: `setTextPadding` filled roughly
480×34 px of background, then the new text landed on top. At one update per
second that erase-then-draw cycle was plainly visible as a flicker across the
frequency band — worse on the 480×320 panel, where the erase covers 2.4x the
pixels.

v0.93 buffers the three live regions in RAM instead — header, frequency band and
data area — as `TFT_eSprite` objects. Each redraw clears and repaints its
sprite invisibly in RAM, then pushes the finished band to the panel in **one
continuous SPI transfer**. There is no intermediate state on the glass, so
there is nothing to flicker. The frame and separators sit outside the sprite
bounds (or, where a separator crosses a band, are drawn into the sprite and
pushed with it), so they are never touched.

Memory is modest given the palettes — 4-bit for the header and frequency bands,
1-bit for the data area, ~25 KB total on the 480 panel, comfortably inside the
F411's 128 KB. The frame rides along inside the sprites rather than being drawn
on the panel: this is why it is white on both sizes. The data sprite is 1-bit,
so its only two colours are white and the background — a navy frame could not
be drawn into it, and would have had to be repainted on the panel after every
push, defeating the point. White keeps frame and text in the same atomic
transfer.

If `createSprite()` ever fails (fragmented heap), each band falls back to
drawing directly to the panel: you get the old flicker, but nothing breaks. The
boot log says which path is live:

```
TFT: freq-band sprite (4-bit) created
TFT: header sprite (4-bit) created
TFT: data sprite (1-bit) created
```

---

### Colour TFT support (TFT_eSPI)

Any display supported by the TFT_eSPI library at **320×240** or **480×320**
should work — the UI scales via `TFT_SX()/TFT_SY()`. Tested: ILI9341
(320×240), ST7789 (240×320), ILI9488 (480×320). To add yours:

1. enable the matching `GPSDO_TFT_*` in `gpsdo_config.h`,
2. set the driver and pins in TFT_eSPI `User_Setup.h` (SPI1: SCK=PA5,
   MOSI=PA7; CS/DC/RST as in `gpsdo_config.h`),
3. a different controller just needs selecting in `User_Setup.h` — any panel
   reporting 320×240 or 480×320 fits without code changes.

---

---

## Clock displays (HT16K33 and TM1637)

Two small 7-segment clock modules are supported. Both show local time, follow the
`LT` setting for UTC versus local, and blink the colon so a stopped display is
obvious at a glance. Neither shows any GPSDO state — they exist to make the unit
useful as a clock while it disciplines.

### HT16K33 — 4 digits, I2C

```
 ┌────────────────┐
 │  1 4 : 3 2     │   HH:MM, colon blinks once per second
 └────────────────┘
```

I2C address 0x70, shares the bus with the sensors and the OLED. Enabled with
`GPSDO_HT16K33`. Reported at boot as `HW: HT16K33 clock display OK (I2C 0x70)`
so a missing or mis-addressed module is caught immediately.

### TM1637 — 4 or 6 digits, 2-wire

```
 ┌────────────────┐
 │  1 4 : 3 2     │   4-digit build: HH:MM
 └────────────────┘
 ┌────────────────────┐
 │  1 4 : 3 2 : 4 5   │   6-digit build: HH:MM:SS
 └────────────────────┘
```

Uses its own CLK/DIO pins rather than I2C. `GPSDO_TM1637` selects the 4-digit
variant, `GPSDO_TM1637_6` the 6-digit one. The colon blinks on even seconds.

> **TM1637 and the 20×4 LCD cannot be used together** — they contend for the same
> pins. Pick one at compile time.

---

## LED signalling

| LED | Pin | Function |
|-----|-----|----------|
| Blue (built-in) | PC13 | Blinks every 1PPS — heartbeat |
| Yellow | PB8 | See state table below |

**Yellow LED state machine:**

| State | Condition | Signalling |
|-------|-----------|-----------|
| No GPS fix | After boot or persistent no signal | OFF |
| Fix OK, disciplined mode | Normal operation | ON steady |
| Manual holdover (`MH`) | User enabled holdover | Slow pulse 1000 ms |
| Auto-holdover | Fix lost during operation | Fast pulse 200 ms |

---

---

## picDIV synchronization

The optional picDIV (PD11/PD13/PD17 family by Tom Van Baak, leapsecond.com)
divides the OCXO 10 MHz down to a clean 1PPS output with <2 ps jitter.
The STM32 controls its Arm pin (PB3); the GPS 1PPS drives its Sync pin
directly in hardware.

**Arm sequence** (`AP` command):

1. STM32 pulls Arm LOW — divider output stops
2. Arm held LOW for 1.0–1.2 s (spec requires >1 s)
3. STM32 releases Arm (HIGH)
4. Divider restarts synchronized to the next GPS 1PPS rising edge

Arming is refused (deferred) when there is no GPS fix — without a 1PPS
edge on Sync the divider would stay stopped with a dead output.

**Long-term synchronization — important:**

The picDIV output is phase-coherent with the **OCXO**, not with GPS.
What happens after arming depends on the active control algorithm:

| Algorithm type | Frequency | Phase | picDIV behaviour |
|---------------|-----------|-------|-----------------|
| FLL (0, 3, 6, 8*) | bounded | random walk | 1PPS slowly drifts from GPS |
| PLL (4, 5, 7) | bounded | bounded | 1PPS stays aligned with GPS |

*Algorithm 8 behaves as FLL for large errors, PLL near lock.

An FLL only zeroes the average frequency error; each small residual
integrates into phase, so the picDIV 1PPS performs a random walk relative
to GPS (typically µs/day at 1e-11 average error). If long-term 1PPS
alignment matters, run a PLL algorithm (`LA 4`, `LA 5` or `LA 7`) or
re-arm (`AP`) periodically. Arm only after the loop reports lock
(trend `hit`) — arming during convergence starts the phase drift
immediately.

---

---

## Automatic holdover

When GPS loses fix during normal operation (e.g. antenna disconnected):

1. `vControlTask` detects `pos_valid` transition from `true` to `false`
2. Automatically sets `holdover_mode=true`, `holdover_auto=true`
3. PWM is frozen at the last value — OCXO runs free
4. Yellow LED pulses fast (200 ms), displays show `A` (blink)
5. On fix recovery: auto-holdover cleared, returns to steady ON

Manual command `MH` sets holdover independently (indicated as `H`).
`MD` disables holdover (both manual and automatic).

---

---

## CLI commands (Serial / Bluetooth)

Connection: 115200 Bd (USB) or 57600 Bd (Bluetooth HC-06, `GPSDO_BLUETOOTH`).
Commands terminated by `\r\n` or `\n`. Command names are **case-insensitive**
(`LA`, `la` and `La` are equivalent), so any letter case works.

### General

| Command | Description |
|---------|-------------|
| `H` | Display help |
| `V` | Version, authors and GitHub links |
| `F` | Flush frequency ring buffers (restart averaging) |
| `C` | Start auto-calibration (PWM centring only) |
| `CT` | Calibrate + auto-tune: measure K, derive PID for algos 3-9 **and both LTIC loops (10 and 11)**; auto-saves the PID group on success |
| `T [baud]` | GPS tunnel on USB for u-center — clean bidirectional NMEA/UBX (telemetry moves to Bluetooth if present, else muted); optional GPS-UART baud, kept after exit; exits after 300 s |
| `SP <n>` | Set PWM DAC directly (1–65535), bypasses algorithm |
| `DAC` | Control-voltage output: path, 24/16-bit/exact code, measured Vctl, step size in µHz and df/f (needs `CT` for the Hz figures) |
| `SPAN [CLR FULL\|REDUCED]` | EFC span jumper on PB14 (`GPSDO_SPAN_SENSE`): the position it is in and the `CT` calibration each position carries — one per span, switched and remapped when the jumper moves; `CLR` forgets one |
| `RH` | Report mode: human-readable (default) |
| `RD` | Report mode: tab-delimited |
| `RP` | Pause serial/BT data stream |
| `RR` | Resume serial/BT data stream |
| `SW` | FreeRTOS task stack watermarks, heap free, uptime source and the measured MCU clock error against GPS (diagnostics) |
| `CS` | Correction statistics: how hard the loop is working, and in df/f after `CT` |

### Control

| Command | Description |
|---------|-------------|
| `MH` | Enable holdover mode (manual) |
| `MD` | Enable disciplined mode |
| `LA [0-12]` | Select / show control algorithm |
| `AP` | Arm picDIV — stops output 1.0–1.2 s, resyncs to GPS 1PPS |

### Algorithm tuning

| Command | Description |
|---------|-------------|
| `LP [n]` | List PID parameters for algo `n` (or current) |
| `KP n val` | Set Kp for algo `n` (3–7) |
| `KI n val` | Set Ki for algo `n` (3–7) |
| `KD n val` | Set Kd for algo `n` (3–7) |
| `IL n val` | Set I_LIMIT for algo `n` (3–9) |
| `BC [val]` | Algo 8 blend crossover (Hz) |
| `BS [val]` | Algo 8 blend sigmoid width (Hz) |
| `NS [val]` | Algo 9 NN max step (LSB) |

### Configuration

| Command | Description |
|---------|-------------|
| `TO [n]` | Show / set a fixed offset — hours or `h:mm` (`TO 9:30`, `TO -5`) |
| `TO A` | Auto timezone: zone from GPS position + EU DST rule (Europe only) |
| `TZ [zone]` | Timezone with DST — `TZ Adelaide`, or a POSIX rule. See `H TZ` |
| `PO [f]` | Show / set pressure offset |
| `AO [f]` | Show / set altitude offset |
| `SV [0\|1]` | Survey-in / Time Mode on timing receiver (saved by `ES`, applied at next boot) |

### Timezones

`TZ <city>` is usually all it takes:

```
TZ Adelaide          → UTC+9:30, and UTC+10:30 when DST is active
TZ Warsaw            → UTC+1 / UTC+2
TZ Kolkata           → UTC+5:30, no DST
```

City names are unique across the whole IANA database, so the region is
optional — `TZ Australia/Adelaide` works too — and case is ignored. 503 zones
are built in.

The rule can also be given in full, which matters if a government changes the
rules before this firmware catches up:

```
TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3
```

`H TZ` explains the format. `ES` saves the setting.

**Why not the IANA database?** It is ~2 MB — four times this MCU's entire
flash — and its value lies in being updated several times a year, which a GPSDO
with no internet cannot benefit from. The POSIX TZ string each zone reduces to
is 4–44 bytes and carries the same present-day behaviour: standard offset, DST
offset, and both transitions. Southern-hemisphere zones need no special case —
a start month after the end month simply means DST wraps through New Year.

`TO A` (auto from GPS position) is still available and unchanged. It is right
across most of Europe and needs no configuration, but it knows nothing of DST
elsewhere and returns whole hours only — so it cannot express Adelaide's
+9:30, or India's +5:30. Use `TZ` outside Europe.


### Saving and restart

Storage is the flash ring described under *Settings storage* below — there is no
EEPROM. Preferences save themselves; loop tuning needs an explicit `ES`, and the
reply always says which applied.

| Command | Effect |
|---------|--------|
| `ES [group]` | Save all settings, or one group: `TZ` / `PID` / `LTIC` / `FLAGS` / `ALGO` / `PO` |
| `ER` | Recall settings from the ring |
| `EE` | Erase settings (restore defaults) |
| `EW` | Flash wear: erase cycles and slots used |
| `RB` | Warm reboot — settings kept, OCXO stays warm, disciplined state recalled |
| `CR YES` | Cold restart — wipe the ring, then reset (requires the `YES` confirmation) |

### LTIC — algorithm 10 (three-stage ACQ/DPLL/LOCK)

Algorithm 10 disciplines the OCXO from the hardware TIC phase (PA1), which
resolves phase far finer than the TIM2 cycle counter. It is a hybrid design:
the coarse stages lean on the robust TIM2 **frequency** error (no wrap
ambiguity), the fine stages lean on the high-resolution TIC **phase**. A
three-state machine walks the loop from cold start to tight lock:

| Stage | Leads on | What it does | Exits when |
|-------|----------|--------------|------------|
| **ACQ** | frequency (TIM2) | Frequency-led pull-in — get the OCXO close to 10 MHz so the phase ramps slowly enough to catch. picDIV is armed on entry. | \|phase\| stays inside `acq_threshold` for a few cycles |
| **DPLL** | freq + phase | Both terms: `Kp·e_freq` (fast, TIM2) plus a phase PI (LTIC). Centres the phase quickly. | \|phase\| small **and** drift low (below `dpll_threshold`) |
| **LOCK** | phase (LTIC) | Phase-led, slow narrow-band updates every `lock_interval` s. | falls back to DPLL if \|phase\| leaves a hysteresis band persistently |

Phase comes from `g_ltic_voltage`. When calibrated (`ns_per_volt ≠ 0`) the
loop works in nanoseconds against `zero_offset`; uncalibrated, it falls back
to a volts-based error around mid-rail with a one-time warning. Crucially the
detector's working band may sit well away from mid-ADC (e.g. 0–0.45 V), so the
loop never assumes 1.65 V is centre — it uses the calibrated `zero_offset`.
State persists in the flash ring, so a warm reboot (`RB`) resumes where it left off
rather than restarting cold from ACQ.

Select it with `LA 10`; the picDIV arms automatically on ACQ entry. Run `LC`
first to calibrate (the loop falls back to a nominal volt-based phase with a
warning if you do not). `LC` can be run at any time — it suppresses the
discipline loop for the duration of its sweep, so it works even while
algorithm 10 is already locked. A passing `LC` **auto-saves** its result
(ns/V, zero-offset, range) to the flash ring as live data; you do **not** need
to run `ES` afterwards. A detector that does not wrap within the sweep still
passes, as long as slope, centre and span are sane; only a genuinely weak
result is refused, with the reason. The other commands below set/show
parameters, which `ES` saves.

| Command | Description |
|---------|-------------|
| `LC` | **Self-calibrate** ns/V (local slope), zero-offset (anchored ~1.85 V) and range (auto, ~7 min; prints per-second `t=/V=/n=` diagnostics) |
| `LL` | List all LTIC parameters + current state |
| `LNV [v]` | Calibration: ns per volt (TIC voltage→time slope) |
| `LZO [v]` | Calibration: TIC volts at zero phase difference |
| `LRN [v]` | Detector unambiguous range (ns, for wrap-around) |
| `AQP/AQI/AQD/AQL [v]` | ACQ-stage PID: Kp / Ki / Kd / I_LIMIT |
| `DPP/DPI/DPD/DPL [v]` | DPLL-stage PID: Kp / Ki / Kd / I_LIMIT |
| `LKP/LKI/LKD/LKL [v]` | LOCK-stage PID: Kp / Ki / Kd / I_LIMIT |
| `LAT [v]` | ACQ→DPLL threshold (phase in range, ns) |
| `LDT [v]` | DPLL→LOCK threshold (frequency error) |
| `LIV [v]` | LOCK update interval (seconds, default 300, 1..600 s) |
| `LPOL [-1/0/1]` | Phase-detector polarity (0 = auto) |
| `LCV` | Show the current TIC voltage (calibration aid) |

### Calibration order: `CT` first, then `LC`

**Run `CT` before `LC`.** The two are not independent: `LC` sweeps the PWM to hit
a target phase rate, and to know how far to steer it needs K — the Hz-per-LSB
slope of your OCXO, which is exactly what `CT` measures. Run without it, `LC`
falls back to a generic 3000 LSB/Hz and the calibration comes out scaled by
however far your oscillator differs from that guess.

The trap is that the wrong answer does not look wrong. On one board, `LC` before
`CT` reported ns_per_volt = 1592.8 and a WEAK result; after `CT` the same board
gave 921.2 and PASSED — a factor of 1.7, with nothing in the first run to suggest
it was suspect.

So the order for a new board is:

1. `CT` — measures K, derives the PID sets, auto-saves them
2. `LC` — calibrates the phase detector, auto-saves on PASS
3. `LPOL -1` or `LPOL 1` if the loop reports the polarity is unset, then `ES LTIC`
4. `LA 10` or `LA 11`, then `ES ALGO`

`LC` warns if `CT` has not been run, but continues anyway — a re-run costs three
minutes and there are legitimate reasons to sweep first.

### Algorithm 11 — LTIC-Lars (continuous PI)

A single continuous PI loop with no ACQ/DPLL/LOCK state machine, after the
original GPSDO controller by the late Lars Walenius. It shares the algo-10
detector calibration (`LC`), so calibrate once and both loops benefit. Trend
labels are the same vocabulary as algorithm 10: **ACQ** (frequency-led, phase
detector blind), **PLL** (phase-led) and **LOCK**.

| Command | Description |
|---------|-------------|
| `LG [v]` | Gain. **0 = auto**, derived from the `CT` calibration; non-zero sets a manual scale |
| `LD [v]` | Damping |
| `LTC [s]` | Loop time constant, 1..600 s |
| `LFD [n]` | Filter divisor — the pre-filter constant is `LTC / n` |
| `LTO [adc]` | TIC offset: phase target in ADC counts |
| `LPL [ns]` | Lock phase limit — width of the lock window |
| `LPF [n]` | Lock factor: the window must hold for `LPF × LTC` seconds |
| `LTK [v]` | Temperature coefficient feed-forward (0 = off) |
| `LTR [adc]` | Temperature reference, ADC counts |

None of these auto-save; `ES LTIC` stores them together with the algorithm-10
parameters.

### Damping average window — `FA` / `FAD` / `FAL`

| Command | Description |
|---------|-------------|
| `FA [n]` | Set the damping-term averaging window in **both** LTIC stages (10/100/1000 s) |
| `FAD [n]` | DPLL stage only |
| `FAL [n]` | LOCK stage only |

100 is the historical value and changes nothing. A shorter window is the
candidate fix for a limit cycle whose period is a couple of times the averaging
length — the group delay of a long average can land near quadrature with the
loop's own response.


#### Sawtooth (qErr) correction — `SAW 0|1`

A u-blox timing receiver generates its 1PPS by dividing an internal
oscillator, so each pulse lands on a clock edge — up to one clock period away
from true GPS time. This per-pulse quantization error is the dominant
short-term phase term on older receivers (LEA-6T granularity is 21 ns). The
receiver reports it in advance as `qErr` in the UBX-TIM-TP message.

The firmware enables TIM-TP automatically at GPS init and a passive sniffer
parses `qErr` from the same byte stream the NMEA parser reads. With `SAW 1`
the TIC phase path subtracts it, so the loop disciplines against the OCXO's
own error instead of chasing the receiver's granularity sawtooth. `qErr` is a
signed 32-bit picosecond field at the same payload offset on **LEA-6T,
LEA/NEO-M8T and ZED-F9T**, so one parser covers all three. The correction ages
out if TIM-TP stops (receiver reset) so a stale value is never applied.

`SAW` with no argument shows the state and live qErr; `SAW 1`/`SAW 0` toggles
it (saved with `ES`, default off). When on, the `Learn:` telemetry line shows
`qErr=…ns` for algorithm 10, and the value is subtracted from each TIC phase
reading. Because Vphase is sampled on the ramp peak right after the PPS edge
(see the TIC hardware notes below), each phase reading already pairs with the
qErr reported for that same second's pulse.

---

---

## TIC hardware notes — Kaashoek's gated ramp integrator

The phase-detector front end is **Erik Kaashoek's 1 ns TIC** (as used by
André Balsa's STM32 GPSDO, schematic rev 0.4), and understanding exactly how
it works cost real bench time — three flip-flops (two 74HC74 at 5 V, finally a
74LVC74 at 3.3 V), a wrong filter value, and a long detour through two
incorrect detector models. It is written down here so the next person does not
repeat it.

### How it actually works (confirmed on the scope)

A **74-type D flip-flop pair** (the `xx74`) turns the phase difference between
two 1PPS edges into a pulse: **charging starts on the rising edge of the
GPS 1PPS and ends on the rising edge of the picDIV 1PPS**, so the pulse width
*equals the phase interval* between them. That pulse gates a Schottky diode
(1N5711) which charges C13 through R8 — a **time-to-voltage ramp**, exactly
like Lars Walenius' original, only with a flip-flop instead of an HC4046. The
MCU reads the ramp peak once per second and the charge then bleeds away
(~25 ms) before the next pulse.

Two things follow, and both were learned the hard way:

- **The RC must be small.** R8×C13 = 1 kΩ × 1 nF, τ ≈ 1 µs — matched to the
  µs-scale pulse so the cap tracks pulse width linearly. This is the value on
  Kaashoek's schematic (note "R8×C13 = 100 ns" on rev 0.4, 1000 ns on the
  later sheet); it is **not** a low-pass average of a duty cycle. An earlier
  revision of these notes claimed the opposite (a "duty-cycle detector"
  needing a large 51 kΩ/1 µF filter) — that was wrong. With 51 kΩ/1 µF the
  µs pulse barely moved the cap (≈14 mV span in `LC`); with 1 kΩ/1 nF the ramp
  spans ~1.5–2 V and `LC` works.
- **The read must be on the peak.** The ramp peaks at the end of the pulse
  (≤ ~2 µs after the GPS edge) and holds under ~1 ms before decaying. Sampling
  it from the 2 s sensor loop always caught the discharged cap (~0.065 V,
  independent of phase — the root cause of weeks of "failed calibrations").
  Vphase is now read ~50 µs after the PPS edge, from the PPS-notified relay
  task, landing on the peak. No active discharge: the diode blocks and the
  ~25 ms leakage clears the cap before the next 1 Hz pulse.

### picDIV's role

picDIV is **not** part of the ramp value — it generates the disciplined
**1PPS output** (UTC-synchronised, holdover-capable), and its edge marks the
end of the charge pulse. The `AP`/arm step at the start of `LC` just parks the
phase near the GPS edge so the sweep starts from a known point; the phase
detector compares GPS-PPS against picDIV-PPS (both derived, respectively, from
the sky and from the disciplined OCXO), which is why minimising Vphase aligns
the output PPS to UTC.

### Calibration: anchored operating point (Option D)

The ramp is exponential (τ ≈ 1 µs), so ns/V is **not constant** along it. A
whole-transit average (range/span) therefore depends on where the arm parked
the phase, and drifted ~15–20 % run to run. Two 1 s-resolved `LC` logs showed
the **local slope** dV/dt is repeatable to ~0.3 % in a narrow band near
**1.85 V**, and diverges above and below it — that voltage is the repeatable
sweet spot of this detector (≈0.63·Vsat, the middle of the usable range).
`LC` now anchors `zero_offset` there and reads ns/V from the local slope in a
±0.20 V window, clear of the **dead zones** Dan Wiering characterised: the
Schottky drop + pull-down below ~0.05 V, and the ADC rail/wraparound near
3.3 V (PA1 tolerates 5 V but reads only to ~3.23 V). If a sweep never crosses
the anchor band, `LC` falls back to the range/span average and says so.

### Resolution

The 1 kΩ/1 nF ramp spans ~1.5–2 V of the 12-bit ADC over the usable phase
window, and 16× oversampling with a median reject glitches — comparable to or
better than Lars' single-read HC4046 at ~1 ns. The ~25 ms decay is irrelevant
to loop bandwidth: LOCK updates every few seconds (well under 0.2 Hz), so the
detector's own time constant is orders of magnitude clear of the loop.

### LTIC phase input (Lars' TIC)

With `GPSDO_LTIC` enabled, the firmware reads a hardware time-interval
counter (Lars Walenius' TIC): a 1 nF capacitor is charged with a constant
current during the GPS-1PPS → OCXO-1PPS interval, and the latched voltage on
PA1 is sampled on the ramp peak ~50 µs after the PPS edge; no active
discharge is needed — the diode blocks and the ~25 ms leakage clears the cap
before the next 1 Hz pulse. The voltage is a
direct, high-resolution measure of the phase difference between the two pulses
— far finer than the TIM2 cycle-counter used by the frequency-domain
algorithms (3–9).

The control loop **disciplines directly from this phase** via algorithm 10
(`LA 10`) — the three-stage ACQ → DPLL → LOCK loop described below. The phase
appears in the serial report (`Vphase:` and `dph:` in ns), as a `Vph:`/`dph:`
row on the TFT, and as an `LTIC phase (PA1)` line in the boot checklist. Once
`LC` has calibrated the ramp, phase is reported in nanoseconds relative to the
calibrated `zero_offset`, using the measured `ns_per_volt`; before calibration
only volts are shown. (The compile-time `LTIC_NS_PER_VOLT` in `gpsdo_config.h`
is a legacy fallback and normally stays 0 — `LC` measures the real slope per
board and stores it in the live parameters.)

**Self-calibration (`LC`).** Once the TIC hardware is built, `LC` calibrates it
automatically — no external reference needed. It briefly forces a small PWM
offset so the phase ramps at a rate known from the TIM2 frequency error, fits
the TIC voltage against time, and derives `ns_per_volt`, `zero_offset` and
`range_ns` (GPS is the implicit time reference). Review with `LL`, then `ES` to
save. The absolute scale is only as good as the TIM2 measurement of the ramp
rate, but the linearity and range it captures are what the loop actually needs.

---

---

## Settings storage (flash ring)

Settings live in a wear-levelled ring buffer in flash **sector 7**
(0x08060000, 128 KB) — the last sector, so firmware keeps the maximum contiguous
space below it. There is no EEPROM, emulated or otherwise.

Records are typed, so the settings block and the live learned data share one ring
without colliding. Each slot carries a CRC16, slots are sequence-numbered so the
newest valid one wins, and every write is read back and verified. A power cut
mid-write therefore leaves the previous settings intact rather than a corrupt
half-record.

The settings block holds the PWM and algorithm, the PID sets for algorithms 3-9,
the LTIC calibration and stage gains, the LTIC-Lars parameters, the damping
windows, the sensor offsets, the boot flags and the timezone. It is versioned:
a block written by an older firmware whose layout differs is rejected rather than
misread, and the board comes up on compile-time defaults.

| Command | Effect |
|---------|--------|
| `ES [group]` | Save all settings, or one group: `TZ` / `PID` / `LTIC` / `FLAGS` / `ALGO` / `PO` |
| `ER` | Recall settings from the ring |
| `EE` | Erase settings — back to defaults |
| `EW` | Flash wear: erase cycles and slots used |
| `CR YES` | Cold restart: wipe the ring entirely |

Preferences save themselves; loop tuning needs an explicit `ES`. Either way the
reply says which applied — see the persistence notes under the CLI section.

### Flash wear levelling (live data)

"Live" data — learned drift/damping (`LRN`), LC calibration, and the last
PWM — changes far more often than settings, so it is stored separately from
the settings block in a **wear-levelled ring buffer** occupying flash
sector 6 (0x08040000, 128 KB). Toggle it with `FR 0|1` (saved by `ES`,
default on); check wear with `EW`.

Each save writes the next 32-byte slot; the sector is erased only when the
ring wraps (once per 4095 saves). At 100 saves/day that is ~9 erases/year, so
the flash endurance (~10 000 cycles) lasts on the order of a thousand years.
A save happens only when a value has settled on a new level — drift changed
by > 8 LSB or damping by > 0.03, and ≥ 20 min since the last save — while a
successful `LC` saves immediately. Every slot has a CRC and sequence number,
so a power-cut mid-write is detected and the previous good slot is used.

With the ring **on**, `ES` never overwrites calibration or learned values —
it saves only genuine settings (PID gains, thresholds, flags). With the ring
**off**, `ES` still saves those live values to the settings block as a fallback.

### Keeping live data when re-flashing firmware

- **Bootloader / DFU / Arduino IDE upload** touches only the firmware sectors
  (0–5); the ring in sector 7 survives.
- **J-Link/ST-Link full-chip erase** wipes everything. To keep calibration and
  learning, erase only sectors 0–5:
  `erase 0x08000000 0x0803FFFF` then `loadbin firmware.bin 0x08000000`.
- If the ring is wiped, the firmware relearns/recalibrates from defaults —
  nothing breaks, only the accumulated tuning is lost.

---

---

## GPS timing receivers (LEA-6T / LEA-M8T / NEO-M8T / ZED-F9T)

NEO-6M / NEO-8M modules work out of the box (default). For a u-blox timing
receiver, enable the option in `gpsdo_config.h`:

```c
#define GPSDO_GPS_TIMING            // u-blox timing receiver (see below)
#define GPSDO_SVIN_MIN_SECS   300   // min survey-in duration [s]
#define GPSDO_SVIN_ACC_LIMIT  5000  // accuracy limit [mm] (5 m)
```

The LEA-6T and LEA-M8T accept **different** Time Mode commands, so the
firmware tries each in turn and keeps the first that is ACKed: `CFG-TMODE2`
(0x06 0x3D, used by the LEA-M8T) and the older `CFG-TMODE` (0x06 0x1D, used
by the u-blox 6 LEA-6T). Progress is read back with `TIM-SVIN` (0x0D 0x04) on
both. (The newer `CFG-TMODE3` / `NAV-SVIN` pair exists only on high-precision
firmware such as NEO-M8P / ZED-F9P, not on these timing units — verified in
u-center against a LEA-M8T-0 / TIM 1.10 and a LEA-6T.)

**NEO-M8T** is fully compatible with the LEA-M8T here — same u-blox M8 silicon
and FW3 firmware, same `CFG-TMODE2` / `TIM-SVIN` messages — so it works with
no code change beyond enabling the switch. (Note both M8T variants default to
GPS + GLONASS + QZSS; reconfigure to GPS + QZSS via `CFG-GNSS` in u-center and
save to flash if you want a single-constellation timing solution.)

**ZED-F9T (Gen9)** is supported as well. The F9 generation replaced the legacy
configuration messages (deprecated as of firmware TIM 2.24) with a
configuration-key interface, and reports survey-in via `NAV-SVIN` (0x01 0x3B)
rather than `TIM-SVIN`. Support is wired in as a third path:
`ubx_start_survey_in()` also sends a `CFG-VALSET` (0x06 0x8A) frame setting
`CFG-TMODE-MODE` / `CFG-TMODE-SVIN_MIN_DUR` / `CFG-TMODE-SVIN_ACC_LIMIT` (the
last converted from mm to the F9T's 0.1 mm unit), and the survey-in monitor
falls back to `NAV-SVIN` when `TIM-SVIN` does not answer. This path was tested
on real hardware by EEVblog user **danieljw**. The legacy `CFG-NAV5`
stationary-mode frame may be NAKed by an F9T; that is harmless (the survey-in
path is independent).


At every power-up the receiver runs a **survey-in**: it averages the
antenna position, then switches to a fixed-position **time-only** solution.
This gives a markedly cleaner 1PPS — single-satellite timing with no
navigation jitter — which directly improves phase stability. Survey-in ends
when either the minimum duration **or** the accuracy limit is reached.

Progress is shown on all displays as `SVIN <seconds> <accuracy>m`. The
position keeps streaming in NMEA throughout Time Mode (the frozen, averaged
fix), so the location display and automatic timezone (`TO A`) keep working —
in fact more stably, since the position no longer wanders.

> **Antenna matters.** Run survey-in only with a good outdoor antenna that
> has a clear, full view of the sky. Survey-in averages the antenna position
> and only completes once the accuracy limit is met; with an indoor or
> obstructed antenna it may converge slowly or stall at a poor accuracy
> (tens of metres). On a proper outdoor/rooftop antenna both the LEA-6T and
> LEA-M8T complete within the configured time and switch cleanly to Time
> Mode. (In testing the older LEA-6T proved noticeably more sensitive in
> marginal conditions than the LEA-M8T.)

In Time Mode the receiver stops optimising position, so the reported HDOP
becomes meaningless (~99.99). The displays and the human-readable serial
report show `HDOP:TIME` in that state instead of the bogus number; the
tab-delimited log keeps the raw value for plotting.

With neither option defined, NEO modules use the existing stationary-mode
path unchanged.

---

---

## Auto-tuning (`CT` command)

`CT` measures the oscillator's plant gain and derives PID coefficients for
all algorithms from it — no manual tuning, no risky forced oscillation.

Procedure (~3 minutes, deterministic):

1. Drive the PWM to three points (1.5 / 2.0 / 2.5 V), settling at each.
2. Least-squares fit of frequency vs PWM → **K** [Hz/LSB], the plant gain,
   plus the PWM that yields exactly 10 MHz.
3. Compute coefficients from K:
   - **PLL (4, 5, 7):** Kp = 0.40/K on frequency; Kd = 2.0, Ki = 0.02 on phase
   - **FLL (3, 6):** Kp = 0.35/K; Ki = Kp/300; Kd = Kp·73
   - **NN (9):** max step = 0.05/K
4. Apply the centred PWM and the new coefficients, print before/after.

The result is sanity-checked (K must be 0.1–2 mHz/LSB and the GPS must hold
a fix); on failure the parameters are left unchanged. Run `ES` afterwards to
save the tuned values to the flash ring. Unlike relay-feedback auto-tuning, `CT`
never destabilises the loop — the loop time constant here is hundreds of
seconds, so forced oscillation would take hours and be corrupted by thermal
drift; deriving the gains directly from a measured K is both faster and safer.

---

---

## Automatic timezone (`TO A`)

Local time can follow the GPS position automatically. In auto mode the
firmware recomputes the UTC offset continuously from latitude/longitude
and the date:

- **Inside Europe** (lat 35–72, lon −11–42): a compact civil-zone rule set
  (UTC+0 west of −7.5°, UTC+1 for the CET belt including all of Poland,
  UTC+2 for the Baltics/Finland/Balkans), plus the **EU DST rule** —
  +1 h from the last Sunday of March 01:00 UTC to the last Sunday of
  October 01:00 UTC.
- **Outside Europe**: solar zone `round(lon/15)`, without DST (rules vary
  too much worldwide to guess safely).

`TO <n>` switches back to a fixed manual offset. The mode and offset are
saved with `ES` and restored at boot.

---

---

## Startup hardware report

Every optional device reports its detection result on serial/Bluetooth at
boot, giving a complete inventory of what was found:

```
HW: AHT10/AHT20 sensor    OK  (I2C 0x38)
HW: BMP280 sensor         OK  (I2C 0x77)
HW: INA219 sensor         not found
HW: OLED 128x64           OK  (I2C 0x3C)
HW: LCD 20x4              OK  (I2C expander)
HW: HT16K33 clock display OK  (I2C 0x70)
HW: TFT 320x240            enabled (SPI1, write-only - not verifiable)
HW: TM1637 clock display  enabled (GPIO PA8/PB4, write-only - not verifiable)
```

A missing device reports `not found` and the firmware continues without it.

---

---

## Algorithm 12 — multi-level accumulator

After Alan Cashin's (MIS42N on EEVblog) Budget GPSDO. Every other loop here has
one time constant, and that constant is a compromise nobody wins: measured
against a rubidium reference, `LTC 60` is up to 1.58x better past 800 s while
`LTC 240` is up to 1.44x better between 10 and 400 s. You pick one.

This algorithm does not pick. Readings accumulate into levels — level n covering
2^n seconds — and a correction is applied at the **lowest** level whose error
exceeds that level's limit. A large error acts within two seconds; a small one
waits for a longer average. There is no `LTC` to set.

The levels come out of the bit pattern of the seconds counter rather than from an
array of buffers: inspect from the least significant bit upward, stop at the first
zero. Eleven levels — 2 s to 2048 s — cost 22 bytes.

### What it measures

**Phase in nanoseconds, from the LTIC detector.** The first port fed the TIM2
count error in whole hertz and was blind: a disciplined oscillator sits far below
1 Hz, so that field read zero in 83% and 95% of samples across two runs and the
algorithm never corrected. Phase integrates where a one-second frequency count
does not — an error of 1e-11 is invisible in the count but becomes 25 ns of phase
in 2500 s.

**The detector is required.** An earlier version fell back to integrating the
count error on boards without one; that fallback did not recover phase, it
accumulated a random walk of quantisation noise which crossed a level limit,
fired a correction, hit the clamp and railed the detector — measured at 6000
counts of PWM swing over 148 corrections with the detector railed 58% of the
time. `LA 12` now refuses without `GPSDO_LTIC`, and holds rather than guessing
whenever the detector is fitted but not reading.
Like the other LTIC algorithms, it arms the picDIV. Without that the detector ramp sits
against a rail, `ph_valid` never becomes true, and the algorithm falls through to
integrating the count error — back to being blind, silently, in exactly the way
using the detector was meant to fix. The bridge waits for several consecutive
railed readings before disturbing the divider, then holds off while the phase
settles. Trend shows `ARM` while it is doing this.

### Commands

| Command | Effect |
|---------|--------|
| `LA 12` | Select the algorithm |
| `MG [v]` | Gain, LSB per ns. 0 derives it from the `CT` calibration |
| `MR [n]` | Force a correction once level n is reached, whatever the limits say |
| `MLP <n> [ns]` | Phase limit for level n; without a value, prints it |
| `MF [0-3]` | Where the limits come from, independently of `MG`: 0 follow `MG`, 1 stored table, 2 sigma formula, 3 measured |
| `MFT [s]` | `MF 3` only: target seconds between corrections triggered by noise alone (default 3600) |
| `ML` | List gain, run level and all eleven limits |
| `ES ALGO12` | Save the block to the flash ring |

### Untuned, and why

Only **one** limit was ever derived. Alan's specification was 10 MHz ±0.01 Hz,
which is 125 ns of phase in 128 seconds — that fixes the 128 s entry. He could
have used 64 ns at 64 s, but a cheap module with a poor signal wanders ±100 ns
and would raise false errors. Of the rest he says plainly: *"they work most of
the time but were not optimised in any way."*

So there is nothing to reproduce faithfully beyond that one anchor, which is why
the whole table is editable and saved per board.

---

## Self-assessment without a reference — `CS`

Algorithm 11 was validated against a rubidium standard on someone else's bench.
Almost nobody who builds this will have one, and without it there is the author's
word and a green rectangle. `CS` gives something better.

The correction the loop applies is the error it just observed, so the size of
those corrections says whether the discipline is working — and GPS is the
reference, so nothing better exists to compare frequency against. The firmware
already computed every one of these numbers and threw them away. The idea is
Alan's (MIS42N on EEVblog), whose own design relies on exactly this, which is why
it needs no secondary standard.

`CS` reports RMS correction over the last 100, 1 000, 10 000 and 100 000
corrections, in DAC counts and — once `CT` has measured the oscillator's slope —
in fractional frequency, directly comparable to an ADEV figure. It also reports
the steady bias, non-zero when the loop is tracking real drift rather than noise.

**The windows count corrections, not seconds**, because the correction rate
depends on the algorithm: algorithm 11 steers once a second, algorithm 10 once
per `LIV`. Labelling them in minutes would have meant one thing under one
algorithm and sixty times that under the other. `CS` measures the actual interval
and prints what the windows currently span in wall-clock time, so the reader does
not have to work it out — at one correction per second, 100 000 covers about 28
hours.

These are exponential weights rather than hard windows: about 63% of the weight
falls inside N corrections and 95% inside 3N, so old data fades rather than
dropping out. That costs four multiply-adds per correction and no memory, where a
buffer holding 100 000 samples would be most of the RAM budget to answer the same
question no better.

**Counted only while the loop is locked and no calibration is running.** The
acquisition ramp, the three jumps `CT` makes while measuring the VCO slope and
the sweep `LC` uses are commands, not corrections; one of them would dominate the
hour-long average long after it ended. Algorithms 0-9 have no lock state to gate
on, so they are excluded and `CS` says so rather than reporting a number with no
defined meaning.

> **What it does not tell you.** It measures whether the LOOP IS SETTLED, not
> whether the OUTPUT IS GOOD, and those are the same thing only while the phase
> detector is trustworthy. A noisy detector makes the loop chase noise: the
> corrections grow, this reports them faithfully, and the oscillator was fine
> until the loop made it worse. Nothing measured from inside the loop can see
> that. Read a small figure as "not fighting anything" — necessary, not
> sufficient. A growing figure is the useful signal.

---

## 24-bit control voltage — dithered PWM (`GPSDO_PWM_DITHER`)

The idea is Alan Cashin's (MIS42N on EEVBlog), from his Budget GPSDO: run the PWM
at fewer bits than you need and vary the duty cycle from one period to the next,
so that the **average** carries the missing bits.

**The gain is the carrier, not the extra bits.** Ripple has to be filtered below
one output step, and how hard that is depends on how far the carrier sits above
the filter's corner:

| | carrier | filter corner | time constant |
|---|---|---|---|
| 16-bit PWM | 2 kHz | 0.7 Hz | 230 ms |
| 13-bit dithered (shipped) | 12.2 kHz | 4.2 Hz | 38 ms |
| 12-bit dithered | 24.4 kHz | 8.4 Hz | 19 ms |

Filter delay goes straight into the loop as phase lag, so a six-fold shorter
filter is worth more than the resolution is. The resolution comes along for free.

**How it runs here.** Alan dithers in a timer interrupt, because a PIC has no
DMA. That would be 12 000 interrupts a second on this part, competing with the
1PPS capture — the one interrupt in this firmware that must not be delayed. But
the dither pattern for a constant value is periodic: it repeats every
2^(24−N) periods. So it is computed once into a table and replayed by DMA into
the compare register, with the CPU touching nothing between value changes.

The average is **exact**, not approximate: the table holds exactly Y entries of
X+1 among 2^(24−N), so it averages X + Y/2^(24−N), which is the 24-bit value by
construction. Replaying it cannot drift.

| `GPSDO_PWM_DITHER_BITS` | table | RAM (two buffers) | carrier | CPU |
|---|---|---|---|---|
| 12 | 4096 entries | 16 KB | 24.4 kHz | 0.025% |
| 13 (default) | 2048 entries | 8 KB | 12.2 kHz | 0.012% |

Same pin as the plain PWM — **PB9, TIM4 CH4** — so the existing filter and wiring
are unchanged. TIM4_UP drives DMA1 Stream 6 Channel 2; the 2 Hz tick is on TIM9
and the 1PPS chain on TIM2/TIM3, so nothing else is disturbed. Two buffers in
hardware double-buffer mode mean a value change never glitches the pin, and both
are refilled on every write — filling only one leaves the other replaying the
previous code until the next write, which puts a square wave on the output at
about 3 Hz.

**On** in the shipped `gpsdo_config.h` since v1.05 — it was off by default in
v1.04, while the output path was still being proven. Comment out
`GPSDO_PWM_DITHER` to fall back to the plain 16-bit PWM; the pin, the filter and
the wiring are the same either way, so nothing outside the board changes. Needs
`configUSE_MUTEXES` and `INCLUDE_xTaskGetSchedulerState`, which this project now
sets explicitly. Mutually exclusive with `GPSDO_DAC_EXT` — enabling both is a
compile error, since both drive the control voltage.

### What the loop does with the low 8 bits

Since v1.05, the control loop keeps the fraction of its correction instead of
truncating it. On the plant measured here one 16-bit step is about 320 µHz —
3.2e-11 of 10 MHz, coarser than the 4e-12 the loop holds over 10 000 s, which it
reached by hunting between adjacent codes. With the fraction kept, the step is
**1.25e-13** and the hunting goes away.

The fraction is owned by the DAC layer, not by the loop. The control value is
written from 21 places and twenty of them — the `CT` and `LC` sweeps, the
acquisition ramps, holdover steering, `SP` — are deliberately coarse, and every
one of them clears the fraction simply by calling `gpsdo_dac_write16()`. Nothing
above that layer changed: the displays, the telemetry line, the flash ring and
the settings block all still see a plain 16-bit code.

Run `DAC` to see which path is compiled in, the code in all three views, and the
step size in µHz and in fractional parts. A 24-bit code that is not a multiple of
256 is the proof the fine path is driving the pin. The step figures need the
plant gain from `CT`; without it the command says so rather than printing a
number from a default.

---

## External SPI DAC — planned, not implemented

`GPSDO_DAC_EXT` switches the control voltage from the 16-bit PWM to an external
SPI DAC. Enabling it today is a compile error by design: `gpsdo_dac_ext.cpp` is a stub
with no device chosen.

The PWM gives about 50 uV per step at 3.3 V, near 2.7e-11 fractional on a
5.3 Hz/V oscillator. An 18-bit part with a reference designed for the job reaches
roughly 17 uV, near 9e-12, with no filter delay inside the loop.

No hardware SPI is needed or available — SPI1 belongs to the TFT and every SPI2
pin on this package is taken. It does not matter: the DAC is written once per
second, so bit-banging the word costs on the order of a microsecond. Suggested
pins are PB0, PB2 and PB4, chosen to avoid PB6/PB7, which look free but are the
default I2C1 pins that `Wire.begin()` claims.

All 23 places that used to write the PWM now go through `gpsdo_dac_write16()`, so
adding a device means filling in one function rather than editing 23 call sites.

---

## Oscillator (OCXO)

The firmware works with any 10 MHz voltage-controlled OCXO whose EFC input
sits within the 0–3.3 V range delivered by the STM32 PWM DAC (a 0–4 V EFC
oscillator works too — about 82.5% of its range is reachable). The oscillator
type does **not** need to be selected at compile time.

Instead, run the **`CT` (Calibrate & Tune)** command once after warm-up: it
measures the actual control gain *K* [Hz/LSB] from a three-point PWM sweep,
finds the PWM value for exactly 10 MHz, and derives every PID coefficient for
the fitted oscillator. Save with `ES`. Before the first `CT`, the loop starts
from a universal mid-range PWM (32767 ≈ 1.65 V), which is safe for any
0–4 V EFC unit.

This replaces the earlier per-oscillator coefficient tables — one calibration
adapts the loop to whatever crystal is installed, including unit-to-unit
variation between two nominally identical parts.

---

---

## Build configuration

The file `gpsdo_config.h` controls the build. Key switches:

```c
// Displays — uncomment as needed:
#define GPSDO_OLED_SSD1309       // or SH1106, SSD1306
#define GPSDO_LCD_20x4           // HD44780 20x4 I2C
#define GPSDO_TM1637_6           // 6-digit TM1637 (HH:MM:SS)
#define GPSDO_TFT_ST7789         // ILI9341/ST7789 320x240, or GPSDO_TFT_ILI9488 480x320
#define GPSDO_HT16K33            // 4-digit HT16K33 clock, I2C 0x70

// Sensors:
#define GPSDO_AHT10              // AHT10/20 temperature + humidity
#define GPSDO_BMP280_I2C         // BMP280 temperature + pressure
#define GPSDO_INA219             // INA219 voltage + current

// Communication:
#define GPSDO_BLUETOOTH          // HC-06 on Serial2 (57600 Bd)

// Other:
#define GPSDO_PWM_DITHER        // 24-bit control voltage (dithered PWM, PB9)
#define GPSDO_PICDIV             // picDIV support
#define GPSDO_UBX_CONFIG         // NEO-6M/7M UBX configuration
#define GPSDO_GEN_2kHz_PB5       // 2 kHz generator on PB5
```

### Serial buffer (`build_opt.h`)

The sketch folder also contains `build_opt.h`, which STM32duino passes to
the whole build (including the core) as compiler flags:

```
-DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=512
```

This enlarges the GPS serial RX buffer from the 64-byte default so NMEA
sentences are not dropped or merged at 38400 baud when the GPS task is
briefly preempted. A plain `#define` in the sketch would not work — the
core's `HardwareSerial.cpp` is a separate translation unit that only sees
compiler flags. The file is picked up automatically; nothing to enable.

---

---

## Pin assignments

| Pin | Function |
|-----|----------|
| PA15 | TIM2 ETR — 10 MHz OCXO input |
| PB10 | TIM3 CH3 — GPS 1PPS capture |
| PB9 | PWM DAC — Vctl control (16-bit) |
| PB1 | ADC — Vctl measurement |
| PA0 | ADC — Vcc/2 measurement |
| PB8 | Yellow LED — fix / holdover indication |
| PC13 | Blue LED — 1PPS heartbeat |
| PB5 | 2 kHz generator (optional) |
| PB3 | picDIV ARM (optional) |
| PA1 | LTIC Vphase (optional) |
| PA9/PA10 | Serial1 TX/RX — GPS NMEA |
| PA2/PA3 | Serial2 TX/RX — Bluetooth HC-06 |
| PB6/PB7 | I2C1 SCL/SDA — OLED, LCD, sensors |
| PA5/PA7 | SPI1 SCK/MOSI — TFT display |
| PB12/PB13/PB15 | TFT D/C, CS, RES |

---

---

## Roadmap

### Planned

**An external SPI DAC for the control voltage.** The 16-bit PWM gives about 50 µV
per step at 3.3 V, near 2.7e-11 fractional on a 5.3 Hz/V oscillator. An 18-bit
part with a reference designed for the job reaches roughly 17 µV, near 9e-12,
with no filter delay inside the loop.

The part is now chosen and written: the **AD5680**, 18 bits, behind an external
REF5045, bit-banged on CS/SCK/MOSI = PB4/PB0/PB2 on Dan Wiering's PCB. No
hardware SPI is needed or available — the DAC is written once per second, so
bit-banging costs microseconds. `GPSDO_DAC_EXT` enables it, and it is mutually
exclusive with `GPSDO_PWM_DITHER` because both drive the control voltage. The
driver compiles and links for cortex-m4 in `tools/hostcheck`; the hardware test
is Dan's to run. Still open: choosing the output span against the oscillator's
EFC range, and the runtime `DAC [PWM|DITH|EXT]` path selection that goes with
the jumpered PCB.

Worth knowing where the useful limit sits. At 24 bits the step is 197 nV;
thermal noise in 10 kΩ over 1 Hz is about 13 nV, so Johnson noise is not the
obstacle, but a good zero-drift amplifier contributes around 180 nV peak-to-peak
over 0.1–10 Hz and a precision reference around 1 µV. **18 bits is the sweet spot
and 20 the sensible ceiling** — beyond that the analogue chain cannot deliver
what the converter promises.

### Tried and abandoned

Both of these were attempted properly, and both are recorded here because the
reasons are more useful than the ideas.

**A 24-bit sigma-delta DAC on a spare pin, DMA-fed.** Built, host-tested, then
measured — at which point it stopped being attractive. The command is 24 bits,
but the resolution reaching the oscillator is set by how long the analogue filter
averages: 13.6 effective bits over 4 096 bits of averaging, 19.0 over 262 144,
and 24.0 only after 16.7 million — 43 seconds at 390 kHz. A filter fast enough
not to delay the loop yields about 18 bits, for 6% of the CPU running
continuously plus a precision reference, a CMOS gate and a fourth-order active
filter. Raising the bit rate does not rescue it: 24 bits inside a 32 ms filter
would need 524 MHz.

An earlier version of this document claimed a 320-fold improvement. That was
wrong — it compared the command width against the PWM's step size, which compares
nothing. The code remains in the tree, marked as a dead end: the modulator core is
correct and host-tested, and a measured dead end is worth more than an untried
idea.

**Two USB CDC ports**, one for the CLI and one as a transparent GPS tunnel for
u-center. Not possible without forking the core's USB layer: STM32duino's stack
is single-class, the composite-device request has been open for years, and core
3.0.0 changed nothing there. Hand-written descriptors would be a few hundred
lines that break with every core update — exactly what happened to TFT_eSPI on
3.0.0. The existing parallel-Bluetooth mode already achieves the same outcome:
`T` tunnels GPS over USB while the CLI stays on Bluetooth, undisturbed.

### Not planned

**Algorithms 0–9 are frozen.** They remain in the firmware, they still work, and
settings already tuned for them are still honoured. But development has stopped:
algorithm 11 measured 2–3× better than a properly tuned algorithm 10 at the
averaging times where a GPSDO earns its keep, on independent hardware against a
rubidium reference. Effort goes to the LTIC algorithms 10–12.

They are kept rather than deleted because eleven documented approaches to the
same control problem are worth more as a reference than a tidier source tree, and
because nobody's existing configuration should break. Expect no new features
there, and treat the self-learning feed-forward (`LRN`) as belonging to that same
frozen group — it serves algorithms 3–9 only.

---

## Requirements

- **Board**: WeAct BlackPill STM32F411CE or F401CCU6
- **IDE**: Arduino IDE with STM32duino core **2.2.0 – 2.12.0**
  (see the note below on core 3.0.0)
- **Libraries**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (for LCD), TFT_eSPI (for TFT) (settings live in the on-chip flash ring, so no EEPROM library is needed)
- **Build settings**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

### STM32duino core 3.0.0 — not yet supported

**Build against core 2.12.0 or earlier.** Core 3.0.0 (released 23 July 2026)
carries two changes flagged as major by its own release notes: the
**ArduinoCore-API deployment** and HALv2 for the STM32C5xx series. Three
consequences matter here:

1. **`ltoa()` is gone.** It is a non-standard extension that the older core
   supplied; the firmware uses it in four places. On 3.0.0 these fail to compile
   and would have to become `snprintf(buf, n, "%ld", …)`.
2. **`HardwareSerial` is no longer the concrete class.** Under ArduinoCore-API
   it is an abstract interface, so `HardwareSerial Serial2(PA3, PA2)` no longer
   instantiates. A typedef selecting the right class per core version is the
   usual remedy.
3. **TFT_eSPI stops driving the panel.** This is the blocking one. The library
   uses `SPIClass` only to configure the pins and bring the peripheral up, then
   talks to the controller through raw `HAL_SPI_Transmit()` on its own handle.
   When the SPI layer beneath it changes, the panel can be left without a valid
   init sequence — the observed symptom is a **white screen**, with the CLI and
   telemetry still working normally.

Points 1 and 2 are small and could be made version-conditional at any time.
Point 3 lives in TFT_eSPI, not in this firmware, so 3.0.0 has to wait for the
library. Since 2.12.0 is a full release and nothing here needs 3.0.0, staying
on 2.12.0 costs nothing.

> Worth knowing when searching: most "Arduino core 3.0.0" material online is
> about the **ESP32** core, a different project whose 3.0.0 migration guide does
> not apply to STM32.

---

---

## Licence

Published under the same terms as André Balsa's original project.
