/* ======================================================================
 * live_store.cpp  —  live-data persistence over the flash ring buffer
 * Part of GPSDO FreeRTOS v0.95
 * See live_store.h for the design and payload layout.
 * ====================================================================== */

#include "live_store.h"
#include "flash_ring.h"
#include "gpsdo_state.h"
#include "GPSDO_algorithms.h"
#include <string.h>
#include <math.h>

/* hysteresis thresholds (see header) */
#define LS_DRIFT_LSB   8.0f
#define LS_DAMP_DELTA  0.03f
#define LS_MIN_MS      (20u * 60u * 1000u)   /* 20 minutes */

#define LS_PAYLOAD     27u

/* last-saved values, to measure change since the previous write */
static float    s_last_drift = 0.0f;
static float    s_last_damp  = 1.0f;
static uint32_t s_last_ms    = 0;
static bool     s_have_last  = false;
static bool     s_force_save = false;

/* ---- little-endian pack/unpack helpers ---- */

static void ls_put_f(uint8_t *p, float v)  { memcpy(p, &v, 4); }
static void ls_put_u16(uint8_t *p, uint16_t v){ p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static float    ls_get_f(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static uint16_t ls_get_u16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }

static void ls_pack(uint8_t *buf)
{
    float drift, damp, nsv, zero, range, centre;
    uint16_t pwm; uint8_t algo;

    drift = g_lrn_drift; damp = g_lrn_damp;
    nsv   = g_ltic.ns_per_volt; zero = g_ltic.zero_offset;
    range = g_ltic.range_ns;    centre = g_ltic.centre_v;

    pwm = DEFAULT_PWM_OUTPUT; algo = 0;
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        pwm  = gCtrl.pwm_output;          /* boot path: single-threaded */
        algo = gCtrl.active_algo;
    } else if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        pwm  = gCtrl.pwm_output;
        algo = gCtrl.active_algo;
        xSemaphoreGive(xCtrlMutex);
    }

    ls_put_f  (buf +  0, drift);
    ls_put_f  (buf +  4, damp);
    ls_put_f  (buf +  8, nsv);
    ls_put_f  (buf + 12, zero);
    ls_put_f  (buf + 16, range);
    ls_put_u16(buf + 20, pwm);
    ls_put_f  (buf + 22, centre);
    buf[26] = algo;
}

static void ls_apply(const uint8_t *buf)
{
    /* learned values */
    g_lrn_drift = ls_get_f(buf +  0);
    g_lrn_damp  = ls_get_f(buf +  4);
    /* The damping floor can change between firmware versions (it was raised
     * 0.30 → 0.45 when a too-hard floor was found to starve the loop and drop
     * lock). Flash written by an older build can therefore hold a damp below
     * the current floor, and since damp only adapts on limit-cycle crossings
     * it would otherwise stay stuck there for a long time. Clamp the restored
     * value into the current legal band so a reflash takes effect immediately. */
    if (g_lrn_damp < LRN_DAMP_LO) g_lrn_damp = LRN_DAMP_LO;
    if (g_lrn_damp > LRN_DAMP_HI) g_lrn_damp = LRN_DAMP_HI;
    /* LC calibration */
    g_ltic.ns_per_volt = ls_get_f(buf +  8);
    g_ltic.zero_offset = ls_get_f(buf + 12);
    g_ltic.range_ns    = ls_get_f(buf + 16);
    g_ltic.centre_v    = ls_get_f(buf + 22);
    /* PWM is applied: it is genuinely live (freshest operating point) and
     * self-correcting even if slightly stale. The algo byte is NOT applied:
     * the active algorithm is a SETTING — the user's explicit choice, saved
     * by ES and owned by the EEPROM — and a stale ring slot must never
     * override it (bug: LA 9 + ES + reset came back as algo 7, restored
     * from a slot written while 7 was still running). The byte stays in the
     * payload purely as provenance: which algorithm produced these values. */
    uint16_t pwm  = ls_get_u16(buf + 20);
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        gCtrl.pwm_output  = pwm;
    } else if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gCtrl.pwm_output  = pwm;
        xSemaphoreGive(xCtrlMutex);
    }

    s_last_drift = g_lrn_drift;
    s_last_damp  = g_lrn_damp;
    s_have_last  = true;
}

bool live_store_begin(void)
{
    uint8_t buf[LS_PAYLOAD];
    uint16_t n = flash_ring_read(buf, sizeof(buf));
    if (n < LS_PAYLOAD) return false;      /* nothing stored / ring disabled */
    ls_apply(buf);
    return true;
}

static bool ls_should_save(uint32_t now_ms)
{
    if (s_force_save) return true;
    if (!s_have_last) {
        /* first save allowed once we have any learned value away from theory */
        return (fabsf(g_lrn_drift) > LS_DRIFT_LSB) ||
               (fabsf(g_lrn_damp - 1.0f) > LS_DAMP_DELTA);
    }
    /* time gate first (cheap), then magnitude gate */
    if ((uint32_t)(now_ms - s_last_ms) < LS_MIN_MS) return false;
    if (fabsf(g_lrn_drift - s_last_drift) > LS_DRIFT_LSB) return true;
    if (fabsf(g_lrn_damp  - s_last_damp ) > LS_DAMP_DELTA) return true;
    return false;
}

void live_store_tick(uint32_t now_ms)
{
    if (flash_ring_slot_count() == 0) return;   /* ring disabled → no-op */
    if (!ls_should_save(now_ms)) return;

    uint8_t buf[LS_PAYLOAD];
    ls_pack(buf);
    if (flash_ring_write(buf, LS_PAYLOAD)) {
        s_last_drift = g_lrn_drift;
        s_last_damp  = g_lrn_damp;
        s_last_ms    = now_ms;
        s_have_last  = true;
    }
    s_force_save = false;   /* clear regardless; retry next tick if write failed */
}

void live_store_request_save(void)
{
    s_force_save = true;
}
