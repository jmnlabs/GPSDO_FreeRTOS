/**
 * gpsdo_config.h — Compile-time configuration
 *
 * Part of GPSDO FreeRTOS v0.47
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
#define PROGRAM_VERSION  "v0.47-rtos"

/* ---- Serial output macro ----
 * When GPSDO_BLUETOOTH is defined, all user-facing output goes to Serial2.
 * USB Serial is still available for low-level boot diagnostics.
 * CLI_SERIAL and REPORT_SERIAL are defined locally in their .cpp files. */
#ifdef GPSDO_BLUETOOTH
  #define OUT_SERIAL Serial2
#else
  #define OUT_SERIAL Serial
#endif
#define AUTHOR_NAME      "André Balsa"

/* ── Feature switches ────────────────────────────────────────────────── */

/* ── OLED display type — select exactly one, or comment all out ──────── */
//#define GPSDO_OLED_SH1106        /* SH1106  128x64 I2C — original hardware */
/* #define GPSDO_OLED_SSD1306 */ /* SSD1306 128x64 I2C                     */
#define GPSDO_OLED_SSD1309       /* SSD1309 128x64 I2C (same init as 1306) */

/* ── LCD 20x4 I2C — independent of OLED, enable or comment out ──────── */
//#define GPSDO_LCD_20x4     /* HD44780 20x4 via PCF8574T I2C expander */

/* ── TM1637 clock display — select exactly one, or comment both out ──── */
#define GPSDO_TM1637_6           /* 6-digit TM1637: HH:MM:SS               */
/* #define GPSDO_TM1637      */  /* 4-digit TM1637: HH:MM                  */

/* ── HT16K33 clock display — 4-digit 7-seg with colon, I2C ────────────
 * Common AliExpress/Adafruit-style 0.56" clock modules (addr 0x70).
 * Shows HH:MM with the colon blinking each second.  Pure I2C device —
 * shares the bus with OLED/LCD/sensors, no extra pins, no conflicts.   */
#define GPSDO_HT16K33            /* 4-digit HT16K33: HH:MM                 */
#define HT16K33_I2C_ADDR  0x70   /* default; A0/A1/A2 jumpers raise it     */
#define HT16K33_BRIGHTNESS  8    /* 0 (dim) .. 15 (max)                    */

/* ── TFT 240x320 SPI display — select exactly one, or comment both out ─
 *
 * Cheap ILI9341 / ST7789 modules driven by the TFT_eSPI library over
 * hardware SPI1.  Landscape orientation (320x240).  Independent of the
 * I2C displays (OLED/LCD) — all can run simultaneously.
 *
 * Wiring (fixed, hardware SPI1):
 *   SCK  → PA5   (SPI1 SCLK)
 *   SDI  → PA7   (SPI1 MOSI)
 *   RES  → PB15
 *   D/C  → PB12
 *   CS   → PB13
 *
 * TFT_eSPI REQUIRES library-side configuration.  Edit User_Setup.h in
 * the TFT_eSPI library folder (Arduino/libraries/TFT_eSPI/) to contain:
 *
 *   #define ST7789_DRIVER          // or ILI9341_DRIVER
 *   #define TFT_WIDTH  240
 *   #define TFT_HEIGHT 320
 *   #define TFT_MISO PA6      // required by TFT_eSPI on STM32 even for write-only
 *   #define TFT_MOSI PA7
 *   #define TFT_SCLK PA5
 *   #define TFT_CS   PB13
 *   #define TFT_DC   PB12
 *   #define TFT_RST  PB15
 *   #define TFT_RGB_ORDER TFT_BGR   // colour order Blue-Green-Red
 *   #define TFT_INVERSION_OFF       // fixes inverted colours on some ST7789 modules
 *   #define LOAD_GLCD
 *   #define LOAD_FONT2
 *   #define LOAD_FONT4
 *   #define SPI_FREQUENCY 27000000
 *
 * The defines below only gate the display code in gpsdo_tasks.cpp —
 * driver selection happens in the TFT_eSPI User_Setup.h.              */
/* #define GPSDO_TFT_ILI9341 */  /* ILI9341 240x320 SPI TFT */
#define GPSDO_TFT_ST7789         /* ST7789  240x320 SPI TFT */

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
#if defined(GPSDO_TFT_ILI9341) && defined(GPSDO_TFT_ST7789)
  #error "Select only one TFT type: GPSDO_TFT_ILI9341 or GPSDO_TFT_ST7789"
#endif

/* ── Convenience alias: any TFT defined ─────────────────────────────── */
#if defined(GPSDO_TFT_ILI9341) || defined(GPSDO_TFT_ST7789)
  #define GPSDO_TFT
#endif

/* ── Convenience alias: any OLED defined ────────────────────────────── */
#if defined(GPSDO_OLED_SH1106) || defined(GPSDO_OLED_SSD1306) || defined(GPSDO_OLED_SSD1309)
  #define GPSDO_OLED
#endif

#define GPSDO_PWM_DAC
#define GPSDO_AHT10
#define GPSDO_BMP280_I2C
#define GPSDO_INA219
#define GPSDO_BLUETOOTH
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
 * Survey-in ends when EITHER limit is met (whichever comes first):       */
#define GPSDO_GPS_TIMING       /* u-blox LEA-6T / LEA-M8T timing receiver */
#define GPSDO_SVIN_MIN_SECS   300u    /* minimum survey-in duration [s]   */
#define GPSDO_SVIN_ACC_LIMIT  5000u   /* position accuracy limit [mm] (5 m) */


#define GPSDO_PICDIV
/* #define GPSDO_LTIC */          /* Lars' TIC: read Vphase on PA1, discharge 1nF capacitor */
#define GPSDO_EEPROM
#define GPSDO_GEN_2kHz_PB5

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
  #define LTIC_AVG_SAMPLES   10   /* moving average window                               */
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
