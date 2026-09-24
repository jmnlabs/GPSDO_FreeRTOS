/*
 * gpsdo_health.h — self-assessment from the loop's own corrections.
 *
 * Part of GPSDO v1.07.57rt
 *
 * WHY THIS EXISTS
 * ---------------
 * Algorithm 11 was validated against a rubidium standard on someone else's
 * bench. Almost nobody who builds this will have one, and without it they have
 * the author's word and a green rectangle on the display. That is a poor
 * position to leave a builder in.
 *
 * The idea is Alan's (MIS42N on EEVblog), who pointed out that a disciplined
 * oscillator can assess itself: the correction the loop applies is the error it
 * just observed, so the size of those corrections says whether the discipline is
 * working. GPS is the reference and there is nothing better to compare frequency
 * against. His own design relies on this, which is why it needs no secondary
 * standard — a real advantage over the approach taken here.
 *
 * The firmware already computes every one of these numbers and throws them away.
 *
 * WHAT IT DOES AND DOES NOT TELL YOU
 * ----------------------------------
 * It measures whether the LOOP IS SETTLED, not whether the OUTPUT IS GOOD. Those
 * are the same thing only while the phase detector is trustworthy.
 *
 * If the detector is noisy, the loop makes corrections chasing that noise. The
 * corrections grow, and this reports them faithfully — but the oscillator was
 * fine and the loop has just made it worse. That is precisely the failure a GPSDO
 * is prone to: perfectly locked by every indicator, and worse at short averaging
 * times than if left alone. Nothing measured from inside the loop can see it.
 *
 * So read a small figure as "the loop is not fighting anything", which is
 * necessary but not sufficient. A large or growing figure is the useful signal:
 * something is wrong, even if this cannot say what.
 */
#ifndef GPSDO_HEALTH_H
#define GPSDO_HEALTH_H

#include <stdint.h>
#include <stdbool.h>

/* Called from the DAC layer on every write, so it sees every correction whatever
 * algorithm produced it and needs no hook in the control code. */
void health_note_output(uint16_t pwm);

/* Reset the statistics — after CT, LC or an algorithm change, where the step
 * that follows is a deliberate jump rather than a correction. */
void health_reset(void);

/* The running algorithm's own lock verdict: 10 LOCK state, 11 g_lars_locked,
 * 12 and 13 their published flags; false for 0-9, which have no lock concept.
 * The statistics gate on it (with a calibration check on top), and the span
 * module uses it to decide whether a code is a null worth remembering. */
bool health_loop_locked(void);

typedef struct {
    bool     have_k;        /* false until CT has measured the VCO slope   */
    double   k_hz_per_lsb;  /* 0 if unknown                                */
    bool     counting;      /* gate open right now?                        */
    uint32_t updates;       /* corrections counted since reset             */
    uint32_t gated;         /* writes ignored: not locked, or calibrating  */
    /* RMS correction over the last N corrections, in DAC counts. Counted in
     * CORRECTIONS, not seconds, because the correction rate depends on the
     * algorithm: algorithm 11 steers once a second, algorithm 10 once per LIV.
     * Labelling these in minutes would have meant one thing under algorithm 11
     * and sixty times that under algorithm 10 with LIV 60 — the same number
     * describing two different spans. Corrections are what the loop actually
     * did, so that is what is counted. */
    double   rms_100;       /* last ~100 corrections                       */
    double   rms_1k;        /* ~1 000                                      */
    double   rms_10k;       /* ~10 000                                     */
    double   rms_100k;      /* ~100 000 — over a day at one per second     */
    double   bias_100k;     /* mean correction: non-zero means steady drift */
    /* Measured interval between corrections, seconds. Lets the caller turn the
     * correction counts above into wall-clock time without knowing which
     * algorithm is running or what LIV is set to — algorithm 11 steers once a
     * second, algorithm 10 once per LIV, and the answer differs by that factor.
     * Zero until at least two corrections have been seen. */
    double   secs_per_corr;
    uint16_t peak;          /* largest single correction since reset, LSB   */
} health_stats_t;

void health_get(health_stats_t *out);

/* ---- CPU load ---------------------------------------------------------
 *
 * The share of the last second the processor did NOT spend in the idle task,
 * which is the same thing as the sum of every other task's share. It comes
 * straight out of the per-task measurement below — exact cycle counts between
 * context switches — so there is nothing to calibrate and nothing to assume.
 *
 * It used to be counted the other way round: an idle-hook spin counter, with
 * the highest count ever seen taken as 0% load. That works, and it has a flaw
 * that cannot be measured from inside it — a board that has never once been
 * near idle has an underestimated reference, and therefore an optimistic
 * reading, for ever. The cycle counter has no reference to be wrong about.
 *
 * What it measures is the CPU the idle task did not get, so interrupt time
 * counts too: an ISR steals from whatever is running, idle included. */
uint8_t cpu_load_pct(void);     /* 0..100, 255 until the first second */

/* ---- CPU load per task ------------------------------------------------
 *
 * The figure above says how much of the processor is spoken for. It does not
 * say BY WHAT, and that is the question worth answering when the number moves:
 * a display refresh, a GPS sentence burst and a control cycle cost very
 * different amounts and only one of them is on the critical path.
 *
 * HOW IT IS MEASURED. Not by sampling. FreeRTOS calls traceTASK_SWITCHED_IN()
 * on every context switch, and the Cortex-M4 has a free-running cycle counter
 * in the DWT block that costs one load to read — so the time between two
 * switches is known exactly and belongs, exactly, to the task that was running.
 * That is a handful of cycles per switch and no timer: the sampling profiler
 * this replaced would have needed the tick, and would have been blind to any
 * task that starts on a tick boundary and finishes before the next one, which
 * on this firmware is most of them.
 *
 * WHAT IT ATTRIBUTES WHERE. Interrupt time lands on whichever task was
 * interrupted, because an ISR does not switch context. The PPS capture, the
 * DMA completion and the UART all steal from whoever is unlucky, so a task's
 * figure is "the processor spent this long with this task current", which is
 * the honest reading and not quite the same as "this task used this much".
 *
 * THE WINDOW is a hundred one-second buckets, a true sliding mean rather than
 * an exponential one: asked for a 100 s average, an EWMA with a 100 s time
 * constant would still carry a fifth of its weight from five minutes ago.
 * Percentages are shares of the second's total switched time, so they always
 * sum to 100 and no clock rate enters the arithmetic.
 *
 * There is no persistence and none is wanted: this is a measurement of what the
 * firmware is doing now, and a figure recalled from flash would describe a
 * different build. */
#define CPU_TASKS_MAX   12u     /* seven tasks, IDLE, the timer service, room */
#define CPU_WINDOW_S    100u

typedef struct {
    const char *name;
    uint16_t    pct100;         /* hundredths of a percent, mean over window */
} cpu_task_t;

/* Called from setup() before the scheduler starts: enables the cycle counter. */
void    cpu_trace_begin(void);
/* Called once a second from a task that always runs — see the note in the .cpp
 * about why this is not driven from the telemetry line. */
void    cpu_second_tick(void);
/* Fills up to `max` entries, busiest first; returns how many. */
uint8_t cpu_tasks_get(cpu_task_t *out, uint8_t max);
/* Seconds of the window filled so far, capped at CPU_WINDOW_S. */
uint16_t cpu_tasks_filled(void);

/* The extra telemetry line, off at boot and never stored. */
extern volatile bool g_cpu_task_line;

#endif /* GPSDO_HEALTH_H */
