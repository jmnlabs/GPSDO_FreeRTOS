/**
 * gpsdo_config.h — Compile-time configuration
 *
 * Part of GPSDO FreeRTOS v0.29
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

/* ── Serial buffers (before HardwareSerial include) ─────────────────────
 * Default STM32duino TX buffer = 64 bytes.
 * A full human-readable report is ~500 bytes.
 * With 512-byte TX buffer, Serial.write(buf, len) returns after one DMA
 * kick without blocking — so the display task never stalls on serial TX.
 * RX 256 is sufficient for CLI commands (longest < 20 bytes).
 * ────────────────────────────────────────────────────────────────────── */
#define SERIAL_TX_BUFFER_SIZE 512
#define SERIAL_RX_BUFFER_SIZE 256

/* ── Version ─────────────────────────────────────────────────────────── */
#define PROGRAM_NAME     "GPSDO"
#define PROGRAM_VERSION  "v0.29-rtos"

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
//#define GPSDO_OLED_SSD1306       /* SSD1306 128x64 I2C                     */
//#define GPSDO_OLED_SSD1309       /* SSD1309 128x64 I2C (same init as 1306) */

/* ── LCD 20x4 I2C — independent of OLED, enable or comment out ──────── */
#define GPSDO_LCD_20x4     /* HD44780 20x4 via PCF8574T I2C expander */

/* ── TM1637 clock display — select exactly one, or comment both out ──── */
//#define GPSDO_TM1637_6           /* 6-digit TM1637: HH:MM:SS               */
//#define GPSDO_TM1637             /* 4-digit TM1637: HH:MM                  */

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

/* ── Convenience alias: any OLED defined ────────────────────────────── */
#if defined(GPSDO_OLED_SH1106) || defined(GPSDO_OLED_SSD1306) || defined(GPSDO_OLED_SSD1309)
  #define GPSDO_OLED
#endif

#define GPSDO_PWM_DAC
#define GPSDO_AHT10
//#define GPSDO_BMP280_I2C
#define GPSDO_INA219
#define GPSDO_BLUETOOTH
#define GPSDO_VCC
#define GPSDO_VDD
#define GPSDO_UBX_CONFIG
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

/* ── OCXO selection ───────────────────────────────────────────────────
 *
 * Uncomment exactly ONE OCXO define.  It sets compile-time defaults for:
 *   - PID coefficients (Kp, Ki, Kd, I_LIMIT) for algorithms 3-9
 *   - Algorithm 8 blend crossover / scale
 *   - Algorithm 9 NN max step
 *
 * Both OCXOs: supply 5 V, EFC input 0..4 V (full range).
 * PWM DAC is limited to 0..3.3 V (STM32 Vcc) — only 82.5% of EFC range.
 *
 * CTI OSC5A2B02  — Kv = 7.50 Hz/V,  EFC range: −10 to +20 Hz (30 Hz)
 *                  PWM-accessible: −10 to +14.75 Hz (24.75 Hz), 0.378 mHz/LSB
 * Vectron C4550  — Kv = 10.00 Hz/V, EFC range: ±20 Hz (40 Hz, symmetric)
 *                  PWM-accessible: −20 to +13.00 Hz (33.00 Hz), 0.504 mHz/LSB
 *
 * Scale factor Vectron/CTI = 1.333 — Vectron gains = CTI × 0.75
 * All parameters can be overridden at runtime via CLI (KP/KI/KD/IL).
 * ──────────────────────────────────────────────────────────────────── */
//#define GPSDO_OCXO_CTI_OSC5A2B02     /* CTI OSC5A2B02  10 MHz OCXO  (DIP, HCMOS) */
#define GPSDO_OCXO_VECTRON_C4550     /* Vectron C4550A1-0213  10 MHz OCXO  (SMD, HCMOS) */

#if defined(GPSDO_OCXO_CTI_OSC5A2B02) && defined(GPSDO_OCXO_VECTRON_C4550)
  #error "Select only one OCXO: GPSDO_OCXO_CTI_OSC5A2B02 or GPSDO_OCXO_VECTRON_C4550"
#endif

/* ── Default PWM ─────────────────────────────────────────────────────
 *
 * PWM DAC output range: 0..3.3 V (STM32 Vcc).
 * EFC input range of both OCXOs: 0..4 V — only 0..3.3 V is accessible.
 *
 * CTI OSC5A2B02  — nominal 0-Hz point ≈ 1.33 V (asymmetric -1/+2 ppm),
 *                  unit-to-unit variance is significant; calibration
 *                  corrects automatically. Safe start: 1.65 V midpoint.
 *                  → DEFAULT_PWM = 32767  (1.65 V = midpoint 0..3.3 V)
 *
 * Vectron C4550  — nominal 0-Hz point = 2.00 V (symmetric ±2 ppm),
 *                  within accessible 0..3.3 V PWM range.
 *                  → DEFAULT_PWM = 39718  (2.00 V)
 *
 * Accessible tuning ranges (limited by 3.3 V PWM max):
 *   CTI:     −10 Hz (0 V) to +14.75 Hz (3.3 V) — 24.75 Hz of 30 Hz total
 *   Vectron: −20 Hz (0 V) to +13.00 Hz (3.3 V) — 33.00 Hz of 40 Hz total
 * ──────────────────────────────────────────────────────────────────── */
#if defined(GPSDO_OCXO_VECTRON_C4550)
  #define DEFAULT_PWM_OUTPUT  39718u   /* Vectron: 0-Hz nominal at 2.00 V */
#else
  #define DEFAULT_PWM_OUTPUT  32767u   /* CTI:     safe midpoint at 1.65 V */
#endif

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
#define STACK_DISPLAY    512
#define STACK_UPTIME     192

#ifdef __cplusplus
}
#endif
