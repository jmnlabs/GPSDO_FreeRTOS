/**
 * gpsdo_control.cpp — vControlTask — OCXO control loop
 *
 * Part of GPSDO FreeRTOS v0.49
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Handles the complete OCXO disciplining lifecycle:
 *   1. Warmup delay (configurable, default 300 s)
 *   2. Two-point linear interpolation calibration
 *   3. Periodic PWM DAC adjustment via the selected algorithm
 *   4. ADC readings (Vctl, Vcc, Vdd) with 10-sample moving average
 *   5. picDIV arm sequencing
 *   6. Automatic holdover on GPS fix loss / recovery
 */

#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include "GPSDO_algorithms.h"
#include <Arduino.h>
#include <string.h>

/* Shared with CLI task */
float g_pressure_offset = 1230.0f;
float g_altitude_offset = 1.0f;
int8_t g_time_offset    = 2;
bool   g_show_local_time = true;
bool   g_report_paused  = false;
bool   g_tz_auto        = false;   /* true = derive g_time_offset from GPS */
bool   g_svin_enabled   = true;    /* true = run survey-in on timing module */

volatile bool     g_calib_active    = false;
volatile uint16_t g_calib_remaining = 0;
volatile bool     g_warmup_active    = false;
volatile uint16_t g_warmup_remaining = 0;
volatile bool     g_svin_active = false;
volatile bool     g_svin_pending = false;
volatile uint16_t g_svin_dur    = 0;
volatile uint16_t g_svin_acc_m  = 0;

/* ----------------------------------------------------------------------
 * Automatic timezone from GPS position (TO A command)
 *
 * Civil timezones do not follow meridians, so a pure round(lon/15) rule
 * is wrong across most of Europe (e.g. eastern Poland at 23.6°E is CET,
 * not EET).  Approach:
 *
 *  Inside the European box (lat 35..72, lon −11..42) a compact rule set
 *  approximates the civil zones:
 *    lon < −7.5                       → UTC+0  (UK, IE, PT)
 *    lat 49..55  and lon < 24.3       → UTC+1  (Poland incl. eastern tip)
 *    lat ≥ 55    and lon ≥ 19.5       → UTC+2  (Baltics, Finland)
 *    lat < 49    and lon ≥ 22.6       → UTC+2  (RO, BG, GR)
 *    lon ≥ 26.5                       → UTC+2  (eastern edge)
 *    otherwise                        → UTC+1  (CET core)
 *  plus the EU DST rule: +1 h from the last Sunday of March 01:00 UTC
 *  to the last Sunday of October 01:00 UTC.
 *
 *  Outside Europe: round(lon/15) solar-zone estimate, no DST (DST rules
 *  vary too much worldwide to guess safely).
 * ---------------------------------------------------------------------- */

/* Day of week, Sunday = 0 (Zeller) — local copy, display task has its own */
static uint8_t tz_dow(uint8_t d, uint8_t m, uint16_t y)
{
    if (m < 3) { m += 12; y--; }
    return (uint8_t)((d + 13*(m+1)/5 + y + y/4 - y/100 + y/400 + 6) % 7);
}

/* EU DST: active between last Sun of March 01:00 UTC
 *                  and last Sun of October 01:00 UTC                    */
static bool tz_eu_dst(uint8_t day, uint8_t month, uint16_t year, uint8_t hour_utc)
{
    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;
    /* March and October both have 31 days; find last Sunday */
    uint8_t last_sun = 31 - tz_dow(31, month, year);
    if (month == 3)
        return (day > last_sun) || (day == last_sun && hour_utc >= 1);
    else /* October */
        return (day < last_sun) || (day == last_sun && hour_utc < 1);
}

static int8_t tz_auto_offset(float lat, float lon,
                             uint8_t day, uint8_t month, uint16_t year,
                             uint8_t hour_utc)
{
    /* European box: civil-zone rules + EU DST */
    if (lat >= 35.0f && lat <= 72.0f && lon >= -11.0f && lon <= 42.0f) {
        int8_t base;
        if      (lon < -7.5f)                          base = 0;
        else if (lat >= 49.0f && lat < 55.0f
                              && lon < 24.3f)          base = 1;  /* Poland */
        else if (lat >= 55.0f && lon >= 19.5f)         base = 2;  /* Baltic/FI */
        else if (lat <  49.0f && lon >= 22.6f)         base = 2;  /* RO/BG/GR  */
        else if (lon >= 26.5f)                         base = 2;
        else                                           base = 1;  /* CET core  */
        if (tz_eu_dst(day, month, year, hour_utc)) base++;
        return base;
    }
    /* Rest of world: solar zone, no DST guessing */
    int z = (int)((lon >= 0 ? lon + 7.5f : lon - 7.5f) / 15.0f);
    if (z >  14) z =  14;
    if (z < -12) z = -12;
    return (int8_t)z;
}

/* picDIV arm sequencing */
static uint32_t s_arm_started_ms  = 0;     /* timestamp of arm LOW edge      */
static bool     s_arm_active      = false; /* true while Arm pin is held LOW */
static bool     s_arm_wait_warned = false; /* one-shot no-fix warning        */

/* Simple integer moving average (N=10) without heap alloc */
typedef struct {
    int32_t  sum;
    int16_t  buf[10];
    uint8_t  idx;
    bool     full;
} MovAvg10_t;

static int16_t movavg_update(MovAvg10_t *m, int16_t v)
{
    if (m->full) m->sum -= m->buf[m->idx];
    m->buf[m->idx] = v;
    m->sum += v;
    m->idx = (m->idx + 1) % 10;
    if (m->idx == 0) m->full = true;
    return (int16_t)(m->sum / (m->full ? 10 : (m->idx == 0 ? 10 : m->idx)));
}

static MovAvg10_t s_avg_vctl = {0}, s_avg_vcc = {0}, s_avg_vdd = {0};

/* ---- Wait N seconds, yielding to RTOS ---------------------------------
 * During calibration the main control loop (which normally reads the Vctl
 * ADC and publishes PWM) is blocked here, so the displays would freeze on
 * stale PWM/Vctl.  We therefore publish the current PWM and sample the
 * Vctl ADC ourselves each second, keeping the displays live.            */
static void wait_secs_pwm(uint16_t n, uint16_t cur_pwm)
{
    while (n--) {
        g_calib_remaining = n + 1;
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gCtrl.pwm_output   = cur_pwm;
            gCtrl.avg_vctl_adc = movavg_update(&s_avg_vctl,
                                   (int16_t)analogRead(PIN_VCTL_ADC));
            xSemaphoreGive(xCtrlMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    g_calib_remaining = 0;
}

/* Legacy entry point — waits without touching PWM (non-calibration use) */
static void wait_secs(uint16_t n)
{
    while (n--) {
        g_calib_remaining = n + 1;     /* seconds still to wait */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    g_calib_remaining = 0;
}

/* ---- Calibration routine ---------------------------------------------- */
static void do_calibration(void)
{
    OUT_SERIAL.println("Calibration started");
    g_calib_active = true;

    /* Set Vctl=1.5V */
    analogWrite(PIN_VCTL_PWM, 30720);
    OUT_SERIAL.print("Vctl=1.5V, waiting "); OUT_SERIAL.print(OCXO_CALIB_SECS); OUT_SERIAL.println("s");
    wait_secs_pwm(OCXO_CALIB_SECS, 30720);

    /* Snapshot frequency averages */
    double f1 = 0.0;
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gpsdo_calc_averages(&gFreq);
        f1 = gFreq.avg10;
        xSemaphoreGive(xFreqMutex);
    }
    OUT_SERIAL.print("f1="); OUT_SERIAL.println(f1, 2);

    /* Set Vctl=2.5V */
    analogWrite(PIN_VCTL_PWM, 51200);
    OUT_SERIAL.print("Vctl=2.5V, waiting "); OUT_SERIAL.print(OCXO_CALIB_SECS); OUT_SERIAL.println("s");
    wait_secs_pwm(OCXO_CALIB_SECS, 51200);

    double f2 = 0.0;
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gpsdo_calc_averages(&gFreq);
        f2 = gFreq.avg10;
        xSemaphoreGive(xFreqMutex);
    }
    OUT_SERIAL.print("f2="); OUT_SERIAL.println(f2, 2);

    /* Linear interpolation: PWM for 10 MHz */
    uint16_t new_pwm = DEFAULT_PWM_OUTPUT;
    if (f2 != f1) {
        double slope = (f2 - f1) / 20480.0;
        int32_t v = (int32_t)(30720.0 - (f1 - 10000000.0) / slope);
        if (v < 1)     v = 1;
        if (v > 65535) v = 65535;
        new_pwm = (uint16_t)v;
    }
    OUT_SERIAL.print("Calibrated PWM="); OUT_SERIAL.println(new_pwm);

    analogWrite(PIN_VCTL_PWM, new_pwm);

    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gCtrl.pwm_output = new_pwm;
        xSemaphoreGive(xCtrlMutex);
    }
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }

    xEventGroupClearBits(xSysEvents, EVT_NEED_CALIBRATION);
    g_calib_active = false;

    /* Do NOT auto-arm the picDIV here: the discipline loop has not yet
     * converged after calibration, so the OCXO frequency still carries an
     * error and the picDIV phase would immediately start drifting from
     * GPS.  Arm manually (AP command) once the loop reports lock.        */
    OUT_SERIAL.println("Calibration done");
#ifdef GPSDO_PICDIV
    OUT_SERIAL.println("Tip: arm picDIV (AP) after the loop locks (trend 'hit')");
#endif
}

/* ======================================================================
 * do_calibrate_tune (CT command)
 *
 * Extended calibration that BOTH centres the PWM and derives the PID
 * coefficients for all algorithms from the measured plant gain.
 *
 * Procedure (deterministic, ~3 × OCXO_CALIB_SECS):
 *   1. Drive PWM to three points (1.5 / 2.0 / 2.5 V), settle, measure f.
 *   2. Least-squares slope → K [Hz/LSB] (the plant gain), plus the PWM
 *      that yields exactly 10 MHz.
 *   3. Compute coefficients from K:
 *        PLL (4,5,7): Kp = 0.40/K on frequency; Kd=2.0, Ki=0.02 on phase
 *        FLL (3,6):   Kp = 0.35/K; Ki = Kp/300; Kd = Kp·73
 *        NN  (9):     nn_max_step = 0.05/K
 *   4. Write g_pid[], show before/after.  Caller (CT) decides on ES save.
 *
 * No iterative tuning, no forced oscillation — safe to run any time.
 * ====================================================================== */
static void do_calibrate_tune(void)
{
    const uint16_t PWM_A = 30720;   /* 1.5 V */
    const uint16_t PWM_B = 39322;   /* 2.0 V */
    const uint16_t PWM_C = 51200;   /* 2.5 V */

    OUT_SERIAL.println("CT: calibrate + auto-tune started (3 points)");
    g_calib_active = true;

    double f[3]; const uint16_t pwm[3] = { PWM_A, PWM_B, PWM_C };
    for (int i = 0; i < 3; i++) {
        analogWrite(PIN_VCTL_PWM, pwm[i]);
        OUT_SERIAL.print("CT: PWM="); OUT_SERIAL.print(pwm[i]);
        OUT_SERIAL.print(" settle "); OUT_SERIAL.print(OCXO_CALIB_SECS);
        OUT_SERIAL.println("s");
        wait_secs_pwm(OCXO_CALIB_SECS, pwm[i]);
        f[i] = 0.0;
        if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            gpsdo_calc_averages(&gFreq);
            f[i] = gFreq.avg10;
            xSemaphoreGive(xFreqMutex);
        }
        OUT_SERIAL.print("CT: f"); OUT_SERIAL.print(i);
        OUT_SERIAL.print("="); OUT_SERIAL.println(f[i], 3);
    }

    /* Least-squares slope of f vs pwm over the three points → K [Hz/LSB] */
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < 3; i++) {
        sx += pwm[i]; sy += f[i];
        sxx += (double)pwm[i] * pwm[i];
        sxy += (double)pwm[i] * f[i];
    }
    double denom = 3.0 * sxx - sx * sx;
    if (denom == 0.0) {
        OUT_SERIAL.println("CT: ERROR singular fit — aborting, params unchanged");
        analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
        xEventGroupClearBits(xSysEvents, EVT_NEED_TUNE);
        g_calib_active = false;
        return;
    }
    double K = (3.0 * sxy - sx * sy) / denom;        /* Hz/LSB */
    double b = (sy - K * sx) / 3.0;                  /* intercept */

    /* Sanity: K must be positive and within an order of magnitude of the
     * physical range (0.1–2 mHz/LSB).  Reject noise/no-GPS runs.        */
    if (K < 1e-4 || K > 2e-3) {
        OUT_SERIAL.print("CT: ERROR K out of range (");
        OUT_SERIAL.print(K * 1000.0, 4);
        OUT_SERIAL.println(" mHz/LSB) — check GPS fix. Params unchanged.");
        analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
        xEventGroupClearBits(xSysEvents, EVT_NEED_TUNE);
        g_calib_active = false;
        return;
    }

    /* PWM for exactly 10 MHz: 10e6 = K·pwm + b */
    int32_t v = (int32_t)((10000000.0 - b) / K);
    if (v < 1)     v = 1;
    if (v > 65535) v = 65535;
    uint16_t new_pwm = (uint16_t)v;

    OUT_SERIAL.print("CT: K=");       OUT_SERIAL.print(K * 1000.0, 4);
    OUT_SERIAL.print(" mHz/LSB  PWM(10MHz)="); OUT_SERIAL.println(new_pwm);

    /* ---- derive coefficients from K ---- */
    double kp_pll = 0.40 / K;
    double kp_fll = 0.35 / K;

    /* PLL algorithms 4, 5, 7 — Kp on frequency, gentle fixed phase terms */
    for (int n = 4; n <= 7; n++) {
        if (n == 6) continue;                 /* 6 is FLL, handled below */
        g_pid[n].Kp = kp_pll;
        g_pid[n].Ki = 0.02;
        g_pid[n].Kd = 2.0;
    }
    /* FLL algorithms 3 and 6 — classic frequency-domain PID */
    for (int n = 3; n <= 6; n += 3) {
        g_pid[n].Kp = kp_fll;
        g_pid[n].Ki = kp_fll / 300.0;
        g_pid[n].Kd = kp_fll * 73.0;
    }
    /* NN output step */
    g_nn_max_step = 0.05 / K;

    /* Note: algo 8 (hybrid) is tuned implicitly — it reads g_pid[6] and
     * g_pid[7], both updated above.  algo 9 (NN) uses fixed weights, so
     * only g_nn_max_step matters.  g_pid[8]/[9] Kp/Ki/Kd are left as-is
     * on purpose (unused by those algorithms). */

    OUT_SERIAL.println("CT: new coefficients —");
    OUT_SERIAL.print("  PLL(4,5,7) Kp="); OUT_SERIAL.print(kp_pll, 1);
    OUT_SERIAL.println(" Kd=2.0 Ki=0.02");
    OUT_SERIAL.print("  FLL(3,6)   Kp="); OUT_SERIAL.print(kp_fll, 1);
    OUT_SERIAL.print(" Ki=");             OUT_SERIAL.print(kp_fll / 300.0, 4);
    OUT_SERIAL.print(" Kd=");             OUT_SERIAL.println(kp_fll * 73.0, 0);
    OUT_SERIAL.print("  NN max_step=");   OUT_SERIAL.println(g_nn_max_step, 0);

    /* Apply centred PWM */
    analogWrite(PIN_VCTL_PWM, new_pwm);
    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gCtrl.pwm_output = new_pwm;
        xSemaphoreGive(xCtrlMutex);
    }
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }

    xEventGroupClearBits(xSysEvents, EVT_NEED_TUNE);
    g_calib_active = false;
    OUT_SERIAL.println("CT: done. Review values, then 'ES' to save to EEPROM.");
}

/* ---- OCXO warmup ------------------------------------------------------ */
static void do_warmup(void)
{
    OUT_SERIAL.print("OCXO warming up, "); OUT_SERIAL.print(OCXO_WARMUP_SECS); OUT_SERIAL.println("s");
    g_warmup_active = true;
    for (uint16_t i = OCXO_WARMUP_SECS; i > 0; i--) {
        g_warmup_remaining = i;
        vTaskDelay(pdMS_TO_TICKS(1000));
        if ((i % 30) == 0) {
            OUT_SERIAL.print(i); OUT_SERIAL.println("s remaining");
        }
    }
    g_warmup_remaining = 0;
    g_warmup_active = false;
    xEventGroupSetBits(xSysEvents, EVT_OCXO_WARM);
    OUT_SERIAL.println("OCXO warmup done");
}

/* ---- Control task ----------------------------------------------------- */
void vControlTask(void *pvParameters)
{
    (void)pvParameters;

    /* ---- Warmup (skip if EEPROM had a stored value) ---- */
    if (!g_eeprom_valid)
        do_warmup();
    else
        xEventGroupSetBits(xSysEvents, EVT_OCXO_WARM);

    /* ---- Calibration (skip if EEPROM had a stored value) ---- */
    if (!g_eeprom_valid)
        xEventGroupSetBits(xSysEvents, EVT_NEED_CALIBRATION);

    for (;;)
    {
        /* Calibration requested? */
        if (xEventGroupGetBits(xSysEvents) & EVT_NEED_CALIBRATION)
            do_calibration();

        if (xEventGroupGetBits(xSysEvents) & EVT_NEED_TUNE)
            do_calibrate_tune();

        /* ---- picDIV arm sequence ----------------------------------------
         *
         * Per picDIV spec (PD11/PD13/PD17): holding Arm (pin 4) LOW for
         * >1 s stops the divider; it then synchronizes its output to the
         * next rising edge of Sync (pin 5), which is wired to GPS 1PPS.
         *
         * Critical conditions checked here:
         *  1. GPS fix must be present — with no 1PPS on Sync the divider
         *     stays stopped forever (dead output).  Arming is deferred
         *     until fix returns (event bit stays set).
         *  2. A dedicated bool flag (not the timestamp) tracks the pulse,
         *     avoiding a stuck-LOW edge case at millis() wrap.
         *  3. Arm LOW duration: PICDIV_ARM_MS (1001 ms) checked on a
         *     200 ms loop → actual LOW time 1.0–1.2 s, per spec.
         *
         * NOTE on long-term sync: the picDIV output is phase-coherent
         * with the OCXO, not with GPS.  FLL algorithms (0,3,6) bound only
         * frequency — phase performs a random walk and the picDIV 1PPS
         * slowly drifts from GPS 1PPS.  Use a PLL algorithm (4,5,7) to
         * bound phase, or re-arm (AP) when drift accumulates.
         * ---------------------------------------------------------------- */
#ifdef GPSDO_PICDIV
        if (xEventGroupGetBits(xSysEvents) & EVT_ARM_PICDIV) {
            bool fix_ok = false;
            if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                fix_ok = gGps.pos_valid;
                xSemaphoreGive(xGpsMutex);
            }
            if (fix_ok && !s_arm_active) {
                digitalWrite(PIN_PICDIV_ARM, LOW);
                s_arm_active     = true;
                s_arm_started_ms = millis();
                xEventGroupClearBits(xSysEvents, EVT_ARM_PICDIV);
                OUT_SERIAL.println("picDIV: armed (output stopped, waiting for 1PPS sync)");
            } else if (!fix_ok && !s_arm_wait_warned) {
                s_arm_wait_warned = true;   /* warn once, keep event bit set */
                OUT_SERIAL.println("picDIV: no GPS fix - arming deferred until fix");
            }
        }
        if (s_arm_active && (millis() - s_arm_started_ms) > PICDIV_ARM_MS) {
            digitalWrite(PIN_PICDIV_ARM, HIGH);
            s_arm_active      = false;
            s_arm_wait_warned = false;
            OUT_SERIAL.println("picDIV: arm released - output syncs on next 1PPS edge");
        }
#else
        /* No picDIV support compiled in — just clear the request */
        if (xEventGroupGetBits(xSysEvents) & EVT_ARM_PICDIV)
            xEventGroupClearBits(xSysEvents, EVT_ARM_PICDIV);
#endif

        /* ---- ADC readings ---- */
        {
            int16_t raw;
            raw = (int16_t)analogRead(PIN_VCTL_ADC);
            int16_t avg_vctl = movavg_update(&s_avg_vctl, raw);
#ifdef GPSDO_VCC
            raw = (int16_t)analogRead(PIN_VCC_DIV2);
            int16_t avg_vcc = movavg_update(&s_avg_vcc, raw);
#else
            int16_t avg_vcc = 0;
#endif
#ifdef GPSDO_VDD
            raw = (int16_t)analogRead(AVREF);
            int16_t avg_vdd = movavg_update(&s_avg_vdd, raw);
#else
            int16_t avg_vdd = 0;
#endif
            if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                gCtrl.avg_vctl_adc = avg_vctl;
                gCtrl.avg_vcc_adc  = avg_vcc;
                gCtrl.avg_vdd_adc  = avg_vdd;
                xSemaphoreGive(xCtrlMutex);
            }
        }

        /* ---- Auto-holdover: engage on fix loss, disengage on fix recovery ----
         * Rules (evaluated every 200 ms loop tick):
         *   Fix lost  → holdover_mode=true,  holdover_auto=true   (engage)
         *   Fix back  → holdover_mode=false, holdover_auto=false  (disengage)
         *               (only if holdover was set automatically; manual HO untouched)
         * prev_pos_valid is a local variable that tracks the last known fix state. */
        {
            static bool prev_pos_valid = false;
            bool cur_fix = false;
            if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                cur_fix = gGps.pos_valid;
                xSemaphoreGive(xGpsMutex);
            }
            if (prev_pos_valid && !cur_fix) {
                /* Fix just lost — engage auto-holdover */
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    gCtrl.holdover_mode = true;
                    gCtrl.holdover_auto = true;
                    xSemaphoreGive(xCtrlMutex);
                }
                OUT_SERIAL.println("GPS fix lost — auto-holdover engaged");
            } else if (!prev_pos_valid && cur_fix) {
                /* Fix gained.  Distinguish the very first fix after boot
                 * (nothing to disengage) from a genuine recovery after a
                 * fix loss (auto-holdover active).                        */
                bool was_auto_ho = false;
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    was_auto_ho = gCtrl.holdover_auto;
                    if (gCtrl.holdover_auto) {
                        gCtrl.holdover_mode = false;
                        gCtrl.holdover_auto = false;
                    }
                    xSemaphoreGive(xCtrlMutex);
                }
                OUT_SERIAL.println(was_auto_ho
                    ? "GPS fix recovered — auto-holdover disengaged"
                    : "GPS fix acquired");
            }
            prev_pos_valid = cur_fix;

            /* ---- Auto timezone (TO A): recompute offset from position.
             * Cheap (runs at 5 Hz, pure arithmetic); writes the int8
             * g_time_offset that all display code already consumes.     */
            if (g_tz_auto && cur_fix) {
                float lat = 0, lon = 0;
                uint8_t  gd = 0, gmo = 0, gh = 0; uint16_t gy = 0;
                bool tvalid = false;
                if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    lat = gGps.lat;  lon = gGps.lon;
                    gd  = gGps.day;  gmo = gGps.month;
                    gy  = gGps.year; gh  = gGps.hours;
                    tvalid = gGps.valid;
                    xSemaphoreGive(xGpsMutex);
                }
                if (tvalid)
                    g_time_offset = tz_auto_offset(lat, lon, gd, gmo, gy, gh);
            }
        }

        /* ---- Frequency averaging ---- */
        {
            bool adjust = false;
            uint32_t pps = 0;
            uint8_t  algo = 0;
            uint16_t old_pwm = 0;
            bool holdover = false;

            if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                gpsdo_calc_averages(&gFreq);
                adjust = gFreq.must_adjust && gFreq.full100;
                pps    = gFreq.ppscount;
                if (adjust) gFreq.must_adjust = false;
                xSemaphoreGive(xFreqMutex);
            }
            if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                algo     = gCtrl.active_algo;
                old_pwm  = gCtrl.pwm_output;
                holdover = gCtrl.holdover_mode;
                xSemaphoreGive(xCtrlMutex);
            }

            if (adjust && !holdover) {
                uint16_t new_pwm = adjustVctlPWM(old_pwm, pps, algo);
                if (new_pwm != old_pwm) {
                    analogWrite(PIN_VCTL_PWM, new_pwm);
                    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        gCtrl.pwm_output = new_pwm;
                        /* trendstr is updated inside adjustVctlPWM via extern */
                        xSemaphoreGive(xCtrlMutex);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));   /* re-check at 5 Hz */
    }
}
