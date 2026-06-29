/**
 * gpsdo_tasks.cpp — Sensor, Display and Uptime tasks
 *
 * Part of GPSDO FreeRTOS v0.51
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * vSensorTask   — reads AHT/BMP/INA sensors every 2 s under xWireMutex
 * vDisplayTask  — drives OLED, LCD, TFT, TM1637, serial report, and LEDs
 * vUptimeTask   — increments uptime counter, formats dd hh:mm:ss
 *
 * Display update uses dirty-flag comparison per row — only changed rows
 * trigger I2C writes, minimising bus traffic.
 *
 * OLED: two alternating pages (GPS / sensors), version splash at boot,
 *       then local time clock (LMT:hh:mm:ss DAY) on row 0.
 * LCD:  frequency on line 0, UTC+uptime on line 1, 6-mode rotating
 *       view on line 2, PWM+trend+holdover on line 3.
 * Yellow LED: 4-state machine (off / on / slow pulse / fast pulse).
 */

#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include <Arduino.h>
#include <string.h>
#include <Wire.h>

/* ----------------------------------------------------------------------
 * i2c_probe — robust device-presence check.
 *
 * Dual verification: address ACK (endTransmission == 0) AND a 1-byte
 * read-back (requestFrom > 0).  A lone ACK check is unreliable on some
 * STM32duino core versions, which can return 0 for absent devices and
 * produce false "OK" detections in the startup hardware report.
 * Caller must hold xWireMutex.
 * ---------------------------------------------------------------------- */
static bool i2c_probe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) return false;
    return Wire.requestFrom(addr, (uint8_t)1) > 0;
}

/* ---- Sensor data globals ---------------------------------------------- */
float  g_bmp_temp = 0.0f, g_bmp_pres = 0.0f, g_bmp_alti = 0.0f;
float  g_aht_temp = 0.0f, g_aht_humi = 0.0f;
float  g_ina_volt = 0.0f, g_ina_curr = 0.0f;

/* ---- LTIC globals (Lars TIC) ------------------------------------------ */
#ifdef GPSDO_LTIC
/* Simple 10-sample circular-buffer moving average — no external library.
 * All access is from SensorTask only (single writer, other tasks read the
 * final averaged result g_ltic_voltage which is updated atomically). */
static int16_t  s_ltic_buf[LTIC_AVG_SAMPLES];
static uint8_t  s_ltic_idx  = 0;
static bool     s_ltic_full = false;
static int32_t  s_ltic_sum  = 0;

volatile bool g_ltic_must_read = false;
int16_t       g_ltic_adc_raw   = 0;
int16_t       g_ltic_adc_avg   = 0;
float         g_ltic_voltage   = 0.0f;

/* ltic_moving_avg — feed one new sample, return averaged value */
static int16_t ltic_moving_avg(int16_t sample)
{
    s_ltic_sum -= s_ltic_buf[s_ltic_idx];
    s_ltic_buf[s_ltic_idx] = sample;
    s_ltic_sum += sample;
    s_ltic_idx = (s_ltic_idx + 1) % LTIC_AVG_SAMPLES;
    if (!s_ltic_full && s_ltic_idx == 0) s_ltic_full = true;
    int16_t count = s_ltic_full ? LTIC_AVG_SAMPLES : (int16_t)s_ltic_idx;
    if (count == 0) count = 1;
    return (int16_t)(s_ltic_sum / count);
}

/* ltic_read — read PA1 ADC, compute moving average, discharge capacitor.
 * Must be called from task context (uses vTaskDelay, not delay()).
 * PA1 is reconfigured as open-drain output to discharge the 1nF capacitor,
 * then restored to INPUT_ANALOG — identical to André's original readvphase().
 */
static void ltic_read(void)
{
    g_ltic_must_read = false;   /* clear flag before reading so we don't miss next PPS */

    g_ltic_adc_raw = (int16_t)analogRead(PIN_LTIC_VPHASE);
    g_ltic_adc_avg = ltic_moving_avg(g_ltic_adc_raw);
    g_ltic_voltage = ((float)g_ltic_adc_avg / 4096.0f) * 3.3f;

    /* Discharge 1 nF TIC capacitor */
    pinMode(PIN_LTIC_VPHASE, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_LTIC_VPHASE, LOW);
    vTaskDelay(pdMS_TO_TICKS(LTIC_DISCHARGE_MS));
    digitalWrite(PIN_LTIC_VPHASE, HIGH);
    pinMode(PIN_LTIC_VPHASE, INPUT_ANALOG);
}
#endif /* GPSDO_LTIC */

/* ---- Sensor objects & safe compile-time fallbacks --------------------- */
#ifdef GPSDO_AHT10
  #include <Adafruit_AHTX0.h>
  static Adafruit_AHTX0 s_aht;
  static bool s_aht_ok = false;
#else
  static const bool s_aht_ok = false;
#endif

#ifdef GPSDO_BMP280_I2C
  #include <Adafruit_BMP280.h>
  static Adafruit_BMP280 s_bmp;
  static bool s_bmp_ok = false;
#else
  static const bool s_bmp_ok = false;
#endif

#ifdef GPSDO_INA219
  #include <Adafruit_INA219.h>
  static Adafruit_INA219 s_ina;
  static bool s_ina_ok = false;
#else
  static const bool s_ina_ok = false;
#endif

/* Set true by vSensorTask once all sensor detection has completed, so the
 * TFT boot splash (in vDisplayTask) can wait briefly for valid flags. */
static volatile bool s_sensors_probed = false;

extern float g_pressure_offset;
extern float g_altitude_offset;

/* ======================================================================
 * vSensorTask — reads slow I2C sensors every 2 s
 * ====================================================================== */
void vSensorTask(void *pvParameters)
{
    (void)pvParameters;

    /* ---- Hardware detection report (sensors) ----
     * Every optional I2C device reports both outcomes so the startup log
     * gives a complete picture of what was found on the bus.            */
#ifdef GPSDO_AHT10
    s_aht_ok = s_aht.begin();
    OUT_SERIAL.println(s_aht_ok ? "HW: AHT10/AHT20 sensor    OK  (I2C 0x38)"
                                : "HW: AHT10/AHT20 sensor    not found");
#endif
#ifdef GPSDO_BMP280_I2C
    s_bmp_ok = s_bmp.begin(0x77, 0x58);
    if (s_bmp_ok) {
        s_bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                          Adafruit_BMP280::SAMPLING_X2,
                          Adafruit_BMP280::SAMPLING_X16,
                          Adafruit_BMP280::FILTER_X16,
                          Adafruit_BMP280::STANDBY_MS_500);
        OUT_SERIAL.println("HW: BMP280 sensor         OK  (I2C 0x77)");
    } else {
        OUT_SERIAL.println("HW: BMP280 sensor         not found");
    }
#endif
#ifdef GPSDO_INA219
    s_ina_ok = s_ina.begin();
    if (s_ina_ok) {
        s_ina.setCalibration_32V_1A();
        OUT_SERIAL.println("HW: INA219 sensor         OK  (I2C 0x40)");
    } else {
        OUT_SERIAL.println("HW: INA219 sensor         not found");
    }
#endif

#ifdef GPSDO_LTIC
    /* Initialise PA1 as analog input and take the first reading */
    pinMode(PIN_LTIC_VPHASE, INPUT_ANALOG);
    analogRead(PIN_LTIC_VPHASE);   /* dummy read to settle ADC mux */
    ltic_read();
    OUT_SERIAL.println("HW: LTIC phase input      OK  (PA1 analog)");
#endif

    s_sensors_probed = true;   /* signal vDisplayTask the flags are valid */

    for (;;)
    {
#ifdef GPSDO_BMP280_I2C
        if (s_bmp_ok && xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_bmp_temp = s_bmp.readTemperature();
            float raw_pres = s_bmp.readPressure();
            g_bmp_pres = (raw_pres + g_pressure_offset) / 100.0f;
            g_bmp_alti = s_bmp.readAltitude(g_bmp_pres + g_altitude_offset);
            xSemaphoreGive(xWireMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
#ifdef GPSDO_AHT10
        if (s_aht_ok && xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            sensors_event_t hum, tmp;
            s_aht.getEvent(&hum, &tmp);
            g_aht_temp = tmp.temperature;
            g_aht_humi = hum.relative_humidity;
            xSemaphoreGive(xWireMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
#ifdef GPSDO_INA219
        if (s_ina_ok && xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_ina_volt = s_ina.getBusVoltage_V();
            g_ina_curr = s_ina.getCurrent_mA();
            xSemaphoreGive(xWireMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
#ifdef GPSDO_LTIC
        /* Read Vphase as soon as the PPS ISR sets the flag.
         * The vTaskDelay(2000) below means we might service the flag
         * up to 2s late, which is acceptable — the voltage on the 1nF
         * capacitor holds well beyond 2s with a high-impedance input.
         * For better accuracy enable GPSDO_LTIC only when the hardware
         * is actually connected (PA1 floating gives meaningless values). */
        if (g_ltic_must_read) ltic_read();
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ======================================================================
 * vUptimeTask — increments uptime counter at 1 Hz via 2Hz semaphore
 * ====================================================================== */
extern SemaphoreHandle_t xTwoHzSemaphore;

void vUptimeTask(void *pvParameters)
{
    (void)pvParameters;
    bool half = false;

    for (;;)
    {
        xSemaphoreTake(xTwoHzSemaphore, portMAX_DELAY);
        half = !half;
        if (!half) continue;   /* advance only on 1Hz edge */

        if (xSemaphoreTake(xUptimeMutex, pdMS_TO_TICKS(5)) != pdTRUE) continue;

        Uptime_t *u = &gUptime;
        if (++u->secs > 59) {
            u->secs = 0;
            if (++u->mins > 59) {
                u->mins = 0;
                if (++u->hours > 23) {
                    u->hours = 0;
                    u->days++;
                }
            }
        }

        u->time_str[0] = '0' + u->hours / 10;
        u->time_str[1] = '0' + u->hours % 10;
        u->time_str[2] = ':';
        u->time_str[3] = '0' + u->mins  / 10;
        u->time_str[4] = '0' + u->mins  % 10;
        u->time_str[5] = ':';
        u->time_str[6] = '0' + u->secs  / 10;
        u->time_str[7] = '0' + u->secs  % 10;
        u->time_str[8] = '\0';

        uint16_t d = u->days;
        u->days_str[0] = '0' + (d / 100);
        u->days_str[1] = '0' + ((d % 100) / 10);
        u->days_str[2] = '0' + (d % 10);
        u->days_str[3] = 'd';
        u->days_str[4] = '\0';

        xSemaphoreGive(xUptimeMutex);
    }
}

/* ======================================================================
 * Serial report helpers
 *
 * All output is assembled into a static buffer (SBUF_SIZE bytes) and
 * flushed with a single Serial.write() call.
 * ====================================================================== */

#ifdef GPSDO_BLUETOOTH
  #define REPORT_SERIAL Serial2
#else
  #define REPORT_SERIAL Serial
#endif

#define SBUF_SIZE 640

/* Append a C-string */
static int sa(char *buf, int pos, const char *s)
{
    while (*s && pos < SBUF_SIZE - 2) buf[pos++] = *s++;
    return pos;
}

/* Append a decimal integer */
static int si(char *buf, int pos, int32_t v)
{
    static char tmp[14];
    ltoa((long)v, tmp, 10);
    return sa(buf, pos, tmp);
}

/* Append unsigned 64-bit */
static int su64(char *buf, int pos, uint64_t v)
{
    static char tmp[22];
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t lo = (uint32_t)(v & 0xFFFFFFFFUL);
    if (hi > 0)
        snprintf(tmp, sizeof(tmp), "%lu%010lu", (unsigned long)hi, (unsigned long)lo);
    else
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)lo);
    return sa(buf, pos, tmp);
}

/* Append double with given decimal places */
static int sd(char *buf, int pos, double v, int dec)
{
    /* dtostrf: Arduino-native, always safe with newlib-nano.
     * Width=-1 = minimum width (no padding). */
    static char tmp[32];
    dtostrf(v, -1, dec, tmp);
    return sa(buf, pos, tmp);
}

/* Append two-digit zero-padded uint8 */
static int s2(char *buf, int pos, uint8_t v)
{
    buf[pos++] = '0' + v / 10;
    buf[pos++] = '0' + v % 10;
    return pos;
}

/* ---- Tab-delimited report -------------------------------------------- */
static void print_tab_report(const GpsData_t *g, const FreqSnap_t *f,
                              const CtrlData_t *c, const Uptime_t *u)
{
    static uint64_t line_no = 0;
    static char buf[SBUF_SIZE];
    int p = 0;

    p = su64(buf, p, ++line_no);                    buf[p++] = '\t';
    p = si  (buf, p, g->day);                       buf[p++] = '/';
    p = si  (buf, p, g->month);                     buf[p++] = '/';
    p = si  (buf, p, g->year);                      buf[p++] = ' ';
    p = s2  (buf, p, g->hours);                     buf[p++] = ':';
    p = s2  (buf, p, g->mins);                      buf[p++] = ':';
    p = s2  (buf, p, g->secs);                      buf[p++] = '\t';
    p = sa  (buf, p, u->days_str);                  buf[p++] = ' ';
    p = sa  (buf, p, u->time_str);                  buf[p++] = '\t';
    p = su64(buf, p, f->fcount64);                  buf[p++] = '\t';
    p = su64(buf, p, f->calcfreq64);                buf[p++] = '\t';
    p = sd  (buf, p, f->avg10,    1);               buf[p++] = '\t';
    p = sd  (buf, p, f->avg100,   2);               buf[p++] = '\t';
    p = sd  (buf, p, f->avg1000,  3);               buf[p++] = '\t';
    p = sd  (buf, p, f->avg10000, 4);               buf[p++] = '\t';
    p = sd  (buf, p, f->avg20000, 5);               buf[p++] = '\t';
    p = si  (buf, p, g->sats);                      buf[p++] = '\t';
    /* Tab format keeps numeric HDOP (99.99 in Time Mode) for machine
     * parsing/plotting; the human-readable report shows HDOP:TIME. */
    p = sd  (buf, p, (double)g->hdop/100.0, 2);     buf[p++] = '\t';
    p = si  (buf, p, c->pwm_output);                buf[p++] = '\t';
    p = sd  (buf, p, (c->avg_vctl_adc/4096.0)*3.3, 3);        buf[p++] = '\t';
    p = sd  (buf, p, (c->avg_vcc_adc /4096.0)*3.3*2.0, 2);    buf[p++] = '\t';
    p = sd  (buf, p, (1.21*4096.0)/(double)c->avg_vdd_adc, 3); buf[p++] = '\t';
    p = sd  (buf, p, g_bmp_temp, 1);                buf[p++] = '\t';
    p = sd  (buf, p, g_bmp_pres, 1);                buf[p++] = '\t';
    p = sd  (buf, p, g_aht_temp, 1);                buf[p++] = '\t';
    p = sd  (buf, p, g_aht_humi, 1);                buf[p++] = '\t';
    p = sd  (buf, p, g_ina_volt, 2);                buf[p++] = '\t';
    p = sd  (buf, p, g_ina_curr, 0);                buf[p++] = '\t';
#ifdef GPSDO_LTIC
    /* Vphase as 12-bit ADC value (matches André's original TIC column) */
    p = si(buf, p, g_ltic_adc_avg);
#else
    buf[p++] = '0';   /* TIC placeholder — GPSDO_LTIC not enabled */
#endif
    buf[p++] = '\r';  buf[p++] = '\n';

    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        REPORT_SERIAL.write((uint8_t *)buf, p);
        xSemaphoreGive(xSerialMutex);
    }
}

/* ---- Human-readable report ------------------------------------------- */
static void print_human_report(const GpsData_t *g, const FreqSnap_t *f,
                                const CtrlData_t *c, const Uptime_t *u)
{
    static char buf[SBUF_SIZE];
    int p = 0;

    /* Uptime + UTC date and time */
    p=sa(buf,p,"Up: "); p=sa(buf,p,u->days_str); buf[p++]=' ';
    p=sa(buf,p,u->time_str);
    if (g->valid) {
        p=sa(buf,p,"  UTC: ");
        p=si(buf,p,g->day);   buf[p++]='/';
        p=si(buf,p,g->month); buf[p++]='/';
        p=si(buf,p,g->year);  buf[p++]=' ';
        p=s2(buf,p,g->hours); buf[p++]=':';
        p=s2(buf,p,g->mins);  buf[p++]=':';
        p=s2(buf,p,g->secs);
    }
    buf[p++]='\r'; buf[p++]='\n';

    /* GPS position */
    if (g->pos_valid) {
        p=sa(buf,p,"Lat: "); p=sd(buf,p,g->lat,6);
        p=sa(buf,p," Lon: "); p=sd(buf,p,g->lon,6);
        p=sa(buf,p," Alt: "); p=sd(buf,p,g->alt,1);
        p=sa(buf,p,"m Sat:"); p=si(buf,p,g->sats);
        if (g->time_mode) { p=sa(buf,p," HDOP:TIME"); }
        else { p=sa(buf,p," HDOP:"); p=sd(buf,p,(double)g->hdop/100.0,2); }
        buf[p++]='\r'; buf[p++]='\n';
    } else {
        p=sa(buf,p,"GPS: no position fix yet\r\n");
    }

    /* Frequency */
    p=sa(buf,p,"Freq: ");
    if (f->calcfreq64 > 0) { p=su64(buf,p,f->calcfreq64); p=sa(buf,p," Hz"); }
    else                   { p=sa(buf,p,"---"); }
    if (f->full10)    { p=sa(buf,p,"  10s:");  p=sd(buf,p,f->avg10,   1); }
    if (f->full100)   { p=sa(buf,p," 100s:");  p=sd(buf,p,f->avg100,  2); }
    if (f->full1000)  { p=sa(buf,p,"  1ks:");  p=sd(buf,p,f->avg1000, 3); }
    buf[p++]='\r'; buf[p++]='\n';

    /* PWM / Vctl */
    p=sa(buf,p,"PWM:"); p=si(buf,p,c->pwm_output);
    p=sa(buf,p,"  Vctl:"); p=sd(buf,p,((double)c->avg_vctl_adc/4096.0)*3.3,3);
    p=sa(buf,p,"V");
    if (c->holdover_mode) p=sa(buf,p," [HOLDOVER]");
    else { p=sa(buf,p," "); p=sa(buf,p,c->trendstr); }
    buf[p++]='\r'; buf[p++]='\n';

    /* Sensors */
    p=sa(buf,p,"BMP:"); p=sd(buf,p,g_bmp_temp,1); p=sa(buf,p,"C ");
                         p=sd(buf,p,g_bmp_pres,1); p=sa(buf,p,"hPa  ");
    p=sa(buf,p,"AHT:"); p=sd(buf,p,g_aht_temp,1); p=sa(buf,p,"C ");
                         p=sd(buf,p,g_aht_humi,1); p=sa(buf,p,"%rH  ");
    p=sa(buf,p,"INA:"); p=sd(buf,p,g_ina_volt,2); p=sa(buf,p,"V ");
                         p=sd(buf,p,g_ina_curr,0); p=sa(buf,p,"mA");
#ifdef GPSDO_LTIC
    p=sa(buf,p,"  Vphase:"); p=sd(buf,p,(double)g_ltic_voltage,3); p=sa(buf,p,"V");
#endif
    buf[p++]='\r'; buf[p++]='\n';
    buf[p++]='\r'; buf[p++]='\n';

    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        REPORT_SERIAL.write((uint8_t *)buf, p);
        xSemaphoreGive(xSerialMutex);
    }
}

/* ======================================================================
 * LCD 20x4 I2C  (PCF8574T expander, hd44780 library)
 *
 * Layout:
 *   Line 0: Frequency — right-justified, "F:  10000000.0000 Hz" (20 chars)
 *   Line 1: UTC time HH:MM:SS + uptime days
 *   Line 2: Rotating view — GPS coords / Sats+HDOP / AHT / INA / BMP
 *   Line 3: PWM + Vctl + trend  OR  PWM + Vctl + blinking [H] (holdover)
 *
 * Holdover indication: trend field on line 3 replaced by blinking '[H]'
 * at HOLDOVER_BLINK_MS rate.
 *
 * Dirty-flag caching: each line compared with previous content — I2C
 * write only for lines whose content changed.
 * ====================================================================== */
#ifdef GPSDO_LCD_20x4
  #include <Wire.h>
  #include <hd44780.h>
  #include <hd44780ioClass/hd44780_I2Cexp.h>

  static hd44780_I2Cexp lcd;
  static bool s_lcd_ok = false;
  static char lcd_prev[4][21];   /* previous content per row (20 chars + NUL) */

  /* Write row only if content changed */
  static void lcd_set_line(uint8_t row, const char *text)
  {
      char padded[21];
      int i = 0;
      while (text[i] && i < 20) { padded[i] = text[i]; i++; }
      while (i < 20) padded[i++] = ' ';
      padded[20] = '\0';

      if (memcmp(padded, lcd_prev[row], 20) == 0) return;
      memcpy(lcd_prev[row], padded, 21);

      lcd.setCursor(0, row);
      lcd.print(padded);
  }

  /* Write a single character at a specific column without dirtying
   * the full row cache — used for the blinking holdover indicator. */
  static void lcd_set_char(uint8_t row, uint8_t col, char c)
  {
      lcd_prev[row][col] = c;
      lcd.setCursor(col, row);
      lcd.print(c);
  }
#endif /* GPSDO_LCD_20x4 */

/* ======================================================================
 * OLED 128x64 I2C  (U8x8 library, 16 chars × 8 rows)
 *
 * Two alternating pages, switched every OLED_PAGE_SWITCH_SECS seconds:
 *
 *   Page A — GPS focus:
 *     Row 0: LMT:hh:mm:ss DAY      (local time + day; version shown 2 s at boot)
 *     Row 1: F 9999999.9999Hz      (frequency, best precision + Hz at pos 14-15)
 *     Row 2: La  52.12345          (latitude  OR "GPS: acquiring")
 *     Row 3: Lo  23.12345          (longitude OR "Sat: XX        ")
 *     Row 4: Al  175m Sat: 9       (alt+sats  OR BMP temp+pres)
 *     Row 5: Up 000d 00:00:00      (uptime)
 *     Row 6: 12:34:56  23.4C       (UTC time + AHT temperature)
 *     Row 7: PWM:40908 ___[H]      (PWM, trend, holdover blink)
 *
 *   Page B — sensors focus:
 *     Row 0: LMT:hh.mm.ss DAY      (same)
 *     Row 1: F 9999999.9999Hz      (frequency, Hz at pos 14-15)
 *     Row 2: BM:23.4C 1013hPa      (BMP280: temp 1dec + pressure 0dec, 16 chars)
 *     Row 3: AH:22.1C 45.3%rH     (AHT20: temp 1dec + humidity 1dec, 16 chars)
 *     Row 4: IN:12.05V  250mA     (INA219: voltage 2dec + current 0dec, 16 chars)
 *     Row 5: Sat:09 HDOP:0.90     (GPS quality indicators)
 *     Row 6: UTC:hh:mm:ss DAY      (UTC time + day of week)
 *     Row 7: PWM:40908 ___[H]      (same as page A)
 *
 * Holdover blink: letter 'H' toggled at col 15 of row 7, every
 * HOLDOVER_BLINK_MS ms. Written via oled_set_last_char() to avoid
 * invalidating the dirty-flag cache for the rest of row 7.
 *
 * On page switch: dirty flags cleared for rows 2–6 to force redraw.
 * Row 0 (static title) written only at init.
 * Rows 1 and 7 (frequency and PWM) are identical on both pages and
 * redraw only if their value changes.
 * ====================================================================== */
#ifdef GPSDO_OLED
  #include <U8x8lib.h>

  #if defined(GPSDO_OLED_SH1106)
    static U8X8_SH1106_128X64_NONAME_HW_I2C  s_oled(U8X8_PIN_NONE);
  #elif defined(GPSDO_OLED_SSD1306)
    static U8X8_SSD1306_128X64_NONAME_HW_I2C s_oled(U8X8_PIN_NONE);
  #elif defined(GPSDO_OLED_SSD1309)
    static U8X8_SSD1309_128X64_NONAME0_HW_I2C s_oled(U8X8_PIN_NONE);
  #endif

  static bool s_oled_ok = false;
  static char oled_prev[8][17];   /* previous content per row (16 chars + NUL) */

  /* Write row only if content changed */
  static void oled_set_line(uint8_t row, const char *text)
  {
      char padded[17];
      int i = 0;
      while (text[i] && i < 16) { padded[i] = text[i]; i++; }
      while (i < 16) padded[i++] = ' ';
      padded[16] = '\0';

      if (memcmp(padded, oled_prev[row], 16) == 0) return;
      memcpy(oled_prev[row], padded, 17);

      s_oled.setCursor(0, row);
      s_oled.print(padded);
  }

  /* Write only the last character of a row (column 15) without
   * dirtying the full row cache — used for holdover blink indicator. */
  static void oled_set_last_char(uint8_t row, char c)
  {
      oled_prev[row][15] = c;
      s_oled.setCursor(15, row);
      s_oled.print(c);
  }

  /* Invalidate dirty flags for rows first..last, forcing their redraw. */
  static void oled_invalidate_rows(uint8_t first, uint8_t last)
  {
      for (uint8_t r = first; r <= last; r++)
          memset(oled_prev[r], 0, sizeof(oled_prev[r]));
  }
#endif /* GPSDO_OLED */

/* ======================================================================
 * TFT SPI  (TFT_eSPI library, ILI9341 / ST7789 320x240, ILI9488 480x320)
 *
 * Landscape, hardware SPI1 (PA5/PA7), exclusive bus — no mutex needed (only
 * vDisplayTask touches it). The layout below is authored for 320x240 and
 * scaled up to 480x320 (ILI9488) at compile time via TFT_SX/TFT_SY/TFT_F
 * (see gpsdo_config.h). The ILI9488 path is UNTESTED — no panel on hand yet.
 *
 * Layout (landscape 320 x 240, scaled x1.5 wide / x1.33 tall for 480x320):
 *
 *  y=  0..23  ── header bar (navy):  "GPSDO v0.xx"      "LMT 14:32:45 Thu"
 *  y= 30..62  ── FREQUENCY, font 4 doubled, centred, colour-coded:
 *                  green  = locked (trend "hit")
 *                  white  = adjusting
 *                  orange = holdover
 *                  red    = no signal
 *  y= 70..151 ── info grid, font 2, two columns (left x=8, right x=168):
 *                  UTC:12:32:45 Thu  │  Sat: 9 HDOP:0.90
 *                  11/06/2026        │  Lat: 52.123456
 *                  Up 000d 02:15:33  │  Lon: 23.123456
 *                  Algo:5  hit       │  Alt: 175m
 *                  PWM:44653 V:1.97  │  IN:12.05V 250mA
 *  y=156..195 ── sensor row, font 2:
 *                  BMP:23.4C 1013hPa │  AHT:22.1C 45.3%rH
 *  y=204..239 ── status bar, font 4, full width, colour-coded background:
 *                  green  "DISCIPLINED  FIX OK"
 *                  orange "HOLDOVER (manual)"
 *                  red    "HOLDOVER (fix lost)" / "WAITING FOR GPS FIX"
 *
 * Selective redraw: every value cell caches its previous string and is
 * redrawn only on change (setTextPadding clears the old glyphs).
 * ====================================================================== */
#ifdef GPSDO_TFT
  #include <TFT_eSPI.h>

  /* Forward declarations — implementations are further down this file */
  static const char *day_of_week_str(uint8_t day, uint8_t month, uint16_t year);
  static void apply_time_offset(uint8_t  utc_h,  uint8_t utc_m,  uint8_t utc_s,
                                uint8_t  utc_day, uint8_t utc_mon, uint16_t utc_yr,
                                uint8_t *lh, uint8_t *lm, uint8_t *ls,
                                uint8_t *ld, uint8_t *lmo, uint16_t *lyr);

  static TFT_eSPI s_tft;
  static bool     s_tft_ok = false;

  /* Colours (RGB565) */
  #define TFT_COL_BG      TFT_BLACK
  #define TFT_COL_HEADER  0x0A33u        /* dark navy           */
  #define TFT_COL_LABEL   0x8C51u        /* mid grey            */
  #define TFT_COL_VALUE   TFT_WHITE
  #define TFT_COL_LOCK    0x07E0u        /* green               */
  #define TFT_COL_HOLD    0xFC60u        /* orange              */
  #define TFT_COL_ALERT   0xF800u        /* red                 */
  #define TFT_COL_SINEL   0x3D7Fu        /* blue  (left wave)   */
  #define TFT_COL_SINER   0xFD80u        /* amber (right wave)  */

  /* Grid geometry — authored for 320x240, scaled so the same layout fills a
   * 480x320 ILI9488. X/width via TFT_S (3/2), Y/height via TFT_SY (4/3),
   * since the panel aspect differs. Identity on the small panels. */
  #define TFT_ROW_H       TFT_SY(16)     /* font 2 row height   */
  #define TFT_GRID_Y      TFT_SY(70)
  #define TFT_COL_L       TFT_S(8)
  #define TFT_COL_R       TFT_S(168)
  #define TFT_SENS_Y      TFT_SY(156)
  #define TFT_STATUS_Y    TFT_SY(204)

  /* Previous-value cache for selective redraw */
  static char tft_prev[16][28];

  /* Draw a value string only when it changed.
   * slot: cache index, x/y: position, pad: text padding px, col: colour.
   * x/y/pad are passed already-scaled by callers (via TFT_S/grid constants);
   * the font is mapped up for the large panel via TFT_F(). */
  static void tft_val(uint8_t slot, int32_t x, int32_t y,
                      uint16_t pad, uint16_t col, const char *s)
  {
      if (strncmp(tft_prev[slot], s, sizeof(tft_prev[0]) - 1) == 0)
          return;
      strncpy(tft_prev[slot], s, sizeof(tft_prev[0]) - 1);
      tft_prev[slot][sizeof(tft_prev[0]) - 1] = '\0';
      s_tft.setTextColor(col, TFT_COL_BG);
      s_tft.setTextPadding(pad);
      s_tft.drawString(s, x, y, TFT_F(2));
  }

  /* One-time hardware init only (no layout — splash draws first) */
  static void tft_init(void)
  {
      /* Diagnostic marker — if this is the last serial message, the hang
       * is inside TFT_eSPI init(): check User_Setup.h pin config first. */
      OUT_SERIAL.println("TFT: init start (SPI1 PA5/PA7, CS=PB13 DC=PB12 RST=PB15)");
      vTaskDelay(pdMS_TO_TICKS(50));   /* let serial flush + bus settle */

      s_tft.init();
      s_tft.setRotation(1);              /* landscape (USB right) */
      s_tft.fillScreen(TFT_COL_BG);
      s_tft_ok = true;
#if defined(GPSDO_TFT_ILI9488)
      OUT_SERIAL.println("HW: TFT 480x320 ILI9488    enabled (SPI1, UNTESTED, write-only)");
#else
      OUT_SERIAL.println("HW: TFT 320x240            enabled (SPI1, write-only - not verifiable)");
#endif
  }

  /* Draw the static operating-screen layout (header + separators).
   * Called once after the splash, before the live update loop starts. */
  static void tft_draw_layout(void)
  {
      s_tft.fillScreen(TFT_COL_BG);
      memset(tft_prev, 0, sizeof(tft_prev));

      s_tft.fillRect(0, 0, TFT_W, TFT_SY(24), TFT_COL_HEADER);
      s_tft.setTextDatum(TL_DATUM);
      s_tft.setTextColor(TFT_WHITE, TFT_COL_HEADER);
      s_tft.setTextPadding(0);
      s_tft.drawString(PROGRAM_NAME " " PROGRAM_VERSION, TFT_S(6), TFT_SY(4), TFT_F(2));

      s_tft.drawFastHLine(0, TFT_SY(66),  TFT_W, TFT_COL_LABEL);
      s_tft.drawFastHLine(0, TFT_SY(152), TFT_W, TFT_COL_LABEL);
      s_tft.drawFastHLine(0, TFT_SY(200), TFT_W, TFT_COL_LABEL);
  }

  /* Animated boot splash: credits first, then two phase-shifted sine waves
   * (blue above, amber below) whose phase slowly converges until they
   * coincide and merge into a single thick green 10 MHz wave — a visual
   * metaphor for GPS and OCXO pulling into phase lock. A hardware
   * checklist follows. Purely cosmetic; runs once before the layout. */
  static void tft_splash(bool oled_ok, bool lcd_ok, bool ht_ok,
                         bool aht_ok, bool bmp_ok, bool ina_ok)
  {
      s_tft.fillScreen(TFT_COL_BG);

      /* Header bar */
      s_tft.fillRect(0, 0, TFT_W, TFT_SY(26), TFT_COL_HEADER);
      s_tft.setTextDatum(TL_DATUM);
      s_tft.setTextColor(TFT_WHITE, TFT_COL_HEADER);
      s_tft.drawString(PROGRAM_NAME " " PROGRAM_VERSION, TFT_S(6), TFT_SY(5), TFT_F(2));

      /* --- credits drawn FIRST, and they persist through the animation --- */
      s_tft.setTextDatum(MC_DATUM);
      s_tft.setTextColor(TFT_COL_LOCK, TFT_COL_BG);
      s_tft.drawString("GPSDO", TFT_W/2, TFT_SY(48), TFT_F(6));
      s_tft.setTextColor(0x9CD3, TFT_COL_BG);   /* soft blue-grey */
      s_tft.drawString("GPS-Disciplined OCXO", TFT_W/2, TFT_SY(80), TFT_F(4));
      s_tft.setTextColor(0x8410, TFT_COL_BG);
      s_tft.drawString("jmnlabs with Claude (Anthropic)", TFT_W/2, TFT_SY(214), TFT_F(1));

      /* --- two phase-shifted waves converging to synchronism ---
       * Animation requires redrawing each frame, so each wave is erased
       * (drawn in background colour) before the next frame is drawn. The
       * waves live in a band around y=130, clear of the credits above.
       * All geometry scales with TFT_S() so the band fits the larger panel. */
      const int   yc      = TFT_SY(130); /* wave centre line               */
      const float SPL_AMP  = TFT_S(15); /* amplitude px                   */
      const float SPL_WCYC = 5.0f;      /* cycles across the screen       */
      const float SPL_PH0  = 2.5f;      /* initial phase offset [rad]     */
      const int   SPL_GAP  = TFT_S(12); /* initial vertical separation px */
      const int   STEP     = TFT_S(4);  /* x sampling step                */

      #define WAVE_Y(xx,yoff,ph) \
          (yc + (yoff) + (int)(SPL_AMP * sinf((float)(xx)/(float)TFT_W*6.2831853f*SPL_WCYC + (ph))))

      const int FRAMES = 64;       /* convergence frames             */
      for (int fr = 0; fr <= FRAMES; fr++) {
          float k    = (float)fr / (float)FRAMES;     /* 0..1 */
          float ease = k * k * (3.0f - 2.0f * k);     /* smoothstep */
          float ph   = SPL_PH0 * (1.0f - ease);           /* phase → 0 */
          int   g    = (int)(SPL_GAP * (1.0f - ease));   /* gap → 0   */

          int ptx = 0, pty = WAVE_Y(0, -g, ph);
          int pbx = 0, pby = WAVE_Y(0, +g, 0.0f);

          /* erase previous frame band, then draw current two waves */
          s_tft.fillRect(0, yc - (int)SPL_AMP - SPL_GAP - 4, TFT_W,
                         2*((int)SPL_AMP + SPL_GAP + 4), TFT_COL_BG);

          for (int x = STEP; x <= TFT_W; x += STEP) {
              int ty = WAVE_Y(x, -g, ph);
              int by = WAVE_Y(x, +g, 0.0f);
              /* 2px thickness: draw the segment twice, offset by 1px */
              s_tft.drawLine(ptx, pty,   x, ty,   TFT_COL_SINEL);
              s_tft.drawLine(ptx, pty+1, x, ty+1, TFT_COL_SINEL);
              s_tft.drawLine(pbx, pby,   x, by,   TFT_COL_SINER);
              s_tft.drawLine(pbx, pby+1, x, by+1, TFT_COL_SINER);
              ptx = x; pty = ty; pbx = x; pby = by;
          }
          vTaskDelay(pdMS_TO_TICKS(45));    /* ~2.9 s convergence */
      }

      /* --- merge: erase band, draw one thick (4px) green wave --- */
      s_tft.fillRect(0, yc - (int)SPL_AMP - SPL_GAP - 4, TFT_W,
                     2*((int)SPL_AMP + SPL_GAP + 4), TFT_COL_BG);
      {
          int px0 = 0, py0 = WAVE_Y(0, 0, 0.0f);
          for (int x = STEP; x <= TFT_W; x += STEP) {
              int y = WAVE_Y(x, 0, 0.0f);
              for (int t = -1; t <= 2; t++)   /* 4px thickness */
                  s_tft.drawLine(px0, py0+t, x, y+t, TFT_COL_LOCK);
              px0 = x; py0 = y;
          }
      }
      #undef WAVE_Y

      /* hold the synchronised wave so the eye can rest */
      vTaskDelay(pdMS_TO_TICKS(1800));

      /* clear the wave band to make room for the checklist */
      s_tft.fillRect(0, yc - (int)SPL_AMP - SPL_GAP - 4, TFT_W,
                     2*((int)SPL_AMP + SPL_GAP + 4), TFT_COL_BG);

      /* --- hardware checklist (rows light up sequentially) ---
       * `show` reflects whether the device is COMPILED IN (#ifdef), so a
       * device absent from the build is omitted entirely rather than shown
       * as "not detected". `ok` reflects runtime detection: a compiled-in
       * device that wasn't found stays as an orange [ ]. OCXO/GPS are always
       * present. Sensors collapse to one row, shown if ANY sensor is built
       * in. Keeping this consistent across all rows is the whole point —
       * OLED and the sensor row previously forced show=true and so appeared
       * even when not compiled. */
      struct { const char *label; bool ok; bool show; } rows[] = {
          { "OCXO  discipline loop", true,   true   },
#ifdef GPSDO_GPS_TIMING
          { "GPS   LEA timing (SVIN)", true,  true  },
#else
          { "GPS   NEO receiver",     true,  true   },
#endif
          /* TFT is write-only like the TM1637 — but the splash is being drawn
           * on it right now, so if you can read this it works: [x]. Label
           * shows the driver and resolution of the selected build. */
#if defined(GPSDO_TFT_ILI9488)
          { "TFT   ILI9488 480x320",  true,   true   },
#elif defined(GPSDO_TFT_ILI9341)
          { "TFT   ILI9341 320x240",  true,   true   },
#elif defined(GPSDO_TFT_ST7789)
          { "TFT   ST7789  320x240",  true,   true   },
#endif
          { "OLED  128x64",          oled_ok,
#ifdef GPSDO_OLED
                                              true
#else
                                              false
#endif
          },
          { "LCD   20x4",            lcd_ok,
#ifdef GPSDO_LCD_20x4
                                              true
#else
                                              false
#endif
          },
          { "HT16K33 clock",         ht_ok,
#ifdef GPSDO_HT16K33
                                              true
#else
                                              false
#endif
          },
          /* TM1637 is write-only (no detection possible), like the TFT — so
           * if it is compiled in we assume it works and show [x], same as
           * OCXO/GPS. The label reflects the 4- vs 6-digit build. */
#if defined(GPSDO_TM1637_6)
          { "TM1637 clock HH:MM:SS",  true,   true   },
#elif defined(GPSDO_TM1637)
          { "TM1637 clock HH:MM",     true,   true   },
#endif
          { "Sensors AHT/BMP/INA",   (aht_ok||bmp_ok||ina_ok),
#if defined(GPSDO_AHT10) || defined(GPSDO_BMP280_I2C) || defined(GPSDO_INA219)
                                              true
#else
                                              false
#endif
          },
      };

      s_tft.setTextDatum(TL_DATUM);
      /* brief pause so the eye moves from the wave to the checklist before
       * the first item appears (otherwise the first rows are missed) */
      vTaskDelay(pdMS_TO_TICKS(600));
      int y = TFT_SY(120);
      for (unsigned i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
          if (!rows[i].show) continue;
          uint16_t col = rows[i].ok ? TFT_COL_LOCK : TFT_COL_HOLD;
          s_tft.setTextColor(col, TFT_COL_BG);
          s_tft.drawString(rows[i].ok ? "[x]" : "[ ]", TFT_S(36), y, TFT_F(2));
          s_tft.setTextColor(TFT_COL_VALUE, TFT_COL_BG);
          s_tft.drawString(rows[i].label, TFT_S(72), y, TFT_F(2));
          y += TFT_SY(15);
          vTaskDelay(pdMS_TO_TICKS(650));   /* slower sequential reveal */
      }

      vTaskDelay(pdMS_TO_TICKS(1800));       /* final hold before operating screen */
  }

  /* Per-second update — called from vDisplayTask main loop */
  static void tft_update(const GpsData_t *g, const FreqSnap_t *f,
                         const CtrlData_t *c, const Uptime_t *u,
                         bool aht_ok, bool bmp_ok, bool ina_ok)
  {
      char s[28];

      /* ---- header right: LMT clock + day ---- */
      if (g->valid) {
          uint8_t lh, lm, ls, ld, lmo; uint16_t lyr;
          apply_time_offset(g->hours, g->mins, g->secs,
                            g->day, g->month, g->year,
                            &lh, &lm, &ls, &ld, &lmo, &lyr);
          snprintf(s, sizeof(s), "LMT %02d:%02d:%02d %s",
                   lh, lm, ls, day_of_week_str(ld, lmo, lyr));
      } else {
          snprintf(s, sizeof(s), "LMT --:--:-- ---");
      }
      s_tft.setTextDatum(TR_DATUM);
      s_tft.setTextColor(TFT_WHITE, TFT_COL_HEADER);
      s_tft.setTextPadding(TFT_S(150));
      s_tft.drawString(s, TFT_W - TFT_S(6), TFT_SY(4), TFT_F(2));
      s_tft.setTextDatum(TL_DATUM);

      /* ---- frequency, font 4 size 2, centred, colour-coded ----
       * Lock detection is algorithm-independent: based on the actual
       * deviation of the best available average from 10 MHz, since most
       * algorithms never emit the "hit" trend string (algos 3-5 only on
       * an exact 0.0 PID output, 6-9 never).  Thresholds:
       *   10000-s window: |e| ≤ 1.0 mHz  (1e-10)
       *    1000-s window: |e| ≤ 10  mHz  (1e-9)                          */
      {
          uint16_t fcol = TFT_COL_VALUE;
          bool locked = false;
          bool busy = (g_svin_active || g_warmup_active || g_calib_active);
          if (busy) {
              if (g_svin_active)
                  snprintf(s, sizeof(s), "SVIN %us %um", (unsigned)g_svin_dur, (unsigned)g_svin_acc_m);
              else if (g_warmup_active)
                  snprintf(s, sizeof(s), "WARMUP %us", (unsigned)g_warmup_remaining);
              else
                  snprintf(s, sizeof(s), "CAL %us", (unsigned)g_calib_remaining);
              fcol = TFT_COL_HOLD;
              static uint16_t prev_cf = 0;
              if (prev_cf != fcol) { tft_prev[0][0] = '\0'; prev_cf = fcol; }
              s_tft.setTextDatum(TC_DATUM);
              if (strncmp(tft_prev[0], s, sizeof(tft_prev[0])-1) != 0) {
                  strncpy(tft_prev[0], s, sizeof(tft_prev[0])-1);
                  s_tft.setTextColor(fcol, TFT_COL_BG);
                  s_tft.setTextPadding(TFT_S(316));
                  s_tft.drawString(s, TFT_W/2, TFT_SY(34), TFT_F(4));
              }
              s_tft.setTextDatum(TL_DATUM);
              /* Do NOT return — fall through so the PWM/Vctl cell (slot 5)
               * keeps updating live during calibration. The info grid below
               * shows current PWM and Vctl, which is exactly what the user
               * wants to watch while C/CT sweeps the DAC. */
          } else {
          if      (f->full10000) { double e = f->avg10000 - 10000000.0;
                                   locked = (e > -0.001 && e < 0.001); }
          else if (f->full1000)  { double e = f->avg1000  - 10000000.0;
                                   locked = (e > -0.010 && e < 0.010); }
          if (strncmp(c->trendstr,"hit",3) == 0) locked = true;

          if      (c->holdover_mode) fcol = TFT_COL_HOLD;
          else if (locked)           fcol = TFT_COL_LOCK;

          if      (f->full10000) { static char ff[16]; dtostrf(f->avg10000,14,4,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->full1000)  { static char ff[16]; dtostrf(f->avg1000, 14,3,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->full100)   { static char ff[16]; dtostrf(f->avg100,  14,2,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->full10)    { static char ff[16]; dtostrf(f->avg10,   14,1,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->calcfreqint > 0) { snprintf(s,sizeof(s),"%14ld Hz",(long)f->calcfreqint); }
          else { snprintf(s,sizeof(s),"   no signal   "); fcol = TFT_COL_ALERT; }

          /* colour participates in cache key: append colour marker */
          static uint16_t prev_fcol = 0;
          if (prev_fcol != fcol) { tft_prev[0][0] = '\0'; prev_fcol = fcol; }

          s_tft.setTextDatum(TC_DATUM);
          if (strncmp(tft_prev[0], s, sizeof(tft_prev[0])-1) != 0) {
              strncpy(tft_prev[0], s, sizeof(tft_prev[0])-1);
              /* Fixed-width font 1 at size 3 (18x24): the frequency digits
               * keep a constant column position instead of shifting as the
               * value changes — much easier to read at a glance. */
              s_tft.setTextColor(fcol, TFT_COL_BG);
              s_tft.setTextPadding(TFT_S(316));
              s_tft.setTextFont(1);
#if defined(GPSDO_TFT_ILI9488)
              s_tft.setTextSize(5);   /* font1 x5 (~30x40) on the big panel */
#else
              s_tft.setTextSize(3);   /* font1 x3 (18x24)                   */
#endif
              s_tft.drawString(s, TFT_W/2, TFT_SY(30));
              s_tft.setTextSize(1);
          }
          s_tft.setTextDatum(TL_DATUM);
          }
      }

      /* ---- info grid, left column ---- */
      if (g->valid)
          snprintf(s,sizeof(s),"UTC:%02d:%02d:%02d %s", g->hours,g->mins,g->secs,
                   day_of_week_str(g->day,g->month,g->year));
      else snprintf(s,sizeof(s),"UTC:--:--:--");
      tft_val(1, TFT_COL_L, TFT_GRID_Y+0*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      if (g->valid) snprintf(s,sizeof(s),"%02d/%02d/%04d", g->day,g->month,g->year);
      else          snprintf(s,sizeof(s),"--/--/----");
      tft_val(2, TFT_COL_L, TFT_GRID_Y+1*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      snprintf(s,sizeof(s),"Up %s %s", u->days_str, u->time_str);
      tft_val(3, TFT_COL_L, TFT_GRID_Y+2*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      snprintf(s,sizeof(s),"Algo:%u  %s", c->active_algo, c->trendstr);
      tft_val(4, TFT_COL_L, TFT_GRID_Y+3*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      { static char fv[8];
        double vctl = ((double)c->avg_vctl_adc / 4096.0) * 3.3;
        dtostrf(vctl, 5, 3, fv);
        snprintf(s,sizeof(s),"PWM:%5u V:%s", c->pwm_output, fv); }
      tft_val(5, TFT_COL_L, TFT_GRID_Y+4*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      /* ---- info grid, right column ---- */
      if (g->pos_valid) {
          if (g->time_mode) {
              snprintf(s,sizeof(s),"Sat:%2d HDOP:TIME", g->sats);
          } else {
              static char fhd[8]; dtostrf((double)g->hdop/100.0,4,2,fhd);
              snprintf(s,sizeof(s),"Sat:%2d HDOP:%s", g->sats, fhd);
          }
      } else snprintf(s,sizeof(s),"Sat:%2d  no fix", g->sats);
      tft_val(6, TFT_COL_R, TFT_GRID_Y+0*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (g->pos_valid) { static char fl[12]; dtostrf(g->lat,10,6,fl);
          snprintf(s,sizeof(s),"Lat:%s",fl); }
      else snprintf(s,sizeof(s),"Lat: ---");
      tft_val(7, TFT_COL_R, TFT_GRID_Y+1*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (g->pos_valid) { static char fn[12]; dtostrf(g->lon,10,6,fn);
          snprintf(s,sizeof(s),"Lon:%s",fn); }
      else snprintf(s,sizeof(s),"Lon: ---");
      tft_val(8, TFT_COL_R, TFT_GRID_Y+2*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (g->pos_valid) snprintf(s,sizeof(s),"Alt:%5dm",(int)g->alt);
      else              snprintf(s,sizeof(s),"Alt: ---");
      tft_val(9, TFT_COL_R, TFT_GRID_Y+3*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (ina_ok) { static char fv[10],fi[10];
          dtostrf(g_ina_volt,5,3,fv); dtostrf(g_ina_curr,6,2,fi);
          snprintf(s,sizeof(s),"INA:%sV %smA",fv,fi); }
      else snprintf(s,sizeof(s),"INA: ---");
      tft_val(10, TFT_COL_R, TFT_GRID_Y+4*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      /* ---- sensor row ---- */
      if (bmp_ok) { static char ft[8],fp[10];
          dtostrf(g_bmp_temp,4,2,ft); dtostrf(g_bmp_pres,7,2,fp);
          snprintf(s,sizeof(s),"BMP:%sC %shPa",ft,fp); }
      else snprintf(s,sizeof(s),"BMP: ---");
      tft_val(11, TFT_COL_L, TFT_SENS_Y, TFT_S(156), TFT_COL_VALUE, s);

      if (aht_ok) { static char ft[8],fh[8];
          dtostrf(g_aht_temp,4,2,ft); dtostrf(g_aht_humi,4,2,fh);
          snprintf(s,sizeof(s),"AHT:%sC %s%%rH",ft,fh); }
      else snprintf(s,sizeof(s),"AHT: ---");
      tft_val(12, TFT_COL_R, TFT_SENS_Y, TFT_S(148), TFT_COL_VALUE, s);

      /* ---- status bar (full redraw only on state change) ---- */
      {
          /* states: 0=no fix, 1=disciplined, 2=manual HO, 3=auto HO */
          uint8_t st;
          if      (c->holdover_mode && c->holdover_auto) st = 3;
          else if (c->holdover_mode)                     st = 2;
          else if (g->pos_valid)                         st = 1;
          else                                           st = 0;

          static uint8_t prev_st = 0xFF;
          if (st != prev_st) {
              prev_st = st;
              uint16_t bg; const char *txt;
              switch (st) {
                  case 1:  bg = TFT_COL_LOCK;  txt = "DISCIPLINED  FIX OK";   break;
                  case 2:  bg = TFT_COL_HOLD;  txt = "HOLDOVER (manual)";     break;
                  case 3:  bg = TFT_COL_ALERT; txt = "HOLDOVER (fix lost)";   break;
                  default: bg = TFT_COL_ALERT; txt = "WAITING FOR GPS FIX";   break;
              }
              s_tft.fillRect(0, TFT_STATUS_Y, TFT_W, TFT_SY(36), bg);
              s_tft.setTextDatum(MC_DATUM);
              s_tft.setTextColor(TFT_BLACK, bg);
              s_tft.setTextPadding(0);
              s_tft.drawString(txt, TFT_W/2, TFT_STATUS_Y + TFT_SY(18), TFT_F(4));
              s_tft.setTextDatum(TL_DATUM);
          }
      }
  }
#endif /* GPSDO_TFT */

/* ======================================================================
 * SPI→T6963C bridge (PowerTip PG240128, 240x128 mono)
 *
 * *** EXPERIMENTAL / UNTESTED — see the banner in gpsdo_config.h ***
 * This backend compiles and the protocol matches the bridge firmware, but
 * the link has not yet worked cleanly on real hardware: long-wire/breadboard
 * bring-up showed oscilloscope ringing and CS-edge glitches (the same on the
 * proven reference master), i.e. a signal-integrity problem to be solved in
 * hardware before this is trusted. Leave GPSDO_T6963C disabled until then.
 *
 * Alternative graphical display to the TFT. Talks to the external
 * "T6963C_SPI_bridge" over SPI1 with high-level drawing commands
 * (T6963C_Bridge.h). Layout is a condensed version of the TFT screen:
 *
 *   y  0..17   header  : "GPSDO v0.51"            "14:32:45"   (NCEN10)
 *   y 18..54   freq    : "10000000.000 Hz"                     (LOGISOSO28)
 *   y 59..80   status  : [LOCK]  A7 hit   12 sat              (8x13B/6x13)
 *   y 82..117  values  : PWM/Vctl, INA, sensors               (6x13)
 *   y 120..127 svin bar: progress() during survey-in
 *
 * Mono panel → colour cues become inversion: the lock/holdover state is a
 * filled (inverted) box around the status word. One batched transaction per
 * refresh (single READY wait), with the library's auto-split as a safety net.
 * ====================================================================== */
#ifdef GPSDO_T6963C
  #include "T6963C_Bridge.h"

  static T6963C_Bridge s_lcd(PIN_T6963C_CS, PIN_T6963C_READY, T6963C_SPI_HZ);
  static bool s_t6963c_ok = false;

  /* Cache of last-drawn strings so we can skip a full repaint when nothing
   * changed (saves SPI traffic). Indexed: 0 freq,1 hdr-time,2 status,
   * 3 pwm,4 ina,5 sensor. */
  static char t69_prev[6][40];
  static uint8_t t69_prev_state = 0xFF;

  /* Panel geometry */
  #define T69_W      240
  #define T69_H      128
  #define T69_FREQ_Y  20
  #define T69_STAT_Y  60
  #define T69_VAL_Y   84
  #define T69_VROW    13
  #define T69_BAR_Y  120

  static void t6963c_init(void)
  {
      SPI.begin();
      s_lcd.begin();
      s_lcd.setReadyTimeout(1000);
      /* Wait for the bridge to finish its OWN power-on sequence before the
       * first frame. Both boards power up together, but the bridge only
       * enables its SPI2 slave AFTER u8g2.begin() (T6963C init over the slow
       * 8080 bus) and autotest() (renders a test pattern). Until then it
       * cannot receive a single byte, so an early splash is lost and the
       * autotest pattern stays on screen. 200 ms was too short; give a
       * generous margin. vTaskDelay yields (runs in vDisplayTask). */
      vTaskDelay(pdMS_TO_TICKS(800));
      s_t6963c_ok = true;
      for (uint8_t i = 0; i < 6; i++) t69_prev[i][0] = '\0';
      OUT_SERIAL.println("T6963C: bridge init (SPI1 PA5/PA7, CS=PB13, READY=PB12)");
  }

  /* Static boot splash: logo + subtitle + hardware checklist. No animation —
   * the bridge renders via batched SPI, so a smooth wave would be costly and
   * add little on a small mono panel. */
  static void t6963c_splash(bool oled_ok, bool lcd_ok, bool ht_ok,
                            bool aht_ok, bool bmp_ok, bool ina_ok)
  {
      (void)oled_ok; (void)lcd_ok; (void)ht_ok;
      /* One batch = one transaction = one READY wait, exactly like the
       * proven reference master. Sending each command as its own
       * transaction worked less reliably during bring-up. */
      s_lcd.batchBegin();
      s_lcd.clear();
      s_lcd.frameRect(0, 0, T69_W, T69_H);
      s_lcd.setFontPos(FPOS_TOP);
      s_lcd.setFont(FONT_LOGISOSO28);
      s_lcd.str(20, 4, "GPSDO");
      s_lcd.setFont(FONT_NCEN10);
      s_lcd.str(150, 18, PROGRAM_VERSION);
      s_lcd.setFont(FONT_6x13);
      s_lcd.str(8, 40, "GPS-Disciplined OCXO");
      s_lcd.hline(0, 56, T69_W);

      /* hardware checklist */
      char line[28];
      uint8_t y = 62;
      s_lcd.setFont(FONT_6x10);
      snprintf(line, sizeof(line), "AHT %s  BMP %s",
               aht_ok ? "ok" : "--", bmp_ok ? "ok" : "--");
      s_lcd.str(8, y, line); y += 12;
      snprintf(line, sizeof(line), "INA %s", ina_ok ? "ok" : "--");
      s_lcd.str(8, y, line); y += 12;
      s_lcd.str(8, y, "jmnlabs + Claude");
      s_lcd.setFontPos(FPOS_BASELINE);
      s_lcd.batchEnd(true);   /* pack + auto-SEND, one transaction */
  }

  /* One field, with change-cache. Returns true if it (re)drew. */
  static bool t69_field(uint8_t idx, int16_t x, int16_t y, uint8_t fontId,
                        const char *s)
  {
      if (strncmp(t69_prev[idx], s, sizeof(t69_prev[0]) - 1) == 0)
          return false;
      strncpy(t69_prev[idx], s, sizeof(t69_prev[0]) - 1);
      t69_prev[idx][sizeof(t69_prev[0]) - 1] = '\0';
      s_lcd.setFont(fontId);
      s_lcd.str(x, y, s);
      return true;
  }

  static void t6963c_update(const GpsData_t *g, const FreqSnap_t *f,
                            const CtrlData_t *c, const Uptime_t *u,
                            bool aht_ok, bool bmp_ok, bool ina_ok)
  {
      (void)u;
      char s[40];

      /* Decide the operating state (mirrors the TFT status bar). */
      uint8_t st;
      if      (c->holdover_mode && c->holdover_auto) st = 3;
      else if (c->holdover_mode)                     st = 2;
      else if (g->pos_valid)                         st = 1;
      else                                           st = 0;

      bool busy = (g_svin_active || g_warmup_active || g_calib_active);

      /* Build everything into one batched transaction. */
      s_lcd.batchBegin();
      s_lcd.setFontPos(FPOS_TOP);

      /* First update after the splash wipes the panel so the splash content
       * doesn't linger. In addition, force a periodic full repaint: if the
       * bridge is ever reset while the master keeps running (its autotest
       * pattern reappears and our per-field cache would otherwise suppress
       * redraws), this clears it and repaints everything within ~20 s, so
       * the screen self-heals without needing a master restart. */
      static bool     first_paint = true;
      static uint16_t repaint_ctr = 0;
      bool full_repaint = first_paint || (++repaint_ctr >= 20);
      if (full_repaint) {
          first_paint = false;
          repaint_ctr = 0;
          s_lcd.clear();
          for (uint8_t i = 0; i < 6; i++) t69_prev[i][0] = '\0';
          t69_prev_state = 0xFF;
      }

      /* ---- header: static title + live time (right) ---- */
      static bool hdr_drawn = false;
      if (full_repaint) hdr_drawn = false;   /* clear() wiped it — redraw */
      if (!hdr_drawn) {
          hdr_drawn = true;
          s_lcd.setFont(FONT_NCEN10);
          s_lcd.str(2, 1, PROGRAM_NAME);
          s_lcd.hline(0, 17, T69_W);
      }
      if (g->valid) snprintf(s, sizeof(s), "%02d:%02d:%02d", g->hours, g->mins, g->secs);
      else          snprintf(s, sizeof(s), "--:--:--");
      t69_field(1, 150, 1, FONT_NCEN10, s);

      /* ---- frequency (big) ---- */
      if (busy) {
          if (g_svin_active)
              snprintf(s, sizeof(s), "SVIN %us %um", (unsigned)g_svin_dur, (unsigned)g_svin_acc_m);
          else if (g_warmup_active)
              snprintf(s, sizeof(s), "WARMUP %us", (unsigned)g_warmup_remaining);
          else
              snprintf(s, sizeof(s), "CAL %us", (unsigned)g_calib_remaining);
          if (strncmp(t69_prev[0], s, sizeof(t69_prev[0]) - 1) != 0) {
              strncpy(t69_prev[0], s, sizeof(t69_prev[0]) - 1);
              s_lcd.setColor(0); s_lcd.box(0, T69_FREQ_Y, T69_W, 30); s_lcd.setColor(1);
              s_lcd.setFont(FONT_10x20);
              s_lcd.str(4, T69_FREQ_Y + 4, s);
          }
      } else {
          if      (f->full10000) { static char ff[20]; dtostrf(f->avg10000,12,3,ff); snprintf(s,sizeof(s),"%sHz",ff); }
          else if (f->full1000)  { static char ff[20]; dtostrf(f->avg1000, 12,3,ff); snprintf(s,sizeof(s),"%sHz",ff); }
          else if (f->full100)   { static char ff[20]; dtostrf(f->avg100,  12,2,ff); snprintf(s,sizeof(s),"%sHz",ff); }
          else if (f->full10)    { static char ff[20]; dtostrf(f->avg10,   12,1,ff); snprintf(s,sizeof(s),"%sHz",ff); }
          else if (f->calcfreqint > 0) { snprintf(s,sizeof(s),"%lu Hz",(unsigned long)f->calcfreqint); }
          else snprintf(s,sizeof(s),"no signal");
          if (strncmp(t69_prev[0], s, sizeof(t69_prev[0]) - 1) != 0) {
              strncpy(t69_prev[0], s, sizeof(t69_prev[0]) - 1);
              s_lcd.setColor(0); s_lcd.box(0, T69_FREQ_Y, T69_W, 30); s_lcd.setColor(1);
              s_lcd.setFont(FONT_LOGISOSO16);
              s_lcd.str(2, T69_FREQ_Y + 4, s);
          }
      }

      /* ---- status row: [STATE]  algo+trend   sats ---- */
      if (st != t69_prev_state) {
          t69_prev_state = st;
          const char *txt;
          switch (st) {
              case 1:  txt = "LOCK"; break;
              case 2:  txt = "HOLD"; break;
              case 3:  txt = "H-LOST"; break;
              default: txt = "NOFIX"; break;
          }
          /* inverted (filled) box behind the state word = colour substitute */
          s_lcd.setColor(0); s_lcd.box(0, T69_STAT_Y - 1, 64, 16); s_lcd.setColor(1);
          s_lcd.box(0, T69_STAT_Y - 1, 64, 16);          /* white block */
          s_lcd.setColor(0);                             /* black text on white */
          s_lcd.setFont(FONT_8x13B);
          s_lcd.str(4, T69_STAT_Y, txt);
          s_lcd.setColor(1);
      }
      snprintf(s, sizeof(s), "A%u %s", c->active_algo, c->trendstr);
      t69_field(2, 70, T69_STAT_Y, FONT_6x13, s);
      { char sat[16]; snprintf(sat, sizeof(sat), "%2d sat", g->sats);
        /* sats reuse cache slot 2's neighbour — draw unconditionally, cheap */
        s_lcd.setFont(FONT_6x13); s_lcd.str(168, T69_STAT_Y, sat); }

      /* ---- value rows ---- */
      { static char fv[10]; double vctl = ((double)c->avg_vctl_adc/4096.0)*3.3;
        dtostrf(vctl,5,3,fv);
        snprintf(s,sizeof(s),"PWM%5u %sV", c->pwm_output, fv); }
      t69_field(3, 4, T69_VAL_Y + 0*T69_VROW, FONT_6x13, s);

      if (ina_ok) { static char fv[10],fi[10];
          dtostrf(g_ina_volt,5,3,fv); dtostrf(g_ina_curr,6,2,fi);
          snprintf(s,sizeof(s),"%sV %smA",fv,fi); }
      else snprintf(s,sizeof(s),"INA ---");
      t69_field(4, 4, T69_VAL_Y + 1*T69_VROW, FONT_6x13, s);

      if (bmp_ok || aht_ok) {
          double t = bmp_ok ? g_bmp_temp : g_aht_temp;
          static char ft[8]; dtostrf(t,4,1,ft);
          if (bmp_ok) { static char fp[8]; dtostrf(g_bmp_pres,6,1,fp);
                        snprintf(s,sizeof(s),"%sC %shPa",ft,fp); }
          else        { static char fh[8]; dtostrf(g_aht_humi,4,1,fh);
                        snprintf(s,sizeof(s),"%sC %s%%rH",ft,fh); }
      } else snprintf(s,sizeof(s),"sensors ---");
      t69_field(5, 4, T69_VAL_Y + 2*T69_VROW, FONT_6x13, s);

      /* ---- survey-in progress bar (only while surveying) ---- */
      if (g_svin_active) {
          /* acc shrinks toward the limit; show inverse as % toward done.
           * Rough: 0..(2*limit) maps to 100..0%. */
          uint32_t lim = GPSDO_SVIN_ACC_LIMIT / 1000u; if (!lim) lim = 1;
          uint32_t am = g_svin_acc_m;
          uint8_t pct;
          if (am >= 2*lim) pct = 0;
          else if (am <= lim) pct = 100;
          else pct = (uint8_t)(100u - (100u * (am - lim)) / lim);
          s_lcd.progress(4, T69_BAR_Y, T69_W - 8, 6, pct);
      }

      s_lcd.setFontPos(FPOS_BASELINE);
      s_lcd.batchEnd(true);   /* pack + send everything, then SEND to panel */
  }
#endif /* GPSDO_T6963C */

/* ======================================================================
 * TM1637
 *
 * GPSDO_TM1637_6 — 6-digit: HH:MM:SS, colon blinks on even seconds
 * GPSDO_TM1637   — 4-digit: HH:MM,    colon blinks on even seconds
 * ====================================================================== */
#if defined(GPSDO_TM1637) || defined(GPSDO_TM1637_6)
  #include <TM1637Display.h>
  #define TM_CLK PA8
  #define TM_DIO PB4
  #ifdef GPSDO_TM1637_6
    static TM1637Display s_tm(TM_CLK, TM_DIO, 6);
  #else
    static TM1637Display s_tm(TM_CLK, TM_DIO, 4);
  #endif

  /* ---- TM1637 status segment patterns (same as André's original) ------
   *
   * mid_dashes  — displayed at startup / waiting for GPS fix
   *               All digits show a single middle horizontal bar (segment G).
   *               Communicates "device alive but no time data yet".
   *
   * low_oooo_s  — displayed when GPS fix is lost
   *               All digits show a lowercase 'o' (segments C,D,E,G).
   *               Communicates "searching for GPS fix".
   *
   * Both arrays are declared for the maximum digit count (6); the
   * setSegments() call passes m_digitCount as length so the correct
   * number of digits is always written regardless of 4 or 6 digit mode.
   * -------------------------------------------------------------------- */
  static const uint8_t mid_dashes[] = {
      SEG_G, SEG_G, SEG_G, SEG_G, SEG_G, SEG_G   /* - - - - - - */
  };
  static const uint8_t low_oooo_s[] = {
      SEG_C | SEG_D | SEG_E | SEG_G,   /* o */
      SEG_C | SEG_D | SEG_E | SEG_G,   /* o */
      SEG_C | SEG_D | SEG_E | SEG_G,   /* o */
      SEG_C | SEG_D | SEG_E | SEG_G,   /* o */
      SEG_C | SEG_D | SEG_E | SEG_G,   /* o */
      SEG_C | SEG_D | SEG_E | SEG_G    /* o */
  };
  /* "CAL " — shown during C/CT calibration */
  static const uint8_t seg_cal[] = {
      SEG_A | SEG_D | SEG_E | SEG_F,                 /* C */
      SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G, /* A */
      SEG_D | SEG_E | SEG_F,                         /* L */
      0, 0, 0                                        /* blank */
  };
#endif

/* ======================================================================
 * HT16K33 — 4-digit 7-segment clock display with colon, I2C (addr 0x70)
 *
 * Self-contained minimal driver (no external library).  Targets the
 * common Adafruit-style 0.56" clock backpack and its AliExpress clones:
 * display RAM layout — digits at addresses 0,2,6,8; colon at address 4
 * (bit 1).  Shows HH:MM, colon blinks each second, "oooo" when no fix.
 *
 * Shares the I2C bus → every transaction is wrapped in xWireMutex by
 * the caller (vDisplayTask).
 * ====================================================================== */
#ifdef GPSDO_HT16K33
  #include <Wire.h>

  /* 7-segment encoding: bit0=A .. bit6=G */
  static const uint8_t ht_digit[10] = {
      0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
  };
  #define HT_SEG_o    0x5C            /* lowercase o = c+d+e+g */
  #define HT_SEG_dash 0x40            /* dash = segment g (middle bar) */
  static bool s_ht_ok = false;

  static bool ht_cmd(uint8_t cmd)
  {
      Wire.beginTransmission(HT16K33_I2C_ADDR);
      Wire.write(cmd);
      return Wire.endTransmission() == 0;
  }

  /* Write 4 digit patterns + colon state to display RAM */
  static void ht_write(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                       bool colon)
  {
      Wire.beginTransmission(HT16K33_I2C_ADDR);
      Wire.write((uint8_t)0x00);          /* RAM address 0 */
      Wire.write(d0); Wire.write((uint8_t)0x00);
      Wire.write(d1); Wire.write((uint8_t)0x00);
      Wire.write((uint8_t)(colon ? 0x02 : 0x00));  /* addr 4: colon */
      Wire.write((uint8_t)0x00);
      Wire.write(d2); Wire.write((uint8_t)0x00);
      Wire.write(d3); Wire.write((uint8_t)0x00);
      Wire.endTransmission();
  }

  /* Init: oscillator on, display on, brightness; returns true if ACKed */
  static bool ht_init(void)
  {
      if (!ht_cmd(0x21)) return false;             /* oscillator on      */
      ht_cmd(0x81);                                 /* display on, no blink */
      ht_cmd(0xE0 | (HT16K33_BRIGHTNESS & 0x0F));   /* brightness         */
      /* Show "----" at startup, matching the TM1637 (mid_dashes) — both LED
       * clocks now signal "alive, waiting for GPS" the same way. (The
       * no-fix-during-operation indicator stays "oooo", set elsewhere.) */
      ht_write(HT_SEG_dash, HT_SEG_dash, HT_SEG_dash, HT_SEG_dash, false);
      return true;
  }
#endif /* GPSDO_HT16K33 */

extern int8_t g_time_offset;
extern bool   g_show_local_time;
extern volatile bool     g_calib_active;
extern volatile uint16_t g_calib_remaining;
extern volatile bool     g_warmup_active;
extern volatile uint16_t g_warmup_remaining;
extern volatile bool     g_svin_active;
extern volatile uint16_t g_svin_dur;
extern volatile uint16_t g_svin_acc_m;

/* -----------------------------------------------------------------------
 * day_of_week_str — Zeller's congruence → 3-letter English abbreviation.
 * Returns pointer to a static string: "Mon","Tue","Wed","Thu","Fri","Sat","Sun".
 * Handles the Gregorian calendar correctly for any date 2000-2099.
 * ----------------------------------------------------------------------- */
static const char *day_of_week_str(uint8_t day, uint8_t month, uint16_t year)
{
    static const char * const names[7] = {
        "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
    };
    /* Zeller: shift Jan/Feb to month 13/14 of prior year */
    int m = month, y = (int)year;
    if (m < 3) { m += 12; y--; }
    int k = y % 100;
    int j = y / 100;
    int h = ((int)day + (13*(m+1))/5 + k + k/4 + j/4 - 2*j) % 7;
    if (h < 0) h += 7;
    /* h: 0=Sat,1=Sun,2=Mon,...,6=Fri → map to Sun=0 */
    static const int zeller_to_sun[7] = {6, 0, 1, 2, 3, 4, 5};
    return names[zeller_to_sun[h]];
}

/* -----------------------------------------------------------------------
 * apply_time_offset — add g_time_offset hours to UTC h:m:s, adjust date.
 * year/month/day are adjusted if the offset crosses midnight.
 * ----------------------------------------------------------------------- */
static void apply_time_offset(uint8_t  utc_h,  uint8_t utc_m,  uint8_t utc_s,
                               uint8_t  utc_day, uint8_t utc_mon, uint16_t utc_yr,
                               uint8_t *lh, uint8_t *lm, uint8_t *ls,
                               uint8_t *ld, uint8_t *lmo, uint16_t *lyr)
{
    static const uint8_t days_in_month[13] = {
        0, 31,28,31,30,31,30,31,31,30,31,30,31
    };
    int h = (int)utc_h + (int)g_time_offset;
    *lm = utc_m; *ls = utc_s;
    *ld = utc_day; *lmo = utc_mon; *lyr = utc_yr;
    /* Wrap hours and adjust day */
    if (h >= 24) { h -= 24; (*ld)++; }
    if (h < 0)   { h += 24; if (*ld > 1) (*ld)--; else { /* underflow month */
        if (*lmo > 1) { (*lmo)--; } else { (*lmo) = 12; (*lyr)--; }
        uint8_t dim = days_in_month[*lmo];
        if (*lmo == 2 && (*lyr % 4 == 0)) dim = 29; /* leap year */
        *ld = dim; } }
    /* Overflow month */
    { uint8_t dim = days_in_month[*lmo];
      if (*lmo == 2 && (*lyr % 4 == 0)) dim = 29;
      if (*ld > dim) { *ld = 1; (*lmo)++; if (*lmo > 12) { *lmo = 1; (*lyr)++; } } }
    *lh = (uint8_t)h;
}

/* LTIC globals (defined in this file; referenced here for clarity) */
#ifdef GPSDO_LTIC
  /* g_ltic_voltage is defined as a global in this translation unit */
  /* g_ltic_adc_avg is also defined in this translation unit        */
#endif

/* ======================================================================
 * vDisplayTask
 *
 * Woken by vFreqRelayTask notification (~1 Hz per PPS) or after 1100 ms
 * fallback timeout. In tunnel mode, GpsTask sends the notification every
 * 1 s so all displays keep refreshing.
 * ====================================================================== */
void vDisplayTask(void *pvParameters)
{
    (void)pvParameters;

    /* ---- Hardware init ---- */

#ifdef GPSDO_OLED
    /*
     * OLED init — must hold xWireMutex.
     *
     * Root cause of TM1637 + OLED conflict:
     *   s_oled.begin() calls Wire.begin() which configures the STM32 hardware
     *   I2C peripheral. Without the mutex, vSensorTask (higher priority) can
     *   be mid-transaction on the same I2C peripheral. The resulting collision
     *   locks the I2C peripheral in a busy state — Wire hangs indefinitely,
     *   DisplayTask never reaches the TM1637 update code below, and the LED
     *   display appears completely dead.
     *
     *   The fix mirrors the LCD init pattern: short delay so the first sensor
     *   cycle finishes, then acquire the mutex before touching Wire.
     */
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_oled.begin();               /* also calls Wire.begin()           */
        /* u8x8.begin() does not verify the display is present — probe the
         * bus ourselves so the HW report reflects reality.               */
        s_oled_ok = i2c_probe(0x3C);
        if (s_oled_ok) {
            s_oled.clear();
            memset(oled_prev, 0, sizeof(oled_prev));
            /* Character-mode boot splash (U8x8, no graphics buffer):
             *   rows 0-1: big "GPSDO" via draw2x2String (uses a 2x2 font)
             *   row3: version   row5: sine accent   row7: footer
             * Replaced by the live clock once oled_splash_done. */
            s_oled.setFont(u8x8_font_chroma48medium8_r);
            s_oled.draw2x2String(3, 0, "GPSDO");        /* 10 cols, centred in 16 */
            { char vl[17]; int vlen = (int)strlen(PROGRAM_VERSION);
              int vcol = (16 - vlen) / 2; if (vcol < 0) vcol = 0;
              s_oled.drawString(vcol, 3, PROGRAM_VERSION); (void)vl; }
            s_oled.drawString(0, 5, "~~~~~~~~~~~~~~~~");
            s_oled.drawString(1, 7, "jmnlabs+Claude");   /* 14 ch, fits 16 col */
            /* Row 0 not cached → forced redraw to clock after splash */
        }
        xSemaphoreGive(xWireMutex);
        OUT_SERIAL.println(s_oled_ok ? "HW: OLED 128x64           OK  (I2C 0x3C)"
                                     : "HW: OLED 128x64           not found");
    } else {
        OUT_SERIAL.println("HW: OLED 128x64           init failed (I2C mutex timeout)");
    }
#endif /* GPSDO_OLED */

#ifdef GPSDO_LCD_20x4
    /* LCD init — must hold xWireMutex.
     *
     * vSensorTask has higher priority and starts using Wire before
     * vDisplayTask reaches this point. Without the mutex, lcd.begin()
     * collides with an ongoing I2C transaction → hd44780_I2Cexp fails
     * to detect the PCF8574T address → begin() returns non-zero → LCD
     * is marked not found even though hardware is present.
     *
     * Sequence:
     *   1. Short delay so vSensorTask finishes its first sensor read cycle.
     *   2. Acquire xWireMutex with generous timeout.
     *   3. Run lcd.begin() + backlight() under mutex.
     *   4. Release mutex, then do a separate guarded lcd_set_line() for
     *      the title row (lcd_set_line itself does NOT acquire the mutex —
     *      caller is responsible, matching the pattern used in the main loop).
     */
    vTaskDelay(pdMS_TO_TICKS(200));   /* let sensor task finish first I2C cycle */
    if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        int lcd_status = lcd.begin(20, 4);
        s_lcd_ok = (lcd_status == 0);
        if (s_lcd_ok) {
            lcd.backlight();
            memset(lcd_prev, 0, sizeof(lcd_prev));
        }
        xSemaphoreGive(xWireMutex);
    }
    if (s_lcd_ok) {
        /* 4-line boot splash (centred in 20 cols), held ~3 s:
         *   line0:  ====================
         *   line1:       GPSDO  vX.XX
         *   line2:  GPS-Disciplined OCXO
         *   line3:  jmnlabs  +  Claude
         * lcd_set_line uses I2C → guard with the Wire mutex.            */
        if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            char l[21];
            lcd_set_line(0, "====================");
            /* "  GPSDO vX.XX-rtos" — two leading spaces, suffix not cut */
            snprintf(l, sizeof(l), "  GPSDO %s", PROGRAM_VERSION);
            lcd_set_line(1, l);
            lcd_set_line(2, "GPS-Disciplined OCXO");
            lcd_set_line(3, " jmnlabs  +  Claude ");
            xSemaphoreGive(xWireMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(4500));
        /* Clear cache so the operating screen redraws all four lines */
        memset(lcd_prev, 0, sizeof(lcd_prev));
        OUT_SERIAL.println("HW: LCD 20x4              OK  (I2C expander)");
    } else {
        OUT_SERIAL.println("HW: LCD 20x4              not found (check I2C addr / wiring)");
    }
#endif

#if defined(GPSDO_TM1637) || defined(GPSDO_TM1637_6)
    /*
     * TM1637 init — must run AFTER any I2C library init (OLED, LCD).
     *
     * Root cause of TM1637 dead when LCD is enabled:
     *   hd44780_I2Cexp::begin() calls Wire.begin() which calls
     *   HAL_I2C_MspInit() → GPIO_Init for the I2C SCL/SDA pins.
     *   On STM32F411, I2C3 uses PA8 (SCL) and PC9 (SDA).
     *   If Wire is mapped to I2C3 in the board variant, Wire.begin()
     *   reconfigures PA8 from GPIO to AF4 (I2C3_SCL open-drain).
     *   TM1637 CLK is on PA8 — the pin is no longer GPIO-controllable.
     *   Result: TM1637 bit-bang produces no valid signal → blank display.
     *
     *   Even if Wire uses I2C1 (PB8/PB9), the explicit re-init below is
     *   a zero-cost safety measure that guarantees TM1637 pins are GPIO
     *   regardless of what any preceding I2C library init may have done.
     *
     * Fix: explicitly re-assert PA8 and PB4 as GPIO INPUT (open-drain
     * with pull-up, matching the TM1637 protocol's idle state) immediately
     * before the first TM1637 call.  This mirrors what the TM1637Display
     * constructor does and cannot conflict with any I2C peripheral because
     * the mutex is not held here.
     */
    pinMode(TM_CLK, INPUT);       /* PA8 → GPIO input (open-drain idle HIGH) */
    pinMode(TM_DIO, INPUT);       /* PB4 → GPIO input (open-drain idle HIGH) */
    digitalWrite(TM_CLK, LOW);    /* pre-load LOW so OUTPUT drive is LOW      */
    digitalWrite(TM_DIO, LOW);
    delayMicroseconds(500);       /* let pins settle after possible AF→GPIO transition */

    s_tm.setBrightness(1);        /* brightness 5/7 — clearly visible at startup */
    s_tm.clear();
    /* Show mid_dashes immediately — communicates "device alive, no GPS yet" */
    s_tm.setSegments(mid_dashes);
    OUT_SERIAL.println("HW: TM1637 clock display  enabled (GPIO PA8/PB4, write-only - not verifiable)");
#endif

#ifdef GPSDO_TFT
    /* TFT on exclusive SPI1 — no mutex needed, init directly */
    tft_init();
#endif

#ifdef GPSDO_T6963C
    /* SPI→T6963C bridge on exclusive SPI1 — init directly */
    t6963c_init();
#endif

#ifdef GPSDO_HT16K33
    /* HT16K33 shares I2C — init under Wire mutex.
     * Wire.begin() here is a safety net for builds where no other I2C
     * device initialised the bus first; calling it twice is harmless.
     * i2c_probe gives a reliable presence check before configuring.    */
    if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Wire.begin();
        s_ht_ok = i2c_probe(HT16K33_I2C_ADDR) && ht_init();
        xSemaphoreGive(xWireMutex);
    }
    OUT_SERIAL.println(s_ht_ok ? "HW: HT16K33 clock display OK  (I2C 0x70)"
                               : "HW: HT16K33 clock display not found");
#endif

#ifdef GPSDO_TFT
    /* All detection done — play the boot splash with the real hardware
     * checklist, then draw the operating-screen layout. */
    if (s_tft_ok) {
        /* Wait up to 1 s for vSensorTask to finish probing, so the
         * checklist reflects the sensors too (cosmetic only). */
        for (int w = 0; w < 100 && !s_sensors_probed; w++)
            vTaskDelay(pdMS_TO_TICKS(10));

        bool oled_ok = false, lcd_ok = false, ht_ok = false;
        bool aht_ok = false, bmp_ok = false, ina_ok = false;
#ifdef GPSDO_OLED
        oled_ok = s_oled_ok;
#endif
#ifdef GPSDO_LCD_20x4
        lcd_ok = s_lcd_ok;
#endif
#ifdef GPSDO_HT16K33
        ht_ok = s_ht_ok;
#endif
#ifdef GPSDO_AHT10
        aht_ok = s_aht_ok;
#endif
#ifdef GPSDO_BMP280_I2C
        bmp_ok = s_bmp_ok;
#endif
#ifdef GPSDO_INA219
        ina_ok = s_ina_ok;
#endif
        tft_splash(oled_ok, lcd_ok, ht_ok, aht_ok, bmp_ok, ina_ok);
        tft_draw_layout();
    }
#endif

#ifdef GPSDO_T6963C
    /* Same checklist splash on the T6963C bridge. */
    if (s_t6963c_ok) {
        for (int w = 0; w < 100 && !s_sensors_probed; w++)
            vTaskDelay(pdMS_TO_TICKS(10));

        bool oled_ok = false, lcd_ok = false, ht_ok = false;
        bool aht_ok = false, bmp_ok = false, ina_ok = false;
#ifdef GPSDO_OLED
        oled_ok = s_oled_ok;
#endif
#ifdef GPSDO_LCD_20x4
        lcd_ok = s_lcd_ok;
#endif
#ifdef GPSDO_HT16K33
        ht_ok = s_ht_ok;
#endif
#ifdef GPSDO_AHT10
        aht_ok = s_aht_ok;
#endif
#ifdef GPSDO_BMP280_I2C
        bmp_ok = s_bmp_ok;
#endif
#ifdef GPSDO_INA219
        ina_ok = s_ina_ok;
#endif
        /* Send the splash once the bridge has had time to finish its own
         * power-on init. Kept simple — one batch, like the proven reference. */
        t6963c_splash(oled_ok, lcd_ok, ht_ok, aht_ok, bmp_ok, ina_ok);
        vTaskDelay(pdMS_TO_TICKS(1500));   /* let the splash be readable */
    }
#endif

    /* ---- State for alternating displays ---- */
#ifdef GPSDO_OLED
    uint8_t  oled_page         = 0;              /* 0 = GPS focus, 1 = sensors */
    uint32_t oled_page_counter = 0;              /* PPS ticks on current page  */
    bool     oled_splash_done  = false;          /* true after 4.5-s version splash */
    uint32_t oled_splash_ms    = millis();       /* timestamp of OLED init       */
#endif

#ifdef GPSDO_LCD_20x4
    uint8_t  lcd_line2_mode    = 0;              /* 0-4: current line 2 view   */
    uint32_t lcd_line2_counter = 0;              /* PPS ticks on current view  */
    /* Holdover blink state for LCD line 3 */
    bool     lcd_hold_blink    = false;
    uint32_t lcd_hold_last     = 0;
#endif

    /* ---- Holdover blink state (shared by OLED) ---- */
    bool     holdover_blink_state = false;
    uint32_t holdover_blink_last  = 0;

    for (;;)
    {
        /* Wait for PPS notification from FreqRelayTask, or fall through
         * after 1100 ms so the display doesn't freeze without GPS. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1100));

        /* ---- Snapshot shared state ---- */
        FreqSnap_t snap_f;
        GpsData_t  snap_g;
        CtrlData_t snap_c;
        Uptime_t   snap_u;

        memset(&snap_f, 0, sizeof(snap_f));
        memset(&snap_g, 0, sizeof(snap_g));
        strcpy(snap_u.time_str, "00:00:00");
        strcpy(snap_u.days_str, "000d");

        if (xSemaphoreTake(xFreqMutex,   pdMS_TO_TICKS(5)) == pdTRUE) { snap_f = gFreqSnap; xSemaphoreGive(xFreqMutex); }
        if (xSemaphoreTake(xGpsMutex,    pdMS_TO_TICKS(5)) == pdTRUE) { snap_g = gGps;      xSemaphoreGive(xGpsMutex); }
        if (xSemaphoreTake(xCtrlMutex,   pdMS_TO_TICKS(5)) == pdTRUE) { snap_c = gCtrl;     xSemaphoreGive(xCtrlMutex); }
        if (xSemaphoreTake(xUptimeMutex, pdMS_TO_TICKS(5)) == pdTRUE) { snap_u = gUptime;   xSemaphoreGive(xUptimeMutex); }

        /* ---- Yellow LED state machine ----
         *
         * State | Condition                            | Behaviour
         * ------+--------------------------------------+----------------------
         * OFF   | No GPS fix                           | LED off (LOW)
         * ON    | Fix OK, no holdover                  | LED on steady (HIGH)
         * SLOW  | Fix OK, manual holdover (user MH)    | 1000 ms pulse
         * FAST  | Fix lost during operation,           | 200 ms pulse
         *       | auto-holdover engaged                |
         *
         * When fix returns after auto-holdover, control task clears
         * holdover_auto and holdover_mode → LED transitions back to ON.
         */
        {
            static uint32_t led_blink_last = 0;
            static bool     led_blink_state = false;
            uint32_t now_ms = millis();

            bool fix     = snap_g.pos_valid;
            bool hold    = snap_c.holdover_mode;
            bool hold_auto = snap_c.holdover_auto;

            if (!fix && !hold_auto) {
                /* No fix, no auto-holdover (never had fix or manual mode) */
                digitalWrite(PIN_YELLOW_LED, LOW);   /* OFF */
                led_blink_state = false;
            } else if (fix && !hold) {
                /* Fix acquired, disciplined mode */
                digitalWrite(PIN_YELLOW_LED, HIGH);  /* ON steady */
                led_blink_state = false;
            } else if (hold && !hold_auto) {
                /* Manual holdover — slow pulse 1000 ms */
                if ((now_ms - led_blink_last) >= LED_SLOW_BLINK_MS) {
                    led_blink_last  = now_ms;
                    led_blink_state = !led_blink_state;
                    digitalWrite(PIN_YELLOW_LED, led_blink_state ? HIGH : LOW);
                }
            } else {
                /* Auto-holdover (fix lost during operation) — fast pulse 200 ms */
                if ((now_ms - led_blink_last) >= LED_FAST_BLINK_MS) {
                    led_blink_last  = now_ms;
                    led_blink_state = !led_blink_state;
                    digitalWrite(PIN_YELLOW_LED, led_blink_state ? HIGH : LOW);
                }
            }
        }

        /* ---- Serial report (skipped when paused via RP command) ---- */
        if (!g_report_paused) {
            EventBits_t bits = xEventGroupGetBits(xSysEvents);
            if (bits & EVT_REPORT_TAB)
                print_tab_report  (&snap_g, &snap_f, &snap_c, &snap_u);
            else
                print_human_report(&snap_g, &snap_f, &snap_c, &snap_u);
        }

        /* ==============================================================
         * OLED update
         * ============================================================== */
#ifdef GPSDO_OLED
        if (s_oled_ok && xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
            char line[18];

            /* ---- Page switch ---- */
            if (++oled_page_counter >= OLED_PAGE_SWITCH_SECS) {
                oled_page_counter = 0;
                oled_page         = oled_page ^ 1u;   /* toggle 0↔1 */
                /* Invalidate rows 2–6 so they redraw for the new page */
                oled_invalidate_rows(2, 6);
            }

            /* ---- Row 0: version splash (2 s) then LMT clock + day ----
             * Splash: firmware version shown for 4500 ms after init.
             * Normal: "LMT:hh:mm:ss DAY" — local time + 3-letter day name (16 chars).
             * Format: "LMT:" (4) + "hh:mm:ss" (8) + " " (1) + "DAY" (3) = 16. */
            if (!oled_splash_done) {
                if ((millis() - oled_splash_ms) >= 4500UL) {
                    oled_splash_done = true;
                    /* The splash used draw2x2String (a 2x tile-doubled font)
                     * for the big GPSDO across rows 0-1, plus rows 3/5/7.
                     * clear() alone sometimes leaves tile remnants, so:
                     *   1. restore the normal 8x8 font (undo 2x2 state),
                     *   2. clear the whole panel,
                     *   3. blank every one of the 8 text rows explicitly,
                     *   4. invalidate the full row cache so the operating
                     *      screen redraws each row from scratch. */
                    if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        s_oled.setFont(u8x8_font_chroma48medium8_r);
                        s_oled.clear();
                        for (uint8_t r = 0; r < 8; r++) {
                            s_oled.setCursor(0, r);
                            s_oled.print("                ");  /* 16 spaces */
                        }
                        xSemaphoreGive(xWireMutex);
                    }
                    memset(oled_prev, 0, sizeof(oled_prev));
                }
                /* Still showing splash — don't touch row 0 */
            } else {
                /* LMT clock — computed every second */
                char row0[17];
                if (snap_g.valid) {
                    uint8_t lh, lm, ls, ld, lmo;
                    uint16_t lyr;
                    apply_time_offset(snap_g.hours, snap_g.mins, snap_g.secs,
                                      snap_g.day,   snap_g.month, snap_g.year,
                                      &lh, &lm, &ls, &ld, &lmo, &lyr);
                    const char *dow = day_of_week_str(ld, lmo, lyr);
                    snprintf(row0, sizeof(row0), "LMT:%02d:%02d:%02d %s",
                             lh, lm, ls, dow);
                } else {
                    snprintf(row0, sizeof(row0), "LMT:--:--:-- ---");
                }
                oled_set_line(0, row0);
            }

            /* ---- Row 1: frequency (identical on both pages) ----
             * Layout (16 chars): F + 13-char right-justified number + Hz
             *   Hz lands on positions 14-15 (last 2 chars of the 16-char row).
             *   e.g. "F 9999999.9999Hz"  (dtostrf width=13, 4 decimals)
             *        "F  9999999.999Hz"  (3 decimals)
             *        "F     10000000Hz"  (integer fallback) */
            if (g_svin_active)
                snprintf(line,sizeof(line),"F SVIN%3us %2um", (unsigned)g_svin_dur, (unsigned)g_svin_acc_m);
            else if (g_warmup_active)
                snprintf(line,sizeof(line),"F WARMUP %3us  ", (unsigned)g_warmup_remaining);
            else if (g_calib_active)
                snprintf(line,sizeof(line),"F  CAL %3us    ", (unsigned)g_calib_remaining);
            else if (snap_f.full10000)
                { static char ff[15]; dtostrf(snap_f.avg10000,13,4,ff); snprintf(line,sizeof(line),"F%sHz",ff); }
            else if (snap_f.full1000)
                { static char ff[15]; dtostrf(snap_f.avg1000, 13,3,ff); snprintf(line,sizeof(line),"F%sHz",ff); }
            else if (snap_f.full100)
                { static char ff[15]; dtostrf(snap_f.avg100,  13,2,ff); snprintf(line,sizeof(line),"F%sHz",ff); }
            else if (snap_f.full10)
                { static char ff[15]; dtostrf(snap_f.avg10,   13,1,ff); snprintf(line,sizeof(line),"F%sHz",ff); }
            else if (snap_f.calcfreqint > 0)
                { char ft[13]; ltoa(snap_f.calcfreqint,ft,10); snprintf(line,sizeof(line),"F%13sHz",ft); }
            else
                snprintf(line,sizeof(line),"F  no signal    ");
            oled_set_line(1, line);

            /* ---- Rows 2–6: page-dependent content ---- */
            if (oled_page == 0) {

                /* PAGE A: GPS position focus */
                if (snap_g.pos_valid) {
                    /* Row 2: latitude */
                    { static char ff[14]; dtostrf(snap_g.lat,10,5,ff); snprintf(line,sizeof(line),"La%s",ff); }
                    oled_set_line(2, line);
                    /* Row 3: longitude */
                    { static char ff[14]; dtostrf(snap_g.lon,10,5,ff); snprintf(line,sizeof(line),"Lo%s",ff); }
                    oled_set_line(3, line);
                    /* Row 4: altitude + satellites */
                    { static char fa[8]; dtostrf(snap_g.alt,5,0,fa); snprintf(line,sizeof(line),"Al%sm Sat:%2d",fa,snap_g.sats); }
                    oled_set_line(4, line);
                } else {
                    snprintf(line,sizeof(line),"GPS: acquiring  ");  oled_set_line(2,line);
                    snprintf(line,sizeof(line),"Sat: %2d         ",snap_g.sats); oled_set_line(3,line);
                    { static char ft[6],fp[7]; dtostrf(g_bmp_temp,4,1,ft); dtostrf(g_bmp_pres,5,0,fp); snprintf(line,sizeof(line),"BM:%sC%shPa",ft,fp); }
                    oled_set_line(4, line);
                }
                /* Row 5: uptime */
                snprintf(line,sizeof(line),"Up %s %s",snap_u.days_str,snap_u.time_str);
                oled_set_line(5, line);
                /* Row 6 (Page A): UTC date in dd/mm/yyyy format (10 chars).
                 * Date fits comfortably in 16 chars with no overflow risk.
                 * Time moved to Page B row 6; temperature shown on row 4 fallback. */
                if (snap_g.valid)
                    snprintf(line, sizeof(line), "%02d/%02d/%04u",
                             snap_g.day, snap_g.month, snap_g.year);
                else
                    snprintf(line, sizeof(line), "--/--/----");
                oled_set_line(6, line);

            } else {

                /* PAGE B: sensors + date focus */
                /* Row 2: BMP280 */
                /* BM: + temp(4,1) + C + pres(5,0) + hPa = 3+4+1+5+3 = 16 chars */
                { static char ft[6],fp[7]; dtostrf(g_bmp_temp,4,1,ft); dtostrf(g_bmp_pres,5,0,fp); snprintf(line,sizeof(line),"BM:%sC%shPa",ft,fp); }
                oled_set_line(2, line);
                /* Row 3: AHT */
                /* AH: + temp(4,1) + C + humi(5,1) + %rH = 3+4+1+5+3 = 16 chars */
                { static char ft[6],fh[7]; dtostrf(g_aht_temp,4,1,ft); dtostrf(g_aht_humi,5,1,fh); snprintf(line,sizeof(line),"AH:%sC%s%%rH",ft,fh); }
                oled_set_line(3, line);
                /* Row 4: INA219 */
                /* IN: + volt(5,2) + V + curr(5,0) + mA = 3+5+1+5+2 = 16 chars */
                { static char fv[7],fi[7]; dtostrf(g_ina_volt,5,2,fv); dtostrf(g_ina_curr,5,0,fi); snprintf(line,sizeof(line),"IN:%sV%smA",fv,fi); }
                oled_set_line(4, line);
                /* Row 5: satellites + HDOP */
                { static char fh[6];
                  if (snap_g.time_mode) {
                      snprintf(line,sizeof(line),"Sat:%2d HDOP:TIME",snap_g.sats);
                  } else {
                      dtostrf((double)snap_g.hdop/100.0,4,2,fh);
                      snprintf(line,sizeof(line),"Sat:%2d HDOP:%s",snap_g.sats,fh);
                  } }
                oled_set_line(5, line);
                /* Row 6 (Page B): UTC time + day of week.
                 * Format: "UTC:hh:mm:ss DAY" — exactly 16 chars.
                 * "UTC:" (4) + "hh:mm:ss" (8) + " " (1) + "DAY" (3) = 16. */
                if (snap_g.valid) {
                    const char *dow = day_of_week_str(snap_g.day, snap_g.month, snap_g.year);
                    snprintf(line, sizeof(line), "UTC:%02d:%02d:%02d %s",
                             snap_g.hours, snap_g.mins, snap_g.secs, dow);
                } else {
                    snprintf(line, sizeof(line), "UTC:--:--:-- ---");
                }
                oled_set_line(6, line);
            }

            /* ---- Row 7: PWM + trend (both pages) ----
             * First 15 chars: PWM and trend text.
             * Col 15 is reserved for holdover blink 'H' — managed separately. */
            {
                char pwm_line[17];
                snprintf(pwm_line, sizeof(pwm_line), "PWM:%5u %s",
                         snap_c.pwm_output, snap_c.trendstr);
                int llen = (int)strlen(pwm_line);
                while (llen < 15) pwm_line[llen++] = ' ';
                pwm_line[15] = ' ';   /* placeholder for blink char */
                pwm_line[16] = '\0';
                oled_set_line(7, pwm_line);
            }

            /* ---- Holdover blink: row 7, col 15 ----
             * 'H' blinks at HOLDOVER_BLINK_MS — manual holdover (user MH)
             * 'A' blinks at HOLDOVER_BLINK_MS — auto-holdover (fix lost) */
            {
                uint32_t now_ms = millis();
                if (snap_c.holdover_mode) {
                    char blink_ch = snap_c.holdover_auto ? 'A' : 'H';
                    if ((now_ms - holdover_blink_last) >= HOLDOVER_BLINK_MS) {
                        holdover_blink_last  = now_ms;
                        holdover_blink_state = !holdover_blink_state;
                        oled_set_last_char(7, holdover_blink_state ? blink_ch : ' ');
                    }
                } else {
                    if (holdover_blink_state) {
                        holdover_blink_state = false;
                        oled_set_last_char(7, ' ');
                    }
                }
            }

            xSemaphoreGive(xWireMutex);
        }
#endif /* GPSDO_OLED */

        /* ==============================================================
         * LCD 20x4 update
         *
         * Line 0: frequency
         * Line 1: UTC HH:MM:SS + uptime days
         * Line 2: rotating sensor/GPS view
         * Line 3: PWM + Vctl + trend (or blinking [H] holdover)
         * ============================================================== */
#ifdef GPSDO_LCD_20x4
        if (s_lcd_ok && xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
            char line[22];

            /* ---- Line 0: frequency ----
             * Layout (20 chars): "F:" + 15-char right-justified number + " Hz"
             *   full10000: "F:  10000000.0000 Hz"
             *   full1000:  "F:   10000000.000 Hz"
             *   full100:   "F:    10000000.00 Hz"
             *   full10:    "F:     10000000.0 Hz"
             *   integer:   "F:       10000000 Hz"  (right-justified in 15 chars + space + Hz) */
            if (g_svin_active)
                snprintf(line,sizeof(line),"F: SVIN %3us acc%2um", (unsigned)g_svin_dur, (unsigned)g_svin_acc_m);
            else if (g_warmup_active)
                snprintf(line,sizeof(line),"F: OCXO WARMUP %3us ", (unsigned)g_warmup_remaining);
            else if (g_calib_active)
                snprintf(line,sizeof(line),"F: CALIBRATING %3us ", (unsigned)g_calib_remaining);
            else if (snap_f.full10000)
                { static char ff[16]; dtostrf(snap_f.avg10000,15,4,ff); snprintf(line,sizeof(line),"F:%s Hz",ff); }
            else if (snap_f.full1000)
                { static char ff[16]; dtostrf(snap_f.avg1000, 15,3,ff); snprintf(line,sizeof(line),"F:%s Hz",ff); }
            else if (snap_f.full100)
                { static char ff[16]; dtostrf(snap_f.avg100,  15,2,ff); snprintf(line,sizeof(line),"F:%s Hz",ff); }
            else if (snap_f.full10)
                { static char ff[16]; dtostrf(snap_f.avg10,   15,1,ff); snprintf(line,sizeof(line),"F:%s Hz",ff); }
            else if (snap_f.calcfreqint > 0)
                { char ft[13]; ltoa(snap_f.calcfreqint,ft,10); snprintf(line,sizeof(line),"F:%15s Hz",ft); }
            else
                snprintf(line,sizeof(line),"F: no signal        ");
            lcd_set_line(0, line);

            /* ---- Line 1: UTC time + uptime days ---- */
            /* Format: "UTC:HH:MM:SS Up000d" = 19 chars */
            if (snap_g.valid)
                snprintf(line,sizeof(line),"UTC:%02d:%02d:%02d Up %s",
                         snap_g.hours,snap_g.mins,snap_g.secs,snap_u.days_str);
            else
                snprintf(line,sizeof(line),"UTC:--:--:-- Up %s",snap_u.days_str);
            lcd_set_line(1, line);

            /* ---- Line 2: rotating view ----
             * Modes: 0=GPS coords, 1=Sats+HDOP, 2=Date+day, 3=AHT, 4=INA, 5=BMP
             * Skips unavailable modes automatically. */
            if (++lcd_line2_counter >= LCD_LINE2_SWITCH_SECS) {
                lcd_line2_counter = 0;
                /* Advance to next available mode */
                for (uint8_t attempt = 0; attempt < 6; attempt++) {
                    lcd_line2_mode = (lcd_line2_mode + 1) % 6;
                    bool avail = false;
                    if (lcd_line2_mode == 0) avail = snap_g.pos_valid;
                    if (lcd_line2_mode == 1) avail = snap_g.pos_valid;
                    if (lcd_line2_mode == 2) avail = snap_g.valid;
                    if (lcd_line2_mode == 3) avail = s_aht_ok;
                    if (lcd_line2_mode == 4) avail = s_ina_ok;
                    if (lcd_line2_mode == 5) avail = s_bmp_ok;
                    if (avail) break;
                }
            }

            switch (lcd_line2_mode) {
                case 0: /* GPS coordinates + sats */
                    if (snap_g.pos_valid) {
                        static char fl[8], fn[8];
                        dtostrf(snap_g.lat,6,3,fl); dtostrf(snap_g.lon,7,3,fn);
                        snprintf(line,sizeof(line),"La:%s Lo:%s S:%2d",fl,fn,snap_g.sats);
                    } else {
                        snprintf(line,sizeof(line),"GPS:no fix          ");
                    }
                    break;
                case 1: /* Satellites + HDOP */
                    if (snap_g.pos_valid && snap_g.time_mode) {
                        snprintf(line,sizeof(line),"Sats:%2d  HDOP:TIME  ",snap_g.sats);
                    } else if (snap_g.pos_valid) {
                        static char fhd[6]; dtostrf((double)snap_g.hdop/100.0,4,2,fhd);
                        snprintf(line,sizeof(line),"Sats:%2d  HDOP:%s",snap_g.sats,fhd);
                    } else {
                        snprintf(line,sizeof(line),"Sats:%2d  HDOP:-.--",snap_g.sats);
                    }
                    break;
                case 2: /* Date + day of week + local time hh:mm (20 chars) */
                    /* Format: "DD/MM/YYYY DAY hh:mm" */
                    if (snap_g.valid) {
                        uint8_t lh, lm, ls, ld, lmo;
                        uint16_t lyr;
                        apply_time_offset(snap_g.hours, snap_g.mins, snap_g.secs,
                                          snap_g.day,   snap_g.month, snap_g.year,
                                          &lh, &lm, &ls, &ld, &lmo, &lyr);
                        const char *dow = day_of_week_str(ld, lmo, lyr);
                        snprintf(line,sizeof(line),"%02d/%02d/%04d %s %02d:%02d",
                                 ld, lmo, (int)lyr, dow, lh, lm);
                    } else {
                        snprintf(line,sizeof(line),"--/--/---- --- --:--");
                    }
                    break;
                case 3: /* AHT */
                    if (s_aht_ok) {
                        static char ft[6],fh[6];
                        dtostrf(g_aht_temp,4,1,ft); dtostrf(g_aht_humi,4,1,fh);
                        snprintf(line,sizeof(line),"AHT:%sC %s%%rH",ft,fh);
                    } else {
                        snprintf(line,sizeof(line),"AHT: not found      ");
                    }
                    break;
                case 4: /* INA219 */
                    if (s_ina_ok) {
                        static char fv[6],fi[7];
                        dtostrf(g_ina_volt,4,2,fv); dtostrf(g_ina_curr,5,0,fi);
                        snprintf(line,sizeof(line),"INA:%sV %smA",fv,fi);
                    } else {
                        snprintf(line,sizeof(line),"INA: not found      ");
                    }
                    break;
                case 5: /* BMP280 */
                default:
                    if (s_bmp_ok) {
                        static char ft[6],fp[7];
                        dtostrf(g_bmp_temp,4,1,ft); dtostrf(g_bmp_pres,5,1,fp);
                        snprintf(line,sizeof(line),"BMP:%sC %shPa",ft,fp);
                    } else {
                        snprintf(line,sizeof(line),"BMP: not found      ");
                    }
                    break;
            }
            lcd_set_line(2, line);

            /* ---- Line 3: PWM + Vctl + indicator (20 chars) ----
             *
             * Layout: "PWM:XXXXX V:X.XX IND"
             *   "PWM:" = 4, pwm = 5, " V:" = 3, vctl = 4, " " = 1, ind = 3
             *   Total = 20 chars exactly (Vctl stays 0.00..3.30 = 4 chars max)
             *
             * Indicator field (cols 17-19, 3 chars):
             *   Normal mode:   first 3 chars of trendstr (no leading space).
             *     Examples: "hit", "f+ ", "p- ", "FLL", "PLL", "HYB", "NN+", "___"
             *   Manual holdover: "[H]" blinks with "   " at HOLDOVER_BLINK_MS rate.
             *   Auto-holdover:   "[A]" blinks with "   " at HOLDOVER_BLINK_MS rate. */
            {
                static char fv_str[7];
                double vctl = ((double)snap_c.avg_vctl_adc / 4096.0) * 3.3;
                dtostrf(vctl, 4, 2, fv_str);   /* always 4 chars for 0.00..3.30 V */

                /* Build indicator string (exactly 3 chars + NUL) */
                char ind[4];
                if (snap_c.holdover_mode) {
                    /* Update blink timer — independent of display refresh rate */
                    uint32_t now_ms = millis();
                    if ((now_ms - lcd_hold_last) >= HOLDOVER_BLINK_MS) {
                        lcd_hold_last  = now_ms;
                        lcd_hold_blink = !lcd_hold_blink;
                    }
                    const char *htag = snap_c.holdover_auto ? "[A]" : "[H]";
                    strcpy(ind, lcd_hold_blink ? htag : "   ");
                } else {
                    lcd_hold_blink = false;   /* reset when leaving holdover */
                    /* trendstr is always 4 chars, no leading space — display first 3 */
                    snprintf(ind, sizeof(ind), "%-3.3s", snap_c.trendstr);
                }

                snprintf(line, sizeof(line), "PWM:%5u V:%s %s",
                         snap_c.pwm_output, fv_str, ind);
                lcd_set_line(3, line);
            }

            xSemaphoreGive(xWireMutex);
        }
#endif /* GPSDO_LCD_20x4 */

        /* ==============================================================
         * TFT 240x320 update (SPI1 — exclusive bus, no mutex)
         * ============================================================== */
#ifdef GPSDO_TFT
        if (s_tft_ok)
            tft_update(&snap_g, &snap_f, &snap_c, &snap_u,
                       s_aht_ok, s_bmp_ok, s_ina_ok);
#endif

#ifdef GPSDO_T6963C
        if (s_t6963c_ok)
            t6963c_update(&snap_g, &snap_f, &snap_c, &snap_u,
                          s_aht_ok, s_bmp_ok, s_ina_ok);
#endif

        /* ==============================================================
         * TM1637 clock update
         *
         * Updates only when GPS time is valid (snap_g.valid).
         * In tunnel mode gGps retains the last valid time, so TM1637
         * continues showing it without freezing.
         * ============================================================== */
#if defined(GPSDO_TM1637) || defined(GPSDO_TM1637_6)
        /* On the very first loop iteration, re-assert TM1637 GPIO state.
         * This catches any late Wire.begin() calls (e.g. from hd44780
         * auto-detect rescanning the bus on the first real LCD update)
         * that might reconfigure PA8 after the init block ran. */
        {
            static bool tm_pins_ok = false;
            if (!tm_pins_ok) {
                tm_pins_ok = true;
                pinMode(TM_CLK, INPUT);
                pinMode(TM_DIO, INPUT);
                digitalWrite(TM_CLK, LOW);
                digitalWrite(TM_DIO, LOW);
                delayMicroseconds(500);
                s_tm.clear();
                s_tm.setSegments(mid_dashes);
            }
        }
        /* No GPS position fix → 'oooo'; fix → time; calibration → CAL.
         * pos_valid (not valid) is the true "GPS locked" indicator:
         * valid turns true on the first NMEA time sentence, before any
         * position lock, so it would show a stale 00:00:00. */
        if (g_svin_active || g_warmup_active) {
            s_tm.setSegments(mid_dashes, TM1637_MAX_DIGITS);  /* ------ svin/warmup */
        } else if (g_calib_active) {
            s_tm.setSegments(seg_cal, TM1637_MAX_DIGITS);
        } else if (!snap_g.pos_valid) {
            s_tm.setSegments(low_oooo_s, TM1637_MAX_DIGITS);
        } else {
            int h = snap_g.hours;
            if (g_show_local_time) {
                h += g_time_offset;
                if (h >= 24) h -= 24;
                else if (h < 0) h += 24;
            }
#ifdef GPSDO_TM1637_6
            /* 6-digit: HHMMSS, colons blink on even seconds.
             *
             * Dots mask 0b00100100 (= 0x24):
             *   showDots() shifts left from MSB: bit5 → visual pos 2,
             *   bit2 → visual pos 5.
             *   With digitMap {2,1,0,5,4,3}: visual 2 → reg 0, visual 5 → reg 3.
             *   Physical registers 0 and 3 carry the colon DP segments on the
             *   diymore-style 6-digit TM1637 module.
             *   Source: library header comment "HH:MM:SS → dots = 0b00100100".
             *
             * Previous code used 0b01010000 (visual pos 1,3 → regs 1,5 —
             * those registers have no colon hardware → colons never lit).
             */
            long t = (long)h * 10000L + (long)snap_g.mins * 100L + snap_g.secs;
            if ((snap_g.secs & 1) == 0)
                s_tm.showNumberDecEx(t, 0b01010000, true);
            else
                s_tm.showNumberDec(t, true);
#else
            /* 4-digit: HHMM, colon blinks on even seconds */
            long t = (long)h * 100L + snap_g.mins;
            if ((snap_g.secs & 1) == 0)
                s_tm.showNumberDecEx(t, 0b01000000, true);
            else
                s_tm.showNumberDec(t, true);
#endif
        } /* else snap_g.valid */
#endif /* TM1637 */

        /* ==============================================================
         * HT16K33 clock update — HH:MM, colon blinks each second
         *
         * Same status logic as TM1637: "oooo" while searching for a fix,
         * time once pos_valid.  Local time per g_show_local_time / TO.
         * I2C bus shared → wrapped in xWireMutex.
         * ============================================================== */
#ifdef GPSDO_HT16K33
        if (s_ht_ok &&
            xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            if (g_svin_active || g_warmup_active) {
                /* survey-in / warmup → four dashes (segment G) */
                ht_write(0x40, 0x40, 0x40, 0x40, false);
            } else if (g_calib_active) {
                /* "CAL" + tens-of-seconds digit, colon off.
                 * 7-seg: C=A,D,E,F ; A=A,B,C,E,F,G ; L=D,E,F */
                const uint8_t SEG_C7 = 0x39, SEG_A7 = 0x77, SEG_L7 = 0x38;
                uint8_t d = ht_digit[(g_calib_remaining / 10) % 10];
                ht_write(SEG_C7, SEG_A7, SEG_L7, d, false);
            } else if (!snap_g.pos_valid) {
                ht_write(HT_SEG_o, HT_SEG_o, HT_SEG_o, HT_SEG_o, false);
            } else {
                int h = snap_g.hours;
                if (g_show_local_time) {
                    h += g_time_offset;
                    if      (h >= 24) h -= 24;
                    else if (h <  0)  h += 24;
                }
                ht_write(ht_digit[h / 10],          ht_digit[h % 10],
                         ht_digit[snap_g.mins / 10], ht_digit[snap_g.mins % 10],
                         (snap_g.secs & 1) == 0);   /* colon on even seconds */
            }
            xSemaphoreGive(xWireMutex);
        }
#endif /* GPSDO_HT16K33 */

    } /* for(;;) */
}
