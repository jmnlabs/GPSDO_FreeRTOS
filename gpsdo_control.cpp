/**
 * gpsdo_control.cpp — vControlTask — OCXO control loop
 *
 * Part of GPSDO FreeRTOS v0.91
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
#include "live_store.h"
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
volatile bool     g_warmup_enable    = true;   /* WU 0/1, saved in EEPROM */
volatile bool     g_splash_enable    = true;   /* SPL 0/1, saved in EEPROM */
volatile bool     g_flash_ring_enable = true;   /* FR 0/1, saved in EEPROM */
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
        /* countdown of the WHOLE procedure (set by the caller at start,
         * topped up when adaptive phases add time), not of this segment */
        if (g_calib_remaining > 1) g_calib_remaining--;
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gCtrl.pwm_output   = cur_pwm;
            gCtrl.avg_vctl_adc = movavg_update(&s_avg_vctl,
                                   (int16_t)analogRead(PIN_VCTL_ADC));
            xSemaphoreGive(xCtrlMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Legacy entry point — waits without touching PWM (non-calibration use) */
static void wait_secs(uint16_t n)
{
    while (n--) {
        if (g_calib_remaining > 1) g_calib_remaining--;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---- Calibration routine ---------------------------------------------- */
static void do_calibration(void)
{
    OUT_SERIAL.println("Calibration started");
    g_calib_active = true;
    g_calib_remaining = 2u * OCXO_CALIB_SECS + 5u;   /* real total for displays */

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
    g_calib_remaining = 0;

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
        g_calib_remaining = 0;
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
        g_calib_remaining = 0;
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
    g_calib_remaining = 0;
    OUT_SERIAL.println("CT: done. Review values, then 'ES' to save to EEPROM.");
}

#ifdef GPSDO_LTIC
/* ======================================================================
 * do_ltic_calibrate (LC command) — LTIC self-calibration
 *
 * Forces a small PWM offset so the OCXO runs slightly off-nominal; the phase
 * between GPS-1PPS and OCXO-1PPS then ramps linearly. The ramp RATE is known
 * from the TIM2 frequency error (df), so we never need an external reference:
 *
 *   phase_rate [ns/s] = df / BASE_FREQ * 1e9        (df = avg10 - BASE_FREQ)
 *
 * Within one ramp segment the TIC voltage also moves linearly with time, so a
 * least-squares fit of V(t) gives dV/dt, and:
 *
 *   ns_per_volt = phase_rate / (dV/dt)
 *
 * range_ns comes from the voltage span actually swept (Vmax-Vmin) × ns_per_volt.
 * zero_offset is approximated as the mid-scale voltage (the true phase-zero
 * crossing needs a separate alignment step; mid-scale is a safe starting point
 * the loop can refine). Every result is range-guarded; on any failure the PWM
 * is restored and the LTIC params are left unchanged.
 *
 * Runs in the control task (like CT) so PWM manipulation is safe. The loop
 * itself (algo 10) is still phase A — this only fills the calibration fields.
 * ====================================================================== */
static void do_ltic_calibrate(void)
{
    OUT_SERIAL.println("LC: LTIC self-calibration started");
    g_calib_active = true;

    /* Real total for the display countdown: prep(≤64)+arm(4)+settle(30)
     * +dir-check(8)+df(~10)+sweep+finish(~5); adaptive phases top it up. */
    g_calib_remaining = 64u + 4u + 30u + 8u + 10u + LTIC_CAL_SECS + 5u;  /* rate-measurement iterations add 112 s each */

    /* Remember the current PWM so we can always restore it */
    uint16_t saved_pwm = DEFAULT_PWM_OUTPUT;
    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        saved_pwm = gCtrl.pwm_output;
        xSemaphoreGive(xCtrlMutex);
    }

    /* ---- Prep phase: arm picDIV, then wait for the phase to settle near the
     * middle of the detector window before ramping. This removes the manual
     * "LA 7 / AP / wait for centre" dance the operator used to do by hand:
     * a bad start (phase against a rail) was the main cause of poor cals. ---- */
#ifdef GPSDO_PICDIV
    {
        /* Only arm if we have a GPS fix (picDIV syncs to 1PPS). */
        bool fix_ok = false;
        if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            fix_ok = gGps.pos_valid;
            xSemaphoreGive(xGpsMutex);
        }
        if (fix_ok) {
            OUT_SERIAL.println("LC: arming picDIV (sync to 1PPS)...");
            digitalWrite(PIN_PICDIV_ARM, LOW);          /* stop divider        */
            wait_secs_pwm(2, saved_pwm);                /* hold >1 s, PWM steady */
            digitalWrite(PIN_PICDIV_ARM, HIGH);         /* release: syncs on 1PPS */
            wait_secs_pwm(2, saved_pwm);                /* let output resume   */
        } else {
            OUT_SERIAL.println("LC: no GPS fix — skipping picDIV arm (phase may be unsynced)");
        }
    }
#endif

    /* Wait (up to ~60 s) for the phase voltage to land inside the central band
     * of the detector, so the ramp starts from a clean mid-range point and can
     * sweep both ways before wrapping. The target band is centre ± ¼ range. */
    /* SINGLE-PASS, ZERO-DEAD-TIME sweep. The old sequence burned ~60 s
     * between commanding the ramp and the first sample (settle 30 s + 10 s
     * + a 20 s d1/d2 read) — at the ~9 ns/s that a fixed offset can land on
     * (it adds to whatever df the saved PWM already has), the phase flew
     * through the entire band (0.061→2.62 V) BEFORE sampling began, so the
     * fit saw only saturation. Now: re-arm picDIV (parks the phase at the
     * deterministic bottom), command the offset, and start sampling within
     * ~3 s. The exact rate is read AFTER the pass from clean avg100; if
     * saturation arrives before 10 fit points, halve the offset, re-arm and
     * retry once. */
    uint16_t ramp_pwm = saved_pwm;
    double lsbhz0 = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40) : 3000.0;
    /* commanded fallback offset: NEGATIVE, matching the sweep direction
     * (interval grows when the OCXO runs below 10 MHz) */
    int32_t cmd_off = (int32_t)(-0.06 * lsbhz0);
    bool rate_measured = false;            /* true once avg100 gave the rate */
    /* Fallback rate = the rate actually COMMANDED (cmd_off LSB -> Hz -> ns/s;
     * x100 because 1 Hz at 10 MHz is 100 ns/s). Replaced by the measured
     * value in the 110 s window below when that read is sane. */
    double phase_rate = (double)cmd_off / lsbhz0 * 100.0;
    {
        /* NOTE: the picDIV is NOT armed yet. The rate windows below need only
         * TIM2 (frequency), not the ramp — and every second spent measuring
         * lets the phase walk away from wherever it was. Seen on air: after
         * ~350 s of rate windows the picPPS had overtaken the GPS edge, the
         * interval became ~a full second and the ramp sat saturated at 3.22 V
         * for the whole collection. The arm is done AFTER the rate is set,
         * immediately before sampling, so the transit always starts at ~0. */
        /* --- Establish a KNOWN sweep rate, by measurement, not by faith ---
         *
         * The phase drifts at (f_ocxo − f_gps)/f_gps, so the sweep rate is
         * fixed by the OCXO's offset from 10 MHz at the ramp PWM. Two earlier
         * attempts got this wrong:
         *
         *  - trusting cmd_off/lsb_per_hz: lsb_per_hz comes from the algo-7
         *    PID and is a few percent off, so ns/V (∝ rate) drifted with it;
         *  - nulling a "baseline" read from avg100 before the ramp: avg100 is
         *    an average over the PREVIOUS 100 s, during which the discipline
         *    loop was still moving PWM. That baseline described history, not
         *    the present operating point — it swung ±0.2 Hz between runs and
         *    the nulling overshot by hundreds of LSB (rate came out 26–32
         *    ns/s instead of 4, so the window flew past in ~11 s and the fit
         *    had 5–7 points).
         *
         * Correct approach: hold PWM CONSTANT for a full 100 s window and read
         * avg100. That single number is the current offset, hence the exact
         * phase rate — no baseline, no lsb_per_hz. Then steer PWM to put the
         * rate near the target and verify with a second constant-PWM window.
         * The phase wraps freely during these windows; we ignore it and
         * re-arm picDIV before the sampling pass. */
        /* SLOW sweep. The phase voltage publishes in steps every ~4 s
         * (oversample+median), and a 10 MHz-clocked flip-flop detector has a
         * 100 ns unambiguous window. At the old 4 ns/s a sawtooth tooth
         * lasted only ~25 s ≈ 6 publications — every wrap fell inside a
         * median window, smeared into intermediate values, and the fit
         * averaged across many teeth (observed ns/V: 34 with a clean wrap
         * vs 104/421/441/531 without — random multiples of the truth).
         * 1.5 ns/s stretches one tooth to ~66 s ≈ 16 publications: the fit
         * has real shape and a wrap is a full-height negative step. */
        /* The 1k/1n ramp spans ~0–2000 ns before saturating, and avg100
         * quantises at 1 ns/s — so the sweep must be fast enough that the
         * quantisation is a small fraction of the rate (6 ns/s → ±8 %), yet
         * slow enough that the ~1500 ns usable window takes minutes, not
         * seconds (6 ns/s → ~250 s: fits the collection). */
        /* NEGATIVE target: with the pulse defined GPS-edge → next picPPS-edge,
         * an OCXO slightly BELOW 10 MHz delays each picPPS a bit more, so the
         * interval GROWS — after the arm (phase ≈ 0) the ramp then climbs
         * unambiguously from the bottom. 6 ns/s keeps the avg100 quantisation
         * (1 ns/s) at ±8 % and crosses the ~1.5 µs usable window in ~250 s. */
        const double RATE_TARGET = -6.0;     /* ns/s */
        const double RATE_MIN    = -9.0;     /* acceptance band (negative)   */
        const double RATE_MAX    = -4.0;

        uint16_t meas_pwm = saved_pwm;
        double   r        = 0.0;
        bool     r_ok     = false;

        for (int iter = 0; iter < 3; iter++) {
            OUT_SERIAL.print("LC: measuring phase rate at PWM ");
            OUT_SERIAL.print((int)meas_pwm);
            OUT_SERIAL.println(" (110 s, constant)...");
            g_calib_remaining += 112u;
            analogWrite(PIN_VCTL_PWM, meas_pwm);
            wait_secs_pwm(110, meas_pwm);

            r_ok = false;
            if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                gpsdo_calc_averages(&gFreq);
                if (gFreq.full100) {
                    double dev = gFreq.avg100 - (double)BASE_FREQ;
                    r = dev / (double)BASE_FREQ * 1.0e9;      /* ns/s */
                    r_ok = true;
                }
                xSemaphoreGive(xFreqMutex);
            }
            if (!r_ok) { OUT_SERIAL.println("LC: no 100 s average yet — retrying."); continue; }

            OUT_SERIAL.print("LC: phase rate "); OUT_SERIAL.print(r, 2);
            OUT_SERIAL.println(" ns/s");

            /* good enough to sweep with? */
            if (r >= RATE_MIN && r <= RATE_MAX) break;

            /* steer: Δf needed is (target − r) ns/s → Hz → LSB */
            double need_hz = (RATE_TARGET - r) / 100.0;       /* 1 ns/s = 10 mHz */
            int32_t dpwm   = (int32_t)(need_hz * lsbhz0);
            if (dpwm >  6000) dpwm =  6000;
            if (dpwm < -6000) dpwm = -6000;
            int32_t np = (int32_t)meas_pwm + dpwm;
            if (np > 65535) np = 65535; if (np < 1) np = 1;
            OUT_SERIAL.print("LC: steering PWM by "); OUT_SERIAL.print(dpwm);
            OUT_SERIAL.println(" LSB toward the target sweep rate");
            meas_pwm = (uint16_t)np;
        }

        if (r_ok && r >= RATE_MIN && r <= RATE_MAX) {
            phase_rate    = r;
            rate_measured = true;
            ramp_pwm      = meas_pwm;
        } else {
            /* could not land in the band — fall back to the commanded offset
             * from the saved operating point, and say so plainly */
            int32_t rp6 = (int32_t)saved_pwm + cmd_off;
            if (rp6 > 65535) rp6 = 65535; if (rp6 < 1) rp6 = 1;
            ramp_pwm   = (uint16_t)rp6;
            phase_rate = (double)cmd_off / lsbhz0 * 100.0;
            OUT_SERIAL.print("LC: could not reach a usable rate — using commanded ");
            OUT_SERIAL.print(phase_rate, 2); OUT_SERIAL.println(" ns/s");
        }

        OUT_SERIAL.print("LC: sampling ramp at PWM "); OUT_SERIAL.print((int)ramp_pwm);
        OUT_SERIAL.print(", rate "); OUT_SERIAL.print(phase_rate, 2);
        OUT_SERIAL.println(rate_measured ? " ns/s (measured)" : " ns/s (commanded)");
        analogWrite(PIN_VCTL_PWM, ramp_pwm);
#ifdef GPSDO_PICDIV
        /* ARM LOTTERY. The picDIV sync parks the phase within ~±100 ns of the
         * GPS edge, but on a RANDOM side (edge race at sync). The flip-flop
         * pair only produces a pulse for a POSITIVE interval (GPS→picPPS):
         * land on the wrong side and the ramp reads either the bottom noise
         * (~0.06 V) or, one step later, a full-second pulse (3.28 V rail) —
         * both seen on air. Reversing the sweep does NOT help (it walks
         * further out); re-rolling the dice does. So: arm, look where we
         * landed, and re-arm until the ramp starts inside its live band
         * (~50 % per try; 6 tries ≈ 98 %). The sweep itself stays at the
         * MEASURED −6 ns/s, which grows the interval up the ramp. */
        {
            bool on_ramp = false;
            for (int a = 0; a < 6 && !on_ramp; a++) {
                OUT_SERIAL.print("LC: arming picDIV (try ");
                OUT_SERIAL.print(a + 1); OUT_SERIAL.println(")...");
                digitalWrite(PIN_PICDIV_ARM, LOW);
                wait_secs_pwm(2, ramp_pwm);
                digitalWrite(PIN_PICDIV_ARM, HIGH);
                wait_secs_pwm(6, ramp_pwm);      /* sync + first pulses settle */
                float v0 = g_ltic_voltage;
                OUT_SERIAL.print("LC: post-arm V = "); OUT_SERIAL.println(v0, 3);
                if (v0 > 0.10f && v0 < 2.5f) on_ramp = true;
            }
            if (!on_ramp) {
                OUT_SERIAL.println("LC: ERROR could not land on the ramp after 6 arms — check picARM / pulse path; aborting.");
                analogWrite(PIN_VCTL_PWM, saved_pwm);
                xEventGroupClearBits(xSysEvents, EVT_NEED_LTIC_CAL);
                g_calib_active = false; g_calib_remaining = 0;
                return;
            }
            OUT_SERIAL.println("LC: on the ramp — collecting transit.");
        }
#endif
        /* NO picDIV re-arm here — and that is deliberate. The statistics do
         * not need a deterministic bottom start (that was a line-fit
         * requirement); min/max/wraps come from the phase sawing FREELY, and
         * it was sawing healthily during the rate windows above. On air, a
         * re-arm at this point was followed by the phase creeping at
         * ~0.05 ns/s in a 0.47 V band for the whole 300 s collection while
         * TIM2 said 2 ns/s — the detector was NOT seeing the same signal
         * after the re-arm/sync. Removing the re-arm removes the suspect
         * element and keeps the exact conditions under which the rate was
         * measured. */
        wait_secs_pwm(3, ramp_pwm);
    }
    /* Fallback rate = the rate actually COMMANDED (cmd_off LSB → Hz → ns/s;
     * ×100 because 1 Hz at 10 MHz is 100 ns/s). Tracks the halved offset on
     * a retry pass — the old fixed 4.0 silently doubled the slope scale when
     * the avg100 refinement below could not run. Refined after the pass. */


    /* =====================================================================
     * RAMP-TRANSIT CALIBRATION (architecture confirmed by André's post and
     * DSO captures, EEVblog msg4111609):
     *
     *   The 74HC74 pair emits a PULSE whose width equals the phase interval
     *   between the GPS-PPS edge and the picDIV-PPS edge. That pulse charges
     *   C13 (1 nF) through D2+R8 (1 kΩ, τ≈1 µs) — the scope shows 0→1.75 V
     *   in ~600 ns. Vphase is sampled on the ramp PEAK ~300 µs after each
     *   PPS edge (ltic_read_fast in vFreqRelayTask); the cap self-clears
     *   through leakage (~25 ms) before the next pulse. So Vphase is a RAMP
     *   phase interval: it rises with growing phase and SATURATES — it never
     *   wraps, and its usable range is a property of the RC, not of any
     *   clock period. Both earlier models (line fit; fixed-100 ns sawtooth)
     *   were wrong for this hardware.
     *
     *   With the sweep rate measured (constant-PWM windows above), the whole
     *   calibration is one transit:
     *       span   = Vhigh − Vlow            (observed, trimmed)
     *       t10,t90 = first crossings of 10 % / 90 % of the span
     *       range  = rate × (t90 − t10) / 0.8   [ns]
     *       nsv    = range / span
     *       zero   = Vlow + span/2
     * ================================================================== */
    static float    s_v[300];
    static uint16_t s_t[300];
    uint16_t cnt = 0, railed = 0;
    float v_prev = -1.0f, vlow = 3.3f, vhigh = 0.0f;
    int consec_rail = 0;
    bool saturated = false;

    for (uint16_t t = 1; t <= LTIC_CAL_SECS; t++) {
        wait_secs_pwm(1, ramp_pwm);
        float v = g_ltic_voltage;
        if (v >= 3.28f || v <= 0.02f) {
            railed++;
            /* end-of-transit: sustained rail (either end) after real data */
            if (cnt >= 10 && ++consec_rail >= 8) { saturated = true; break; }
            v_prev = v; continue;
        }
        consec_rail = 0;
        if (v_prev >= 0.0f && fabsf(v - v_prev) <= 0.002f) continue;   /* dedup steps */
        if (cnt < 300) { s_v[cnt] = v; s_t[cnt] = t; cnt++; }
        if (v < vlow)  vlow  = v;
        if (v > vhigh) vhigh = v;
        v_prev = v;
        /* DIAGNOSTIC: print every sample (1/s) so a full exponential-model fit
         * can be done off-line. Dedup still feeds s_v[], so cnt is the
         * fit-array index; this raw stream catches every second including
         * flat ones the dedup skips. Revert to (t % 30u) after the model is
         * verified. */
        OUT_SERIAL.print("LC: t="); OUT_SERIAL.print((int)t);
        OUT_SERIAL.print("s V="); OUT_SERIAL.print(v, 3);
        OUT_SERIAL.print("V n="); OUT_SERIAL.println((int)cnt);
    }
    if (railed > 0) {
        OUT_SERIAL.print("LC: note "); OUT_SERIAL.print((int)railed);
        OUT_SERIAL.println(" railed samples skipped");
    }
    OUT_SERIAL.print("LC: collected "); OUT_SERIAL.print((int)cnt);
    OUT_SERIAL.print(" samples"); OUT_SERIAL.println(saturated ? " (transit complete: top saturation)" : "");

    if (cnt < LTIC_CAL_MIN_POINTS) {
        OUT_SERIAL.println("LC: ERROR too few points — aborting, params unchanged");
        analogWrite(PIN_VCTL_PWM, saved_pwm);
        xEventGroupClearBits(xSysEvents, EVT_NEED_LTIC_CAL);
        g_calib_active = false; g_calib_remaining = 0;
        return;
    }

    double span = (double)(vhigh - vlow);
    /* Direction of the transit is a BUILD property: depending on which PPS
     * starts and which stops the pulse, a rising OCXO frequency can either
     * widen or SHRINK the interval (seen on air: V fell 0.485→0.378 at a
     * positive rate). Detect the trend and take the 10 %/90 % crossings in
     * that direction — the maths is symmetric. */
    bool falling = false;
    if (cnt >= 4) {
        float head = 0, tail = 0; uint16_t q = cnt / 4;
        for (uint16_t i = 0; i < q; i++)          head += s_v[i];
        for (uint16_t i = cnt - q; i < cnt; i++)  tail += s_v[i];
        falling = (tail < head);
    }
    float v10 = falling ? (vhigh - 0.10f * (float)span) : (vlow + 0.10f * (float)span);
    float v90 = falling ? (vhigh - 0.90f * (float)span) : (vlow + 0.90f * (float)span);
    uint16_t t10 = 0, t90 = 0;
    for (uint16_t i = 0; i < cnt; i++) {
        if (falling) {
            if (!t10 && s_v[i] <= v10) t10 = s_t[i];
            if (!t90 && s_v[i] <= v90) { t90 = s_t[i]; break; }
        } else {
            if (!t10 && s_v[i] >= v10) t10 = s_t[i];
            if (!t90 && s_v[i] >= v90) { t90 = s_t[i]; break; }
        }
    }
    if (falling)
        OUT_SERIAL.println("LC: falling transit (interval shrinks as frequency rises) — check LPOL sign");
    double range_ns = 0.0, nsv = 0.0, zero_off = (double)vlow + 0.5 * span;
    bool transit_ok = (t90 > t10) && (t90 - t10) >= 5;
    if (transit_ok && fabs(phase_rate) > 0.1)
        range_ns = fabs(phase_rate) * (double)(t90 - t10) / 0.8;

    /* --- Option D: anchored zero_offset + LOCAL-slope ns/V --------------
     * ns/V from the whole-transit average (range/span) drifts ~20 % run to
     * run because the exponential ramp has no single slope and the arm parks
     * the phase at a random height. The local slope in a narrow band near
     * LTIC_ZERO_ANCHOR_V is repeatable to ~0.3 % (verified on two 1 s LC
     * logs), so we anchor the operating point there and read ns/V from a
     * least-squares dV/dt in a ±LTIC_ANCHOR_WIN_V window: nsv = rate /|dV/dt|.
     * Falls back to the whole-ramp average only if the sweep never crossed
     * the anchor band (too few samples). */
    int anchor_n = 0;
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (uint16_t i = 0; i < cnt; i++) {
            if (fabsf(s_v[i] - LTIC_ZERO_ANCHOR_V) <= LTIC_ANCHOR_WIN_V) {
                double tt = (double)s_t[i], vv = (double)s_v[i];
                sx += tt; sy += vv; sxx += tt * tt; sxy += tt * vv; anchor_n++;
            }
        }
        double denom = (double)anchor_n * sxx - sx * sx;
        if (anchor_n >= 5 && fabs(denom) > 1e-9) {
            double dvdt = ((double)anchor_n * sxy - sx * sy) / denom;  /* V/s */
            if (fabs(dvdt) > 1e-6) {
                nsv      = fabs(phase_rate) / fabs(dvdt);
                zero_off = (double)LTIC_ZERO_ANCHOR_V;                 /* anchored */
            }
        }
    }
    if (nsv <= 0.0 && span > 0.05) {              /* fallback: whole-ramp avg */
        nsv = range_ns / span;
        OUT_SERIAL.println("LC: (anchor band not crossed — using range/span average)");
    }
    OUT_SERIAL.print("LC: transit "); OUT_SERIAL.print((int)t10);
    OUT_SERIAL.print("s -> "); OUT_SERIAL.print((int)t90);
    OUT_SERIAL.print("s at "); OUT_SERIAL.print(phase_rate, 2); OUT_SERIAL.println(" ns/s");

    g_ltic.ns_per_volt = (float)nsv;
    g_ltic.zero_offset = (float)zero_off;
    g_ltic.range_ns    = (float)range_ns;
    g_ltic.centre_v    = 0.0f;
    ltic_autotune();

    OUT_SERIAL.print("LC: ns_per_volt=");  OUT_SERIAL.print(nsv, 3);
    OUT_SERIAL.print("  zero_offset=");    OUT_SERIAL.print(zero_off, 4);
    OUT_SERIAL.print("V  span=");          OUT_SERIAL.print(span, 3);
    OUT_SERIAL.print("V  range_ns=");      OUT_SERIAL.print(range_ns, 1);
    if (anchor_n >= 5) {
        OUT_SERIAL.print("  (local slope, ");
        OUT_SERIAL.print(anchor_n); OUT_SERIAL.println(" pts @ anchor)");
    } else {
        OUT_SERIAL.println("  (range/span average)");
    }

    analogWrite(PIN_VCTL_PWM, saved_pwm);
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }
    xEventGroupClearBits(xSysEvents, EVT_NEED_LTIC_CAL);
    g_calib_active = false; g_calib_remaining = 0;

    {
        bool span_sane = span > 0.30 && span <= 3.4;   /* ramp: expect ~1.5-3 V */
        bool nsv_sane  = nsv > 20.0 && nsv < 5000.0;
        if (span_sane && transit_ok && nsv_sane && rate_measured) {
            live_store_request_save();
            OUT_SERIAL.println("LC: PASSED — clean ramp transit at a measured rate; auto-saved to flash ring.");
        } else {
            OUT_SERIAL.print("LC: WEAK result — ");
            if (!span_sane)     OUT_SERIAL.print("voltage span too small; ");
            if (!transit_ok)    OUT_SERIAL.print("no clean 10-90% transit; ");
            if (!nsv_sane)      OUT_SERIAL.print("slope implausible; ");
            if (!rate_measured) OUT_SERIAL.print("sweep rate was not measured; ");
            OUT_SERIAL.println("check detector/RC, then re-run LC (not auto-saved).");
        }
    }
}
#endif /* GPSDO_LTIC */

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

    /* ---- Warmup — runs whenever enabled (WU 1, default). A valid EEPROM
     * used to skip it silently, but a cold OCXO after power-on drifts
     * thermally for minutes regardless of stored settings; the recalled PWM
     * is already applied, so the oven warms at its proper operating point.
     * Operators who restart often can disable with `WU 0` + `ES`. ---- */
    if (g_warmup_enable)
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

#ifdef GPSDO_LTIC
        if (xEventGroupGetBits(xSysEvents) & EVT_NEED_LTIC_CAL)
            do_ltic_calibrate();
#endif

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

            /* Do NOT run the discipline loop while an LTIC/OCXO calibration
             * is sweeping PWM: LC drives its own known offset and measures
             * the phase response, so a simultaneous loop correction fights
             * the sweep and corrupts the slope/range (seen on air: running LC
             * under LA 10 gave rate ±1 ns/s and absurd range 1502/3518 ns).
             * g_calib_active is set for the whole LC/CT sequence. */
            if (adjust && !holdover && !g_calib_active) {
                uint16_t new_pwm = adjustVctlPWM(old_pwm, pps, algo);
                if (new_pwm != old_pwm) {
                    analogWrite(PIN_VCTL_PWM, new_pwm);
                    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        gCtrl.pwm_output = new_pwm;
                        /* trendstr is updated inside adjustVctlPWM via extern */
                        xSemaphoreGive(xCtrlMutex);
                    }
                }
            } else if (holdover && algo == 9 && !g_calib_active) {
                /* Thermally compensated holdover (algo 9): steer PWM from
                 * the learned tempco. Runs on this 5 Hz path because with
                 * the antenna gone there is no PPS to gate on; the function
                 * itself rate-limits to ~1 Hz and no-ops until a tempco has
                 * been learned. */
                int16_t dp = nn_thermal_holdover_step();
                if (dp != 0) {
                    int32_t np = (int32_t)old_pwm + dp;
                    if (np < 0) np = 0; if (np > 65535) np = 65535;
                    analogWrite(PIN_VCTL_PWM, (uint16_t)np);
                    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        gCtrl.pwm_output = (uint16_t)np;
                        xSemaphoreGive(xCtrlMutex);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));   /* re-check at 5 Hz */

        /* persist learned/calibration data to the flash ring with hysteresis
         * (no-op when the ring is disabled or nothing has changed enough) */
        live_store_tick((uint32_t)millis());
    }
}
