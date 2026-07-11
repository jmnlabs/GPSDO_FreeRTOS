/**
 * gpsdo_state.cpp — Shared state instances and EEPROM helpers
 *
 * Part of GPSDO FreeRTOS v0.91
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
 * EEPROM layout (144 bytes, signature "GPSD2"):
 *   [0..5]     signature (6 B)
 *   [6..7]     pwm_output (uint16_t big-endian)
 *   [8]        active_algo
 *   [9]        g_time_offset
 *   [10..121]  g_pid[3..9]: 7 x {Kp, Ki, Kd, I_LIMIT} as float
 *   [122..133] g_blend_crossover, g_blend_scale, g_nn_max_step
 *   [134..137] g_pressure_offset (float)
 *   [138..141] g_altitude_offset (float)
 *   [142]      g_tz_auto (0 = manual offset, 1 = auto from GPS)
 *   [143]      g_svin_enabled (0 = survey-in off, 1 = on)  (v0.47+)
 *   [144..207] Algorithm 10 (LTIC) params, same signature, NaN/0xFF-guarded
 *              (v0.52+): ns_per_volt, zero_offset, range_ns, DPLL PID, LOCK
 *              PID, ACQ/DPLL thresholds, lock interval, state, submode +
 *              reserve. See the EE_LTIC_* defines below.
 */
#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include "ubx_timtp.h"
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

/* PID parameters for algos 3-9 (v0.47+):
 *   7 algos × 4 floats (Kp,Ki,Kd,I_LIMIT) × 4 bytes = 112 bytes [10..121]
 *   Blend/NN params: 3 floats × 4 bytes = 12 bytes [122..133] */
#define EE_PID_START    10u  /* g_pid[3..9] starts here */
#define EE_BLEND_CROSS  122u /* g_blend_crossover as float */
#define EE_BLEND_SCALE  126u /* g_blend_scale as float */
#define EE_NN_MAX_STEP  130u /* g_nn_max_step as float */
#define EE_PRESS_OFF    134u /* g_pressure_offset as float */
#define EE_ALT_OFF      138u /* g_altitude_offset as float */
#define EE_TZ_AUTO      142u /* g_tz_auto (0=manual, 1=auto)  (v0.47+) */
#define EE_SVIN_EN      143u /* g_svin_enabled (0=off, 1=on)  (v0.47+) */
/* Original block total: 144 bytes [0..143] */

/* ---- Algorithm 10 (LTIC) parameters — reserved block [144..207] (v0.52+) ----
 * Backward-compatible: kept under the same "GPSD2" signature. Fields written
 * by older firmware do not exist, so on an EEPROM saved before this block was
 * added these bytes are fresh flash (0xFF). The recall path guards every
 * field: float 0xFFFFFFFF reads as NaN → replaced by the compile-time default;
 * the uint8/uint16 fields treat 0xFF / 0xFFFF as "unset" → default. So old
 * saves load cleanly and the LTIC params come up at their defaults until the
 * user sets and saves them. */
#define EE_LTIC_NSV     144u /* float  ns_per_volt        */
#define EE_LTIC_ZERO    148u /* float  zero_offset [V]    */
#define EE_LTIC_RANGE   152u /* float  range_ns           */
#define EE_LTIC_DPLL_KP 156u /* float  dpll.Kp            */
#define EE_LTIC_DPLL_KI 160u /* float  dpll.Ki            */
#define EE_LTIC_DPLL_KD 164u /* float  dpll.Kd            */
#define EE_LTIC_DPLL_IL 168u /* float  dpll.I_LIMIT       */
#define EE_LTIC_LOCK_KP 172u /* float  lock.Kp            */
#define EE_LTIC_LOCK_KI 176u /* float  lock.Ki            */
#define EE_LTIC_LOCK_KD 180u /* float  lock.Kd            */
#define EE_LTIC_LOCK_IL 184u /* float  lock.I_LIMIT       */
#define EE_LTIC_ACQ_TH  188u /* float  acq_threshold_ns   */
#define EE_LTIC_DPLL_TH 192u /* float  dpll_lock_thresh   */
#define EE_LTIC_LOCK_IV 196u /* uint16 lock_interval_s    */
#define EE_LTIC_STATE   198u /* uint8  state              */
#define EE_LTIC_SUBMODE 199u /* uint8  submode            */
#define EE_LTIC_ACQ_KP  200u /* float  acq.Kp             (v0.55+) */
#define EE_LTIC_ACQ_KI  204u /* float  acq.Ki             */
#define EE_LTIC_ACQ_KD  208u /* float  acq.Kd             */
#define EE_LTIC_ACQ_IL  212u /* float  acq.I_LIMIT        */
#define EE_LTIC_POLARITY 216u /* int8   polarity (0=auto)  (v0.60+) */
#define EE_LTIC_CENTRE_V 217u /* float  centre_v [V]        */
#define EE_WARMUP_EN     221u /* uint8  warmup enable (WU)  */
#define EE_LRN_EN        222u /* uint8  learn enable (LRN)  */
#define EE_LRN_DRIFT     223u /* float  learned feed-fwd    */
#define EE_LRN_DAMP      227u /* float  damping multiplier  */
#define EE_SPLASH_EN     231u /* uint8  splash animation on */
#define EE_FLASH_RING_EN 232u /* uint8  flash ring enable   */
#define EE_SAW_EN        233u /* uint8  sawtooth qErr correction */
/* [221..223] reserved for future LTIC params */
/* Total used: 221 bytes, reserved to 224 (well within 1024-byte page) */


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

    /* Write data. PWM is a live value too, but harmless to keep here: at boot
     * eeprom_recall() runs BEFORE live_store_begin(), so when the ring is on
     * it overrides this with the freshest PWM; when the ring is off this is
     * the operating-point fallback. algo/time_offset are plain settings. */
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
    eeprom_buffered_write_byte(EE_TZ_AUTO, g_tz_auto ? 1u : 0u);
    eeprom_buffered_write_byte(EE_SVIN_EN, g_svin_enabled ? 1u : 0u);

    /* Algorithm 10 (LTIC) SETTINGS (always saved: these are user-tuned, not
     * auto-learned). The three CALIBRATION values (ns_per_volt, zero_offset,
     * range_ns) and centre_v are "live data" — normally owned by the flash
     * ring buffer, so ES only writes them as a FALLBACK when the ring is
     * disabled (FR 0). With the ring on, ES never overwrites calibration. */
    if (!g_flash_ring_enable) {
        ee_write_float(EE_LTIC_NSV,   g_ltic.ns_per_volt);
        ee_write_float(EE_LTIC_ZERO,  g_ltic.zero_offset);
        ee_write_float(EE_LTIC_RANGE, g_ltic.range_ns);
    }
    ee_write_float(EE_LTIC_DPLL_KP, (float)g_ltic.dpll.Kp);
    ee_write_float(EE_LTIC_DPLL_KI, (float)g_ltic.dpll.Ki);
    ee_write_float(EE_LTIC_DPLL_KD, (float)g_ltic.dpll.Kd);
    ee_write_float(EE_LTIC_DPLL_IL, (float)g_ltic.dpll.I_LIMIT);
    ee_write_float(EE_LTIC_LOCK_KP, (float)g_ltic.lock.Kp);
    ee_write_float(EE_LTIC_LOCK_KI, (float)g_ltic.lock.Ki);
    ee_write_float(EE_LTIC_LOCK_KD, (float)g_ltic.lock.Kd);
    ee_write_float(EE_LTIC_LOCK_IL, (float)g_ltic.lock.I_LIMIT);
    ee_write_float(EE_LTIC_ACQ_TH,  g_ltic.acq_threshold_ns);
    ee_write_float(EE_LTIC_DPLL_TH, g_ltic.dpll_lock_thresh);
    eeprom_buffered_write_byte(EE_LTIC_LOCK_IV,     (uint8_t)(g_ltic.lock_interval_s >> 8));
    eeprom_buffered_write_byte(EE_LTIC_LOCK_IV + 1, (uint8_t)(g_ltic.lock_interval_s & 0xFFu));
    eeprom_buffered_write_byte(EE_LTIC_STATE,   g_ltic.state);
    eeprom_buffered_write_byte(EE_LTIC_SUBMODE, g_ltic.submode);
    ee_write_float(EE_LTIC_ACQ_KP, (float)g_ltic.acq.Kp);
    ee_write_float(EE_LTIC_ACQ_KI, (float)g_ltic.acq.Ki);
    ee_write_float(EE_LTIC_ACQ_KD, (float)g_ltic.acq.Kd);
    ee_write_float(EE_LTIC_ACQ_IL, (float)g_ltic.acq.I_LIMIT);
    eeprom_buffered_write_byte(EE_LTIC_POLARITY, (uint8_t)g_ltic.polarity);
    if (!g_flash_ring_enable)                        /* centre_v is live data */
        ee_write_float(EE_LTIC_CENTRE_V, g_ltic.centre_v);
    eeprom_buffered_write_byte(EE_WARMUP_EN, g_warmup_enable ? 1u : 0u);
    eeprom_buffered_write_byte(EE_SPLASH_EN, g_splash_enable ? 1u : 0u);
    eeprom_buffered_write_byte(EE_FLASH_RING_EN, g_flash_ring_enable ? 1u : 0u);
    eeprom_buffered_write_byte(EE_SAW_EN, g_qerr_enable ? 1u : 0u);
    eeprom_buffered_write_byte(EE_LRN_EN, g_lrn_enable ? 1u : 0u);
    if (!g_flash_ring_enable) {                       /* LRN values are live data */
        ee_write_float(EE_LRN_DRIFT, g_lrn_drift);
        ee_write_float(EE_LRN_DAMP,  g_lrn_damp);
    }

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
    if (algo > 10u)                 algo = 0u;   /* 10 = LTIC three-stage */
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
    {
        /* tz_auto: only 0/1 are valid; 0xFF (fresh flash) → manual */
        uint8_t tz = eeprom_buffered_read_byte(EE_TZ_AUTO);
        g_tz_auto = (tz == 1u);
    }
    {
        /* svin_enabled: 0xFF (fresh flash) → default ON, so timing modules
         * still survey-in out of the box; explicit 0 disables it. */
        uint8_t sv = eeprom_buffered_read_byte(EE_SVIN_EN);
        g_svin_enabled = (sv != 0u);   /* 1 or 0xFF → on; 0 → off */
    }

    /* Algorithm 10 (LTIC) parameters — every field guarded so an EEPROM saved
     * before this block existed (fresh-flash 0xFF / NaN) loads as the default.
     * Calibration fields (NSV/ZERO/RANGE) accept 0 (= uncalibrated). */
    {
        float v;
        v = ee_read_float(EE_LTIC_NSV);     if (isfinite(v) && v >= 0.0f && v <= 1.0e6f)  g_ltic.ns_per_volt = v;
        v = ee_read_float(EE_LTIC_ZERO);    if (isfinite(v) && v >= 0.0f && v <= 3.3f)    g_ltic.zero_offset = v;
        v = ee_read_float(EE_LTIC_RANGE);   if (isfinite(v) && v >= 0.0f && v <= 1.0e9f)  g_ltic.range_ns = v;
        v = ee_read_float(EE_LTIC_DPLL_KP); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.dpll.Kp = v;
        v = ee_read_float(EE_LTIC_DPLL_KI); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.dpll.Ki = v;
        v = ee_read_float(EE_LTIC_DPLL_KD); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.dpll.Kd = v;
        v = ee_read_float(EE_LTIC_DPLL_IL); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.dpll.I_LIMIT = v;
        v = ee_read_float(EE_LTIC_LOCK_KP); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.lock.Kp = v;
        v = ee_read_float(EE_LTIC_LOCK_KI); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.lock.Ki = v;
        v = ee_read_float(EE_LTIC_LOCK_KD); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.lock.Kd = v;
        v = ee_read_float(EE_LTIC_LOCK_IL); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.lock.I_LIMIT = v;
        v = ee_read_float(EE_LTIC_ACQ_TH);  if (isfinite(v) && v > 0.0f && v <= 1.0e9f)    g_ltic.acq_threshold_ns = v;
        v = ee_read_float(EE_LTIC_DPLL_TH); if (isfinite(v) && v > 0.0f && v <= 1.0f)      g_ltic.dpll_lock_thresh = v;
        uint16_t iv = ((uint16_t)eeprom_buffered_read_byte(EE_LTIC_LOCK_IV) << 8)
                    |             eeprom_buffered_read_byte(EE_LTIC_LOCK_IV + 1);
        if (iv != 0u && iv != 0xFFFFu) g_ltic.lock_interval_s = iv;
        uint8_t st = eeprom_buffered_read_byte(EE_LTIC_STATE);
        if (st <= LTIC_LOCK) g_ltic.state = st;        /* 0xFF → keep default ACQ */
        uint8_t sm = eeprom_buffered_read_byte(EE_LTIC_SUBMODE);
        if (sm <= 1u) g_ltic.submode = sm;             /* 0xFF → keep default 0   */
        v = ee_read_float(EE_LTIC_ACQ_KP); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.acq.Kp = v;
        v = ee_read_float(EE_LTIC_ACQ_KI); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.acq.Ki = v;
        v = ee_read_float(EE_LTIC_ACQ_KD); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.acq.Kd = v;
        v = ee_read_float(EE_LTIC_ACQ_IL); if (isfinite(v) && v >= 0.0f && v <= 100000.0f) g_ltic.acq.I_LIMIT = v;
        /* polarity: stored as int8. 0xFF (fresh flash) or 0 → auto; +1/-1 forced. */
        {
            int8_t sp = (int8_t)eeprom_buffered_read_byte(EE_LTIC_POLARITY);
            g_ltic.polarity = (sp == 1 || sp == -1) ? sp : 0;   /* else auto */
        }
        v = ee_read_float(EE_LTIC_CENTRE_V); if (isfinite(v) && v >= 0.0f && v <= 3.3f) g_ltic.centre_v = v;
        { uint8_t wu = eeprom_buffered_read_byte(EE_WARMUP_EN);
          g_warmup_enable = (wu == 0u) ? false : true; }   /* 0xFF/1 → on (default) */
        { uint8_t sp = eeprom_buffered_read_byte(EE_SPLASH_EN);
          g_splash_enable = (sp == 0u) ? false : true; }   /* 0xFF/1 → on (default) */
        { uint8_t fr = eeprom_buffered_read_byte(EE_FLASH_RING_EN);
          g_flash_ring_enable = (fr == 0u) ? false : true; }   /* 0xFF/1 → on (default) */
        { uint8_t sw = eeprom_buffered_read_byte(EE_SAW_EN);
          g_qerr_enable = (sw == 1u);   /* default OFF: blank EEPROM (0xFF) → off */ }
        { uint8_t le = eeprom_buffered_read_byte(EE_LRN_EN);
          g_lrn_enable = (le == 0u) ? false : true; }       /* 0xFF/1 → on (default) */
        v = ee_read_float(EE_LRN_DRIFT); if (isfinite(v) && v > -400.0f && v < 400.0f) g_lrn_drift = v;
        v = ee_read_float(EE_LRN_DAMP);  if (isfinite(v) && v >= 0.5f && v <= 1.5f)   g_lrn_damp  = v;
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
