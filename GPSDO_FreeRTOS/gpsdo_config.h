/**
 * gpsdo_config.h — Compile-time configuration
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 *
 * All hardware features, pin assignments, display selection, RTOS task
 * priorities and stack sizes are controlled here.  Sanity checks enforce
 * mutual exclusion rules (e.g. LCD 20x4 and TM1637 cannot coexist).
 *
 * OUT_SERIAL macro routes all user-facing output to Serial2 when
 * GPSDO_BLUETOOTH is defined, or to Serial (USB) otherwise.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ── Serial buffers ─────────────────────────────────────────────────────
 * The serial RX/TX buffer sizes are set in build_opt.h (in the sketch
 * folder) as compiler flags, NOT here. A #define in this header does not
 * reach HardwareSerial.cpp in the STM32duino core (separate translation
 * unit), so it would be silently ignored. See build_opt.h:
 *     -DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=256
 * ────────────────────────────────────────────────────────────────────── */

/* ── Version ───────────────────────────────────────────────────────────
 * THE NAME IS "GPSDO vX.YY.NNrt": the release below, NN the build —
 * BUILD_SERIAL in gpsdo_build_id.h, so a bump renames the firmware by itself —
 * and "rt" for the FreeRTOS lineage the old "-rtos" suffix marked (build 56
 * alone spelled it vX.YY-rtNN). The build is appended in the sketch
 * (g_fw_version, see gpsdo_build.h) and not here, because the sketch is the
 * only unit that includes gpsdo_build_id.h: that is what keeps a bump from
 * recompiling every file that includes this one.
 * Wherever the firmware names itself, print g_fw_version, not this. */
#define PROGRAM_NAME     "GPSDO"
#define PROGRAM_VERSION  "v1.07"

/* ---- Serial output macro ----
 * OUT_SERIAL routes user-facing output to Serial2 (Bluetooth) or Serial
 * (USB). It depends on GPSDO_BLUETOOTH, so it is defined LATER in this file,
 * AFTER all feature switches — otherwise GPSDO_BLUETOOTH would not yet be
 * visible here and OUT_SERIAL would always resolve to USB. See the
 * "Derived macros" section below. */
#define AUTHOR_NAME      "J. M. Niewinski (jmnlabs)"   /* this firmware's author (ASCII for serial) */
#define ORIG_AUTHOR_NAME "Andre Balsa"   /* author of the original GPSDO v0.06c (ASCII for serial) */

/* ── Feature switches ────────────────────────────────────────────────── */

/* ── OLED display type — select exactly one, or comment all out ──────── */
//#define GPSDO_OLED_SH1106        /* SH1106  128x64 I2C — original hardware */
//#define GPSDO_OLED_SSD1306       /* SSD1306 128x64 I2C                     */
//#define GPSDO_OLED_SSD1309       /* SSD1309 128x64 I2C (same init as 1306) */

/* ── LCD 20x4 I2C — independent of OLED, enable or comment out ──────── */
//#define GPSDO_LCD_20x4     /* HD44780 20x4 via PCF8574T I2C expander */

/* ── TM1637 clock display — select exactly one, or comment both out ──── */
//#define GPSDO_TM1637_6           /* 6-digit TM1637: HH:MM:SS               */
//#define GPSDO_TM1637             /* 4-digit TM1637: HH:MM                  */
/* Brightness was buried in gpsdo_tasks.cpp as a bare setBrightness(1) with a
 * comment claiming "5/7". The argument is 0..7, so the code was asking for the
 * second-dimmest setting the part has while the comment described something
 * five times brighter — and the first person to fit one of these displays had
 * no reason to suspect the firmware rather than the module. Here, beside the
 * switch that enables the display and next to HT16K33_BRIGHTNESS below, is
 * where somebody would look for it. Reported by Dave (Solder_Junkie). */
#define TM1637_BRIGHTNESS   4    /* 0 (dim) .. 7 (max)                     */

/* ── HT16K33 clock display — 4-digit 7-seg with colon, I2C ────────────
 * Common AliExpress/Adafruit-style 0.56" clock modules (addr 0x70).
 * Shows HH:MM with the colon blinking each second.  Pure I2C device —
 * shares the bus with OLED/LCD/sensors, no extra pins, no conflicts.   */
#define GPSDO_HT16K33            /* 4-digit HT16K33: HH:MM                 */
#define HT16K33_I2C_ADDR  0x70   /* default; A0/A1/A2 jumpers raise it     */
#define HT16K33_BRIGHTNESS  2    /* 0 (dim) .. 15 (max)                    */

/* ── TFT SPI display — select exactly one, or comment all out ─────────
 *
 * Cheap ILI9341 / ST7789 (320x240) or ILI9488 (480x320) modules driven by
 * the TFT_eSPI library over hardware SPI1.  Landscape orientation.
 * Independent of the I2C displays (OLED/LCD) — all can run simultaneously.
 * The operating screen + splash are authored for 320x240 and scaled up to
 * 480x320 automatically (see TFT_SX/TFT_SY/TFT_F below); the layout code is
 * shared across all three panels.
 *
 * Wiring (fixed, hardware SPI1):
 *   SCK  → PA5   (SPI1 SCLK)
 *   SDI  → PA7   (SPI1 MOSI)
 *   RES  → PB15
 *   D/C  → PB12
 *   CS   → PB13
 *
 * TFT_eSPI REQUIRES library-side configuration.  Edit User_Setup.h in
 * the TFT_eSPI library folder (Arduino/libraries/TFT_eSPI/).
 *
 *   For ILI9341 / ST7789 (320x240):
 *     #define ST7789_DRIVER          // or ILI9341_DRIVER
 *     #define TFT_WIDTH  240
 *     #define TFT_HEIGHT 320
 *   For ILI9488 (480x320):
 *     #define ILI9488_DRIVER
 *     #define TFT_WIDTH  320
 *     #define TFT_HEIGHT 480
 *   Common to all:
 *     #define TFT_MISO PA6      // required by TFT_eSPI on STM32 even write-only
 *     #define TFT_MOSI PA7
 *     #define TFT_SCLK PA5
 *     #define TFT_CS   PB13
 *     #define TFT_DC   PB12
 *     #define TFT_RST  PB15
 *     #define TFT_RGB_ORDER TFT_BGR   // colour order Blue-Green-Red
 *     #define TFT_INVERSION_OFF       // some ST7789 modules need this
 *     #define LOAD_GLCD               // font 1 — frequency (320) + splash credits
 *     #define LOAD_FONT2              // 320x240 only: header + data grid
 *     #define LOAD_FONT4              // status bar + splash subtitle
 *     #define LOAD_GFXFF              // 480x320 only: GFX free fonts
 *     #define SPI_FREQUENCY 40000000  // F411 SPI1 max 50 MHz; 40 MHz leaves headroom
 *
 * The defines below only gate the display code in gpsdo_tasks.cpp —
 * driver selection happens in the TFT_eSPI User_Setup.h.              */
//#define GPSDO_TFT_ILI9341        /* ILI9341 240x320 SPI TFT */
//#define GPSDO_TFT_ST7789         /* ST7789  240x320 SPI TFT */
#define GPSDO_TFT_ILI9488        /* ILI9488 320x480 SPI TFT (480x320 landscape) */

/* TFT control pins (documentation — actual config is in User_Setup.h) */
#define PIN_TFT_SCK   PA5
#define PIN_TFT_MOSI  PA7
#define PIN_TFT_RST   PB15
#define PIN_TFT_DC    PB12
#define PIN_TFT_CS    PB13

/* ── OLED page alternation: seconds per page ─────────────────────────── */
#define OLED_PAGE_SWITCH_SECS   10u   /* flip between page A and B every N seconds */

/* ── LCD line-2 rotation: seconds per view ──────────────────────────── */
#define LCD_LINE2_SWITCH_SECS   10u   /* rotate line 2 content every N seconds     */

/* ── Remaining feature switches (sensors, comms, GPS timing, etc.) ───── */
#define GPSDO_AHT10
#define GPSDO_BMP280_I2C
#define GPSDO_INA219
//#define GPSDO_BLUETOOTH
/* GPSDO_BLUETOOTH_PARALLEL — USB + Bluetooth at the same time: telemetry, CLI
 * and boot output mirror to BOTH ports, and commands are accepted from either.
 * Needs the Serial2 wiring (PA2/PA3). This switch supersedes GPSDO_BLUETOOTH;
 * leave that one off when using parallel mode. */
#define GPSDO_BLUETOOTH_PARALLEL
#define GPSDO_VCC
#define GPSDO_VDD
#define GPSDO_UBX_CONFIG

/* Chinese u-blox CLONE (fake M8N etc.): talk NMEA only, configure nothing.
 * Clones answer the baud probe but ignore CFG-MSG/CFG-NAV5 and lack
 * survey-in and TIM-TP; the unanswered config stream can even upset their
 * auto-baud (module goes silent → "acquiring" until a T-tunnel re-probe).
 * With this defined the firmware probes the baud rate and sends NOTHING
 * else. Discipline is unaffected (PPS never needed UBX); you lose NMEA
 * silencing, stationary mode, survey-in/Time Mode and qErr.
 * Leave commented out for genuine u-blox modules. */
 
//#define GPSDO_FAKE_UBLOX

/* ── GPS timing module (LEA-6T / LEA-M8T) ─────────────────────────────
 *
 * Uncomment to enable Time Mode (survey-in → time-only fix) on a u-blox
 * timing receiver. Survey-in runs at every power-up; it averages the
 * antenna position, then switches to a fixed-position time solution with
 * a much cleaner 1PPS (single-satellite timing, no navigation jitter).
 *
 * Position is still reported in NMEA after survey-in (the frozen, averaged
 * fix), so location display and auto-timezone (TO A) keep working.
 *
 * Protocol: both LEA-6T and LEA-M8T (TIM 1.10, PROTVER 22) use the SAME
 * messages — CFG-TMODE2 (0x06 0x3D) to start survey-in and TIM-SVIN
 * (0x0D 0x04) to read progress. (CFG-TMODE3/NAV-SVIN is only on newer
 * high-precision firmware like NEO-M8P/ZED-F9P, not on these timing units;
 * verified in u-center against a LEA-M8T-0 / TIM 1.10.)
 *
 * NEO-M8T is fully compatible with LEA-M8T here (same M8 + FW3, same
 * CFG-TMODE2/TIM-SVIN) — no code change needed beyond this switch.
 *
 * ZED-F9T (Gen9) is also supported: ubx_start_survey_in() additionally tries
 * a CFG-VALSET (CFG-TMODE-* keys) variant, and the monitor falls back to
 * NAV-SVIN (0x01 0x3B) when TIM-SVIN does not answer. Tested on real hardware
 * by EEVblog user danieljw. The legacy CFG-NAV5 sent for stationary mode may
 * NAK on an F9T; that is tolerated (the survey-in path is independent).
 *
 * Survey-in ends when EITHER limit is met (whichever comes first):       */
#define GPSDO_GPS_TIMING     /* u-blox timing rx: LEA-6T / LEA/NEO-M8T / ZED-F9T */
#define GPSDO_SVIN_MIN_SECS   300u    /* minimum survey-in duration [s]     */
#define GPSDO_SVIN_ACC_LIMIT  5000u   /* position accuracy limit [mm] (5 m) */
#define GPSDO_PICDIV
/* GPSDO_LTIC REQUIRES THE PHYSICAL RAMP DETECTOR ON PA1. It is not a software
 * option: the algorithms behind it (10, 11 and 12) read a real voltage ramp
 * built by an external charge/discharge circuit — the PPS starts the ramp, the
 * OCXO-derived edge stops it, and PA1 samples what is left. Without that
 * circuit PA1 is a floating analog input, ADC noise is read as a phase in
 * nanoseconds, and the loop disciplines the oscillator against nothing.
 * Enable this ONLY if the detector hardware is fitted; a counter-only board
 * leaves it commented out and uses algorithms 0-9. See doc/MANUAL_*.md 1.3. */
#define GPSDO_LTIC           /* Lars' TIC: read Vphase on PA1, discharge 1nF capacitor */
/* GPSDO_EEPROM removed in v1.00: persistence is 100% flash ring
 * (settings_store + live_store, sector 7). See doc/FLASH_RING_BRINGUP_*. */
/* ---- Control-voltage output --------------------------------------------
 * Default is the 16-bit PWM on PIN_VCTL_PWM: about 50 uV per step at 3.3 V,
 * near 2.7e-11 fractional on a 5.3 Hz/V oscillator.
 *
 * GPSDO_DAC_EXT switches to the external AD5680 SPI DAC (18-bit, external
 * REF5045 reference): about 17 uV per step, near 9e-12 fractional, with no
 * filter delay in the loop. Bit-banged on PIN_DAC_SCK/MOSI/CS (PB0/PB2/PB4,
 * Dan Wiering's PCB routing; PB2 is BOOT1 - keep the MOSI trace pull-up free).
 * TM1637 and the 2 kHz generator are skipped at compile time when this is on.
 *
 * No hardware SPI is needed: the DAC is written once per second, so bit-banging
 * costs microseconds. See gpsdo_dac_ext.h for the pin proposal and the reasoning. */
 
/* GPSDO_PWM_DITHER replaces the plain 16-bit PWM with a shorter PWM whose duty
 * is dithered from period to period, giving 24 bits after the filter. Idea from
 * Alan Cashin (MIS42N); the table is replayed by DMA rather than run in an
 * interrupt, so it costs no measurable CPU.
 *
 * The point is the carrier, not the extra bits. At 13 bits the carrier is
 * 12.2 kHz against 2 kHz now, which lets the filter corner move from 0.7 Hz to
 * 4.2 Hz for the same ripple — a six-fold shorter time constant, and filter delay
 * goes straight into the loop as phase lag.
 *
 * Same pin (PB9, TIM4 CH4), so the existing filter and wiring are unchanged.
 * Costs 2 x 2^(24-N) x 2 bytes of RAM: 8 KB at 13 bits, 16 KB at 12. */
#define GPSDO_PWM_DITHER
#define GPSDO_PWM_DITHER_BITS  13   /* 12 = 24.4 kHz/16 KB, 13 = 12.2 kHz/8 KB */

#define GPSDO_DAC_EXT

/* ---- EFC span jumper on PB14 -------------------------------------------
 * A board with the EFC level shifter runs its oscillator over the full control
 * span or a reduced one, and the two have different plants — CT's K moved
 * 5.0-5.7x between them on Dan Wiering's prototype. With a second pole of the
 * span jumper wired to PB14, the firmware keeps one CT calibration per
 * position and switches between them when the jumper moves, remapping the
 * control code so the EFC pin keeps its voltage. See gpsdo_span.h.
 *
 * Jumper fitted = PB14 to ground = REDUCED. Open, or nothing wired = FULL,
 * which is the position that always works: a board without the wire reads
 * FULL on the internal pull-up and keeps its one calibration as before. That
 * is why this is on by default — it costs a pull-up on a pin nothing else
 * uses.
 *
 * Comment out only if PB14 is wired to something else on your board. The
 * stored calibrations are carried through every save either way. */
#define GPSDO_SPAN_SENSE
#define PIN_SPAN_SENSE  PB14

/* Historical bring-up aid; OFF by default per the v1.06 policy. */
/* #define GPSDO_GEN_2kHz_PB5 */


/* ── Sanity checks ───────────────────────────────────────────────────── */
#if defined(GPSDO_OLED_SH1106) && defined(GPSDO_OLED_SSD1306)
  #error "Select only one OLED type"
#endif
#if defined(GPSDO_OLED_SH1106) && defined(GPSDO_OLED_SSD1309)
  #error "Select only one OLED type"
#endif
#if defined(GPSDO_OLED_SSD1306) && defined(GPSDO_OLED_SSD1309)
  #error "Select only one OLED type"
#endif
#if defined(GPSDO_TM1637) && defined(GPSDO_TM1637_6)
  #error "Select only one TM1637 mode: GPSDO_TM1637 (4-digit) or GPSDO_TM1637_6 (6-digit)"
#endif
#if defined(GPSDO_LCD_20x4) && (defined(GPSDO_TM1637) || defined(GPSDO_TM1637_6))
  #error "GPSDO_LCD_20x4 and TM1637 cannot be used together (I2C/GPIO conflict). Disable one."
#endif
/* The external DAC's chip-select shares PB4 with the TM1637 data line. Both are
 * plain GPIO driven by software, so nothing detects the clash at run time: the
 * display would corrupt every DAC write and the control voltage would jump
 * whenever the clock updated. Caught here instead.
 *
 * If a board needs both, move PIN_DAC_CS to PC14 or PC15 (free unless the
 * 32 kHz crystal is fitted). NOT PB14 while GPSDO_SPAN_SENSE is on — that is
 * the span jumper's input now, and gpsdo_span.cpp refuses to compile the
 * collision. Nothing else in the firmware depends on which. NOT PA4 (SPI1_NSS,
 * the display's chip select), NOT PA6
 * (TFT_eSPI wants it as TFT_MISO), NOT PB6/PB7 (I2C1), NOT PA9/PA10 (Serial1,
 * the GPS). And note there is no free ADC channel at all — see the pin audit
 * in gpsdo_dac_ext.h. */
/* BOTH MAY NOW BE COMPILED TOGETHER. They used to be mutually exclusive because
 * both drive the control voltage — but the thing that must not happen is two
 * drivers fighting over one node, and that is settled by the JUMPERS on the
 * board, not by the build. Compiling both lets one binary serve either wiring
 * and lets the DAC command switch between them at runtime for a back-to-back
 * comparison, which is otherwise a reflash apart. The pins do not collide:
 * dither is PB9/TIM4, the AD5680 is PB4/PB0/PB2. See gpsdo_dac.h. */
/* With the external DAC on, TM1637 and the 2 kHz generator yield their pins
 * instead of failing the build: TM1637's data line is the DAC's chip-select
 * (PB4), and both options are historical anyway — the HT16K33 on I2C is the
 * maintained clock display, and the generator was a bring-up aid. The v1.06
 * policy keeps both OFF by default regardless (see the defines above). */
#if defined(GPSDO_DAC_EXT)
  #undef GPSDO_TM1637
  #undef GPSDO_TM1637_6
  #undef GPSDO_GEN_2kHz_PB5
#endif
/* exactly one TFT driver */
#if (defined(GPSDO_TFT_ILI9341) + defined(GPSDO_TFT_ST7789) + defined(GPSDO_TFT_ILI9488)) > 1
  #error "Select only one TFT type: GPSDO_TFT_ILI9341, GPSDO_TFT_ST7789 or GPSDO_TFT_ILI9488"
#endif

/* ── Convenience alias: any TFT defined ─────────────────────────────── */
#if defined(GPSDO_TFT_ILI9341) || defined(GPSDO_TFT_ST7789) || defined(GPSDO_TFT_ILI9488)
  #define GPSDO_TFT
#endif

/* ── TFT backlight dimming on PB5 (TIM3 CH2) ─────────────────────────────
 *
 * Enabled automatically with any TFT, because PB5 and TIM3 are otherwise idle
 * and a board without the MOSFET stage simply has a square wave on a spare
 * pin — inert. The `BL` command sets 30..100 % and auto-saves. See
 * gpsdo_backlight.h for the stage, and why 100 % is the quietest setting
 * rather than the loudest.
 *
 * IT YIELDS TO THE 2 kHz GENERATOR, not the other way round. Both want PB5,
 * and the generator is an explicit opt-in a person had to type; the backlight
 * is on by default. A default must never take a pin away from a deliberate
 * choice, so the deliberate one wins and nothing has to #error. (Under
 * GPSDO_DAC_EXT the generator has already been undefined just above, so on
 * those boards the question does not arise.) */
#if defined(GPSDO_TFT) && !defined(GPSDO_GEN_2kHz_PB5)
  #define GPSDO_TFT_BL_PWM
#endif

/* ── TFT panel geometry & layout scaling ────────────────────────────────
 * The layout is authored for the 320x240 landscape panels. The ILI9488 is
 * 480x320 landscape. Note the axes scale by DIFFERENT factors:
 *   width  320 → 480  = 3/2  (1.50x)
 *   height 240 → 320  = 4/3  (1.33x)
 * so X and Y must scale independently, or the bottom status bar would run
 * off the screen. TFT_SX scales an X/width value, TFT_SY a Y/height value.
 * TFT_S (used where the original code mixed the two for padding/among small
 * deltas) uses the X factor. On the small panels everything is identity. */
#if defined(GPSDO_TFT_ILI9488)
  #define TFT_W         480
  #define TFT_H         320
  #define TFT_SX_N      3       /* width  3/2 */
  #define TFT_SX_D      2
  #define TFT_SY_N      4       /* height 4/3 */
  #define TFT_SY_D      3
#else
  #define TFT_W         320
  #define TFT_H         240
  #define TFT_SX_N      1
  #define TFT_SX_D      1
  #define TFT_SY_N      1
  #define TFT_SY_D      1
#endif
/* scale X (width/horizontal) and Y (height/vertical), integer rounded */
#define TFT_SX(v)  (((int)(v) * TFT_SX_N + (TFT_SX_D/2)) / TFT_SX_D)
#define TFT_SY(v)  (((int)(v) * TFT_SY_N + (TFT_SY_D/2)) / TFT_SY_D)
/* generic scale for sizes/padding not tied to an axis — uses the X factor */
#define TFT_S(v)   TFT_SX(v)

/* ── GFX Free Fonts (GFXFF) ──────────────────────────────────────────────
 * The display text is drawn with the Adafruit GFX free fonts bundled with
 * TFT_eSPI (proportional FreeSans / monospace FreeMono, 9/12/18/24 pt). This
 * replaced the old numeric GLCD fonts (1,2,4,6,7,8): fonts 6/7/8 contain ONLY
 * the glyphs 0-9 . : - a p m, so any lettered string drawn in them collapsed
 * to whatever numeric-font glyph happened to exist — e.g. "GPS Disciplined
 * OCXO" in font 6 rendered as a lone "p". GFXFF has the full character set and
 * looks far cleaner on the big panel.
 *
 * `#define LOAD_GFXFF` in User_Setup.h is needed for the 480×320 build only.
 * The font structs are pulled in automatically by the library — do NOT
 * #include any Fonts/GFXFF/*.h yourself, that double-defines them. Both builds
 * need LOAD_GLCD (font 1); the 320×240 build also needs LOAD_FONT2 and
 * LOAD_FONT4, and nothing else.
 *
 * Fonts are chosen per ROLE. The layout is authored once and TFT_SX/TFT_SY
 * scale the coordinates for both panels — but the two panels do NOT use the
 * same faces. GFXFF is proportional and too wide for the 320×240 operating
 * screen: values overran their columns (tried in v0.92, reverted in v0.93),
 * and there is nothing smaller to fall back on (FreeSans starts at 9pt; below
 * it is only the unreadable 3×5 TomThumb). So the small panel keeps the
 * classic numeric fonts it was authored for, while the 480×320 panel — which
 * has the room — uses GFXFF throughout. The splash is classic on both, so that
 * upgrading from an older build needs no new User_Setup lines. TFT_FONT_*
 * (below) apply the choice; GF_* name the GFXFF faces.
 *
 *   Role        320×240            480×320            purpose
 *   ─────────   ────────────────   ────────────────   ────────────────────────
 *   data grid   classic font2      FreeSans    9pt    grid labels + values
 *   header      classic font2      FreeSans    9pt    header (name/ver + LMT)
 *   status bar  classic font4      FreeSansBold 12pt  bottom status bar (half-height)
 *   frequency   classic font1 ×3   FreeMonoBold 24pt  big frequency (both fixed-width)
 *   splash sub  classic font4      FreeSansBold 12pt  "GPS Disciplined OCXO"
 *   credits     classic font1      classic font1      splash credit lines
 *
 * FreeMono is used for the frequency on the big panel so the digits are
 * fixed-width and don't shuffle as the value changes (classic font 1 is
 * already fixed-width, so the small panel gets this for free). Grid values use
 * FreeSans but are right-datum anchored (see tft_val_r) so changing widths
 * stay pinned. */

/* GFX free-font choices — 480×320 only. */
#if defined(GPSDO_TFT_ILI9488)
  #define GF_DATA    &FreeSans9pt7b
  #define GF_HEAD    &FreeSans9pt7b
  #define GF_STATUS  &FreeSansBold12pt7b
  #define GF_FREQ    &FreeMonoBold24pt7b
#endif

/* ── Font application: GFX on the big panel, classic on the small one ─────
 * The GFX free fonts are proportional and comparatively wide. On the 480×320
 * panel they look good and there is room for them. On 320×240 the same layout
 * in FreeSans overflowed its columns — values ran into the neighbouring column
 * and the centre divider cut through the text — so the small panel goes back
 * to the classic numeric fonts it was authored for. The splash keeps GFX on
 * both panels (its layout has the room, and it reads well).
 *
 * TFT_FONT_DATA / _HEAD / _STATUS take a TFT_eSPI-ish object (the panel or a
 * sprite) and select the right font for this build. On the classic path they
 * set a plain font number; drawString() then needs no font argument because
 * setTextFont() has already applied it.
 *
 * The frequency is special: the classic path uses font 1 scaled ×3 (18×24),
 * which is fixed-width, so it needs setTextSize() too — see TFT_FONT_FREQ_ON
 * / TFT_FONT_FREQ_OFF, which must bracket the drawString. */
#if defined(GPSDO_TFT_ILI9488)
  #define TFT_FONT_DATA(o)    (o).setFreeFont(GF_DATA)
  #define TFT_FONT_HEAD(o)    (o).setFreeFont(GF_HEAD)
  #define TFT_FONT_STATUS(o)  (o).setFreeFont(GF_STATUS)
  #define TFT_FONT_FREQ_ON(o)  (o).setFreeFont(GF_FREQ)
  #define TFT_FONT_FREQ_OFF(o) do { } while (0)
#else
  #define TFT_FONT_DATA(o)    (o).setTextFont(2)
  #define TFT_FONT_HEAD(o)    (o).setTextFont(2)
  #define TFT_FONT_STATUS(o)  (o).setTextFont(4)
  #define TFT_FONT_FREQ_ON(o)  do { (o).setTextFont(1); (o).setTextSize(3); } while (0)
  #define TFT_FONT_FREQ_OFF(o) (o).setTextSize(1)
#endif

/* ── Freq-band sprite RAM budget ─────────────────────────────────────────
 * The frequency readout is drawn to a 4-bit TFT_eSprite (double buffer) then
 * pushed to the panel in one transfer — eliminates the per-second flicker
 * caused by setTextPadding erasing ~16 k px on the live panel before each
 * redraw. The sprite covers only the freq band (between the header separator
 * and the freq separator); the rest of the screen stays direct-draw.
 *   ILI9488 (480x320):  480 × 48 px × 0.5 B = 11.2 KB
 *   ILI9341/ST7789:     320 × 48 px × 0.5 B =  7.5 KB
 * Both well within the 128 KB RAM of the F411. If heap is too low at runtime
 * createSprite() returns nullptr and the code falls back to direct-draw
 * (flicker returns, but the screen still works). */

/* ── Convenience alias: any OLED defined ────────────────────────────── */
#if defined(GPSDO_OLED_SH1106) || defined(GPSDO_OLED_SSD1306) || defined(GPSDO_OLED_SSD1309)
  #define GPSDO_OLED
#endif

/* ── Derived macros (must come AFTER all feature switches) ────────────────
 * OUT_SERIAL depends on the comms switches, so it is evaluated here — after
 * they are defined above. In parallel mode all user-facing output goes to the
 * tee (USB + Bluetooth); with plain Bluetooth it goes to Serial2; otherwise to
 * USB Serial (still available for low-level boot diagnostics regardless).
 * CLI_SERIAL and REPORT_SERIAL follow the same pattern in their .cpp files. */
#if defined(GPSDO_BLUETOOTH_PARALLEL)
  /* g_tee is a TeeSerial (defined in gpsdo_tee_serial.h). We must NOT include that
   * header here — this file is inside an extern "C" block and gpsdo_tee_serial.h pulls
   * in Arduino.h with C++ templates, which are illegal under C linkage. Each
   * .cpp that uses OUT_SERIAL includes gpsdo_tee_serial.h itself, outside extern "C". */
  #define OUT_SERIAL g_tee
#elif defined(GPSDO_BLUETOOTH)
  #define OUT_SERIAL Serial2
#else
  #define OUT_SERIAL Serial
#endif

/* ── Pin definitions ─────────────────────────────────────────────────── */
#define PIN_BLUE_LED     PC13
#define PIN_YELLOW_LED   PB8
#define PIN_VCTL_PWM     PB9
#define PIN_VCTL_ADC     PB1
#define PIN_VCC_DIV2     PA0
#define PIN_OCXO_ETR     PA15
#define PIN_PPS_CAPTURE  PB10
#define PIN_TEST_2KHZ    PB5
#define PIN_TFT_BL       PB5   /* backlight PWM — THE SAME PIN as the 2 kHz   */
                               /* generator; see the yield rule further up    */
#define PIN_PICDIV_ARM   PB3
#define PIN_BT_RX        PA3   /* Bluetooth UART (Serial2) RX — module TX  */
#define PIN_BT_TX        PA2   /* Bluetooth UART (Serial2) TX — module RX  */

/* ── LTIC (Lars TIC) ────────────────────────────────────────────────── */
#ifdef GPSDO_LTIC
  #define PIN_LTIC_VPHASE   PA1   /* ADC input + open-drain output to discharge 1nF cap */
  #define LTIC_DISCHARGE_MS   1   /* ms to discharge 1nF capacitor via open-drain low   */
  #define LTIC_OVERSAMPLE    16   /* fast ADC reads per PPS, median taken (glitch-proof) */
  /* How many consecutive railed cycles with no improvement in |e_freq| before
   * the loop declares a runaway and freezes. A cold or far-off OCXO rails the
   * detector while it is legitimately being pulled in, so the guard waits for
   * evidence that the correction is NOT working rather than firing on a large
   * error alone. ACQ corrects every 5 s, so 5 cycles is roughly 25 s of no
   * progress. Raise it if a very sluggish OCXO gets frozen mid-acquisition. */
  #define LTIC_RUNAWAY_STALL  5
  /* Active discharge for the CURRENT-SOURCE ramp detector variant. Leave this
   * commented out for the classic RC Kaashoek detector — that one self-clears
   * through leakage and driving PA1 low would corrupt the measured charge. Only
   * enable it if you have rebuilt the front end as a gated current integrator
   * (diode-hold cap with no leakage path), where the cap must be actively zeroed
   * after each read. LTIC_RESET_US is the low-pulse width; a few microseconds is
   * plenty to sink the cap through PA1's drive. */
  #define GPSDO_LTIC_ACTIVE_RESET   /* current-source variant only — see docs */
  #define LTIC_RESET_US       5   /* PA1 low-pulse width to zero the hold cap (us)      */
  /* Legacy voltage→time calibration constant for the TIC ramp. The TIC charges
   * a 1 nF cap with a constant current during the GPS-1PPS → OCXO-1PPS interval,
   * so the latched voltage tracks the phase difference. This compile-time
   * constant is a FALLBACK only: the `LC` command measures the real slope per
   * board at runtime and stores ns_per_volt / zero_offset / range_ns in the
   * live parameters (flash ring), which is what algorithm 10 and the
   * displays actually use. Leave at 0; displays then show volts until LC runs,
   * after which they show ns from the measured slope. */
  #define LTIC_NS_PER_VOLT    0.0f /* 0 = use measured LC calibration (normal) */
  /* Self-calibration (LC command) parameters. LC forces a small PWM offset so
   * the phase ramps linearly across the detector, measures the ramp rate from
   * the TIM2 frequency error, and regresses the TIC voltage vs time to get the
   * slope. LTIC_CAL_PWM_OFFSET sets how hard to push (bigger = faster ramp,
   * but must stay inside the OCXO's pull range); LTIC_CAL_SECS is how long to
   * log. A narrow detector (small ns range) needs a SLOW ramp or the phase
   * wraps every second and every sample rails — LC scales this offset down
   * automatically when it measures a fast drift, but the starting value is kept
   * small (60 LSB) so it is gentle by default. */
  #define LTIC_CAL_PWM_OFFSET   70   /* LSB added to centre PWM during LC  */ 
  #define LTIC_CAL_SECS        300u  /* seconds of ramp logging. Sized for a    
                                      /* wide detector window: LVC74 @ 3.3 V
                                      * spans ~700+ ns, so at ~4 ns/s the sweep
                                      * needs ≥ ~180 s of clean ramp plus margin
                                      * for a railed start. 300 s covers ~1200
                                      * ns with room to spare. (HC74 @ 5 V was
                                      * narrower and fit in 180 s.)            */
  #define LTIC_DET_PERIOD_NS  100.0  /* unambiguous range of the phase detector */
                                      /* = one period of its clock. The xx74
                                      * flip-flop is clocked at 10 MHz here, so
                                      * 100 ns BY CONSTRUCTION — a physical
                                      * constant, not something to estimate.
                                      * (Lars' HC4046 at 1 MHz would be 1000.) */
  /* Operating-point anchor for the calibration (Option D).
   *
   * The ramp detector is exponential, so ns/V is NOT constant along it, and a
   * whole-transit average (range/span) depends on where the picDIV arm happened
   * to park the phase — back-to-back runs disagreed ~20 %. Two full 1 s-resolved
   * runs showed the local slope dV/dt is REPEATABLE to ~0.3 % in a narrow band
   * near ~1.85 V and diverges above and below it. That voltage is the physical
   * sweet spot of THIS detector (Vsat·(1−1/e) ≈ 0.63·Vsat, near the middle of
   * its usable range and clear of Dan Wiering's measured dead zones: the diode
   * drop + pull-down below ~0.05 V, and the ADC rail/wraparound near 3.3 V).
   * We anchor the loop's zero_offset at 0.632·Vsat (measured per board by LC)
   * and read ns/V from the local slope in a ±window around it, instead of
   * averaging the whole ramp.
   *
   * NOTE: on the reference board (LVC74 @ 3.3 V, 1k/1n ramp) that sweet spot
   * lands near 1.85 V, since Vsat ≈ 2.93 V. LC now derives the anchor from the
   * measured Vsat rather than a fixed constant, so it adapts to any detector;
   * see do_ltic_calibrate() in gpsdo_control.cpp. */
  #define LTIC_ANCHOR_WIN_V   0.20f  /* half-width for the local-slope fit [V] */
#define LTIC_CAL_MIN_POINTS   12   /* reject fit with fewer samples          */
#endif

/* ── OCXO / frequency ────────────────────────────────────────────────── */
#define BASE_FREQ        10000000UL
#define FREQ_LOWER       9999500UL
#define FREQ_UPPER       10000500UL
#define CIRCBUF_SIZE     20000u

/* ── Timing ──────────────────────────────────────────────────────────── */
#define OCXO_WARMUP_SECS    300u
#define OCXO_CALIB_SECS      60u
#define TUNNEL_TIMEOUT_SECS 300u
#define POS_LOST_TIMEOUT_MS 10000u   /* ms without position fix before clearing pos_valid */
#define PICDIV_ARM_MS       1001u

/* ── Holdover blink period (ms) ──────────────────────────────────────── */
#define HOLDOVER_BLINK_MS   500u   /* OLED/LCD [H] blink period (ms) */
#define LED_SLOW_BLINK_MS  1000u   /* Yellow LED slow pulse — manual holdover */
#define LED_FAST_BLINK_MS   200u   /* Yellow LED fast pulse — fix lost / auto holdover */

/* ── Default PWM (pre-calibration start point) ────────────────────────
 *
 * The OCXO type no longer needs to be selected: the CT (Calibrate & Tune)
 * command measures the actual plant gain K and derives both the 10 MHz PWM
 * and all PID coefficients for whatever oscillator is fitted.
 *
 * Before the first CT run, the loop starts from a universal mid-range
 * value. PWM DAC output is 0..3.3 V (STM32 Vcc); 32767 is the 16-bit
 * midpoint ≈ 1.65 V, a safe neutral start for any 0..4 V EFC oscillator.
 * Run CT once (then ES to save) to centre PWM and tune for your unit.
 * ──────────────────────────────────────────────────────────────────── */
#define DEFAULT_PWM_OUTPUT  32767u   /* 16-bit midpoint ~1.65 V */

/* ── RTOS task priorities ────────────────────────────────────────────── */
#define PRI_ISR_RELAY    (configMAX_PRIORITIES - 1)
#define PRI_CONTROL      (configMAX_PRIORITIES - 2)
#define PRI_GPS          (configMAX_PRIORITIES - 3)
#define PRI_CLI          (configMAX_PRIORITIES - 4)
#define PRI_SENSORS      (configMAX_PRIORITIES - 5)
#define PRI_DISPLAY      (configMAX_PRIORITIES - 6)
#define PRI_UPTIME       (configMAX_PRIORITIES - 7)

/* ── RTOS stack sizes (words = 4 bytes each) ─────────────────────────────
 *
 * FreqRelayTask: small — only PpsEvent processing + ring buffer math,
 *                no FP, no snprintf, no I2C.              192 words = 768B
 * ControlTask:   medium — calibration loop, PID math (double),
 *                ADC reads, no large locals.               384 words = 1.5KB
 * GpsTask:       medium — TinyGPS++ state machine,
 *                NMEA buffer (256B internal to TinyGPS++). 384 words = 1.5KB
 * CliTask:       small — 64-byte line buffer, strcmp dispatch.
 *                                                          256 words = 1KB
 * SensorTask:    medium — Adafruit I2C libs use heap but also stack.
 *                                                          384 words = 1.5KB
 * DisplayTask:   large — 640-byte serial TX buffer (static),
 *                snprintf scratch, OLED/LCD line buffers.  512 words = 2KB
 * UptimeTask:    tiny — just increments and formats two strings.
 *                                                          192 words = 768B
 * ─────────────────────────────────────────────────────────────────────── */
#define STACK_ISR_RELAY  192
#define STACK_CONTROL    832   /* writes the ring too: CT auto-save, live_store */
#define STACK_GPS        384
/* CLI and CONTROL both write the flash ring, and that path is stack-hungry:
 * fr_write() builds a 512-byte slot image and a second 512-byte read-back copy
 * on the stack, and settings_store adds a ~324-byte SettingsBlock_t on top —
 * about 1.4 KB before any call frames. The old 1 KB CLI stack overflowed and
 * corrupted its neighbour: the symptom was the board printing the save
 * confirmation and then hanging, with the display frozen. Sized with ~1 KB of
 * head-room over the measured worst case; check with SW after any change. */
#define STACK_CLI        768
#define STACK_SENSORS    384
#ifdef GPSDO_TFT
  #define STACK_DISPLAY  1024   /* TFT_eSPI font rendering + scaled fonts + */
                                /* OLED clear loop need generous headroom;
                                * 768 was marginal and caused intermittent
                                * boot hangs (no stack-overflow hook). */
#else
  #define STACK_DISPLAY  512
#endif
#define STACK_UPTIME     192

#ifdef __cplusplus
}
#endif

/* g_tee declaration for parallel USB+Bluetooth. This sits AFTER the extern "C"
 * block above, because gpsdo_tee_serial.h pulls in Arduino.h (C++ templates), which is
 * illegal under C linkage. Every .cpp includes this config header, so declaring
 * g_tee here makes the OUT_SERIAL macro resolve everywhere without touching each
 * file. The single definition lives in the .ino. */
#if defined(__cplusplus) && defined(GPSDO_BLUETOOTH_PARALLEL)
  #include "gpsdo_tee_serial.h"
  extern TeeSerial g_tee;
#endif
