# GPSDO FreeRTOS v0.48

**English** | [Polski](README_PL.md) | [Español](README_ES.md)

Real-time (FreeRTOS) firmware for a GPS-Disciplined Oscillator (GPSDO)
on the STM32 BlackPill platform (WeAct F411CE / F401CCU6).

## Credits

| Role | Person / source |
|------|-----------------|
| FreeRTOS port author, algorithms 3–9 | **J. M. Niewiński** — [repository](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Programming assistant (Anthropic) | **Claude AI** |
| Original firmware v0.06c author | **André Balsa** — [repository](https://github.com/AndrewBCN/STM32-GPSDO/tree/main/software/GPSDO_V006c) |
| PCB design (prototype) | **Scrachi** (EEVBlog forum) — [post with files](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [profile](https://www.eevblog.com/forum/profile/?u=762266) |
| Project thread | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — EEVBlog Forum |

This firmware was written from scratch as a port of André Balsa's original
code to the FreeRTOS architecture, with a complete redesign of tasks,
synchronisation, and display handling. The hardware design is based on the
v0.06c schematic, using PCBs shared by the user Scrachi on the EEVBlog forum.

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
               │  NEO-6M/7M  │       │  10 MHz      │                     │
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
  AHT  BMP  INA        │ ILI9488    │  480x320 (untested)
  20   280  219        │ T6963C *   │  240x128 mono (via SPI bridge,
                       └────────────┘            experimental)
                       * mutually exclusive with the colour TFTs
```

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
- **TFT 480×320** SPI (ILI9488, TFT_eSPI library) — *untested, no panel on
  hand yet; the 320×240 layout is auto-scaled up*
- **T6963C 240×128** mono LCD via external SPI→T6963C bridge (PG240128);
  shares the TFT's SPI1 pins, mutually exclusive with the TFT.
  ⚠️ **Experimental / untested** — the backend is complete but the link has
  only been bench-tested on long wires with signal-integrity issues (ringing,
  spurious CS edges). Needs validation on clean, short-wired hardware before
  use; leave disabled for now.
- **HT16K33** 4-digit 7-segment clock with colon, I2C addr 0x70 (HH:MM)

OLED and LCD can operate simultaneously (different I2C addresses).
LCD and TM1637 **cannot** operate simultaneously (bus conflict).

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
| Lowest | `vUptimeTask` | 768 B | Uptime counter (dd hh:mm:ss) |

**Shared state** is protected by FreeRTOS mutexes:

- `xFreqMutex` — frequency data (`gFreq`, `gFreqSnap`)
- `xGpsMutex` — GPS data (`gGps`)
- `xCtrlMutex` — control data (`gCtrl`: PWM, algorithm, holdover, trend)
- `xUptimeMutex` — uptime (`gUptime`)
- `xWireMutex` — I2C bus (shared by sensors and displays)
- `xSerialMutex` — serial / Bluetooth port

---

## Control algorithms

Ten algorithms selectable via the `LA n` command:

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
| 9 | Neural net | e/∫e/de | 10 s | Experimental — single-layer perceptron |

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
no recompilation needed. Parameters are persisted to EEPROM with `ES`.

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

## TFT display layout (ILI9341 / ST7789 320×240, ILI9488 480×320, TFT_eSPI)

Cheap SPI TFT modules are supported in landscape orientation, driven over
hardware SPI1: **ILI9341** and **ST7789** at 320×240, and **ILI9488** at
480×320. All three share the same `User_Setup.h` wiring — switching panels
requires only changing the driver define and the width/height. The
`TFT_RGB_ORDER` / `TFT_INVERSION_OFF` lines are needed for correct colours on
ST7789 modules and are harmless on the others. Independent of the I2C
displays — OLED, LCD and TFT can all run simultaneously.

> **ILI9488 is untested** — there is no panel on hand yet. The 320×240
> operating screen and splash are auto-scaled to 480×320 at compile time
> (width ×1.5, height ×1.33, with fonts mapped up one size). The code
> compiles and the geometry is verified to fit the panel, but it has not been
> run on real hardware. Treat as experimental until confirmed. Note ILI9488
> over SPI is appreciably slower (480×320, 18-bit colour), so repaints are
> more visible than on the small panels.

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
│ GPSDO v0.48-rtos        LMT 14:32:45 Thu   │ ← header bar (navy)
├────────────────────────────────────────────┤
│                                            │
│        10000000.0000 Hz                    │ ← frequency (large, colour-coded)
│                                            │
├────────────────────────────────────────────┤
│ UTC:12:32:45 Thu     │ Sat: 9 HDOP:0.90    │
│ 11/06/2026           │ Lat: 52.123456      │
│ Up 000d 02:15:33     │ Lon: 23.123456      │
│ Algo:5  hit          │ Alt:  175m          │
│ PWM:44653 V:1.97     │ IN:12.05V  250mA    │
├────────────────────────────────────────────┤
│ BMP:23.4C 1013.2hPa  │ AHT:22.1C 45.3%rH   │
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
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SPI_FREQUENCY 27000000
```

For the **ILI9488 (480×320)** panel, change the driver and dimensions, and
add `LOAD_FONT6` (the larger frequency font the scaled layout uses):

```c
#define ILI9488_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
// ...same TFT_MISO/MOSI/SCLK/CS/DC/RST/RGB_ORDER lines as above...
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6              // large frequency font on the scaled layout
#define SPI_FREQUENCY 27000000
```

**Troubleshooting:** if the firmware freezes at the OLED version splash after
enabling TFT, check the serial output. The message `TFT: init start ...` is
printed immediately before `TFT_eSPI::init()` — if it is the last line, the
hang is inside the library: verify that `User_Setup.h` contains exactly the
pins above (including `TFT_MISO PA6`) and the correct driver define. The
DisplayTask stack is automatically raised to 768 words when `GPSDO_TFT` is
enabled — if you modified stack sizes manually, restore that value.

Then enable `GPSDO_TFT_ST7789`, `GPSDO_TFT_ILI9341` or `GPSDO_TFT_ILI9488` in `gpsdo_config.h`.

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

## CLI commands (Serial / Bluetooth)

Connection: 115200 Bd (USB) or 57600 Bd (Bluetooth HC-06, `GPSDO_BLUETOOTH`).
Commands terminated by `\r\n` or `\n`.

### General

| Command | Description |
|---------|-------------|
| `H` | Display help |
| `V` | Version, authors and GitHub links |
| `F` | Flush frequency ring buffers (restart averaging) |
| `C` | Start auto-calibration (PWM centring only) |
| `CT` | Calibrate + auto-tune: measure K, derive PID for all algos 3-9 |
| `T` | GPS tunnel mode — pass-through to GPS UART (exits after 300 s) |
| `SP <n>` | Set PWM DAC directly (1–65535), bypasses algorithm |
| `RH` | Report mode: human-readable (default) |
| `RD` | Report mode: tab-delimited |
| `RP` | Pause serial/BT data stream |
| `RR` | Resume serial/BT data stream |
| `SW` | FreeRTOS task stack watermarks (diagnostics) |

### Control

| Command | Description |
|---------|-------------|
| `MH` | Enable holdover mode (manual) |
| `MD` | Enable disciplined mode |
| `LA [0-9]` | Select / show control algorithm |
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
| `TO [n]` | Show / set local time offset manually (hours, −23..23) |
| `TO A` | Auto timezone: zone from GPS position + EU DST rule |
| `PO [f]` | Show / set pressure offset |
| `AO [f]` | Show / set altitude offset |
| `SV [0\|1]` | Survey-in / Time Mode on timing receiver (saved by `ES`, applied at next boot) |

### EEPROM

| Command | Description |
|---------|-------------|
| `ES` | Save all parameters to EEPROM |
| `ER` | Recall parameters from EEPROM |
| `EE` | Erase EEPROM (restore defaults) |

---

## EEPROM

EEPROM (emulated in STM32 Flash) stores 144 bytes:

| Address | Size | Content |
|---------|------|---------|
| 0–5 | 6 B | Signature `"GPSD2"` |
| 6–7 | 2 B | PWM DAC value (big-endian) |
| 8 | 1 B | Algorithm number (0–9) |
| 9 | 1 B | Time offset (±23 h) |
| 10–121 | 112 B | PID: g_pid[3..9] × {Kp, Ki, Kd, I_LIMIT} |
| 122–133 | 12 B | g_blend_crossover, g_blend_scale, g_nn_max_step |
| 134–137 | 4 B | g_pressure_offset (PO command) |
| 138–141 | 4 B | g_altitude_offset (AO command) |
| 142 | 1 B | timezone mode (0 = manual, 1 = auto `TO A`) |
| 143 | 1 B | survey-in enable (0 = off, 1 = on, `SV` command) |



---

## GPS timing receivers (LEA-6T / LEA-M8T)

NEO-6M / NEO-8M modules work out of the box (default). For a u-blox timing
receiver, enable the option in `gpsdo_config.h`:

```c
#define GPSDO_GPS_TIMING            // u-blox LEA-6T / LEA-M8T timing receiver
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
save the tuned values to EEPROM. Unlike relay-feedback auto-tuning, `CT`
never destabilises the loop — the loop time constant here is hundreds of
seconds, so forced oscillation would take hours and be corrupted by thermal
drift; deriving the gains directly from a measured K is both faster and safer.

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
#define GPSDO_EEPROM             // Parameter persistence
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

## Requirements

- **Board**: WeAct BlackPill STM32F411CE or F401CCU6
- **IDE**: Arduino IDE with STM32duino core ≥ 2.2.0
- **Libraries**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (for LCD), TFT_eSPI (for TFT), EEPROM (STM32)
- **Build settings**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

---

## Licence

Published under the same terms as André Balsa's original project.
