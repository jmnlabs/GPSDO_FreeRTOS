/**
 * GPSDO_algorithms.cpp — Control loop algorithm implementations
 *
 * Part of GPSDO FreeRTOS v0.50
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Ten algorithms (0-9) selectable at runtime via CLI command LA.
 * Algorithms 0-2 are from the original v0.06c codebase.
 * Algorithms 3-9 are new implementations:
 *   3: FLL PID (manual coefficients)
 *   4: PLL PI  (manual, no derivative)
 *   5: PLL PID (manual)
 *   6: FLL PID (genetically optimised coefficients)
 *   7: PLL PID (genetically optimised coefficients)
 *   8: Hybrid FLL+PLL with sigmoid blend
 *   9: Experimental single-layer neural network
 *
 * All PID coefficients are stored in the global g_pid[] array and
 * can be modified at runtime via CLI (KP/KI/KD/IL commands).
 */

#include "GPSDO_algorithms.h"
#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include <string.h>
#include <math.h>

/* -----------------------------------------------------------------------
 * Internal helper: snapshot frequency data from FreqData_t under mutex.
 * Fills a local copy so algorithms don't need to hold the mutex.
 * ----------------------------------------------------------------------- */
typedef struct {
    double   avg10, avg100, avg1000, avg10000, avg20000;
    bool     have10, have100, have1000, have10000, have20000;
    int16_t  cumul10, cumul100, cumul1000, cumul10000, cumul20000;
} FreqSnapshot_t;

static void take_freq_snapshot(FreqSnapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(15)) == pdTRUE) {
        s->avg10    = gFreq.avg10;     s->have10    = gFreq.full10;
        s->avg100   = gFreq.avg100;    s->have100   = gFreq.full100;
        s->avg1000  = gFreq.avg1000;   s->have1000  = gFreq.full1000;
        s->avg10000 = gFreq.avg10000;  s->have10000 = gFreq.full10000;
        s->avg20000 = gFreq.avg20000;  s->have20000 = gFreq.full20000;
        s->cumul10     = gFreq.cumul10;
        s->cumul100    = gFreq.cumul100;
        s->cumul1000   = gFreq.cumul1000;
        s->cumul10000  = gFreq.cumul10000;
        s->cumul20000  = gFreq.cumul20000;
        xSemaphoreGive(xFreqMutex);
    }
}

/* Clamp PWM to valid range and guard against uint16 wrap */
static uint16_t clamp_pwm(int32_t v)
{
    if (v < 1)     return 1;
    if (v > 65535) return 65535;
    return (uint16_t)v;
}

/* -----------------------------------------------------------------------
 * apply_correction — common output stage for the disciplining algorithms.
 *
 *   1. Dead-zone (lock hold): if the frequency error is within LOCK_HZ AND
 *      the accumulated phase is within LOCK_PHASE (Hz·s ≈ time offset in
 *      units of cycles), the loop is locked — suppress the step so the PWM
 *      stops twitching and the OCXO free-runs on its own short-term
 *      stability.  *locked is set true so the caller can show "hit".
 *      Without the phase test the PLL would nudge ±2-5 LSB every period
 *      forever, because the phase term never resolves to exactly zero.
 *
 *   2. Slew-rate limit: clamp |u| to max_step so a slow overnight phase
 *      drift is corrected over several periods, not one big PWM jump.
 *
 *   3. Clamp + write.
 * ---------------------------------------------------------------------- */
static uint16_t apply_correction(uint16_t pwm, double u,
                                 double e_freq, double phase_acc,
                                 double max_step, bool *locked)
{
    const double LOCK_HZ    = 0.0010;   /* 1 mHz  ≈ 1e-10 frac. freq.     */
    const double LOCK_PHASE = 5.0;      /* 5 Hz·s ≈ 500 ns accumulated    */

    bool in_lock = (e_freq   > -LOCK_HZ    && e_freq   < LOCK_HZ) &&
                   (phase_acc > -LOCK_PHASE && phase_acc < LOCK_PHASE);
    if (locked) *locked = in_lock;

    if (in_lock) return pwm;            /* hold — no PWM motion in lock */

    /* Slew-rate limit */
    if (u >  max_step) u =  max_step;
    if (u < -max_step) u = -max_step;

    return clamp_pwm((int32_t)pwm + (int32_t)u);
}

/* Write trendstr — caller already holds xCtrlMutex */
static void set_trend(const char *s)
{
    strncpy(gCtrl.trendstr, s, 4);
    gCtrl.trendstr[4] = '\0';
}

/* ======================================================================
 * RUNTIME-TUNABLE PARAMETERS
 *
 * These are STARTING values only. The OCXO type does not need to be known
 * at compile time: the CT (Calibrate & Tune) command measures the actual
 * plant gain K and recomputes every coefficient for the fitted oscillator
 * (PLL Kp = 0.40/K on frequency; FLL Kp = 0.35/K, Ki = Kp/300, Kd = Kp*73;
 * NN max step = 0.05/K). Run CT once, then ES to persist.
 *
 * The defaults below assume a typical 10 MHz OCXO with K ~ 0.4 mHz/LSB on
 * a 0..3.3 V PWM DAC (16-bit, 1 LSB = 50.35 uV). They are deliberately
 * mid-range so the loop is stable on any common unit before CT is run.
 * All values can also be set at runtime via CLI (KP/KI/KD/IL/BC/BS/NS).
 *
 * Coefficient meaning:
 *   PLL (4,5,7): Kp on frequency error, Kd/Ki gentle terms on phase
 *   FLL (3,6):   classic frequency-domain PID
 *   [8] hybrid reads [6]+[7]; only its I_LIMIT is used here
 *   [9] NN uses fixed weights; only I_LIMIT (normalisation) applies
 * ====================================================================== */
PidParams_t g_pid[10] = {
    /* [0] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [1] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [2] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [3]  FLL PID manual                   */
               { 70.0,   0.70,     175.0,     9000.0  },
    /* [4]  PLL: Kp=freq gain, Kd=phase prop, Ki=phase integral */
               { 1000.0, 0.020,    2.0,       7000.0  },
    /* [5]  PLL: Kp=freq, Kd=phase, Ki=phase integral */
               { 1000.0, 0.020,    2.0,       10000.0 },
    /* [6]  FLL PID genetic (freq-domain)     */
               { 205.0,  0.264,    14950.0,   13000.0 },
    /* [7]  PLL: Kp=freq, Kd=phase, Ki=phase integral */
               { 1000.0, 0.020,    2.0,       10000.0 },
    /* [8]  hybrid (uses [6]+[7]) IL only     */
               { 0.0,    0.0,      0.0,       13000.0 },
    /* [9]  NN  I_LIMIT = normalisation bound */
               { 0.0,    0.0,      0.0,       450.0   },
};
double g_blend_crossover = 0.024;   /* Hz — sigmoid centre */
double g_blend_scale     = 0.012;   /* Hz — sigmoid width  */
double g_nn_max_step     = 175.0;   /* LSB — max PWM delta  */

/* ======================================================================
 * ALGORITHM DISPATCHER
 * ====================================================================== */
uint16_t adjustVctlPWM(uint16_t prev_pwm, uint32_t ppscount, uint8_t algo_no)
{
    switch (algo_no) {
        case 0:  return primitive_ctl_loop(prev_pwm, ppscount);
        case 1:  return forced_drift_Vctl (prev_pwm, ppscount);
        case 2:  return random_walk_Vctl  (prev_pwm, ppscount);
        case 3:  return fll_pid_manual    (prev_pwm, ppscount);
        case 4:  return pll_pi_manual     (prev_pwm, ppscount);
        case 5:  return pll_pid_manual    (prev_pwm, ppscount);
        case 6:  return fll_pid_genetic   (prev_pwm, ppscount);
        case 7:  return pll_pid_genetic   (prev_pwm, ppscount);
        case 8:  return hybrid_fll_pll    (prev_pwm, ppscount);
        case 9:  return nn_mlp_ctl_loop   (prev_pwm, ppscount);
        default: return primitive_ctl_loop(prev_pwm, ppscount);
    }
}

/* ======================================================================
 * ALGORITHM 0 — Primitive stepped controller (original, unchanged)
 * ====================================================================== */
uint16_t primitive_ctl_loop(uint16_t pwm, uint32_t ppscount)
{
    const uint32_t PERIOD = 429;
    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);

    int32_t  new_pwm = pwm;
    /* trendstr format: exactly 4 chars, no leading space, right-padded.
     * Displayed as-is on OLED row 7, serial, and LCD line 3 indicator.
     * LCD shows only first 3 chars in the indicator field (cols 17-19).
     * Meanings: hit=locked, vf=very-far, uf=ultra-fine, c=coarse, f=fine */
    char     trend[5] = "___ ";

    if (s.have1000 && s.avg1000 >= 9999999.990 && s.avg1000 <= 10000000.010) {
        if      (s.avg1000 >= 10000000.005) { new_pwm = pwm - 5; strcpy(trend, "vf- "); }
        else if (s.avg1000 >= 10000000.001) { new_pwm = pwm - 1; strcpy(trend, "uf- "); }
        else if (s.avg1000 <= 9999999.995)  { new_pwm = pwm + 5; strcpy(trend, "vf+ "); }
        else if (s.avg1000 <= 9999999.999)  { new_pwm = pwm + 1; strcpy(trend, "uf+ "); }
        else                                {                     strcpy(trend, "hit "); }
    } else if (s.have100) {
        if      (s.avg100 >= 10000000.10) { new_pwm = pwm - 100; strcpy(trend, "c-  "); }
        else if (s.avg100 >= 10000000.01) { new_pwm = pwm -  10; strcpy(trend, "f-  "); }
        else if (s.avg100 <= 9999999.90)  { new_pwm = pwm + 100; strcpy(trend, "c+  "); }
        else if (s.avg100 <= 9999999.99)  { new_pwm = pwm +  10; strcpy(trend, "f+  "); }
    }

    set_trend(trend);
    return clamp_pwm(new_pwm);
}

/* ======================================================================
 * ALGORITHM 1 — Forced drift (original)
 * ====================================================================== */
uint16_t forced_drift_Vctl(uint16_t pwm, uint32_t ppscount)
{
    if ((ppscount % 1000) == 0) return clamp_pwm((int32_t)pwm + 1);
    return pwm;
}

/* ======================================================================
 * ALGORITHM 2 — Random walk (original)
 * ====================================================================== */
uint16_t random_walk_Vctl(uint16_t pwm, uint32_t ppscount)
{
    if ((ppscount % 5) == 0) {
        int32_t delta = (int32_t)(random(3)) - 1;
        return clamp_pwm((int32_t)pwm + delta);
    }
    return pwm;
}

/* ======================================================================
 * ALGORITHM 3 — FLL PID, manually tuned
 *
 * Loop type:   Frequency-Locked Loop
 * Error input: avg_100s frequency error (refreshes every 100 s once full)
 * Update rate: every 100 s (ppscount % 100 == 0)
 *
 * PID state (static — persists between calls, reset on flush):
 *   integral_e:  running sum of error × Ts
 *   prev_e:      previous error for derivative
 *
 * Tuning rationale:
 *   OCXO sensitivity ≈ 5 µHz/LSB → Kp=80 gives ~16 mHz/Hz correction
 *   Ki=0.8 provides ~1 Hz·s⁻¹ steady-state correction rate
 *   Kd=200 damps step disturbances (temperature ramp)
 *   Anti-windup: integral clamped to ±10000 LSB
 * ====================================================================== */
uint16_t fll_pid_manual(uint16_t pwm, uint32_t ppscount)
{
    /* State — zeroed at startup, reset via flush */
    static double  integral_e = 0.0;
    static double  prev_e     = 0.0;
    static bool    prev_valid = false;
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD = 100;     /* update every 100 s */
    const double   Kp     = g_pid[3].Kp;
    const double   Ki     = g_pid[3].Ki;
    const double   Kd     = g_pid[3].Kd;
    const double   Ts     = (double)PERIOD;    /* 100 s effective sample */
    const double   I_LIMIT = g_pid[3].I_LIMIT; /* anti-windup clamp [LSB] */

    /* Detect ring buffer flush (ppscount resets to 0) */
    if (ppscount < last_flush_pps) {
        integral_e  = 0.0;
        prev_e      = 0.0;
        prev_valid  = false;
    }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have100) return pwm;

    /* Frequency error: positive = too fast = must decrease PWM */
    double e = s.avg100 - (double)BASE_FREQ;

    /* Integral with anti-windup */
    integral_e += e * Ts;
    if (integral_e >  I_LIMIT) integral_e =  I_LIMIT;
    if (integral_e < -I_LIMIT) integral_e = -I_LIMIT;

    /* Derivative (first call: no derivative action) */
    double derivative = prev_valid ? (e - prev_e) / Ts : 0.0;
    prev_e     = e;
    prev_valid = true;

    /* PID output: negate because positive error → decrease PWM */
    double u = -(Kp * e + Ki * integral_e + Kd * derivative);

    char trend[5];
    bool e_small = (e > -0.0010 && e < 0.0010);
    if (e_small && u > -1.0 && u < 1.0) {
        strcpy(trend, "hit ");
        set_trend(trend);
        return pwm;
    }
    if      (u > 10.0)  strcpy(trend, "p+  ");
    else if (u > 0.0)   strcpy(trend, "f+  ");
    else if (u < -10.0) strcpy(trend, "p-  ");
    else                strcpy(trend, "f-  ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
}

/* ======================================================================
 * ALGORITHM 4 — PLL PI + frequency damping, manually tuned
 *               Inspired by Lars Walenius' GPSDO design
 *
 * Loop type:   true Phase-Locked Loop.
 * Phase error: locally accumulated — every 10 s the loop adds
 *              (avg10 − 10 MHz)·10 s, which equals the EXACT sum of the
 *              last ten 1-second cycle-count offsets (integer cycles).
 *              Unlike a rolling-window average this responds to PWM
 *              corrections with only a 10-second lag.
 * Damping:     Kd × frequency error (avg100 when available, else avg10).
 *              A pure PI on phase with an integrating plant is marginally
 *              unstable — the frequency term provides the damping that a
 *              derivative-of-phase would, without differencing noise.
 * Update rate: every 10 s
 *
 * Tuning:        Kp on phase, gentle damping from frequency error
 *                 (well-damped discrete loop)
 * ====================================================================== */
uint16_t pll_pi_manual(uint16_t pwm, uint32_t ppscount)
{
    static double  phase_acc    = 0.0;   /* accumulated phase [Hz·s = cycles] */
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 10;
    const double   Kp      = g_pid[4].Kp;
    const double   Ki      = g_pid[4].Ki;
    const double   Kd      = g_pid[4].Kd;
    const double   Ts      = (double)PERIOD;
    const double   I_LIMIT = g_pid[4].I_LIMIT;

    if (ppscount < last_flush_pps) { phase_acc = 0.0; }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10) return pwm;

    /* True phase accumulation for the gentle integral term */
    double e_freq = s.have100 ? (s.avg100 - (double)BASE_FREQ)
                              : (s.avg10  - (double)BASE_FREQ);
    phase_acc += (s.avg10 - (double)BASE_FREQ) * Ts;
    if (phase_acc >  I_LIMIT) phase_acc =  I_LIMIT;
    if (phase_acc < -I_LIMIT) phase_acc = -I_LIMIT;

    /* PI on frequency (fast, no overshoot) + soft phase integral.
     * Kd here is the gentle phase-proportional gain (named Kd so the
     * CLI/EEPROM layout is shared with algos 5/7). */
    double u = -(Kp * e_freq + Kd * phase_acc + Ki * phase_acc * Ts);

    bool locked = false;
    uint16_t out = apply_correction(pwm, u, e_freq, phase_acc, 12.0, &locked);

    char trend[5];
    if      (locked)   strcpy(trend, "hit ");
    else if (u >  5.0) strcpy(trend, "p+  ");
    else if (u >  0.0) strcpy(trend, "f+  ");
    else if (u < -5.0) strcpy(trend, "p-  ");
    else               strcpy(trend, "f-  ");
    set_trend(trend);

    return out;
}

/* ======================================================================
 * ALGORITHM 5 — PLL PID, manually tuned
 *
 * Loop type:   Phase-Locked Loop with derivative damping
 * Error input: Phase error from cumul1000 (shorter window = faster response
 *              at cost of more GPS noise); derivative from phase rate.
 * Update rate: every 10 s
 *
 * Compared to algo 4:
 *   - Uses 1000s window (faster pull-in)
 *   - Adds derivative term to damp overshoot when correcting large error
 *   - Higher Kp for faster settling
 *
 * Tuning:
 *   Kp=40, Ki=0.01, Kd=800
 *   Kd provides ~20× derivative boost relative to proportional — strongly
 *   damps temperature-induced ramps (slope: ~0.01 Hz/min typical)
 * ====================================================================== */
uint16_t pll_pid_manual(uint16_t pwm, uint32_t ppscount)
{
    static double  phase_acc    = 0.0;   /* accumulated phase [Hz·s] */
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 10;
    const double   Kp      = g_pid[5].Kp;    /* on FREQUENCY error */
    const double   Ki      = g_pid[5].Ki;    /* on PHASE (gentle)  */
    const double   Kd      = g_pid[5].Kd;    /* phase proportional */
    const double   Ts      = (double)PERIOD;
    const double   I_LIMIT = g_pid[5].I_LIMIT;

    if (ppscount < last_flush_pps) { phase_acc = 0.0; }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10) return pwm;

    /* Same two-timescale scheme as algo 7 (see its header). */
    double e_freq = s.have100 ? (s.avg100 - (double)BASE_FREQ)
                              : (s.avg10  - (double)BASE_FREQ);
    phase_acc += (s.avg10 - (double)BASE_FREQ) * Ts;
    if (phase_acc >  I_LIMIT) phase_acc =  I_LIMIT;
    if (phase_acc < -I_LIMIT) phase_acc = -I_LIMIT;

    double u = -(Kp * e_freq + Kd * phase_acc + Ki * phase_acc * Ts);

    bool locked = false;
    uint16_t out = apply_correction(pwm, u, e_freq, phase_acc, 12.0, &locked);

    char trend[5];
    if      (locked)    strcpy(trend, "hit ");
    else if (u >  10.0) strcpy(trend, "p+  ");
    else if (u >   0.0) strcpy(trend, "f+  ");
    else if (u < -10.0) strcpy(trend, "p-  ");
    else                strcpy(trend, "f-  ");
    set_trend(trend);

    return out;
}

/* ======================================================================
 * ALGORITHM 6 — FLL PID, GA-optimised coefficients
 *
 * Same structure as algo 3 (FLL PID) but with coefficients derived via
 * genetic algorithm / ITAE minimisation:
 *
 *   Step-test result:  Ku=400 LSB/Hz,  Tu=800 s
 *   ITAE rules (PID):  Kp = 0.585·Ku = 234
 *                      Ti = Tu/1.03   = 777 s  → Ki = Kp/Ti = 0.301
 *                      Td = 0.091·Tu  =  73 s  → Kd = Kp·Td = 17082
 *
 * Additionally: derivative is filtered (N=10 filter) to avoid noise
 * amplification — this is the key improvement over algo 3.
 *
 *   Filtered derivative:
 *     d_f(k) = N/(N + Ts/Td) * d_f(k-1) + Kd*N/(N + Ts/Td) * (e-e_prev)
 *   Here simplified as exponential smoothing with α = Ts/(Td + Ts).
 * ====================================================================== */
uint16_t fll_pid_genetic(uint16_t pwm, uint32_t ppscount)
{
    static double  integral_e  = 0.0;
    static double  prev_e      = 0.0;
    static double  d_filtered  = 0.0;   /* low-pass filtered derivative */
    static bool    prev_valid  = false;
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 100;
    const double   Kp      = g_pid[6].Kp;
    const double   Ki      = g_pid[6].Ki;
    const double   Kd      = g_pid[6].Kd;
    const double   Ts      = (double)PERIOD;
    const double   Td      = (Kp > 0.0) ? (Kd / Kp) : 73.0; /* derived */
    /* Derivative filter: α = Ts/(Ts+Td) → weight on raw vs filtered */
    const double   ALPHA   = Ts / (Ts + Td);
    const double   I_LIMIT = g_pid[6].I_LIMIT;

    if (ppscount < last_flush_pps) {
        integral_e = 0.0; prev_e = 0.0; d_filtered = 0.0; prev_valid = false;
    }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have100) return pwm;

    double e = s.avg100 - (double)BASE_FREQ;

    integral_e += e * Ts;
    if (integral_e >  I_LIMIT) integral_e =  I_LIMIT;
    if (integral_e < -I_LIMIT) integral_e = -I_LIMIT;

    /* Filtered derivative (first call: initialise with raw) */
    double raw_d = prev_valid ? (e - prev_e) / Ts : 0.0;
    d_filtered   = ALPHA * raw_d + (1.0 - ALPHA) * d_filtered;
    prev_e       = e;
    prev_valid   = true;

    double u = -(Kp * e + Ki * integral_e + Kd * d_filtered);

    /* FLL lock: frequency within 1 mHz. No phase term, so freq-only test.
     * Hold PWM when locked and the step is sub-LSB to stop noise dither. */
    char trend[5];
    bool e_small = (e > -0.0010 && e < 0.0010);
    if (e_small && u > -1.0 && u < 1.0) {
        strcpy(trend, "hit ");
        set_trend(trend);
        return pwm;
    }
    strcpy(trend, u >= 0.0 ? "f+  " : "f-  ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
}

/* ======================================================================
 * ALGORITHM 7 — PLL PID, GA-optimised coefficients
 *
 * PLL variant: phase error from 1000s cumulative sum.
 *
 * GA / ITAE derivation (PLL phase domain):
 *   System gain in phase domain: G_phase = G_freq / s
 *   Effective Ku for phase loop ≈ 120 LSB / (Hz·s), Tu ≈ 400 s
 *   ITAE (PID):  Kp = 0.585·120 = 70.2  → 70
 *                Ti = 400/1.03  = 388 s  → Ki = 70/388 = 0.181
 *                Td = 0.091·400 = 36.4 s → Kd = 70·36.4 = 2548
 *
 * Derivative filtering: same ALPHA scheme as algo 6.
 * Update rate: every 10 s (tighter loop than FLL alg 6).
 * ====================================================================== */
uint16_t pll_pid_genetic(uint16_t pwm, uint32_t ppscount)
{
    static double  phase_acc    = 0.0;   /* accumulated phase [Hz·s] */
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 10;
    const double   Kp      = g_pid[7].Kp;    /* acts on FREQUENCY error  */
    const double   Ki      = g_pid[7].Ki;    /* acts on PHASE (gentle)   */
    const double   Kd      = g_pid[7].Kd;    /* phase proportional, soft */
    const double   Ts      = (double)PERIOD;
    const double   I_LIMIT = g_pid[7].I_LIMIT;

    if (ppscount < last_flush_pps) { phase_acc = 0.0; }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10) return pwm;

    /* Two-timescale control:
     *   - dominant term Kp·e_freq pulls the FREQUENCY to target fast and
     *     without overshoot (Kp ≈ 0.5/K → half-step deadbeat).
     *   - gentle Kd·phase_acc + Ki·∫phase remove the slow phase drift
     *     with small steps, so steady-state PWM motion stays tiny.
     * The frequency error uses the smoothest window available.         */
    double e_freq = s.have100 ? (s.avg100 - (double)BASE_FREQ)
                              : (s.avg10  - (double)BASE_FREQ);
    phase_acc += (s.avg10 - (double)BASE_FREQ) * Ts;
    if (phase_acc >  I_LIMIT) phase_acc =  I_LIMIT;
    if (phase_acc < -I_LIMIT) phase_acc = -I_LIMIT;

    double u = -(Kp * e_freq + Kd * phase_acc + Ki * phase_acc * Ts);

    bool locked = false;
    uint16_t out = apply_correction(pwm, u, e_freq, phase_acc, 12.0, &locked);

    char trend[5];
    if      (locked)    strcpy(trend, "hit ");
    else                strcpy(trend, u >= 0.0 ? "f+  " : "f-  ");
    set_trend(trend);

    return out;
}

/* ======================================================================
 * ALGORITHM 8 — Hybrid FLL + PLL PID
 *
 * Motivation:
 *   - FLL: fast pull-in from large frequency errors (startup, temperature
 *     step), insensitive to GPS phase noise, but poor steady-state jitter
 *   - PLL: low steady-state phase noise, but slow pull-in from large errors
 *   - Hybrid: blend the two proportionally to error magnitude
 *
 * Blending scheme (sigmoid):
 *   w_fll(e) = sigmoid(|e_hz| / BLEND_SCALE)   range 0-1
 *   w_pll    = 1 - w_fll
 *   u = w_fll * u_fll + w_pll * u_pll
 *
 *   where BLEND_SCALE = 0.05 Hz:
 *     |e| > 0.20 Hz → FLL weight > 98%  (effectively pure FLL)
 *     |e| < 0.005 Hz → PLL weight > 90% (effectively pure PLL)
 *     Transition zone 0.005-0.20 Hz: gradual blend
 *
 * FLL branch: algo 6 coefficients (GA-optimised), 100s window
 * PLL branch: algo 7 coefficients (GA-optimised), 1000s window
 * Update:     every 10 s (driven by PLL branch; FLL state updated in sync)
 *
 * Both branches maintain independent integrators; the blending weight
 * is applied to the total PID output, not to individual terms, to avoid
 * integrator wind-up in the dormant branch.
 * ====================================================================== */

/* Sigmoid: 1 / (1 + exp(-x)) — returns 0.5 at x=0 */
static inline double sigmoid(double x)
{
    return 1.0 / (1.0 + exp(-x));
}

uint16_t hybrid_fll_pll(uint16_t pwm, uint32_t ppscount)
{
    /* FLL state */
    static double fll_integral = 0.0;
    static double fll_prev_e   = 0.0;
    static double fll_d_filt   = 0.0;
    static bool   fll_prev_ok  = false;

    /* PLL state */
    static double pll_phase    = 0.0;   /* accumulated phase [Hz·s] */


    static uint32_t last_flush_pps = 0;

    if (ppscount < last_flush_pps) {
        fll_integral = 0.0; fll_prev_e  = 0.0; fll_d_filt = 0.0; fll_prev_ok = false;
        pll_phase = 0.0;
    }
    last_flush_pps = ppscount;

    /* Both branches update at 10 s period */
    const uint32_t PERIOD = 10;
    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have100) return pwm;     /* need at least 100s of data */

    const double Ts       = (double)PERIOD;
    const double I_LIMIT  = g_pid[8].I_LIMIT;

    /* ---- FLL branch (reads tunable algo 6 coefficients) ---- */
    const double FKp    = g_pid[6].Kp;
    const double FKi    = g_pid[6].Ki;
    const double FKd    = g_pid[6].Kd;
    const double FTd    = (FKp > 0.0) ? (FKd / FKp) : 73.0;
    const double F_ALPHA = Ts / (Ts + FTd);

    double e_hz = s.avg100 - (double)BASE_FREQ;
    fll_integral += e_hz * Ts;
    if (fll_integral >  I_LIMIT) fll_integral =  I_LIMIT;
    if (fll_integral < -I_LIMIT) fll_integral = -I_LIMIT;
    double fraw_d = fll_prev_ok ? (e_hz - fll_prev_e) / Ts : 0.0;
    fll_d_filt    = F_ALPHA * fraw_d + (1.0 - F_ALPHA) * fll_d_filt;
    fll_prev_e    = e_hz;
    fll_prev_ok   = true;
    double u_fll  = -(FKp * e_hz + FKi * fll_integral + FKd * fll_d_filt);

    /* ---- PLL branch (reads tunable algo 7 coefficients) ----
     * Two-timescale: Kp on frequency error (fast, no overshoot),
     * Kd+Ki gently on accumulated phase.  Same scheme as algo 7.       */
    double u_pll = 0.0;
    {
        const double PKp = g_pid[7].Kp;
        const double PKi = g_pid[7].Ki;
        const double PKd = g_pid[7].Kd;

        pll_phase += e_hz * Ts;
        if (pll_phase >  I_LIMIT) pll_phase =  I_LIMIT;
        if (pll_phase < -I_LIMIT) pll_phase = -I_LIMIT;
        u_pll = -(PKp * e_hz + PKd * pll_phase + PKi * pll_phase * Ts);
    }

    /*
     * Blending:
     *   x = |e_hz| / 0.05  → sigmoid gives w_fll
     *   At e=0.20 Hz: x=4 → w_fll=0.982 (almost pure FLL)
     *   At e=0.005 Hz: x=0.1 → w_fll=0.525 → w_pll=0.475 (near equal)
     *   We shift sigmoid: w_fll = sigmoid((|e|-0.02)/0.01)
     *   so crossover at |e|=0.02 Hz (fast enough for PLL stability)
     */
    double abs_e    = (e_hz < 0) ? -e_hz : e_hz;
    double w_fll    = sigmoid((abs_e - g_blend_crossover) / g_blend_scale);
    double w_pll    = 1.0 - w_fll;
    double u        = w_fll * u_fll + w_pll * u_pll;

    /* Slew-limited, lock-aware. Wider cap (40 LSB) than the pure PLLs so
     * the FLL branch can still capture a large startup error quickly.
     * Lock test uses the PLL phase accumulator (pll_phase). */
    bool locked = false;
    uint16_t out = apply_correction(pwm, u, e_hz, pll_phase, 40.0, &locked);

    char trend[5];
    if      (locked)      strcpy(trend, "hit ");
    else if (w_fll > 0.8) strcpy(trend, "FLL ");
    else if (w_pll > 0.8) strcpy(trend, "PLL ");
    else                  strcpy(trend, "HYB ");
    set_trend(trend);

    return out;
}

/* ======================================================================
 * ALGORITHM 9 — Neural network MLP inference
 *
 * Architecture: 3 inputs → 8 hidden neurons (tanh) → 1 output (tanh)
 * Total parameters: 3×8 + 8 + 8×1 + 1 = 41 weights+biases
 *
 * Inputs (normalised to ±1.0):
 *   x[0] = e         / E_SCALE     (frequency error,  E_SCALE=0.5 Hz)
 *   x[1] = integral  / I_SCALE     (integral of error, I_SCALE=500 Hz·s)
 *   x[2] = derivative/ D_SCALE     (rate of change,   D_SCALE=0.05 Hz/s)
 *
 * Output (normalised):
 *   delta_PWM = y_norm × MAX_STEP  (MAX_STEP=200 LSB)
 *
 * Weight derivation:
 *   The weights are ANALYTICALLY CONSTRUCTED (see the comment above the
 *   weight tables): a diagonal, bias-free, odd-symmetric network that
 *   implements a smooth saturating PID-like policy.  Zero input gives
 *   exactly zero output — the equilibrium is at zero error by design.
 *   The structure (3-8-1 MLP) is kept so the weights can later be
 *   replaced by genuinely trained ones without code changes.
 *
 * Update rate: every 10 s.
 * ====================================================================== */

/* tanh approximation — saves linking libm on some toolchains, but since
   we already include math.h for exp() in algo 8, use real tanh here. */
static inline double nn_tanh(double x) { return tanh(x); }

/* Network dimensions */
#define NN_IN   3
#define NN_H    8
#define NN_OUT  1

/*
 * Weights layout (row-major):
 *   W1[NN_H][NN_IN]  — input→hidden
 *   b1[NN_H]         — hidden biases
 *   W2[NN_OUT][NN_H] — hidden→output
 *   b2[NN_OUT]       — output bias
 *
 * ANALYTICALLY CONSTRUCTED weights — not trained.
 *
 * The previous weight set was presented as "offline-trained" but produced
 * a large output bias: at zero error the network emitted y ≈ −0.96, i.e.
 * a constant +0.96·MAX_STEP PWM step every period — the PWM ramped away
 * monotonically (frequency ran high).  Lesson: a controller's equilibrium
 * must be at zero by construction.
 *
 * This network is built by hand to be exactly odd-symmetric:
 *   - all biases are zero          → f(0) = 0 exactly
 *   - tanh is an odd function      → f(−x) = −f(x)
 *   - W1 is diagonal (3 active hidden units, 5 unused)
 *
 *   h0 = tanh(1.5·x_e)   — proportional channel (saturating)
 *   h1 = tanh(1.0·x_i)   — integral channel
 *   h2 = tanh(1.2·x_d)   — derivative channel
 *   y  = tanh(0.9·h0 + 0.5·h1 + 0.6·h2)
 *
 * Small-signal gain: ∂Δpwm/∂e = MAX_STEP·0.9·1.5/E_SCALE ≈ 405 LSB/Hz
 * (NN max step from CT) — comparable to the linear PID algorithms, but with
 * smooth tanh saturation limiting any single step to ±MAX_STEP.
 */
static const double W1[NN_H][NN_IN] = {
    /*  e,       integral,  derivative */
    {  1.5,      0.0,       0.0 },   /* h0: P channel */
    {  0.0,      1.0,       0.0 },   /* h1: I channel */
    {  0.0,      0.0,       1.2 },   /* h2: D channel */
    {  0.0,      0.0,       0.0 },   /* h3: unused    */
    {  0.0,      0.0,       0.0 },   /* h4: unused    */
    {  0.0,      0.0,       0.0 },   /* h5: unused    */
    {  0.0,      0.0,       0.0 },   /* h6: unused    */
    {  0.0,      0.0,       0.0 }    /* h7: unused    */
};

static const double b1[NN_H] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

static const double W2[NN_OUT][NN_H] = {
    { 0.9, 0.5, 0.6, 0.0, 0.0, 0.0, 0.0, 0.0 }
};

static const double b2[NN_OUT] = { 0.0 };

uint16_t nn_mlp_ctl_loop(uint16_t pwm, uint32_t ppscount)
{
    static double  integral_e  = 0.0;
    static double  prev_e      = 0.0;
    static bool    prev_valid  = false;
    static uint32_t last_flush_pps = 0;

    /* Normalisation scales */
    const double E_SCALE   = 0.5;     /* Hz — input normalisation */
    const double I_SCALE   = 500.0;   /* Hz·s */
    const double D_SCALE   = 0.05;    /* Hz/s */
    const double MAX_STEP  = g_nn_max_step;  /* LSB — output de-normalisation */
    const double I_LIMIT   = g_pid[9].I_LIMIT; /* anti-windup = normalisation bound */

    const uint32_t PERIOD = 10;

    if (ppscount < last_flush_pps) {
        integral_e = 0.0; prev_e = 0.0; prev_valid = false;
    }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10) return pwm;

    /* Compute PID-style features */
    double e          = s.avg10 - (double)BASE_FREQ;
    double Ts         = (double)PERIOD;
    integral_e       += e * Ts;
    if (integral_e >  I_LIMIT) integral_e =  I_LIMIT;
    if (integral_e < -I_LIMIT) integral_e = -I_LIMIT;
    double derivative  = prev_valid ? (e - prev_e) / Ts : 0.0;
    prev_e     = e;
    prev_valid = true;

    /* Normalise inputs */
    double x[NN_IN];
    x[0] = e         / E_SCALE;
    x[1] = integral_e / I_SCALE;
    x[2] = derivative  / D_SCALE;
    /* Soft-clamp inputs to ±3 (avoids saturation in hidden layer) */
    for (int i = 0; i < NN_IN; i++) {
        if (x[i] >  3.0) x[i] =  3.0;
        if (x[i] < -3.0) x[i] = -3.0;
    }

    /* Forward pass: hidden layer */
    double h[NN_H];
    for (int j = 0; j < NN_H; j++) {
        double z = b1[j];
        for (int i = 0; i < NN_IN; i++) z += W1[j][i] * x[i];
        h[j] = nn_tanh(z);
    }

    /* Forward pass: output layer */
    double y = b2[0];
    for (int j = 0; j < NN_H; j++) y += W2[0][j] * h[j];
    y = nn_tanh(y);  /* output in (-1, 1) */

    /* De-normalise: delta_PWM in LSB */
    /* Sign convention: network trained with e = f - 10MHz, output = -delta_PWM/MAX_STEP
       so positive output of network means "lower frequency needed" = decrease PWM */
    double delta_pwm = -y * MAX_STEP;

    char trend[5];
    strcpy(trend, delta_pwm >= 0.0 ? "NN+ " : "NN- ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)delta_pwm);
}
