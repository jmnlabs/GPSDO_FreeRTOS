/**
 * gpsdo_state.cpp — Shared state instances and EEPROM helpers
 *
 * Part of GPSDO FreeRTOS v0.29
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Instantiates all global state structs and FreeRTOS synchronisation
 * primitives.  Provides eeprom_save(), eeprom_recall() and eeprom_erase()
 * for persistent storage of PWM, algorithm, time offset and PID parameters.
 *
 * EEPROM layout (134 bytes, signature "GPSD2"):
 *   [0..5]     signature (6 B)
 *   [6..7]     pwm_output (uint16_t big-endian)
 *   [8]        active_algo
 *   [9]        g_time_offset
 *   [10..121]  g_pid[3..9]: 7 x {Kp, Ki, Kd, I_LIMIT} as float
 *   [122..133] g_blend_crossover, g_blend_scale, g_nn_max_step
 *   [134..137] g_pressure_offset (float)
 *   [138..141] g_altitude_offset (float)
 */
#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include "GPSDO_algorithms.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

/* ---- RTOS handles ----------------------------------------------------- */
SemaphoreHandle_t xFreqMutex;
SemaphoreHandle_t xGpsMutex;
SemaphoreHandle_t xCtrlMutex;
SemaphoreHandle_t xUptimeMutex;
SemaphoreHandle_t xSerialMutex;
SemaphoreHandle_t xWireMutex;
SemaphoreHandle_t xTwoHzSemaphore;

QueueHandle_t     xPpsQueue;
QueueHandle_t     xCmdQueue;

EventGroupHandle_t xSysEvents;

TaskHandle_t xFreqRelayTask = NULL;
TaskHandle_t xControlTask   = NULL;
TaskHandle_t xGpsTask       = NULL;
TaskHandle_t xCliTask       = NULL;
TaskHandle_t xSensorTask    = NULL;
TaskHandle_t xDisplayTask   = NULL;
TaskHandle_t xUptimeTask    = NULL;

/* ---- Shared data ------------------------------------------------------ */
FreqData_t  gFreq;
FreqSnap_t  gFreqSnap;
GpsData_t   gGps;
CtrlData_t  gCtrl;
Uptime_t    gUptime;
bool        g_eeprom_valid = false;

/* ---- EEPROM helpers ---------------------------------------------------- */
#ifdef GPSDO_EEPROM
#include <EEPROM.h>

/* Signature written at bytes [0..5] — changed to "GPSD2" to force
 * force re-initialisation after EEPROM layout changes. */
static const char EEPROM_SIG[6] = "GPSD2";

/* Byte addresses — original fields */
#define EE_SIG_START    0u   /* [0..5]  6 bytes */
#define EE_PWM_HI       6u   /* [6]     MSB of pwm_output */
#define EE_PWM_LO       7u   /* [7]     LSB of pwm_output */
#define EE_ALGO         8u   /* [8]     active_algo (0..9) */
#define EE_TIME_OFFSET  9u   /* [9]     g_time_offset as uint8_t */

/* PID parameters for algos 3-9 (v0.25+):
 *   7 algos × 4 floats (Kp,Ki,Kd,I_LIMIT) × 4 bytes = 112 bytes [10..121]
 *   Blend/NN params: 3 floats × 4 bytes = 12 bytes [122..133] */
#define EE_PID_START    10u  /* g_pid[3..9] starts here */
#define EE_BLEND_CROSS  122u /* g_blend_crossover as float */
#define EE_BLEND_SCALE  126u /* g_blend_scale as float */
#define EE_NN_MAX_STEP  130u /* g_nn_max_step as float */
#define EE_PRESS_OFF    134u /* g_pressure_offset as float */
#define EE_ALT_OFF      138u /* g_altitude_offset as float */
/* Total used: 142 bytes (well within 1024-byte EEPROM page) */

/* ---- float ↔ EEPROM byte helpers ---- */
static void ee_write_float(uint16_t addr, float val)
{
    union { float f; uint8_t b[4]; } u;
    u.f = val;
    for (int i = 0; i < 4; i++)
        eeprom_buffered_write_byte(addr + i, u.b[i]);
}

static float ee_read_float(uint16_t addr)
{
    union { float f; uint8_t b[4]; } u;
    for (int i = 0; i < 4; i++)
        u.b[i] = eeprom_buffered_read_byte(addr + i);
    return u.f;
}

/*
 * g_time_offset is defined in gpsdo_cli.cpp.
 * Declaring extern here allows eeprom_save / eeprom_recall to access it
 * without moving its definition.
 */
extern int8_t g_time_offset;
extern float  g_pressure_offset;
extern float  g_altitude_offset;

/* ---- eeprom_save -------------------------------------------------------
 * Saves current PWM, active algorithm and time offset to EEPROM.
 * Writes signature first, then data bytes, then flushes the buffer.
 * Called by CLI command "ES" under xCtrlMutex context.
 * ----------------------------------------------------------------------- */
void eeprom_save(void)
{
    /* Read live values under mutex */
    uint16_t pwm  = DEFAULT_PWM_OUTPUT;
    uint8_t  algo = 0;
    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pwm  = gCtrl.pwm_output;
        algo = gCtrl.active_algo;
        xSemaphoreGive(xCtrlMutex);
    }
    int8_t toffs = g_time_offset;   /* atomic read (single byte) */

    /* Write signature */
    for (int i = 0; i < 6; i++)
        eeprom_buffered_write_byte((uint16_t)(EE_SIG_START + i),
                                   (uint8_t)EEPROM_SIG[i]);

    /* Write data */
    eeprom_buffered_write_byte(EE_PWM_HI,      (uint8_t)(pwm >> 8));
    eeprom_buffered_write_byte(EE_PWM_LO,      (uint8_t)(pwm & 0xFFu));
    eeprom_buffered_write_byte(EE_ALGO,        algo);
    eeprom_buffered_write_byte(EE_TIME_OFFSET, (uint8_t)toffs);   /* cast preserves bit pattern */

    /* Write PID parameters for algos 3-9 */
    for (int n = 3; n <= 9; n++) {
        uint16_t base = EE_PID_START + (uint16_t)(n - 3) * 16u;
        ee_write_float(base +  0, (float)g_pid[n].Kp);
        ee_write_float(base +  4, (float)g_pid[n].Ki);
        ee_write_float(base +  8, (float)g_pid[n].Kd);
        ee_write_float(base + 12, (float)g_pid[n].I_LIMIT);
    }
    ee_write_float(EE_BLEND_CROSS, (float)g_blend_crossover);
    ee_write_float(EE_BLEND_SCALE, (float)g_blend_scale);
    ee_write_float(EE_NN_MAX_STEP, (float)g_nn_max_step);
    ee_write_float(EE_PRESS_OFF,   g_pressure_offset);
    ee_write_float(EE_ALT_OFF,     g_altitude_offset);

    eeprom_buffer_flush();
    g_eeprom_valid = true;

    /* Flush frequency ring buffers so control task sees clean state */
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }

    OUT_SERIAL.print("EEPROM save: PWM=");   OUT_SERIAL.print(pwm);
    OUT_SERIAL.print(" algo=");              OUT_SERIAL.print(algo);
    OUT_SERIAL.print(" toffs=");             OUT_SERIAL.print((int)toffs);
    OUT_SERIAL.println(" + PID params");
}

/* ---- eeprom_recall -----------------------------------------------------
 * Reads PWM, algorithm and time offset from EEPROM.
 * Validates signature; applies range guards for backward compatibility.
 * Safe to call both before and after vTaskStartScheduler:
 *   - Before scheduler: mutex takes skipped (scheduler not running)
 *   - After  scheduler: full mutex protection
 * ----------------------------------------------------------------------- */
void eeprom_recall(void)
{
    eeprom_buffer_fill();

    /* Verify signature */
    bool ok = true;
    for (int i = 0; i < 6; i++) {
        if (eeprom_buffered_read_byte((uint16_t)(EE_SIG_START + i))
                != (uint8_t)EEPROM_SIG[i]) {
            ok = false;
            break;
        }
    }

    if (!ok) {
        g_eeprom_valid = false;
        OUT_SERIAL.println("EEPROM: invalid signature — using defaults");
        return;
    }

    /* Read raw bytes */
    uint16_t pwm  = ((uint16_t)eeprom_buffered_read_byte(EE_PWM_HI)  << 8)
                  |            eeprom_buffered_read_byte(EE_PWM_LO);
    uint8_t  algo = eeprom_buffered_read_byte(EE_ALGO);
    int8_t   toffs = (int8_t)eeprom_buffered_read_byte(EE_TIME_OFFSET);

    /* Range guards — protect against old/garbage bytes in [8..9] */
    if (pwm  == 0)                  pwm  = DEFAULT_PWM_OUTPUT;
    if (algo > 9u)                  algo = 0u;
    if (toffs < -23 || toffs > 23)  toffs = 0;

    /* Read PID parameters for algos 3-9 */
    for (int n = 3; n <= 9; n++) {
        uint16_t base = EE_PID_START + (uint16_t)(n - 3) * 16u;
        float kp = ee_read_float(base +  0);
        float ki = ee_read_float(base +  4);
        float kd = ee_read_float(base +  8);
        float il = ee_read_float(base + 12);
        /* Range guard: reject NaN/Inf and absurd values */
        if (isfinite(kp) && kp >= 0.0f && kp <= 100000.0f) g_pid[n].Kp = (double)kp;
        if (isfinite(ki) && ki >= 0.0f && ki <= 100000.0f) g_pid[n].Ki = (double)ki;
        if (isfinite(kd) && kd >= 0.0f && kd <= 100000.0f) g_pid[n].Kd = (double)kd;
        if (isfinite(il) && il >= 100.0f && il <= 100000.0f) g_pid[n].I_LIMIT = (double)il;
    }
    {
        float bc = ee_read_float(EE_BLEND_CROSS);
        float bs = ee_read_float(EE_BLEND_SCALE);
        float ns = ee_read_float(EE_NN_MAX_STEP);
        if (isfinite(bc) && bc > 0.0f && bc < 1.0f)     g_blend_crossover = (double)bc;
        if (isfinite(bs) && bs > 0.0f && bs < 1.0f)     g_blend_scale     = (double)bs;
        if (isfinite(ns) && ns >= 1.0f && ns <= 10000.0f) g_nn_max_step    = (double)ns;
    }
    {
        float po = ee_read_float(EE_PRESS_OFF);
        float ao = ee_read_float(EE_ALT_OFF);
        if (isfinite(po) && po >= -500.0f && po <= 5000.0f) g_pressure_offset = po;
        if (isfinite(ao) && ao >= -500.0f && ao <= 10000.0f) g_altitude_offset = ao;
    }

    /* Apply PWM immediately to DAC */
    analogWrite(PIN_VCTL_PWM, pwm);

    /* Update gCtrl under mutex (skip mutex if scheduler not started) */
    bool sched = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    bool got_mutex = true;
    if (sched)
        got_mutex = (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE);

    if (got_mutex) {
        gCtrl.pwm_output  = pwm;
        gCtrl.active_algo = algo;
        if (sched) xSemaphoreGive(xCtrlMutex);
    }

    /* g_time_offset is a single byte — safe to write directly */
    g_time_offset = toffs;

    g_eeprom_valid = true;

    OUT_SERIAL.print("EEPROM recall: PWM=");  OUT_SERIAL.print(pwm);
    OUT_SERIAL.print(" algo=");               OUT_SERIAL.print(algo);
    OUT_SERIAL.print(" toffs=");              OUT_SERIAL.print((int)toffs);
    OUT_SERIAL.println(" + PID params");
}

/* ---- eeprom_erase ------------------------------------------------------
 * Overwrites entire emulated EEPROM page with a pattern that does NOT
 * match the signature so the next boot uses compile-time defaults.
 * ----------------------------------------------------------------------- */
void eeprom_erase(void)
{
    for (uint16_t i = 0; i < 1024u; i++)
        eeprom_buffered_write_byte(i, (uint8_t)(i & 0xFFu));
    eeprom_buffer_flush();
    g_eeprom_valid = false;

    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }

    OUT_SERIAL.println("EEPROM erased — defaults will be used on next boot");
}

/* ---- eeprom_check_on_boot ----------------------------------------------
 * Non-destructive signature check. Called from setup() before the
 * RTOS scheduler starts. Returns true if valid data is present.
 * ----------------------------------------------------------------------- */
bool eeprom_check_on_boot(void)
{
    eeprom_buffer_fill();
    for (int i = 0; i < 6; i++)
        if (eeprom_buffered_read_byte((uint16_t)(EE_SIG_START + i))
                != (uint8_t)EEPROM_SIG[i]) return false;
    return true;
}

#else  /* GPSDO_EEPROM not defined — stub implementations */
void eeprom_save(void)          {}
void eeprom_recall(void)        {}
void eeprom_erase(void)         {}
bool eeprom_check_on_boot(void) { return false; }
#endif /* GPSDO_EEPROM */
