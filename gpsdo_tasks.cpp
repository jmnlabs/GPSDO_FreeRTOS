/**
 * gpsdo_tasks.cpp — Sensor, Display and Uptime tasks
 *
 * Part of GPSDO FreeRTOS v0.95
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
#include "ubx_timtp.h"
#include "GPSDO_algorithms.h"   /* g_ltic (calibrated span) for the ADC outlier gate */
#include <Arduino.h>
#include <string.h>
#include <math.h>
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
volatile bool g_ltic_must_read = false;
int16_t       g_ltic_adc_raw   = 0;
int16_t       g_ltic_adc_avg   = 0;
float         g_ltic_voltage   = 0.0f;

/* ltic_read_fast — read PA1 (the TIC ramp) ~50 µs after the PPS edge, from
 * vFreqRelayTask (which is woken directly by the GPS-PPS ISR). Oversamples
 * within the one PPS slot, takes the median, applies an outlier/dead-zone
 * gate, and publishes g_ltic_voltage. Crucially it does NOT discharge and
 * NEVER switches the pin to open-drain: the Kaashoek detector self-clears
 * through leakage (~25 ms << 1 s pulse spacing), and driving the pin low was
 * both unnecessary and a way to corrupt the very charge we want to measure.
 * The pin stays INPUT_ANALOG for the whole life of the program. */
void ltic_read_fast(void)
{
    /* Settle onto the ramp peak. Charging ends on the picPPS edge, at most
     * ~2 µs after the GPS-PPS edge that woke the caller; 50 µs clears the
     * widest pulse while sitting only ~1 % down the ~5 ms leakage decay. */
    delayMicroseconds(50);

    int16_t s[LTIC_OVERSAMPLE];
    for (int i = 0; i < LTIC_OVERSAMPLE; i++) {
        int16_t x = (int16_t)analogRead(PIN_LTIC_VPHASE);
        int j = i;
        while (j > 0 && s[j-1] > x) { s[j] = s[j-1]; j--; }
        s[j] = x;
    }
    int16_t med = s[LTIC_OVERSAMPLE / 2];
    g_ltic_adc_raw = med;

    static int16_t last_ok = -1, pending = -1;
    int16_t thr = 120;
    if (g_ltic.range_ns > 1.0f && g_ltic.ns_per_volt > 1.0f) {
        float span_v = g_ltic.range_ns / g_ltic.ns_per_volt;
        thr = (int16_t)(0.25f * span_v * 4096.0f / 3.3f);
        if (thr < 60)  thr = 60;
        if (thr > 400) thr = 400;
    }
    int16_t accepted;
    if (last_ok < 0 || abs(med - last_ok) <= thr) {
        accepted = med;  pending = -1;
    } else if (pending >= 0 && abs(med - pending) <= thr) {
        accepted = med;  pending = -1;
    } else {
        pending = med;   accepted = last_ok;
    }
    last_ok = accepted;
    g_ltic_adc_avg = accepted;
    g_ltic_voltage = ((float)accepted / 4096.0f) * 3.3f;
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
    ltic_read_fast();              /* no discharge; pin stays analog */
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
        /* Vphase is now sampled in vFreqRelayTask ~300 µs after the PPS edge
         * (ltic_read_fast), on the ramp peak. The old read here fired from
         * this 2 s loop — far too late for a <1 ms peak — and its open-drain
         * discharge actively corrupted the charge. Disabled; the flag is left
         * for compatibility but no longer drives a read/discharge. */
        g_ltic_must_read = false;
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
    if (f->full10000) { p=sa(buf,p," 10ks:");  p=sd(buf,p,f->avg10000,4); }
    buf[p++]='\r'; buf[p++]='\n';

    /* PWM / Vctl */
    p=sa(buf,p,"PWM:"); p=si(buf,p,c->pwm_output);
    p=sa(buf,p,"  Vctl:"); p=sd(buf,p,((double)c->avg_vctl_adc/4096.0)*3.3,3);
    p=sa(buf,p,"V");
    if (c->holdover_mode) p=sa(buf,p," [HOLDOVER]");
    else { p=sa(buf,p," "); p=sa(buf,p,c->trendstr); }
    /* live self-learning telemetry: always shown when LRN is on, so the user
     * can see it is active even before it has gathered enough to act on */
    if (g_lrn_enable) {
        /* algorithm number + short name, so the log says which loop is learning */
        static const char *algo_name[] = {
            "primitive", "forced-drift", "random-walk", "FLL-PID-man",
            "PLL-PI-man", "PLL-PID-man", "FLL-PID-gen", "PLL-PID-gen",
            "hybrid-FLL-PLL", "NN-MLP", "LTIC-3stage" };
        p=sa(buf,p,"\r\nLearn: algo="); p=si(buf,p,c->active_algo);
        p=sa(buf,p," (");
        p=sa(buf,p,(c->active_algo <= 10) ? algo_name[c->active_algo] : "?");
        p=sa(buf,p,") drift="); p=sd(buf,p,(double)g_lrn_drift,1);
        p=sa(buf,p,"LSB slope="); p=sd(buf,p,(double)g_lrn_slope_ns_s,3);
        p=sa(buf,p,"ns/s damp="); p=sd(buf,p,(double)g_lrn_damp,3);
        if (g_lrn_osc_period) { p=sa(buf,p," osc="); p=si(buf,p,g_lrn_osc_period);
                                p=sa(buf,p,"s/"); p=sd(buf,p,(double)g_lrn_osc_amp_ns,1); p=sa(buf,p,"ns"); }
        else                  { p=sa(buf,p," (gathering)"); }
        if (c->active_algo == 9) {   /* NN learned oscillator tempco */
            p=sa(buf,p," tempco="); p=sd(buf,p,(double)g_nn_tempco,1);
            p=sa(buf,p,"LSB/C");
        }
        /* Not gated on the algorithm: dph now has the sawtooth removed whatever
         * is steering, so the log has to show what was subtracted or the figure
         * cannot be checked afterwards. */
        if (g_qerr_enable) {                          /* sawtooth correction */
            p=sa(buf,p," qErr=");
            if (g_qerr_valid) { p=sd(buf,p,(double)g_qerr_ns,1); p=sa(buf,p,"ns"); }
            else              { p=sa(buf,p,"(wait)"); }
        }
    } else {
        p=sa(buf,p,"\r\nLearn: off");
    }
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
    /* Derived phase in ns, once LC has calibrated the detector. Measured
     * relative to zero_offset (mid-band), using the measured ns_per_volt —
     * same convention as the loop's ltic_phase_error_ns() and the TFT row. */
    if (g_ltic.ns_per_volt > 1.0f) {
        double centre = (g_ltic.zero_offset > 0.001f)
                        ? (double)g_ltic.zero_offset : 0.22;
        double ph_ns = ((double)g_ltic_voltage - centre)
                       * (double)g_ltic.ns_per_volt;
        p=sa(buf,p," dph:"); p=sd(buf,p,ph_ns,1); p=sa(buf,p,"ns");
    }
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
 *                (480 panel only: the Alt cell is split and its right half
 *                 carries qErr, so the fix data sits together; Vcc — the 5 V
 *                 rail — then sits beside Vdd in the phase row below, so the
 *                 supplies sit together too. At 320 the Alt cell stays whole
 *                 and qErr stays in the phase row: Alt and qErr want ~168 px
 *                 there and the cell is 148.)
 *  y=156..195 ── sensor row, font 2:
 *                  BMP:23.4C 1013hPa │  AHT:22.1C 45.3%rH
 *                (480 panel: two rows, grouped by column rather than by
 *                 sensor — BMP and AHT take the left column, the phase field
 *                 and the supply rails the right:
 *                  BMP: 23.40 C 1013.25 hPa │ Vph: 3.077 V dph: +1390ns
 *                  AHT: 22.10 C    45.30 % rH │ Vcc: 4.98 V     Vdd: 3.29 V
 *                 Without LTIC the phase field is absent and its half stays
 *                 empty; the rails do not depend on it.)
 *  y=204..239 ── status bar, font 4, full width, colour-coded background:
 *                  green  "DISCIPLINED  FIX OK"
 *                  orange "HOLDOVER (manual)"
 *                  red    "HOLDOVER (fix lost)" / "WAITING FOR GPS FIX"
 *                A background survey-in appends itself to whichever line is
 *                showing — "DISCIPLINED  FIX OK SURVEY" ("SV" at 320) — and
 *                disappears when the receiver reaches Time Mode.
 *
 * Selective redraw: every value cell caches its previous string and is
 * redrawn only on change (setTextPadding clears the old glyphs).
 * ====================================================================== */
#ifdef GPSDO_TFT
  #include <TFT_eSPI.h>   /* pulls in Extensions/Sprite.h → TFT_eSprite */

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
  #define TFT_COL_LABEL   0x0C1Fu        /* brighter navy — data labels */
  #define TFT_COL_VALUE   TFT_WHITE      /* white — data values          */

  /* Frame/separator colour — white on both panels. Besides matching the big
   * panel, this is what lets the 1-bit data sprite carry the frame itself:
   * that sprite has exactly two colours (white and background), so a navy
   * frame could not be drawn into it and had to be repainted on the panel
   * after every push. White means frame and text go out together in one
   * atomic transfer. */
  #define TFT_COL_FRAME   TFT_WHITE
  #define TFT_COL_LOCK    0x07E0u        /* green                        */
  #define TFT_COL_FREQ    TFT_WHITE      /* white — frequency (green on lock) */
  #define TFT_COL_HOLD    0xFC60u        /* orange              */
  #define TFT_COL_ALERT   0xF800u        /* red                 */
  /* Yellow. Reserved: it carried the SURVEY notice in the header until that
   * moved onto the status bar, where the text is black on a colour-coded band
   * and needs no accent of its own. Kept as a palette slot because the header
   * sprite is 4-bit and has fifteen spare entries anyway. */
#define TFT_COL_NOTICE  0xFFE0u        /* yellow              */
  #define TFT_COL_SINEL   0x3D7Fu        /* blue  (left wave)   */
  #define TFT_COL_SINER   0xFD80u        /* amber (right wave)  */

  /* ── Freq-band sprite (4-bit double buffer) ──────────────────────────────
   * The frequency readout and the busy message (SVIN/WARMUP/CAL) share the
   * band between the header separator (y=TFT_SY(22)) and the freq separator
   * (y=TFT_SY(58)). The old code drew directly to the panel: setTextPadding
   * erased ~16 k px (480×34) of background BEFORE each redraw, and that
   * erase-then-draw on the live panel flickered visibly once per second.
   *
   * The 4-bit sprite erases + draws the whole band in RAM (invisible) then
   * pushes it to the panel in ONE continuous transfer. The separator and the
   * side rails are never touched, so the frame stays intact. Memory cost:
   *   480×36 × 0.5 B = 8.6 KB   (ILI9488)
   *   320×36 × 0.5 B = 5.8 KB   (ILI9341/ST7789)
   * Both sit well inside the 128 KB RAM of the F411.
   *
   * Palette (16 entries; only the ones we use are meaningful, the rest are
   * black). Indices map 1:1 to the RGB565 colours the band can show. */
  #define FREQ_SPR_Y       TFT_SY(22)                          /* top = header sep   */
  #define FREQ_SPR_H       (TFT_SY(58) - TFT_SY(22))           /* band height (px)   */

#if defined(GPSDO_TFT_ILI9488)
  /* Right-edge anchor for the frequency reading (MR_DATUM) — 480×320 only.
   * The value places the nominal reading dead centre: "10000000.0000 Hz" is
   * 16 chars, FreeMonoBold24 is fixed-width at 28 px, so 448 px wide, and
   * (480 + 448) / 2 = 464 — i.e. 16 px in from the panel edge, mirroring the
   * 16 px of air the nominal string leaves on the left. Verified on-panel;
   * the small panel uses the padded-field approach instead (see the format
   * block in the display task). */
  #define FREQ_ANCHOR_X    464
#endif
  /* palette indices */
  #define PAL_BG       0    /* black   (band background)                  */
  #define PAL_WHITE    1    /* freq normal colour                         */
  #define PAL_LOCK     2    /* green   (LOCK)                             */
  #define PAL_HOLD     3    /* orange  (holdover / busy)                  */
  #define PAL_ALERT    4    /* red     (no signal / alert)                */
  /* Header sprite has its OWN palette (navy background, not black) — these
   * indices are not interchangeable with the PAL_* above. */
  #define HPAL_BG      0    /* navy    (header band background)           */
  #define HPAL_WHITE   1    /* white   (name, version, clock)             */
  #define HPAL_NOTICE  2    /* yellow  (spare; see TFT_COL_NOTICE)        */
  static TFT_eSprite s_freq_sprite(&s_tft);
  static bool        s_freq_sprite_ok = false;   /* false → direct-draw fallback */

  /* ── Header + data sprites (anti-flicker) ────────────────────────────────
   * The header band (navy + white text) and the data area (grid rows + sensor
   * rows, white on black) also flickered: setTextPadding erased the whole
   * field width on the live panel before each redraw. The header LMT clock
   * is redrawn unconditionally every wake, and UTC/Uptime/Algo change every
   * second — so the flicker was constant.
   *
   * Two more sprites mirror these regions in RAM:
   *   s_hdr_sprite  4-bit  (navy bg needs a 2nd palette colour)
   *   s_data_sprite 1-bit  (all data values are white-on-black → 1 bit is
   *                         enough; halves the RAM cost vs 4-bit)
   * 1-bit sprites treat colour 0 as transparent on pushSprite, so the data
   * sprite only rewrites the glyph pixels, leaving separators untouched.
   *
   * Memory (480×320):
   *   header   480×29 × 0.5 B  =  6.8 KB
   *   data     480×192 / 8     = 11.2 KB   (1-bit)
   *   freq     480×48 × 0.5 B  = 11.2 KB   (already counted)
   *   total all sprites        ≈ 29.3 KB
   * Remaining for heap/bss: ~87 KB — comfortable. */
  #define DATA_SPR_Y   (TFT_SY(64) + TFT_YOFF)     /* top = first grid row */
  #define DATA_SPR_H   (TFT_SY(210) - DATA_SPR_Y)  /* to status separator  */
  static TFT_eSprite s_hdr_sprite(&s_tft);    /* header band */
  static TFT_eSprite s_data_sprite(&s_tft);   /* grid + sensor rows */
  static bool        s_hdr_sprite_ok  = false;
  static bool        s_data_sprite_ok = false;

  /* Grid geometry — authored for 320x240, scaled so the same layout fills a
   * 480x320 ILI9488. X/width via TFT_S (3/2), Y/height via TFT_SY (4/3),
   * since the panel aspect differs. Identity on the small panels.
   *
   * TFT_YOFF shifts every TEXT baseline 3 px DOWN, leaving the separator
   * frame where it is. GFXFF glyphs have no descenders in the all-caps/numeric
   * strings used here, so they sit optically high inside their band — a 3 px
   * nudge rebalances them visually. Separators/side rails are drawn at their
   * authored Y (they are the fixed frame, not content).
   *
   * The Y coordinates below are the TEXT BASELINE for each grid row (GFXFF
   * draws from the baseline; with TL_DATUM drawString offsets to the baseline
   * for us, so these are the top-of-cell reference points passed to
   * drawString). Rows are spaced by TFT_ROW_H, sized for GF_DATA:
   *   320×240  FreeSans 9pt   → ~15 px glyph, row 18 px
   *   480×320  FreeSans 12pt  → ~17 px glyph, row 24 px (TFT_SY(18)=24) */
  /* Baseline nudge for the GFX fonts on the big panel — they sit optically
   * high in their rows without it. The 320×240 build uses the classic fonts
   * the layout was authored against, which need no nudge. */
#if defined(GPSDO_TFT_ILI9488)
  #define TFT_YOFF        3             /* text baseline nudge down (px, authored) */
#else
  #define TFT_YOFF        0
#endif
  #define TFT_ROW_H       TFT_SY(20)     /* GF_DATA row pitch — wider spacing (authored 320x240) */
  #define TFT_GRID_Y      (TFT_SY(64) + TFT_YOFF)   /* first grid row top   */
  #define TFT_COL_L       TFT_S(8)       /* left column x         */
  #define TFT_COL_R       TFT_S(168)     /* right column label x  */
  #define TFT_SENS_Y      (TFT_SY(172) + TFT_YOFF)  /* sensor row 1 top      */
  #define TFT_STATUS_Y    (TFT_SY(212) + TFT_YOFF)  /* status bar top (half-height bar) */
  /* right-edge anchor for right-column values (pinned to screen edge) */
  #define TFT_COL_RVAL    (TFT_W - TFT_S(8))
  /* left-column value anchor: right edge of the left half */
  #define TFT_COL_LVAL    (TFT_S(158))

  /* Previous-value cache for selective redraw (16 + 1 extra slot for the
   * split AHT humidity on the 480×320 panel). */
  static char tft_prev[22][28];

  /* Dirty flag: set when any tft_val/tft_val_r writes to the data sprite.
   * The update loop pushes the sprite once at the end of the cycle, so all
   * cell changes in one frame go out in a single transfer. */
  static bool s_data_dirty = false;

  /* Draw a value string only when it changed.
   * slot: cache index, x/y: position, pad: text padding px, col: colour.
   * x/y/pad are passed already-scaled by callers (via TFT_S/grid constants);
   * text is drawn in the GF_DATA free font. Left-datum (TL): x is the left
   * edge of the field, padding fills rightward to erase the previous glyphs.
   *
   * Sprite path: draws to s_data_sprite (Y translated to sprite-local) and
   * sets s_data_dirty. Fallback: direct draw to the panel (flickers, but
   * works if heap was too low to create the sprite). */
  static void tft_val(uint8_t slot, int32_t x, int32_t y,
                      uint16_t pad, uint16_t col, const char *s)
  {
      if (strncmp(tft_prev[slot], s, sizeof(tft_prev[0]) - 1) == 0)
          return;
      strncpy(tft_prev[slot], s, sizeof(tft_prev[0]) - 1);
      tft_prev[slot][sizeof(tft_prev[0]) - 1] = '\0';
      if (s_data_sprite_ok) {
          TFT_FONT_DATA(s_data_sprite);
          s_data_sprite.setTextColor(TFT_WHITE, TFT_BLACK);
          s_data_sprite.setTextPadding(pad);
          s_data_sprite.setTextDatum(TL_DATUM);
          s_data_sprite.drawString(s, x, y - DATA_SPR_Y);
          s_data_dirty = true;
      } else {
          TFT_FONT_DATA(s_tft);
          s_tft.setTextColor(col, TFT_COL_BG);
          s_tft.setTextPadding(pad);
          s_tft.setTextDatum(TL_DATUM);
          s_tft.drawString(s, x, y);
      }
  }

  /* Right-anchored value: the string's RIGHT edge sits at x_right (TR_DATUM),
   * so a field whose width changes (e.g. Vdd 1 vs 2 decimals, or a qErr of
   * varying width to its left) stays pinned to the screen edge instead of
   * floating. Padding fills the field width leftward from the anchor. */
  /* Width of a string in the font tft_val() draws with. Used to place the
   * columns' right-hand alignment lines: every value in those rows is
   * fixed-width (dtostrf with explicit widths), so each row's string has a
   * constant width and therefore a constant right edge — but that edge follows
   * from the font's glyph metrics, which are not worth guessing at. This asks
   * the library instead. */
  static int16_t tft_text_w(const char *s)
  {
      if (s_data_sprite_ok) {
          TFT_FONT_DATA(s_data_sprite);
          return (int16_t)s_data_sprite.textWidth(s);
      }
      TFT_FONT_DATA(s_tft);
      return (int16_t)s_tft.textWidth(s);
  }

  static void tft_val_r(uint8_t slot, int32_t x_right, int32_t y,
                        uint16_t pad, uint16_t col, const char *s)
  {
      if (strncmp(tft_prev[slot], s, sizeof(tft_prev[0]) - 1) == 0)
          return;
      strncpy(tft_prev[slot], s, sizeof(tft_prev[0]) - 1);
      tft_prev[slot][sizeof(tft_prev[0]) - 1] = '\0';
      if (s_data_sprite_ok) {
          TFT_FONT_DATA(s_data_sprite);
          s_data_sprite.setTextColor(TFT_WHITE, TFT_BLACK);
          s_data_sprite.setTextPadding(pad);
          s_data_sprite.setTextDatum(TR_DATUM);
          s_data_sprite.drawString(s, x_right, y - DATA_SPR_Y);
          s_data_dirty = true;
      } else {
          TFT_FONT_DATA(s_tft);
          s_tft.setTextColor(col, TFT_COL_BG);
          s_tft.setTextPadding(pad);
          s_tft.setTextDatum(TR_DATUM);
          s_tft.drawString(s, x_right, y);
      }
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

      /* Create the freq-band 4-bit sprite (double buffer). If heap is too low
       * createSprite returns nullptr and we silently fall back to the old
       * direct-draw path (s_freq_sprite_ok stays false). The palette mirrors
       * the RGB565 band colours so the sprite shows the same hues as direct. */
      s_freq_sprite.setColorDepth(4);
      if (s_freq_sprite.createSprite(TFT_W, FREQ_SPR_H) != nullptr) {
          /* 16-entry palette mirroring the RGB565 band colours. Index order
           * matches the PAL_* macros. Unused slots stay 0 (= black = bg). */
          static const uint16_t freq_pal[16] = {
              TFT_COL_BG,    /* 0 PAL_BG    black   */
              TFT_COL_FREQ,  /* 1 PAL_WHITE white   */
              TFT_COL_LOCK,  /* 2 PAL_LOCK  green   */
              TFT_COL_HOLD,  /* 3 PAL_HOLD  orange  */
              TFT_COL_ALERT, /* 4 PAL_ALERT red     */
              0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
          };
          s_freq_sprite.createPalette(freq_pal);   /* FLASH const overload */
          s_freq_sprite_ok = true;
      }
      if (s_freq_sprite_ok)
          OUT_SERIAL.println("TFT: freq-band sprite (4-bit) created");
      else
          OUT_SERIAL.println("TFT: freq-band sprite FAILED — direct-draw fallback");

      /* Header-band 4-bit sprite: navy background (PAL_HEADER_BG) + white
       * text. Two palette entries are enough; the rest are black. */
      s_hdr_sprite.setColorDepth(4);
      if (s_hdr_sprite.createSprite(TFT_W, TFT_SY(22)) != nullptr) {
          static const uint16_t hdr_pal[16] = {
              TFT_COL_HEADER,  /* 0  navy (band background) */
              TFT_WHITE,       /* 1  white (header text)    */
              TFT_COL_NOTICE,  /* 2  yellow (spare)         */
              0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
          };
          s_hdr_sprite.createPalette(hdr_pal);
          s_hdr_sprite_ok = true;
      }
      if (s_hdr_sprite_ok)
          OUT_SERIAL.println("TFT: header sprite (4-bit) created");
      else
          OUT_SERIAL.println("TFT: header sprite FAILED — direct-draw fallback");

      /* Data-area 1-bit sprite: white-on-black only (all data values share
       * one colour). 1-bit pushSprite paints bit=1 as bitmap_fg and bit=0 as
       * bitmap_bg; set fg=white, bg=black so cleared pixels paint black-on-
       * black (invisible over the data area). Frame segments crossing the
       * sprite region are redrawn after each push. */
      s_tft.setBitmapColor(TFT_WHITE, TFT_COL_BG);
      s_data_sprite.setColorDepth(1);
      if (s_data_sprite.createSprite(TFT_W, DATA_SPR_H) != nullptr) {
          s_data_sprite_ok = true;
      }
      if (s_data_sprite_ok)
          OUT_SERIAL.println("TFT: data sprite (1-bit) created");
      else
          OUT_SERIAL.println("TFT: data sprite FAILED — direct-draw fallback");

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

      /* Header band — filled navy, product name/version left, LMT right. The
       * band height (0..22 authored) is sized for GF_HEAD (FreeSans 9/12pt). */
      s_tft.fillRect(0, 0, TFT_W, TFT_SY(22), TFT_COL_HEADER);
      TFT_FONT_HEAD(s_tft);
      s_tft.setTextColor(TFT_WHITE, TFT_COL_HEADER);
      s_tft.setTextPadding(0);
      s_tft.setTextDatum(TL_DATUM);
      {
          char hdr[24];
          snprintf(hdr, sizeof(hdr), "%s %s", PROGRAM_NAME, PROGRAM_VERSION);
          s_tft.drawString(hdr, TFT_S(6), TFT_SY(3) + TFT_YOFF);
      }

      /* Frame separators. Authored (320×240) band boundaries, TFT_SY-scaled to
       * 480×320. Verified so GF_DATA rows never cross a separator on either
       * panel:
       *   header  0    .. 22
       *   freq    22   .. 58      (big FreeMonoBold, centred)
       *   grid    58   .. 168     (5 data rows, GF_DATA, wider pitch)
       *   sensors 168  .. 210     (2 sensor rows)
       *   status  212  .. bottom  (GF_STATUS bar, half height) */
      s_tft.drawFastHLine(0, TFT_SY(22),  TFT_W, TFT_COL_FRAME);   /* under header */
      s_tft.drawFastHLine(0, TFT_SY(58),  TFT_W, TFT_COL_FRAME);   /* under freq   */
      s_tft.drawFastHLine(0, TFT_SY(168), TFT_W, TFT_COL_FRAME);   /* under grid   */
      s_tft.drawFastHLine(0, TFT_SY(210), TFT_W, TFT_COL_FRAME);   /* over status  */
      /* Side rails close the data frame from the header edge to the status
       * separator, so the top corners wrap around the frequency. 1px white. */
      s_tft.drawFastVLine(0,         TFT_SY(22), TFT_SY(210) - TFT_SY(22), TFT_COL_FRAME);
      s_tft.drawFastVLine(TFT_W - 1, TFT_SY(22), TFT_SY(210) - TFT_SY(22), TFT_COL_FRAME);
      /* Vertical divider between the two data columns (grid + sensors), from
       * the freq separator down to the status separator. Only on the 480×320
       * panel: at 320×240 the classic-font columns run right up to the middle
       * and the divider cut through the text, so the small panel goes without.
       * On the big panel it's also drawn on the data sprite before each push. */
#if defined(GPSDO_TFT_ILI9488)
      s_tft.drawFastVLine(TFT_W/2, TFT_SY(58), TFT_SY(210) - TFT_SY(58), TFT_COL_FRAME);
#endif
  }

  /* Animated boot splash: credits first, then two phase-shifted sine waves
   * (blue above, amber below) whose phase slowly converges until they
   * coincide and merge into a single thick green 10 MHz wave — a visual
   * metaphor for GPS and OCXO pulling into phase lock. A hardware
   * checklist follows. Purely cosmetic; runs once before the layout. */
  /* Blend an RGB565 colour toward the background by factor a (0=background,
   * 1=full colour) — used for the splash wave fade-in. Channels are unpacked,
   * scaled and repacked. */
  static uint16_t tft_fade565(uint16_t c, float a)
  {
      if (a <= 0.0f) return TFT_COL_BG;
      if (a >= 1.0f) return c;
      uint16_t bg = TFT_COL_BG;
      int r  = (c  >> 11) & 0x1F, g  = (c  >> 5) & 0x3F, b  = c  & 0x1F;
      int rb = (bg >> 11) & 0x1F, gb = (bg >> 5) & 0x3F, bb = bg & 0x1F;
      r = rb + (int)((r - rb) * a);
      g = gb + (int)((g - gb) * a);
      b = bb + (int)((b - bb) * a);
      return (uint16_t)((r << 11) | (g << 5) | b);
  }

  static void tft_splash(bool oled_ok, bool lcd_ok, bool ht_ok,
                         bool aht_ok, bool bmp_ok, bool ina_ok)
  {
      s_tft.fillScreen(TFT_COL_BG);

      /* --- title/credits drawn FIRST; they persist through the animation.
       * Title sits at the frequency field's height so the splash and the live
       * screen share a visual anchor. --- */
      s_tft.setTextDatum(MC_DATUM);
      s_tft.setTextPadding(0);
      /* Subtitle only — the big green "GPSDO" title was dropped in v0.93; the
       * splash leads with "GPS Disciplined OCXO" raised to the top.
       *
       * Font 4, not GFXFF. The splash was the last GFX holdout, which meant a
       * user upgrading from v0.92 still had to add LOAD_GFXFF to User_Setup.h
       * or watch this line collapse to a lone "p" — an obscure failure for a
       * cosmetic gain. Font 4 carries the full alphabet, so the splash now
       * needs nothing beyond the fonts the operating screen already loads.
       * (Fonts 6/8 are the ones that hold only 0-9 . : - a p m; 1/2/4 are
       * complete.) */
      TFT_FONT_STATUS(s_tft);                   /* font 4 / GF_STATUS */
      s_tft.setTextColor(0x9CD3, TFT_COL_BG);   /* soft blue-grey */
      s_tft.drawString("GPS Disciplined OCXO", TFT_W/2, TFT_SY(28));
      /* Credits as fine print: classic font 1 (6×8) on both panels. The long
       * "inspired by…" line is ~270 px in font 1 — it fits 320 px, and on the
       * 480 panel it simply reads as fine print, which is what it is. */
      s_tft.setTextFont(1);
      s_tft.setTextSize(1);
      s_tft.setTextColor(0x8410, TFT_COL_BG);
      s_tft.drawString("jmnlabs with Claude (Anthropic)", TFT_W/2, TFT_SY(206));
      s_tft.drawString("inspired by STM32-GPSDO v0.06c by Andre Balsa",
                       TFT_W/2, TFT_SY(218));

      /* The animated boot sequence below — two oscillators fading in out of
       * phase, drifting into agreement, and merging into one green wave with a
       * halo of victory — is a small labour of love. Some souls, however, are
       * immune to art and just want to see "10 MHz" already. `SPL 0` is for
       * them: title and credits, a dignified two-second pause, done. Their
       * loss. (SPL 1 restores civilisation; saved by ES.) */
      if (!g_splash_enable) {
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }

      /* --- two phase-shifted waves converging to synchronism ---
       * Animation requires redrawing each frame, so each wave is erased
       * (drawn in background colour) before the next frame is drawn. The
       * wave band is raised (now that the header bar is gone) to sit below
       * the title, clear of the credits. All geometry scales with TFT_S().
       * The waves also FADE IN: their colour is blended from the background
       * toward full colour over the first ~3 s, a soft tonal reveal. */
      const int   yc      = TFT_SY(108); /* wave centre line              */
      const float SPL_AMP  = TFT_S(15); /* amplitude px                   */
      const float SPL_WCYC = 5.0f;      /* cycles across the screen       */
      const float SPL_PH0  = 2.5f;      /* initial phase offset [rad]     */
      const int   SPL_GAP  = TFT_S(12); /* upper wave initial offset up   */
      const int   SPL_GAPB = TFT_S(12) + TFT_S(30); /* lower wave offset down (extra 30px) */
      /* Convergence meets at the proportional midpoint of the two starts:
       * upper starts at yc-SPL_GAP, lower at yc+SPL_GAPB, so they merge at
       * yc + (SPL_GAPB - SPL_GAP)/2 rather than at yc. */
      const int   SPL_MEET = (SPL_GAPB - SPL_GAP) / 2;
      const int   STEP     = TFT_S(4);  /* x sampling step                */

      #define WAVE_Y(xx,yoff,ph) \
          (yc + (yoff) + (int)(SPL_AMP * sinf((float)(xx)/(float)TFT_W*6.2831853f*SPL_WCYC + (ph))))

      /* --- FADE-IN phase: the two waves hold at their initial (out-of-phase)
       * positions and brighten from the background to full colour over ~1.5 s.
       * This is a dedicated, motionless stage so the tonal reveal is actually
       * visible — during the moving convergence the eye tracks motion and a
       * slow brightness ramp is lost. No band erase between steps here: we
       * only ever draw brighter over the same pixels, so it truly "fades up".*/
      {
          const int FADE_STEPS = 24;                 /* ×45ms ≈ 1.1 s */
          int g0u = SPL_GAP, g0b = SPL_GAPB;
          for (int fs = 1; fs <= FADE_STEPS; fs++) {
              float a = (float)fs / (float)FADE_STEPS;   /* 0..1 brightness */
              uint16_t colL = tft_fade565(TFT_COL_SINEL, a);
              uint16_t colR = tft_fade565(TFT_COL_SINER, a);
              int ptx = 0, pty = WAVE_Y(0, -g0u, SPL_PH0);
              int pbx = 0, pby = WAVE_Y(0, +g0b, 0.0f);
              for (int x = STEP; x <= TFT_W; x += STEP) {
                  int ty = WAVE_Y(x, -g0u, SPL_PH0);
                  int by = WAVE_Y(x, +g0b, 0.0f);
                  for (int th = 0; th <= 1; th++) {   /* 2px */
                      s_tft.drawLine(ptx, pty+th, x, ty+th, colL);
                      s_tft.drawLine(pbx, pby+th, x, by+th, colR);
                  }
                  ptx = x; pty = ty; pbx = x; pby = by;
              }
              vTaskDelay(pdMS_TO_TICKS(45));
          }
      }

      const int FRAMES = 64;       /* convergence frames             */
      for (int fr = 0; fr <= FRAMES; fr++) {
          float k    = (float)fr / (float)FRAMES;     /* 0..1 */
          float ease = k * k * (3.0f - 2.0f * k);     /* smoothstep */
          float ph   = SPL_PH0 * (1.0f - ease);           /* phase → 0 */
          /* each wave interpolates from its own start toward the shared meet
           * point SPL_MEET: upper from -SPL_GAP, lower from +SPL_GAPB */
          int gu = (int)(-SPL_GAP  + (SPL_MEET - (-SPL_GAP)) * ease);
          int gb = (int)( SPL_GAPB + (SPL_MEET -  SPL_GAPB ) * ease);

          int ptx = 0, pty = WAVE_Y(0, gu, ph);
          int pbx = 0, pby = WAVE_Y(0, gb, 0.0f);

          /* erase previous frame band (covers the full initial spread) */
          s_tft.fillRect(0, yc - (int)SPL_AMP - SPL_GAP - 4, TFT_W,
                         (int)SPL_AMP*2 + SPL_GAP + SPL_GAPB + 8, TFT_COL_BG);

          /* full brightness — the fade-in already happened above */
          uint16_t colL = TFT_COL_SINEL;
          uint16_t colR = TFT_COL_SINER;

          for (int x = STEP; x <= TFT_W; x += STEP) {
              int ty = WAVE_Y(x, gu, ph);
              int by = WAVE_Y(x, gb, 0.0f);
              /* 2px thickness */
              for (int th = 0; th <= 1; th++) {
                  s_tft.drawLine(ptx, pty+th, x, ty+th, colL);
                  s_tft.drawLine(pbx, pby+th, x, by+th, colR);
              }
              ptx = x; pty = ty; pbx = x; pby = by;
          }
          vTaskDelay(pdMS_TO_TICKS(45));    /* ~2.9 s convergence */
      }

      /* --- merge: erase band, draw one 4px green wave --- */
      s_tft.fillRect(0, yc - (int)SPL_AMP - SPL_GAP - 4, TFT_W,
                     (int)SPL_AMP*2 + SPL_GAP + SPL_GAPB + 8, TFT_COL_BG);
      {
          int px0 = 0, py0 = WAVE_Y(0, SPL_MEET, 0.0f);
          for (int x = STEP; x <= TFT_W; x += STEP) {
              int y = WAVE_Y(x, SPL_MEET, 0.0f);
              for (int t = -1; t <= 2; t++)   /* 4px core */
                  s_tft.drawLine(px0, py0+t, x, y+t, TFT_COL_LOCK);
              px0 = x; py0 = y;
          }
      }

      /* --- HALO: a green glow that first SWELLS out of the locked wave, then
       * dissipates — a clear "settling into lock" flourish. Brightness is a
       * triangular envelope over the whole pulse (rise then fall) multiplied
       * by a radial falloff (dimmer further from the 4px core). 12px reach,
       * many steps and a long duration so the bloom is unmistakable. The
       * core is redrawn each step so it stays crisp on top of the glow. */
      {
          const int   HALO_STEPS = 60;                 /* ×55ms ≈ 3.3 s */
          const int   HALO_MAX   = TFT_S(12);          /* glow reach px */
          for (int hs = 0; hs < HALO_STEPS; hs++) {
              float u = (float)hs / (float)(HALO_STEPS - 1);   /* 0..1 */
              /* triangular envelope: 0 → 1 at the middle → 0 */
              float env = (u < 0.5f) ? (u * 2.0f) : (2.0f - u * 2.0f);
              env = env * env * (3.0f - 2.0f * env);           /* smoothstep */

              s_tft.fillRect(0, yc + SPL_MEET - (int)SPL_AMP - HALO_MAX - 1, TFT_W,
                             2*((int)SPL_AMP + HALO_MAX + 1), TFT_COL_BG);
              int px0 = 0, py0 = WAVE_Y(0, SPL_MEET, 0.0f);
              for (int x = STEP; x <= TFT_W; x += STEP) {
                  int y = WAVE_Y(x, SPL_MEET, 0.0f);
                  /* glow rings, dim → outer */
                  for (int off = HALO_MAX; off >= 3; off--) {
                      float radial = 1.0f - (float)(off - 2) / (float)(HALO_MAX - 1);
                      float a = env * radial * 0.85f;
                      if (a < 0.02f) continue;
                      uint16_t gc = tft_fade565(TFT_COL_LOCK, a);
                      s_tft.drawLine(px0, py0-off, x, y-off, gc);
                      s_tft.drawLine(px0, py0+off, x, y+off, gc);
                  }
                  for (int t = -1; t <= 2; t++)   /* crisp 4px core on top */
                      s_tft.drawLine(px0, py0+t, x, y+t, TFT_COL_LOCK);
                  px0 = x; py0 = y;
              }
              vTaskDelay(pdMS_TO_TICKS(55));
          }
      }
      #undef WAVE_Y

      /* hold the synchronised wave so the eye can rest */
      vTaskDelay(pdMS_TO_TICKS(1800));

      /* clear the wave band to make room for the checklist */
      s_tft.fillRect(0, yc - (int)SPL_AMP - SPL_GAP - 4, TFT_W,
                     (int)SPL_AMP*2 + SPL_GAP + SPL_GAPB + 8, TFT_COL_BG);

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
          /* LTIC is read-only (we sample its ADC, can't probe presence), so
           * show [x] when compiled in — like the TM1637/TFT. */
#ifdef GPSDO_LTIC
          { "LTIC  phase (PA1)",      true,   true   },
#endif
      };

      s_tft.setTextDatum(TL_DATUM);
      /* brief pause so the eye moves from the wave to the checklist before
       * the first item appears (otherwise the first rows are missed) */
      vTaskDelay(pdMS_TO_TICKS(600));

      /* Scrolling window (DAPU-style): rows reveal sequentially inside a
       * FIXED-HEIGHT window that ends above the credits, so the credits never
       * move and the list can be any length. Once the window is full, the
       * content scrolls up one line — the oldest row drops off the top and the
       * new row appears at the bottom, like a terminal. */
      const int LIST_TOP = TFT_SY(80);
      const int ROW_H    = TFT_SY(18);            /* GF_DATA row pitch (taller than the old font2) */
      const int WIN_ROWS = 6;                     /* visible rows; window ends ~192, credits at 214 */
      const int WIN_H    = WIN_ROWS * ROW_H;

      TFT_FONT_DATA(s_tft);                 /* checklist uses the data font */
      s_tft.setTextDatum(TL_DATUM);
      s_tft.setTextPadding(0);

      /* collect the rows that are actually shown */
      int shown_idx[16]; int n_shown = 0;
      for (unsigned i = 0; i < sizeof(rows)/sizeof(rows[0]) && n_shown < 16; i++)
          if (rows[i].show) shown_idx[n_shown++] = (int)i;

      int first_visible = 0;                       /* index into shown_idx of top window row */
      for (int r = 0; r < n_shown; r++) {
          /* if this row would fall below the window, scroll up by one */
          if (r - first_visible >= WIN_ROWS) {
              first_visible = r - WIN_ROWS + 1;
              s_tft.fillRect(0, LIST_TOP, TFT_W, WIN_H, TFT_COL_BG);   /* clear window */
              /* redraw the rows now inside the window */
              for (int k = first_visible; k <= r; k++) {
                  int idx = shown_idx[k];
                  int yy  = LIST_TOP + (k - first_visible) * ROW_H;
                  uint16_t col = rows[idx].ok ? TFT_COL_LOCK : TFT_COL_HOLD;
                  s_tft.setTextColor(col, TFT_COL_BG);
                  s_tft.drawString(rows[idx].ok ? "[x]" : "[ ]", TFT_S(36), yy);
                  s_tft.setTextColor(TFT_COL_VALUE, TFT_COL_BG);
                  s_tft.drawString(rows[idx].label, TFT_S(72), yy);
              }
          } else {
              /* still filling the window: just draw the new row at its slot */
              int idx = shown_idx[r];
              int yy  = LIST_TOP + (r - first_visible) * ROW_H;
              uint16_t col = rows[idx].ok ? TFT_COL_LOCK : TFT_COL_HOLD;
              s_tft.setTextColor(col, TFT_COL_BG);
              s_tft.drawString(rows[idx].ok ? "[x]" : "[ ]", TFT_S(36), yy);
              s_tft.setTextColor(TFT_COL_VALUE, TFT_COL_BG);
              s_tft.drawString(rows[idx].label, TFT_S(72), yy);
          }
          vTaskDelay(pdMS_TO_TICKS(650));   /* sequential reveal / scroll cadence */
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
      if (s_hdr_sprite_ok) {
          /* ── sprite path: erase band in RAM, redraw both fields, one push ──
           * The whole header band is rebuilt each wake (product name is static
           * but cheap to redraw) so the LMT erase happens invisibly in RAM. */
          s_hdr_sprite.fillSprite(HPAL_BG);
          TFT_FONT_HEAD(s_hdr_sprite);
          s_hdr_sprite.setTextColor(HPAL_WHITE, HPAL_BG);
          /* product name left */
          {
              char hdr[24];
              snprintf(hdr, sizeof(hdr), "%s %s", PROGRAM_NAME, PROGRAM_VERSION);
              s_hdr_sprite.setTextDatum(TL_DATUM);
              s_hdr_sprite.setTextPadding(0);
              s_hdr_sprite.drawString(hdr, TFT_S(6), TFT_SY(3) + TFT_YOFF);
          }
          /* LMT right-anchored. Drawn BEFORE the notice on purpose: with a
           * padding of TFT_S(130) TFT_eSPI erases a band running from
           * (anchor - padding) rightwards to the start of the text — 276..361
           * on the 480 panel — and the notice sits immediately to its left.
           * The two are ~1 px apart by calculation, which is far too thin a
           * margin to rely on: the glyph widths are estimates, and if they are
           * off by a few percent the padding eats the notice's last letter.
           * Drawing the notice last removes the question rather than answering
           * it. It carries no padding of its own, so it cannot return the
           * favour and erase the clock. */
          s_hdr_sprite.setTextDatum(TR_DATUM);
          s_hdr_sprite.setTextPadding(TFT_S(130));
          s_hdr_sprite.drawString(s, TFT_W - TFT_S(6), TFT_SY(3) + TFT_YOFF);

          s_hdr_sprite.pushSprite(0, 0);
      } else {
          /* ── direct-draw fallback ── */
          TFT_FONT_HEAD(s_tft);
          s_tft.setTextColor(TFT_WHITE, TFT_COL_HEADER);
          s_tft.setTextDatum(TR_DATUM);
          s_tft.setTextPadding(TFT_S(130));
          s_tft.drawString(s, TFT_W - TFT_S(6), TFT_SY(3) + TFT_YOFF);
      }
      s_tft.setTextDatum(TL_DATUM);

      /* ---- frequency, font 4 size 2, centred, colour-coded ----
       * Lock detection is algorithm-independent: based on the actual
       * deviation of the best available average from 10 MHz, since most
       * algorithms never emit the "hit" trend string (algos 3-5 only on
       * an exact 0.0 PID output, 6-9 never).  Thresholds:
       *   10000-s window: |e| ≤ 1.0 mHz  (1e-10)
       *    1000-s window: |e| ≤ 10  mHz  (1e-9)                          */
      {
          uint16_t fcol = TFT_COL_FREQ;   /* white by default; green on lock */
          bool locked = false;
          bool busy = (g_svin_active || g_warmup_active || g_calib_active);
          /* busy↔normal mode change: invalidate the cache so the next draw
           * fires. With the sprite the old band-wipe + separator touch-up are
           * gone — fillSprite(PAL_BG) erases the whole band in RAM and the
           * separators/side rails are never inside the sprite, so they stay. */
          static bool tft_prev_busy = false;
          if (tft_prev_busy != busy) {
              tft_prev_busy = busy;
              tft_prev[0][0] = '\0';
          }
          if (busy) {
              /* One phrasing for every long-running procedure: a spelled-out
               * name, then its progress figure. Short codes like "SVIN"/"CAL"
               * told the user little, and "CAL" was ambiguous — C, CT and LC
               * take very different times, so the countdown alone was
               * confusing.
               *
               * Note the two figures differ in kind: warmup and the
               * calibrations count DOWN (seconds left), while survey-in counts
               * UP — the receiver reports elapsed time, and completion also
               * depends on accuracy, so a "remaining" figure would be a guess.
               * The "+/-" marks the accuracy so the pair reads unambiguously.
               * The label is "Survey" rather than "Survey-in" so the worst
               * case (a poor-signal fix: four-digit seconds AND three-digit
               * metres) still fits the 320-px panel in font 4. */
              if (g_svin_active)
                  snprintf(s, sizeof(s), "Survey %us +/-%um",
                           (unsigned)g_svin_dur, (unsigned)g_svin_acc_m);
              else if (g_warmup_active)
                  snprintf(s, sizeof(s), "OCXO warmup %us",
                           (unsigned)g_warmup_remaining);
              else {
                  const char *what = (g_calib_kind == CALIB_CT) ? "Tune"
                                   : (g_calib_kind == CALIB_LC) ? "LTIC cal"
                                                                : "Calibrate";
                  snprintf(s, sizeof(s), "%s %us", what,
                           (unsigned)g_calib_remaining);
              }
              fcol = TFT_COL_HOLD;
          } else {
          /* Green must mean a TRUSTWORTHY, CURRENT lock — not the echo of a
           * long averaging window (a 1000-s average keeps showing ~10 MHz for
           * minutes after discipline is lost, e.g. when LTIC drops to ACQ).
           *  - algo 10 (LTIC): the loop has an authoritative live state, so
           *    green comes ONLY from trend "LOCK"; no average fallback.
           *  - algos 0-9: no live lock flag exists ("hit" is rare), so the
           *    long-window criterion stays, but it must be BACKED by the
           *    fast 10-s average still sitting near 10 MHz (±50 mHz) —
           *    losing discipline now kills the green in ~10 s, not minutes. */
          if (c->active_algo == 10) {
              locked = (strncmp(c->trendstr,"LOCK",4) == 0);
          } else {
              if      (f->full10000) { double e = f->avg10000 - 10000000.0;
                                       locked = (e > -0.001 && e < 0.001); }
              else if (f->full1000)  { double e = f->avg1000  - 10000000.0;
                                       locked = (e > -0.010 && e < 0.010); }
              if (locked && f->full10) {
                  double e10 = f->avg10 - 10000000.0;
                  if (e10 < -0.050 || e10 > 0.050) locked = false;  /* stale echo */
              }
              if (strncmp(c->trendstr,"hit",3) == 0) locked = true;
          }

          if      (c->holdover_mode) fcol = TFT_COL_HOLD;
          else if (locked)           fcol = TFT_COL_LOCK;

          /* Two panels, two ways of holding the reading still.
           *
           * 320×240 pads the value to a 14-char field, as the display has done
           * since v0.89: with a fixed-width font that makes the WHOLE string a
           * constant width regardless of how many decimals the averaging
           * window shows, so the centred text has nothing to shift. (Monospace
           * alone is not enough — it holds the digits steady relative to each
           * other, but a string that loses a character still gets re-centred,
           * moving every glyph half a character sideways.)
           *
           * 480×320 leaves the value unpadded and anchors the string by its
           * right edge instead (FREQ_ANCHOR_X) — same result, "Hz" nailed
           * down, and it is what's verified on that panel. */
#if defined(GPSDO_TFT_ILI9488)
          #define FW 1
#else
          #define FW 14
#endif
          if      (f->full10000) { static char ff[16]; dtostrf(f->avg10000,FW,4,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->full1000)  { static char ff[16]; dtostrf(f->avg1000, FW,3,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->full100)   { static char ff[16]; dtostrf(f->avg100,  FW,2,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->full10)    { static char ff[16]; dtostrf(f->avg10,   FW,1,ff); snprintf(s,sizeof(s),"%s Hz",ff); }
          else if (f->calcfreqint > 0) { snprintf(s,sizeof(s),"%*ld Hz",FW,(long)f->calcfreqint); }
          else { snprintf(s,sizeof(s),"no signal"); fcol = TFT_COL_ALERT; }
          #undef FW
          }

          /* colour participates in cache key */
          static uint16_t prev_fcol = 0;
          if (prev_fcol != fcol) { tft_prev[0][0] = '\0'; prev_fcol = fcol; }

          /* draw only when the displayed string or colour actually changed */
          if (strncmp(tft_prev[0], s, sizeof(tft_prev[0])-1) != 0) {
              strncpy(tft_prev[0], s, sizeof(tft_prev[0])-1);

              if (s_freq_sprite_ok) {
                  /* ── sprite path: erase+draw in RAM, one push to panel ── */
                  s_freq_sprite.fillSprite(PAL_BG);
                  /* Font per panel, exactly as the direct path: the sprites are
                   * created on BOTH panels, so hard-coding GF_* here would have
                   * kept the 320×240 build on the GFX faces regardless of the
                   * TFT_FONT_* macros. */
                  if (busy) TFT_FONT_STATUS(s_freq_sprite);
                  else      TFT_FONT_FREQ_ON(s_freq_sprite);
                  /* map RGB565 colour → palette index */
                  uint8_t pidx = (fcol == TFT_COL_LOCK)  ? PAL_LOCK
                               : (fcol == TFT_COL_HOLD)  ? PAL_HOLD
                               : (fcol == TFT_COL_ALERT) ? PAL_ALERT
                               : PAL_WHITE;
                  s_freq_sprite.setTextColor(pidx, PAL_BG);
                  /* text Y in sprite-local coords (screen Y − sprite top).
                   * On the 480×320 panel the big GF_FREQ glyph sits a touch
                   * low in the band; nudge it up 4 px for optical centring. */
#if defined(GPSDO_TFT_ILI9488)
                  int sy_local = (TFT_SY(40) + TFT_YOFF - 4) - FREQ_SPR_Y;
#else
                  int sy_local = (TFT_SY(40) + TFT_YOFF) - FREQ_SPR_Y;
#endif
#if defined(GPSDO_TFT_ILI9488)
                  /* Right-anchored (see FREQ_ANCHOR_X). Busy messages keep
                   * MC_DATUM: proportional font, no columns to line up. */
                  if (busy) {
                      s_freq_sprite.setTextDatum(MC_DATUM);
                      s_freq_sprite.drawString(s, TFT_W/2, sy_local);
                  } else {
                      s_freq_sprite.setTextDatum(MR_DATUM);
                      s_freq_sprite.drawString(s, FREQ_ANCHOR_X, sy_local);
                  }
#else
                  /* Centred — the padded field makes the string a constant
                   * width, so there is nothing for centring to shift. */
                  s_freq_sprite.setTextDatum(MC_DATUM);
                  s_freq_sprite.drawString(s, TFT_W/2, sy_local);
#endif
                  if (!busy) TFT_FONT_FREQ_OFF(s_freq_sprite);
                  /* FREQ_SPR_Y is TFT_SY(22) — the sprite's first row lands
                   * exactly on the header separator, so the push would wipe it.
                   * Draw it into the sprite instead (PAL_WHITE is in the
                   * palette): line and text then go out in the same transfer,
                   * with nothing to repaint afterwards. The side rails need the
                   * same treatment — fillSprite clears the whole band, so
                   * without them the frame simply vanished either side of the
                   * frequency and the rails appeared not to meet the header
                   * line. */
                  s_freq_sprite.drawFastHLine(0, 0, TFT_W, PAL_WHITE);
                  s_freq_sprite.drawFastVLine(0,         0, FREQ_SPR_H, PAL_WHITE);
                  s_freq_sprite.drawFastVLine(TFT_W - 1, 0, FREQ_SPR_H, PAL_WHITE);
                  s_freq_sprite.pushSprite(0, FREQ_SPR_Y);
              } else {
                  /* ── direct-draw fallback (no sprite / low heap) ── */
                  if (busy) TFT_FONT_STATUS(s_tft);
                  else      TFT_FONT_FREQ_ON(s_tft);
                  s_tft.setTextColor(fcol, TFT_COL_BG);
                  s_tft.setTextPadding(TFT_S(316));
#if defined(GPSDO_TFT_ILI9488)
                  if (busy) {
                      s_tft.setTextDatum(MC_DATUM);
                      s_tft.drawString(s, TFT_W/2, TFT_SY(40) + TFT_YOFF);
                  } else {
                      s_tft.setTextDatum(MR_DATUM);
                      s_tft.drawString(s, FREQ_ANCHOR_X, TFT_SY(40) + TFT_YOFF);
                  }
#else
                  s_tft.setTextDatum(MC_DATUM);
                  s_tft.drawString(s, TFT_W/2, TFT_SY(40) + TFT_YOFF);
#endif
                  if (!busy) TFT_FONT_FREQ_OFF(s_tft);
                  /* the wide padding fill spills past the freq separator and
                   * the side rails — redraw them after each update */
                  s_tft.drawFastHLine(0, TFT_SY(58), TFT_W, TFT_COL_FRAME);
                  s_tft.drawFastVLine(0,         TFT_SY(23), TFT_SY(58) - TFT_SY(23), TFT_COL_FRAME);
                  s_tft.drawFastVLine(TFT_W - 1, TFT_SY(23), TFT_SY(58) - TFT_SY(23), TFT_COL_FRAME);
              }
          }
          s_tft.setTextDatum(TL_DATUM);
      }

      /* ---- info grid, left column ---- */
      if (g->valid)
          snprintf(s,sizeof(s),"UTC: %02d:%02d:%02d %s", g->hours,g->mins,g->secs,
                   day_of_week_str(g->day,g->month,g->year));
      else snprintf(s,sizeof(s),"UTC: --:--:--");
      tft_val(1, TFT_COL_L, TFT_GRID_Y+0*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      if (g->valid) snprintf(s,sizeof(s),"DATE: %02d/%02d/%04d", g->day,g->month,g->year);
      else          snprintf(s,sizeof(s),"DATE: --/--/----");
      tft_val(2, TFT_COL_L, TFT_GRID_Y+1*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      snprintf(s,sizeof(s),"Uptime: %s %s", u->days_str, u->time_str);
      tft_val(3, TFT_COL_L, TFT_GRID_Y+2*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

      snprintf(s,sizeof(s),"Algo: %u %s", c->active_algo, c->trendstr);
      tft_val(4, TFT_COL_L, TFT_GRID_Y+3*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);

#if defined(GPSDO_TFT_ILI9488)
      /* One right-hand alignment line per column, measured once.
       *
       * The left column reads down to "hPa" on the BMP row and the right column
       * to "ns" on the phase row: those are the widest strings in their columns,
       * so the eye already takes their right edges as the columns' edges. The
       * remaining fields are pulled onto those lines instead of being left to
       * stop wherever their own text happens to run out.
       *
       * Measured, not estimated. Every value in these rows is fixed-width
       * (dtostrf with explicit widths), so each string has a constant width and
       * a constant right edge — but that edge follows from the font's glyph
       * metrics, which are not the sort of thing to guess at. The sample strings
       * use '0' for every digit: FreeSans digits are tabular, so a zero measures
       * exactly as wide as any other, so this is the real edge.
       * Computed on first use: these strings change their digits, never their
       * shape. */
      static int16_t align_L = 0, align_R = 0;
      if (align_L == 0) {
          align_L = TFT_COL_L + tft_text_w("BMP: 00.00 C 0000.00 hPa");
          align_R = TFT_COL_R + tft_text_w("Vph: 0.000 V dph: +0000ns");
      }
#endif

      { static char fv[8];
        double vctl = ((double)c->avg_vctl_adc / 4096.0) * 3.3;
        dtostrf(vctl, 5, 3, fv);
#if defined(GPSDO_TFT_ILI9488)
        /* Split, so "Vct: 1.970 V" can sit on the alignment line while "PWM:"
         * stays with the other labels at the column's left edge. As one string
         * only one of those two could be true.
         *
         * Both carry padding. An earlier version passed zero here, reasoning
         * that "%5u" and dtostrf(5,3) are fixed-width so nothing could shrink
         * and leave a tail, and that the font paints its own glyph backgrounds
         * anyway. On the panel the last PWM digit kept fragments of the digit
         * before it. Fixed character count is not fixed pixel width — and more
         * to the point, padding is how every other cell in this file clears
         * itself, which is a convention that has been working for far longer
         * than that reasoning had been thought about.
         *
         * Sized by measurement rather than assumption, like the rows below: the
         * value's pad is its own widest form, the label's is the rest of the
         * space up to it, so the two tile the row exactly and cannot overlap. */
        static int16_t vct_w = 0;
        if (vct_w == 0) vct_w = tft_text_w("Vct: 0.000 V");
        snprintf(s,sizeof(s),"PWM: %5u", c->pwm_output);
        tft_val(5, TFT_COL_L, TFT_GRID_Y+4*TFT_ROW_H,
                (uint16_t)(align_L - vct_w - TFT_COL_L), TFT_COL_VALUE, s);
        snprintf(s,sizeof(s),"Vct: %s V", fv);
        tft_val_r(19, align_L, TFT_GRID_Y+4*TFT_ROW_H,
                  (uint16_t)vct_w, TFT_COL_VALUE, s);
#else
        snprintf(s,sizeof(s),"PWM:%5u Vct:%sV", c->pwm_output, fv);
        tft_val(5, TFT_COL_L, TFT_GRID_Y+4*TFT_ROW_H, TFT_S(156), TFT_COL_VALUE, s);
#endif
      }

      /* ---- info grid, right column ---- */
      if (g->pos_valid) {
          if (g->time_mode) {
              snprintf(s,sizeof(s),"Sat: %2d HDOP: TIME", g->sats);
          } else {
              static char fhd[8]; dtostrf((double)g->hdop/100.0,4,2,fhd);
              snprintf(s,sizeof(s),"Sat: %2d HDOP: %s", g->sats, fhd);
          }
      } else snprintf(s,sizeof(s),"Sat: %2d no fix", g->sats);
      tft_val(6, TFT_COL_R, TFT_GRID_Y+0*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (g->pos_valid) { static char fl[12]; dtostrf(g->lat,10,6,fl);
          snprintf(s,sizeof(s),"Lat: %s",fl); }
      else snprintf(s,sizeof(s),"Lat: ---");
      tft_val(7, TFT_COL_R, TFT_GRID_Y+1*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (g->pos_valid) { static char fn[12]; dtostrf(g->lon,10,6,fn);
          snprintf(s,sizeof(s),"Lon: %s",fn); }
      else snprintf(s,sizeof(s),"Lon: ---");
      tft_val(8, TFT_COL_R, TFT_GRID_Y+2*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);

      if (g->pos_valid) snprintf(s,sizeof(s),"Alt:  %dm",(int)g->alt);
      else              snprintf(s,sizeof(s),"Alt: ---");
#if defined(GPSDO_TFT_ILI9488)
      snprintf(s,sizeof(s), g->pos_valid ? "Alt:  %d m" : "Alt: ---", (int)g->alt);
      /* Alt gives up the right half of its field to qErr. Grouping is the whole
       * point: qErr is the receiver's own report on its 1PPS, so it belongs
       * with the fix data, which then leaves the row below free to hold Vcc
       * beside Vdd — supplies together, GPS together. This row is where the
       * space was: ~134 px of slack after the altitude, so the split costs
       * nothing the grid was using.
       *
       * Label and value are two fields, not one string. Right-anchoring the
       * whole thing nailed "ns" down but then dragged "qErr:" along with the
       * digits — the label belongs to the slot, not to the value. So the label
       * sits at its slot's left edge and never moves, the value keeps a right
       * anchor so the unit stays put, and only the gap between them changes.
       * This is how the Vph/dph row has always behaved: fixed label,
       * fixed-width value.
       *
       * All three paddings tile the original Alt field exactly and must not
       * overlap. TFT_eSPI fills background rightward from a TL_DATUM anchor and
       * leftward from a TR_DATUM one, so if two fills met, every repaint would
       * erase the neighbour's tail:
       *     Alt     TFT_S(70) = 105 px   252..357   ("Alt:  175 m" ~88 px)
       *     qErr:   TFT_S(30) =  45 px   357..402   ("qErr:"       ~41 px)
       *     value   TFT_S(48) =  72 px   402..474   ("+12.3ns"     ~63 px)
       * 105 + 45 + 72 = 222 = TFT_S(148), the width the field had as one cell.
       * The label carries padding only so that it erases itself when SAW goes
       * off and its string becomes empty.
       *
       * The 320 panel keeps the field undivided and leaves qErr in the sensor
       * row below: at 8 px per character the two strings want ~168 px there and
       * the cell is 148. */
      tft_val(9, TFT_COL_R, TFT_GRID_Y+3*TFT_ROW_H, TFT_S(70), TFT_COL_VALUE, s);
  #ifdef GPSDO_LTIC
      /* Guarded like the row it came from: qErr only ever shows under algo 10,
       * which is the LTIC algorithm, so without that hardware these two fields
       * would repaint an empty slot once a second forever. */
      {
          bool saw = g_qerr_enable;   /* what dph has had removed, whatever the algo */
          if (saw && g_qerr_valid) {
              /* Always a sign and a fixed-width magnitude, so the digits don't
               * shuffle sideways when qErr crosses zero — otherwise the '-'
               * appears and disappears and shifts everything after it. */
              double q = (double)g_qerr_ns;
              char sign = (q < 0.0) ? '-' : '+';
              static char fq[8]; dtostrf(fabs(q), 4, 1, fq);  /* width 4: " 9.9" */
              snprintf(s, sizeof(s), "%c%sns", sign, fq);
          } else if (saw) {
              snprintf(s, sizeof(s), "---");
          } else {
              s[0] = '\0';
          }
          tft_val  (17, TFT_COL_R + TFT_S(70),  TFT_GRID_Y+3*TFT_ROW_H,
                    TFT_S(30), TFT_COL_VALUE, saw ? "qErr:" : "");
          tft_val_r(14, TFT_COL_R + TFT_S(148), TFT_GRID_Y+3*TFT_ROW_H,
                    TFT_S(48), TFT_COL_VALUE, s);
      }
  #endif
#else
      tft_val(9, TFT_COL_R, TFT_GRID_Y+3*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);
#endif

#if defined(GPSDO_TFT_ILI9488)
      /* Split so the current lands on the right column's alignment line, level
       * with the "ns" above it. Both fields need padding: each collapses to a
       * shorter form when the sensor is missing, and a shrinking string leaves
       * its old tail behind otherwise.
       *
       * Both dtostrf widths are sized for the LONGEST form, not the usual one,
       * because dtostrf's width is a minimum rather than a field size — ask for
       * five and "12.050" still prints six. Get that wrong and the text after
       * the number jumps a digit's width the moment the value crosses a power of
       * ten, which is exactly what the pressure field did below 1000 hPa.
       *   voltage 6: "12.050" / " 4.920".  Reads ~4.9 V today, but the changelog
       *     has this field as "IN:12.05V" in v0.92, so the crossing is a real
       *     configuration rather than a hypothetical.
       *   current 7: "1250.00" / " 178.00".  Draws ~180 mA today with an OCXO,
       *     but a DOCXO's oven pulls well over an amp on warm-up, and the point
       *     of this field is to watch exactly that.
       *
       * The split is measured, not apportioned. align_R was measured from the
       * phase row's whole string, so the current's widest form is the only other
       * number needed and the two then tile the row exactly. That matters more
       * here than elsewhere: at 1250 mA the two fields want ~211 px of the 219
       * available, so there is no room for a constant that guesses wrong. */
      { static char fv[10],fi[10];
        static int16_t ina_ma_w = 0;
        if (ina_ma_w == 0) ina_ma_w = tft_text_w("0000.00 mA");
        if (ina_ok) {
            dtostrf(g_ina_volt,6,3,fv); dtostrf(g_ina_curr,7,2,fi);
            snprintf(s,sizeof(s),"INA: %s V",fv);
        } else {
            snprintf(s,sizeof(s),"INA: ---");
        }
        tft_val(10, TFT_COL_R, TFT_GRID_Y+4*TFT_ROW_H,
                (uint16_t)(align_R - ina_ma_w - TFT_COL_R), TFT_COL_VALUE, s);
        if (ina_ok) snprintf(s,sizeof(s),"%s mA",fi);
        else        s[0] = '\0';
        tft_val_r(20, align_R, TFT_GRID_Y+4*TFT_ROW_H,
                  (uint16_t)ina_ma_w, TFT_COL_VALUE, s);
      }
#else
      if (ina_ok) { static char fv[10],fi[10];
          dtostrf(g_ina_volt,5,3,fv); dtostrf(g_ina_curr,6,2,fi);
          snprintf(s,sizeof(s),"INA: %sV %smA",fv,fi);
      }
      else snprintf(s,sizeof(s),"INA: ---");
      tft_val(10, TFT_COL_R, TFT_GRID_Y+4*TFT_ROW_H, TFT_S(148), TFT_COL_VALUE, s);
#endif

      /* ---- sensor row ---- */
      if (bmp_ok) { static char ft[8],fp[10];
          /* Pressure width is 7 (480) / 6 (320), not 6 / 5. dtostrf's width is a
           * MINIMUM, not a field size: "1013.25" overruns a 6 and prints seven
           * characters, while "999.87" fits and prints six. So every time the
           * weather dropped below 1000 hPa the string lost a character and the
           * whole tail — "hPa" included — slid one digit left. Asking for the
           * width the four-digit form actually needs pads the three-digit form
           * with a leading space instead, and the unit stops moving.
           *
           * This is also what the left column's alignment line is measured
           * against ("BMP: 00.00 C 0000.00 hPa"), so below 1000 hPa the field
           * was not even reaching its own line. */
#if defined(GPSDO_TFT_ILI9488)
          dtostrf(g_bmp_temp,5,2,ft); dtostrf(g_bmp_pres,7,2,fp);
          snprintf(s,sizeof(s),"BMP: %s C %s hPa",ft,fp);
#else
          dtostrf(g_bmp_temp,4,1,ft); dtostrf(g_bmp_pres,6,1,fp);
          snprintf(s,sizeof(s),"BMP: %sC %shPa",ft,fp);
#endif
      }
      else snprintf(s,sizeof(s),"BMP: ---");
      tft_val(11, TFT_COL_L, TFT_SENS_Y, TFT_S(156), TFT_COL_VALUE, s);

      if (aht_ok) { static char ft[8],fh[8];
          dtostrf(g_aht_temp,4,2,ft); dtostrf(g_aht_humi,4,2,fh);
#if defined(GPSDO_TFT_ILI9488)
          /* temp left-anchored in the right column; humidity right-anchored to
           * the screen edge (like Vdd) so the "% rH" stays pinned. Draw them
           * as two separate slots so neither floats when the other changes. */
          snprintf(s,sizeof(s),"AHT: %s C",ft);
#else
          snprintf(s,sizeof(s),"AHT: %sC %s%%rH",ft,fh);
#endif
      }
      else {
#if defined(GPSDO_TFT_ILI9488)
          snprintf(s,sizeof(s),"AHT: ---");
#else
          snprintf(s,sizeof(s),"AHT: ---");
#endif
      }
#if defined(GPSDO_TFT_ILI9488)
      /* AHT sits in the LEFT column of the second sensor row, directly under
       * BMP: the two environmental sensors now share a column and the
       * electrical fields — phase, then the supply rails — share the other. It
       * swapped places with Vph/dph, which is drawn further down in this file
       * because the phase field has to stay inside the LTIC guard; source order
       * and screen order part company here.
       *
       * Temp left-anchored, humidity right-anchored to TFT_COL_LVAL so the
       * "% rH" stays pinned. Two separate slots, so neither floats when the
       * other changes width. Padding: temp TFT_S(70) = 105 px covers
       * "AHT: 25.50 C" (~103); humidity TFT_S(60) = 90 px covers "45.50 % rH"
       * (~88). They fill 12..117 and 147..237, and the 30 px between is written
       * by neither, so it stays background.
       *
       * The right anchor is align_L — the same line "hPa" ends on in the row
       * above and "Vct: … V" ends on further up, so the column has one edge
       * rather than three near-misses. That also keeps it clear of Vcc, which
       * starts at 252 in the next column. */
      { const int16_t rh_pad = TFT_S(60);            /* fits "45.30 % rH" */
        tft_val(12, TFT_COL_L, TFT_SENS_Y + TFT_ROW_H,
                (uint16_t)(align_L - rh_pad - TFT_COL_L), TFT_COL_VALUE, s);
        if (aht_ok) { static char fh[8]; dtostrf(g_aht_humi,4,2,fh);
            snprintf(s,sizeof(s),"%s %% rH",fh);
            tft_val_r(16, align_L, TFT_SENS_Y + TFT_ROW_H,
                      (uint16_t)rh_pad, TFT_COL_VALUE, s);
        } else {
            tft_val_r(16, align_L, TFT_SENS_Y + TFT_ROW_H,
                      (uint16_t)rh_pad, TFT_COL_VALUE, "");
        }
      }
#else
      tft_val(12, TFT_COL_R, TFT_SENS_Y, TFT_S(148), TFT_COL_VALUE, s);
#endif

#ifdef GPSDO_LTIC
      /* ---- LTIC phase row (only when the TIC hardware is built in) ----
       * Shows the latched TIC voltage, and — once LC has calibrated the
       * detector — the derived phase in ns. Phase is measured RELATIVE to the
       * calibrated zero_offset (the mid-point of the swept band), not from
       * 0 V, and uses the MEASURED ns_per_volt from LC, mirroring the loop's
       * own ltic_phase_error_ns(). The old code multiplied the raw voltage by
       * the compile-time LTIC_NS_PER_VOLT constant (default 0), so it showed
       * nothing once calibrated and would have been wrong (no zero_offset) if
       * the constant were set. */
      {
          static char fv[8]; dtostrf((double)g_ltic_voltage, 5, 3, fv);
          const bool cal = (g_ltic.ns_per_volt > 1.0f);
          /* ns_per_volt is a LOCAL slope: LC reads it in a narrow window around
           * the anchor it places at 0.632*Vsat, because the ramp is
           * V = Vsat*(1 - e^(-phase/tau)) and an exponential has no single
           * slope. Far from that anchor the curve flattens and the linear
           * reading understates the phase; past Vsat the stop pulse has simply
           * missed the window and the cap charges on to the supply rail, at
           * which point the number means nothing whatever.
           *
           * That last state is not hypothetical and not rare: it has twice been
           * reported here as a rock-steady "+1561 ns" while the phase was in
           * fact somewhere unknown, and both times it cost a measurement before
           * anyone noticed. A reading that is wrong is recoverable; a reading
           * that is wrong and looks calm is not.
           *
           * Vsat is not stored — LC fits it, places the anchor and discards it —
           * but the anchor IS 0.632*Vsat by construction, so zero_offset
           * recovers it. The 15%..85% band is where the local slope stays
           * within about a third of its anchor value.
           *
           * The loop's own guard (railed_now, GPSDO_algorithms.cpp) tests a
           * hard-coded 3.28 V. On a detector saturating near 2.9 V that never
           * fires, so it is no help here and is worth a look in its own right. */
          double ns = 0.0;
          bool in_band = false;
          if (cal) {
              double centre = (g_ltic.zero_offset > 0.001f)
                              ? (double)g_ltic.zero_offset : 0.22;
              double vsat_est = centre / 0.63212;
              double v = (double)g_ltic_voltage;
              in_band = (v > 0.15 * vsat_est) && (v < 0.85 * vsat_est);
              ns = (v - centre) * (double)g_ltic.ns_per_volt;
              /* Subtract the receiver's sawtooth, exactly as ltic_phase_error_ns()
               * does for the loop. Without this the display was computing phase
               * down a second, parallel path that skipped the correction — so
               * algo 10 steered on a corrected phase while showing an
               * uncorrected one, and the two differed by the whole sawtooth
               * (~±10 ns on a LEA-6T, more on an M8T). Measured on air as ~14 ns
               * of 1-sigma scatter on an otherwise flat reading.
               *
               * Not gated on the algorithm: ubx_timtp_correction_ns() already
               * returns 0 unless SAW is on and a fresh qErr exists, and the
               * sawtooth is the receiver's, not the loop's — it is there to be
               * removed whatever is steering. This is what makes dph a usable
               * instrument under algorithms 3-9, which never call the loop's
               * phase path at all.
               *
               * Pairing is sound: UBX-TIM-TP describes the NEXT pulse, so it
               * arrives after pulse N-1 carrying qErr(N), and g_ltic_voltage is
               * sampled on pulse N's own ramp peak. Same pulse, same qErr. */
              ns -= (double)ubx_timtp_correction_ns();
          }
#if defined(GPSDO_TFT_ILI9488)
          /* FIRST sensor row, RIGHT column — swapped with AHT so the
           * environmental sensors share the left column and the electrical
           * fields the right, with the supply rails directly beneath.
           *
           * Split into label and value, like qErr and Vdd: the label holds the
           * column's left edge, "ns" is pinned to the column's right-hand
           * alignment line, and only the gap between them moves. Drawn as one
           * string the tail wandered — "%+4ld" is four characters wide for a
           * three-digit reading and five for a four-digit one, so "ns" hopped
           * sideways every time the phase crossed 1000, which is precisely when
           * one is watching it.
           *
           * "dph", not "dPh", to match "Vph" beside it: same shape, same case.
           *
           * The split is measured, not apportioned by eye. align_R was itself
           * measured from the whole string, so the label's own width is the only
           * other number needed and the two then tile the field exactly — no
           * constant has to assume how wide FreeSans renders "Vph: 0.000 V dph:".
           * Guessing here is what makes fields overlap and eat each other's
           * edges on repaint, and the guesses in this file have not had a good
           * record.
           *
           * Both fields need their padding: each collapses to a shorter form —
           * the value to nothing at all — when LC has not calibrated the ramp,
           * and a shrinking string leaves its tail behind otherwise. The label's
           * pad equals its own widest form, which is exactly what erases "dph:"
           * when the reading falls back to bare volts. */
          static int16_t ph_lab_w = 0;
          if (ph_lab_w == 0) ph_lab_w = tft_text_w("Vph: 0.000 V dph:");
          const int16_t ns_pad = (int16_t)(align_R - TFT_COL_R - ph_lab_w);
          snprintf(s, sizeof(s), cal ? "Vph: %s V dph:" : "Vph: %s V", fv);
          tft_val(13, TFT_COL_R, TFT_SENS_Y,
                  (uint16_t)ph_lab_w, TFT_COL_VALUE, s);
          /* "ovf" rather than a dimmed colour: tft_val only repaints when the
           * STRING changes, and out of band the value is pinned — so a colour
           * would never get applied in exactly the case it was meant for. The
           * raw Vph sits beside it and says which end it ran out of. */
          if (cal && in_band) snprintf(s, sizeof(s), "%+ldns", (long)ns);
          else if (cal)       snprintf(s, sizeof(s), "ovf");
          else                s[0] = '\0';
          tft_val_r(21, align_R, TFT_SENS_Y,
                    (uint16_t)ns_pad, TFT_COL_VALUE, s);
#else
          if (cal) snprintf(s, sizeof(s), "Vph:%sV dph:%+4ldns", fv, (long)ns);
          else     snprintf(s, sizeof(s), "Vph:%sV", fv);
          tft_val(13, TFT_COL_L, TFT_SENS_Y + TFT_ROW_H,
                  TFT_S(156), TFT_COL_VALUE, s);
#endif
      }
#endif /* GPSDO_LTIC — the phase row ends here */

      /* Supply rails: Vcc (the 5 V input, halved by a divider on PA0) and Vdd
       * (the MCU's own 3.3 V, from VREFINT: 1.21 V * 4096 / adc).
       *
       * Deliberately OUTSIDE the LTIC guard above. Neither rail has anything to
       * do with the TIC hardware; Vdd was only ever gated that way because it
       * happened to be written next to the phase field, and a board without a
       * TIC still has a 3.3 V rail worth watching. The two share this row's
       * right half — the left half is the phase field, which simply stays empty
       * without LTIC.
       *
       * Drawn as separate fields rather than one string, so neither shifts as
       * the other's width changes: Vcc is left-aligned in the R column, Vdd is
       * anchored to the right screen edge (TR_DATUM), same as the LMT clock. */
      {
          static char fv[8];
          float vdd = (c->avg_vdd_adc > 0)
                    ? (float)((1.21 * 4096.0) / (double)c->avg_vdd_adc) : 0.0f;
#if defined(GPSDO_TFT_ILI9488)
  #ifdef GPSDO_VCC
          /* Same conversion as the telemetry line: the divider halves the rail
           * and the ADC reference is the nominal 3.3 V. Vdd beside it is derived
           * from VREFINT and is the honest figure for that reference — if this
           * ever has to be a measurement rather than an indication, scale by the
           * measured Vdd instead of the nominal and the two will stop disagreeing
           * by ~0.5%. */
          if (c->avg_vcc_adc > 0) {
              /* Three decimals, like Vctl and the INA voltage — the divider
               * halves the rail, so the ADC's own LSB is worth ~1.6 mV at the
               * input and the third digit is carrying real information rather
               * than dressing up noise. */
              static char fc[10];
              dtostrf((double)c->avg_vcc_adc / 4096.0 * 3.3 * 2.0, 5, 3, fc);
              snprintf(s, sizeof(s), "Vcc: %s V", fc);
          } else {
              snprintf(s, sizeof(s), "Vcc: ---");
          }
          tft_val(18, TFT_COL_R, TFT_SENS_Y + TFT_ROW_H,
                  TFT_S(75), TFT_COL_VALUE, s);
  #endif
          /* Vdd at full precision. It used to drop to 1 decimal whenever qErr
           * shared the row — that was the only reason — and qErr has moved up to
           * the Alt row. Vcc's field ends at 364 and this padding starts at 369,
           * so the two background fills never meet. */
          dtostrf(vdd, 4, 2, fv);
          snprintf(s, sizeof(s), "Vdd: %s V", fv);
          tft_val_r(15, TFT_W - TFT_S(6), TFT_SENS_Y + TFT_ROW_H,
                    TFT_S(66), TFT_COL_VALUE, s);
#else
          /* 320 has no room for Vcc, so qErr keeps this row where it always was
           * — but only when the TIC is built, since qErr only ever shows under
           * algo 10. Without it there is nothing to make room for and Vdd keeps
           * its second decimal. */
  #ifdef GPSDO_LTIC
          /* Always a sign and a fixed-width magnitude, so the digits don't
           * shuffle sideways when qErr crosses zero (the '-' would otherwise
           * appear and disappear, shifting everything after it). */
          bool saw = g_qerr_enable;   /* what dph has had removed, whatever the algo */
          if (saw) {
              if (g_qerr_valid) {
                  double q = (double)g_qerr_ns;
                  char sign = (q < 0.0) ? '-' : '+';
                  static char fq[8]; dtostrf(fabs(q), 4, 1, fq); /* width 4: " 9.9" */
                  snprintf(s, sizeof(s), "qErr:%c%sns", sign, fq);
              } else {
                  snprintf(s, sizeof(s), "qErr: ---");
              }
          } else {
              s[0] = '\0';
          }
          tft_val(14, TFT_COL_R, TFT_SENS_Y + TFT_ROW_H, TFT_S(84), TFT_COL_VALUE, s);
  #else
          const bool saw = false;
  #endif
          /* Padding 66 px fits the longest form "Vdd: 3.29V" (SAW off). When SAW
           * is on and qErr occupies the left of the row, Vdd is the short
           * "Vdd: 3.3V" (1 decimal), so the wider background never reaches the
           * qErr field — the long Vdd only appears when qErr is blank. */
          dtostrf(vdd, saw ? 3 : 4, saw ? 1 : 2, fv);
          snprintf(s, sizeof(s), "Vdd: %sV", fv);
          tft_val_r(15, TFT_W - TFT_S(6), TFT_SENS_Y + TFT_ROW_H,
                    TFT_S(66), TFT_COL_VALUE, s);
#endif
      }

      /* ---- push the data sprite if any cell changed this cycle ----
       * 1-bit pushSprite paints every pixel (bit=1 → bitmap_fg=white, bit=0 →
       * bitmap_bg=black), so the whole sprite region is rewritten on the panel.
       * To avoid erase-then-draw flicker on the frame, the frame lines that
       * pass through the data region are drawn ON the sprite (in white) before
       * the push, so frame + text go out together in one atomic transfer.
       * Separators above/below the sprite (freq sep y=58, status sep y=210)
       * are just outside the sprite top/bottom edges and are never touched. */
      if (s_data_sprite_ok && s_data_dirty) {
          /* Draw frame segments on the sprite (sprite-local Y = screen Y − top).
           * Lines are drawn in white (non-zero → bit 1) so they show as a white
           * frame on the panel — flicker-free, because frame + text go out in
           * one atomic push. Side rails run the full sprite height; the sensor
           * separator crosses horizontally. The freq/status separators are just
           * outside the sprite top/bottom and are never touched. */
          s_data_sprite.drawFastVLine(0,         0, DATA_SPR_H, TFT_WHITE); /* left rail  */
          s_data_sprite.drawFastVLine(TFT_W - 1, 0, DATA_SPR_H, TFT_WHITE); /* right rail */
#if defined(GPSDO_TFT_ILI9488)
          /* Centre divider: 480 only — at 320 the columns reach the middle and
           * the line would cut through the text. */
          s_data_sprite.drawFastVLine(TFT_W/2,   0, DATA_SPR_H, TFT_WHITE);
#endif
          int grid_sep_local = TFT_SY(168) - DATA_SPR_Y;  /* sensor separator */
          s_data_sprite.drawFastHLine(0, grid_sep_local, TFT_W, TFT_WHITE);
          s_data_sprite.pushSprite(0, DATA_SPR_Y);
          s_data_dirty = false;
      }

      /* ---- status bar (full redraw only on state change) ---- */
      {
          /* states: 0=no fix, 1=disciplined, 2=manual HO, 3=auto HO */
          uint8_t st;
          if      (c->holdover_mode && c->holdover_auto) st = 3;
          else if (c->holdover_mode)                     st = 2;
          else if (g->pos_valid)                         st = 1;
          else                                           st = 0;

          /* A survey-in that outlived our monitor window is still running inside
           * the receiver, and nothing else on screen would say so. It rides on
           * the status text rather than owning a slot of its own: this bar
           * repaints its entire background before drawing, so an extra word
           * costs nothing and cannot be clipped by a neighbour's padding — which
           * is precisely what defeated it as a separate header field.
           *
           * g_svin_background already means the right thing: set at the survey
           * timeout, and only if the receiver was actually replying, then
           * cleared the moment Time Mode arrives. */
          const bool svin = g_svin_background;

          /* The survey state must be part of the redraw key. Keyed on `st`
           * alone, the bar would hold its old text until the fix state happened
           * to change — which, once disciplined, could be hours. */
          static uint8_t prev_key = 0xFF;
          const uint8_t key = (uint8_t)(st | (svin ? 0x10u : 0u));
          if (key != prev_key) {
              prev_key = key;
              uint16_t bg; const char *base;
              switch (st) {
                  case 1:  bg = TFT_COL_LOCK;  base = "DISCIPLINED  FIX OK";   break;
                  case 2:  bg = TFT_COL_HOLD;  base = "HOLDOVER (manual)";     break;
                  case 3:  bg = TFT_COL_ALERT; base = "HOLDOVER (fix lost)";   break;
                  default: bg = TFT_COL_ALERT; base = "WAITING FOR GPS FIX";   break;
              }
              /* Full word on the 480, abbreviated on the 320. The longest line
               * plus " SURVEY" measures ~365 px of the 470 available in
               * FreeSansBold12pt, but ~338 px of only 312 in font 4. */
#if defined(GPSDO_TFT_ILI9488)
              static const char SVIN_SUFFIX[] = " SURVEY";
#else
              static const char SVIN_SUFFIX[] = " SV";
#endif
              char txt[40];
              snprintf(txt, sizeof(txt), "%s%s", base, svin ? SVIN_SUFFIX : "");
              /* Fill the whole band from the separator to the screen bottom so
               * no dead colour strip is left below the text (the old fixed
               * TFT_SY(36) height left a gap on the taller 480×320 panel). */
              s_tft.fillRect(0, TFT_STATUS_Y, TFT_W, TFT_H - TFT_STATUS_Y, bg);
              TFT_FONT_STATUS(s_tft);
              s_tft.setTextDatum(MC_DATUM);
              s_tft.setTextColor(TFT_BLACK, bg);
              s_tft.setTextPadding(0);
              /* Vertical placement in the band. MC_DATUM centres the font's
               * glyph BOX, but these labels are all-caps — so the ~4 px of
               * descender space at the bottom of the box is entirely empty
               * while the caps butt up near the top, and the visible ink ends
               * up sitting high in the bar. Nudging down 2 px centres what the
               * eye actually sees. There is room: with no descenders the ink
               * stops ~3 px short of the screen edge even after the nudge.
               * 480×320 is left as-is — verified good on-panel. */
#if defined(GPSDO_TFT_ILI9488)
              #define STATUS_TEXT_NUDGE 0
#else
              #define STATUS_TEXT_NUDGE 2
#endif
              s_tft.drawString(txt, TFT_W/2,
                               (TFT_STATUS_Y + TFT_H) / 2 + STATUS_TEXT_NUDGE);
              s_tft.setTextDatum(TL_DATUM);
          }
      }
  }
#endif /* GPSDO_TFT */


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

  /* TM1637 write cache. The TM1637 protocol is software bit-banged (~5–8 ms
   * for a full 6-digit write at the default bit delay), and the display task
   * now wakes ~every 150 ms during spinner animations. Without a cache each
   * wake would re-push identical segments and burn milliseconds for nothing.
   * tm_set() compares against the last pattern and only bit-bangs on a real
   * change, so a spinner that only advances every 200 ms costs one transfer
   * per frame, not one per wake. */
  static uint8_t s_tm_prev[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
  static bool    s_tm_prev_valid = false;
  static void tm_set(const uint8_t *seg, uint8_t n)
  {
      if (s_tm_prev_valid && n <= sizeof(s_tm_prev) &&
          memcmp(s_tm_prev, seg, n) == 0)
          return;                       /* identical — skip the bit-bang */
      s_tm.setSegments(seg, n);
      if (n <= sizeof(s_tm_prev)) { memcpy(s_tm_prev, seg, n); s_tm_prev_valid = true; }
  }
  /* Any showNumberDec* path writes digits directly (bypassing tm_set), so it
   * must invalidate the cache or a following identical setSegments would be
   * wrongly skipped. Call after each showNumberDec*. */
  static inline void tm_cache_invalidate(void) { s_tm_prev_valid = false; }

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

extern int16_t g_time_offset_min;
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
 * apply_time_offset — add g_time_offset_min minutes to UTC h:m:s, adjusting
 * the date if that crosses midnight in either direction.
 *
 * Minutes, not hours: half-hour zones (India +5:30, Chatham +12:45) mean the
 * minutes field shifts too, so the old hours-only version could never be
 * right there. Working in total minutes-from-midnight also makes the wrap
 * arithmetic a plain loop instead of the tangle of ifs it replaced.
 * ----------------------------------------------------------------------- */
static uint8_t days_in(uint8_t mon, uint16_t yr)
{
    static const uint8_t dim[13] = { 0, 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (mon < 1 || mon > 12) return 30;      /* garbage in — don't index off the end */
    uint8_t d = dim[mon];
    /* Proper leap rule, not just yr%4: 2100 is not a leap year, and this
     * firmware may well outlive the assumption. */
    if (mon == 2 && ((yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0)) d = 29;
    return d;
}

static void apply_time_offset(uint8_t  utc_h,  uint8_t utc_m,  uint8_t utc_s,
                               uint8_t  utc_day, uint8_t utc_mon, uint16_t utc_yr,
                               uint8_t *lh, uint8_t *lm, uint8_t *ls,
                               uint8_t *ld, uint8_t *lmo, uint16_t *lyr)
{
    int32_t tot  = (int32_t)utc_h * 60 + utc_m + g_time_offset_min;
    int8_t  dday = 0;
    while (tot <    0) { tot += 1440; dday--; }
    while (tot >= 1440) { tot -= 1440; dday++; }

    *lh = (uint8_t)(tot / 60);
    *lm = (uint8_t)(tot % 60);
    *ls = utc_s;
    *ld = utc_day; *lmo = utc_mon; *lyr = utc_yr;

    if (dday > 0) {
        (*ld)++;
        if (*ld > days_in(*lmo, *lyr)) {
            *ld = 1;
            if (++(*lmo) > 12) { *lmo = 1; (*lyr)++; }
        }
    } else if (dday < 0) {
        if (*ld > 1) {
            (*ld)--;
        } else {
            if (*lmo > 1) (*lmo)--; else { *lmo = 12; (*lyr)--; }
            *ld = days_in(*lmo, *lyr);
        }
    }
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
         *   line2:  GPS Disciplined OCXO
         *   line3:  jmnlabs  +  Claude
         * lcd_set_line uses I2C → guard with the Wire mutex.            */
        if (xSemaphoreTake(xWireMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            char l[21];
            lcd_set_line(0, "====================");
            /* "  GPSDO vX.XX-rtos" — two leading spaces, suffix not cut */
            snprintf(l, sizeof(l), "  GPSDO %s", PROGRAM_VERSION);
            lcd_set_line(1, l);
            lcd_set_line(2, "GPS Disciplined OCXO");
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
         * after a timeout so the display doesn't freeze without GPS. The
         * timeout is SHORT while a LED spinner animation is running
         * (warmup / survey-in / calibration), because those animations step
         * their frame every 200 ms (millis()/200) and the task only redraws
         * when it wakes — at the normal 1100 ms PPS cadence the spinner would
         * update just once a second and look like it's "skipping" / running
         * ~5x too slow. During an animation we wake ~every 150 ms so the
         * spinner is smooth; otherwise we keep the slow 1100 ms cadence (the
         * clock only changes once a second, so there's nothing to gain from
         * waking faster, and it keeps the display task cheap). */
        bool anim_active = g_warmup_active || g_svin_active || g_calib_active;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(anim_active ? 150 : 1100));

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

        /* ---- Serial report (skipped when paused via RP command, and —
         * without Bluetooth — muted during GPS tunnel so telemetry never
         * pollutes the NMEA/UBX stream on the shared USB port; with BT the
         * report lives on Serial2 and does not clash with the USB bridge) */
        EventBits_t bits = xEventGroupGetBits(xSysEvents);
#ifndef GPSDO_BLUETOOTH
        bool tunnel_mute = (bits & EVT_TUNNEL_MODE) != 0;
#else
        bool tunnel_mute = false;
#endif
        if (!g_report_paused && !tunnel_mute) {
            /* Emit the serial report ONCE PER PPS. vDisplayTask is notified
             * from two ~1 Hz sources — the frequency relay (per PPS) and the
             * GPS parser (per time sentence) — so with a fix it wakes twice a
             * second and used to print two report lines per second (spotted by
             * Dan Wiering in RD mode). The display refresh below still runs on
             * every wake for smoothness; only the serial line is gated, on a
             * change of the PPS counter. */
            static uint32_t s_last_report_pps = 0xFFFFFFFFu;
            if (snap_f.ppscount != s_last_report_pps) {
                s_last_report_pps = snap_f.ppscount;
                if (bits & EVT_REPORT_TAB)
                    print_tab_report  (&snap_g, &snap_f, &snap_c, &snap_u);
                else
                    print_human_report(&snap_g, &snap_f, &snap_c, &snap_u);
            }
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
                tm_set(mid_dashes, TM1637_MAX_DIGITS);
            }
        }
        /* No GPS position fix → 'oooo'; fix → time; calibration → CAL.
         * pos_valid (not valid) is the true "GPS locked" indicator:
         * valid turns true on the first NMEA time sentence, before any
         * position lock, so it would show a stale 00:00:00. */
        if (g_warmup_active) {
            /* warmup: lowercase-'o' spinner on EVERY digit, phase-shifted per
             * digit so the chase travels across the display like a wave */
            static const uint8_t spin_o[4] = { SEG_G, SEG_C, SEG_D, SEG_E };
            uint8_t w[TM1637_MAX_DIGITS];
            uint8_t f = (uint8_t)((millis() / 200u) % 4u);
            for (uint8_t i = 0; i < TM1637_MAX_DIGITS; i++)
                w[i] = spin_o[(f + i) % 4u];
            tm_set(w, TM1637_MAX_DIGITS);
        } else if (g_svin_active) {
            /* survey-in: UPPER-'o' spinner (A→B→G→F chase around the top
             * loop of the digit), phase-shifted per digit — distinct from
             * the warmup's lower-'o' wave */
            static const uint8_t spin_top[4] = { SEG_A, SEG_B, SEG_G, SEG_F };
            uint8_t w2[TM1637_MAX_DIGITS];
            uint8_t f2 = (uint8_t)((millis() / 200u) % 4u);
            for (uint8_t i = 0; i < TM1637_MAX_DIGITS; i++)
                w2[i] = spin_top[(f2 + i) % 4u];
            tm_set(w2, TM1637_MAX_DIGITS);
        } else if (g_calib_active) {
            /* "CAL" + a spinner on digit 4: segments G→C→D→E chase in a loop,
             * tracing the outline of a lowercase 'o' — visual "working" cue */
            static const uint8_t spin_o[4] = { SEG_G, SEG_C, SEG_D, SEG_E };
            uint8_t seg_cal_anim[TM1637_MAX_DIGITS];
            memcpy(seg_cal_anim, seg_cal, sizeof(seg_cal_anim));
            seg_cal_anim[3] = spin_o[(millis() / 200u) % 4u];
            tm_set(seg_cal_anim, TM1637_MAX_DIGITS);
        } else if (!snap_g.pos_valid) {
            tm_set(low_oooo_s, TM1637_MAX_DIGITS);
        } else {
            /* Local time in minutes, so half-hour zones land on the right
             * minute digits too — these displays show hh:mm. */
            int h = snap_g.hours, mi = snap_g.mins;
            if (g_show_local_time) {
                int32_t tmin = (int32_t)h * 60 + mi + g_time_offset_min;
                while (tmin <    0) tmin += 1440;
                while (tmin >= 1440) tmin -= 1440;
                h = (int)(tmin / 60); mi = (int)(tmin % 60);
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
            long t = (long)h * 10000L + (long)mi * 100L + snap_g.secs;
            if ((snap_g.secs & 1) == 0)
                s_tm.showNumberDecEx(t, 0b01010000, true);
            else
                s_tm.showNumberDec(t, true);
#else
            /* 4-digit: HHMM, colon blinks on even seconds */
            long t = (long)h * 100L + mi;
            if ((snap_g.secs & 1) == 0)
                s_tm.showNumberDecEx(t, 0b01000000, true);
            else
                s_tm.showNumberDec(t, true);
            tm_cache_invalidate();   /* clock wrote digits directly */
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
            if (g_warmup_active) {
                /* warmup: 'o' spinner on every digit, phase-shifted (wave) */
                static const uint8_t sp[4] = { 0x40, 0x04, 0x08, 0x10 };  /* g,c,d,e */
                uint8_t f = (uint8_t)((millis() / 200u) % 4u);
                ht_write(sp[f], sp[(f+1)%4], sp[(f+2)%4], sp[(f+3)%4], false);
            } else if (g_svin_active) {
                /* survey-in: upper-'o' spinner (a→b→g→f), wave across digits */
                static const uint8_t st7[4] = { 0x01, 0x02, 0x40, 0x20 };  /* a,b,g,f */
                uint8_t f2 = (uint8_t)((millis() / 200u) % 4u);
                ht_write(st7[f2], st7[(f2+1)%4], st7[(f2+2)%4], st7[(f2+3)%4], false);
            } else if (g_calib_active) {
                /* "CAL" + lowercase-'o' spinner on digit 4 (G→C→D→E chase),
                 * colon off. 7-seg: C=A,D,E,F ; A=A,B,C,E,F,G ; L=D,E,F */
                const uint8_t SEG_C7 = 0x39, SEG_A7 = 0x77, SEG_L7 = 0x38;
                static const uint8_t spin_o7[4] = { 0x40, 0x04, 0x08, 0x10 };  /* g,c,d,e */
                uint8_t d = spin_o7[(millis() / 200u) % 4u];
                ht_write(SEG_C7, SEG_A7, SEG_L7, d, false);
            } else if (!snap_g.pos_valid) {
                ht_write(HT_SEG_o, HT_SEG_o, HT_SEG_o, HT_SEG_o, false);
            } else {
                /* As above: minutes, so +5:30 zones show correctly. */
                int h = snap_g.hours, mi = snap_g.mins;
                if (g_show_local_time) {
                    int32_t t = (int32_t)h * 60 + mi + g_time_offset_min;
                    while (t <    0) t += 1440;
                    while (t >= 1440) t -= 1440;
                    h = (int)(t / 60); mi = (int)(t % 60);
                }
                ht_write(ht_digit[h / 10],  ht_digit[h % 10],
                         ht_digit[mi / 10], ht_digit[mi % 10],
                         (snap_g.secs & 1) == 0);   /* colon on even seconds */
            }
            xSemaphoreGive(xWireMutex);
        }
#endif /* GPSDO_HT16K33 */

    } /* for(;;) */
}
