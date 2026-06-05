# GPSDO FreeRTOS v0.25

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
               ┌─────────────┐      ┌──────────────┐
   GPS Antenna ┤  u-blox     │      │    OCXO      ├── TIM2 ETR (PA15) ──┐
               │  NEO-6M/7M  │      │  10 MHz      │                     │
               └──┬──────┬───┘      └──────▲───────┘                     │
                  │      │                 │                              │
        NMEA      │  1PPS (PB10)     PWM (PB9)                           │
     (Serial1)    │      │           + RC filter                         │
                  │      │                 │                              │
               ┌──▼──────▼─────────────────┴──────┐                      │
               │          STM32 F411CE             │◄─────────────────────┘
               │          BlackPill                │
               └──────┬───────────┬───────┬───────┘
                      │           │       │
                    I2C bus    Serial    GPIO
                      │           │       │
                ┌─────┼─────┐     │    TM1637
                │     │     │     │    (clock)
               OLED  Sen-  LCD   BT
              128x64 sors  20x4  HC-06
                     │
                ┌────┼────┐
               AHT  BMP  INA
               20   280  219
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
| Low | `vDisplayTask` | 2 KB | OLED, LCD, TM1637, serial report, LEDs |
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
| 4 | PLL PI | phase10k | 10 s | Low phase noise, slow pull-in |
| 5 | PLL PID | phase1k | 10 s | Balanced: speed + noise |
| 6 | FLL PID (GA) | avg100 | 100 s | Genetically optimised coefficients |
| 7 | PLL PID (GA) | phase1k | 10 s | Genetically optimised coefficients |
| 8 | Hybrid | FLL+PLL | 100 s | Automatic FLL↔PLL sigmoid blend |
| 9 | Neural net | e/∫e/de | 10 s | Experimental — single-layer perceptron |

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
| `RH` | Report mode: human-readable (default) |
| `RD` | Report mode: tab-delimited |
| `RP` | Pause serial/BT reports |
| `RR` | Resume serial/BT reports |
| `SW` | FreeRTOS stack diagnostics |

### Control

| Command | Description |
|---------|-------------|
| `MH` | Enable holdover mode (manual) |
| `MD` | Enable disciplined mode |
| `LA [0-9]` | Select / show control algorithm |
| `AP` | Arm picDIV sequence |

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
| `TO [n]` | Show / set local time offset (hours) |
| `PO [f]` | Show / set pressure offset |
| `AO [f]` | Show / set altitude offset |

### EEPROM

| Command | Description |
|---------|-------------|
| `ES` | Save all parameters to EEPROM |
| `ER` | Recall parameters from EEPROM |
| `EE` | Erase EEPROM (restore defaults) |

---

## EEPROM

EEPROM (emulated in STM32 Flash) stores 142 bytes:

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

---

## Build configuration

The file `gpsdo_config.h` controls the build. Key switches:

```c
// Displays — uncomment as needed:
#define GPSDO_OLED_SSD1309       // or SH1106, SSD1306
#define GPSDO_LCD_20x4           // HD44780 20x4 I2C
#define GPSDO_TM1637_6           // 6-digit TM1637 (HH:MM:SS)

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

---

## Requirements

- **Board**: WeAct BlackPill STM32F411CE or F401CCU6
- **IDE**: Arduino IDE with STM32duino core ≥ 2.2.0
- **Libraries**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (for LCD), EEPROM (STM32)
- **Build settings**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

---

## Licence

Published under the same terms as André Balsa's original project.
