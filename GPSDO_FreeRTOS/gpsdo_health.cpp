/*
 * gpsdo_health.cpp — correction statistics. See the header for what these
 * numbers mean and, more importantly, what they cannot mean.
 *
 * Part of GPSDO v1.07.57rt
 *
 * Exponentially weighted rather than buffered: three time constants cost three
 * multiply-adds per second and no memory, where a ring buffer covering an hour
 * would be several kilobytes to answer the same question no better.
 */
#include <Arduino.h>
#include <math.h>
#include "gpsdo_config.h"
#include "gpsdo_algorithms.h"
#include "gpsdo_state.h"
#include "gpsdo_health.h"

/* Weights, in CORRECTIONS rather than seconds. A decade apart, so the four
 * windows cover four orders of magnitude without overlapping usefully.
 *
 * What they span in wall-clock time depends on the algorithm, which is exactly
 * why they are not labelled in minutes:
 *
 *              algorithm 11 (1 s)   algorithm 10, LIV 60
 *     100            1.7 min              1.7 h
 *     1 000          17 min               17 h
 *     10 000         2.8 h                7 days
 *     100 000        28 h                 70 days
 *
 * These are exponential weights, so N is a time constant and not a hard window:
 * about 63% of the weight falls inside N corrections and 95% inside 3N. The
 * oldest data fades rather than dropping out. That costs four multiply-adds per
 * correction and no memory, where a ring buffer holding 100 000 samples would be
 * most of the RAM budget to answer the same question no better. */
#define A_100   (1.0 / 100.0)
#define A_1K    (1.0 / 1000.0)
#define A_10K   (1.0 / 10000.0)
#define A_100K  (1.0 / 100000.0)

/* True while the loop is disciplining normally: locked, and no calibration
 * sweeping the DAC underneath us. Anything else writing to the DAC is issuing a
 * command, not correcting an error, and must not enter the statistics.
 *
 * Algorithms 0-9 have no lock concept, so they are excluded entirely rather than
 * counted with a meaning nobody could interpret; CS says so instead of quietly
 * reporting a number about nothing.
 *
 * ALGORITHMS 12 AND 13 WERE EXCLUDED TOO, and that was a plain omission rather
 * than a decision. They fell through the `default:` below with 0-9, so on the
 * two newest loops — the two most likely to be running on a board whose owner
 * cares about this number — the statistics never counted a single correction,
 * and CS explained the blank by saying the board was "running an algorithm
 * below 10". Both of them can say whether they are locked; until now neither
 * was asked. See mlacc_locked() / kf_locked() in gpsdo_algorithms.h. */
bool health_loop_locked(void)
{
    switch (gCtrl.active_algo) {
        case 10: return (g_ltic.state == LTIC_LOCK);
        case 11: return g_lars_locked;
        case 12: return mlacc_locked();
        case 13: return kf_locked();
        default: break;             /* algorithms 0-9: no lock state to gate on */
    }
    return false;
}

static bool counting_now(void)
{
    /* setup() writes the DAC three times — the initial 127, the recalled PWM and
     * the default — before xEventGroupCreate() has run, so this is reached with
     * xSysEvents still NULL. Passing NULL to xEventGroupGetBits() trips
     * configASSERT and the processor stops: the board went dead before the LED
     * ever blinked, with nothing on the console to say why.
     *
     * Those early writes are commands anyway, not corrections, so returning
     * false is both safe and correct. */
    if (xSysEvents == NULL) return false;

    EventBits_t ev = xEventGroupGetBits(xSysEvents);
    if (ev & (EVT_NEED_CALIBRATION | EVT_NEED_LTIC_CAL)) return false;

    return health_loop_locked();
}

static uint16_t s_prev;
static bool     s_have_prev;
static double   s_ms100, s_ms1k, s_ms10k, s_ms100k;   /* mean squares */
static double   s_bias;
static uint16_t s_peak;
static uint32_t s_n;
static uint32_t s_gated;   /* writes ignored because the loop was not locked */
/* Interval between corrections, smoothed. Measured rather than assumed: it is
 * the only way to state the windows in seconds without hard-coding an
 * assumption about the algorithm that would be wrong half the time. */
static uint32_t s_last_ms;
static double   s_dt;

void health_reset(void)
{
    s_have_prev = false;
    s_ms100 = s_ms1k = s_ms10k = s_ms100k = 0.0;
    s_bias = 0.0;
    s_peak = 0;
    s_n = 0;
    s_gated = 0;
    s_last_ms = 0;
    s_dt = 0.0;
}

void health_note_output(uint16_t pwm)
{
    if (!counting_now()) {
        /* Drop the reference too. Resuming across a gap would fabricate a single
         * enormous "correction" equal to everything that happened while we were
         * not looking. */
        s_have_prev = false;
        s_last_ms   = 0;   /* forget the timestamp too: the first interval after
                            * a gap would otherwise be the length of the gap */
        s_gated++;
        return;
    }
    uint32_t now = millis();
    if (s_last_ms != 0) {
        double dt = (double)(now - s_last_ms) / 1000.0;
        /* Ignore absurd gaps: a resumed session after the gate was shut would
         * otherwise report an interval of hours. */
        if (dt > 0.0 && dt < 600.0)
            s_dt = (s_dt == 0.0) ? dt : (s_dt + 0.05 * (dt - s_dt));
    }
    s_last_ms = now;

    if (!s_have_prev) { s_prev = pwm; s_have_prev = true; return; }

    double d = (double)pwm - (double)s_prev;
    s_prev = pwm;

    /* A correction of zero is still a correction: it says the loop saw nothing
     * worth acting on, which is exactly the information wanted. Skipping them
     * would flatter the statistics. */
    double sq = d * d;
    s_ms100  += A_100  * (sq - s_ms100);
    s_ms1k   += A_1K   * (sq - s_ms1k);
    s_ms10k  += A_10K  * (sq - s_ms10k);
    s_ms100k += A_100K * (sq - s_ms100k);
    s_bias   += A_100K * (d  - s_bias);

    double ad = fabs(d);
    if (ad > (double)s_peak && ad < 65535.0) s_peak = (uint16_t)ad;
    if (s_n < 0xFFFFFFFFu) s_n++;
}

void health_get(health_stats_t *out)
{
    /* CT stores the slope as g_pid[7].Kp = 0.40/K, so K is recovered from it.
     * Without CT there is no way to turn LSB into hertz, and the caller is told
     * so rather than being handed a number derived from a guess. */
    double kp = (double)g_pid[7].Kp;
    out->have_k       = (kp > 100.0);
    out->k_hz_per_lsb = out->have_k ? (0.40 / kp) : 0.0;
    out->updates = s_n;
    out->gated   = s_gated;
    out->counting = counting_now();
    out->rms_100  = sqrt(s_ms100);
    out->rms_1k   = sqrt(s_ms1k);
    out->rms_10k  = sqrt(s_ms10k);
    out->rms_100k = sqrt(s_ms100k);
    out->bias_100k = s_bias;
    out->secs_per_corr = s_dt;
    out->peak    = s_peak;
}

/* ---------------------------------------------------------------------- */
/* CPU load. Not measured here any more — it falls out of the per-task
 * accounting below as 100% minus the idle task's share, which is exact where
 * the idle-hook spin counter it replaced was a self-calibrating estimate. */
static uint8_t  s_load_pct = 255;   /* 255 = not measured yet */

uint8_t cpu_load_pct(void) { return s_load_pct; }


/* ======================================================================
 * CPU load per task. See the header for what it measures and what it
 * attributes where.
 * ====================================================================== */

/* The Cortex-M4 cycle counter. Enabling TRCENA powers the DWT block; no
 * debugger need be attached and no timer is consumed. CYCCNT is 32 bits and
 * wraps every 43 s at 100 MHz, which does not matter: only DIFFERENCES between
 * consecutive context switches are taken, and unsigned subtraction is exact
 * across the wrap. */
void cpu_trace_begin(void)
{
#if defined(DWT) && defined(CoreDebug)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

static inline uint32_t cpu_cycles(void)
{
#if defined(DWT) && defined(CoreDebug)
    return DWT->CYCCNT;
#else
    return 0;
#endif
}

volatile bool g_cpu_task_line = false;   /* TL 1 turns it on; never stored */

struct cpu_slot {
    void       *tcb;                     /* identity, not dereferenced here  */
    const char *name;
    uint32_t    cyc;                     /* cycles since the last roll       */
    uint32_t    sum;                     /* running total over the window    */
    uint16_t    ring[CPU_WINDOW_S];      /* per second, hundredths of %      */
};
static struct cpu_slot s_slot[CPU_TASKS_MAX];
static uint8_t  s_slots;
static uint16_t s_ring_pos;
static uint16_t s_filled;
static void    *s_cur;                   /* task currently switched in       */
static uint32_t s_mark;                  /* cycle count when it came in      */

/* traceTASK_SWITCHED_IN() lands here, from inside vTaskSwitchContext with the
 * scheduler suspended. Everything it does must be short and must not call any
 * FreeRTOS API — hence the linear scan over a handful of pointers rather than
 * anything cleverer, and hence pcTaskGetName() being resolved at REPORT time
 * and not here. */
extern "C" void cpu_trace_switch_in(void *tcb)
{
    uint32_t now = cpu_cycles();

    if (s_cur != 0) {
        uint32_t d = now - s_mark;       /* exact across the 32-bit wrap */
        for (uint8_t i = 0; i < s_slots; i++) {
            if (s_slot[i].tcb == s_cur) { s_slot[i].cyc += d; break; }
        }
    }
    s_cur  = tcb;
    s_mark = now;

    for (uint8_t i = 0; i < s_slots; i++) if (s_slot[i].tcb == tcb) return;
    if (s_slots < CPU_TASKS_MAX) {
        s_slot[s_slots].tcb  = tcb;
        s_slot[s_slots].name = 0;
        s_slots++;
    }
    /* Past CPU_TASKS_MAX a task simply goes uncounted. Silently dropping it
     * would make the percentages lie, so the report says so instead: the shares
     * are of the switched time actually seen, and the caller is told the slot
     * count when it is at the limit. */
}

/* Once a second, from a task that always runs.
 *
 * NOT from the telemetry line, which is where the whole-CPU figure is ticked
 * from: TAB pauses telemetry, and a load measurement that stops when you stop
 * watching it is worse than none. The uptime task runs whatever else is
 * happening.
 *
 * Shares are taken of the second's own total rather than of a clock rate, so
 * the arithmetic needs no idea how fast the part runs and the columns always
 * add to 100. */
void cpu_second_tick(void)
{
    /* SELF-TIMING, because the only call site that is guaranteed to run is a
     * 2 Hz loop that skips its body whenever the PPS has already claimed the
     * second. Rolling on elapsed milliseconds rather than on being called
     * exactly once a second means the caller does not have to be exact, and a
     * stalled or a doubled call cannot shorten or double a bucket. */
    static uint32_t s_last_roll;
    uint32_t now = millis();
    if (s_last_roll != 0u && (uint32_t)(now - s_last_roll) < 1000u) return;
    s_last_roll = now;

    /* SNAPSHOT UNDER A CRITICAL SECTION, and close the in-flight task's account
     * while we are in there.
     *
     * Two things go wrong without this. A context switch landing between the
     * sum and the clear loses or double-counts that task's cycles — small, but
     * wrong for no reason. Far worse: the task running RIGHT NOW has not been
     * switched out, so none of its current slice is in any accumulator yet. The
     * idle task routinely runs for most of a second uninterrupted, so the
     * bucket would be built from whatever happened to have switched — a second
     * in which the board was 95% idle would report a total of a few hundred
     * microseconds and percentages drawn from noise. Charging the open slice
     * here and moving the mark makes each bucket exactly one second of
     * accounted cycles. */
    uint32_t cyc[CPU_TASKS_MAX];
    uint8_t  n_slot;
    uint32_t total = 0;

    taskENTER_CRITICAL();
    {
        uint32_t now_c = cpu_cycles();
        if (s_cur != 0) {
            for (uint8_t i = 0; i < s_slots; i++)
                if (s_slot[i].tcb == s_cur) { s_slot[i].cyc += now_c - s_mark; break; }
        }
        s_mark = now_c;
        n_slot = s_slots;
        for (uint8_t i = 0; i < n_slot; i++) { cyc[i] = s_slot[i].cyc; s_slot[i].cyc = 0; }
    }
    taskEXIT_CRITICAL();

    for (uint8_t i = 0; i < n_slot; i++) total += cyc[i];

    uint16_t idle100 = 0;
    for (uint8_t i = 0; i < n_slot; i++) {
        uint16_t v = 0;
        if (total != 0u) {
            uint32_t p = (uint32_t)(((uint64_t)cyc[i] * 10000u) / total);
            v = (p > 10000u) ? 10000u : (uint16_t)p;
        }
        /* Names are resolved here rather than in the switch hook: this runs in
         * a task, where calling into FreeRTOS is allowed. */
        if (s_slot[i].name == 0 && s_slot[i].tcb != 0)
            s_slot[i].name = pcTaskGetName((TaskHandle_t)s_slot[i].tcb);
        if (s_slot[i].name && s_slot[i].name[0] == 'I' &&
            s_slot[i].name[1] == 'D' && s_slot[i].name[2] == 'L')
            idle100 = v;
        s_slot[i].sum -= s_slot[i].ring[s_ring_pos];
        s_slot[i].ring[s_ring_pos] = v;
        s_slot[i].sum += v;
    }

    /* The headline figure, for this second: everything that was not idle. Only
     * once there is something to divide — before the first switches the honest
     * answer is "not measured", not "0% busy". */
    if (total != 0u) {
        uint16_t busy = (idle100 >= 10000u) ? 0u : (uint16_t)(10000u - idle100);
        s_load_pct = (uint8_t)((busy + 50u) / 100u);
    }
    s_ring_pos = (uint16_t)((s_ring_pos + 1u) % CPU_WINDOW_S);
    if (s_filled < CPU_WINDOW_S) s_filled++;
}

uint16_t cpu_tasks_filled(void) { return s_filled; }

uint8_t cpu_tasks_get(cpu_task_t *out, uint8_t max)
{
    if (out == 0 || max == 0) return 0;
    uint16_t div = (s_filled > 0u) ? s_filled : 1u;

    uint8_t n = 0;
    for (uint8_t i = 0; i < s_slots && n < max; i++) {
        out[n].name   = s_slot[i].name ? s_slot[i].name : "?";
        out[n].pct100 = (uint16_t)(s_slot[i].sum / div);
        n++;
    }
    /* Busiest first — insertion sort over at most a dozen entries. */
    for (uint8_t i = 1; i < n; i++) {
        cpu_task_t k = out[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && out[j].pct100 < k.pct100) { out[j+1] = out[j]; j--; }
        out[j+1] = k;
    }
    return n;
}
