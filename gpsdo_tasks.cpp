/**
 * gpsdo_tasks.cpp — Sensor, Display and Uptime tasks
 *
 * Part of GPSDO FreeRTOS v0.24
 * Author:   J. M. Niewiński
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * vSensorTask   — reads AHT/BMP/INA sensors every 2 s under xWireMutex
 * vDisplayTask  — drives OLED, LCD, TM1637, serial report, and LEDs
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

extern float g_pressure_offset;
extern float g_altitude_offset;

/* ======================================================================
 * vSensorTask — reads slow I2C sensors every 2 s
 * ====================================================================== */
void vSensorTask(void *pvParameters)
{
    (void)pvParameters;

#ifdef GPSDO_AHT10
    s_aht_ok = s_aht.begin();
    if (!s_aht_ok) OUT_SERIAL.println("AHT10 not found");
#endif
#ifdef GPSDO_BMP280_I2C
    s_bmp_ok = s_bmp.begin(0x77, 0x58);
    if (s_bmp_ok) {
        s_bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                          Adafruit_BMP280::SAMPLING_X2,
                          Adafruit_BMP280::SAMPLING_X16,
                          Adafruit_BMP280::FILTER_X16,
                          Adafruit_BMP280::STANDBY_MS_500);
    } else {
        OUT_SERIAL.println("BMP280 not found");
    }
#endif
#ifdef GPSDO_INA219
    s_ina_ok = s_ina.begin();
    if (s_ina_ok) s_ina.setCalibration_32V_1A();
    else          OUT_SERIAL.println("INA219 not found");
#endif

#ifdef GPSDO_LTIC
    /* Initialise PA1 as analog input and take the first reading */
    pinMode(PIN_LTIC_VPHASE, INPUT_ANALOG);
    analogRead(PIN_LTIC_VPHASE);   /* dummy read to settle ADC mux */
    ltic_read();
    OUT_SERIAL.println("LTIC PA1 configured");
#endif

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
        p=sa(buf,p," HDOP:"); p=sd(buf,p,(double)g->hdop/100.0,2);
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
#endif

extern int8_t g_time_offset;
extern bool   g_show_local_time;

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
        s_oled.begin();
        s_oled.setFont(u8x8_font_chroma48medium8_r);
        s_oled.clear();
        memset(oled_prev, 0, sizeof(oled_prev));
        /* Row 0: version splash — shown for 2 s, then replaced by LMT clock */
        s_oled.setCursor(0, 0);
        s_oled.print(PROGRAM_NAME " " PROGRAM_VERSION);
        /* Do NOT copy to oled_prev[0] — forces redraw after splash expires */
        s_oled_ok = true;
        xSemaphoreGive(xWireMutex);
        OUT_SERIAL.println("OLED OK");
    } else {
        OUT_SERIAL.println("OLED: mutex timeout at init");
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
        /* Write title row under mutex (lcd_set_line uses I2C) */
        if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            lcd_set_line(0, PROGRAM_NAME " " PROGRAM_VERSION);
            xSemaphoreGive(xWireMutex);
        }
        /* Hold the splash screen for 2 seconds before the main loop starts */
        vTaskDelay(pdMS_TO_TICKS(2000));
        OUT_SERIAL.println("LCD 20x4 OK");
    } else {
        OUT_SERIAL.println("LCD 20x4 not found — check I2C address / wiring");
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

    s_tm.setBrightness(5);        /* brightness 5/7 — clearly visible at startup */
    s_tm.clear();
    /* Show mid_dashes immediately — communicates "device alive, no GPS yet" */
    s_tm.setSegments(mid_dashes);
    OUT_SERIAL.println("TM1637 init OK");
#endif

    /* ---- State for alternating displays ---- */
#ifdef GPSDO_OLED
    uint8_t  oled_page         = 0;              /* 0 = GPS focus, 1 = sensors */
    uint32_t oled_page_counter = 0;              /* PPS ticks on current page  */
    bool     oled_splash_done  = false;          /* true after 2-s version splash */
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
             * Splash: firmware version shown for 2000 ms after init.
             * Normal: "LMT:hh:mm:ss DAY" — local time + 3-letter day name (16 chars).
             * Format: "LMT:" (4) + "hh:mm:ss" (8) + " " (1) + "DAY" (3) = 16. */
            if (!oled_splash_done) {
                if ((millis() - oled_splash_ms) >= 2000UL) {
                    oled_splash_done = true;
                    /* Force row 0 redraw on next iteration */
                    memset(oled_prev[0], 0, sizeof(oled_prev[0]));
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
            if (snap_f.full10000)
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
                { static char fh[6]; dtostrf((double)snap_g.hdop/100.0,4,2,fh); snprintf(line,sizeof(line),"Sat:%2d HDOP:%s",snap_g.sats,fh); }
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
            if (snap_f.full10000)
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
                    if (snap_g.pos_valid) {
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
        if (!snap_g.pos_valid) {
            /* No GPS position fix — show lowercase 'o' on all digits.
             *
             * Condition changed from snap_g.valid (time) to snap_g.pos_valid
             * (position fix). Reason: snap_g.valid becomes true as soon as
             * TinyGPS++ receives the first NMEA time sentence — typically
             * within seconds of power-on, before any position lock. The
             * module sends time data even with 0 satellites in view, so
             * valid=true while showing 00:00:00 or a stale time value.
             *
             * snap_g.pos_valid is only set when gps.location.isValid() is
             * true AND a GGA/RMC sentence with a proper fix has been decoded.
             * This is the correct "GPS locked" indicator for status display.
             *
             * Behaviour:
             *   No fix (searching)  → low_oooo_s  (o o o o o o)
             *   Fix acquired        → time display (HH:MM:SS or HH:MM)
             */
            /* Pass TM1637_MAX_DIGITS (6); the library clips to m_digitCount */ 
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

    } /* for(;;) */
}
