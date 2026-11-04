/* ======================================================================
 * gpsdo_live_store.cpp  —  live-data persistence over the flash ring buffer
 * Part of GPSDO v1.08.65rt
 * See gpsdo_live_store.h for the design and payload layout.
 * ====================================================================== */

#include "gpsdo_live_store.h"
#include "gpsdo_flash_ring.h"
#include "gpsdo_state.h"
#include "gpsdo_algorithms.h"
#include "gpsdo_health.h"
#include "gpsdo_span.h"
#include <string.h>
#include <math.h>

/* hysteresis thresholds (see header) */
#define LS_DRIFT_LSB   8.0f
#define LS_DAMP_DELTA  0.03f
#define LS_MIN_MS      (20u * 60u * 1000u)   /* 20 minutes */
/* The PWM's own reason to be saved (build 65): a move worth more than this
 * from the saved code, after the loop has been locked this long without a
 * break. IN HERTZ, so it means the same on every board and span: 0.02 Hz is
 * 62 LSB on the author's 16-bit board - a cold start from there settles in
 * about two minutes (tools/loopsim/coldstart.sh), against five to nine from
 * the 1469 LSB the ring held - and ~820 LSB on the REDUCED span of the V3
 * level shifter, where a fixed 64 LSB was 1.6e-10 and saved about once a day.
 * Turned into LSB with CT's K (g_pid[7].Kp = 0.40 / K), within 16..4096;
 * without a CT there is no K, and it is 64 LSB. */
#define LS_PWM_HZ      0.02
#define LS_PWM_LSB     64       /* without CT */
#define LS_LOCK_MS     (20u * 60u * 1000u)   /* 20 minutes of lock */
/* A code that moves more than this between two ticks (200 ms) was put there -
 * SP, a span move, a calibration's sweep or its restore - and not settled on
 * by the loop, whose lock verdict only changes at its next step and so still
 * says locked. A locked loop moves a few LSB a step; even algorithm 11's cap
 * (4 mHz a step) is ~160 LSB on a REDUCED-span board. A jump starts the 20
 * minutes again. */
#define LS_JUMP_LSB    256
/* A tick this long after the one before means this task was busy elsewhere -
 * C, CT and LC sweep the code for minutes and then put it back - and the lock
 * was not watched in between (build 65). Ticks are 200 ms apart; a flash
 * wrap holds one for a second or two. */
#define LS_GAP_MS      5000u

#define LS_PAYLOAD     28u      /* written since build 65 (the span byte)   */
#define LS_PAYLOAD_V1  27u      /* what build 64 and earlier wrote: loads   */

/* last-saved values, to measure change since the previous write */
static float    s_last_drift = 0.0f;
static float    s_last_damp  = 1.0f;
static uint32_t s_last_ms    = 0;
static bool     s_have_last  = false;
static bool     s_force_save = false;
static uint16_t s_last_pwm   = 0;       /* the code in the newest slot        */
static uint8_t  s_code_span  = 0xFFu;   /* that code's span, as boot found it */
/* lock bookkeeping for the PWM criterion: since when, unbroken */
static bool     s_lock_on    = false;
static uint32_t s_lock_t0    = 0;
static uint16_t s_tick_pwm   = 0;       /* the code at the previous tick      */
static uint32_t s_tick_ms    = 0;       /* ...and when that tick was          */
static bool     s_ticked     = false;
/* bumped by the CLI's SP, up and dp before they move the code */
static volatile uint8_t s_hand = 0;
static uint8_t  s_hand_seen  = 0;
/* after a write that failed: wait before the next try */
static bool     s_wr_failed  = false;
static uint32_t s_wr_fail_ms = 0;

/* ---- little-endian pack/unpack helpers ---- */

static void ls_put_f(uint8_t *p, float v)  { memcpy(p, &v, 4); }
static void ls_put_u16(uint8_t *p, uint16_t v){ p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static float    ls_get_f(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static uint16_t ls_get_u16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }

static bool ls_pack(uint8_t *buf)
{
    float drift, damp, nsv, zero, range, centre;
    uint16_t pwm; uint8_t algo;

    drift = g_lrn_drift; damp = g_lrn_damp;
    nsv   = g_ltic.ns_per_volt; zero = g_ltic.zero_offset;
    range = g_ltic.range_ns;    centre = g_ltic.centre_v;

    pwm = DEFAULT_PWM_OUTPUT; algo = 0;
    /* The span the code is in, read on the same task that moves it: the
     * control task calls both live_store_tick() and span_poll(), and boot is
     * single-threaded. */
    const uint8_t span = span_active();
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        pwm  = gCtrl.pwm_output;          /* boot path: single-threaded */
        algo = gCtrl.active_algo;
    } else if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        pwm  = gCtrl.pwm_output;
        algo = gCtrl.active_algo;
        xSemaphoreGive(xCtrlMutex);
    } else {
        /* NOT THE DEFAULT IN THE LOOP'S PLACE (build 65). Up to build 64 a
         * mutex held past 50 ms packed DEFAULT_PWM_OUTPUT as the code, and
         * the next boot started from it. Nothing is written this tick; the
         * next one tries again. */
        return false;
    }

    ls_put_f  (buf +  0, drift);
    ls_put_f  (buf +  4, damp);
    ls_put_f  (buf +  8, nsv);
    ls_put_f  (buf + 12, zero);
    ls_put_f  (buf + 16, range);
    ls_put_u16(buf + 20, pwm);
    ls_put_f  (buf + 22, centre);
    buf[26] = algo;
    buf[27] = span;
    return true;
}

static void ls_apply(const uint8_t *buf, uint16_t n)
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

    /* Which span that code is in. A slot from build 64 or earlier has no
     * such byte, and then nobody knows: span_begin() falls back to the span
     * the settings record names, as every build before 65 did. */
    s_code_span = (n > LS_PAYLOAD_V1 && buf[27] <= SPAN_REDUCED) ? buf[27] : 0xFFu;
    s_last_pwm   = pwm;

    s_last_drift = g_lrn_drift;
    s_last_damp  = g_lrn_damp;
    s_have_last  = true;
}

bool live_store_begin(void)
{
    uint8_t buf[LS_PAYLOAD];
    uint16_t n = flash_ring_read_newest(REC_LIVE, buf, sizeof(buf));
    if (n < LS_PAYLOAD_V1) return false;   /* nothing stored yet */
    ls_apply(buf, n);
    return true;
}

uint8_t live_store_code_span(void)
{
    return s_code_span;
}

/* THE PWM CRITERION (build 65). Evaluated on every tick, whatever else is
 * due, because it has to see every second of the lock to know it was
 * unbroken. Locked means the running algorithm's own verdict
 * (health_loop_locked(): algorithms 10-13 - 0-9 have none and never save a
 * code this way), not in holdover, no calibration running, and nothing but
 * the loop having moved the code: no jump since the last tick (LS_JUMP_LSB),
 * nothing typed with SP, up or dp, however small (live_store_note_manual()),
 * and no stretch this task spent elsewhere (LS_GAP_MS: C, CT and LC run on it
 * for minutes, unseen by any tick). The verdict changes only at the loop's
 * next step, so after any of these it still says locked: the 20 minutes
 * start again instead. And the span settled: no jumper move still being
 * saved, no anchor or CT pending, so the code is a code of the span the slot
 * will name. */
static int32_t ls_pwm_lsb(void)
{
    const double kp = (double)g_pid[7].Kp;
    if (!(kp > 100.0)) return LS_PWM_LSB;              /* no CT: no K */
    double lsb = LS_PWM_HZ * kp / 0.40;                /* Hz * (LSB per Hz) */
    if (lsb < 16.0)   lsb = 16.0;
    if (lsb > 4096.0) lsb = 4096.0;
    return (int32_t)(lsb + 0.5);
}

static bool ls_pwm_due(uint32_t now_ms)
{
    /* The note before the code: SP bumps it before it moves the code, so a
     * code it typed is never read here without its note. */
    const uint8_t  hand = s_hand;
    const uint16_t code = gCtrl.pwm_output;
    int32_t step = (int32_t)code - (int32_t)s_tick_pwm;
    if (step < 0) step = -step;
    s_tick_pwm = code;
    const bool away = s_ticked && (uint32_t)(now_ms - s_tick_ms) > LS_GAP_MS;
    s_tick_ms = now_ms;
    s_ticked  = true;
    const bool typed = hand != s_hand_seen;
    s_hand_seen = hand;
    if (!health_loop_locked() || gCtrl.holdover_mode || g_calib_active ||
        step > LS_JUMP_LSB || typed || away) {
        s_lock_on = false;
        return false;
    }
    if (!s_lock_on) { s_lock_on = true; s_lock_t0 = now_ms; return false; }
    if ((uint32_t)(now_ms - s_lock_t0) < LS_LOCK_MS) return false;
    if (!span_settled()) return false;
    int32_t d = (int32_t)code - (int32_t)s_last_pwm;
    if (d < 0) d = -d;
    return d > ls_pwm_lsb();
}

static bool ls_should_save(uint32_t now_ms)
{
    const bool pwm_due = ls_pwm_due(now_ms);   /* first: it tracks the lock */
    if (s_force_save) return true;
    if (!s_have_last) {
        /* first save allowed once we have any learned value away from theory,
         * or a code worth keeping (a board whose ring has no live slot yet) */
        return pwm_due ||
               (fabsf(g_lrn_drift) > LS_DRIFT_LSB) ||
               (fabsf(g_lrn_damp - 1.0f) > LS_DAMP_DELTA);
    }
    /* time gate first (cheap), then magnitude gate */
    if ((uint32_t)(now_ms - s_last_ms) < LS_MIN_MS) return false;
    if (pwm_due) return true;
    if (fabsf(g_lrn_drift - s_last_drift) > LS_DRIFT_LSB) return true;
    if (fabsf(g_lrn_damp  - s_last_damp ) > LS_DAMP_DELTA) return true;
    return false;
}

void live_store_tick(uint32_t now_ms)
{
    /* The criteria first, ring or not: the PWM's has to see every tick. */
    const bool due = ls_should_save(now_ms);
    if (flash_ring_slot_count() == 0) return;   /* ring disabled → no-op */
    if (!due) return;
    /* A WRITE THAT FAILED WAITS LS_MIN_MS BEFORE THE NEXT TRY (build 65). Up
     * to build 64 it was tried again on the next tick, five times a second for
     * as long as the reason held - each try an erase or a program against a
     * sector that had just refused one, and the PWM criterion now makes that
     * reachable on algorithms 11-13 too. A requested save (LC, a span
     * move) stays pending through the wait instead of being dropped. */
    if (s_wr_failed && (uint32_t)(now_ms - s_wr_fail_ms) < LS_MIN_MS) return;

    uint8_t buf[LS_PAYLOAD];
    if (!ls_pack(buf)) return;              /* the code could not be read: next tick */
    if (flash_ring_write(REC_LIVE, buf, LS_PAYLOAD)) {
        s_last_drift = g_lrn_drift;
        s_last_damp  = g_lrn_damp;
        s_last_pwm   = ls_get_u16(buf + 20);   /* the code the slot holds */
        s_last_ms    = now_ms;
        s_have_last  = true;
        s_force_save = false;
        s_wr_failed  = false;
    } else {
        s_wr_failed  = true;
        s_wr_fail_ms = now_ms;
    }
}

void live_store_request_save(void)
{
    s_force_save = true;
}

/* A code typed with SP, up or dp (build 65): not where the loop settled,
 * however close, and the loop's lock verdict has not noticed it yet. CLI
 * task, before the code moves; the PWM criterion starts its 20 minutes
 * again. */
void live_store_note_manual(void)
{
    s_hand++;
}
