/**
 * gpsdo_config.h — Compile-time configuration
 *
 * Part of GPSDO FreeRTOS v0.95
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
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

/* ── Version ─────────────────────────────────────────────────────────── */
#define PROGRAM_NAME     "GPSDO"
#define PROGRAM_VERSION  "v0.95-rtos"

/* ---- Serial output macro ----
 * OUT_SERIAL routes user-facing output to Serial2 (Bluetooth) or Serial
 * (USB). It depends on GPSDO_BLUETOOTH, so it is defined LATER in this file,
 * AFTER all feature switches — otherwise GPSDO_BLUETOOTH would not yet be
 * visible here and OUT_SERIAL would always resolve to USB. See the
 * "Derived macros" section below. */
#define AUTHOR_NAME      "Andre Balsa"   /* v0.06c author — inspiration for the RTOS port (ASCII for serial) */

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

/* ── HT16K33 clock display — 4-digit 7-seg with colon, I2C ────────────
 * Common AliExpress/Adafruit-style 0.56" clock modules (addr 0x70).
 * Shows HH:MM with the colon blinking each second.  Pure I2C device —
 * shares the bus with OLED/LCD/sensors, no extra pins, no conflicts.   */
#define GPSDO_HT16K33            /* 4-digit HT16K33: HH:MM                 */
#define HT16K33_I2C_ADDR  0x70   /* default; A0/A1/A2 jumpers raise it     */
#define HT16K33_BRIGHTNESS  8    /* 0 (dim) .. 15 (max)                    */

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
/* #define GPSDO_TFT_ILI9341 */  /* ILI9341 240x320 SPI TFT */
//#define GPSDO_TFT_ST7789         /* ST7789  240x320 SPI TFT */
#define GPSDO_TFT_ILI9488   /* ILI9488 320x480 SPI TFT (480x320 landscape)
                                  * — UNTESTED: no panel on hand yet. Layout is
                                  * the 320x240 design scaled up ~1.5x. Set the
                                  * matching driver + TFT_WIDTH 320 / TFT_HEIGHT
                                  * 480 in TFT_eSPI User_Setup.h. ILI9488 over
                                  * SPI is slow (18-bit colour, 480x320); expect
                                  * a more visible repaint than on the small
                                  * panels. */

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
#define GPSDO_VCC
#define GPSDO_VDD
#define GPSDO_UBX_CONFIG

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
#define GPSDO_LTIC           /* Lars' TIC: read Vphase on PA1, discharge 1nF capacitor */
#define GPSDO_EEPROM
#define GPSDO_GEN_2kHz_PB5


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
/* exactly one TFT driver */
#if (defined(GPSDO_TFT_ILI9341) + defined(GPSDO_TFT_ST7789) + defined(GPSDO_TFT_ILI9488)) > 1
  #error "Select only one TFT type: GPSDO_TFT_ILI9341, GPSDO_TFT_ST7789 or GPSDO_TFT_ILI9488"
#endif

/* ── Convenience alias: any TFT defined ─────────────────────────────── */
#if defined(GPSDO_TFT_ILI9341) || defined(GPSDO_TFT_ST7789) || defined(GPSDO_TFT_ILI9488)
  #define GPSDO_TFT
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
 * OUT_SERIAL depends on GPSDO_BLUETOOTH, so it has to be evaluated here —
 * after the switch is defined above — not near the top of the file. When
 * Bluetooth is enabled all user-facing output goes to Serial2; otherwise to
 * USB Serial (still available for low-level boot diagnostics regardless).
 * CLI_SERIAL and REPORT_SERIAL are defined locally in their .cpp files. */
#ifdef GPSDO_BLUETOOTH
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
#define PIN_PICDIV_ARM   PB3

/* ── LTIC (Lars TIC) ────────────────────────────────────────────────── */
#ifdef GPSDO_LTIC
  #define PIN_LTIC_VPHASE   PA1   /* ADC input + open-drain output to discharge 1nF cap */
  #define LTIC_DISCHARGE_MS   1   /* ms to discharge 1nF capacitor via open-drain low   */
  #define LTIC_OVERSAMPLE    16   /* fast ADC reads per PPS, median taken (glitch-proof) */
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
  #define LTIC_CAL_PWM_OFFSET   70   /* LSB added to centre PWM during LC      */
  #define LTIC_CAL_SECS        300u  /* seconds of ramp logging. Sized for a
                                      * wide detector window: LVC74 @ 3.3 V
                                      * spans ~700+ ns, so at ~4 ns/s the sweep
                                      * needs ≥ ~180 s of clean ramp plus margin
                                      * for a railed start. 300 s covers ~1200
                                      * ns with room to spare. (HC74 @ 5 V was
                                      * narrower and fit in 180 s.)            */
  #define LTIC_DET_PERIOD_NS  100.0  /* unambiguous range of the phase detector
                                      * = one period of its clock. The xx74
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
#define STACK_CONTROL    384
#define STACK_GPS        384
#define STACK_CLI        256
#define STACK_SENSORS    384
#ifdef GPSDO_TFT
  #define STACK_DISPLAY  1024  /* TFT_eSPI font rendering + scaled fonts +
                                * OLED clear loop need generous headroom;
                                * 768 was marginal and caused intermittent
                                * boot hangs (no stack-overflow hook). */
#else
  #define STACK_DISPLAY  512
#endif
#define STACK_UPTIME     192

#ifdef __cplusplus
}
#endif
