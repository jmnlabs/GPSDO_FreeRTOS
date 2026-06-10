/**
 * gpsdo_control.cpp — vControlTask — OCXO control loop
 *
 * Part of GPSDO FreeRTOS v0.29
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

/* ---- Wait N seconds, yielding to RTOS --------------------------------- */
static void wait_secs(uint16_t n)
{
    while (n--) vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ---- Calibration routine ---------------------------------------------- */
static void do_calibration(void)
{
    OUT_SERIAL.println("Calibration started");

    /* Set Vctl=1.5V */
    analogWrite(PIN_VCTL_PWM, 30720);
    OUT_SERIAL.print("Vctl=1.5V, waiting "); OUT_SERIAL.print(OCXO_CALIB_SECS); OUT_SERIAL.println("s");
    wait_secs(OCXO_CALIB_SECS);

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
    wait_secs(OCXO_CALIB_SECS);

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

    /* Do NOT auto-arm the picDIV here: the discipline loop has not yet
     * converged after calibration, so the OCXO frequency still carries an
     * error and the picDIV phase would immediately start drifting from
     * GPS.  Arm manually (AP command) once the loop reports lock.        */
    OUT_SERIAL.println("Calibration done");
#ifdef GPSDO_PICDIV
    OUT_SERIAL.println("Tip: arm picDIV (AP) after the loop locks (trend 'hit')");
#endif
}

/* ---- OCXO warmup ------------------------------------------------------ */
static void do_warmup(void)
{
    OUT_SERIAL.print("OCXO warming up, "); OUT_SERIAL.print(OCXO_WARMUP_SECS); OUT_SERIAL.println("s");
    for (uint16_t i = OCXO_WARMUP_SECS; i > 0; i--) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if ((i % 30) == 0) {
            OUT_SERIAL.print(i); OUT_SERIAL.println("s remaining");
        }
    }
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
                /* Fix just recovered — disengage auto-holdover only */
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    if (gCtrl.holdover_auto) {
                        gCtrl.holdover_mode = false;
                        gCtrl.holdover_auto = false;
                    }
                    xSemaphoreGive(xCtrlMutex);
                }
                OUT_SERIAL.println("GPS fix recovered — auto-holdover disengaged");
            }
            prev_pos_valid = cur_fix;
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
