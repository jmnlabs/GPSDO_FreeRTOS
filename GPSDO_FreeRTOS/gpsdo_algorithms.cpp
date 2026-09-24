/**
 * gpsdo_algorithms.cpp — Control loop algorithm implementations
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
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

#include "gpsdo_algorithms.h"
#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include "gpsdo_dac.h"
#include "gpsdo_ubx_timtp.h"
#include "gpsdo_flash_ring.h"
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
    /* Instantaneous count error this second, TIM2 ticks from BASE_FREQ.
     * Algorithm 12 accumulates these one at a time; every other algorithm uses
     * the averages above. int16_t: FREQ_LOWER/UPPER admit ±500 Hz and the old
     * int8_t wrapped past ±127, feeding garbage to any gate that read it. */
    int16_t  instant_offset;
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
        s->instant_offset = gFreq.instant_offset;
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

/* ---- the fine control value, published for the control task ----------------
 *
 * adjustVctlPWM() returns uint16_t and that signature is load-bearing: the
 * control task, the displays, the telemetry line and the flash ring all speak
 * 16 bits, and widening the return would touch every one of them. So the exact
 * value rides alongside instead.
 *
 * g_vctl_fine_valid is cleared at the top of every adjustVctlPWM() call and set
 * only by an algorithm that actually computed a fractional target this cycle.
 * A stale fraction is therefore impossible: if the algorithm held (returned pwm
 * unchanged), or if it is one that does not do fractional arithmetic, the
 * control task falls back to the plain 16-bit write it has always done. */
double g_vctl_fine       = 0.0;
bool   g_vctl_fine_valid = false;

/* The value an output stage should add its correction to.
 *
 * NOT the uint16_t pwm the caller passed in — that is gCtrl.pwm_output, which
 * is the rounded view. Adding to it would discard the fraction once per cycle
 * and leave the whole fine path buying nothing.
 *
 * The DAC layer's value is used only when it agrees with what the caller thinks
 * the PWM is, to within one step. If anything else has moved the output since
 * the last loop cycle — a CT sweep, an SP from the CLI, a holdover step — they
 * will disagree, and then the caller's value is the truth and the fraction is
 * correctly abandoned. */
static double fine_base(uint16_t pwm)
{
    if (!gpsdo_dac_fine_available()) return (double)pwm;
    double f = gpsdo_dac_last16f();
    double d = f - (double)pwm;
    if (d < -1.0 || d > 1.0) return (double)pwm;
    return f;
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
/* ===================== self-learning loop aid (LRN) =====================
 * Two SLOW, passive learners shared by algo 7 and LTIC. Neither injects any
 * excitation — they only observe the phase the loop already produces, so they
 * cannot destabilise it. Both are gated to run ONLY when the loop is locked,
 * update at most once per LRN_TICK seconds, and are clamped to narrow bands
 * around the theoretical values.
 *
 *  (1) DRIFT feed-forward: the OCXO ages/thermally drifts, so the phase ramps
 *      steadily between corrections (Dan's overnight trace: a ~9000 s ±80 ns
 *      sawtooth, an 8E-12/day drift the integrator kept chasing). We estimate
 *      the mean phase slope over a long window and add a constant PWM term
 *      that cancels it — the loop stops chasing a moving target and the phase
 *      goes flat.  d_ppm accumulates in PWM LSB.
 *  (2) DAMPING adaption: an ADEV bump at the loop time constant (Dan: ~1.4e-11
 *      at 80-100 s) means the proportional gain is a touch high — the phase
 *      overshoots zero after each correction. We watch zero-crossings of the
 *      phase error: overshoot (crossing with growing amplitude) → nudge the
 *      damping factor down; sluggish (no crossing, slow approach) → up.
 *
 * Persistence: learned drift term and damping factor are saved by ES and
 * recalled at boot; LRN 0 freezes learning, LRN R resets to theory. */
bool     g_lrn_enable   = true;          /* LRN 0/1 (EEPROM byte 222)        */
float    g_lrn_drift    = 0.0f;          /* learned feed-forward, PWM LSB     */
float    g_lrn_damp     = 1.0f;          /* damping multiplier, 0.5..1.5      */

#define LRN_TICK        30               /* seconds between learner updates    */
#define LRN_DRIFT_MAX   400.0f           /* |feed-forward| clamp, LSB          */
/* LRN_DAMP_LO / LRN_DAMP_HI live in gpsdo_algorithms.h (shared with
 * live_store for clamping restored values). The floor was raised 0.30 → 0.45
 * when a too-hard floor was found to starve the loop of correction authority
 * on a wide HC74 detector (phase ran 11→−425 ns in 51 s at damp 0.30 and
 * dropped LOCK→DPLL→ACQ). 0.45 still damps a limit cycle but keeps enough gain
 * to track a real OCXO drift. */

/* Live telemetry (printed by the report task when learning is active). */
float    g_lrn_slope_ns_s = 0.0f;        /* observed mean phase slope, ns/s    */
uint16_t g_lrn_osc_period = 0;           /* observed limit-cycle period, s     */
float    g_lrn_osc_amp_ns = 0.0f;        /* observed limit-cycle amplitude, ns */

/* Feed one locked-loop phase sample (ns) to the learners. dt = seconds since
 * the previous call. Returns the drift feed-forward to add to PWM (already
 * damped and clamped). Safe to call every PPS; internally rate-limited. */
static float lrn_update_ef(double e_freq, double phase_ns, double dt, bool locked)
{
    static double ef_win = 0.0;    static double win_t = 0.0;
    static double last_cross_t = 0.0, run_t = 0.0;
    static double last_ext = 0.0;  static int8_t  last_sign = 0;
    static bool   armed = false;
    static uint8_t ff_boot = 0;    /* fast feed-forward windows after (re)lock */

    if (!g_lrn_enable || !locked) { armed = false; return g_lrn_drift; }

    run_t += dt;
    /* ---- (1) drift feed-forward: slow integral of the FREQUENCY error ----
     * The feed-forward exists to cancel the OCXO's systematic drift so the
     * main loop's integrator does not have to chase it. We accumulate e_freq
     * over LRN_TICK windows and nudge the feed-forward to drive that mean
     * toward zero. Because e_freq already reflects the feed-forward we are
     * applying, this is inherently closed-loop: once the drift is absorbed
     * the mean e_freq is ~0 and the feed-forward stops moving. A deadband
     * (0.2 mHz) and a small per-tick step prevent wind-up and hunting. */
    ef_win += e_freq; win_t += dt;
    /* Feed-forward window and step: normally slow (30 s, 25 % of residual) to
     * stay quiet in steady state. But right after lock the drift is unknown
     * and the phase can run away before a 30 s window even closes — on air the
     * phase reached −425 ns (past the ACQ threshold) in 51 s while the learner
     * was still gathering its first window, so lock was lost before the
     * feed-forward ever moved. BOOTSTRAP: for the first few windows after lock,
     * use a short 8 s window and take a larger 60 % step, so the drift is
     * absorbed within ~10–20 s; then relax to the slow, quiet regime. */
    double tick = (ff_boot > 0) ? 8.0  : (double)LRN_TICK;
    double gain = (ff_boot > 0) ? 0.60 : 0.25;
    if (win_t >= tick) {
        double mean_ef = ef_win / win_t;          /* Hz, residual after ff  */
        g_lrn_slope_ns_s = (float)(mean_ef * 1.0e9 / (double)BASE_FREQ); /* ns/s */
        if (fabs(mean_ef) > 2.0e-4) {             /* 0.2 mHz deadband       */
            double lsbhz = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40) : 3000.0;
            /* u = -(…): positive e_freq (freq high) must LOWER pwm, so the
             * feed-forward carries -e_freq. Move a fraction of the residual. */
            double corr = -mean_ef * lsbhz;
            g_lrn_drift += (float)(gain * corr);
            if (g_lrn_drift >  LRN_DRIFT_MAX) g_lrn_drift =  LRN_DRIFT_MAX;
            if (g_lrn_drift < -LRN_DRIFT_MAX) g_lrn_drift = -LRN_DRIFT_MAX;
        }
        if (ff_boot > 0) ff_boot--;
        ef_win = 0.0; win_t = 0.0;
    }

    /* ---- (2) damping adaption from the phase limit cycle ----
     * Amplitude threshold is SCALED to the detector range: a fixed 5 ns limit
     * treats a wide-detector HC74 (~1650 ns span, ADC-noise floor ~50 ns) as a
     * perpetual oscillator and floors damping at LRN_DAMP_LO forever — exactly
     * what was seen on air (damp stuck at 0.5, drift hunting). Threshold =
     * 3 % of the measured range (clamped 5..150 ns) is the ADC + qErr noise
     * floor on a wide detector and a real oscillation on a narrow one. */
    float amp_thr = (g_ltic.range_ns > 1.0f) ? 0.03f * g_ltic.range_ns : 5.0f;
    if (amp_thr < 5.0f)   amp_thr = 5.0f;
    if (amp_thr > 150.0f) amp_thr = 150.0f;
    if (armed) {
        int8_t sgn = (phase_ns > amp_thr * 0.2) ? 1 : (phase_ns < -amp_thr * 0.2) ? -1 : 0;
        if (sgn != 0 && last_sign != 0 && sgn != last_sign) {
            double period = run_t - last_cross_t;
            if (period > 4.0) {
                g_lrn_osc_period = (uint16_t)(2.0 * period);
                g_lrn_osc_amp_ns = (float)fabs(last_ext);
                float amp = fabs(last_ext);
                if (amp > amp_thr) {
                    /* overshoot grows → damp more. Step grows with how far
                     * over the noise floor the cycle swings, but is capped so a
                     * single noisy crossing cannot collapse damping. */
                    float step = 0.01f + 0.0005f * (amp - amp_thr);
                    if (step > 0.10f) step = 0.10f;
                    g_lrn_damp -= step;
                } else {
                    /* cycle is inside the noise floor → relax damping back up
                     * toward unity, gently, so a quiet loop isn't over-damped. */
                    g_lrn_damp += 0.01f;
                }
                if (g_lrn_damp < LRN_DAMP_LO) g_lrn_damp = LRN_DAMP_LO;
                if (g_lrn_damp > LRN_DAMP_HI) g_lrn_damp = LRN_DAMP_HI;
                last_cross_t = run_t; last_ext = 0.0;
            }
        }
        if (fabs(phase_ns) > fabs(last_ext)) last_ext = phase_ns;
        if (sgn != 0) last_sign = sgn;
    } else {
        armed = true; last_cross_t = run_t; last_ext = 0.0; last_sign = 0;
        ff_boot = 3;    /* three fast 8 s windows to absorb drift after lock */
    }
    return g_lrn_drift;
}

/* Convenience wrapper: apply learning to any classic algorithm (3-8) in one
 * line. Pass the loop's correction u, its accumulated phase (Hz·s), the
 * frequency error (Hz) and the update period Ts. Returns the adjusted u
 * (damped + drift feed-forward). Locked = frequency on target. Algorithms
 * without a phase accumulator can pass 0 for phase_hz_s (drift learner then
 * simply sees no slope and stays neutral). */
static double lrn_apply(double u, double phase_hz_s, double e_freq, double Ts)
{
    bool lk = (fabs(e_freq) < 0.02);
    /* feed-forward is driven by the FREQUENCY error, not the (clamped)
     * phase accumulator: when the feed-forward has absorbed the OCXO drift,
     * e_freq → 0 on its own, which is the closed-loop signal that stops the
     * feed-forward growing. phase_hz_s is passed only for the damping/limit-
     * cycle observer (amplitude/period). */
    float ff = lrn_update_ef(e_freq, phase_hz_s, Ts, lk);
    return u * (double)g_lrn_damp + (double)ff;
}

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

    /* Keep the fraction. (int32_t)u truncated toward zero, so a correction of
     * 6.7 LSB was applied as 6 and the missing 0.7 was thrown away — in the
     * same direction every time, which the loop cannot distinguish from a gain
     * error. Accumulating it instead is what makes a run of sub-LSB corrections
     * add up to a real one. */
    double target = fine_base(pwm) + u;
    g_vctl_fine       = target;
    g_vctl_fine_valid = true;

    return clamp_pwm((int32_t)(target + (target < 0.0 ? -0.5 : 0.5)));
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

/* The coefficient set CT derives from K, exactly as it always has — moved here
 * from do_calibrate_tune() so that the span jumper (gpsdo_span.cpp), which has
 * to produce the same set from a STORED K, calls the same lines rather than a
 * copy of them. The formulas are the ones documented above.
 *
 * Ki and Kd of the PLL algorithms (4, 5, 7) are fixed and do NOT follow K. That
 * is CT's long-standing choice for those three, not something decided here: a
 * span move gives exactly what CT in that span would give, no more. */
void algo_coeffs_from_k(double K)
{
    if (!(K > 0.0)) return;
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
}

/* Algorithm 10 (LTIC three-stage PLL) defaults. Calibration fields start at 0
 * (uncalibrated) — the loop must not run until they are set on real hardware.
 * The PID/threshold defaults are plausible starting points to be tuned once
 * the TIC is characterised; they are NOT validated (no hardware yet). */
LticParams_t g_ltic = {
    /* ns_per_volt */ 0.0f,    /* 0 = uncalibrated */
    /* zero_offset */ 0.0f,
    /* range_ns    */ 0.0f,    /* 0 = unknown; loop must refuse to run */
    /* acq   */ { 1.20, 0.02, 0.0, 8000.0 },   /* coarse, frequency-led  */
    /* dpll  */ { 0.40, 0.02, 2.0, 5000.0 },   /* wide-band, fast settle */
    /* lock  */ { 0.05, 0.001, 0.0, 2000.0 },  /* narrow-band, slow      */
    /* acq_threshold_ns */ 100.0f,
    /* dpll_lock_thresh */ 5.0e-10f,
    /* lock_interval_s  */ 300u,
    /* state   */ LTIC_ACQ,
    /* submode */ 0u,
    /* polarity*/ 0,          /* 0 = auto-detect PWM→phase sign */
    /* centre_v*/ 0.0f,       /* 0 = use detected range middle  */
};

/* Algorithm 11 (LTIC-Lars) parameters. gain 0 = auto from CT calibration; a
 * non-zero gain set with LG overrides the auto value with a manual scale. */
LarsParams_t g_lars = {
    /* gain         */ 0.0f,    /* 0 = auto from CT calibration; LG sets manual */
    /* damping      */ 3.0f,    /* Lars' default                                */
    /* time_const_s */ 60u,     /* ~ limit-cycle period / 6                      */
    /* filter_div   */ 2u,      /* pre-filter = time_const / 2                   */
    /* tic_offset   */ 2620u,   /* ~2.11 V at 3.3 V / 4096 (mid-band from log)   */
    /* lock_ns_lim  */ 100u,    /* Lars' default phase window                    */
    /* lock_factor  */ 5u,      /* Lars' default                                */
    /* temp_coeff   */ 0,       /* off                                          */
    /* temp_ref     */ 0u,      /* set from BMP at enable time                   */
    /* flags        */ 0u,      /* temp comp disabled                           */
    /* reserved     */ { 0, 0, 0, 0, 0, 0 },
};

/* Algo 11 live telemetry (see header). Published by ltic_lars_pi each cycle. */
float g_lars_scale      = 0.0f;
float g_lars_phase_filt = 0.0f;
bool  g_lars_locked     = false;
bool  g_lars_gain_auto  = true;

/* ======================================================================
 * ALGORITHM 10 — LTIC three-stage PLL (ACQ → DPLL → LOCK)
 *
 * Disciplines the OCXO using the hardware TIC phase voltage (PA1), which
 * resolves phase far finer than the TIM2 cycle counter. A hybrid design:
 * the COARSE stages lean on the TIM2 frequency error (robust, no wrap), and
 * the fine stages lean on the LTIC phase (high resolution). State machine:
 *
 *   ACQ   frequency-led pull-in (TIM2). Get the OCXO close to 10 MHz so the
 *         phase ramps slowly and the detector can be caught. picDIV is armed
 *         on entry. Exit when |phase| is inside acq_threshold_ns for a few
 *         cycles.
 *   DPLL  both terms: Kp·e_freq (TIM2, fast) + phase PI (LTIC). Centre the
 *         phase quickly. Exit to LOCK when |phase| is small AND drift is low.
 *   LOCK  phase-led (LTIC), slow updates every lock_interval_s, narrow band.
 *         Exit back to DPLL if |phase| exceeds a hysteresis band persistently.
 *
 * Phase is taken from g_ltic_voltage. If calibrated (ns_per_volt != 0) the
 * loop works in nanoseconds against zero_offset; if not, it falls back to a
 * volts-based error around mid-rail and a nominal scale (a warning is printed
 * once). State persists in g_ltic.state so a warm reboot (RB) resumes where it
 * left off instead of restarting cold from ACQ.
 *
 * This is our own design; it borrows the staged structure and the bumpless
 * idea from the time-nuts discussion but works in calibrated units with the
 * project's existing snapshot/clamp/trend helpers.
 * ====================================================================== */
/* ---- ALGORITHM RESTART ----------------------------------------------------
 *
 * Every loop keeps private state across calls — integrators, filter memories,
 * hold-off counters, "have I warned about this yet" flags, the picDIV arming
 * bookkeeping. None of it was ever cleared when the operator changed algorithm,
 * so switching away and back resumed mid-flight with numbers from whenever the
 * loop last ran. Reported from the bench: 11 (locked) -> 12 sat at zero
 * corrections; algorithm 12 had been left with s_mla_returning set, and that
 * flag suppresses BOTH the limit path and the scheduled one, for the 300
 * algorithm-12 seconds its timeout needs to expire — which only tick while
 * algorithm 12 is the one running.
 *
 * That is the loud case. The quiet ones are worse: algorithm 10's `integ` is an
 * absolute PWM target, so re-entering it applies a control voltage chosen for
 * conditions an hour old, and `prev_state` still holding LOCK means the entry
 * transition never fires and the picDIV is never armed.
 *
 * The dispatcher notices the change and every loop clears its own state on the
 * next call. Each does it itself because only it knows what its statics mean —
 * a central reset would have to reach into three functions' locals and would
 * rot the first time one of them gained a variable. */
static bool s_algo_restart = false;

/* True exactly once per restart, to the first loop that asks. */
static bool algo_take_restart(void)
{
    if (!s_algo_restart) return false;
    s_algo_restart = false;
    return true;
}

#ifdef GPSDO_LTIC

/* Convert the latest TIC voltage to a signed phase error.
 *   calibrated:   error_ns = (V - zero_offset) * ns_per_volt
 *   uncalibrated: error is measured against zero_offset if LC has at least set
 *                 it (mid of the swept range), else against a coarse mid-rail.
 * The key point learned on real hardware: the detector's working range may be
 * a narrow band well away from mid-ADC (e.g. 0..0.45 V), so we must NOT assume
 * 1.65 V is the centre — we use the calibrated zero_offset. Returns phase
 * error; *valid is false if the reading is railed (outside the detector
 * window), which the loop treats specially. */
/* Damping-term frequency error, windowed per the FA command. Reads the average
 * the caller passes in (10/100/1000), falling back to the smooth default
 * (e_freq, i.e. avg100) whenever the requested window is not full yet. With the
 * default 100 it returns bit-for-bit e_freq: the whole point is that a window of
 * 100 changes nothing.
 *
 * Called once per state — DPLL and LOCK pass their own windows (FAD / FAL) — so
 * the frequency term can be damped differently in acquisition and steady state.
 * Escape detection, self-learning, the state machine and the phase PI all keep
 * the smooth avg100 via e_freq, where a noisier average would cost more than it
 * gains. */
static double damp_e_freq(const FreqSnapshot_t *s, double e_freq_default,
                          uint16_t win)
{
    switch (win) {
        case 10:   return s->have10   ? (s->avg10   - (double)BASE_FREQ) : e_freq_default;
        case 1000: return s->have1000 ? (s->avg1000 - (double)BASE_FREQ) : e_freq_default;
        case 100:
        default:   return e_freq_default;   /* identical to the historical path */
    }
}

static double ltic_phase_error_ns(bool *valid, uint32_t ppscount)
{
    float v = g_ltic_voltage;
    *valid = (v > 0.02f && v < 3.28f);          /* not railed low/high */

    /* SATURATION GUARD for the Kaashoek RC detector. The voltage-to-phase
     * mapping is only linear over the swept band measured by LC (span =
     * range_ns / ns_per_volt, centred on zero_offset). Outside it the cap is
     * plateaued: V no longer tracks phase, so a "phase_ns" computed from a
     * near-rail V is garbage that the loop then integrates — the mechanism of
     * the observed ~370 s limit cycle (saturate → false phase → integrator
     * overshoot → re-saturate). Mark such samples invalid so the loop holds
     * instead of chasing a phantom. Allow ~10 % margin past the band. */
    if (*valid && g_ltic.range_ns > 1.0f && g_ltic.ns_per_volt > 1.0f) {
        double span_v = (double)g_ltic.range_ns / (double)g_ltic.ns_per_volt;
        double lo = (double)g_ltic.zero_offset - 0.55 * span_v;
        double hi = (double)g_ltic.zero_offset + 0.55 * span_v;
        if ((double)v < lo || (double)v > hi) *valid = false;
    }

    /* Centre: use the calibrated zero_offset when we have one (LC sets it to
     * the mid-point of the actual swept band). Fall back to it even when
     * ns_per_volt is 0, since LC may set zero_offset/range without a trusted
     * slope. Only if nothing is known do we use a coarse 0.22 V guess that
     * matches the narrow low-band detectors seen in practice. */
    double centre = (g_ltic.zero_offset > 0.001f) ? (double)g_ltic.zero_offset : 0.22;
    double slope  = (g_ltic.ns_per_volt != 0.0f)  ? (double)g_ltic.ns_per_volt : 100.0;
    double phase  = ((double)v - centre) * slope;

    /* Sawtooth correction: the receiver's 1PPS lands on an internal clock
     * edge, off true GPS time by a known qErr (UBX-TIM-TP). Subtracting it
     * removes the receiver granularity sawtooth (LEA-6T: ~±10 ns) and leaves
     * the OCXO's own phase error. Zero when SAW is off or no fresh qErr.
     *
     * Pairing by ppscount (v0.95 audit fix): the correction is taken ONLY if
     * the latest TIM-TP genuinely describes THIS pulse — accounting for the
     * mode bit (next-pulse vs this-pulse). If TIM-TP hasn't arrived yet for
     * this PPS (vGpsTask lagging) or arrived for a different one, the call
     * returns 0 and the sample is treated as uncorrected, rather than
     * subtracting a stale qErr off by one PPS. */
    phase -= (double)ubx_timtp_correction_for_pps(ppscount);
    return phase;
}

/* Request a picDIV arm (non-blocking; control task sequences the pulse). */
static void ltic_arm_picdiv(void)
{
    xEventGroupSetBits(xSysEvents, EVT_ARM_PICDIV);
}

/* ---- LTIC auto-tuning ----------------------------------------------------
 * Derive every loop coefficient from the two MEASURED hardware constants:
 *   K   (Hz per PWM LSB, from CT — recovered via g_pid[7].Kp = 0.40/K)
 *   nsv (ns per volt) and range_ns (from LC)
 * so no per-board hand tuning (AQP/DPP/...) is ever required. Called after a
 * successful LC and on entry to algorithm 10. Design rules:
 *   freq loop : cancel ~50-60%% of Δf per step  → Kp = 0.5·(1/K)
 *   phase loop: pull phase to zero with τ≈20 s  → Kd = (1/K)/(100·20) LSB/ns
 *               integral 10× slower; LOCK 4× gentler than DPLL
 *   ACQ threshold: quarter of the measured detector range (clamped 20..200).
 */
void ltic_autotune(void)
{
    double lsb_per_hz = (g_pid[7].Kp > 100.0) ? (g_pid[7].Kp / 0.40) : 3000.0;

    /* RECOMPUTE ONLY WHEN THE INPUTS CHANGED.
     *
     * Every gain below is a pure function of lsb_per_hz (from CT) and range_ns
     * (from LC), yet this ran on EVERY transition into ACQ and silently threw
     * away anything the operator had typed. Hit twice in one evening on 25.08
     * while testing the ACQ gain by hand: AQP is set, the loop drops to ACQ,
     * autotune puts the old value straight back, and the next observation is
     * of the tuning you thought you had replaced. Nothing in the log says so.
     *
     * The design intent — "no per-board hand tuning is ever required" — is
     * served by running once per boot and again whenever CT or LC moves the
     * measured constants. It is not served by overwriting a live experiment.
     * A hand tune still does not survive a RESET; that is what ES LTIC is
     * for, and the boot pass then lands on the same numbers anyway. */
    static double s_at_lsbhz = -1.0;
    static float  s_at_range = -1.0f;
    if (lsb_per_hz == s_at_lsbhz && g_ltic.range_ns == s_at_range) return;
    s_at_lsbhz = lsb_per_hz;
    s_at_range = g_ltic.range_ns;

    /* Phase gain for DPLL. The original τ=20 s (lsb_per_hz/2000) crawled: with
     * a narrow detector (ns/V small, e.g. 34 on an LVC74 clocked at 10 MHz)
     * an 18 ns error produced ~28 LSB per 2 s step, i.e. ~1 ns/s of phase
     * pull — the DPLL→LOCK gate (|phase| ≤ 0.4×acq_threshold) took many
     * minutes. /800 gives ~2.5× the pull and still leaves the loop well
     * damped, because the phase term is integrated over a 2 s period. */
    double kd_dpll = lsb_per_hz / 800.0;

    /* ACQ GAIN IS BOUNDED BY THE AVERAGE IT ACTS ON, NOT BY THE PLANT.
     *
     * ACQ updates every 5 s (period below) but steers on `avg100` — a SLIDING
     * 100 s boxcar of the frequency error. A correction therefore cannot show
     * up in the measurement for up to 100 s, and the mean delay is 50 s, while
     * the loop keeps acting twenty times inside that window. With 0.5 the loop
     * removed half the REPORTED error every 5 s and so applied about ten times
     * what was needed before the measurement could answer.
     *
     * That is not slow convergence, it is divergence, and it was measured:
     * 25.08 21:06, a cold ACQ entry with the OCXO already within 0.02 Hz —
     * PWM swung 31229..51512 (twenty thousand LSB), Vctl 1.38..2.15 V, the
     * runaway guard fired twice and the run ended parked at +2.53 Hz. The
     * simulator reproduces it from the shipped constants alone: PWM ±13319,
     * ending at 2.79 Hz, and it diverges from a one-LSB start.
     *
     * The bound is  Kp * K < 2 * period / window  = 2*5/100 = 0.10, i.e.
     * Kp < 0.10 * lsb_per_hz. Swept in simulation from a 0.02 Hz start:
     * 0.50 diverges, 0.25 crawls, 0.10 is the edge (settles 334 s), 0.05
     * settles in 167 s. 0.05 is a factor of two inside the boundary and
     * scales with the board's own measured K, so it carries to any OCXO.
     *
     * This only ever bit a COLD ACQ: g_ltic.state is persisted, so a warm
     * start resumes in DPLL or LOCK and never runs this path. */
    g_ltic.acq.Kp  = 0.05 * lsb_per_hz;  /* LSB per Hz of e_freq            */
    /* acq.Ki and acq.Kd are NOT READ ANYWHERE. The ACQ branch uses pid->Kp and
     * nothing else; the centring pull is g_ltic_acq_centre_gain (the ACG
     * command), a separate global. They are still set here, printed by LL,
     * settable with AQI/AQD and persisted — four ways to be told a knob works
     * when turning it changes nothing. Left in place for now because removing
     * them touches the settings block; see TODO item 22. */
    g_ltic.acq.Ki  = 0.02;               /* UNUSED — see note above          */
    g_ltic.acq.Kd  = 0.0;                /* UNUSED — see note above          */
    g_ltic.acq.I_LIMIT = 8000.0;

    g_ltic.dpll.Kp = 0.5 * lsb_per_hz;   /* freq feed                       */
    g_ltic.dpll.Kd = kd_dpll;            /* phase proportional, LSB per ns  */
    g_ltic.dpll.Ki = kd_dpll / 6.0;      /* phase integral per 2 s step     */
    g_ltic.dpll.I_LIMIT = 5000.0;

    /* LOCK stays deliberately slow: it is the narrow-band state and its gains
     * are referenced to the ORIGINAL conservative constant, not to the faster
     * DPLL one, so speeding up acquisition does not raise the locked noise. */
    double kd_lock = lsb_per_hz / 2000.0;
    /* ZERO IS DELIBERATE, AND IT IS NOT THE SAME ZERO AS BEFORE v1.06.
     *
     * The LOCK branch still computes `pid->Kp * e_freq * 0.1`, and the comment
     * beside it records a real failure: with no frequency term the only
     * defence against OCXO drift was the slow drift feed-forward, which lagged
     * about 60x too slow — the phase walked 11 to -425 ns in 51 s and lock
     * dropped. Kp = 0 makes that whole expression identically zero, which
     * reads like the fix was quietly deleted. It was not; it was REPLACED.
     *
     * The v1.06 LOCK rework gives the stage its own frequency measurement: the
     * pair test's slope, converted to LSB and added straight to the integrator
     * (slope_lsb), gated on the standard error of a difference of two means.
     * That is the same job the 0.1*Kp term was doing, done from the detector
     * instead of from TIM2 and with a measured significance threshold instead
     * of a fixed gain — so it costs nothing when there is no resolvable drift.
     *
     * Measured 25.08 23:21, algo 10 with this Kp at zero, LNV corrected to
     * 1252 and the new ACQ gain: LOCK pulled the phase in from -151 ns with a
     * clean exponential, tau = 469 s, reaching -5 ns; the last 300 s held mean
     * -5.4 ns at 3.7 ns RMS with PWM moving over seven LSB. LOCK is not blind
     * to frequency without this term, and adding it back would inject avg100's
     * 0.01 Hz quantisation — 1.25 LSB per update against a 2.3 LSB output
     * noise — for no demonstrated gain. Left at zero on that evidence.
     *
     * LKP still sets it by hand if someone wants to experiment. */
    g_ltic.lock.Kp = 0.0;                /* see above — replaced by slope_lsb */
    g_ltic.lock.Kd = kd_lock / 4.0;
    g_ltic.lock.Ki = kd_lock / 40.0;
    g_ltic.lock.I_LIMIT = 2000.0;

    if (g_ltic.range_ns > 1.0f) {
        float th = g_ltic.range_ns / 4.0f;
        if (th < 20.0f)  th = 20.0f;
        if (th > 200.0f) th = 200.0f;
        g_ltic.acq_threshold_ns = (uint16_t)th;
    }
    OUT_SERIAL.print("LTIC autotune: lsb/Hz=");   OUT_SERIAL.print(lsb_per_hz, 0);
    OUT_SERIAL.print("  dpll Kd=");               OUT_SERIAL.print(g_ltic.dpll.Kd, 3);
    OUT_SERIAL.print(" Ki=");                     OUT_SERIAL.print(g_ltic.dpll.Ki, 4);
    OUT_SERIAL.print("  acq_thresh=");            OUT_SERIAL.print(g_ltic.acq_threshold_ns);
    OUT_SERIAL.println(" ns");
}

/* ---- ALGORITHM 10: LTIC handling borrowed from algorithm 12 ---------------
 *
 * Two things algorithm 12 does with this detector that the three-stage loop did
 * not, and the 20.08 19:42 log shows what each one costs.
 *
 * IT LOOKED ONCE PER UPDATE. The phase was read inside the update-rate gate, so
 * in LOCK the loop steered on a SINGLE second's reading taken every
 * lock_interval_s — up to ten minutes apart. g_ltic_voltage is one median-
 * filtered ADC sample per second, so that one reading carries the full
 * per-sample noise plus the receiver's sawtooth. Algorithm 12 reads every
 * second and lets the hierarchy average; here the sampler below does the same
 * and hands LOCK a mean instead of a sample. Averaging the 300 s between two
 * default LOCK updates divides the noise on that figure by about seventeen.
 *
 * IT IGNORED SMALL ERRORS INSTEAD OF MEASURING THEM. LOCK had a deadband of
 * range_ns/40 with a soft knee: inside it the error was treated as zero and the
 * integrator held, outside it only (|phase| - deadband) was acted on. On this
 * board that is 1881/40 = 47 ns, and the loop parked at +76 ns for ninety
 * minutes — where it was acting on 38% of the error, which is exactly the
 * equilibrium a deadband plus a soft knee produces against a small drift. The
 * mean over that run was +76 ns with 77% of samples past 50 ns, while algorithm
 * 12 on the same board in the same hour held 5.5 ns about zero.
 *
 * The deadband's INTENT was right — do not chase the ADC noise floor — but a
 * threshold is the wrong instrument for it, because an error below the
 * threshold is invisible forever however long it persists. Algorithm 12 states
 * the intent differently: a small error is not ignored, it is AVERAGED until it
 * can be measured, and then acted on in full. That is the significance gate
 * below, and it cannot leave a standing offset: the noise on a mean of n
 * samples falls as sqrt(n), so any constant bias becomes significant given
 * enough seconds, and the loop then removes ALL of it rather than a fraction.
 * -------------------------------------------------------------------------- */

/* Sigma multiplier on the MEAN. Three, not the eight algorithm 12 uses on its
 * test statistic: that eight compensates for a hierarchy which tests level 0 a
 * thousand times more often than level 10, and there is no hierarchy here — one
 * test per LOCK update. Three sigma on a mean fires on noise about once in 370
 * updates, which at the default cadence is a spurious correction every day. */
#define LTIC_LOCK_SIGMA_K   3.0

/* Seconds to ignore the detector after a picDIV re-arm. The divider lands the
 * phase at a quantised offset — about +/-3 us on this build — and that jump is
 * not a phase error the loop should answer. Algorithm 12 has skipped it since
 * v1.05 (s_mla_post_arm); the three-stage loop armed at three separate places
 * and skipped nothing, so every re-arm fed the landing straight into the PI. */
#define LTIC_POST_ARM_S     5u

/* How far the phase may travel between LOCK updates, in units of its own
 * measured 1-sigma. Swept in simulation against the drift measured on this
 * hardware; with the step cap and the pair estimator below, phase RMS at
 * lock_interval_s = 300 came out 90 ns at eight sigma, 50 at four and 34 at
 * two, and a board with no resolvable drift keeps its full interval at any of
 * them because the bound is a division by a slope that reads zero. Two is not
 * a tight loop pretending to be a slow one: at 2 sigma the individual PWM steps
 * MEASURE about 10 LSB, against 17 at eight sigma, because the loop that looks
 * more often has less to undo each time. */
#define LTIC_LOCK_ROOM_K    2.0

/* TWO ADJACENT HALF-WINDOWS, not one mean. This is the correction to a first
 * attempt that averaged the whole interval and acted on the mean, and it is
 * worth recording why that failed, because the reason is not obvious and the
 * hardware found it in one run.
 *
 * A mean over H seconds is the phase as it was H/2 seconds ago. Feeding that to
 * an integrator adds H/2 of transport delay — and this loop already updates only
 * every lock_interval_s. Measured on 21.08: with a real drift of 0.547 ns/s and
 * LIV=300, the mean handed the integrator 82 ns of stale error every update, the
 * loop chased its own lag, and the phase swept +/-500 ns where the old deadband
 * had parked it at a steady +76. Removing the deadband was right; replacing it
 * with a laggy estimate was not.
 *
 * Algorithm 12 does not have this problem, and re-reading Alan's construction
 * shows why: its test is (a+b) + 2*(b-a), which is NOT an average. The (a+b)
 * term is the phase over the pair and the 2*(b-a) term is twice the change
 * between the halves — together they EXTRAPOLATE the phase to the end of the
 * window. The averaging and the lag cancel by design. Keeping two halves gives
 * that, and it gives the slope as a measurement in its own right, which is the
 * other half of what algorithm 12 applies: cancel the frequency error AND move
 * the phase, never one without the other.
 *
 * a is the older half, b the newer; each covers `half` seconds, so the centres
 * are `half` apart and the slope is simply (mean_b - mean_a) / half. */
static double   s_lt_a_sum, s_lt_b_sum;
static uint32_t s_lt_a_n,   s_lt_b_n;
static double   s_lt_ms;           /* mean square of first differences           */
static int32_t  s_lt_prev;         /* previous phase, for the difference         */
static bool     s_lt_prev_valid;   /* was the previous second contiguous?        */
static uint32_t s_lt_post_arm;     /* seconds left to ignore after an arm        */
/* Window GEOMETRY, measured rather than assumed. The first version took the
 * separation of the two window centres to be lock_interval_s. It is not: the
 * cadence bound below SHORTENS the update interval whenever the loop resolves
 * drift, so on a board asking for 300 s and running at 73 the slope came out
 * four times too small and the extrapolation reached four times too far. Record
 * where the last roll happened and derive both lengths from that. */
static uint32_t s_lt_roll_pps;     /* ppscount at the last roll (0 = none yet)   */
static uint32_t s_lt_a_len;        /* seconds the older window actually spans    */

static void ltic_sample_reset(void)
{
    s_lt_a_sum = s_lt_b_sum = 0.0; s_lt_a_n = s_lt_b_n = 0;
    s_lt_roll_pps = 0u;            s_lt_a_len = 0u;
}

/* Called EVERY second, before the update-rate gate. Keeps the running mean and
 * the noise estimate that the LOCK significance test needs.
 *
 * The noise comes from FIRST DIFFERENCES, for the reason recorded at length in
 * the algorithm 12 estimator: a mean-square of the phase itself counts a
 * standing offset as noise, which is precisely the quantity this gate exists to
 * detect, and a phase RAMP sweeps the estimate upward with it until nothing can
 * cross. Differencing removes both exactly. The outlier gate is ABSOLUTE rather
 * than a multiple of the estimate it feeds, because a gate read from its own
 * output is a one-way ratchet. */
static void ltic_sample(bool valid, double phase_ns)
{
    if (s_lt_post_arm > 0u) { s_lt_post_arm--; s_lt_prev_valid = false; return; }
    if (!valid) { s_lt_prev_valid = false; return; }

    int32_t p = (int32_t)phase_ns;
    if (s_lt_prev_valid) {
        double dp = (double)(p - s_lt_prev);
        if (dp < 0.0) dp = -dp;
        if (dp < 300.0) s_lt_ms += 0.002 * (0.5 * dp * dp - s_lt_ms);
    }
    s_lt_prev = p;
    s_lt_prev_valid = true;

    s_lt_b_sum += phase_ns;
    s_lt_b_n++;
}

/* Close the current window and make it the older one. Called from the LOCK
 * update tick, NOT from the sampler.
 *
 * The first version rolled inside the sampler when the newer half reached
 * lock_interval_s/2 samples — which lands on exactly the same second as the
 * update tick, every time, because one is half the other. So the loop always
 * looked immediately after a roll, found the newer window empty, and did
 * nothing at all: PWM moved zero counts in a six-hour simulation. Rolling where
 * the pair is consumed removes the coincidence by construction rather than by
 * choosing a phase offset that happens to avoid it. */
static void ltic_roll_window(uint32_t pps, uint32_t blen)
{
    s_lt_a_sum = s_lt_b_sum; s_lt_a_n = s_lt_b_n;
    s_lt_b_sum = 0.0;        s_lt_b_n = 0;
    s_lt_a_len = blen;       s_lt_roll_pps = pps;
}

/* 1-sigma per-sample phase noise, floored where this detector plus the
 * sawtooth stop resolving. Same floor and same reasoning as algorithm 12. */
static double ltic_sample_sigma(void)
{
    double s = sqrt(s_lt_ms);
    return (s < 5.0) ? 5.0 : s;
}


uint16_t ltic_three_stage(uint16_t pwm, uint32_t ppscount)
{
    static uint32_t warn_ms = 0;
    static uint16_t start_pwm = 0;
    static bool     start_set = false;
    static bool     runaway_warned = false;
    static double   prev_abs_ef = 0.0;
    static uint8_t  no_improve  = 0;
    static uint32_t acq_railed_cnt = 0;
    static uint32_t acq_rearm_hold = 0;


    /* ---- persistent loop memory ---- */
    static double   integ        = 0.0;     /* integral term (PWM units)     */
    static double   last_phase   = 0.0;     /* for drift estimate            */
    static uint32_t stable_cnt   = 0;       /* consecutive in-band cycles    */
    static uint32_t exit_cnt     = 0;       /* consecutive out-of-band (LOCK)*/
    static uint32_t last_lock_pps= 0;       /* LOCK cadence timer            */
    static uint32_t last_pps     = 0xFFFFFFFF;
    static bool     warned_uncal = false;
    static bool     seeded       = false;
    static uint8_t  prev_state   = 0xFF;

    /* A fresh entry into this algorithm starts from a defined state.
     * integ is an ABSOLUTE PWM target here, so carrying it across a switch
     * would apply a control voltage chosen for conditions that may be hours
     * old; and prev_state still holding LOCK means the entry transition never
     * fires, so autotune never runs and the picDIV is never armed. */
    if (algo_take_restart()) {
        integ = 0.0; last_phase = 0.0; stable_cnt = 0; exit_cnt = 0;
        last_lock_pps = 0; last_pps = 0xFFFFFFFFu;
        warned_uncal = false; seeded = false;
        prev_state = 0xFF;                  /* force the entry transition */
        warn_ms = 0;
        start_pwm = 0; start_set = false; runaway_warned = false;
        prev_abs_ef = 0.0; no_improve = 0;
        acq_railed_cnt = 0; acq_rearm_hold = 0;
        ltic_sample_reset();
        s_lt_post_arm = LTIC_POST_ARM_S;    /* whatever the divider is doing, wait */
    }


    /* Seed integral from the incoming PWM once, so the first correction is
     * bumpless (no jump from a cold integrator). */
    if (!seeded) { integ = (double)pwm; seeded = true; }

    /* Flush detector on PPS-counter reset */
    if (ppscount < last_pps) { stable_cnt = 0; exit_cnt = 0; }
    last_pps = ppscount;

    /* One-time uncalibrated warning */
    if (g_ltic.ns_per_volt == 0.0f && !warned_uncal) {
        OUT_SERIAL.println("LTIC: running UNCALIBRATED (run LC). Using nominal V-based phase.");
        warned_uncal = true;
    }

    /* Resume state from EEPROM-backed g_ltic.state on first run / state change.
     * On entering ACQ, arm the picDIV. */
    uint8_t state = g_ltic.state;
    if (state > LTIC_LOCK) state = LTIC_ACQ;
    /* BOOT SANITY: a persisted LOCK/DPLL is only trustworthy if the present
     * phase is both VALID and already CLOSE to zero_offset. After a power cycle
     * the OCXO has thermally drifted and the picDIV edge can be anywhere, so
     * the detector may start railed OR merely far off centre. Seen on air: a
     * warm boot resumed LOCK with Vphase ≈2.09 V while zero_offset was 1.85 V
     * (~300 mV ≈ hundreds of ns off) — technically "on the ramp" so the old
     * valid-only check passed, but far too coarse for LOCK; DPLL then dropped
     * it to ACQ a minute later and the full ~6 min pull-in ran anyway. On the
     * FIRST call of this boot (prev_state == 0xFF), demote a persisted
     * LOCK/DPLL to ACQ unless the phase is valid AND within the ACQ window of
     * zero_offset — i.e. genuinely where a lock belongs. This costs nothing on
     * a clean warm boot (already centred → stays LOCK) and skips the wasted
     * LOCK→DPLL→ACQ bounce when it isn't. */
    if (prev_state == 0xFF && state != LTIC_ACQ) {
        bool boot_valid = false;
        double boot_ph = ltic_phase_error_ns(&boot_valid, ppscount);
        bool near_centre = boot_valid &&
                           fabs(boot_ph) <= (double)g_ltic.acq_threshold_ns;
        if (!near_centre) {
            state = LTIC_ACQ;
            g_ltic.state = LTIC_ACQ;
        }
    }
    if (state != prev_state) {
        if (state == LTIC_ACQ) { ltic_autotune(); ltic_arm_picdiv();
                                 s_lt_post_arm = LTIC_POST_ARM_S;
                                 ltic_sample_reset(); }
        prev_state = state;
    }

    /* Update period depends on state. DPLL corrects every 2 s (not 10): on a
     * narrow detector the phase sweeps its whole range in ~10-15 s of residual
     * drift, so a 10 s interval let it wander between corrections and the lock
     * never closed. LOCK uses lock_interval_s but capped so it can still track
     * a narrow detector; if that is set very large (legacy) we clamp to 5 s. */
    uint32_t lock_iv = g_ltic.lock_interval_s;
    /* Clamp to the nearest bound rather than snapping to a default: an
     * out-of-range value used to jump to 5 s, so asking for a SLOWER loop
     * silently gave you the fastest one. The field is uint16 and 600 fits. */
    if (lock_iv < 1u)        lock_iv = 1u;
    else if (lock_iv > 600u) lock_iv = 600u;
    uint32_t period = (state == LTIC_LOCK) ? lock_iv
                    : (state == LTIC_DPLL) ? 2u : 5u;

    /* LOCK cadence bounded by the drift the loop has just MEASURED.
     *
     * lock_interval_s is what the operator asked for, and at a slow cadence this
     * stage carries about two intervals of delay — one to fill the window, one
     * until the next update — around an integrator. Simulated against the drift
     * measured on 21.08 (0.547 ns/s) the phase path is stable to roughly 120 s
     * and rails beyond 180; at 1.5 ns/s the boundary drops to about 60. The old
     * deadband survived a 300 s cadence only because it acted on a fraction of
     * the error and therefore had little gain to be unstable with — it bought
     * margin with a permanent offset.
     *
     * Rather than pick a number, bound the movement: never let the phase travel
     * more than a few sigma of its own noise between updates, using the slope
     * this stage already measures. Eight sigma is the same significance language
     * the rest of this file speaks, and it lands where the simulation says it
     * should — 73 s at the drift measured here, 27 s at 1.5 ns/s — while a quiet
     * board with no resolvable drift keeps the full interval it was given.
     *
     * This only ever SHORTENS the interval. Asking for a slow loop on a board
     * that is drifting is asking for something the detector band cannot deliver,
     * and quietly obliging would put the phase on a rail. */
    if (state == LTIC_LOCK && s_lt_a_n > 0u && s_lt_b_n > 0u) {
        uint32_t bl = (s_lt_roll_pps != 0u) ? (ppscount - s_lt_roll_pps) : lock_iv;
        double sp  = 0.5 * ((double)s_lt_a_len + (double)bl);
        if (sp < 1.0) sp = (double)lock_iv;
        double sl  = fabs((s_lt_b_sum / (double)s_lt_b_n)
                        - (s_lt_a_sum / (double)s_lt_a_n)) / sp;
        double room = LTIC_LOCK_ROOM_K * ltic_sample_sigma();
        if (sl > 1e-6) {
            uint32_t lim = (uint32_t)(room / sl);
            if (lim < 5u) lim = 5u;               /* the sampler needs a window */
            if (lim < period) period = lim;
        }
    }

    /* SAMPLE EVERY SECOND, act at the stage's own rate. This runs BEFORE the
     * gate below on purpose: the detector produces one reading a second whether
     * or not the loop is ready to use it, and throwing 299 of every 300 away —
     * which is what reading inside the gate amounted to in LOCK — leaves the
     * loop steering on a single sample's noise. See the block above. */
    {
        bool sv = false;
        double sp = ltic_phase_error_ns(&sv, ppscount);
        ltic_sample(sv, sp);
    }

    /* A shrinking cadence can put two ticks a second apart, which would present
     * the pair test with a one-sample window. The significance tests would
     * mostly refuse it anyway - the standard error of a single sample is the
     * whole sigma - but a slope divided by a one-second span is a large number
     * to leave to a statistical veto. Five seconds is the same floor the bound
     * above uses. */
    if (state == LTIC_LOCK && s_lt_roll_pps != 0u &&
        (ppscount - s_lt_roll_pps) < 5u) return pwm;

    /* ELAPSED TIME, not ppscount % period, once the period can change.
     *
     * The modulo gate is correct only for a constant period, and the cadence
     * bound above makes it anything but: with lock_interval_s = 300 and a bound
     * of ~110 s the tick fired on multiples of whatever `period` happened to be
     * that second, which in a 6 h simulation gave intervals from 7 s to 551 s
     * and a MEDIAN of 300 - the bound computed the right number every second
     * and almost never got to use it. ACQ and DPLL keep the modulo form; their
     * periods are fixed and the phase alignment to the PPS count is free. */
    if (state == LTIC_LOCK) {
        if (last_lock_pps != 0u && (ppscount - last_lock_pps) < period)
            return pwm;
        last_lock_pps = ppscount;
    } else {
        last_lock_pps = 0u;
        if ((ppscount % period) != 0u) return pwm;
    }

    /* ---- read both sensors ---- */
    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    /* e_freq from avg100 (0.01 Hz), not avg10 (0.1 Hz). avg10's coarse
     * quantisation, times Kp (~1550 LSB/Hz), produced ±150 LSB PWM jumps — a
     * ~10 s limit cycle in ACQ that stopped the phase settling under the lock
     * threshold (GML-5.2 analysis). avg100 is 10× finer and settles cleanly.
     * Fall back to avg10 only until the first 100 s window has filled. */
    double e_freq = s.have100 ? (s.avg100 - (double)BASE_FREQ)
                  : s.have10  ? (s.avg10  - (double)BASE_FREQ) : 0.0;

    /* Damping-term windows, per state (FAD / FAL). With both at the default 100
     * these are bit-for-bit e_freq, so behaviour is unchanged unless the operator
     * shortens a window to chase a limit cycle. */
    double e_freq_damp_dpll = damp_e_freq(&s, e_freq, g_freq_damp_win_dpll);
    double e_freq_damp_lock = damp_e_freq(&s, e_freq, g_freq_damp_win_lock);

    bool ph_valid = false;
    double phase_ns = ltic_phase_error_ns(&ph_valid, ppscount);

    /* drift = change in phase per second. Reject wrap-induced spikes: if the
     * phase appears to jump more than half the detector range in one step, it
     * wrapped rather than really moved that far, so we skip the drift update
     * this cycle (a false huge slope would otherwise wreck the ACQ drive and
     * the slope-gated transitions). */
    double drift = 0.0;
    double half_range = (g_ltic.range_ns > 1.0f) ? (double)g_ltic.range_ns * 0.5 : 150.0;
    double dphase = phase_ns - last_phase;
    if (fabs(dphase) < half_range) {
        drift = dphase / (double)period;
    }   /* else: wrap — leave drift at 0 for this cycle */
    last_phase = phase_ns;

    /* ---- pick the active PID set for this state ---- */
    PidParams_t *pid = (state == LTIC_ACQ)  ? &g_ltic.acq
                     : (state == LTIC_DPLL) ? &g_ltic.dpll
                     :                        &g_ltic.lock;

    /* ---- compute correction ----
     * ACQ : drive the phase toward the CENTRE of the detector range (not a rail
     *       and not zero_offset, which can sit near the floor). The PWM→phase
     *       sign is board-dependent and set once by the user via LPOL (a
     *       single-cycle auto-probe proved unreliable on a narrow, drifting
     *       detector — the phase's own drift swamped the probe and the sign
     *       came out wrong), so ACQ holds until g_ltic.polarity is set.
     * DPLL: frequency Kp + phase PI (same polarity).
     * LOCK: phase-led PI, frequency term dropped (TIM2 quantisation noise). */

    /* Centre target: explicit centre_v if set, else the middle of the detected
     * range (range_ns/ns_per_volt gives the span in volts), else a safe 0.16 V
     * that keeps us off both rails on a narrow low-band detector. */
    double centre_v;
    if (g_ltic.centre_v > 0.001f) {
        centre_v = (double)g_ltic.centre_v;          /* explicit LCV override  */
    } else if (g_ltic.zero_offset > 0.001f) {
        /* zero_offset IS the middle of the observed band since v0.66/v0.74 —
         * the old "+ span/2" here dated from when it was the band's floor and
         * made ACQ park the phase half a range away from the point the
         * ACQ→DPLL threshold is measured against (a permanent stalemate:
         * loop holds at its "centre", threshold never satisfied). One point
         * of truth now: the pull target equals the threshold's zero. */
        centre_v = (double)g_ltic.zero_offset;
    } else {
        centre_v = 0.16;
    }

    double u = 0.0;
    if (state == LTIC_ACQ) {
        float vraw = g_ltic_voltage;
        double err_v = (double)vraw - centre_v;    /* how far from centre, in volts */

        /* --- polarity handling ---
         * A single-cycle probe cannot separate the PWM effect from the phase's
         * own drift on a narrow, drifting detector (the drift dominates dV and
         * the sign comes out wrong), so we do NOT auto-probe. If polarity is
         * unset, hold and ask the user to set it once with LPOL (then ES). This
         * is reliable; the probe was not. */
        int8_t pol = g_ltic.polarity;
        /* warn_ms: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */

        if (pol == 0) {
            u = 0.0;                         /* hold — do not guess the sign */
            uint32_t now = millis();
            if (now - warn_ms > 10000u) {    /* remind every 10 s */
                OUT_SERIAL.println("LTIC ACQ: polarity unset — run 'LPOL -1' (or +1) then 'ES'. Holding.");
                warn_ms = now;
            }
        } else {
            /* Two-part ACQ drive:
             *  drift_term  — dominant: nulls the phase slope (frequency offset)
             *                so the phase stops sweeping/wrapping.
             *  centre_term — once the drift is small, actively walk the phase
             *                to mid-range. A weak pull is not enough: to move
             *                the phase from a parked position we must inject a
             *                deliberate, bounded frequency offset (PWM step)
             *                proportional to how far off-centre we are, then the
             *                drift term arrests it again near the middle. */
            /* ACQ drives the TIM2-measured frequency error to zero — NOT the
             * voltage-derived drift. The stepped detector read goes flat at a
             * band edge (seen on air: phase parked at 0.336 V while a real
             * −0.3 Hz offset persisted), which blinds a V-derived slope; TIM2
             * sees the offset regardless. Gains come from ltic_autotune()
             * (Kp = LSB per Hz, derived from measured K).
             * SIGN: K is positive on every board (+PWM → +f), so the frequency
             * path takes NO board polarity; pol applies only to the PHASE
             * (Vphase) path below. Routing e_freq through pol=-1 was inverting
             * a correct frequency correction. */
            double freq_term   = pid->Kp * e_freq;          /* no pol here */
            double centre_term = 0.0;
            /* The centring term steers on the RAW voltage, and near a rail
             * that voltage no longer tracks phase — err_v becomes a number
             * about the rail, not about the oscillator. The drift gate does
             * not catch it: a railed reading is flat, so drift is ~0 and the
             * test passes with enthusiasm. Measured 20.08 19:42, three seconds
             * after a switch into algorithm 10: Vphase 3.187 V against a band
             * of 0.818..2.865 V, err_v +1.35 V, the term saturating its own
             * cap and pushing for as long as the detector stayed out of band —
             * 690 LSB of PWM swept before it settled.
             *
             * THE GATE IS "NOT RAILED", NOT "IN BAND" — and the magnitude is
             * clamped to the band instead of being thrown away with it.
             *
             * Two opposite failures, one term. Gating on ph_valid (the full
             * band test in ltic_phase_error_ns) stopped the 20.08 shove, but
             * it also stops the pull-in whenever the band recorded by LC is
             * narrower than the detector really is — and on this hardware it
             * is a fifth of it. Measured 25.08 21:xx, with the ACQ gain fixed:
             * after the picDIV arm the phase parked at -1320 ns, i.e. 1.583 V
             * against a recorded band of 1.729..2.433 V. Outside the band, so
             * ph_valid false, so no centring; frequency already on target, so
             * no frequency term either. u = 0, PWM frozen, phase parked, and
             * ACQ->DPLL needs |phase| <= 200 ns — a permanent stall with every
             * guard quiet, because nothing was wrong except that the loop had
             * switched itself off.
             *
             * What is actually true outside the band is that the MAGNITUDE of
             * (V - centre) is meaningless, not its SIGN: the ramp is monotonic
             * up to the rails, so the reading still says which way home is.
             * That is all ACQ needs — it is a bounded proportional nudge, it
             * integrates nothing. So steer whenever the reading is off the
             * rails, and clamp the error to the band edge. In band, nothing
             * changes. Out of band, the pull is the band-edge pull rather than
             * the 1.35 V shove that swept 690 LSB on 20.08. Railed, hold as
             * before and let the frequency path bring it back.
             *
             * DPLL and LOCK keep the strict band test: they integrate the
             * phase, and integrating a phantom is what the gate exists for. */
            bool v_usable = (vraw > 0.02f && vraw < 3.28f);
            if (g_ltic.range_ns > 1.0f && g_ltic.ns_per_volt > 1.0f) {
                double half = 0.55 * (double)g_ltic.range_ns
                                   / (double)g_ltic.ns_per_volt;
                if (err_v >  half) err_v =  half;
                if (err_v < -half) err_v = -half;
            }
            if (v_usable && fabs(drift) < 4.0) {              /* settled enough to steer */
                /* err_v>0 → phase above centre → drive it down. The step must
                 * be small enough that the phase CRAWLS toward centre without
                 * shooting through a sensitive detector window and wrapping to
                 * the far rail (the old 6000×/±400 LSB pairing was tuned for a
                 * less sensitive HC74). Tunable live with ACG. */
                centre_term = (double)g_ltic_acq_centre_gain * err_v;
                double cap = (double)g_ltic_acq_centre_cap;
                if (centre_term >  cap) centre_term =  cap;
                if (centre_term < -cap) centre_term = -cap;
            }
            u = -freq_term - (double)pol * centre_term;
            if (u >  1500.0) u =  1500.0;
            if (u < -1500.0) u = -1500.0;
        }
    } else {
        /* DPLL / LOCK: phase PI in ns. Apply the SAME board polarity as ACQ.
         * If polarity is still unknown (0), do NOT guess a sign and risk
         * running the phase onto a rail — hold PWM and let the machine drop
         * back to ACQ, which will probe and set g_ltic.polarity.
         *
         * SATURATION HANDLING: if the phase read is INVALID (detector
         * saturated — see the guard in ltic_phase_error_ns), the
         * voltage-derived phase is garbage, so we freeze the PHASE path
         * (proportional + integral) but KEEP the FREQUENCY path (TIM2 sees
         * the true offset regardless of detector saturation). That is how the
         * loop recovers: TIM2 pulls the OCXO back into the detector window,
         * phase becomes valid again, and the PI resumes. Without this, a
         * saturated read fed a false 1000 ns phase to the integrator and
         * drove the ~370 s limit cycle (saturate → slam → overshoot →
         * re-saturate). */
        int8_t pol = g_ltic.polarity;
        if (pol == 0) {
            u = 0.0;                    /* hold; polarity not established yet */
        } else if (!ph_valid) {
            /* frequency-only recovery: TIM2 pull, no phase integral wind-up */
            double freq_term = (state == LTIC_DPLL) ? (pid->Kp * e_freq_damp_dpll)
                           : (state == LTIC_LOCK)  ? (pid->Kp * e_freq_damp_lock * 0.3) : 0.0;
            u = -freq_term;
        } else {
            /* LOCK gentleness (deadband + soft knee): inside the deadband the
             * phase error is treated as zero (ADC noise floor — do not chase
             * it) and the integrator holds; outside, the error ramps from
             * zero (soft knee). Sized from the same measured constants as
             * autotune. Computed FIRST — both the integrator and the phase
             * term below use p_eff. */
            double p_eff = phase_ns;
            double slope_lsb = 0.0;          /* frequency correction, LOCK only */
            if (state == LTIC_LOCK) {
                /* THE PAIR, EXTRAPOLATED — and both of algorithm 12's terms.
                 *
                 * This replaces two things that were each wrong on their own.
                 * First a deadband of range_ns/40 with a soft knee, which left a
                 * permanent offset by construction: 47 ns invisible on this
                 * board, and the loop parked at +76 ns for ninety minutes.
                 * Then a plain mean over the interval, which removed the offset
                 * (mean fell to +3 ns) and destabilised the loop, because a mean
                 * over H seconds is the phase as it was H/2 ago and this loop
                 * updates only every H — 150 s of added delay around an
                 * integrator. Measured: phase swept +/-500 ns and the detector
                 * reached both rails.
                 *
                 * What was missing both times is that algorithm 12 applies TWO
                 * terms and neither works alone — its own comment says so. The
                 * pair gives both:
                 *
                 *   phase  = (mean_a + mean_b)/2, projected forward by half*slope
                 *            to the present, which is what (a+b) + 2*(b-a)
                 *            encodes and why that expression is not an average.
                 *   slope  = (mean_b - mean_a)/half, in ns/s: a measurement of
                 *            the frequency error, which on this board LOCK has
                 *            no other way to see at all — Kp is 0 here, so the
                 *            TIM2 frequency term is identically zero.
                 *
                 * Each is gated on its own measured significance: the phase on
                 * the standard error of the pair mean, the slope on the standard
                 * error of a DIFFERENCE of two means, which is sqrt(2) larger. No
                 * threshold is assumed; sigma comes from the first differences
                 * above, the same estimator algorithm 12 uses. */
                /* The ROLL is unconditional once there is a newer window, and
                 * the pair test runs only when there is also an older one. The
                 * first version rolled INSIDE the test, so after the reset at
                 * DPLL->LOCK the older window was empty, the test never ran, so
                 * the roll never happened and the older window stayed empty:
                 * LOCK would have entered and then done nothing for as long as
                 * it held. It survived simulation because the simulator seeded
                 * the first window by hand - a difference between model and
                 * firmware that the model existed to rule out. */
                p_eff = 0.0;
                if (s_lt_b_n > 0u) {
                    /* Centre separation is HALF THE SUM of the two window
                     * lengths, and each length is read off the clock. Both are
                     * lock_interval_s only when the cadence bound is inactive. */
                    uint32_t blen = (s_lt_roll_pps != 0u)
                                  ? (ppscount - s_lt_roll_pps) : period;
                    if (blen < 1u) blen = 1u;
                    if (s_lt_a_n > 0u) {
                        double span = 0.5 * ((double)s_lt_a_len + (double)blen);
                        if (span < 1.0) span = (double)blen;
                        double ma   = s_lt_a_sum / (double)s_lt_a_n;
                        double mb   = s_lt_b_sum / (double)s_lt_b_n;
                        double sg   = ltic_sample_sigma();
                        double slope = (mb - ma) / span;                 /* ns per second */
                        double sem_slope = sg * sqrt(1.0/(double)s_lt_a_n
                                                   + 1.0/(double)s_lt_b_n) / span;

                        /* The phase NOW: the newest window's mean carried forward
                         * half a window, because a mean is the value at the centre
                         * of what it averaged. This is the lag that sank the first
                         * attempt, and knowing the slope is what removes it. */
                        double mean = mb;
                        if (fabs(slope) > LTIC_LOCK_SIGMA_K * sem_slope) {
                            double lsb_per_ns = (g_pid[7].Kp > 100.0)
                                              ? ((double)g_pid[7].Kp / 0.40 / 100.0) : 25.0;
                            /* Damped 0.5 — Alan's k. One pair is a single
                             * measurement; half strength converges in two
                             * corrections where full strength stakes it all on one.
                             * Into the INTEGRATOR, because integ is the absolute PWM
                             * target here (u = integ - pwm): a standing frequency
                             * error means integ is simply wrong, and this says by
                             * how much. Adding it to the step instead would fight
                             * the LOCK rate cap, whose job is to stop the OUTPUT
                             * jumping and which should keep doing exactly that. */
                            slope_lsb = -(double)pol * slope * lsb_per_ns * 0.5;
                            mean += slope * 0.5 * (double)blen;
                        }
                        double sem = sg / sqrt((double)s_lt_b_n);
                        if (fabs(mean) > LTIC_LOCK_SIGMA_K * sem) p_eff = mean;
                    }
                    /* consumed: this window becomes the older one */
                    ltic_roll_window(ppscount, blen);
                }
            }
            if (!(state == LTIC_LOCK && p_eff == 0.0))     /* not yet measurable: hold integ */
                integ += -(double)pol * (pid->Ki * p_eff);
            integ += slope_lsb;              /* measured frequency error, LOCK only */
            if (integ > 65300.0) integ = 65300.0;
            if (integ < 200.0)   integ = 200.0;
            /* Frequency path: NO pol (K positive on every board); autotuned
             * Kp is already LSB-per-Hz. Phase path keeps pol.
             *
             * LOCK's frequency path: 0.1×Kp, and autotune sets that Kp to ZERO
             * on purpose — see the note at g_ltic.lock.Kp in ltic_autotune().
             * This term was added when LOCK had no frequency measurement at
             * all and the drift feed-forward lagged ~60× too slow (the phase
             * walked 11→−425 ns in 51 s and lock dropped). The v1.06 rework
             * replaced it with the pair test's own slope, gated on measured
             * significance, and the 25.08 23:21 run confirms LOCK pulls in
             * without this term (τ = 469 s, settling at 3.7 ns RMS). The
             * expression stays so LKP remains a live knob for experiments.
             * Original analysis: GML-5.2. */
            double freq_term  = (state == LTIC_DPLL) ? (pid->Kp * e_freq_damp_dpll)
                              : (state == LTIC_LOCK) ? (pid->Kp * e_freq_damp_lock * 0.1)
                              : 0.0;
            double phase_term = (double)pol * pid->Kd * p_eff;
            u = integ - (double)pwm - freq_term - phase_term;
            /* self-learning: damp the correction, add drift feed-forward when
             * locked. Driven by e_freq (closed-loop); phase_ns feeds the
             * limit-cycle/damping observer. */
            {
                bool  lk = (state == LTIC_LOCK);
                float ff = lrn_update_ef(e_freq, phase_ns, (double)period, lk);
                u *= (double)g_lrn_damp;
                u += (double)ff;
            }
            if (state == LTIC_LOCK) {
                /* hard cap per step: ≈4 mHz regardless of unit (from measured K) */
                double lsbhz = (g_pid[7].Kp > 100.0) ? (g_pid[7].Kp / 0.40) : 3000.0;
                double cap = lsbhz * 0.004;
                if (cap < 3.0) cap = 3.0;
                /* ...but never below what the loop has just MEASURED it needs.
                 * The 4 mHz cap is per UPDATE, not per second, so at a 300 s
                 * cadence it granted the loop a thirtieth of the authority it
                 * had at 30 s while the drift to be cancelled was unchanged.
                 * The integrator then sat against the cap for update after
                 * update and overshot when it finally caught up: simulated
                 * phase RMS 174 ns at lock_interval_s = 300, against 34 with
                 * this floor in place. Four times the frequency step the slope
                 * test just computed leaves room for the phase term as well,
                 * and on a board with no resolvable slope slope_lsb is zero and
                 * the cap is exactly what it always was. */
                double need = 4.0 * fabs(slope_lsb);
                if (cap < need) cap = need;
                if (u >  cap) u =  cap;
                if (u < -cap) u = -cap;
            }
        }
    }

    /* slew-rate limit from I_LIMIT (reuse field as max step here) */
    double max_step = pid->I_LIMIT > 0 ? pid->I_LIMIT : 5000.0;
    if (u >  max_step) u =  max_step;
    if (u < -max_step) u = -max_step;

    /* Runaway guard — reviewed after a real 3 Hz escape reached PWM 63500:
     *  (1) PRIMARY criterion is the measured frequency error from TIM2, not a
     *      PWM-LSB excursion: a hardcoded LSB threshold silently assumes the
     *      OCXO's Hz/LSB sensitivity (a false, per-unit assumption), while
     *      e_freq is hardware truth. Limit: |e_freq| > 0.5 Hz with the phase
     *      railed → freeze.
     *  (2) The baseline must NOT re-anchor on every healthy sample: during a
     *      runaway the phase periodically wraps (briefly un-railed), and the
     *      old guard re-baselined each time — it chased the escape and never
     *      tripped. Re-baseline only when the loop is genuinely healthy
     *      (un-railed AND |e_freq| < 0.25 Hz).
     *  (3) Freezing the step is not enough: the DPLL/LOCK integrator kept
     *      winding up and would slam PWM on recovery — re-seed it while
     *      frozen. */
    /* start_pwm: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */
    /* start_set: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */
    /* runaway_warned: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */
    /* prev_abs_ef: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */   /* |e_freq| at the previous railed cycle */
    /* no_improve: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */     /* consecutive railed cycles with no gain */
    if (!start_set) { start_pwm = pwm; start_set = true; }

    /* Rail threshold from the LC calibration where one exists: the detector band
     * is zero_offset ± half the swept span, so a fixed 3.28 V only happens to be
     * right for one board. */
    float rail_hi = 3.28f;
    if (g_ltic.range_ns > 1.0f && g_ltic.ns_per_volt > 1.0f) {
        float span_v = g_ltic.range_ns / g_ltic.ns_per_volt;
        rail_hi = g_ltic.zero_offset + 0.55f * span_v;   /* high edge of the band */
    }
    bool railed_now = (g_ltic_voltage <= 0.02f || g_ltic_voltage >= rail_hi);
    int32_t pwm_excursion = (int32_t)pwm - (int32_t)start_pwm;

    /* A railed detector together with a large |e_freq| is NOT by itself a
     * runaway — it is the normal state of a cold or far-off OCXO at the start of
     * acquisition: the phase sweeps the whole range in seconds, so the cap sits
     * against a stop while TIM2 still reports the true offset. Freezing there
     * removes the only path back, because the frequency term is exactly what
     * pulls the OCXO into the detector window.
     *
     * What actually distinguishes a runaway is that the correction does not
     * work: with the polarity wrong the loop drives the wrong way and |e_freq|
     * fails to shrink however long it runs. So track improvement across railed
     * cycles and only freeze once the error has stalled for several of them. A
     * healthy acquisition improves every cycle and never trips this; a genuine
     * runaway trips after ~5 corrections. Without this both guards fired during
     * perfectly healthy pull-ins — one observed run moved 3855 LSB while railed
     * and was frozen mid-recovery. */
    double abs_ef = fabs(e_freq);
    if (railed_now) {
        if (prev_abs_ef > 0.0 && abs_ef > prev_abs_ef * 0.98) {
            if (no_improve < 255u) no_improve++;   /* stalled this cycle */
        } else {
            no_improve = 0;                        /* still converging */
        }
        prev_abs_ef = abs_ef;
    } else {
        no_improve  = 0;
        prev_abs_ef = 0.0;
    }
    bool stalled      = (no_improve >= LTIC_RUNAWAY_STALL);
    bool freq_escape  = railed_now && abs_ef > 0.5 && stalled;
    bool lsb_backstop = railed_now && stalled &&
                        (pwm_excursion > 2000 || pwm_excursion < -2000);
    if (freq_escape || lsb_backstop) {
        if (!runaway_warned) {
            OUT_SERIAL.print("LTIC: runaway (");
            OUT_SERIAL.print(e_freq, 2);
            OUT_SERIAL.println(" Hz, phase railed, not converging) — freezing; check LPOL / re-centre.");
            runaway_warned = true;
        }
        /* Freezing outright is a trap, and the 25.08 21:06 run sat in it for
         * the last 527 s: with u = 0 the OCXO stays parked wherever the escape
         * left it (+2.53 Hz there), so the phase races through the detector
         * forever, railed_now never clears, |e_freq| never falls below 0.25,
         * and the release condition below can never be met. The guard removes
         * the only thing that could undo the damage.
         *
         * So walk back to the baseline instead of stopping on the spot.
         * start_pwm is the last PWM the loop held while genuinely healthy —
         * un-railed and inside 0.25 Hz — which is exactly where a runaway
         * should be undone to, whatever caused it (wrong LPOL, a detector that
         * never captured, a gain too high). Bounded to 50 LSB per cycle so the
         * walk is a retreat and not a second escape: 20 000 LSB unwinds in
         * about 33 minutes at the 5 s ACQ cadence, and the guard releases the
         * moment the frequency comes back inside 0.25 Hz on the way. */
        double back = (double)start_pwm - (double)pwm;
        if (back >  50.0) back =  50.0;
        if (back < -50.0) back = -50.0;
        u = back;
        integ = (double)pwm;            /* and stop the integrator winding up */
    } else if (!railed_now && fabs(e_freq) < 0.25) {
        runaway_warned = false;         /* genuinely healthy: recovered */
        no_improve     = 0;
        start_pwm = pwm;                /* re-baseline ONLY here */
    }

    /* ---- ACQ re-arm retry -------------------------------------------------
     * The entry arm above fires once, on the TRANSITION into ACQ. If that sync
     * does not land the phase inside the detector window — the flip-flop's
     * ambiguous point, or the OCXO still far enough off that the phase sweeps
     * straight back out — ACQ gets no second attempt: the state does not
     * change, so the transition never repeats, the detector reads "ovf"
     * indefinitely and only a manual AP recovers it. Reported from the field
     * after a reboot (Dan Wiering).
     *
     * Mirrors algorithm 11's phase-capture bridge: once the frequency has
     * settled but the phase is still railed, re-arm periodically. Gated on the
     * frequency because arming while the OCXO is still far off is pointless —
     * the phase would race out of the window again immediately — and held off
     * afterwards so a failed capture retries about every 20 s rather than every
     * cycle. */
    /* acq_railed_cnt: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */
    /* acq_rearm_hold: hoisted to the top of this function so the restart block can
     * clear it; the storage and the initialiser are unchanged. */
    if (state == LTIC_ACQ) {
        if (acq_rearm_hold > 0) {
            acq_rearm_hold--;
            acq_railed_cnt = 0;
        } else if (railed_now && fabs(e_freq) <= 0.05) {
            if (acq_railed_cnt < 0xFFFFFFFFu) acq_railed_cnt++;
            if (acq_railed_cnt >= 5u) {
                ltic_arm_picdiv();
                acq_railed_cnt = 0;
                acq_rearm_hold = 15u;
                /* The third arm site in this function, and the one easiest to
                 * miss: acq_rearm_hold already stops the LOOP acting, but the
                 * per-second sampler runs before that gate, so without this the
                 * divider's landing jump would still enter the mean and the
                 * noise estimate. */
                s_lt_post_arm  = LTIC_POST_ARM_S;
                ltic_sample_reset();
            }
        } else {
            acq_railed_cnt = 0;
        }
    } else {
        acq_railed_cnt = 0;
        acq_rearm_hold = 0;
    }

    uint16_t out = clamp_pwm((int32_t)pwm + (int32_t)u);

    /* ---- state transitions ---- */
    char trend[5] = "ACQ ";
    if (state == LTIC_ACQ) {
        strcpy(trend, "ACQ ");
        /* ACQ → DPLL: phase inside the ACQ window AND its slope (drift) inside
         * a WIDE window. Slope matters because the phase can sweep through
         * centre with a large slope (frequency still far off) — catching it
         * there would lock onto the wrong frequency. Slope = dPhase/dt, and
         * frequency is the first derivative of phase, so a small slope means
         * the frequency is already close to 10 MHz. Wide slope gate here,
         * tightened at the next transition. (Insight from Dan / time-nuts.) */
        /* Gate on TIM2 (|Δf|), not on the V-derived slope: the stepped
         * detector read produces phantom 50-100 ns/s spikes at each step,
         * which kept the loop parked in ACQ for hundreds of cycles. TIM2 is
         * immune to the stepping. Position (phase_ns) still comes from V. */
        if (ph_valid && fabs(phase_ns) <= (double)g_ltic.acq_threshold_ns
                     && fabs(e_freq)   <= 0.05) {
            if (++stable_cnt >= 3) { state = LTIC_DPLL; stable_cnt = 0; integ = (double)out; }
        } else if (stable_cnt > 0) stable_cnt--;
    } else if (state == LTIC_DPLL) {
        strcpy(trend, "DPLL");
        /* DPLL → LOCK: tight phase AND tight slope, sustained. The gate was
         * 0.4×threshold held for 6 cycles (12 s); with a narrow detector that
         * demanded sub-11 ns centring before LOCK would ever engage. 0.5× for
         * 4 cycles (8 s) reaches LOCK markedly sooner while still requiring a
         * genuinely settled phase — LOCK's own hysteresis (1.5×) and its
         * 3-cycle exit counter still protect against a premature promotion. */
        bool tight = ph_valid && fabs(phase_ns) <= (double)g_ltic.acq_threshold_ns * 0.5
                              && fabs(e_freq)   <= 0.03;
        if (tight) { if (++stable_cnt >= 4) { state = LTIC_LOCK; stable_cnt = 0;
                                             last_lock_pps = ppscount;
                                             /* bumpless: the integrator IS the output,
                                              * so re-seeding it to what was just written
                                              * means the first LOCK update starts from
                                              * where DPLL left the oscillator instead of
                                              * stepping to wherever integ had wound. And
                                              * a fresh averaging window, so LOCK does not
                                              * open with DPLL's transient in its mean. */
                                             integ = (double)out;
                                             ltic_sample_reset(); } }
        else if (stable_cnt > 0) stable_cnt--;   /* a stepped read sets back, not to zero */
        bool broken = !ph_valid || fabs(phase_ns) > (double)g_ltic.acq_threshold_ns * 3.0
                               || fabs(e_freq)   > 0.30;
        if (broken) {
            if (++exit_cnt >= 3) { state = LTIC_ACQ; exit_cnt = 0; ltic_arm_picdiv();
                                   s_lt_post_arm = LTIC_POST_ARM_S;
                                   ltic_sample_reset(); }
        } else if (exit_cnt > 0) exit_cnt--;
    } else { /* LOCK */
        strcpy(trend, "LOCK");
        /* Leave LOCK if the phase leaves a hysteresis band OR the slope grows
         * (frequency drifting away) — both indicate the lock is degrading. */
        double hyst = (double)g_ltic.acq_threshold_ns * 1.5;
        if (!ph_valid || fabs(phase_ns) > hyst || fabs(e_freq) > 0.10) {
            if (++exit_cnt >= 3) { state = LTIC_DPLL; exit_cnt = 0; stable_cnt = 0;
                                   integ = (double)out; }
        } else if (exit_cnt > 0) exit_cnt--;
    }

    /* persist state (cheap; EEPROM only written on ES) */
    g_ltic.state = state;
    prev_state = state;

    set_trend(trend);
    return out;
}

/* ======================================================================
 * ALGORITHM DISPATCHER
 * ====================================================================== */
uint16_t ltic_lars_pi(uint16_t pwm, uint32_t ppscount)
{
    /* ppscount is unused: unlike algo 10, this loop updates every second with
     * no period gate — the adaptive filter provides the smoothing instead. */
    (void)ppscount;

    /* Persistent loop state. Every static this loop owns is declared here, at
     * the top of the function, so the restart hook below can clear all of it in
     * one place — a static declared further down cannot be reached from here,
     * and one left out of the reset is exactly how a switched-to algorithm
     * inherits the previous run's state. */
    static bool     s_pol_warned    = false;  /* polarity warning printed once  */
    static float    s_integ         = 0.0f;   /* dacValue: integral accumulator */
    static float    s_i_remain      = 0.0f;   /* Lars' I_term_remain            */
    static float    s_phase_filt    = 0.0f;   /* filtered phase [ns]            */
    static uint32_t s_near_run      = 0u;     /* s with the phase inside the window */
    static uint32_t s_lock_cnt      = 0;      /* Lars' lockPPScounter           */
    static bool     s_locked        = false;
    static bool     s_init          = false;
    static uint16_t s_tc_old        = 0;      /* detect timeConst change        */
    static uint32_t s_freqok_railed = 0;      /* cycles: freq settled yet railed */
    static bool     s_start_arm = false;      /* (re)start: re-reference once  */
    static uint32_t s_rearm_holdoff = 0;      /* cool-down after a re-arm       */

    /* Fresh entry: see the note at s_algo_restart. s_integ is this loop's DAC
     * accumulator and s_init false makes it re-seed from the PWM actually on
     * the pin, which is the only value known to be right at this moment. This
     * runs before the polarity and calibration guards below, so the state is
     * cleared even on a switch into a loop that cannot yet run. */
    if (algo_take_restart()) {
        s_pol_warned = false;
        s_integ = 0.0f; s_i_remain = 0.0f; s_phase_filt = 0.0f; s_near_run = 0u;
        s_lock_cnt = 0; s_locked = false;
        s_init = false;                     /* re-seed from the live PWM */
        s_tc_old = 0;
        s_freqok_railed = 0; s_rearm_holdoff = 0;
        s_start_arm = true;
        g_lars_locked = false;
    }
    /* An unset polarity is a guess this loop refuses to make: with the wrong
     * sign every correction pushes the phase away. Hold, like algo 10's ACQ. */
    if (g_ltic.polarity == 0) {
        if (!s_pol_warned) {
            OUT_SERIAL.println("LTIC: polarity unset - run 'LPOL -1' (or +1) then 'ES'. Holding.");
            s_pol_warned = true;
        }
        set_trend("ACQ ");
        return pwm;
    }
    /* Refuse to run without TIC calibration — phase has no scale otherwise. */
    if (g_ltic.ns_per_volt == 0.0f || g_ltic.range_ns == 0.0f) {
        /* NOT set_trend(0). That passed a null pointer to strncpy(), which is
         * undefined behaviour rather than a blank indicator, and it was
         * reachable on every board that had not yet been through LC — which is
         * every new board, on its first run, before anything else can go
         * wrong. The state deserves a name in any case: the operator needs to
         * know WHY the loop is holding, and algorithm 13 already answers
         * exactly this question with NoCT for the other missing coefficient. */
        set_trend("NoCT");
        return pwm;
    }

    /* One-time seed: start the integrator at the current PWM so we take over
     * smoothly from whatever value the OCXO was already sitting at. */
    if (!s_init) {
        s_integ      = (float)pwm;
        s_phase_filt = 0.0f;
        s_i_remain   = 0.0f;
        s_lock_cnt   = 0;
        s_locked     = false;
        s_tc_old     = g_lars.time_const_s;
        s_init       = true;
    }

    /* ---- inputs, evaluated EVERY second (no period gate) ---- */
    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    double e_freq = s.have100 ? (s.avg100 - (double)BASE_FREQ)
                  : s.have10  ? (s.avg10  - (double)BASE_FREQ) : 0.0;

    bool   ph_valid = false;
    double phase_ns = ltic_phase_error_ns(&ph_valid, ppscount);   /* 0 = on target       */
    bool   railed   = !ph_valid;

    /* START RE-REFERENCING, before any steering is computed. At boot or after
     * a mid-run switch nobody centres this loop's reference: algo 10's ACQ
     * does it for itself, this one does not, and a valid-but-far reading is
     * inherited as a real phase error — the 28.08 log switched 10->11 onto
     * +1272 ns and the loop moved PWM +2719 LSB in the FIRST second. So at
     * (re)start, one shot: when the phase is readable but outside the ACQ
     * window and the frequency is already home, re-arm the divider and hold
     * this second, instead of steering on somebody else's reference. Cleared
     * the moment the phase is inside the window, so a loop that takes over
     * centred never disturbs the divider; while railed or with the frequency
     * still out, the branches below own the pull-in and the flag waits. */
    if (s_start_arm && !railed) {
        if (fabs(phase_ns) <= (double)g_ltic.acq_threshold_ns) {
            s_start_arm = false;                     /* centred: take over */
        } else if (s_rearm_holdoff == 0u && fabs(e_freq) <= 0.05) {
            ltic_arm_picdiv();
            s_start_arm  = false;
            s_rearm_holdoff = 15u;
            set_trend("ARM ");
            return pwm;                              /* hold this second */
        }
    }

    /* ---- adaptive filter/time constants (Lars 261-263) ----
     * filterConst = timeConst / filterDiv, with the smoothing dropped to 1 while
     * the phase is genuinely out of range so the loop reacts fast during a real
     * acquisition and smooths as soon as it is home.
     *
     * IT USED TO READ `if (!s_locked) filt = 1u;` AND THAT IS A DIFFERENT
     * CONDITION. s_locked is not "the phase is out of range": it is a stopwatch.
     * The lock test below requires phase AND frequency to sit inside their
     * windows CONTINUOUSLY for lock_factor * time_const seconds - five minutes
     * on the defaults - so a loop that is already home stays formally unlocked
     * for five more minutes, and this line kept it in fast, unsmoothed mode for
     * every one of those seconds. Unsmoothed means the loop answers each sample
     * of detector noise at full gain.
     *
     * Measured, 11.09 capture, switching from algorithm 13 to 11 with the phase
     * sitting at 2.4 ns: 299 s of PLL during which the filtered phase never
     * left +/-6.7 ns of a 100 ns window - and the PWM moved by an average of
     * 4.5 LSB per second, as much as 17 in one second, on 145 of those 298
     * seconds by more than 4. Seventeen LSB is 5.4e-10 on this board. The same
     * loop, locked, over 14.6 hours: 0 or 1 LSB every second, 2 LSB forty times,
     * never more. An order of magnitude of output noise, from a flag that was
     * only ever meant to describe how long the loop had been good for.
     *
     * The replacement asks what the fast mode is actually for. RAW phase as well
     * as filtered, because s_phase_filt starts at zero on a restart and would
     * otherwise report "in the window" while the true phase is hundreds of
     * nanoseconds out, smoothing the loop through exactly the acquisition the
     * fast mode exists to hurry. Railed counts as out of range for the same
     * reason. The lock stopwatch still gates the LOCK indication, CS and the
     * display - it just no longer decides how hard the loop hits the EFC. */
    uint16_t time_const = g_lars.time_const_s;
    if (time_const < 1u) time_const = 1u;
    uint32_t filt = (uint32_t)time_const / (g_lars.filter_div ? g_lars.filter_div : 1u);
    if (filt < 1u) filt = 1u;
    {
        double win = (double)g_lars.lock_ns_lim;
        bool near = !railed && ph_valid &&
                    fabs(phase_ns)             <= win &&
                    fabs((double)s_phase_filt) <= win;
        /* AND IT HAS TO HAVE STAYED THERE. A phase on its way OUT passes
         * through the window too, and smoothing it there is how the loop learns
         * about a disturbance late: the switch harness measured algorithm 11
         * ending a 600 s excursion at 340 ns instead of 241 when the window
         * test alone was used. The pre-filter's job is to reject noise, not
         * motion.
         *
         * So the phase must have been inside the window for as long as the
         * filter is about to average over. That is not a new constant - it is
         * `filt` itself, and it is the only self-consistent choice: smoothing
         * over N seconds is meaningful exactly when the last N seconds were
         * describing the same thing. A settled loop waiting out the lock
         * stopwatch has hundreds of such seconds and smooths from the first;
         * a loop whose phase is walking through the window never accumulates
         * them and stays fast, which is what it needs. */
        if (near) { if (s_near_run < 0xFFFFFFFFu) s_near_run++; }
        else        s_near_run = 0;
        if (!s_locked && s_near_run < filt) filt = 1u;
    }

    /* If timeConst changed under us, rescale the integrator so the output does
     * not jump (Lars 266-268: dacValue scales with timeConst). Our integrator
     * is in PWM units directly, so no rescale is needed here — but reset the
     * remainder to avoid a stale fractional carry. */
    if (time_const != s_tc_old) { s_i_remain = 0.0f; s_tc_old = time_const; }

    /* ---- phase pre-filter (Lars 285), outlier-gated ----
     * Exponential smoother with variable constant. Skip the update on a railed
     * reading (no valid phase) so a rail excursion cannot poison the filter. */
    if (!railed) {
        s_phase_filt += ((float)phase_ns - s_phase_filt) / (float)filt;
    }

    /* ---- frequency-led assist when the phase is out of range ----
     * railed → the phase detector is blind, so steer by TIM2 frequency alone,
     * exactly the situation that used to freeze algo 10. e_freq is in Hz;
     * multiply by gain-scaled term to pull PWM back toward the detector window.
     * The sign convention matches the phase path (positive error → reduce PWM),
     * with polarity honoured. */
    double polarity = (g_ltic.polarity == -1) ? -1.0 : 1.0;

    /* Effective frequency-branch scale. g_lars.gain == 0 means "auto": derive it
     * from the CT calibration, exactly as algo 10 does. CT measures K (Hz per PWM
     * LSB) and stores it as g_pid[7].Kp = 0.40/K, so lsb_per_hz = g_pid[7].Kp/0.40
     * is LSB of PWM per Hz of frequency error — which is precisely what the
     * frequency branch needs to convert e_freq into a PWM correction, with no
     * hand-tuning and no board-specific constant. If the user has set a non-zero
     * gain with LG, that takes priority and is used with the fixed 6553.6 scale
     * as before. A light 0.5 factor on the auto path keeps the proportional term
     * from being too hot on boards with a large lsb_per_hz. */
    float freq_scale;      /* LSB per Hz  — used by the acquisition branch */
    float phase_gain;      /* LSB per ns  — used by the phase branch        */
    if (g_lars.gain > 0.0f) {
        freq_scale  = g_lars.gain * 6553.6f;             /* manual: LG value */
        phase_gain  = g_lars.gain;
        g_lars_gain_auto = false;
    } else {
        double lsb_per_hz = (g_pid[7].Kp > 100.0)
                          ? ((double)g_pid[7].Kp / 0.40)  /* from CT          */
                          : 3000.0;                       /* fallback if no CT */
        freq_scale = (float)(0.5 * lsb_per_hz);

        /* The two branches need DIFFERENT units and both must be derived, or the
         * loop half-works: an earlier version fed the auto value to the frequency
         * branch only, leaving the phase branch multiplying by g_lars.gain — which
         * is zero in auto mode. Acquisition then pulled in, the picDIV bridge
         * delivered the phase to the window, and the phase PI did nothing at all.
         *
         * Phase gain is LSB per ns. Nulling a phase error P over the loop time
         * constant needs a fractional frequency offset P/(tau*1e9), which at
         * 10 MHz is P/(100*tau) Hz, so lsb_per_hz/(100*tau) LSB per ns. Half of
         * that is used: the loop also has an integral term, so a proportional
         * term sized to null the error inside one time constant on its own is too
         * hot. The result lands within 15% of the hand-tuned 0.3 that produced a
         * clean lock on the bench, and unlike a fixed number it follows both the
         * measured VCO slope and whatever LTC is set to. */
        phase_gain = (float)(0.5 * lsb_per_hz / (100.0 * (double)time_const));
        g_lars_gain_auto = true;
    }
    g_lars_scale = freq_scale;

    float p_term, i_term;
    if (railed) {
        /* Frequency pull-in as a proper PI, with the PROPORTIONAL term doing the
         * work and the integrator contributing only a small trim. An earlier
         * version fed the full frequency correction into the integrator; that
         * integrator then carried momentum — it reached e_freq=0 with a pile of
         * accumulated LSB, sailed past, and the loop oscillated ±2 Hz around the
         * target with a ~150 s period, never settling into the phase window
         * (seen on air). The fix mirrors Lars' own structure (strong P, integral
         * = a fraction of P): P responds to the current e_freq and self-brakes as
         * the frequency comes down, while a light integral removes any residual
         * static offset without storing momentum.
         *
         * SIGN — from hardware, not assumed. A cold start showed the OCXO +8.4 Hz
         * fast with PWM driven UP to 65535, making it faster: a runaway. So the
         * frequency error needs the OPPOSITE sign from the phase branch (phase and
         * frequency have opposite orientation on this wiring): +polarity*e_freq
         * here vs -polarity below. Positive e_freq (fast) then lowers PWM. */
        float freq_lsb = (float)(polarity) * (float)e_freq * freq_scale;

        /* P dominates and self-brakes; I is a small fraction (Lars: I=P/damping,
         * and here further reduced so acquisition cannot wind up momentum). */
        p_term = freq_lsb;
        i_term = freq_lsb / g_lars.damping / 10.0f + s_i_remain;

        /* STEP CLAMP on the integral only. P is allowed its full self-limiting
         * value (it shrinks with e_freq on its own); the integral is what could
         * accumulate an overshoot, so cap its per-cycle contribution. The real
         * VCO slope (Hz/LSB) is unknown and folded into gain, so a bounded walk
         * is what keeps acquisition from leaping past the lock point. */
        const float ACQ_MAX_ISTEP = 50.0f;
        if (i_term >  ACQ_MAX_ISTEP) i_term =  ACQ_MAX_ISTEP;
        if (i_term < -ACQ_MAX_ISTEP) i_term = -ACQ_MAX_ISTEP;

        /* Also bound the total P excursion so a huge cold-start e_freq cannot
         * slam PWM across the whole span in one cycle; large enough to move
         * briskly, small enough not to overshoot a nearby lock point. */
        const float ACQ_MAX_P = 2000.0f;
        if (p_term >  ACQ_MAX_P) p_term =  ACQ_MAX_P;
        if (p_term < -ACQ_MAX_P) p_term = -ACQ_MAX_P;

        /* ANTI-WINDUP / runaway stop — safety net independent of sign. If PWM is
         * at a rail and the integral would push further in, freeze it, so the
         * loop can never sit pinned at 0 or 65535. */
        bool at_hi = (s_integ >= 65534.0f);
        bool at_lo = (s_integ <= 1.0f);
        if ((at_hi && i_term > 0.0f) || (at_lo && i_term < 0.0f)) {
            i_term     = 0.0f;
            s_i_remain = 0.0f;
        }
    } else {
        /* Lars' phase PI (292-293). ltic_phase_error_ns() already returns the
         * phase relative to the calibrated band centre (zero_offset), so 0 IS
         * the target and we drive the filtered phase to zero directly. gain is
         * DAC bits per ns. NOTE: g_lars.tic_offset is stored and available but
         * intentionally NOT summed in here — phase_ns is already centred on the
         * LC-measured zero, and adding tic_offset as a second reference would
         * fight it. tic_offset is reserved for an explicit target-shift feature
         * (like algo 10's centre_v) once we decide how the two should compose. */
        float phase_err = s_phase_filt;                 /* already relative to 0 */
        p_term  = (float)(-polarity) * phase_err * phase_gain;
        i_term  = p_term / g_lars.damping / (float)time_const + s_i_remain;
    }

    /* ---- integrate with remainder preserved (Lars 293-296) ---- */
    long  i_whole = (long)i_term;
    s_i_remain    = i_term - (float)i_whole;
    s_integ      += (float)i_whole;

    /* ---- optional temperature feed-forward (Lars 306) ----
     * dacValue += (tempRef - tempFiltered) * tempCoeff. Off unless enabled. */
    float temp_ff = 0.0f;
    if (g_lars.flags & LARS_FLAG_TEMP_COMP) {
        extern float g_bmp_temp;                         /* board temperature °C */
        /* Map temperature to ADC-like counts the same way tempRef is stored:
         * tempRef is in ADC counts, so we approximate the current temp in the
         * same units. A dedicated temp-ADC channel can replace this later. */
        float temp_now = g_bmp_temp * 100.0f;            /* 0.01 °C resolution */
        temp_ff = ((float)g_lars.temp_ref - temp_now)
                * (float)g_lars.temp_coeff / 10000.0f;
    }

    /* ---- output = integrator + proportional + temp (Lars 297, 306) ---- */
    float out_f = s_integ + p_term + temp_ff;

    /* clamp to valid PWM range */
    if (out_f < 0.0f)     out_f = 0.0f;
    if (out_f > 65535.0f) out_f = 65535.0f;
    /* keep the integrator itself bounded so it cannot wind up past the rail */
    if (s_integ < 0.0f)     s_integ = 0.0f;
    if (s_integ > 65535.0f) s_integ = 65535.0f;

    uint16_t out = (uint16_t)(out_f + 0.5f);

    /* ---- lock detection (Lars 230-242): phase in window AND frequency in
     * window, held for lock_factor * timeConst seconds ---- */
    double phase_win_ns = (double)g_lars.lock_ns_lim;
    bool   in_phase = ph_valid && fabs(s_phase_filt) <= phase_win_ns;
    bool   in_freq  = fabs(e_freq) <= 0.02;             /* ~20 ppb, Lars' gate */
    if (in_phase && in_freq) {
        if (s_lock_cnt < 0xFFFFFFFFu) s_lock_cnt++;
    } else {
        s_lock_cnt = 0;
    }
    s_locked = (s_lock_cnt > (uint32_t)time_const * g_lars.lock_factor);

    /* publish live state for the Learn telemetry line */
    g_lars_locked     = s_locked;
    g_lars_phase_filt = s_phase_filt;

    /* ---- phase-capture bridge -------------------------------------------
     * The frequency branch can drive e_freq to ~0 while the phase is still
     * railed far outside the detector window (seen on air: e_freq ±0.03 Hz but
     * dph stuck at ~1926 ns, trend ACQ forever). At zero frequency error the
     * phase no longer moves, so it can never drift into range on its own — the
     * loop is disciplined in frequency but stranded in phase. Algo 10 avoids
     * this because entering ACQ re-arms the picDIV, which resyncs the divider to
     * the 1PPS edge and snaps the phase to ~0; algo 11 never armed it at all.
     * So: once the frequency is genuinely settled but the phase is still railed,
     * re-arm the picDIV ONCE to bring the phase into the window, then let the
     * phase branch take over. Guarded by a hold-off counter so it fires at most
     * once per stranding, not every cycle. */
    if (s_rearm_holdoff > 0) {
        s_rearm_holdoff--;
        s_freqok_railed = 0;
    } else if (railed && fabs(e_freq) <= 0.05) {
        /* frequency good, phase blind: count how long we have been stranded */
        if (s_freqok_railed < 0xFFFFFFFFu) s_freqok_railed++;
        /* wait a few seconds of steady frequency before disturbing the divider,
         * so a brief frequency zero-crossing during acquisition does not trigger
         * a needless re-arm */
        if (s_freqok_railed >= 5u) {
            ltic_arm_picdiv();
            s_freqok_railed = 0;
            s_rearm_holdoff = 15u;   /* let the phase settle before trying again */
        }
    } else {
        s_freqok_railed = 0;
    }

    /* Trend string for telemetry/displays/tuner (4 chars). Uses the same
     * ACQ/PLL/LOCK vocabulary as algo 10 so the displays read consistently;
     * algo 11 says PLL where algo 10 says DPLL, which also keeps the two
     * distinguishable in logs. */
    set_trend(s_locked ? "LOCK" : (railed ? "ACQ " : "PLL "));

    return out;
}
#endif /* GPSDO_LTIC */

/* ======================================================================
 * ALGORITHM 12 — Multi-level accumulator
 *
 * After Alan Cashin's (MIS42N on EEVblog) Budget GPSDO, transcribed from the
 * PIC16F1455 assembly he publishes on SourceForge. The structure is his; this is
 * a port rather than a reinterpretation, so that a misbehaviour is traceable to
 * the transcription rather than to a rewrite of the idea.
 *
 * WHAT PROBLEM IT SOLVES
 * ----------------------
 * Every other loop here has one time constant, and that constant is a compromise
 * nobody wins. Measured on Dan Wiering's bench against a rubidium reference:
 *
 *     tau          LTC 60              LTC 240
 *     10-400 s     worse               up to 1.44x better
 *     800-2000 s   up to 1.58x better  worse
 *
 * You pick one. Short tracks the oscillator and lets GPS noise in; long rejects
 * the noise and is slow to catch real drift.
 *
 * This does not pick. Readings accumulate into a hierarchy of levels, level n
 * covering 2^(n+1) seconds, and a correction is applied at the LOWEST level whose
 * error exceeds that level's limit. A large error acts within two seconds; a small
 * one waits for a longer average before anything is done. The error chooses its
 * own averaging time.
 *
 * HOW THE LEVELS WORK — no ring buffers, no loop over timescales
 * -------------------------------------------------------------
 * The level comes out of the bit pattern of the seconds counter: inspect from the
 * least significant bit upward, stop at the first zero. Level n then receives a
 * value once every 2^n seconds exactly, for one 16-bit variable per level. Eleven
 * levels — 2 s to 2048 s — cost 22 bytes.
 *
 * At each level two stored values, A (older) and B (newer):
 *
 *     slope = B - A                  frequency error over the level's span
 *     phase = (A + B) + 2*(B - A)    error extrapolated to end of period
 *
 * Both are tested against per-level limits. Exceed either and a correction is
 * applied and collection restarts from level 0. Pass both and A+B is promoted to
 * the next level, where the same test runs over twice the span.
 *
 * TWO DETAILS WORTH KEEPING
 * -------------------------
 * The frequency test is SKIPPED when slope and phase have opposite signs: the
 * loop is already correcting the phase and testing the frequency would add a
 * redundant nudge. One XOR on the sign bit.
 *
 * Each reading enters as 2*x+1, not x. Alan found that assigning zero to a
 * reading inside the target window let the phase wander; forcing every reading to
 * carry a significant value pinned it, and converges the mean of early and late
 * arrivals on zero rather than on the middle of a quantisation bin. The same
 * principle as dither in a converter: a dead zone in a control loop is worse than
 * noise, because the loop pushes, sees nothing, pushes harder, and then jumps.
 *
 * NOT AN LTIC ALGORITHM
 * ---------------------
 * This works on the raw TIM2 count, so it needs no phase detector and no picDIV.
 * That is the point: Alan's design reaches parts in 10^11 from a PIC and a NEO-6M
 * for under twenty dollars precisely because it never needs the hardware that
 * algorithms 10 and 11 depend on. So this is the algorithm for a board that has
 * none — and a fair comparison for one that does.
 *
 * STATUS: UNTUNED. The limits below are scaled from Alan's, whose counter
 * resolves 25 ns where TIM2 here resolves 100 ns, and whose receiver had no
 * sawtooth correction where this firmware applies qErr. They are a starting point
 * and are expected to need adjusting against measurement.
 * ====================================================================== */

/* Levels 0..MLACC_LEVELS-1 span 2..2048 seconds. Past that an OCXO's own drift
 * dominates whatever the averaging recovers — Alan's limit, and his reasoning
 * carries over. */
/* Per-level limits: {phase, slope}, in TIM2 ticks.
 *
 * Alan's table, scaled. His counter resolves 25 ns against 100 ns here, so his
 * tick counts are divided by four; but his receiver had no sawtooth correction
 * and this one does, so the phase limits are not loosened further to compensate
 * for jitter that is no longer present. The result is a table that should be in
 * the right region and is certainly not yet right.
 *
 * The pattern matters more than the numbers: limits grow with level, because a
 * longer average tolerates a larger absolute error before it is worth acting on,
 * while the frequency limit tightens in relative terms. */
/* Shortest time a correction may be spread over, in seconds.
 *
 * Alan corrects over the measurement span and calls the choice arbitrary — "it
 * could be shorter or longer". Taken literally it is far too aggressive at the
 * low levels: a 1012 ns error seen at level 0 asks to be nulled in two seconds,
 * which needs 5 Hz, which is 15685 DAC counts on this oscillator. It clamped,
 * the phase could not be cleared, and the oscillator was thrown far enough that
 * the arming gate never opened — so the detector stayed railed and the loop
 * could not recover. Measured: 14000 counts of PWM swing.
 *
 * A GPSDO wants a large error corrected gently. The floor makes a big excursion
 * take a minute rather than two seconds, still far quicker than the oscillator
 * drifts. */
/* ---- MEASURED per-level limits (MF 3) -------------------------------------
 *
 * The formula this replaces is
 *
 *     thr[L] = 8 * sigma * sqrt(2^L) * sqrt(10)
 *
 * and every term in it is an assumption. sqrt(2^L) says the phase is WHITE, so
 * that averaging 2^L samples reduces the test by 2^(L/2). Measured on two
 * boards of this design — same PCB, same OCXO, different rooms — the exponent
 * is 0.95 and 1.03, not 0.50. Averaging buys almost nothing, because the phase
 * that matters here is a slow wander (autocorrelation 0.96 at 60 s, 0.64 at
 * 300 s) and not sample-to-sample noise.
 *
 * The error compounds: at level 0 the formula understates the real spread by
 * about 5x, at level 10 by over 100x, so the table falls 32x across the
 * hierarchy where the phase itself falls by 1.3x. On a board holding 5 ns of
 * phase the crossing still lands at level 4-6 and the loop behaves; on the same
 * board in a noisier room, holding 26 ns, it lands at level 0-1 — below
 * MLACC_FREQ_MIN_LEVEL, where the frequency term is gated off and the
 * correction is a bare slew. That is the hunting mode this file's own
 * correction comment warns about.
 *
 * So the exponent is measured instead of assumed. Each level keeps the mean
 * square of its own test statistic; a least-squares fit of log2(sd) against
 * level gives the amplitude and the exponent together, and the table is built
 * from the fit. Fitting ACROSS levels rather than trusting each level alone is
 * what makes it usable early: level 8 is evaluated once every 512 s and would
 * need half a day to have a variance of its own, but it does not need one — the
 * low levels populate in minutes and the fit extrapolates.
 *
 * WHAT KEEPS IT FROM CHASING ITSELF. Two failure modes are on record in this
 * file, and the design has to survive both.
 *
 *   Downward: the loop suppresses the phase, the spread falls, the threshold
 *   falls, the loop acts on less. It stops on its own, and on a floor that is
 *   MEASURED rather than declared: once the phase is down to the per-sample
 *   noise, consecutive readings decorrelate and sd(3b-a) can fall no further
 *   than 2*sigma*sqrt(10). The old code wrote that floor as a constant 5 ns,
 *   which is a property of THIS detector and this receiver's sawtooth, not of
 *   the arithmetic.
 *
 *   Upward: an uncorrected frequency error ramps the phase, the ramp inflates
 *   the spread, the threshold grows with the error it exists to catch and the
 *   loop freezes. That is what the exponent clamp below is for, and it is why
 *   the clamp is [0.5, 1.0] rather than a wider band picked for comfort: 0.5 is
 *   white noise, the most that averaging can ever remove, and 1.0 is a spread
 *   flat in nanoseconds. A fit above 1.0 is not a noisier board — it is a ramp,
 *   and accepting it would be that second failure exactly.
 *
 * The adaptation time constant is the full depth of the hierarchy, so every
 * level averages over the same wall-clock span rather than the same number of
 * evaluations. Nothing here is tuned to a board: the only quantity a user sets
 * is how often a correction may be triggered by noise alone (MFT), and the
 * per-level multiplier follows from it, which is what the hand-picked 8 in the
 * formula was standing in for. */
#define MLACC_FIT_MIN_N     24u        /* evaluations before a level may be fitted */
#define MLACC_FIT_TAU_S     ((double)(1u << (MLACC_LEVELS + 1)))  /* 4096 s */
#define MLACC_ALPHA_WHITE   0.5        /* what white noise gives                */
#define MLACC_ALPHA_FLAT    1.0        /* spread flat in ns; above this is a ramp */

uint8_t  g_mlacc_thr_src   = MLACC_THR_FOLLOW;
uint16_t g_mlacc_thr_tgt_s = 0;        /* 0 = MLACC_THR_TGT_DEFAULT */

static double   s_mla_ms_test[MLACC_LEVELS];   /* EMA of test^2, per level */
static uint16_t s_mla_test_n[MLACC_LEVELS];
static mlacc_fit_t s_mla_fit;

#define MLACC_MIN_HORIZON  64

/* Lowest hierarchy level at which the frequency term is trusted. Below this the
 * pair-slope is dominated by its own noise — see the sign/gate block in
 * multi_level_accum() for the arithmetic and the measurement. The TIM2 trim
 * below is not bound by this: it reads the counter, not the slope. */
#define MLACC_FREQ_MIN_LEVEL 3

/* Gate for the TIM2 frequency trim, in Hz. avg100 resolves 0.01 Hz, so 0.03 Hz
 * is three counts of the measurement. Below it the counter has nothing to say
 * and the pair-slope term owns the fine work as before. */
#define MLACC_TIM2_TRIM_GATE 0.03

/* Per-level phase limits, in accumulator units (1 ns per LSB of phase).
 *
 * These are Alan's own table, multiplied by 25 — his counter stepped 25 ns where
 * the LTIC detector steps about 1, and the accumulator arithmetic is otherwise
 * identical, so his numbers carry across by simple scaling.
 *
 * The first attempt scaled them down instead, on the reasoning that a finer
 * detector should permit tighter thresholds. That was wrong, and the measurement
 * says so. What sets the floor is not the detector's resolution but the GPS
 * signal's own wander, which is the same for both of us — Alan's note about the
 * one derived value makes the point exactly: the specification allowed 64 ns at
 * 64 s, but "a cheap GPS module with a poor signal can often wander +/-100 ns and
 * generate a false error", so he used 125 ns at 128 s instead.
 *
 * Halving his numbers put the level-0 threshold at 225 ns against a phase that
 * swings +/-400 ns, so corrections fired on noise: 177 of them in 720 seconds,
 * with the loop never climbing past level 3. Restored, level 0 sits at 462 ns and
 * noise alone no longer reaches it.
 *
 * The phase threshold each entry represents is limit / 2^(level+2), since the
 * accumulator holds 2^(level+1) samples of (2*phase + 1):
 *
 *     level 0    462 ns     level 4    191 ns     level 8    108 ns
 *     level 1    400 ns     level 5    164 ns     level 9    103 ns
 *     level 2    331 ns     level 6    126 ns  <- the derived one
 *     level 3    264 ns     level 7    117 ns     level 10    63 ns
 *
 * Runtime rather than const: only the 128 s value was ever derived from a
 * specification and Alan calls the rest arbitrary, so every board will want to
 * adjust them. Editable with MLP, listed by ML, saved with ES ALGO12. */
int32_t g_mlacc_lim[MLACC_LEVELS] = {
    /* level  0,    2 s */    1850,
    /* level  1,    4 s */    3200,
    /* level  2,    8 s */    5300,
    /* level  3,   16 s */    8450,
    /* level  4,   32 s */   12200,
    /* level  5,   64 s */   21050,
    /* level  6,  128 s */   32350,   /* 126 ns — the one derived value */
    /* level  7,  256 s */   59700,
    /* level  8,  512 s */  110300,
    /* level  9, 1024 s */  210200,
    /* level 10, 2048 s */  259200,
};

/* Static run state. Not user-tunable — that lives in g_mlacc_* above. */
static int32_t  s_mla_val[MLACC_LEVELS];   /* stored value per level          */
static uint32_t s_mla_count;               /* seconds since last correction   */
static uint8_t  s_mla_last_level;          /* level that last acted           */
static uint32_t s_mla_corrections;
/* Seconds since the loop last LOST the phase, not since it last acted. A
 * correction or a zero-crossing is the loop working, not the loop struggling —
 * coupling the LOCK lamp to s_mla_count showed "ACQ" for 40% of a healthy
 * run (measured, 17.08 log: corrections every ~4 min, 64 s of count needed).
 * Reset only where control was genuinely absent: WAIT/SYNC/FLL/NOPH. */
static uint32_t s_mla_quiet;
/* Scale from phase to DAC counts, published so the zero-crossing test can use it
 * before the correction block computes it. */
static double   s_mla_lsb_ns;
static bool     lsb_per_ns_ready;
static double   s_mla_ms_phase;     /* mean square phase, for the noise estimate */
static uint32_t s_mla_ms_n;
/* Zero-crossing state. See the block in multi_level_accum() for why this is
 * not optional. */
static bool     s_mla_returning;    /* a correction's slew is still settling  */
/* ZERO-CROSS ARMING IS NOT THE SAME THING AS SETTLING (Alan's rule).
 *
 * s_mla_returning has always done two jobs: it suppresses a second correction
 * while the first one's deliberate slew is still walking the phase home (our
 * addition, after the 14.08 +3800 LSB overshoot and the 4000-LSB limit cycle
 * that followed it), and it arms the zero-crossing test.
 *
 * Only the second job belongs to Alan's rule, and his rule is narrower than
 * our port made it: the zero-cross correction follows a correction that fired
 * because a LIMIT was crossed, and nothing else. A scheduled correction (the
 * MR run-level path) is not chasing an overshoot — it fires on a timer with
 * the phase wherever it happens to be, so there is no known slew for the
 * crossing to cancel and no reason to expect a crossing at all. Arming it
 * there just lets an unrelated later crossing pull a step out of a stale
 * slope. A zero-cross correction itself likewise does not re-arm: it IS the
 * cancellation, and mlacc_reset() leaves the state fresh behind it.
 *
 * So the settling suppression stays on every correction, and the arming is
 * split out here and set only on the limit path. */
static bool     s_mla_zc_armed;     /* zero-cross test armed (LIMIT path only) */
static uint32_t s_mla_wait;         /* samples since the correction           */
static int32_t  s_mla_ph_sign;      /* sign of the phase when the limit fired */
static int32_t  s_mla_slew_lsb;     /* deliberate slew, removed at the crossing */
static uint32_t s_mla_zc_hits;      /* zero-crossing corrections applied      */
static uint32_t s_mla_armed;        /* picDIV re-arms, for telemetry */
/* Seconds to ignore the detector after a picDIV re-arm: the divider lands the
 * phase at a quantised offset (~±3 µs on this build), and that jump must not
 * enter the accumulator or the noise estimate. */
static uint32_t s_mla_post_arm;
/* STALL WATCH — is the detector still following the oscillator?
 *
 * Two half-windows of MLACC_STALL_W samples each. The DIFFERENCE of their means
 * is the phase's drift over the window, and its noise is sigma*sqrt(2/W), which
 * is a great deal smaller than sigma itself. That distinction is the whole
 * point: an earlier attempt compared single samples against a reference and
 * reset on any 4-sigma excursion, which the per-second noise does almost every
 * second, so the counter never got anywhere and the guard never fired on the
 * board it was written for. Averages, not samples. */
#define MLACC_STALL_W 32u
static double   s_mla_dw_sum;       /* running sum of the current half-window   */
static uint16_t s_mla_dw_n;         /* samples in it                            */
static double   s_mla_dw_a;         /* mean of the previous half-window         */
static bool     s_mla_dw_have_a;
static uint8_t  s_mla_dw_still;     /* consecutive windows with no drift        */
/* Was the PREVIOUS second a usable, contiguous phase reading? The noise
 * estimate below works on consecutive differences, so a difference taken
 * ACROSS a gap (NOPH, SYNC, a re-arm) is not a measurement of noise — it is
 * the gap. Tracking this structurally is what lets the outlier gate stop
 * being self-referential; see the estimator block. */
static bool     s_mla_prev_valid;
static int32_t  s_mla_last_slope;
static int32_t  s_mla_last_phase;

/* ONE LOCK VERDICT, DECIDED WHERE THE ALGORITHM DECIDES IT.
 *
 * Algorithms 10 and 11 have published a live lock state for a long time
 * (g_ltic.state, g_lars_locked) and everything that needed to know asked them.
 * 12 and 13 published nothing, so every consumer invented its own test: the
 * display read the trend string, and the correction statistics in
 * gpsdo_health.cpp could not read anything at all and simply excluded both
 * algorithms through a `default:` — while CS told the user the reason was
 * "running an algorithm below 10", which for 12 and 13 is not true. Two of the
 * board's own quality numbers were therefore blank on the two newest loops,
 * with a false explanation printed underneath.
 *
 * These two flags are that missing statement. They are set at the one place in
 * each algorithm where the loop actually knows, and read by anyone who asks. */
static bool     s_mla_locked;   /* algorithm 12 */
static bool     s_kf_locked;    /* algorithm 13 */

/* Plain reads of a bool written by the control task. Both consumers are asking
 * "is the loop settled right now", a question whose answer is already one
 * second old by the time anything draws it, so a torn read is not a failure
 * mode that exists here: there is nothing to tear. Carrying the flags through
 * the CtrlData_t snapshot instead would have meant a second copy that could
 * disagree with this one, which is the fault being repaired rather than a fix
 * for it.
 *
 * DELIBERATELY OUTSIDE #ifdef GPSDO_LTIC, unlike the algorithms that write
 * them. Both callers are compiled on every board — gpsdo_health.cpp switches on
 * a runtime algorithm number and the display asks per second — so guarding
 * these would mean guarding the call sites too, in two files, to say something
 * the flags already say: on a board with no phase detector neither algorithm
 * can run (LA 12 and LA 13 refuse at the CLI, and the dispatcher falls through
 * to algorithm 0), so neither flag is ever set and both read false. */
bool mlacc_locked(void) { return s_mla_locked; }
bool kf_locked(void)    { return s_kf_locked; }

/* Every loop clears its own state on its next call (see s_algo_restart), and
 * no loop inherits another's lock verdict. The verdicts are read by
 * health_note_output() on every DAC write, including the writes that happen
 * between the restart and the incoming algorithm's first run — so a flag left
 * true by a loop that is no longer running, or that is about to start over in
 * different units, would put those writes into the statistics under a verdict
 * nobody has earned.
 *
 * Called by the dispatcher on an algorithm change — the one place every change
 * goes through, the CLI and a settings recall at boot included — by a move of
 * the span jumper, which changes the units of every integrator at once, and by
 * CT and C, which put a new code on the pin behind every loop's back. */
void algo_request_restart(void)
{
    s_algo_restart = true;
    s_mla_locked   = false;
    s_kf_locked    = false;
    /* Algorithm 11 also clears its own on its next call; clearing it here as
     * well closes the gap between this call and that one, which is the gap
     * the note above is about. */
    g_lars_locked  = false;
}

/* Scale from phase error to DAC counts. Zero means derive it from the CT
 * calibration, which is what algorithm 11 does and what most users want. */
float   g_mlacc_gain = 0.0f;
/* Force a correction once this level is reached, whatever the limits say —
 * otherwise a slow drift under every limit would never be acted on. */
uint8_t g_mlacc_run_level = 7;   /* 256 s: Alan's default — breadboard-friendly, stops limiting ~15 min after cold start */
/* Zero-crossing correction on/off. On by default because Alan calls it essential,
 * but switchable: it has been the source of two separate faults here and a tester
 * needs to be able to take it out of the picture without recompiling. */

/* Published for telemetry: the last count error seen, and the state of the
 * hierarchy. The tuner plots the offset, so it has to leave the algorithm. */
/* int16_t, not int8_t: this now carries phase in nanoseconds, and the detector
 * band alone spans several hundred. The first version held the count error in
 * whole hertz, where int8_t was ample — the type had to grow with the meaning. */
int16_t g_last_offset = 0;

/* Outside the GPSDO_LTIC guard, like mlacc_get_stats() below it and like every
 * g_mlacc_* global: the accessors and the settings fields exist on any build so
 * that the CLI and the flash ring do not need a guard of their own. Only the
 * loop itself is conditional. */
void mlacc_get_fit(mlacc_fit_t *out)
{
    if (out) *out = s_mla_fit;
}

void mlacc_get_stats(mlacc_stats_t *out)
{
    out->last_action  = s_mla_last_level;
    out->corrections  = s_mla_corrections;
    out->arms         = s_mla_armed;
    out->sigma_ns     = (int32_t)sqrt(s_mla_ms_phase);
    out->zero_cross   = s_mla_zc_hits;
    out->seconds      = s_mla_count;
    out->last_slope   = s_mla_last_slope;
    out->last_phase   = s_mla_last_phase;
    /* Same normalisation the correction uses (see p_ns at the CORR site): the
     * accumulated value is halved and divided by the level's span. Written
     * once here rather than repeated wherever somebody wants nanoseconds. */
    out->est_ns       = (float)((double)s_mla_last_phase / 2.0 /
                                (double)(1u << (s_mla_last_level + 1)));
}

static void mlacc_reset(void)
{
    /* Clears the accumulator hierarchy only. The zero-crossing flag deliberately
     * survives: it is set BY a correction and has to outlive the reset that
     * correction performs, or the crossing it is waiting for never gets noticed. */
    for (int i = 0; i < MLACC_LEVELS; i++) s_mla_val[i] = 0;
    s_mla_count = 0;
}

#ifdef GPSDO_LTIC
/* Algorithm 12 requires the phase detector. It once had a fallback that
 * integrated the TIM2 count error on boards without one; that fallback
 * accumulated a random walk of quantisation noise rather than phase and
 * destroyed the lock, so it is gone and the whole function is guarded instead.
 * LA 12 refuses at the CLI when this is not defined. */

/* Standard normal quantile by bisection on erf(). Called eleven times every
 * 300 s, so the cost does not matter and a closed-form approximation would only
 * add a second thing to be wrong about. */
static double mlacc_probit(double p)
{
    if (p <= 0.5) return 0.0;
    double lo = 0.0, hi = 8.0;
    for (int i = 0; i < 60; i++) {
        double m = 0.5 * (lo + hi);
        if (0.5 * (1.0 + erf(m / sqrt(2.0))) < p) lo = m; else hi = m;
    }
    return 0.5 * (lo + hi);
}

/* How many sigma at this level, from the interval the user is willing to see
 * between corrections that noise alone triggered.
 *
 * This is the term the formula's hard-coded 8 was doing by hand. Its comment
 * says as much — "the hierarchy tests level 0 twice as often as level 1 and
 * eight times as often as level 3, so a threshold that is merely unlikely per
 * test still fires constantly at the bottom" — and then picks a single number
 * for every level anyway. The test rate IS the level, so the multiplier can be
 * derived rather than chosen: level L is evaluated every 2^(L+1) seconds, so
 * allowing one false fire per tgt seconds fixes the tail probability. */
static double mlacc_level_q(int L, double tgt_s)
{
    double p = (double)(1u << (L + 1)) / tgt_s;
    if (p > 0.5) p = 0.5;              /* at the top the hierarchy is MR's job */
    return mlacc_probit(1.0 - 0.5 * p);
}

/* Which source is actually driving the table.
 *
 * MLACC_THR_FOLLOW used to mean "stored table whenever a gain has been typed",
 * kept so an installation that never touches MF saw what it always had. That
 * compatibility is what the 26.08 log cost. MG was set to 2.130 — algorithm
 * 11's `gain`, in its units, not this loop's — and the single entry did two
 * things at once: it made every correction 14.7x too small AND it silently
 * swapped the limit table from the noise formula to the stored one, whose
 * level-6 limit is ~126 ns. The loop then corrected rarely, weakly, and far too
 * late: phase sd 41 ns and a 2.3 hour limit cycle, against algorithm 11's 3.0 ns
 * on the same board the same afternoon.
 *
 * The welding was already argued to be wrong when MF was introduced — the gain
 * belongs to the OSCILLATOR (LSB per ns is a property of its EFC) while the
 * limits belong to the PHASE NOISE the board sees. Nothing about typing a gain
 * says anything about the noise. So FOLLOW now means the formula, and an
 * installation that genuinely wants the hand-edited MLP table asks for it with
 * MF 1, which is a sentence rather than a side effect. */
static uint8_t mlacc_thr_source(void)
{
    if (g_mlacc_thr_src != MLACC_THR_FOLLOW) return g_mlacc_thr_src;
    return MLACC_THR_SIGMA;
}

/* The white-noise table, unchanged in arithmetic and moved out of the control
 * path so MF can select it independently of the gain. */
static void mlacc_build_sigma(void)
{
            double sigma = sqrt(s_mla_ms_phase);
            /* Floor: this detector plus the sawtooth cannot honestly resolve
             * below a few ns, and a sigma under that only means the estimator
             * has been starved, not that the board got quiet. */
            if (sigma < 5.0) sigma = 5.0;
            if (sigma > 1.0 && sigma < 20000.0) {
                /* The threshold applies to the TEST EXPRESSION, not to the
                 * average phase — a distinction that cost a full round of
                 * measurement to notice.
                 *
                 * The test is |(a+b) + 2*(b-a)| = |3b - a|, where a and b are
                 * each sums of 2^L samples. So sd(a) = sd(b) = sigma*sqrt(2^L),
                 * and sd(3b - a) = sigma*sqrt(2^L)*sqrt(10). Setting the
                 * threshold from sigma/sqrt(N) instead — the standard error of
                 * the mean — makes it 4.5x too low at level 0 and worse above,
                 * so noise crossed it constantly and the hierarchy reset before
                 * it could climb. Simulated: level 0 firing 1040 times in 4000 s
                 * with levels 3 and above never reached.
                 *
                 * Five sigma on the correct quantity fires on noise about once in
                 * 3.5 million tests. Real drift accumulates in both terms and
                 * crosses far sooner, because it does not cancel the way noise
                 * does. */
                for (int L = 0; L < MLACC_LEVELS; L++) {
                    double sd_test = sigma * sqrt((double)(1u << L)) * sqrt(10.0);
                    /* Eight sigma, not the usual three or four.
                     *
                     * The hierarchy tests level 0 twice as often as level 1 and
                     * eight times as often as level 3, so a threshold that is
                     * merely "unlikely" per test still fires constantly at the
                     * bottom. Six sigma settled the loop — phase to 10 ns RMS
                     * about a zero mean over seven hours — but was still applying
                     * a correction every 50 s once the noise had fallen to 9 ns,
                     * which is the loop reacting to its own noise floor rather
                     * than to drift. Eight quiets that without loosening the
                     * response to anything real: drift accumulates in both terms
                     * of the test and crosses far sooner than noise, which
                     * largely cancels.
                     *
                     * Adjustable per level with MLP if a board wants otherwise. */
                    int32_t units  = (int32_t)(8.0 * sd_test);
                    if (units < 100) units = 100;
                    g_mlacc_lim[L] = units;
                }
            }
    s_mla_fit.valid = false;
}

/* The measured table. Fits log2(sd of the level-L test) against L and builds
 * the whole table from the fit — amplitude and exponent both measured, and the
 * per-level multiplier derived from MFT rather than chosen. See the block above
 * MLACC_MIN_HORIZON for why every constant the formula version carries is gone
 * and what stops the estimate chasing its own tail. */
static void mlacc_build_measured(void)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int n = 0;
    for (int L = 0; L < MLACC_LEVELS; L++) {
        s_mla_fit.tests[L] = s_mla_test_n[L];
        if (s_mla_test_n[L] < MLACC_FIT_MIN_N) continue;
        if (s_mla_ms_test[L] <= 0.0) continue;
        double y = log(sqrt(s_mla_ms_test[L])) / log(2.0);
        sx += L; sy += y; sxx += (double)L * L; sxy += (double)L * y; n++;
    }
    s_mla_fit.levels_used = (uint8_t)n;
    if (n < MLACC_FIT_MIN_LVL) { s_mla_fit.valid = false; return; }

    double den = (double)n * sxx - sx * sx;
    if (den <= 0.0) { s_mla_fit.valid = false; return; }
    double slope = ((double)n * sxy - sx * sy) / den;
    double inter = (sy - slope * sx) / (double)n;

    /* The clamp is physics, not taste. Below 0.5 would mean averaging removes
     * more than white noise allows; above 1.0 the spread is growing faster than
     * flat-in-ns, which is a phase RAMP rather than a noisier board — and
     * letting a ramp raise the threshold is the failure this file already
     * recorded once, where sigma climbed 165 -> 746 ns and the loop froze. */
    if (slope < MLACC_ALPHA_WHITE) slope = MLACC_ALPHA_WHITE;
    if (slope > MLACC_ALPHA_FLAT)  slope = MLACC_ALPHA_FLAT;

    double tgt = (g_mlacc_thr_tgt_s > 0u) ? (double)g_mlacc_thr_tgt_s
                                          : (double)MLACC_THR_TGT_DEFAULT;
    for (int L = 0; L < MLACC_LEVELS; L++) {
        double sd = pow(2.0, inter + slope * (double)L);
        double u  = mlacc_level_q(L, tgt) * sd;
        /* Same band MLP accepts, so a measured table and a hand-set one are
         * always the same kind of number and ML can print them side by side. */
        if (u < 1.0)      u = 1.0;
        if (u > 500000.0) u = 500000.0;
        g_mlacc_lim[L] = (int32_t)u;
    }
    s_mla_fit.exponent       = (float)slope;
    s_mla_fit.intercept_log2 = (float)inter;
    s_mla_fit.valid          = true;
}

uint16_t multi_level_accum(uint16_t pwm, uint32_t ppscount)
{
    static bool s_pol_warned = false;
    static double f_ema = 0.0;
    static uint32_t railed_cnt  = 0;
    static bool     start_arm_a12 = false;    /* (re)start: re-reference once */
    static uint32_t arm_holdoff = 0;
    static uint32_t fll_holdoff = 0;
    static int32_t prev_ph = 0;

    /* Fresh entry: see the note at s_algo_restart. The one that bites is
     * s_mla_returning — it suppresses the limit path AND the scheduled one, and
     * its 300-sample timeout only ticks while this algorithm is running, so a
     * switch away and back could freeze corrections for five minutes with
     * nothing in the telemetry to say why. */
    if (algo_take_restart()) {
        s_pol_warned = false;
        f_ema = 0.0;
        railed_cnt = 0; arm_holdoff = 0; fll_holdoff = 0; prev_ph = 0;
        s_mla_returning = false; s_mla_zc_armed = false;
        s_mla_wait = 0; s_mla_ph_sign = 0; s_mla_slew_lsb = 0;
        s_mla_quiet = 0; s_mla_post_arm = 0; s_mla_prev_valid = false;
        start_arm_a12 = true;
        s_mla_last_level = 0; s_mla_last_slope = 0; s_mla_last_phase = 0;
        s_mla_corrections = 0; s_mla_zc_hits = 0; s_mla_armed = 0;
        s_mla_dw_sum = 0.0; s_mla_dw_n = 0; s_mla_dw_a = 0.0;
        s_mla_dw_have_a = false; s_mla_dw_still = 0;
        mlacc_reset();
        /* What deliberately SURVIVES: everything this algorithm has MEASURED
         * about the hardware — s_mla_lsb_ns and lsb_per_ns_ready (LSB per ns),
         * s_mla_ms_phase / s_mla_ms_n (the detector noise floor) and
         * s_mla_ms_test / s_mla_test_n / s_mla_fit (the measured thresholds).
         * Those describe the board, not the previous run, and throwing them
         * away would cost minutes of running blind on every switch to rebuild
         * numbers that had not changed. The counters above are cleared for the
         * opposite reason: they are per-session evidence, and "zero corrections
         * since the switch" is only a readable fact if the count starts at
         * zero. */
    }

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10) { s_mla_quiet = 0; s_mla_locked = false; set_trend("WAIT"); return pwm; }

    /* Same refusal as algo 10/11: never steer on a guessed EFC polarity. */
    if (g_ltic.polarity == 0) {
        /* s_pol_warned: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */
        if (!s_pol_warned) {
            OUT_SERIAL.println("LTIC: polarity unset - run 'LPOL -1' (or +1) then 'ES'. Holding.");
            s_pol_warned = true;
        }
        s_mla_quiet = 0; s_mla_locked = false; set_trend("NoPL"); return pwm;
    }

    /* INPUT: phase in nanoseconds, from the LTIC detector.
     *
     * The first port fed instant_offset — the TIM2 count error in whole hertz —
     * and it was blind, for the reason above. Alan accumulates PHASE, and that is
     * the difference: phase integrates, so an error of 1e-11 that is invisible in
     * a one-second frequency count becomes 25 ns of phase in 2500 seconds. He
     * asked why I had said 100 ns when a TIC resolves 1 ns; he was right, I had
     * quoted the counter's resolution rather than the detector's. The detector is
     * 25x finer than the design this comes from. */
    bool    ph_valid = false;
    double  ph       = ltic_phase_error_ns(&ph_valid, ppscount);
    bool    have_phase = (ph_valid && g_ltic.ns_per_volt > 1.0f);
    int32_t phase_ns   = have_phase
                       ? (int32_t)(ph + (ph < 0 ? -0.5 : 0.5))
                       : 0;

    /* TIM2 frequency estimate [Hz]. The 100 s average when it exists (0.01 Hz
     * resolution), otherwise a ~50 s EMA of the 1-second count error. This is
     * the one measurement Alan never had — his PIC had no counter gated by
     * PPS — and it is what breaks the deadlock his design could only escape
     * by restarting: while the phase detector is blind, the frequency still
     * reads true. */
    double  f_meas;
    {
        /* f_ema: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */
        f_ema += ((double)s.instant_offset - f_ema) * 0.02;
        f_meas = s.have100 ? (s.avg100 - 10000000.0) : f_ema;
    }

    /* The picDIV has to be armed, or the detector never gives a valid reading.
     *
     * Algorithms 10 and 11 both arm it: algo 10 on entering ACQ, algo 11 through a
     * hold-off bridge once the frequency has settled but the phase is still
     * railed. Algorithm 12 did neither, and the consequence was quiet rather than
     * loud — with the ramp parked against a rail nothing works and nothing says so.
     *
     * The gate is |f| < 0.5 Hz, read from the TIM2 estimate above. The earlier
     * +/-10 Hz gate re-armed 87 times in one hour on the 14.08 log: each arm
     * lands the phase at a divider-quantised offset (~±3 µs, mostly outside
     * the ±1 µs band) while the uncorrected frequency keeps the phase ramping,
     * so the detector rails again within seconds and the next arm rolls the
     * same dice. Arming is only worth disturbing the divider once the
     * frequency is close enough for the phase to STAY wherever it lands; until
     * then the FLL below brings it there. */
    {
        /* railed_cnt: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */
        /* arm_holdoff: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */
        bool freq_close = (f_meas > -0.5 && f_meas < 0.5);

        /* START RE-REFERENCING, as algorithm 11 does: at (re)start a valid
         * but far phase is an inherited reference, not a hierarchy case —
         * correcting it the long way spends minutes of clamped corrections
         * on an offset one divider re-sync removes. One shot per (re)start,
         * same frequency gate and same debounce as the railed path below. */
        if (start_arm_a12 && have_phase) {
            double a0 = (phase_ns < 0) ? -(double)phase_ns : (double)phase_ns;
            if (a0 <= (double)g_ltic.acq_threshold_ns) start_arm_a12 = false;
        }

        if (arm_holdoff > 0) {
            arm_holdoff--;
            railed_cnt = 0;
        } else if (freq_close && (!have_phase || start_arm_a12)) {
            if (railed_cnt < 0xFFFFFFFFu) railed_cnt++;
            if (railed_cnt >= (!have_phase ? 5u : 2u)) {
                ltic_arm_picdiv();
                railed_cnt     = 0;
                start_arm_a12  = false;
                arm_holdoff    = 60u;   /* was 15: let the phase settle before another try */
                s_mla_post_arm = 5u;    /* keep the ±3 µs landing jump out of the accumulator */
                s_mla_armed++;
                /* AND THROW THE ACCUMULATOR AWAY. Arming re-syncs the divider to
                 * the 1PPS edge, so every phase already in the hierarchy was
                 * measured against an alignment that no longer exists. Keeping
                 * them mixes two different zeros: simulated, the first
                 * correction after an arm came out at -436 LSB from a level-3
                 * test that was half pre-jump and half post-jump, and the loop
                 * went on to walk the PWM 1800 counts and never came back.
                 * post_arm alone does not cover this — it keeps the landing
                 * transient OUT of the accumulator, but the accumulator was
                 * already full. */
                mlacc_reset();
                s_mla_returning = false;
                s_mla_zc_armed  = false;
            }
        } else {
            railed_cnt = 0;
        }
    }

    /* A FRESH-ENTRY ARM WAS TRIED HERE AND REJECTED, which is worth recording
     * because the case that motivated it is real. On 26.08 21:47 the board reset
     * straight into algorithm 12 with the picDIV unsynced: the ramp sat near its
     * top rail, Vphase flat at 3.116 V to +/-5 mV for the whole capture, and
     * with LRN 3000 the usable band is +/-1650 ns so that voltage is INSIDE it.
     * The detector reported a perfectly valid +1295 ns that never changed, the
     * loop never armed (arm=0 throughout), fired one level-0 correction that
     * saturated the +/-500 LSB clamp, and sat there. Algorithm 10 arms on
     * entering ACQ; algorithm 12 boots straight in and had nothing equivalent.
     *
     * The obvious repair — on a fresh entry, arm unless the phase is valid AND
     * inside the ACQ window, exactly algorithm 10's boot question — measures
     * badly, and for a reason that matters. It also fires when the detector is
     * working and the phase merely happens to be far out, and then the arm
     * THROWS AWAY a good measurement: the divider re-syncs to a quantised offset
     * of a few hundred ns, and algorithm 12 handles that step so poorly that the
     * loop walks the PWM off and never returns. Simulated from +1295 ns on the
     * oscillator replayed from that day's log: without the arm the loop pulls in
     * cleanly (19 zero-crossings, back inside +/-16 ns), with it the phase ends
     * at 4.3e6 ns. Arming is not a free action, and "the phase is far out" is
     * not evidence that the divider is lost.
     *
     * What IS evidence is below: far out AND not moving. That distinguishes the
     * two cases, and it is the one that ships. (The fragility of algorithm 12's
     * large-error path is a separate problem and is in the TODO.) */

    /* Just re-armed: the divider output has jumped to a quantised offset and
     * the first few readings are that jump, not the oscillator. Let it settle. */
    if (s_mla_post_arm > 0) {
        s_mla_post_arm--;
        s_mla_quiet = 0;
        set_trend("SYNC");
        s_mla_count++;
        s_mla_prev_valid = false;   /* the next reading is not contiguous */
        return pwm;
    }

    if (!have_phase) {
        /* Detector fitted but not reading — railed, saturated, or not yet armed.
         *
         * Alan's answer to this state was a full restart (RTquickAct: control
         * voltage to zero, re-anchor, begin again). We can do better, because
         * TIM2 measures the frequency directly: a gentle FLL step walks f
         * toward zero, the phase ramp that was carrying it out of the band
         * stops, and the detector recovers on its own — no dice roll needed.
         *
         * This is what the last 19 minutes of the 14.08 log were missing:
         * f = +0.26 Hz (measured, printed every second as f100), the phase
         * sweeping the band every ~20 s, PWM frozen at 41486, trend cycling
         * NOPH/false-LOCK. Gate at 0.3 Hz so the fine loop owns anything
         * smaller; step damped 0.1 and clamped so a mis-scaled CT constant
         * cannot run away — the correction is proportional to the same
         * lsb_per_hz it uses, so it converges at any scale, just slower. */
        /* ONE COMPUTED STEP, THEN WAIT FOR THE AVERAGE TO REFRESH.
         *
         * This was a bang-bang loop and could not have been anything else: the
         * step was -f*lsb_per_hz*0.10 clamped to +/-64, which saturates at
         * |f| = 64/(2500*0.1) = 0.256 Hz, while the gate below it only opened
         * at 0.3 Hz. The proportional part could never act at all. Driven once
         * a second from a 100 s average — about 50 s of lag — that gives
         * 64 LSB/s * 50 s = 3200 LSB of travel before the measurement even
         * begins to respond, which at the measured 319.5 uHz/LSB is 1.0 Hz of
         * overshoot.
         *
         * Both numbers are in the logs. 14.08 21:10: 462 of 655 consecutive
         * steps exactly +/-64, PWM sweeping 12845 LSB, f100 swinging -1.29 to
         * +1.55 Hz. 15.08 00:14, t=108..300: PWM 41289 -> 45897 -> 39241 in one
         * cycle, f100 -5.79 -> -0.59 -> +1.26.
         *
         * So: apply the whole computed correction ONCE, then hold off for as
         * long as the average it was computed from needs to refresh. Damped 0.5
         * against a gain that CT knows only to about 25% (it derives 2500 LSB/Hz
         * where the log measures 3130), which leaves the per-step loop gain near
         * 0.4 — convergent for any gain error within a factor of two.
         *
         * TWO SPEEDS, because one window cannot do both jobs. While the error is
         * large the 10 s average is used: ~5 s of lag, so a step every 10 s, and
         * a cold start walks in briskly. Once it is small the 100 s average
         * takes over, because it is the only one that resolves 0.01 Hz. The
         * hold-off always matches the window in use, so no step is ever applied
         * twice on the strength of a single measurement.
         *
         * GATE at 0.05 Hz, not 0.3. Above 0.147 Hz the phase crosses the whole
         * +/-940 ns detector band inside one 64 s horizon, so the old gate left
         * a dead zone from 0.147 to 0.3 Hz in which the phase loop could not get
         * a long enough look and the FLL considered its work done. The 15.08
         * log ended parked in it, at f = +0.27 Hz.
         *
         * Simulated on a model calibrated against that log, four seeds: over two
         * hours this holds |f| at 0.001 Hz with 83% of seconds giving a usable
         * phase reading and the hierarchy averaging level 3.1, against 0.200 Hz,
         * 36% and level 0.3 for the loop as it stands. */
        /* fll_holdoff: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */
        double lsb_per_hz = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40) : 0.0;
        if (fll_holdoff > 0u) {
            fll_holdoff--;
        } else if (lsb_per_hz > 0.0 && ppscount > 60u &&
                   (f_meas > 0.05 || f_meas < -0.05)) {
            double f_use = f_meas;
            /* Far out: steer from the short window and step again sooner. */
            if (s.have10) {
                double f_fast = s.avg10 - 10000000.0;
                if (f_fast > 1.0 || f_fast < -1.0) {
                    f_use = f_fast;
                    fll_holdoff = 10u;
                } else {
                    fll_holdoff = 100u;
                }
            } else {
                fll_holdoff = 100u;
            }
            /* SIGN FROM THE BOARD, NOT FROM THIS LINE.
             *
             * This read -f_use, which is right only because LPOL is -1 here.
             * The TIM2 frequency branch takes +polarity, the convention algo 11
             * established from a cold-start runaway on this wiring (see the
             * comment at its freq_lsb): TIM2 and the LTIC detector have
             * opposite orientation, so the frequency branch and the phase
             * branch carry opposite signs. With polarity == -1 this evaluates
             * to exactly what it did before, so nothing changes on this board;
             * on an LPOL +1 board the old line was positive feedback. */
            double pol_f = (g_ltic.polarity == -1) ? -1.0 : 1.0;
            int32_t d = (int32_t)(pol_f * f_use * lsb_per_hz * 0.5);
            s_mla_quiet = 0;
            set_trend("FLL ");
            return clamp_pwm((int32_t)pwm + d);
        }
        s_mla_quiet = 0;
        set_trend("NOPH");
        s_mla_count++;
        s_mla_prev_valid = false;   /* the next reading is not contiguous */
        return pwm;
    }
    g_last_offset = (int16_t)phase_ns;

    /* IS THE DETECTOR STILL FOLLOWING THE OSCILLATOR?
     *
     * Twice on 26.08 the board reset straight into algorithm 12 with the picDIV
     * unsynced. The ramp sat near its top rail — Vphase flat at 3.13 V, +/-5 mV
     * — and with LRN 3000 the usable band is +/-1650 ns, so that voltage is
     * INSIDE it. The detector reported a perfectly valid +1320 ns that never
     * changed, for five minutes, through a correction that moved the PWM five
     * hundred counts. At this board's scale that correction alone should have
     * walked the phase 16 ns every second. It did not move at all. That is not a
     * hard control problem, it is a dead measurement, and the loop could not tell
     * because "valid" had only ever meant "inside the band".
     *
     * THE TEST IS A PREDICTION, NOT A THRESHOLD. The loop knows the slew it
     * commanded, so it knows how far the phase should travel in a window:
     * |slew_lsb| / lsb_per_ns nanoseconds per second. Compare that with what the
     * phase actually did — the difference of two half-window means, whose noise
     * is sigma*sqrt(2/W) rather than sigma, so 1.8 ns here instead of 7. If the
     * prediction is large enough to be measurable and the phase delivers less
     * than a quarter of it, three windows running, the reading is not connected
     * to the oscillator any more. Then arm the picDIV, which is the only repair
     * there is, and say so.
     *
     * TWO EARLIER VERSIONS OF THIS ARE WORTH REMEMBERING. The first compared
     * single samples against a reference and reset on any 4-sigma excursion —
     * which the per-second noise does almost every second, so it never counted
     * past one and never fired on the board it was written for. The second added
     * a gate that refused to act on a phase not yet shown to be moving, and
     * deadlocked: the gate blocked the correction whose job was to move it.
     * Diagnose, do not restrain. */
    {
        s_mla_dw_sum += (double)phase_ns;
        if (++s_mla_dw_n >= (uint16_t)MLACC_STALL_W) {
            double mean = s_mla_dw_sum / (double)s_mla_dw_n;
            if (s_mla_dw_have_a) {
                double sg = sqrt(s_mla_ms_phase);
                if (sg < 1.0) sg = 1.0;
                double noise = 4.0 * sg * sqrt(2.0 / (double)MLACC_STALL_W);
                double slew  = (double)((s_mla_slew_lsb < 0) ? -s_mla_slew_lsb
                                                             : s_mla_slew_lsb);
                double expect = (s_mla_lsb_ns > 0.0)
                              ? (slew / s_mla_lsb_ns * (double)MLACC_STALL_W) : 0.0;
                double drift = mean - s_mla_dw_a;
                if (drift < 0.0) drift = -drift;
                int32_t ax = (phase_ns < 0) ? -phase_ns : phase_ns;
                bool testable = (expect > 4.0 * noise) &&
                                ((double)ax > (double)g_ltic.acq_threshold_ns);
                if (testable && drift < 0.25 * expect) {
                    if (s_mla_dw_still < 255u) s_mla_dw_still++;
                } else {
                    s_mla_dw_still = 0;
                }
            }
            s_mla_dw_a      = mean;
            s_mla_dw_have_a = true;
            s_mla_dw_sum    = 0.0;
            s_mla_dw_n      = 0;

            if (s_mla_dw_still >= 3u && arm_holdoff == 0u) {
                OUT_SERIAL.println("MLA: phase not answering corrections - re-arming picDIV");
                ltic_arm_picdiv();
                arm_holdoff     = 60u;
                railed_cnt      = 0;
                s_mla_post_arm  = 5u;
                s_mla_armed++;
                s_mla_dw_still  = 0;
                s_mla_dw_have_a = false;
                s_mla_returning = false;
                s_mla_zc_armed  = false;
                s_mla_slew_lsb  = 0;
                mlacc_reset();
                s_mla_quiet = 0;
                s_mla_locked = false;   /* the loop just said it is stalled */
                set_trend("STAL");
                s_mla_prev_valid = false;
                return pwm;
            }
        }
    }


    /* ZERO-CROSSING TEST.
     *
     * Alan calls this essential and the reason is worth stating: after a limit
     * correction changes the frequency, the phase keeps moving in the direction
     * it was already going. It sweeps through zero and out the other side, and
     * usually fails the limit again — so the loop corrects, overshoots, corrects
     * back, and settles slowly if at all.
     *
     * The moment the phase crosses zero is special. At that instant the phase
     * error is nil, but the frequency error that carried it there is still
     * present. Cancel the frequency error exactly then and the oscillator is left
     * with both the right frequency AND no phase error — a clean state, rather
     * than one the loop has to iterate towards.
     *
     * Measured against Alan's own logs: his loop corrects every 506 seconds where
     * mine corrects every 130. Most of that difference is this test.
     *
     * The flag is set when a limit correction fires, and cleared here on the first
     * sample whose phase has taken the sign the correction was pushing towards —
     * which is the crossing. */
    /* Give up waiting after a while. If the phase does not cross within a few
     * minutes, the correction did not overshoot and there is nothing to cancel —
     * holding the flag open would let a much later, unrelated crossing trigger a
     * correction based on a stale slope. 300 s: a slew at the 64 s minimum
     * horizon nulls its phase in ~64 s, so 5 horizons is generous.
     *
     * THIS CONSTANT SURVIVED A SERIOUS ATTEMPT TO REPLACE IT, and the attempt is
     * worth recording because the evidence against it looked overwhelming. On
     * the 26.08 run the zero-crossing test fired ZERO times in 2.25 hours, and
     * the reason was arithmetic: measured from each correction in that log, the
     * phase took 810, 1107, 1194, 1578, 1619, 1962, 2131, 2515, 2899 and 3283
     * seconds to change sign. Every one outlived this window. The obvious repair
     * — hold the arm while the phase is still getting closer to zero, rather
     * than while a clock runs — was written, and then measured against the
     * oscillator replayed from that same log (tools/loopsim):
     *
     *     phase sd [ns], correct gain      this window   "while improving"
     *       26.08 algo-12 window, 2.25 h       2.95            2.95
     *       26.08 algo-11 window, 6.10 h       4.01            6.76
     *
     * It is WORSE on the longer window, and the mechanism is the one written
     * above: on a board with real drift, a phase that keeps creeping toward zero
     * is often the oscillator doing it, not this correction's slew, and a
     * patient arm dutifully cancels a slew for a crossing it did not cause.
     *
     * The 810..3283 s returns were never a fault in this line. They were the
     * SYMPTOM of corrections 15x too small — MG had been set to algorithm 11's
     * gain, 2.130, against the 31.3 LSB/ns CT measured — and the cure for that
     * belongs where the gain is read, not here. Left as it was, deliberately. */
    if (s_mla_returning && ++s_mla_wait > 300u) {
        s_mla_returning = false;
        s_mla_zc_armed  = false;
    }
    if (s_mla_zc_armed && phase_ns != 0) {
        /* The phase has arrived: it has taken the opposite sign to the error that
         * triggered the correction. Remove the deliberate slew and the oscillator
         * is left at the right frequency AND no phase error — which is the point,
         * and why Alan calls this essential rather than an optimisation. */
        if ((phase_ns > 0) != (s_mla_ph_sign > 0)) {
            s_mla_returning = false;
            s_mla_zc_armed  = false;
            if (s_mla_slew_lsb != 0) {
                int32_t dz = -s_mla_slew_lsb;
                s_mla_slew_lsb = 0;
                s_mla_zc_hits++;
                mlacc_reset();
                /* The jump itself is a COMMAND, not a correction: it cancels a
                 * slew this algorithm applied deliberately. It must not enter
                 * the correction statistics, so the verdict drops for exactly
                 * the one second it takes. (The display keeps counting ZC as
                 * locked — a character blinking white once per crossing is a
                 * different trade from a number being wrong.) */
                s_mla_locked = false;
                set_trend("ZC  ");
                return clamp_pwm((int32_t)pwm + dz);
            }
        }
    }

    /* MEASURE THE PHASE NOISE, AND SET THE THRESHOLDS FROM IT.
     *
     * The limits were first taken from Alan's design and scaled by the ratio of
     * counter steps. That was the wrong quantity to scale by. What a threshold
     * has to clear is not the detector's resolution but the NOISE on the
     * measurement — detector, sawtooth and the GPS signal's own wander together
     * — and that differs between builds for reasons a step size does not capture.
     *
     * Measured here: mean phase -1 ns, standard deviation 462 ns. The oscillator
     * was correctly tuned; all of that spread was noise. The level-0 threshold
     * sat at 462 ns, so 41% of samples crossed it, each firing a correction of a
     * couple of hundred DAC counts. 620 corrections in 1685 seconds, the loop
     * resetting its hierarchy every 2.7 s and never reaching level 2 — the
     * arithmetic was right and the threshold was meaningless.
     *
     * So it is measured instead. An exponential estimate of the mean-square phase
     * gives sigma; averaging N samples divides it by sqrt(N); and a threshold of
     * four sigma on that mean fires on noise about once in 16000 tests, which at
     * level 0 is once in nine hours. Any real drift crosses it far sooner.
     *
     * MG > 0 keeps the stored table instead, for anyone who would rather set them
     * by hand. */
    /* The white-noise estimate runs whatever MG says. It used to live inside
     * `if (g_mlacc_gain <= 0.0f)` together with the table build, which had two
     * costs: MF could not offer the formula table with a hand-set gain, and
     * `sig=` in telemetry read a flat zero on every board running MG > 0 — not
     * because the board was quiet but because nothing was measuring. */
    {
        /* Noise from FIRST DIFFERENCES, not deviations about a tracked mean.
         *
         * Two estimators failed here before. Mean-square-of-phase counted a
         * standing offset as noise (phase parked at -985 ns, 45 ns of real
         * noise, "sigma" read 986). Deviations about a slow tracked mean fixed
         * that but not the worse case: the phase RAMP that an uncorrected
         * frequency error produces swept the estimate upward with it — over
         * the 14.08 log sigma climbed 165 -> 746 ns while every threshold
         * scaled with it, until no threshold was reachable inside the ±1 µs
         * detector band and the loop froze outright. A threshold that grows
         * with the error it is meant to catch can never catch it.
         *
         * Consecutive differences kill a linear ramp exactly (dp of a ramp is
         * constant, and its contribution is (drift*dt)² which at GPSDO rates
         * is nano-noise: 50 ns/s of drift gives dp = 50, vs sigma 165). They
         * halve into the per-sample variance (E[dp²] = 2σ²), and do not care
         * where the mean sits. A re-arm's ±3 µs landing is one huge
         * difference — rejected by the outlier gate below and by the 5 s
         * post-arm skip anyway. */
        /* THE OUTLIER GATE MUST NOT BE ABLE TO SUPPRESS ITS OWN INPUT.
         *
         * The previous gate was dp_lim = 5*sigma, read from the estimate it was
         * feeding. That is a one-way ratchet: once sigma is small, every
         * difference big enough to raise it is rejected as an outlier, so it can
         * only ever fall further. Measured on 14.08 21:10 — sig read exactly
         * 2 ns for all 1020 samples of the run, and with the limits derived from
         * it the whole hierarchy pinned at the 100-unit floor: 79 of 80
         * corrections fired at level 0, one at level 1. A multi-level
         * accumulator that never leaves level 0 is not one.
         *
         * Two changes. The gate is now ABSOLUTE (300 ns — well above any real
         * per-second phase step, well below a re-arm's ±3 µs landing), so it
         * cannot move with the estimate. And the genuine outliers it was really
         * there to catch — differences taken across a NOPH/SYNC/re-arm gap — are
         * excluded structurally by s_mla_prev_valid instead of statistically. */
        /* prev_ph: hoisted to the top of this function so the restart block can
         * clear it; the storage and the initialiser are unchanged. */
        if (s_mla_prev_valid) {
            double dp = (double)(phase_ns - prev_ph);
            if (dp < 0.0) dp = -dp;
            if (dp < 300.0) {
                s_mla_ms_phase += 0.002 * (0.5 * dp * dp - s_mla_ms_phase);
            }
        }
        prev_ph = phase_ns;
        s_mla_prev_valid = true;
        if (++s_mla_ms_n >= 300u) {          /* enough samples to mean something */
            s_mla_ms_n = 0;                  /* re-measure every 300 s */
            if (mlacc_thr_source() == MLACC_THR_MEASURED) mlacc_build_measured();
            else if (mlacc_thr_source() == MLACC_THR_SIGMA) mlacc_build_sigma();
        }
    }

    int32_t carry = 2 * phase_ns + 1;

    uint32_t work = s_mla_count;
    int level = 0;
    int32_t applied = 0;
    bool    from_limit = false;   /* which path fired: limit, or the MR timer */

    for (; level < MLACC_LEVELS; level++) {
        int32_t a = s_mla_val[level];
        s_mla_val[level] = carry;          /* the new value becomes the stored one */

        if ((work & 1u) == 0u) {
            /* First value at this level — nothing to compare against yet. */
            if (work == 0u) break;         /* and nothing above holds anything */
            break;
        }

        /* Two values at this level: A is the one just displaced, B the new one. */
        int32_t b     = carry;
        int32_t slope = b - a;
        int32_t phase = (a + b) + 2 * slope;

        int32_t pabs = phase < 0 ? -phase : phase;
        int32_t sabs = slope < 0 ? -slope : slope;

        /* MEASURE THIS LEVEL'S OWN SPREAD, before deciding anything with it.
         *
         * Updated on every evaluation, fired or not: an estimate that only saw
         * the tests it let through would be censored by its own threshold,
         * which is the shape of the dp_lim ratchet recorded above. Skipped
         * while a slew is in flight, because during those seconds the test is
         * measuring the loop's own deliberate action and not the board.
         *
         * The rate is the same wall-clock time constant at every level rather
         * than the same number of samples — level 0 is evaluated 1024x more
         * often than level 10, and an EMA in samples would give them adaptation
         * spans four hours apart. The span is the depth of the hierarchy
         * itself, so it is not a number anyone had to pick. */
        /* Not while acquiring either. Same reason as s_mla_returning: during
         * pull-in the phase is the loop travelling, not the board's noise. The
         * 20.08 15:08 run measured what it costs — 102 ns rms over the first
         * 300 s against 5.7 ns once settled, which is 321x in the mean SQUARE
         * the estimator accumulates, and with a 4096 s span the pull-in would
         * still be setting the thresholds an hour after boot.
         *
         * s_mla_quiet is the right gate rather than a timer: it counts seconds
         * since control was genuinely absent (WAIT/SYNC/FLL/NOPH) and is NOT
         * reset by corrections, so it says "the loop has the phase" and not
         * "nothing has happened lately". It is the same test the LOCK lamp
         * uses, so the estimator measures exactly the regime the operator is
         * being shown. A board that never settles never fills the estimate,
         * the table stays at whatever is stored, and ML says the fit is not
         * ready — which is the honest outcome, not a silent bad table. */
        if (!s_mla_returning && s_mla_quiet > 16u) {
            double a_ema = (double)(1u << (level + 1)) / MLACC_FIT_TAU_S;
            if (a_ema > 0.25) a_ema = 0.25;
            if (s_mla_test_n[level] < 0xFFFFu) s_mla_test_n[level]++;
            /* WARM-UP. The weight is the larger of the EMA rate and 1/n, so the
             * first samples form a plain running mean and the EMA only takes
             * over once it would average over more history than exists.
             *
             * Seeding the EMA from a single squared test instead — which is
             * what the first version did — costs 1/a_ema samples to forget, and
             * at level 0 that is 2048 evaluations. Caught on the 20.08 home
             * record: eighteen minutes in, every level was still carrying its
             * own first sample, the fit read 0.46 where the same data measured
             * offline gives 0.92, and the clamp turned that into the white-noise
             * exponent — the estimator would have reported exactly the
             * assumption it exists to replace, and looked healthy doing it. */
            double w = 1.0 / (double)s_mla_test_n[level];
            if (w < a_ema) w = a_ema;
            double t2 = (double)phase * (double)phase;
            s_mla_ms_test[level] += w * (t2 - s_mla_ms_test[level]);
        }

        /* MR: force a correction once this level is reached, whatever the limits
         * say. Without it a drift slow enough to stay under every limit would be
         * accumulated forever and never acted on — Alan's SUrunLev serves the
         * same purpose in the original. Suppressed while a slew is in flight,
         * like every limit correction (see the act gate below). */
        if (!s_mla_returning && level >= (int)g_mlacc_run_level) {
            s_mla_last_level = (uint8_t)level;
            s_mla_last_slope = slope;
            s_mla_last_phase = phase;
            applied = phase;
            break;
        }

        /* Phase test only. Alan dropped the frequency test after using it for a
         * while: "It was an experiment. The phase test can be slower to see a
         * deviation, but what we want is a stable system where the tests always
         * pass. So the frequency test is unnecessary." Keeping it would add a
         * second path to a correction that the phase test reaches anyway.
         *
         * Suppressed while a correction's deliberate slew is still walking the
         * phase home (s_mla_returning): stacking a second slew on top of the
         * first is what turned the 14.08 pull-in into a +3800 LSB overshoot
         * and the following minutes into a 4000-LSB limit cycle at one
         * correction every two seconds. The zero-crossing test clears the
         * flag when the phase arrives; the timeout abandons it if it never
         * does. Accumulation continues — the state after the crossing is
         * fresh, not stale. */
        (void)sabs;
        bool act = (!s_mla_returning) && (pabs >= g_mlacc_lim[level]);

        if (act) {
            from_limit = true;      /* the only path that arms the zero-cross */
            s_mla_last_level = (uint8_t)level;
            s_mla_last_slope = slope;
            s_mla_last_phase = phase;
            applied = phase;
            break;
        }

        /* Passed both: promote the sum and test again over twice the span. */
        carry = a + b;
        work >>= 1;
    }

    s_mla_count++;

    if (applied == 0) {
        /* LOCK means: the phase has been readable since the last real
         * anomaly (arm, railed detector, no data) AND the TIM2 frequency is
         * home. Corrections and zero-crossings do NOT reset s_mla_quiet —
         * this algorithm corrects on a schedule as a form of self-test, so
         * a correction is evidence of health, not of acquisition. The old
         * test (s_mla_count > 64) read the accumulator's bookkeeping
         * instead of the loop's state of control and lit ACQ for 40% of a
         * settled run. The frequency gate is two-tier: the 100 s average
         * resolves 0.01 Hz, so when it exists 0.05 Hz is a real bound — the
         * 16.08 band-edge cycle sat at 0.12 Hz and would have shown ACQ
         * under it, as it should. Before have100 exists the EMA only
         * resolves whole hertz; keep the loose 0.5 Hz bound there. */
        bool freq_good = s.have100 ? (f_meas < 0.05 && f_meas > -0.05)
                                   : (f_meas < 0.5  && f_meas > -0.5);
        s_mla_quiet++;
        /* THE one place algorithm 12 decides. Everything else either inherits
         * this (a CORR second is a correction made BY a settled loop and is
         * exactly what the statistics measure) or clears it explicitly. */
        s_mla_locked = (s_mla_quiet > 16u && freq_good);
        set_trend((s_mla_quiet > 16u && freq_good) ? "LOCK" : "ACQ ");
        return pwm;
    }

    /* Convert the accumulated phase into DAC counts.
     *
     * Two things were wrong here and both showed up in the same log.
     *
     * UNITS. `applied` is the sum of (2*phase_ns + 1) over `span` samples, so
     * dividing by two and by the count gives the average phase error in
     * NANOSECONDS. The first version then multiplied that by LSB-per-HERTZ, as
     * though nanoseconds and hertz were the same quantity. Nulling P ns over T
     * seconds needs a fractional offset of P/(T*1e9), which at 10 MHz is
     * P/(100*T) Hz — so the correction was 100*T times too large, from 200x at
     * level 0 to 102400x at level 9. Every correction slammed into the +/-2000
     * clamp, which is exactly what the log showed.
     *
     * POLARITY. Algorithm 11 applies -polarity to its phase term; this did not
     * apply it at all. On a board with LPOL -1 the correction therefore went the
     * wrong way — positive feedback into a loop that was already over-correcting
     * by four orders of magnitude. */
    /* MG: a hand-set gain overrides the CT-derived one, in LSB per ns directly
     * rather than per hertz — the units a user tuning against a scope actually
     * has. Zero means derive it, which is the default and what most want. */
    double lsb_per_ns = s_mla_lsb_ns;
    if (g_mlacc_gain > 0.0f) {
        lsb_per_ns = (double)g_mlacc_gain;
        /* SAY SO WHEN THE HAND-SET GAIN CANNOT BE A TUNING CHOICE.
         *
         * MG and algorithm 11's LG are both printed as "LSB per ns" and are not
         * the same quantity: LG is that loop's own VCO gain, MG is the counts
         * needed to null one nanosecond of phase in one second. On the board
         * this was written for they are 2.13 and 31.3 — a factor of fifteen —
         * and on 26.08 the first number was typed into the second field. The
         * result was a 2.3 hour limit cycle at +/-70 ns that looked like a loop
         * fault and was an entry error, and nothing on the board said a word.
         *
         * CT has measured the real figure, so the board can check the claim.
         * Beyond a factor of four the difference is not tuning — nobody tunes a
         * gain four times away from the measured one — so print it once, and
         * again if the value changes. The value is still used: refusing it
         * would be worse, because a deliberate experiment has to remain
         * possible. This only makes sure the operator hears about it. */
        {
            static float s_warned_mg = -1.0f;
            double der = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40 / 100.0) : 0.0;
            if (der > 0.0 && g_mlacc_gain != s_warned_mg &&
                (lsb_per_ns > 4.0 * der || lsb_per_ns * 4.0 < der)) {
                s_warned_mg = g_mlacc_gain;
                OUT_SERIAL.print("MLA: MG ");
                OUT_SERIAL.print((double)g_mlacc_gain, 3);
                OUT_SERIAL.print(" LSB/ns disagrees with CT (");
                OUT_SERIAL.print(der, 2);
                OUT_SERIAL.println(") - corrections scale with it. 'MG 0' derives it.");
                OUT_SERIAL.println("     (algo 11's LG is a different quantity - do not copy it here)");
            }
        }
    } else {
        double lsb_per_hz = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40) : 0.0;
        if (lsb_per_hz <= 0.0) {
            s_mla_locked = false;
            set_trend("NoCT");
            return pwm;
        }
        /* Hz per ns of phase corrected over one second is 1/100 at 10 MHz, so the
         * per-second gain in LSB per ns is lsb_per_hz/100. The span divides it
         * again below, where the correction horizon is applied. */
        lsb_per_ns = lsb_per_hz / 100.0;
    }
    /* Publish for the zero-crossing test, which runs earlier in the next pass and
     * has no other way to know the scale. */
    s_mla_lsb_ns     = lsb_per_ns;
    lsb_per_ns_ready = (lsb_per_ns > 0.0);

    /* TWO terms, applied together. This is what was missing, and the reason both
     * zero-crossing attempts made things worse instead of better.
     *
     * State is phase p and frequency error f, with dp/dt = f. A limit correction
     * has two separate jobs:
     *
     *     cancel the measured frequency error       df = -f
     *     impose a deliberate slew to bring p back  df = -p/T
     *
     * which together leave f = -p/T exactly. Apply only the second — all this did
     * — and the oscillator runs at f_old - p/T instead, so the phase neither
     * returns cleanly nor stays put when it arrives. The loop hunts, which is what
     * every log showed.
     *
     * With both terms the phase walks to zero along a KNOWN slope. That slope is
     * deliberate, so it has to be removed when the phase arrives — which is what
     * the zero-crossing test does. The three pieces are one mechanism; any one of
     * them alone is worse than none, which is exactly what the measurements said.
     *
     * Simulated over 40000 s, 15 ns phase noise, drifting oscillator, four seeds:
     *     phase only          33.6 ns RMS
     *     phase + frequency   33.0 ns
     *     all three           22.4 ns   */
    double polarity = (g_ltic.polarity == -1) ? -1.0 : 1.0;
    double span     = (double)(1u << (s_mla_last_level + 1));
    if (span < (double)MLACC_MIN_HORIZON) span = (double)MLACC_MIN_HORIZON;

    double p_ns  = (double)applied / 2.0 / (double)(1u << (s_mla_last_level + 1));
    double f_nss = (double)s_mla_last_slope
                 / (double)(1u << (2u * s_mla_last_level + 1u));

    double slew_lsb = -polarity * (p_ns / span) * lsb_per_ns;

    /* The frequency term, restored. Alan's cvPWM applies BOTH corrections
     * (250321-O.asm 1293-1297: "add in the frequency correction"; total =
     * phase_corr + freq_corr), and f_nss below is exactly his slope estimate
     * — the pair's phase drift in ns/s, already divided by his 2^(2L+1).
     *
     * It was removed here after noise measurements, but those were made with
     * a poisoned sigma: the estimator of the day counted an uncorrected
     * frequency error's phase ramp as noise (sigma read 300+ ns), so the term
     * amplified exactly the malfunction it was meant to cure. With the
     * first-difference estimator above, the slope of a 2^(L+1)-second pair is
     * a measurement again — a 50 ns/s drift stands out by construction, not
     * luck. And without this term no correction ever cancels the phase ramp:
     * the 14.08 log ended with f = +0.26 Hz, the phase sweeping the detector
     * band every ~20 s, and the loop frozen for the last 19 minutes.
     *
     * Damped 0.5 rather than full cancellation: one pair over 2^(L+1) s is a
     * single measurement, and half-strength converges in two corrections
     * where full strength stakes everything on one. This is Alan's own k, the
     * experimental divisor he mentions and never publishes. */
    double f_ns   = f_nss;
    if (f_ns >   5000.0) f_ns =   5000.0;   /* 0.5 µs/s is not a measurement, it is a fault */
    if (f_ns <  -5000.0) f_ns =  -5000.0;

    /* SIGN: -polarity, the same factor the phase term uses.
     *
     * It was +polarity, copied from algorithm 11's frequency branch. But algo
     * 11's frequency branch reads TIM2, and this firmware's own algo-11 comment
     * records the hardware finding that TIM2 and the LTIC detector have
     * OPPOSITE orientation on this wiring. f_nss is not a TIM2 reading: it is
     * the slope of the SAME accumulator values a and b that produce p_ns, from
     * the SAME detector. A quantity and its own time derivative, measured by one
     * sensor, cannot need opposite feedback signs. Alan's cvPWM agrees — it
     * pushes phase and slope through one conversion and ADDs them
     * (250321O.asm, correctit: "add in the frequency correction" -> CALL addm).
     *
     * With the plant now MEASURED rather than assumed — +319.5 uHz/LSB, from
     * regressing the 100 s mean of PWM against the printed 100 s frequency
     * average over the 14.08 21:10 log, correlation 0.999 at zero lag — the old
     * sign works out to d(phase_rate) = +0.4*f_ns. That is positive feedback.
     *
     * LEVEL GATE: the slope's own noise is sd(f_nss) = sigma * 2^((1-3L)/2), so
     * at level 0 it is 1.41*sigma of pure noise, scaled by 12.5 LSB per ns/s
     * against the phase term's 0.39 LSB per ns. That is a 32:1 noise-to-signal
     * advantage for the wrong quantity, and the log shows exactly what it buys:
     * 46% of corrections slammed into the ±470 clamp, one of them with the
     * phase reading 0 ns and the correction at full scale. By level 3 the same
     * estimate is averaged over 16 s pairs and is a measurement again.
     *
     * Simulated, 20000 s, four seeds, locked start: with the sign fixed and the
     * gate at L>=3 the loop holds 32 ns RMS with the hierarchy averaging level
     * 3.7 and 100% of samples inside the detector band; without the gate,
     * 4190 ns and level 2.1. */
    double freq_lsb = 0.0;
    /* TIM2 TRIM — the frequency term from the COUNTER, at any level.
     *
     * The 16.08 log is why this exists. A phase disturbance pushed the loop to
     * the detector edge at 14:30; every NOPH re-armed the divider ~1.8 us from
     * zero; the pair test fired immediately at level 0-1 (big phase, fresh
     * hierarchy — the corrections and the zero-crossings had reset it twice
     * over), where the slope term above is gated off. So every correction was
     * pure slew, the crossing dutifully removed it, and the PWM returned to a
     * baseline that TIM2 said was 0.12 Hz wrong — for hours, at "LOCK", while
     * the phase rode the band edge every 4:45 min. The three-part mechanism
     * (slew + frequency + cancel-at-crossing) only closes if the frequency
     * part actually fires; pinning the hierarchy at level 0-1 amputated it.
     *
     * The counter is the one measurement Alan never had, and here it sees the
     * error at twelve times its resolution. It REPLACES the slope term while
     * it is open: both estimate the same quantity, and a 100 s gated count
     * beats one pair of accumulator sums at any level the gate admits. The
     * sign carries no LTIC polarity — TIM2 and the detector sit with opposite
     * orientation on this wiring (the algo-11 finding), and this is the same
     * -f*lsb*0.5 the NOPH FLL step uses, whose convergence the 16.08 15:00
     * storm already demonstrated (-290/-224/-187 LSB steps walking f100 home).
     * Damped 0.5, so a mis-scaled CT cannot make it runaway. */
    bool tim2_trim = (s.have100 &&
                      (f_meas > MLACC_TIM2_TRIM_GATE || f_meas < -MLACC_TIM2_TRIM_GATE));
    if (tim2_trim) {
        /* 1 Hz at 10 MHz is 100 ns of phase per second, and lsb_per_ns*100 is
         * lsb_per_hz — stated that way so a hand-set MG gain divides through
         * the same as the CT-derived one. */
        /* +polarity, like every other TIM2-derived term — see the FLL branch
         * above for why the frequency and phase branches differ in sign. With
         * LPOL -1 this is identical to the -f_meas it replaces. */
        freq_lsb = polarity * f_meas * (100.0 * lsb_per_ns) * 0.5;
    } else if (s_mla_last_level >= MLACC_FREQ_MIN_LEVEL) {
        freq_lsb = -polarity * f_ns * lsb_per_ns * 0.5;
    }
    /* Kept as a double to the end. The measured corrections in normal operation
     * have a median size of 6 LSB, so truncating here discarded up to a sixth
     * of each one, always toward zero. */
    double dq = slew_lsb + freq_lsb;

    {   /* Clamp against the detector band: a correction is only useful while the
         * phase stays measurable. 1% of the band per second crosses it in 100 s,
         * comfortably slower than the 64 s minimum horizon. */
        double lim_lsb = 500.0;
        if (g_ltic.range_ns > 100.0f && lsb_per_ns > 0.0) {
            double l = (double)g_ltic.range_ns * 0.01 * lsb_per_ns;
            if (l > 10.0 && l < lim_lsb) lim_lsb = l;
        }
        if (dq >  lim_lsb) dq =  lim_lsb;
        if (dq < -lim_lsb) dq = -lim_lsb;
    }
    int32_t d = (int32_t)dq;

    /* Remember the DELIBERATE part only, AND ONLY THE PART THAT SURVIVED THE
     * CLAMP. The crossing removes exactly this and nothing else — no second
     * measurement, which is where the earlier attempts went wrong by cancelling
     * a freshly measured slope unrelated to the slew actually imposed.
     *
     * This recorded `slew_lsb`, the value computed BEFORE the clamp a few lines
     * above, and that is a different number whenever the clamp bites — which is
     * exactly when the phase is far out and the loop can least afford a mistake.
     * The 26.08 22:41 log is what it costs. After the stall watch re-armed the
     * divider the phase came back honestly, reached -50 ns, and the crossing
     * then removed a slew of 734 LSB when only 500 had ever reached the pin. The
     * extra 234 LSB is a frequency error injected in the opposite direction, so
     * the phase set off the other way, hit the clamp again at the far rail, and
     * the loop spent seventeen minutes crossing the detector band: PWM 40348 to
     * 41410, phase -1600 to +20, five re-arms, never inside +/-200 ns.
     *
     * dq is the clamped total; freq_lsb is not a deliberate slew and is not
     * cancelled, so what the crossing owes back is the remainder. (The integer
     * truncation below can differ by at most one count, which is nothing beside
     * the hundreds the clamp removes, and the fine path does not truncate at
     * all.) */
    {
        double applied_slew = dq - freq_lsb;
        s_mla_slew_lsb = (int32_t)(applied_slew +
                                   (applied_slew < 0.0 ? -0.5 : 0.5));
    }
    s_mla_returning = true;
    /* ARMED ON BOTH PATHS.
     *
     * This was `= from_limit`, on the argument that a scheduled correction
     * "fires on a timer with the phase wherever it happens to be, so there is
     * no known slew for the crossing to cancel". That argument describes Alan's
     * loop, not this one: a few lines above, BOTH paths compute the same
     * slew_lsb = -(p_ns/span)*lsb_per_ns and both store it here. A deliberate
     * slew was imposed either way, so a crossing is expected either way, and
     * leaving it uncancelled is the overshoot the test exists to prevent.
     *
     * The 26.08 log settles it from the other side too. Of the thirteen
     * corrections there, the only crossing that arrived quickly (83 s) followed
     * a level-9 SCHEDULED correction — the one case the old line refused to
     * arm. Between that and the 300 s window above, the set of corrections that
     * were both armed and crossed in time was empty.
     *
     * One guard was tried here and dropped. The scheduled path fires on a timer
     * with the phase wherever it happens to be, so the accumulator's
     * extrapolated p_ns can disagree in sign with the phase on the pin, and
     * arming on that would let the crossing test be true on the very next
     * sample — cancelling a slew that had not done anything yet. Requiring the
     * two signs to agree fixes that in theory and measures as buying nothing:
     * pull-in from 240 and 500 ns was identical to three decimal places, and
     * holding got WORSE (2.95 -> 3.43 ns on the 26.08 algo-12 window, 4.01 ->
     * 4.51 on the algo-11 window). The self-cancelling case is evidently rare
     * enough that the guard only costs the crossings it also blocks. */
    (void)from_limit;
    s_mla_zc_armed  = true;
    /* RESET THE WAIT COUNTER. Without this line s_mla_wait is free-running: it
     * appeared exactly twice in this file, at its declaration and at the ++ in
     * the give-up test, and never went back to zero. So once it passed 300 —
     * about five minutes into any run — the give-up test fired on the SAME
     * second the flag was raised, and from then on `returning` was dead. That
     * disables both things it gates: the zero-crossing correction, and the
     * suppression of new corrections while a slew is still walking the phase
     * home.
     *
     * Measured on 14.08 22:30, the run that found it: zc = 7 in 76 minutes (all
     * of them inside the first five minutes), and 1072 of 1174 corrections
     * exactly 2 seconds apart — which is the bare level-0 cadence with nothing
     * holding it back (count resets to 0, even second skips, odd second fires).
     * The hierarchy therefore never left level 0: 1133 corrections at level 0,
     * 30 at level 1, 12 at level 2, none above.
     *
     * This is also why the simulation mispredicted: it reset the counter,
     * because that is what the code was clearly meant to do. It modelled the
     * intent, not the source. */
    s_mla_wait      = 0;
    s_mla_ph_sign   = (p_ns > 0.0) ? +1 : -1;


    s_mla_corrections++;
    mlacc_reset();
    set_trend("CORR");
    {
        double target = fine_base(pwm) + dq;
        g_vctl_fine       = target;
        g_vctl_fine_valid = true;
    }
    return clamp_pwm((int32_t)pwm + d);
}

/* ======================================================================
 * ALGORITHM 13 — three-state Kalman filter (phase, frequency, aging)
 *
 * WHY THIS EXISTS, given 10, 11 and 12 all work.
 *
 * Every one of them has a bandwidth chosen once and then lived with. Algo 10
 * switches between three of them; algo 11 has a time constant; algo 12 picks a
 * level from a threshold table. All three are answering the same question —
 * "how much of this second's phase reading should I believe?" — with a number
 * decided in advance.
 *
 * A Kalman filter answers it from the variances instead, and re-answers it every
 * second. It knows how noisy the detector is because it measures it, and it
 * knows how fast this oscillator wanders because it measures that too, so the
 * weight it gives the measurement is whatever those two numbers currently say.
 * At short tau, where the detector is noisy and the OCXO is quiet, it leans on
 * the oscillator. At long tau, where the OCXO walks, it yields to GPS.
 *
 * WHAT IT COSTS. Three states and a SCALAR measurement, so the textbook matrix
 * inversion is a single division. About 140 multiply-adds and 36 bytes, once a
 * second — a microsecond of a 100 MHz M4F. The claim that Kalman needs a
 * Cortex-A or an FPGA is about twenty-state GNSS filters, not this.
 *
 * MEASURED ON THE BENCH, AND IT DOES NOT YET MATCH THE SIMULATOR.
 * The 27/28.08 overnight run, 11.8 h: phase sd 5.94 ns against algorithm 11's
 * 4.68 ns over a comparable 11.0 h night on the same board — 27% WORSE, with
 * the same detector floor (2.74 against 2.64 ns) and the same thermal swing
 * (2.4 against 2.7 C), so it is the loop and not the environment. ADEV at
 * 1024 s 1.0e-11 against 7.8e-12; identical at 1 and 16 s, which says the
 * short end is detector-limited for both and the difference is at mid tau.
 *
 * The simulator says the opposite, by a factor of three, and four separate
 * attempts to make it say otherwise all failed:
 *   - correlated detector noise (LOOPSIM_DNOISE): degrades this loop faster
 *     than algorithm 11, as predicted, but never enough to reverse the order
 *   - a realistic TIM2 (integer 1 s counts, true 100 s boxcar): no effect
 *     (it WAS a simulator bug, and is fixed, but it is not this)
 *   - the horizon KT from 50 to 800 s: no effect against that noise
 *   - feeding the aging state forward into the control: no effect
 * A whiteness test on the innovations was written to explain it and measured
 * worse; it is not in the tree and the reasoning is recorded at its site.
 *
 * ONE CLUE IS UNEXPLAINED and worth the next experiment: over the night this
 * loop moved the PWM 57 LSB where algorithm 11 moved 93 on a comparable night.
 * That is the signature of UNDER-correction, not of chasing noise. The two runs
 * were different nights, though, so the honest next step is A/B on one night —
 * a couple of hours of each, alternating — which removes the environment from
 * the comparison entirely. Until then the numbers below are simulation.
 *
 * UPDATE 29.08: the prime suspect is now fixed in code. R differences at
 * sixteen seconds instead of one, so the slow detector-zero wander that lag-1
 * differencing cancelled is part of the estimate — the replay above said that
 * component alone reverses the bench order. The next overnight run tests that
 * and nothing else.
 *
 * SIMULATED on the 26/27.08 night run, replayed through tools/loopsim against
 * the oscillator reconstructed from that log, five noise seeds:
 *
 *              phase sd        ADEV @ 1024 s
 *     algo 11  3.62 / 7.39     7.7e-12 / 1.5e-11
 *     algo 12  3.07 / 4.20     4.1e-12 / 7.6e-12
 *     algo 13  1.72 / 1.64     2.2e-12 / 2.6e-12
 *
 * (two plants: the 2.25 h algo-12 window and the 6.1 h algo-11 window). The
 * second column is the interesting one — the other loops lose ground on the
 * plant with more drift and this one does not, which is the adaptive bandwidth
 * doing its job rather than a better constant.
 *
 * HOLDOVER IS FREE. The state carries frequency and aging with their
 * covariances, so a lost phase is not a special case: stop updating, keep
 * predicting, keep steering. No separate model, no frozen EFC.
 *
 * TWO MEASUREMENTS, NOT ONE. The first version had only the phase, and that was
 * not an omission of a refinement — it was a loop that could not see. With the
 * picDIV unsynced there is no valid phase at all, so a phase-only filter has NO
 * measurement: it predicts from a state that is still zero, steers nothing, and
 * the oscillator walks. On the bench that showed as the frequency climbing
 * steadily with the loop reporting HOLD and doing nothing about it.
 *
 * TIM2 is the second measurement, of a state the filter already carries, so it
 * costs one more scalar update and no new concepts. It is also what makes the
 * detector checkable: phase and frequency are the same quantity differentiated,
 * so a detector that does not move when TIM2 says it must is not measuring
 * anything. That test replaces guessing, and it is what lets this loop tell a
 * dead picDIV from a quiet one.
 *
 * WHAT IS MEASURED RATHER THAN SET:
 *   R  the detector's noise, from its own first differences, exactly as algo 12
 *      estimates its sigma. 2.64 ns on this board.
 *   Q  the oscillator's frequency random walk, adapted from the innovation
 *      sequence: if the innovations are consistently larger than the filter's
 *      own prediction of their size, the process noise is too small. Measured
 *      independently from the night log at 2e-6 (ns/s)^2/s, consistent across
 *      600, 1800 and 3600 s windows, which is what random-walk FM looks like.
 *   KR and KQ override them for an experiment. Zero means measure.
 * ====================================================================== */
float    g_kf_r_ns      = 0.0f;
float    g_kf_q         = 0.0f;
uint16_t g_kf_horizon_s = 100u;
/* THE CONTROLLER'S HORIZON, WHICH IS NOT THE ESTIMATOR'S. 0 = derive it.
 *
 * One number used to do both jobs: the Q ceiling R/T^3 sets how fast the
 * ESTIMATOR may run, and x0/T sets how fast the CONTROLLER nulls a phase error
 * it already knows about. The note by the KT command used to say those are
 * different things and that separating them needed Sg measured against a
 * reference this board does not have. That was wrong, and the measurement is
 * in the changelog: separating them needs nothing but a second variable,
 * because x0 is an ESTIMATE, not a measurement. Its own error is sqrt(P00) ~
 * 1.05 ns against a 3.3 ns signal on this board, so nulling it quickly does
 * not amplify white noise - the filter already removed it. Only Q/R decides
 * how much of the detector's slow lie gets believed, and this variable does
 * not touch Q/R.
 *
 * Default KT/3 rather than a constant, so it scales with the horizon and is
 * not a number fitted to one board. The measured knee on this one is 20-30 s
 * with a detector wander of tau ~60 s; KT/3 = 33 s lands there. */
uint16_t g_kf_ctl_s = 0u;

/* KC EARNS ITS SPEED, IT IS NOT BORN WITH IT. The fast nulling is a
 * locked-state optimisation - acting quickly on a belief the filter has
 * already smoothed. During acquisition the belief is neither smooth nor
 * settled: an arm lands the phase half a band out, and x0/KC at that
 * distance commands 1278/33 = 39 ns/s of nulling against a limiter meant
 * for a walking pace. The 03.09 20:11 boot rode that limiter through six
 * rail-bounces (+1437/-2482 ns, 116 rejects) before locking. So KC waits
 * until the loop has actually settled once - the phase inside the
 * acquisition band on a reading the filter is using - and then keeps it
 * for good: later excursions are tens of nanoseconds, where the two
 * horizons differ by nothing the limiter cares about, and toggling a
 * controller time constant on every GPS episode would be its own fault.
 * The Q ceiling uses the same shape ("in force only while tracking") for
 * the same reason. The latch lives where `have` is computed, and kf_reset
 * drops it: a restart re-earns it. */
static bool s_kf_ctl_fast = false;

/* The controller horizon in force, floored like KT's. */
static double kf_ctl_horizon(void)
{
    uint16_t t;
    if (!s_kf_ctl_fast)        t = g_kf_horizon_s;       /* acquisition: the gentle one */
    else if (g_kf_ctl_s == 0u) t = (uint16_t)(g_kf_horizon_s / 3u);
    else                       t = g_kf_ctl_s;
    if (t < 10u) t = 10u;
    return (double)t;
}

static double   s_kf_x0, s_kf_x1, s_kf_x2;      /* phase, freq, aging        */
static double   s_kf_P[3][3];
static bool     s_kf_init;
/* Seeded with what the 26/27.08 night measured on this board, so the filter is
 * sane from its first second rather than after the estimators converge. Both
 * are then driven by the measurement and will move if the board is different. */
/* THE LAG IS IN THE NAME BECAUSE READING THE PAIR BACKWARDS IS EASY AND
 * EXPENSIVE. s_kf_ms_diff16 is half the mean square of the detector's
 * differences at KF_R_LAG seconds and is what R uses; s_kf_ms_diff1 is the
 * same at lag 1 and is the WHITE floor alone. The two together separate the
 * detector's white noise from its slow wander (see the Sf derivation), and a
 * written analysis of this filter got them the wrong way round in 09.26 and
 * spent a day chasing a 30% error in R that was the design working. */
static double   s_kf_ms_diff16;    /* lag KF_R_LAG: white + slow. R uses THIS */
static double   s_kf_ms_diff1;     /* lag 1: the white floor alone            */
static int32_t  s_kf_ph_prev1;     /* white noise from phase random walk; see Sf     */
static bool     s_kf_ph_prev1_ok;
static double   s_kf_sf;           /* Sf: phase process noise, ns^2 per second       */
static double   s_kf_ms_f1;        /* scatter of the ONE-SECOND counter, Hz^2         */
#define KF_FWIN 100u               /* samples in the gated frequency average          */
static float    s_kf_du_ring[KF_FWIN];  /* cumulative commanded frequency, ns/s       */
static uint8_t  s_kf_du_i;
static double   s_kf_du_cum;
/* R is estimated from phase differences this many seconds apart. At lag one
 * only the WHITE part of the detector noise survives differencing; at sixteen
 * the slow zero wander comes back into the estimate. See the note at the
 * estimator for the bench run that made this matter. */
/* The widest horizon the CLI will accept. It is not a tuning choice - it is
 * the declared range of KT - but the Q floor below is derived from it, so the
 * two must not be allowed to drift apart in separate literals. */
#define KF_HORIZON_MAX 10000u

#define KF_R_LAG 16u
/* History for the lag-KF_R_LAG R estimator below. */
static int32_t  s_kf_ph_hist[KF_R_LAG];
static uint32_t s_kf_ph_hist_n;    /* 0..KF_R_LAG; a gap or an arm empties it */
static uint32_t s_kf_ph_hist_i;    /* next write slot == oldest sample when full */
static double   s_kf_q_use;        /* oscillator walk, seeded from R and KT, then adapted */
static double   s_kf_ms_innov;     /* EMA of innovation^2                    */
static double   s_kf_q_max;        /* the ceiling in force, for the KL report */
static double   s_kf_q_floor;      /* and the floor, so KL can name that too  */
static bool     s_kf_q_hold;       /* true while the innovations say nothing about Q */
static uint32_t s_kf_q_freeze;     /* seconds left of a post-arm adaptation freeze   */
/* THE ADAPTATION'S OWN READOUT. Q and its ceiling cannot distinguish "pinned all
 * night" from "breathed between the rails and happened to be up at the end",
 * and an unattended run produces exactly one KL. The ratio says which way the
 * innovations are pushing right now; the low and high water marks say what the
 * night actually did. Both cost three doubles and no arithmetic. */
static double   s_kf_q_ratio;      /* last Pobs/Ppred the adaptation acted on  */
static double   s_kf_q_lo;         /* lowest Q since TRACKING began            */
static double   s_kf_q_hi;         /* and the highest                          */
static bool     s_kf_q_wm_ok;      /* false until the first tracking second     */
static uint32_t s_kf_r_upd;        /* how many times R's EMA has been fed     */
static double   s_kf_last_innov;
static double   s_kf_last_S;
static uint32_t s_kf_rejects;
/* The rejection RATE, as against the running total.
 *
 * The 29/30.08 twelve-hour run threw away 4904 readings — ELEVEN PER CENT — and
 * nothing said so: the Learn line carries a cumulative rej= that nobody
 * differentiates while watching it scroll, and the phase sd looked ordinary.
 * The cause was KR pinned at 2.5 ns, which fixes R below the innovations the
 * detector actually produces and leaves the 4-sigma gate too tight; with R
 * measured, the same board and the same simulator both reject nothing at all.
 * A loop discarding a ninth of its data should have to say so out loud. */
static double   s_kf_rej_rate;     /* EMA of the reject flag, ~300 s */
static uint32_t s_kf_holdover;
static uint32_t s_kf_rej_run;      /* CONSECUTIVE rejects — see the gate     */
static uint32_t s_kf_frej_run;     /* the same, for the TIM2 gate            */
static uint16_t s_kf_f_blank;      /* seconds of TIM2 silence left after a reset */
static uint16_t s_kf_ph_blank;     /* ...and of DETECTOR silence after an arm    */
static uint32_t s_kf_far_s;        /* seconds the phase has stayed far out   */
static bool     s_kf_start_arm;    /* (re)start: re-reference once, no wait   */
static uint32_t s_kf_railed;       /* seconds the detector has said nothing  */
static double   s_kf_arm_land;     /* where an arm actually lands, ns (EMA)  */
static bool     s_kf_arm_land_ok;  /* ...once one has been seen              */
static bool     s_kf_arm_wait;     /* an arm is in flight; catch its landing */
static uint32_t s_kf_blind;        /* seconds with no readable phase at all  */
static uint32_t s_kf_arm_hold;     /* seconds to leave the divider alone     */
static uint32_t s_kf_arms;         /* picDIV arms this session               */
static double   s_kf_f_ema;        /* 1 s frequency error, smoothed [Hz]     */
static double   s_kf_ms_fdiff = 4.0;  /* (2 ns/s)^2; TIM2 resolution, not the board's */
static double   s_kf_fprev;
static bool     s_kf_fprev_valid;
/* Does the phase actually move the way TIM2 says it must? See the test. */
#define KF_TRUST_W 32u
static bool     s_kf_trust = true;
/* Measurements the filter has actually used since its last reset. Gates the
 * short control horizon; see the latch. */
static uint32_t s_kf_upd_n;
/* Last second's correction, so the change in it can be rate-limited; see the
 * slew limit in the output stage. */
static double   s_kf_du_prev;
static uint32_t s_kf_w_n;
static double   s_kf_w_ph0, s_kf_w_exp;
static uint32_t s_kf_dead_run;
static uint32_t s_kf_quiet;        /* windows with nothing to conclude from   */
static bool     s_kf_convicted;    /* this detector has been caught before    */
/* The part of a correction the output stage could not take, kept for the next
 * second. See the end of kalman_ctl() for why discarding it parks the loop. */
static double   s_kf_carry;

/* ---- EVERYTHING THIS LOOP NEEDS TO KNOW ABOUT THE BOARD -----------------
 *
 * Algorithm 13 shipped with three numbers measured on ONE board: R seeded at
 * (2.64 ns)^2, Q at 2e-6, and a cold-start covariance of (100 ns)^2. They are
 * this OCXO and this detector, and on anything else they are wrong — a board
 * with a 300 ns detector starts with a prior four times wider than its whole
 * band, and one with a 10 000 ns detector starts far tighter than the truth and
 * spends its first ten minutes rejecting perfectly good readings. The 27.08 run
 * shows the second failure plainly: 281 rejections and 500 s to pull in from
 * 1300 ns, on the board the constants were measured on.
 *
 * CT and LC already measure what is needed, so nothing here is a constant:
 *
 *   CT  ->  lsb_per_ns   the counts that null one ns of phase in one second
 *   LC  ->  ns_per_volt  how many ns the detector's ramp covers per volt
 *           range_ns     the usable width of that ramp
 *
 * and one number the part fixes: the ADC is 12 bits over 3.3 V, so the detector
 * cannot resolve better than ns_per_volt * 3.3/4096. On this board that is
 * 1.01 ns against a measured noise of 2.6 ns — two and a half quanta, which is
 * what a ramp read by a 12-bit ADC looks like, and is where the R seed comes
 * from. Everything else follows from those, with the reasoning at each line.
 *
 * Recomputed every second rather than cached: CT and LC can be re-run while the
 * loop is on, and a scale that needs the operator to remember to restart the
 * algorithm is a scale that will be wrong on somebody's bench. It is ten
 * multiplications. */
typedef struct {
    double res_ns;      /* one ADC step, in ns                              */
    double r_floor;     /* R cannot be below one quantum                    */
    double r_seed;      /* first guess at the detector noise                */
    double p0;          /* cold-start phase variance                        */
    double p1;          /* cold-start frequency variance                    */
    double p2;          /* cold-start aging variance                        */
    double q_seed;      /* first guess at the oscillator's walk             */
    double q_floor;     /* numerical rail under Q - NOT a function of KT     */
    double qa;          /* aging process noise                              */
    double arm_hz;      /* frequency close enough to disturb the divider    */
    double lim_lsb;     /* biggest correction that keeps the phase readable */
    double trust_ns;    /* below this the follow test reads its own noise   */
} kf_scale_t;

static void kf_scale(kf_scale_t *k, double lsb_per_ns)
{
    double nsv   = (double)g_ltic.ns_per_volt;
    double range = (double)g_ltic.range_ns;
    double T     = (double)((g_kf_horizon_s < 10u) ? 10u : g_kf_horizon_s);

    if (nsv   < 1.0)   nsv   = 100.0;     /* LC not run: something sane so the */
    if (range < 10.0)  range = 1000.0;    /* numbers below stay finite         */

    /* The detector's quantum. 12 bits over 3.3 V is the part, not a choice. */
    k->res_ns  = nsv * 3.3 / 4096.0;
    k->r_floor = k->res_ns * k->res_ns;
    /* Two and a half quanta, which is what this ramp measured (2.6 ns against
     * 1.01 ns steps). A seed only: R is replaced by the detector's own first
     * differences within a few hundred seconds. */
    k->r_seed  = (2.5 * k->res_ns) * (2.5 * k->res_ns);

    /* COLD START. The phase can be anywhere in the band the detector can show,
     * so half the band is the honest one-sigma — not 100 ns, which was this
     * board's idea of "far". Getting this wrong is not cosmetic: too tight and
     * the innovation gate rejects the truth, which is the 281 rejections and
     * the 500 s pull-in in the 27.08 log. */
    k->p0 = (range * 0.5) * (range * 0.5);
    /* And the frequency can be anything that crosses that band within one
     * horizon — beyond that the phase would leave the detector before the loop
     * could act on it, so it is the widest rate worth entertaining. */
    { double f = range / (2.0 * T); k->p1 = f * f; }

    /* Q, the oscillator's frequency random walk. There is no measurement of it
     * at CT or LC time — it is a property of the crystal, not of the wiring —
     * but it does not need one: the filter adapts Q from its own innovations
     * within minutes. What the seed must do is set a sane BANDWIDTH for those
     * first minutes, and the scale-correct way to say that is "let the phase
     * noise and the frequency walk balance at the horizon": Q = R / T^3, whose
     * units are (ns/s)^2 per second exactly. On this board that is 6.6e-6
     * against the 2.0e-6 the night run measured — the same order from two
     * completely different routes, which is the most one can ask of a seed. */
    k->q_seed = k->r_seed / (T * T * T);

    /* THE FLOOR UNDER Q, AND WHY IT MUST NOT CONTAIN T.
     *
     * The rail under the adaptation used to be q_seed/1000, which is
     * r_seed/(1000 T^3): the SAME T that sets the ceiling. Both rails therefore
     * moved together, and changing KT slid a fixed 1000x window up and down
     * instead of giving the adaptation any more room than it had before. That
     * is not a theoretical objection. 02.09, three captures on one board:
     *
     *   KT=100  Q = 7.777e-06  = R/T^3        - on the CEILING
     *   KT= 40  Q = 9.936e-08  = q_seed/1000  - on the FLOOR
     *   KT= 20  Q = 7.949e-07  = q_seed/1000  - on the FLOOR
     *
     * to four significant figures in every case. Q had never once come to rest
     * between the rails, and the KT sweep those runs were meant to be measuring
     * measured the position of a rail instead: KT=40 came out WORSE than KT=100
     * (fitted loop time constant 91 s against 40-65 s) because Q fell 78x when
     * the floor moved under it and the estimator's own (R/Q)^(1/3) went to
     * 462 s. Shorter horizon, slower loop.
     *
     * So the floor stops being a statement about the horizon and becomes what
     * it should always have been: a NUMERICAL rail, there to keep the recursion
     * away from zero, placed far below anything a crystal can produce so that
     * it never takes part in the answer. The scale-correct way to say "far
     * below" without inventing a constant is to ask what Q the same formula
     * would give at the longest horizon this firmware will accept, with a
     * detector at its own quantisation limit. On this board that is about
     * 1e-12, six decades under the seed.
     *
     * The CEILING keeps its T, and should: "do not run faster than the horizon
     * you were given" is precisely what KT means. It is only the floor that had
     * no business knowing about it. */
    {
        double Tmax = (double)KF_HORIZON_MAX;
        k->q_floor  = k->r_floor / (Tmax * Tmax * Tmax);
    }


    /* Aging is the one state with no measurement behind its prior, so it gets
     * the tightest one: it may not contribute more than a hundredth of Q's
     * effect over a horizon. A loose aging prior is how a filter explains a
     * transient as a permanent trend and then steers on it. */
    k->qa = k->q_seed / (100.0 * T * T);
    k->p2 = k->qa * 3600.0;              /* an hour of that walk */

    /* Arming re-syncs the divider and lands the phase at a quantised offset, so
     * it is only worth doing when the frequency is close enough for the phase
     * to STAY there: it must not cross half the band during the 60 s hold-off.
     * 1 Hz is 100 ns/s of phase. Algorithm 12 writes 0.5 Hz here as a constant;
     * this is the same number for this board's 3000 ns band and the right one
     * for any other. */
    /* Before any landing has been seen (see the arm gate) this is the only
     * budget available, and half the band over 60 s was far too generous - the
     * landing offset alone turned out to be most of the band. A quarter of the
     * band over one horizon is the conservative first guess, and measurement
     * replaces it as soon as one arm has landed. */
    { double T2 = (T < 10.0) ? 10.0 : T;
      k->arm_hz = range / (4.0 * 100.0 * T2); }

    /* A correction is only useful while the phase stays measurable: 1% of the
     * band per second crosses it in 100 s, comfortably slower than the horizon.
     */
    k->lim_lsb = range * 0.01 * lsb_per_ns;
    if (k->lim_lsb < 10.0) k->lim_lsb = 10.0;

    /* Below two quanta the follow test would be reading the ADC rather than the
     * detector. */
    k->trust_ns = 2.0 * k->res_ns;
}

void kf_get_stats(kf_stats_t *out)
{
    out->phase_ns    = (float)s_kf_x0;
    out->freq_ns_s   = (float)s_kf_x1;
    out->aging_ns_s2 = (float)s_kf_x2;
    out->sigma_ns    = (float)sqrt(s_kf_P[0][0] > 0.0 ? s_kf_P[0][0] : 0.0);
    out->r_ns        = (float)((g_kf_r_ns > 0.0f) ? (double)g_kf_r_ns
                                                  : sqrt(s_kf_ms_diff16));
    out->q           = (float)((g_kf_q > 0.0f) ? (double)g_kf_q : s_kf_q_use);
    out->q_max       = (float)s_kf_q_max;
    /* At the ceiling means the innovations still want a faster filter than the
     * horizon allows — usually correlated detector error, which R has already
     * been told about. Worth saying out loud: the fault it replaced was found
     * only by grepping a capture for this number. */
    out->sf          = (float)s_kf_sf;
    out->q_adapting  = (g_kf_q <= 0.0f);
    out->q_at_max    = (g_kf_q <= 0.0f) && (s_kf_q_max > 0.0) &&
                       (s_kf_q_use >= s_kf_q_max * 0.999);
    /* And the other rail. It went unreported for three captures and cost a
     * whole KT sweep: KL said the ceiling was clear and it was, because Q had
     * gone straight past the middle and parked on the floor instead. A report
     * that names only one of two rails is a report that hides half the faults. */
    out->q_at_min    = (g_kf_q <= 0.0f) && (s_kf_q_floor > 0.0) &&
                       (s_kf_q_use <= s_kf_q_floor * 1.001);
    out->q_min       = (float)s_kf_q_floor;
    out->q_held      = (g_kf_q <= 0.0f) && s_kf_q_hold;
    out->q_freeze_s  = s_kf_q_freeze;
    out->ctl_s       = (uint16_t)kf_ctl_horizon();
    out->q_ratio     = (float)s_kf_q_ratio;
    out->q_lo        = (float)s_kf_q_lo;
    out->q_hi        = (float)s_kf_q_hi;
    out->innov_ns    = (float)s_kf_last_innov;
    out->rejects     = s_kf_rejects;
    out->rej_pct     = (float)(100.0 * s_kf_rej_rate);
    out->r_pinned    = (g_kf_r_ns > 0.0f);
    out->holdover_s  = s_kf_holdover;
    out->arms        = s_kf_arms;
}

static void kf_reset(const kf_scale_t *k)
{
    s_kf_x0 = s_kf_x1 = s_kf_x2 = 0.0;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) s_kf_P[r][c] = 0.0;
    /* Wide open on phase and frequency, tight on aging — but WIDE OPEN ACCORDING
     * TO THIS DETECTOR, not to the one these numbers were first measured on.
     * See kf_scale(): half the band, and the rate that crosses it in a horizon. */
    s_kf_P[0][0] = k->p0;
    s_kf_P[1][1] = k->p1;
    s_kf_P[2][2] = k->p2;
    /* The measured estimators start from the board's own scale too, and are
     * replaced by measurement within a few hundred seconds. */
    s_kf_ms_diff16 = k->r_seed;
    s_kf_ms_diff1   = k->r_seed;   /* both start on the seed, so Sf starts at zero */
    s_kf_ph_prev1_ok = false;
    s_kf_sf         = 0.0;
    s_kf_ms_f1      = 0.25;    /* +/-0.5 Hz of integer count, until measured */
    s_kf_du_i       = 0;
    s_kf_du_cum     = 0.0;
    for (unsigned i = 0; i < KF_FWIN; i++) s_kf_du_ring[i] = 0.0f;
    s_kf_q_use   = k->q_seed;
    s_kf_q_max   = 0.0;      /* until the adaptation computes one from R */
    s_kf_q_floor = k->q_floor;
    s_kf_q_hold  = false;
    s_kf_q_freeze = 0;
    s_kf_q_ratio  = 0.0;
    s_kf_q_lo     = k->q_seed;
    s_kf_q_hi     = k->q_seed;
    s_kf_q_wm_ok  = false;
    s_kf_r_upd   = 0;
    s_kf_rej_rate   = 0.0;
    s_kf_ms_innov   = 0.0;
    s_kf_last_innov = 0.0;
    s_kf_last_S     = 0.0;
    s_kf_ph_hist_n  = 0;
    s_kf_ph_hist_i  = 0;
    s_kf_upd_n      = 0;       /* ...and has averaged nothing yet: see the latch */
    s_kf_du_prev    = 0.0;
    s_kf_locked     = false;   /* a filter that has just been reset knows nothing */
    s_kf_holdover   = 0;
    s_kf_rej_run    = 0;
    s_kf_frej_run   = 0;
    /* TIM2 goes quiet for the first boxcar after a reset - see the comment
     * where the blank is consumed. */
    s_kf_f_blank    = 100u;
    s_kf_ph_blank   = 0u;
    s_kf_far_s      = 0;
    s_kf_railed     = 0;
    s_kf_arm_land   = 0.0;
    s_kf_arm_land_ok = false;
    s_kf_arm_wait   = false;
    s_kf_blind      = 0;
    s_kf_arm_hold   = 0;
    s_kf_fprev_valid = false;
    s_kf_trust      = true;
    s_kf_ctl_fast   = false;    /* KC re-earns its speed after a restart */
    s_kf_w_n        = 0;
    s_kf_dead_run   = 0;
    s_kf_quiet      = 0;
    s_kf_convicted  = false;
    s_kf_carry      = 0.0;
    s_kf_init       = true;
}

/* ---- persistence -------------------------------------------------------
 * Its own ring record, versioned by its first byte. Written when the operator
 * changes a value, which is rare and deliberate — the same pattern LC uses. An
 * older firmware simply never asks for this type, and a record written by an
 * older version of this layout is rejected on the version byte rather than
 * misread. */
#define A13_REC_VER 2u          /* 1 = R,Q,KT (12 B); 2 = adds KC (14 B) */

void kf_store_save(void)
{
    uint8_t b[14];
    b[0] = A13_REC_VER; b[1] = 0;
    memcpy(&b[4], &g_kf_r_ns, 4);
    memcpy(&b[8], &g_kf_q,    4);
    /* KT rides in the two spare header bytes rather than growing the record. */
    b[2] = (uint8_t)(g_kf_horizon_s & 0xFFu);
    b[3] = (uint8_t)(g_kf_horizon_s >> 8);
    b[12] = (uint8_t)(g_kf_ctl_s & 0xFFu);
    b[13] = (uint8_t)(g_kf_ctl_s >> 8);
    (void)flash_ring_write(REC_A13, b, sizeof(b));
}

bool kf_store_load(void)
{
    /* VERSION 1 STILL LOADS. Rejecting it would have been two lines shorter and
     * would have silently reset an operator's KR/KQ/KT on the one upgrade that
     * had no reason to touch them. */
    uint8_t b[14];
    uint16_t n = flash_ring_read_newest(REC_A13, b, sizeof(b));
    if (n < 12u) return false;
    if (b[0] != 1u && b[0] != A13_REC_VER) return false;
    float r, q;
    memcpy(&r, &b[4], 4);
    memcpy(&q, &b[8], 4);
    uint16_t t = (uint16_t)b[2] | ((uint16_t)b[3] << 8);
    if (r >= 0.0f && r <= 1000.0f)   g_kf_r_ns = r;
    if (q >= 0.0f && q <= 1.0f)      g_kf_q    = q;
    if (t >= 10u && t <= (uint16_t)KF_HORIZON_MAX) g_kf_horizon_s = t;
    if (b[0] == A13_REC_VER && n >= 14u) {
        uint16_t c = (uint16_t)b[12] | ((uint16_t)b[13] << 8);
        if (c == 0u || (c >= 10u && c <= (uint16_t)KF_HORIZON_MAX)) g_kf_ctl_s = c;
    }
    return true;
}

uint16_t kalman_ctl(uint16_t pwm, uint32_t ppscount)
{
    (void)ppscount;
    static bool s_warned_pol = false;

    if (algo_take_restart()) {
        s_kf_init    = false;
        s_kf_rejects = 0;
        s_kf_arms    = 0;
        s_kf_start_arm = true;
        s_kf_f_ema   = 0.0;
        s_warned_pol = false;
        /* R and Q survive: they describe the detector and the oscillator, not
         * the previous run. The same rule algo 12 follows. */
    }
    if (g_ltic.polarity == 0) {
        if (!s_warned_pol) {
            OUT_SERIAL.println("LTIC: polarity unset - run 'LPOL -1' (or +1) then 'ES'. Holding.");
            s_warned_pol = true;
        }
        s_kf_locked = false;
        set_trend("NoPL");
        return pwm;
    }
    if (g_ltic.ns_per_volt == 0.0f || g_ltic.range_ns == 0.0f) {
        s_kf_locked = false;
        set_trend("NoCT");   /* not set_trend(0) — see algorithm 11's copy */
        return pwm;
    }
    /* Scale, from CT. lsb_per_ns is the counts that null one nanosecond of
     * phase in one second, so its reciprocal is the ns/s one count buys. */
    double lsb_per_hz = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40) : 0.0;
    if (lsb_per_hz <= 0.0) { s_kf_locked = false; set_trend("NoCT"); return pwm; }
    double lsb_per_ns = lsb_per_hz / 100.0;

    /* Everything below that is not a state comes from here — CT and LC, once a
     * second, so a re-run of either takes effect without restarting the loop. */
    kf_scale_t ks;
    kf_scale(&ks, lsb_per_ns);
    s_kf_q_floor = ks.q_floor;   /* LNV can change under a live loop */

    if (!s_kf_init) {
        kf_reset(&ks);
        /* Say what the board turned out to be. One line, once, at the moment
         * the numbers are chosen — a derived constant nobody can read is a
         * constant nobody can check. */
        OUT_SERIAL.print("KAL: from CT/LC  res ");
        OUT_SERIAL.print(ks.res_ns, 2);       OUT_SERIAL.print("ns  R0 ");
        OUT_SERIAL.print(sqrt(ks.r_seed), 2); OUT_SERIAL.print("ns  Q0 ");
        OUT_SERIAL.print(ks.q_seed, 9);       OUT_SERIAL.print("  P0 ");
        OUT_SERIAL.print(sqrt(ks.p0), 0);     OUT_SERIAL.print("ns  lim ");
        OUT_SERIAL.print(ks.lim_lsb, 0);      OUT_SERIAL.print("LSB  arm<");
        OUT_SERIAL.print(ks.arm_hz, 2);       OUT_SERIAL.println("Hz");
    }

    /* ---- THE SECOND MEASUREMENT: TIM2 -------------------------------------
     *
     * This was missing, and its absence was not a missing feature — it was a
     * loop that could not see. With the picDIV unsynced the detector reports
     * nothing valid, and a filter with only a phase measurement then has NO
     * measurement at all: it predicts, steers from a state that is still zero,
     * and the oscillator walks wherever it likes. That is exactly what the
     * board did — the frequency climbing steadily with the loop reporting HOLD
     * and doing nothing about it.
     *
     * Algorithms 11 and 12 both have this input, and algorithm 12 says why in
     * as many words: while the phase detector is blind, the frequency still
     * reads true. For a Kalman filter it is not even a special case — it is a
     * second measurement of a state the filter already carries, so it goes in
     * as one more scalar update below.
     *
     * ORIENTATION. TIM2 and the detector sit opposite on this wiring (the
     * algorithm-11 finding, and the reason algo 12's frequency and phase terms
     * carry different signs). 1 Hz high at 10 MHz is 1e-7, which is 100 ns of
     * phase per second — with a minus, because the phase RUNS DOWN when the
     * oscillator runs fast. No LTIC polarity here: that belongs to the
     * detector, and this measurement does not come through it. */
    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10) { s_kf_locked = false; set_trend("WAIT"); return pwm; }
    {
        /* The one-second counter's own scatter, which is what the gated average
         * is built from. Measured here rather than assumed: the quantisation of
         * an integer 1 s count is +/-0.5 Hz, but the real figure also carries
         * the PPS jitter and whatever the oscillator did, and only the board
         * knows those. */
        double d1 = (double)s.instant_offset - s_kf_f_ema;
        s_kf_ms_f1 += 0.002 * (d1 * d1 - s_kf_ms_f1);
        if (s_kf_ms_f1 < 0.01) s_kf_ms_f1 = 0.01;
    }
    s_kf_f_ema += ((double)s.instant_offset - s_kf_f_ema) * 0.02;
    double f_meas = s.have100 ? (s.avg100 - 10000000.0) : s_kf_f_ema;
    double z_f    = -100.0 * f_meas;              /* ns/s */

    bool   ph_valid = false;
    double ph       = ltic_phase_error_ns(&ph_valid, ppscount);
    bool   raw      = (ph_valid && g_ltic.ns_per_volt > 1.0f);

    /* ...AND A READING TAKEN WHILE THE DIVIDER IS STOPPED IS NOT A PHASE.
     *
     * Arming the picDIV stops its output and waits for the next 1PPS edge. The
     * LTIC ramp goes on being sampled the whole time, and with nothing to stop
     * it, it reads near its top. That reading is in band, it is quantised, it
     * even carries a nanosecond of jitter - there is nothing in it for the gate
     * or the filter to object to. It is simply not a phase.
     *
     * Six arms in the 03/04.09 overnight capture, the reading in each of the
     * three seconds after the arm and then the fourth:
     *
     *   t+102   1425.4  1378.0  1378.0  |  -1453.4
     *   t+445   1437.5  1381.0  1381.0  |   -947.1
     *   t+510   1437.5  1381.0  1381.0  |  -1106.4
     *   t+575   1437.5  1381.0  1381.0  |  -1740.9
     *   t+1597  1444.7  1394.9  1397.0  |  -1326.9
     *   t+2202 -1357.5  1378.1  1380.3  |  -1320.8
     *
     * Three samples of 1378..1445 - the same numbers every time, because it is
     * the rail and not a measurement - then the landing, which is where the arm
     * model says it should be. WHAT IT COST on the first arm of that capture:
     * the loop was freshly reset, so the gate was open on P00 = (range/2)^2, it
     * took the three rail readings as phase and commanded +1653 LSB in 105 s.
     * The board had been within 0.01 Hz when it started (TIM2 said so) and was
     * 0.50 Hz out when it finished - fifty times the arm gate's own band, put
     * there by the loop itself. The phase then crossed the detector at ~100
     * ns/s, the ramp railed, the trust test convicted it (rightly - it was not
     * following), and three more arms could not help because a convicted loop
     * does not steer. It took the 30-minute expiry to get out: 2373 s of HOLD
     * on a board that had been locked when it was switched on.
     *
     * So: no reading for three seconds after an arm, exactly as TIM2 gets no
     * reading for one boxcar after a reset, and for the same reason. `raw`
     * false is the honest description - the detector said NOTHING, which is
     * true, and every consumer downstream already knows what to do with it.
     * The landing capture below then catches the first real reading, which is
     * the one the arm gate wanted all along and never got.
     *
     * HOW MANY, AND WHY THE COUNT IS FOUR AND NOT THREE (build 42). Three was
     * the number of rail readings VISIBLE in a capture, and the loop sees one
     * more than that, because the count starts at the REQUEST and the request
     * is not the pin. ltic_arm_picdiv() only sets an event bit; the control
     * task picks it up on its next wake and pulls the pin, holds it for
     * PICDIV_ARM_MS = 1001 ms - deliberately just PAST a second, so the release
     * lands after the edge rather than racing it - and the divider then syncs
     * on the following 1PPS. Only the ramp after that is a phase.
     *
     * Build 41 blanked three and leaked the fourth, at both of its arms:
     *
     *   arm at cycle A=101   rails at A+2 A+3 A+4   loop consumed 1377.0 ns
     *   arm at cycle A=661   rails at     A+3 A+4   loop consumed 1361.5 ns
     *
     * and each leak cost about 430 LSB of correction to unwind. Note where the
     * rail STARTS: A+2 in one, A+3 in the other, because the dispatch wake is
     * where the jitter is. Note where it ENDS: A+4 in both, because the end is
     * set by the 1001 ms hold and the resync, which are deterministic. So four
     * is not three plus a safety margin - it is the cycle the rail actually
     * ends on, twice, and the number to raise if a capture ever shows a fifth. */
    if (s_kf_ph_blank > 0u) { s_kf_ph_blank--; raw = false; }

    int32_t ph_i    = raw ? (int32_t)(ph + (ph < 0 ? -0.5 : 0.5)) : 0;

    /* ---- DOES THE PHASE MOVE THE WAY TIM2 SAYS IT MUST? -------------------
     *
     * A detector can fail in a way that looks like data: the 26.08 21:47 reset
     * left the picDIV unsynced with Vphase flat at 3.116 V, and because LRN is
     * 3000 the whole swing counts as in-band, so the firmware read a perfectly
     * valid +1295 ns that never changed for the entire capture. A filter has no
     * defence against that on its own — a constant reading is a consistent
     * reading, the innovations go to zero, R falls to its floor and the filter
     * believes it MORE the longer it lies.
     *
     * The physics settles it. The phase and the frequency are the same quantity
     * differentiated, so over W seconds the phase MUST move by the sum of the
     * frequency error over that window. TIM2 measures that independently of the
     * detector. If the detector does not move when TIM2 says it must, it is not
     * measuring anything.
     *
     * This is algorithm 12's stall watch with the guesswork taken out: that one
     * predicted the motion from the slew it had just commanded, which is the
     * loop marking its own homework. This compares against a second instrument.
     *
     * Tested only when the expected motion clears the detector's noise, so a
     * board sitting on frequency — where nothing is expected to move and
     * nothing does — is never accused. */
    {
        double R_now = (g_kf_r_ns > 0.0f) ? ((double)g_kf_r_ns * (double)g_kf_r_ns)
                                          : s_kf_ms_diff16;
        if (R_now < ks.r_floor) R_now = ks.r_floor;
        /* ONLY ON THE GATED 100 s AVERAGE. The 1 s EMA that stands in for it
         * early on lags by fifty seconds, so during a pull-in it says the phase
         * should be moving at a rate that was true a minute ago — and the test
         * then convicts a perfectly good detector. That is not hypothetical: it
         * happened on the first run of this code, the verdict LATCHED, and the
         * loop spent the rest of the run blind with P00 pinned at its cold-start
         * value, re-arming the divider every ten minutes. A test that can only
         * run when its reference is trustworthy is worth more than a test that
         * always runs. */
        if (!raw || !s.have100) {
            s_kf_w_n = 0;                    /* nothing to test */
        } else {
            if (s_kf_w_n == 0u) { s_kf_w_ph0 = (double)ph_i; s_kf_w_exp = 0.0; }
            s_kf_w_exp += z_f;
            if (++s_kf_w_n >= KF_TRUST_W) {
                double moved  = (double)ph_i - s_kf_w_ph0;
                double aexp   = (s_kf_w_exp < 0.0) ? -s_kf_w_exp : s_kf_w_exp;
                double amov   = (moved < 0.0) ? -moved : moved;
                /* Two samples of detector noise, four sigma of it, and never
                 * less than two ADC steps: below that the test would be reading
                 * its own uncertainty. Both terms come from the board. */
                double floor_ns = 4.0 * sqrt(2.0 * R_now);
                if (floor_ns < ks.trust_ns) floor_ns = ks.trust_ns;
                /* AND NEVER BELOW THE REFERENCE'S OWN RESOLUTION. The
                 * expectation this test runs against is z_f = -100*avg100,
                 * and the gated 100 s average moves in 0.01 Hz quanta - 1 ns/s,
                 * or W ns of "expected motion" over a window, from ONE quantum
                 * of bias. The floor above covers the DETECTOR's noise, not the
                 * reference's, and 4*sqrt(2R) ~ 16.5 ns sits under one quantum
                 * (32 ns at W = 32): an oscillator parked near a quantisation
                 * boundary opened the test on phantom motion while the loop was
                 * locked tight, and three windows convicted a healthy detector
                 * - 03.09 20:11, dph -1.3 ns and Vphase in band at the verdict,
                 * thirty minutes of HOLD until the expiry, and the same fault
                 * already on record at 02.09 10:59. Never accuse on expected
                 * motion the reference cannot distinguish from zero. The cost
                 * is stated: a frozen detector needs real offset above one
                 * quantum before the test can see it at all. */
                double q_ns = 100.0 * (1.0 / 100.0) * (double)KF_TRUST_W;
                if (floor_ns < q_ns) floor_ns = q_ns;
                /* AND ONE QUANTUM IS NOT THE WHOLE OF THE REFERENCE'S NOISE.
                 * The quantum floor above is right in kind and short in
                 * magnitude: measured on the run it was written for, the
                 * windows immediately before the false verdict carried
                 * |aexp| = 83 ns - two and a half quanta - because the gated
                 * average dipped to -0.04 Hz. At 32 ns that window still
                 * convicts (amov 20.0 against 0.25*aexp = 20.7); the verdict
                 * this floor exists to stop is the one it lets through.
                 *
                 * So take the floor from the reference's OWN measured scatter
                 * rather than from its display step. Rf is the variance of z_f
                 * and the filter already computes it for the TIM2 update; the
                 * thirty-two readings in a window come from a hundred-second
                 * boxcar and so share almost all their content, which makes the
                 * accumulated noise W*sigma rather than sqrt(W)*sigma. On this
                 * board that is 32 * 2.9 = 93 ns, which clears the 83 ns event
                 * by twelve per cent and follows the antenna instead of being a
                 * constant.
                 *
                 * It is a one-sigma test and deliberately so: two sigma would
                 * be 186 ns and would blind the detector-frozen check that this
                 * whole test exists for. The stated cost is that a frozen
                 * detector now needs a real frequency offset of about 0.03 Hz
                 * before it can be convicted at all - and below that there is
                 * no phase motion to miss. */
                {
                    double nf = s.have100 ? (double)KF_FWIN : (2.0 / 0.02 - 1.0);
                    double Rf_now = (100.0 * 100.0) * s_kf_ms_f1 / nf;
                    if (Rf_now < 0.25) Rf_now = 0.25;
                    double ref_ns = (double)KF_TRUST_W * sqrt(Rf_now);
                    if (floor_ns < ref_ns) floor_ns = ref_ns;
                }
                if (aexp > floor_ns) {
                    if (amov < 0.25 * aexp) {
                        if (s_kf_dead_run < 255u) s_kf_dead_run++;
                        /* Three failing windows to convict a detector that has
                         * never been caught, ONE for a repeat offender. The
                         * expiry above has to give a convicted detector its
                         * hearing back or the loop can deadlock blind, and on a
                         * genuinely frozen one that hearing costs a phase
                         * excursion every half hour — so the burden of proof
                         * drops once it has lied. Simulated on the frozen
                         * +1295 ns fault, that is a third of the excursion for
                         * the same frequency accuracy. */
                        if (s_kf_dead_run >= (s_kf_convicted ? 1u : 3u)) {
                            s_kf_trust     = false;
                            s_kf_convicted = true;
                        }
                    } else {
                        s_kf_dead_run = 0;
                        s_kf_trust    = true;
                    }
                    s_kf_quiet = 0;
                } else if (!s_kf_trust) {
                    /* INCONCLUSIVE, AND THAT MATTERS WHEN THE VERDICT IS GUILTY.
                     * A distrusted detector leaves the loop steering on TIM2
                     * alone, which holds the frequency well — so nothing is
                     * expected to move, no window can conclude, and the verdict
                     * would stand for ever on evidence that has long expired.
                     * Half an hour without a single conclusive window is not
                     * proof of a fault; it is absence of proof, so the detector
                     * gets its hearing back. If it really is frozen the next
                     * conclusive window convicts it again, and the cost of that
                     * is bounded by the hold-off. */
                    if (++s_kf_quiet >= (1800u / KF_TRUST_W)) {
                        s_kf_trust    = true;
                        s_kf_dead_run = 0;
                        s_kf_quiet    = 0;
                        OUT_SERIAL.println("KAL: no evidence either way for 30 min - trusting the detector again");
                    }
                }
                s_kf_w_n = 0;
            }
        }
    }
    /* An untrusted detector is not a detector. Everything below treats it
     * exactly as it treats a railed one — including the arm bridge, which is
     * the only thing that can repair it. */
    bool have = raw && s_kf_trust;

    /* THE LATCH THAT LETS KC OFF ITS LEASH - see s_kf_ctl_fast. One-way: set
     * on the first in-band reading the filter is actually using, never
     * cleared except by kf_reset.
     *
     * AND NOT ON THE FIRST ONE, which is what it used to do and what put a
     * 122 LSB step on the DAC one second after every restart.
     *
     * The mechanism, traced second by second on the 03.09 plant: the filter
     * takes its first phase measurement, and after one sample the estimate IS
     * that sample - x0 = +3.89 ns from a reading of +3.98, with sigma 2.5 ns
     * beside it. The reading was in band, so the latch fired, the horizon
     * dropped to KC, and the control dutifully nulled the whole of that single
     * noisy sample inside one horizon. Next second it did the same in the other
     * direction. Four seconds of +122 / -175 / +70 LSB before it damped out -
     * on a board that had been locked to a nanosecond a second earlier. The
     * same thing is visible on the bench: 40835 -> 40978 -> 40871 at the moment
     * algorithm 11 handed over to 13 in the 11.09 capture.
     *
     * The short horizon is for a settled filter making fine corrections. So it
     * waits until the filter has actually had a horizon's worth of measurements
     * to average - not a tuning constant but the loop's own timescale, and the
     * period over which a Kalman transient decays. Until then the gentle
     * horizon applies, which is what an acquisition wants anyway. Nothing about
     * settled behaviour changes: by the time this matters the latch is long
     * since set, and it is one-way. */
    if (have && s_kf_upd_n < 0xFFFFu) s_kf_upd_n++;
    if (have && !s_kf_ctl_fast && s_kf_upd_n >= (uint32_t)g_kf_horizon_s) {
        double axp = (ph_i < 0) ? -(double)ph_i : (double)ph_i;
        if (axp <= (double)g_ltic.acq_threshold_ns) s_kf_ctl_fast = true;
    }

    /* ---- R, measured from the detector's differences at lag KF_R_LAG ---- */
    /* NOT WHILE THE LOOP IS MOVING THE PHASE. The 29.08 three-algo run
     * measured the cost of updating R through a pull-in: after the start arm
     * the phase nulled at ~7 ns/s, every 16-second difference carried ~110 ns
     * of the loop's OWN commanded motion, and R climbed to ~33 (against a
     * white floor of 6.4) - the filter then distrusted the detector for the
     * two hours its 0.002 EMA needed to forget a transient it had caused
     * itself. Phase sd: 11.19 ns after ten minutes, 3.36 ns only in the last
     * hour, improving monotonically the whole time. So the history is still
     * pushed (contiguity is what the lag assumes) but the EMA is frozen
     * while the commanded rate - the frequency estimate plus the phase
     * nulling over the horizon, exactly what u below will ask for - is
     * above 1 ns/s. That is motion the filter knows about, not noise it
     * should learn from. */
    {
        /* KC, NOT KT, AND THE COMMENT ABOVE IS WHY. This guard's contract is
         * "freeze the EMAs while the commanded rate - exactly what u below will
         * ask for - is above 1 ns/s", and since build 36 `u` asks for x0/KC.
         * Left on KT it under-reads the commanded nulling rate by KT/KC = 3 at
         * the default split: motion it scores as 0.4 ns/s is really 1.2 and
         * should freeze. Steady state is unaffected because x0 is small, but
         * every nulling transient - pull-in, post-arm, episode recovery - would
         * feed R and ms_diff1 at three times the intended rate, and KC makes
         * those transients three times steeper, so the two errors compound.
         * Found by GLM-5.3 Max reading build 36 against this comment. */
        double Th = kf_ctl_horizon();
        double mv = fabs(s_kf_x1) + fabs(s_kf_x0) / Th;
        bool moving = (mv > 1.0);
        if (s_kf_q_freeze > 0u) s_kf_q_freeze--;
        if (have) {
            /* Lag 1, under exactly the same guards, because the PAIR is what
             * carries the information: see the Sf derivation below. */
            if (s_kf_ph_prev1_ok) {
                double d1 = (double)(ph_i - s_kf_ph_prev1);
                if (d1 < 0.0) d1 = -d1;
                if (d1 < 300.0 && !moving) {
                    /* WINSORIZED, because this EMA now sets the reference the Q
                     * adaptation subtracts (see there) and no longer only feeds
                     * Sf. The 300 ns gate above is two orders over the white
                     * floor - it catches a jumped reference, not a GPS step of
                     * twenty nanoseconds, and one of those in a 500 s EMA moves
                     * the floor several per cent. Clamping the SQUARE rather
                     * than skipping the sample keeps the estimator unbiased on
                     * the bulk and bounded on the tail.
                     *
                     * The constant is 9x the running mean square, and it has to
                     * be 9 rather than the 3 that looks natural: the EMA holds
                     * 0.5*d1^2, whose mean is sigma^2, while d1 itself has
                     * standard deviation sqrt(2)*sigma. Clamping 0.5*d1^2 at
                     * m*sigma^2 therefore clamps |d1| at sqrt(m) standard
                     * deviations. At m = 3 that is 1.73 sigma: it fires on 8.4%
                     * of samples and biases the floor DOWN by 14% - measured on
                     * 400k Gaussian draws, and it would land squarely on the
                     * quantity this change exists to measure accurately. At
                     * m = 9 it is the intended 3 sigma: 0.27% of samples, 0.5%
                     * bias. The lag-16 EMA below stays loose on purpose - slow
                     * wander is exactly what it is there to see. */
                    double v1  = 0.5 * d1 * d1;
                    double cap = 9.0 * s_kf_ms_diff1;
                    if (v1 > cap) v1 = cap;
                    s_kf_ms_diff1 += 0.002 * (v1 - s_kf_ms_diff1);
                }
            }
            s_kf_ph_prev1    = ph_i;
            s_kf_ph_prev1_ok = true;
            if (s_kf_ph_hist_n < KF_R_LAG) {
                s_kf_ph_hist[s_kf_ph_hist_n++] = ph_i;
            } else {
                double dp = (double)(ph_i - s_kf_ph_hist[s_kf_ph_hist_i]);
                if (dp < 0.0) dp = -dp;
                if (dp < 300.0 && !moving) {
                    s_kf_ms_diff16 += 0.002 * (0.5 * dp * dp - s_kf_ms_diff16);
                    /* Counted, because the Q ceiling below is derived from R and
                     * must not act on an R that is still the seed. */
                    if (s_kf_r_upd < 0xFFFFFFFFu) s_kf_r_upd++;
                }
                s_kf_ph_hist[s_kf_ph_hist_i] = ph_i;
                if (++s_kf_ph_hist_i >= KF_R_LAG) s_kf_ph_hist_i = 0;
            }
        } else {
            /* A gap breaks the spacing the lag assumes; refill from scratch. */
            s_kf_ph_hist_n = 0;
            s_kf_ph_hist_i = 0;
            s_kf_ph_prev1_ok = false;
        }

        /* ---- Sf, THE HALF OF THE CLOCK MODEL THIS FILTER NEVER HAD ----------
         *
         * The two-state clock model every timing text uses carries TWO process
         * noise densities:
         *
         *        | Sf*t + Sg*t^3/3   Sg*t^2/2 |
         *   Q =  |                            |
         *        |    Sg*t^2/2        Sg*t    |
         *
         * Sf is the white FREQUENCY noise (the h0 term, Sf = h0/2), which shows
         * up as a random walk in phase; Sg is the frequency random walk (h_-2).
         * Until now this filter injected Q/3, Q/2, Q - which at t = 1 s is Sg's
         * three terms exactly, with Sf identically zero. Not a knob set to
         * zero: it had no name and nothing measured it.
         *
         * That single omission is the ratchet, seen from the model's side. With
         * no Sf, the only way to explain "the phase moved more this second than
         * I predicted" is to raise Sg - to conclude that the OSCILLATOR'S
         * FREQUENCY is wandering fast. Short-term phase noise, detector noise, a
         * lagged counter reading: all of it was booked as frequency random walk,
         * which raises K1, which is the gain that writes the frequency state,
         * which is what the DAC follows. The adaptation was doing the only thing
         * the model left it.
         *
         * MEASURING IT NEEDS TWO LAGS, NOT ONE. Over a lag of k seconds the
         * phase differences carry
         *
         *     0.5 * E[dp^2]  =  sigma_R^2  +  Sf * k / 2
         *
         * - white detector noise, which does not grow with k, plus a random walk,
         * which does. One lag cannot separate them; two can, and the second one
         * is nearly free because the history for lag KF_R_LAG is already here:
         *
         *     Sf = (m16 - m1) / (KF_R_LAG/2 - 1/2)
         *
         * This is the same floor/slow split tools/logab.py has been printing all
         * along (2.5 ns white against 4.9-5.9 ns of slow structure on this
         * board) - measured, then thrown away, for two weeks.
         *
         * WHAT IT DELIBERATELY DOES NOT DO. R keeps its lag-KF_R_LAG value,
         * which still contains the slow part, so the slow structure is counted
         * twice: once as measurement noise and once as process noise. That is
         * not tidy and it is on purpose. Taking the slow part OUT of R would
         * drop R from ~6.3 ns to ~2.5 ns, narrow the 4-sigma gate to match, and
         * that is precisely the configuration that threw away 11% of its
         * readings on the 29.08 bench with KR pinned at 2.5. The slow structure
         * is also not really a random walk - it is a BOUNDED wander of the
         * detector's zero - so a model that lets P00 grow on it without limit
         * would end up following it. Representing it honestly needs its own
         * state (see doc/AUDIT_algo13_model_gaps.md, item 2). Until then,
         * double-counting errs toward distrusting the detector, which is the
         * safe direction.
         *
         * AND IT IS NOT ADAPTED. Sf comes from a measurement of the detector,
         * not from a feedback loop on the innovations, so it cannot ratchet.
         * That was the point. */
        {
            /* AND IT MUST BE ASKED WHETHER THE DIFFERENCE IS A MEASUREMENT AT
             * ALL, because the first version of this did not and the answer was
             * no. Both m's are exponential averages with alpha = 0.002 of a
             * squared Gaussian, so each carries a standard error of about
             * m*sqrt(alpha) = 4.5% of itself, and their difference about 6.3%.
             * On a clean detector the true difference is far below that - and
             * clamping a noisy signed quantity at zero RECTIFIES it, turning
             * symmetric estimator noise into a positive bias of roughly 0.4
             * sigma. Measured in the simulator with a perfectly white detector,
             * where the honest answer is zero: Sf came out 1.5e-2 ns^2/s, the
             * predicted rectification bias almost exactly, and it cost 60% on
             * phase sd and a factor of two on ADEV at tau 1024 - because a
             * fictitious Sf explains the innovations, the adaptation then starves
             * Sg, and Sg is what lets the filter follow a drifting oscillator.
             *
             * So the difference has to clear its own noise before it counts as
             * one. Two sigma of it is 0.126*m, and that threshold is derived
             * from the EMA constant, not chosen: change alpha and it follows. */
            double dm  = s_kf_ms_diff16 - s_kf_ms_diff1;
            double lim = 0.126 * s_kf_ms_diff1;      /* 2 sigma of the difference */
            dm -= lim;
            if (dm < 0.0) dm = 0.0;                  /* not distinguishable from none */
            double sf = dm / ((double)KF_R_LAG * 0.5 - 0.5);
            if (sf > s_kf_ms_diff16) sf = s_kf_ms_diff16;  /* cannot exceed what was measured */
            s_kf_sf = sf;
        }
    }

    /* R, AND WHY THE LAG IS SIXTEEN SECONDS AND NOT ONE.
     *
     * s_kf_ms_diff16 is half the mean square of the detector's differences at a
     * lag of KF_R_LAG seconds. The first-difference estimator this replaces
     * measures exactly the WHITE part of the noise and nothing else: a ramp TIC
     * read through a 12-bit ADC also has slow error — the zero moves with
     * temperature, the receiver's sawtooth residual walks over minutes, the ramp
     * itself drifts — and two consecutive samples share all of it, so
     * differencing at lag one cancels precisely the part that matters.
     *
     * That blindness was not academic. On the 27/28.08 bench run this loop came
     * out 27% WORSE than algorithm 11 over twelve hours with the same white
     * floor (2.74 against 2.64 ns), while the simulator, whose detector noise
     * was white, said three times better. Replaying that plant with 8 ns of
     * slow detector-zero wander reproduces the reversal exactly (algorithm 11:
     * 7.45 -> 10.07 ns; this loop: 1.22 -> 7.23). A filter that under-estimates
     * its measurement noise trusts the detector too much and steers the wander
     * into the oscillator.
     *
     * Differencing at lag k sees what lag one cancels: white noise still
     * contributes its 2*sigma^2, while a zero walking on scales longer than k
     * contributes roughly its variance over k. KF_R_LAG therefore sits near the
     * horizon the control actually steers over (T defaults to 100 s) rather
     * than at 1 s — the filter is told what the detector is worth across the
     * time it uses it. The history empties on a gap and on a picDIV arm, where
     * the reference jumps and every difference across it is garbage.
     *
     * A SECOND correction was tried earlier and REJECTED — inflating R from a
     * lag-1 autocorrelation of the innovations; see the note by the Q
     * adaptation for why it measured worse. This one is a measurement, not an
     * inference, and needs no whiteness to work. */
    double R = (g_kf_r_ns > 0.0f) ? ((double)g_kf_r_ns * (double)g_kf_r_ns)
                                  : s_kf_ms_diff16;
    if (R < ks.r_floor) R = ks.r_floor;   /* one ADC step: see kf_scale() */

    double Q = (g_kf_q > 0.0f) ? (double)g_kf_q : s_kf_q_use;
    /* Sanity rails for a PINNED KQ, and they are deliberately lopsided. The
     * upper one is three decades over the seed, which scales with the board
     * rather than being a fixed window around one oscillator's figure. The
     * lower one is the numerical floor from kf_scale() - six decades down and
     * free of KT - because an operator who pins a small KQ on purpose should
     * get the number they typed, not a silently different one at each horizon.
     * The ADAPTED value reaches neither: it is bounded at R/KT^3 above and by
     * the same numerical floor below, where the adaptation runs. */
    if (Q < ks.q_floor)         Q = ks.q_floor;
    if (Q > ks.q_seed * 1.0e+3) Q = ks.q_seed * 1.0e+3;
    const double QA = ks.qa;             /* aging walks over weeks */
    const double SF = s_kf_sf;           /* white FM: the h0 half of the model */

    /* ---- predict: x = F x, P = F P F' + Q,  dt = 1 s ---- */
    s_kf_x0 += s_kf_x1 + 0.5 * s_kf_x2;
    s_kf_x1 += s_kf_x2;
    {
        static const double F[3][3] = {{1,1,0.5},{0,1,1},{0,0,1}};
        double T1[3][3], P2[3][3];
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
            double v = 0.0; for (int k = 0; k < 3; k++) v += F[r][k] * s_kf_P[k][c];
            T1[r][c] = v;
        }
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
            double v = 0.0; for (int k = 0; k < 3; k++) v += T1[r][k] * F[c][k];
            P2[r][c] = v;
        }
        /* The clock model's two terms, at tau = 1 s:  Sf*t + Sg*t^3/3 in (0,0),
         * Sg*t^2/2 off-diagonal, Sg*t in (1,1). Sf is measured (above); Sg is
         * the adapted Q. */
        P2[0][0] += SF + Q / 3.0; P2[0][1] += Q / 2.0;
        P2[1][0] += Q / 2.0;      P2[1][1] += Q;
        P2[2][2] += QA;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) s_kf_P[r][c] = P2[r][c];
    }

    /* ---- update, if the detector has anything to say ---- */
    if (!have) {
        /* HOLDOVER, and it needs no code of its own: stop updating and keep
         * predicting. The state still carries frequency and aging, so the
         * control below goes on steering from the model. */
        if (s_kf_holdover < 0xFFFFFFFFu) s_kf_holdover++;
        /* NOPH, not HOLD. Three unrelated states used the same word on one
         * screen: this one (the detector said nothing THIS SECOND), the
         * operator's or the automatic holdover mode printed as [HOLDOVER]
         * beside it, and SW's "holdover - MCU crystal" for a system clock
         * running without PPS. Algorithm 12 already calls this NOPH; there was
         * no reason for 13 to name it differently, and every reason not to. */
        set_trend("NOPH");
    } else {
        s_kf_holdover = 0;
        double S = s_kf_P[0][0] + R;
        double y = (double)ph_i - s_kf_x0;
        s_kf_last_innov = y;
        s_kf_last_S     = S;
        /* INNOVATION GATE. A Kalman filter believes its measurement in
         * proportion to the variance it was told to expect, so a single wild
         * reading — and the 26/27.08 night had two, +/-40 ns — walks the state
         * a long way. Four sigma of the filter's OWN prediction of the
         * innovation is the natural threshold: it is not a number about
         * nanoseconds, it is a number about consistency. */
        double lim = 4.0 * sqrt(S);
        s_kf_rej_rate += (1.0 / 300.0) * ((y > lim || y < -lim ? 1.0 : 0.0) - s_kf_rej_rate);
        if (y > lim || y < -lim) {
            s_kf_rejects++;
            set_trend("REJ ");
            /* A GATE THAT NEVER OPENS IS A BROKEN FILTER. Rejecting one wild
             * reading is the point; rejecting every reading means the state is
             * wrong, not the data — which is what happens on a cold start a long
             * way out, where P has not yet grown to admit the truth. Ten in a
             * row and the filter widens its own belief by the size of what it
             * keeps refusing, which lets the next sample in. Simulated: without
             * this a start at +1500 ns never recovered. */
            if (++s_kf_rej_run >= 10u) {
                s_kf_P[0][0] += y * y;
                s_kf_rej_run  = 0;
            }
        } else {
            s_kf_rej_run = 0;
            double K0 = s_kf_P[0][0] / S;
            double K1 = s_kf_P[1][0] / S;
            double K2 = s_kf_P[2][0] / S;
            s_kf_x0 += K0 * y; s_kf_x1 += K1 * y; s_kf_x2 += K2 * y;
            /* Joseph form: P = (I-KH) P (I-KH)' + K R K'. Longer than the short
             * form and it stays symmetric and positive definite when the
             * arithmetic is tight, which matters on a part with no double FPU. */
            {
                double A[3][3];
                A[0][0] = 1.0 - K0; A[0][1] = 0.0; A[0][2] = 0.0;
                A[1][0] =     -K1;  A[1][1] = 1.0; A[1][2] = 0.0;
                A[2][0] =     -K2;  A[2][1] = 0.0; A[2][2] = 1.0;
                double T1[3][3], P2[3][3];
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
                    double v = 0.0; for (int k = 0; k < 3; k++) v += A[r][k] * s_kf_P[k][c];
                    T1[r][c] = v;
                }
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
                    double v = 0.0; for (int k = 0; k < 3; k++) v += T1[r][k] * A[c][k];
                    P2[r][c] = v;
                }
                double KK[3] = { K0, K1, K2 };
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++)
                    s_kf_P[r][c] = P2[r][c] + KK[r] * R * KK[c];
            }
            set_trend("KAL ");
            /* ---- Q, adapted from the innovations ----
             * The filter predicts that y^2 should average S. If it consistently
             * does not, the model is wrong, and on this plant the term that is
             * wrong is almost always the oscillator's random walk. Nudge Q
             * toward agreement, slowly, and clamp it: this is a self-check, not
             * a second control loop. */
            /* A WHITENESS TEST WAS TRIED HERE AND REJECTED, which is worth
             * recording because the reasoning was sound and the measurement was
             * not. The idea: an optimal filter's innovations are white, so a
             * positive lag-1 correlation in them means the MEASUREMENT noise is
             * correlated — which first differences cannot see — and the right
             * response is to inflate R rather than to widen Q. It was written to
             * explain the 27/28.08 bench run, where this loop came out 27% worse
             * than algorithm 11 with the same detector floor.
             *
             * Replayed, it measured WORSE at every level of correlated detector
             * noise, including none at all: 1.22 -> 1.72 ns with a clean
             * detector, 7.23 -> 8.28 ns with 8 ns of zero wander. The test fires
             * on innovations that are not white for reasons that have nothing to
             * do with the detector — this filter's control writes its own
             * frequency state every second, so its innovations were never going
             * to be white — and the inflation then makes it sluggish for no gain.
             *
             * The bench result it was meant to explain is still unexplained; see
             * the note above kalman_ctl(). It is not this. */
            if (g_kf_q <= 0.0f) {
                s_kf_ms_innov += 0.001 * (y * y - s_kf_ms_innov);
                if (s_kf_ms_innov > 0.0 && S > 0.0) {
                    /* COMPARE ONLY THE PART Q CAN ACTUALLY EXPLAIN.
                     *
                     * This used to test ms_innov against S, and S = HPH' + R.
                     * That test has no lower fixed point, and the reason is
                     * arithmetic rather than tuning: S can never fall below R,
                     * so whenever the innovations come out SMALLER than R alone
                     * the ratio is stuck under 0.8 no matter what Q does, and
                     * the 0.98-per-second decay runs until it hits a rail. The
                     * adaptation was being asked to repair an error in R by
                     * shrinking Q, which is not a thing Q can do.
                     *
                     * And the innovations being smaller than R is not a corner
                     * case here - it is the normal state of this board since the
                     * sawtooth pairing was fixed. R is measured at about 2.9-3.0
                     * ns while a structure-function fit of the same captures
                     * puts the white part at 2.35-2.65, so S over-predicts by
                     * roughly 40% in variance and the ratio sits near 0.77.
                     * Near, not far: at KT=100 it measured 0.83 and Q froze on
                     * the ceiling where an earlier climb had left it; at KT=40
                     * it measured 0.77 and Q fell 78x to the floor. A four per
                     * cent change in R flipped the loop between two opposite
                     * failures. That is a knife edge, not an estimator.
                     *
                     * So subtract R from both sides and compare what is left:
                     * HPH' as the filter predicts it against HPH' as the
                     * innovations imply it. When the implied value is negative
                     * the innovations carry no information about Q at all -
                     * they are saying R is too big - and the honest response is
                     * to HOLD Q where it is and say so, not to walk it into a
                     * rail. (The R error itself is real and still open; see the
                     * two-lag estimator above. It is not repaired here, but it
                     * no longer destroys Q while it is unrepaired.) */
                    /* AND SUBTRACT THE WHITE FLOOR, NOT R.
                     *
                     * R is the LAG-16 mean square on purpose (see its
                     * derivation above): it carries the detector's slow wander
                     * as well as its white noise, so that the gate and the gain
                     * treat the detector as worth what it is across the horizon
                     * the loop steers over. That is right for those two uses
                     * and wrong for this one. An innovation at a ONE-SECOND
                     * prediction horizon can only ever carry the white floor
                     * plus whatever the filter failed to track - about 6.4 ns^2
                     * on this board - so it can never reach the 8.45 that R
                     * reports, `Pobs` is negative in quiet GPS, and the
                     * adaptation is information-starved by construction rather
                     * than by accident. The hold added earlier made that
                     * survivable; it did not make it informative.
                     *
                     * Referenced to the lag-1 floor the arithmetic closes:
                     * E[y^2] = P00 + sigma_white^2, so `Pobs` estimates the
                     * true P00 and `Ppred` is the filter's own - the ratio is a
                     * covariance-consistency test that Q can actually move,
                     * because P00 is exactly what Q is the lever on. The gate
                     * and the gains keep the lag-16 R and every protection
                     * documented there.
                     *
                     * (This is GLM-5.3 Max's reading of the 02.09 four-hour
                     * capture, and it corrected mine: I had the two EMAs the
                     * wrong way round and called a documented design decision a
                     * 30% error in R. The lag names now say which is which.) */
                    double Ppred = S - R;                       /* HPH', predicted */
                    double Pobs  = s_kf_ms_innov - s_kf_ms_diff1;  /* HPH', implied */
                    bool   quiet = (s_kf_q_freeze == 0u) && (s_kf_rej_rate < 0.05);
                    if (Pobs > 0.0 && Ppred > 0.0 && quiet) {
                        /* MULTIPLICATIVE STOCHASTIC APPROXIMATION, NOT A RATCHET.
                         *
                         * The 1.02/0.98 deadband law had no fixed point at all,
                         * only two edges to chatter between, and it was wildly
                         * asymmetric in time: at ratio 1.25 it compounds to
                         * x2.7 per MINUTE, while coming back down needs the
                         * ratio to fall under 0.8, which cannot happen until P
                         * has already grown. A step proportional to the
                         * mismatch has its fixed point exactly at ratio 1, is
                         * symmetric, and is slow where it should be slow: the
                         * same ratio 1.25 now e-folds Q in about an hour rather
                         * than a minute, so a GPS episode cannot ratchet it.
                         *
                         * kappa matches the ms_innov EMA constant deliberately.
                         * Adapting faster than you observe is how the old law
                         * ended up on a knife edge where a four per cent change
                         * in R flipped it between opposite failures. The clip
                         * keeps the worst-case rate no higher than before. */
                        double ratio = Pobs / Ppred;
                        s_kf_q_ratio = ratio;
                        double step  = 0.001 * (ratio - 1.0);
                        if (step >  0.02) step =  0.02;
                        if (step < -0.02) step = -0.02;
                        s_kf_q_use *= (1.0 + step);
                        s_kf_q_hold = false;
                    } else {
                        s_kf_q_hold = true;
                    }
                    if (s_kf_q_use < ks.q_floor) s_kf_q_use = ks.q_floor;
                    /* The ceiling, derived in kf_scale(): the filter may not run
                     * more than twice as fast as its horizon. Without it this
                     * loop ratchets Q up on correlated detector noise and then
                     * steers on it - see the derivation there for the bench
                     * numbers and for why the downward side stays generous. */
                    /* THE CEILING, and it is the fix for a measured fault
                     * rather than a belt-and-braces limit.
                     *
                     * (R/Q)^(1/3) has the units of time and it IS the filter's
                     * own time constant, so "Q = R/T^3" is exactly the sentence
                     * *run as fast as the horizon you were given*. Above that
                     * the filter is faster than the control it feeds, and it
                     * spends the difference copying detector noise onto the
                     * oscillator.
                     *
                     * The adaptation below has one systematic bias that takes
                     * it there and leaves it there: when the detector's error
                     * is CORRELATED, the innovations exceed S for a reason that
                     * has nothing to do with the oscillator, so Q ratchets up
                     * at 1.02 per second until P and S have grown enough to
                     * explain them. It stops, but it stops high. Two boards,
                     * independently, went the whole way:
                     *
                     *   30.08, this board:  Q 1.238e-3, 195x its seed, the DAC
                     *   moving 1.80 LSB/s against algorithm 11's 0.49.
                     *
                     *   31.08, Dan Wiering's board, no parameters touched
                     *   beyond CT/LC/SAW: Q pinned against the OLD 1000x rail
                     *   at 4.468e-3 within 2h37m of boot and still there nine
                     *   hours later, the DAC moving 11.05 LSB/s and applying a
                     *   249 LSB span where the oscillator needed 19.8. Measured
                     *   against a rubidium standard, ADEV at 20 s was 8.6e-11
                     *   against 3.1e-12 for algorithm 11 on the same board and
                     *   the same reference - a hump peaking, as it must, at the
                     *   filter's own time constant.
                     *
                     * THE R HERE IS THE MEASURED ONE, NOT THE SEED, and that is
                     * the whole point of computing this at the point of use.
                     * The first version of this bound was 8 x q_seed, and it
                     * worked, but only by luck: q_seed comes from the a-priori
                     * 2.5-quantum guess, and how far a real detector sits above
                     * that is a property of the board. This one measures 2.5x
                     * its seed and Dan's 6.8x, so the same multiplier meant
                     * tau >= 92 s here and >= 95 s there - and would have meant
                     * >= 50 s on a board whose detector matched its seed. A
                     * bound whose meaning nobody can read off the code is not a
                     * bound. Taken from the R in use it says the same thing
                     * everywhere, and it follows a re-run of LC within a
                     * second.
                     *
                     * A faster loop is then asked for the honest way: shorten
                     * KT. The adaptation may still go as far DOWN as it likes.
                     *
                     * Measured over two plants, five noise seeds and three
                     * levels of detector wander: short-tau ADEV twice better
                     * (7.99e-12 against 1.54e-11 at tau 16), DAC motion 2.4x
                     * lower, against 7% on phase sd and 25% on ADEV at tau 1024
                     * - and the phase sd is measured against the detector,
                     * which is the instrument that lies here, while Dan's
                     * rubidium is not.
                     *
                     * AND IT MAKES KT MEAN WHAT IT SAYS, which cuts both ways.
                     * All of the above is at the default KT of 100 s. Replayed
                     * at KT 300 and KT 1000 this bound is what the loop runs
                     * into and stays against - phase sd 0.64 -> 2.82 ns at 300,
                     * 3.04 -> 47.6 ns at 1000, ADEV at tau 4096 2.7e-11 against
                     * 1.0e-12 - because Q_seed = R/KT^3 falls as the cube of the
                     * horizon while the oscillator's real walk does not move at
                     * all. Past a few hundred seconds the seed stops being an
                     * estimate of anything and the old 1000x rail was quietly
                     * doing the work of correcting it.
                     *
                     * That is not hidden. A loop asked for a horizon longer than
                     * its oscillator can hold sits against this ceiling for the
                     * whole run, and KL says "[at ceiling]" for exactly as long,
                     * which is the honest reading of "your KT is longer than
                     * this oscillator supports".
                     *
                     * THE REPAIR THIS COMMENT PROPOSED WAS THE WRONG ONE, and
                     * it is worth leaving the correction here rather than
                     * quietly rewriting it. The proposal was to seed Q from the
                     * oscillator by measuring its frequency walk on TIM2. TIM2
                     * cannot do it: an integer one-second count quantises at
                     * 29 ns/s and a hundred-second average at 2.9, while the
                     * walk this board actually shows is of order 1e-2 ns/s over
                     * the same window - several decades under the instrument.
                     * That is item 4 of doc/AUDIT_algo13_model_gaps.md and it
                     * was already closed as unmeasurable there.
                     *
                     * What the seed's dependence on KT actually broke was not
                     * the seed. It was the FLOOR, which was q_seed/1000 and so
                     * carried the same T^3: both rails moved together and KT
                     * slid a fixed window instead of widening it. That is fixed
                     * in kf_scale() - see the derivation of q_floor - and the
                     * adaptation was given a lower fixed point at the same time,
                     * below. The ceiling keeps its T and should. */
                    {
                        double Th = (double)((g_kf_horizon_s < 10u) ? 10u
                                                                    : g_kf_horizon_s);
                        /* IN FORCE ONLY WHILE THE LOOP IS TRACKING, and that
                         * proviso is not caution — without it this bound is at
                         * its tightest exactly when the filter needs room.
                         *
                         * R starts AT the seed (kf_reset seeds ms_diff with
                         * r_seed), and q_seed is r_seed/T^3, so on the first
                         * second R/T^3 IS q_seed: the ceiling lands on the seed
                         * and the adaptation has no upward room at all until R
                         * has been measured. Acquisition is where a wide Q earns
                         * its keep — a phase a thousand nanoseconds out needs a
                         * filter that can move — and the 01.09 17:12 capture
                         * shows what a strangled one costs: a bad picDIV arm
                         * landed the phase at -2124 ns, and the pull-in that
                         * followed threw away 70% of its readings at the gate
                         * and took 3300 s to settle against 900 s the run
                         * before. (The simulator does not reproduce that start,
                         * so how much of it was this and how much was the arm's
                         * own dice roll is not settled - but a ceiling that
                         * collapses onto the seed at boot is wrong whether or
                         * not it caused that particular morning.)
                         *
                         * So: the phase must be inside the acquisition band, and
                         * R must have had at least one EMA time constant of real
                         * measurement behind it. Until both hold, the adaptation
                         * runs against the wide sanity rail as before. The
                         * ratchet this bound exists to stop is a steady-state
                         * fault - it needs hours of quiet tracking to develop -
                         * so nothing is lost by standing back during pull-in. */
                        double axp = (ph_i < 0) ? -(double)ph_i : (double)ph_i;
                        bool tracking = (axp <= (double)g_ltic.acq_threshold_ns)
                                        && (s_kf_r_upd >= 500u);
                        double qm;
                        if (tracking) {
                            qm = R / (Th * Th * Th);
                            s_kf_q_max = qm;
                        } else {
                            /* Standing back is not the same as letting go. The
                             * wide sanity rail still applies, and it has to:
                             * with no upper clamp at all a 1.02-per-second
                             * ratchet reaches 1e18 in under two hours, which is
                             * exactly what a detector frozen outside the
                             * acquisition band produced when this was first
                             * written - the loop never reads "tracking", so the
                             * only bound left must be an unconditional one. */
                            qm = ks.q_seed * 1.0e+3;
                            s_kf_q_max = 0.0;   /* not in force: KL must not claim one */
                        }
                        if (s_kf_q_use > qm) s_kf_q_use = qm;
                        /* THE WATER MARKS ONLY COUNT WHERE THE CEILING DOES.
                         *
                         * They were added so that one KL at the end of an
                         * unattended night could answer "did Q ever pass the
                         * ceiling" and "did it descend during quiet". Spanning
                         * the whole run defeated both questions on their first
                         * night out: during pull-in `tracking` is false, the
                         * ceiling is not in force and the wide q_seed*1e3 rail
                         * applies instead, so the 03.09 night reported
                         * "5.022e-06 .. 4.982e-05" - a high mark six times the
                         * tracking ceiling, entirely legal, and unreadable as
                         * either an excursion or its absence. An offline replay
                         * of the settled hours put the real range at
                         * 6.4e-06 .. 1.2e-05.
                         *
                         * So the marks start at the first tracking second and
                         * describe the regime the pass conditions are about.
                         * They are never reset afterwards: a momentary loss of
                         * tracking is part of the night, not a new night. */
                        /* AND THE GATE IS `tracking`, WHICH THE FIRST VERSION
                         * OF THIS FIX LEFT OUT. It guarded on the "have we
                         * started yet" flag alone, sitting below the clamp
                         * rather than inside the tracking branch, so the flag
                         * was set on the first second of pull-in and the marks
                         * spanned the run exactly as before. The 03.09 10:08
                         * capture said so in one line - "Q while tracking:
                         * 6.353e-06 .. 2.955e-05" against a ceiling of
                         * 8.88e-06 - and it is the third time in two days that
                         * a rail or a regime was misreported by the very code
                         * added to report it. Hence the gate here and not
                         * above, and hence this note. */
                        if (tracking) {
                            if (!s_kf_q_wm_ok) {
                                s_kf_q_wm_ok = true;
                                s_kf_q_lo = s_kf_q_hi = s_kf_q_use;
                                /* AND THE INNOVATION EMA STARTS HERE TOO, for
                                 * the same reason the water marks do: it is a
                                 * tracking statistic and pull-in is not
                                 * tracking.
                                 *
                                 * An arm lands the phase a thousand nanoseconds
                                 * out, so the innovations during pull-in are of
                                 * that order and their SQUARES are a million
                                 * times the steady-state value. Reconstructed
                                 * from the 03.09 18:35 capture the EMA peaked
                                 * at 3.0e+05 and, 5100 s later and with the run
                                 * long since quiet, still read 1.6e+03 against a
                                 * true value of 9.1 - the 0.001 constant needs
                                 * about three hours to forget an acquisition.
                                 * The firmware's own KL said `ratio 31.55` on a
                                 * run whose last thousand seconds measure 1.7.
                                 *
                                 * That is not a display fault: the adaptation
                                 * ACTS on this ratio, so Q was being driven to
                                 * its ceiling for hours after every boot by
                                 * innovations that belonged to the pull-in. It
                                 * is the same shape as the water-mark bug and
                                 * it went unseen for longer because the number
                                 * it corrupts is plausible.
                                 *
                                 * Seeded to S rather than zeroed: S is what a
                                 * consistent filter expects y^2 to be, so the
                                 * adaptation starts at ratio 1 and moves only
                                 * on evidence gathered while tracking. */
                                s_kf_ms_innov = S;
                            }
                            if (s_kf_q_use < s_kf_q_lo) s_kf_q_lo = s_kf_q_use;
                            if (s_kf_q_use > s_kf_q_hi) s_kf_q_hi = s_kf_q_use;
                        }
                    }
                }
            }
        }
    }

    /* ---- update on TIM2, H = [0 1 0] ----
     *
     * Two SEQUENTIAL scalar updates, not one two-row matrix: taking the
     * measurements one at a time is algebraically identical when their noises
     * are independent, and it keeps the gain a single division. That is the
     * whole reason this filter fits on the part, and it is worth not throwing
     * away for tidiness.
     *
     * This one runs whether or not the detector said anything, which is the
     * point: it is the measurement that survives a dead picDIV, a lost lock and
     * a disconnected antenna's first minutes. */
    /* EXCEPT THE FIRST BOXCAR AFTER A RESET. avg100 is a hundred-second
     * average of whatever the counter saw, and at a cold start that window
     * still contains the power-on transient: the oscillator pulling to
     * frequency with the PWM frozen at its saved value. A fresh kf_reset()
     * seeds P11 wide open, so the first TIM2 update believes that stale
     * average almost wholesale (K1f ~ 0.9) and writes a frequency the board
     * no longer has. 03.09 20:11, one second after the boot arm: f jumped to
     * -29513 ps/s while TIM2's own 10 s said ~0, and the control spent the
     * next nine minutes un-saying it through the limiter. Arms do not
     * re-trigger this - they widen P00 and leave P11 alone - so the blank
     * only guards resets: boot, and a restart. Holdover is not weakened:
     * the arm gate and the trust test read z_f directly, not this update. */
    if (s_kf_f_blank > 0u) {
        s_kf_f_blank--;
    } else {
        /* R FOR THIS MEASUREMENT, AND THE FIRST-DIFFERENCE METHOD IS WRONG HERE.
         *
         * It works for the phase because consecutive phase readings are
         * independent. `avg100` is a BOXCAR: two consecutive ones share
         * ninety-nine of their hundred samples, so their difference is about a
         * hundredth of the single-sample noise and the estimator comes out two
         * orders of magnitude too small. The `Rf < 1.0` floor was quietly doing
         * all the work, and 1 (ns/s)^2 is itself about eight times too
         * optimistic: an integer one-second count quantises at +/-0.5 Hz, which
         * is 0.29 Hz RMS or 29 ns/s, and a hundred of those average to 2.9 ns/s
         * - call it 8.4 (ns/s)^2. So the filter believed a fifty-second-old
         * number about eight times more than it should.
         *
         * The honest construction is to measure the ONE-SECOND scatter (done
         * above, where the counter is read) and divide by the number of samples
         * the average in use contains. That needs no constant and follows the
         * board: a noisier PPS or a worse antenna shows up in it directly. */
        double Rf;
        {
            double n = s.have100 ? (double)KF_FWIN : (2.0 / 0.02 - 1.0);
            Rf = (100.0 * 100.0) * s_kf_ms_f1 / n;   /* Hz^2 -> (ns/s)^2, averaged */
            if (Rf < 0.25) Rf = 0.25;
        }
        /* Kept for the diagnostics and for the gate's sanity, but no longer the
         * source of Rf; see above. */
        if (s_kf_fprev_valid) {
            double d = z_f - s_kf_fprev;
            if (d < 0.0) d = -d;
            if (d < 1000.0) s_kf_ms_fdiff += 0.002 * (0.5 * d * d - s_kf_ms_fdiff);
        }
        s_kf_fprev       = z_f;
        s_kf_fprev_valid = true;

        /* AND IT IS NOT A MEASUREMENT OF THE FREQUENCY NOW. A boxcar over the
         * last hundred seconds describes the average over that window, so it
         * sits about fifty seconds behind - and whenever the loop is correcting,
         * fifty seconds is exactly when the frequency was different. loopsim.cpp
         * has said this about the simulator's plant model since 26.08 and the
         * filter was never told.
         *
         * The loop knows precisely how much frequency it commanded over that
         * window, because it books every correction into x1. If the change was
         * du over the window then the window's average sits about du/2 below
         * where x1 is now, so predict the measurement as x1 - du/2 rather than
         * as x1. It is a correction, not an inflation: the number is known, not
         * merely bounded. */
        double lag = 0.0;
        {
            double cum_old = (double)s_kf_du_ring[s_kf_du_i];
            lag = 0.5 * (s_kf_du_cum - cum_old);
        }
        double Sfz = s_kf_P[1][1] + Rf;
        double yf = z_f - (s_kf_x1 - lag);
        double limf = 4.0 * sqrt(Sfz);
        /* THIS GATE NEEDS A WAY OUT, and it is not symmetry with the phase gate
         * for its own sake — without one it latches shut and takes holdover
         * with it.
         *
         * Found in the simulator with the detector frozen at a constant reading
         * (LOOPSIM_STUCK). The trust test does its job and drops the detector,
         * and from there the counter is the only measurement left — but by then
         * the oscillator is a hertz out, yf is 100 ns/s, and limf is 4 ns/s
         * because P11 has nothing but Q to grow on. Every reading is refused,
         * x1 stays at zero, and the loop rides a model that says the frequency
         * is fine while the phase runs off at 100 ns/s. Holdover, the one thing
         * this measurement exists for, was exactly where it could not act.
         *
         * THE PHASE GATE'S ESCAPE WAS TRIED HERE FIRST AND MEASURED FAR WORSE:
         * widening P11 by yf^2 puts K1 at nearly one, so x1 SNAPS to a
         * measurement that is a 100 s boxcar and therefore fifty seconds
         * behind, and the loop chases its own lag. On the frozen-detector plant
         * that ended at +10.2 Hz with the DAC moving 7795 LSB/s, against
         * -1.04 Hz and 0.06 LSB/s for refusing outright. It works for the phase
         * because that measurement is instantaneous; this one is not.
         *
         * So: CLIP rather than open. After ten refusals in a row the filter's
         * belief, not the counter, is what is wrong — accept the reading, but
         * only 4 sigma of it, so the state walks toward the truth at a bounded
         * rate and can never snap to a lagged number. Frozen detector: -0.49 Hz
         * and 1.9 LSB/s, the best of the three, and every healthy case
         * (railed, 400 LSB cold start, clean run) is bit-for-bit unchanged. */
        bool tim2_ok = (yf <= limf && yf >= -limf);
        if (!tim2_ok && ++s_kf_frej_run >= 10u) {
            yf = (yf > 0.0) ? limf : -limf;
            tim2_ok = true;
        }
        if (tim2_ok) {
            s_kf_frej_run = 0;
            double K0 = s_kf_P[0][1] / Sfz;
            double K1 = s_kf_P[1][1] / Sfz;
            double K2 = s_kf_P[2][1] / Sfz;
            s_kf_x0 += K0 * yf; s_kf_x1 += K1 * yf; s_kf_x2 += K2 * yf;
            double A[3][3];
            A[0][0] = 1.0; A[0][1] = -K0; A[0][2] = 0.0;
            A[1][0] = 0.0; A[1][1] = 1.0 - K1; A[1][2] = 0.0;
            A[2][0] = 0.0; A[2][1] = -K2; A[2][2] = 1.0;
            double T1[3][3], P2[3][3];
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
                double v = 0.0; for (int k = 0; k < 3; k++) v += A[r][k] * s_kf_P[k][c];
                T1[r][c] = v;
            }
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
                double v = 0.0; for (int k = 0; k < 3; k++) v += T1[r][k] * A[c][k];
                P2[r][c] = v;
            }
            double KK[3] = { K0, K1, K2 };
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++)
                s_kf_P[r][c] = P2[r][c] + KK[r] * Rf * KK[c];
        }
    }

    /* ---- IS THE LOOP LOCKED? ----------------------------------------------
     *
     * Algorithm 13 is the only loop here that can say this properly, and until
     * now it did not say it at all. It emits no "LOCK" trend — its whole
     * vocabulary is KAL/REJ/NOPH/ARM/WAIT/NoCT/NoPL — so every consumer that
     * tested the trend string for "LOCK" got false forever, and the display
     * fell back to judging a Kalman loop by a ten-thousand-second frequency
     * average, which is precisely what that fallback exists to avoid.
     *
     * Three terms, and each one is doing work:
     *
     *   s_kf_holdover == 0   THE DETECTOR SPOKE THIS SECOND. Set to zero on
     *   every usable reading, including a rejected one — a REJ is the gate
     *   doing its job, not the loop losing its input. Without this term a
     *   filter flywheeling perfectly on its own model would read as locked
     *   indefinitely, which is the difference between steering and coasting.
     *
     *   |x0| <= band         THE ESTIMATED PHASE IS INSIDE THE BAND. The
     *   ESTIMATE, not this second's reading: that is the whole point of having
     *   a filter. A loop pulling in from 400 ns says KAL every second while it
     *   does so, so the trend alone would have called it locked from the first
     *   accepted sample.
     *
     *   sqrt(P[0][0]) <= band  AND IT KNOWS THAT. A phase estimate is worth
     *   what its variance says it is worth. This term is what makes the verdict
     *   honest at the two moments it matters most and costs nothing at any
     *   other: at a cold start P[0][0] is seeded at (range/2)^2, and after a
     *   picDIV arm it is deliberately reset to the same value because the zero
     *   the estimate referred to no longer exists. In both cases the filter is
     *   not locked and would otherwise have claimed to be within one second of
     *   the first reading that landed inside the band by luck.
     *
     * The band is g_ltic.acq_threshold_ns, which LC measures — the same
     * threshold this algorithm already uses for s_kf_ctl_fast, for the tracking
     * test and for the arm patience gate. Nothing is invented here and nothing
     * is fixed at build time; on a board whose detector resolves better, LC
     * returns a smaller number and this verdict tightens with it.
     *
     * No dwell, deliberately. The status bar applies its own (five samples up,
     * two down) and the digits live without one on every other algorithm; a
     * second copy here would have made the bar's dwell mean something different
     * on 13 than on 10, 11 and 12. */
    {
        double band = (double)g_ltic.acq_threshold_ns;
        double sig  = sqrt(s_kf_P[0][0] > 0.0 ? s_kf_P[0][0] : 0.0);
        s_kf_locked = (band > 0.0) && (s_kf_holdover == 0u) &&
                      (fabs(s_kf_x0) <= band) && (sig <= band);
    }

    /* ---- IS THE DETECTOR ACTUALLY THERE? ----------------------------------
     *
     * Two ways it can fail, and the first version of this handled only the
     * second — which is to say it handled the rarer one and reset itself on the
     * common one.
     *
     *   RAILED: the detector reports nothing valid at all. A picDIV that never
     *   synced sits with the ramp against a rail and stays there. This is
     *   algorithm 12's bridge, gate and hold-off and all: arm only once the
     *   frequency is close, because an arm lands the phase at a quantised
     *   offset of a few hundred nanoseconds and with the frequency still off
     *   the phase rails again within seconds — 87 arms in one hour on the 14.08
     *   log, each rolling the same dice. The TIM2 measurement above is what
     *   makes "close" knowable while the detector says nothing.
     *
     *   FROZEN: the detector reports a valid phase that never moves. The
     *   control below nulls the phase over the horizon, so a reading still
     *   beyond the ACQ window after several horizons is not a slow loop.
     *
     * The old code wrote `if (!have || ax <= thr) s_kf_far_s = 0;` — so the
     * railed case, the one that actually happened, CLEARED the counter and
     * nothing ever armed. */
    {
        double thr = (double)g_ltic.acq_threshold_ns;
        double ax  = (ph_i < 0) ? -(double)ph_i : (double)ph_i;
        /* KC, because the comment above says "the control below nulls the
         * phase over the horizon" and that is the CONTROLLER's horizon since
         * build 36. At the default split this is 5*33 = 165 s, so the 300 s
         * floor takes over and the wait drops from 500 s to 300 - the right
         * direction for a loop that now nulls three times faster. Measured
         * inert on every frozen-detector case in the harness; changed on the
         * meaning, not on a number. */
        uint32_t patience = 5u * (uint32_t)kf_ctl_horizon();
        if (patience < 300u) patience = 300u;
        /* WHERE AN ARM LANDS, MEASURED, AND WHY THE OLD GATE COULD NOT WORK.
         *
         * Arming re-syncs the divider to the next 1PPS edge and the phase lands
         * wherever that puts it. The old gate assumed it landed near the middle
         * and asked only that the frequency error not carry the phase across
         * half the band during the 60 s hold-off:
         *
         *     arm_hz = range / (2 * 100 * 60)      = 0.25 Hz for a 3000 ns band
         *
         * It does not land near the middle. Seven arms across the three captures
         * of 01.09, phase read in the second after each:
         *
         *     -1554  -1431  -899   (21:00)
         *     -1641  -1441  -943   (17:12)
         *     -927                 (10:49 - the one run that then worked)
         *
         * Every one negative, none near zero, -900 to -1650 ns against a band of
         * +/-1500. The landing offset alone eats most of the budget the old gate
         * spent entirely on frequency drift, and the arms of the 21:00 run duly
         * passed it at +0.24 and +0.17 Hz - 24 and 17 ns of phase per second,
         * enough to walk a -1450 landing off the ramp in seconds. It railed
         * within seconds, three times, and that run never acquired at all. The
         * 10:49 run drew the shallowest landing of the seven, and that is the
         * whole difference between a working night and a dead one.
         *
         * So ask the question that actually matters: GIVEN WHERE THIS DIVIDER
         * LANDS, will the phase still be readable one horizon from now? Both
         * terms are measured - the landing from the last arm, the drift from
         * TIM2 - and neither is a constant anybody has to guess. It also stops
         * refusing a LARGE frequency error that happens to push the phase back
         * toward the middle, which the old symmetric window forbade for no
         * reason at all.
         *
         * Until the first landing has been seen there is nothing to reason from,
         * so the old window applies - tightened, because half the band was
         * always the wrong budget. */
        double half_ns = 0.5 * (double)g_ltic.range_ns;
        double drift   = -100.0 * f_meas;            /* ns/s the phase will move */
        /* AND THIS ONE STAYS ON KT, DELIBERATELY. It asks how far the drift
         * carries the landing before the loop has authority over it, which is
         * not the same question as how fast a known phase is nulled - and the
         * blind escape below is a safety timer where the longer horizon is the
         * conservative value. Using KC would project a shorter reach and admit
         * MORE arms; after the week this arm gate has had, the conservative
         * side is the one to be on without a measurement demanding otherwise,
         * and the harness shows none (STUCK=300: 16 arms either way). */
        double horiz   = (double)((g_kf_horizon_s < 10u) ? 10u : g_kf_horizon_s);
        bool freq_close;
        if (s_kf_arm_land_ok) {
            double endp = s_kf_arm_land + drift * horiz;
            freq_close = (s_kf_arm_land < half_ns && s_kf_arm_land > -half_ns)
                      && (endp < half_ns * 0.95 && endp > -half_ns * 0.95);
        } else {
            freq_close = (f_meas > -ks.arm_hz && f_meas < ks.arm_hz);
        }
        /* AND AN ESCAPE, because a gate that can refuse forever is a gate that
         * will. If the detector has said nothing for ten horizons the loop is
         * getting no phase either way, and a lottery ticket beats no ticket. */
        if (s_kf_blind > 10u * (uint32_t)horiz) freq_close = true;

        bool want_arm = false;

        /* The landing itself: the first readable phase after an arm. That is the
         * number the gate above needs and nothing else in the loop records it. */
        if (s_kf_arm_wait && raw) {
            s_kf_arm_wait = false;
            if (!s_kf_arm_land_ok) { s_kf_arm_land = (double)ph_i; s_kf_arm_land_ok = true; }
            else s_kf_arm_land += 0.35 * ((double)ph_i - s_kf_arm_land);
        }
        if (raw) s_kf_blind = 0;
        else if (s_kf_blind < 0xFFFFFFFFu) s_kf_blind++;

        if (s_kf_arm_hold > 0u) {
            s_kf_arm_hold--;
            s_kf_railed = 0; s_kf_far_s = 0;
        } else if (!have) {
            s_kf_far_s = 0;
            /* `!have` IS TWO DIFFERENT THINGS AND ONLY ONE OF THEM WANTS AN ARM.
             *
             * have = raw && trust. A RAILED detector (raw false) says nothing at
             * all, and re-syncing the divider is the only repair there is. A
             * DISTRUSTED one (raw true, trust false) is still reporting a phase,
             * and if that phase is inside the band then arming does not repair
             * anything - it throws away a good reading and lands somewhere
             * between -900 and -1650 ns, which is most of the band gone.
             *
             * That is not hypothetical. 02.09 10:59, t+593: the detector read
             * -53 ns, Vphase 2.041 V, entirely healthy, but the trust test had
             * dropped it eight seconds earlier and the loop was in HOLD. This
             * branch counted its five seconds and armed. The phase went to
             * -1222 ns and the run took another seven hundred seconds to come
             * back - three arms and 1317 s to settle, against one arm and 309 s
             * the night before.
             *
             * So arm on `!have` only when there is nothing to lose: no valid
             * reading at all, or a valid one that is already outside the
             * acquisition band (the FROZEN case, where re-syncing is the repair
             * the comment above describes). A trusted-looking phase sitting in
             * the middle of the ramp is left alone; the trust test has its own
             * expiry and its own bridge for a detector that really is dead.
             *
             * Measured before shipping, against the same tree with this one
             * condition removed: eight railed cold starts, LOOPSIM_RAIL=1,
             * steady state, DNOISE=12/DTAU=300, PH0=-1300 and F0=200 all come
             * out BIT-IDENTICAL - the arm path they use is the railed one, and
             * this condition never touches it. A detector frozen OUTSIDE the
             * band (LOOPSIM_STUCK=300, LAT=200) still arms fourteen times, as
             * it must. The only case that moves is a detector frozen INSIDE it,
             * STUCK=120: fourteen arms become none, phase sd 99036 -> 86497 ns.
             * That case is unrecoverable either way - the sim's detector stays
             * frozen through the arm - so the sd is not the point; the point is
             * that the loop no longer spends the band on a re-sync that cannot
             * return anything it did not already have. */
            bool nothing_to_lose = (!raw) || (ax > thr);
            if (nothing_to_lose && freq_close) {
                if (++s_kf_railed >= 5u) want_arm = true;
            } else {
                s_kf_railed = 0;
            }
        } else {
            s_kf_railed = 0;
            if (ax <= thr) {
                s_kf_far_s = 0;
                s_kf_start_arm = false;   /* centred: nothing to re-reference */
            } else if (s_kf_start_arm) {
                /* START FASTER THAN PATIENCE. The patience below is for a
                 * loop that WALKED out and may come back; a (re)start that
                 * finds the phase far out inherited the reference from
                 * whoever ran before (the 28.08 10->11 switch: +1272 ns),
                 * and waiting 5 horizons to re-sync it only stretches the
                 * nulling the arm makes unnecessary. One shot, same
                 * frequency gate as the railed path. */
                if (freq_close) want_arm = true;
            } else if (++s_kf_far_s >= patience) want_arm = true;
        }

        if (want_arm) {
            if (s_kf_arms < 3u)
                OUT_SERIAL.println("KAL: detector not following - re-arming picDIV");
            ltic_arm_picdiv();
            s_kf_arm_wait = true;      /* catch where it lands */
            s_kf_ph_blank = 4u;        /* ...and not before it has landed */
            /* Back off once arming has plainly not helped: three tries is a
             * fault the divider cannot fix, and re-arming every minute for the
             * rest of the run only fills the console. */
            s_kf_arms++;
            s_kf_start_arm = false;
            s_kf_arm_hold = (s_kf_arms <= 3u) ? 60u : 600u;
            /* And the Q adaptation stands down for ten minutes either way. An
             * arm moves the reference; the innovations that follow describe the
             * landing, not the oscillator, and the ms_innov EMA needs about
             * this long to stop carrying them. Same philosophy as the `moving`
             * guard on the R estimator. */
            s_kf_q_freeze = 600u;
            s_kf_railed = 0; s_kf_far_s = 0;
            /* The window starts again and the count of failed windows with
             * it, but TRUST IS NOT HANDED BACK HERE. Doing that re-admitted a
             * detector already proven dead: the frozen +1295 ns was believed
             * for another ninety seconds after every arm, and ninety seconds of
             * a clamped 500 LSB correction is 0.16 Hz. Simulated, that limit
             * cycle left the oscillator 1.35 Hz out where leaving it blind
             * leaves it at 0.01. The test re-establishes trust by itself, from
             * the first window in which the phase moves as TIM2 says it should
             * — it runs on the raw reading and does not care what the loop
             * currently believes. */
            /* NOT s_kf_quiet: an arm is not evidence about whether the
             * detector follows, and clearing the clock here re-created the
             * deadlock it exists to break — the back-off arms every 600 s, the
             * expiry needs 1800, so the counter never got there. */
            s_kf_w_n = 0; s_kf_dead_run = 0;
            /* A detector that said NOTHING has not been caught lying, and
             * arming is exactly the repair for it — so it starts clean. One
             * that was reading and not following has, and re-arming does not
             * earn that back: it gets in again through the test above, or
             * through the expiry, and not before. */
            if (!raw) s_kf_trust = true;
            /* WIDEN THE PHASE BELIEF, DO NOT RESET THE FILTER. Arming re-syncs
             * the divider, so everything the filter believed about PHASE is now
             * about a zero that no longer exists — but nothing it learned about
             * FREQUENCY or AGING came through the divider, and those are the
             * expensive ones to relearn. Algorithm 12 has to throw its whole
             * accumulator away here; this is what carrying a covariance buys.
             * The earlier version set s_kf_init = false and discarded the lot. */
            s_kf_x0 = 0.0;
            s_kf_P[0][0] = ks.p0;         /* half the band again: the divider
                                           * just moved the zero and the phase
                                           * could be anywhere it can show */
            s_kf_P[0][1] = s_kf_P[1][0] = 0.0;
            s_kf_P[0][2] = s_kf_P[2][0] = 0.0;
            s_kf_ph_hist_n = 0; s_kf_ph_hist_i = 0;  /* new zero: differences
                                                      * across an arm are garbage */
            /* An arm moves the reference, so whatever was decided above this
             * second described a zero that no longer exists. The widened
             * P[0][0] would clear the verdict on the NEXT second anyway; saying
             * it here means the second of the arm itself is never counted. */
            s_kf_locked = false;
            set_trend("ARM ");
            return pwm;
        }
    }

    /* ---- control: cancel the estimated frequency error and null the phase
     * over the horizon. Both terms come from the ESTIMATE, not from this
     * second's reading, which is the whole point of having a filter. ---- */
    /* KC, not KT - see the derivation at g_kf_ctl_s. The frequency term is the
     * estimator's own output and needs no horizon; the phase term is the only
     * place a controller time constant belongs. */
    double T = kf_ctl_horizon();
    double u = -(s_kf_x1 + s_kf_x0 / T);          /* wanted change, ns/s */
    double polarity = (g_ltic.polarity == -1) ? -1.0 : 1.0;
    double du = polarity * u * lsb_per_ns;

    /* Clamp against the detector band, as algo 12 does: a correction is only
     * useful while the phase stays measurable. */
    double lim_lsb = ks.lim_lsb;
    if (du >  lim_lsb) du =  lim_lsb;
    if (du < -lim_lsb) du = -lim_lsb;

    /* ---- WHAT ACTUALLY REACHED THE PIN ------------------------------------
     *
     * The clamp was accounted for and the ROUNDING was not, and that is the
     * whole of the standing phase offset in the 27.08 run: +18.7 ns held for
     * thirty-eight minutes with the PWM motionless and the filter reporting a
     * slew of -0.19 ns/s it was not applying.
     *
     * The mechanism. Once the phase is home the corrections are a fraction of
     * an LSB. Booking `du` into the frequency state tells the filter it is
     * already slewing at -x0/T; next second the control computes
     * -(x1 + x0/T) = 0 and asks for nothing. If that fraction never reached the
     * pin, the filter is now certain it is fixing a phase error that nothing is
     * fixing, and the loop parks — at any offset, forever, because the state
     * that would notice is written by the control rather than estimated from
     * the data. Simulated on a 16-bit board: -5.97 ns standing on the 26.08
     * plant against -0.00 with the sub-LSB path, and +1.89 against +0.21 on a
     * quiet one.
     *
     * The fix is not to round more carefully. It is to book the DIFFERENCE
     * BETWEEN THE DAC VALUES — which covers the clamp, the rounding and the
     * fine path together and cannot be fooled by a future output stage either —
     * and to carry the remainder into the next second so a sub-LSB request is
     * delayed rather than discarded. That carry is a one-second sigma-delta: on
     * a board without the fine path the loop now asks again until a whole count
     * moves, instead of asking once and being told it succeeded.
     *
     * Same lesson as algorithm 12's zero-crossing, one layer lower down: a
     * state updated with an unapplied control is a state that lies. */
    {
        double base = fine_base(pwm);

        /* Whatever the output stage could not take last second, try again. */
        du += s_kf_carry;
        if (du >  lim_lsb) du =  lim_lsb;
        if (du < -lim_lsb) du = -lim_lsb;

        /* ---- AND NOT ALL AT ONCE ------------------------------------------
         *
         * A clamp bounds how far the correction may go; this bounds how fast it
         * may CHANGE, and they are different faults. lim_lsb is the whole
         * detector band and never binds on a settled loop, so a single bad
         * second passed through it untouched: traced on the 03.09 plant, the
         * filter's first frequency measurement after a reset is an EMA of the
         * WHOLE-HERTZ one-second counter, the wide prior lets the state move
         * 61% of the way to it, and the control put 119 LSB on the pin in one
         * second on a plant that was settled. The next second it took most of
         * it back. Four seconds of that, one second after a restart, is the
         * +143 LSB the 11.09 bench capture shows at the moment algorithm 11
         * handed over to 13 - and on a phase analyser each one is a spike.
         *
         * The rate is the band spread over one control horizon: the largest
         * correction this loop is ever allowed to want, delivered no faster
         * than the timescale it steers on. Nothing about that is tuned - both
         * numbers are the loop's own - and on a settled board du changes by far
         * less than an LSB per second, so it never binds. What it removes is
         * exactly the transient: the correction still arrives, as a ramp the
         * filter can converge underneath instead of a step it has to undo.
         *
         * The remainder is NOT discarded. It goes into the same carry the
         * sub-LSB path uses, so a rate-limited second is delayed rather than
         * lost and the loop cannot be made to under-correct by this. */
        {
            double rate = lim_lsb / T;
            if (rate < 1.0) rate = 1.0;       /* never slower than one LSB/s */
            double dd = du - s_kf_du_prev;
            if (dd >  rate) du = s_kf_du_prev + rate;
            if (dd < -rate) du = s_kf_du_prev - rate;
        }
        s_kf_du_prev = du;

        double target = base + du;
        double out;
        if (gpsdo_dac_fine_available()) {
            g_vctl_fine       = target;
            g_vctl_fine_valid = true;
            out               = target;          /* the pin takes the fraction */
        } else {
            out = (double)(int32_t)(target + (target < 0.0 ? -0.5 : 0.5));
        }
        uint16_t pin = clamp_pwm((int32_t)(out + (out < 0.0 ? -0.5 : 0.5)));
        if (!gpsdo_dac_fine_available()) out = (double)pin;
        else if ((double)pin - out > 1.0 || out - (double)pin > 1.0)
            out = (double)pin;                   /* clamp_pwm bit: believe the pin */

        double applied = out - base;
        s_kf_carry = du - applied;
        /* Never let the carry grow without bound: if the output is stuck at a
         * rail, asking harder is not the answer and a saved-up correction that
         * fires all at once later would be worse than none. */
        if (s_kf_carry >  1.0) s_kf_carry =  1.0;
        if (s_kf_carry < -1.0) s_kf_carry = -1.0;

        /* BOOK THE CORRECTION - AND DO NOT PRETEND IT WAS EXACT.
         *
         * Telling the filter what was applied is right: the frequency really did
         * move and the phase measurement should not have to discover it. But
         * `lsb_per_ns` comes from CT, and CT is a measurement like any other. If
         * it is a few per cent out, every correction is mis-booked by a few per
         * cent, the errors accumulate in x1, and NOTHING EVER TAKES THEM BACK -
         * the filter was handed the number as fact, with no covariance attached.
         *
         * That bias has a stable home. The control is u = -(x1 + x0/T), so any
         * pair with x1 = -x0/T commands exactly nothing: the loop parks at a
         * constant phase offset, the innovations go to zero because x0 tracks
         * the measurement perfectly well there, and no measurement disagrees
         * with anything. It is a fixed point, and the loop sits in it. Seen in
         * the simulator parked at -100 ns for 1500 s with x1 = +1.0 ns/s -
         * an error of 0.01 Hz, which is exactly TIM2's resolution, so the second
         * measurement cannot see it either. Seen on hardware as the 01.09 run
         * that took 3600 s to bring 21 ns down to 7.5 ns while its own state
         * reported "already nulling" (TODO 80), and as every standing offset
         * this loop has ever shown.
         *
         * The repair is one line and it is what the textbook says: an uncertain
         * input contributes its uncertainty to the covariance. Five per cent of
         * what was applied, squared, into P11 - so the filter keeps enough doubt
         * about its own frequency for the phase measurement to pull it back. In
         * steady state `applied` is a fraction of an LSB and this is invisible;
         * during a pull-in it is what lets the loop discover that its own
         * bookkeeping was wrong.
         *
         * Measured over sixteen railed cold starts (four frequency offsets, four
         * seeds): mean time to settle 2316 -> 1802 s, worst case 7127 -> 4615 s,
         * and the locked-loop figures unchanged to two decimal places. */
        {
            double du_ns = (polarity * applied) / lsb_per_ns;
            s_kf_x1 += du_ns;
            s_kf_P[1][1] += (0.05 * du_ns) * (0.05 * du_ns);
            /* ...and remember it, so the gated frequency measurement can be
             * predicted at the epoch it actually describes (see the TIM2 lag). */
            s_kf_du_cum += du_ns;
            s_kf_du_ring[s_kf_du_i] = (float)s_kf_du_cum;
            if (++s_kf_du_i >= KF_FWIN) s_kf_du_i = 0;
        }
        return pin;
    }
}
#endif /* GPSDO_LTIC */

uint16_t adjustVctlPWM(uint16_t prev_pwm, uint32_t ppscount, uint8_t algo_no)
{
    /* Invalidate first, so a fractional target can only ever come from THIS
     * cycle. An algorithm that holds, or one that does no fractional
     * arithmetic, simply never sets it and the control task writes 16 bits as
     * it always did. This is the one line that makes the fine path safe to add
     * to some algorithms and not others. */
    g_vctl_fine_valid = false;

    /* ALGORITHM CHANGED? Tell the incoming loop to start from a defined state.
     * Here rather than in the LA handler because this is the one path every
     * change goes through — the CLI, a settings recall at boot, and anything
     * else that ever writes gCtrl.active_algo. A hook on the command would have
     * missed the recall, which is exactly the case nobody tests. */
    {
        static uint8_t s_prev_algo = 0xFF;
        if (algo_no != s_prev_algo) {
            s_prev_algo = algo_no;
            /* Restart flag AND the lock verdicts, together — see the note at
             * algo_request_restart(). Here rather than in each algorithm's
             * restart path because this is the one place every change goes
             * through, the CLI and a settings recall at boot included. */
            algo_request_restart();
        }
    }

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
#ifdef GPSDO_LTIC
        case 10: return ltic_three_stage  (prev_pwm, ppscount);
        case 11: return ltic_lars_pi      (prev_pwm, ppscount);
        /* 12 belongs inside the guard with 10 and 11: it works on the detector's
         * phase and there is no fallback, so without GPSDO_LTIC there is nothing
         * for it to do. LA 12 refuses at the CLI for the same reason. */
        case 12: return multi_level_accum (prev_pwm, ppscount);
        /* 13 belongs in the guard for the same reason: it estimates PHASE. */
        case 13: return kalman_ctl        (prev_pwm, ppscount);
#endif
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
    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) {
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
    u = lrn_apply(u, integral_e, e, Ts);   /* self-learning (LRN) */

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

    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) { phase_acc = 0.0; }
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
    u = lrn_apply(u, phase_acc, e_freq, Ts);   /* self-learning (LRN) */

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

    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) { phase_acc = 0.0; }
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
    u = lrn_apply(u, phase_acc, e_freq, Ts);   /* self-learning (LRN) */

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

    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) {
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
    u = lrn_apply(u, integral_e, e, Ts);   /* self-learning (LRN) */

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

    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) { phase_acc = 0.0; }
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
    u = lrn_apply(u, phase_acc, e_freq, Ts);   /* self-learning (LRN) */

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

    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) {
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
    u = lrn_apply(u, pll_phase, e_hz, Ts);   /* self-learning (LRN) */

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
 * ALGORITHM 9 — Neural network MLP with ONLINE THERMAL LEARNING
 *
 * Architecture: 5 inputs → 8 hidden neurons (tanh) → 1 output (tanh)
 *
 * Inputs (normalised to ±1.0):
 *   x[0] = e         / E_SCALE     (frequency error,  E_SCALE=0.5 Hz)
 *   x[1] = integral  / I_SCALE     (integral of error, I_SCALE=500 Hz·s)
 *   x[2] = derivative/ D_SCALE     (rate of change,   D_SCALE=0.05 Hz/s)
 *   x[3] = ΔT        / T_SCALE     (temp deviation from 1 h baseline, 2 °C)
 *   x[4] = dT/dt     / DT_SCALE    (temp rate, 0.005 °C/s)
 *
 * Output (normalised):
 *   delta_PWM = y_norm × MAX_STEP  (MAX_STEP from CT)
 *
 * The PID channels (h0-h2) are analytically constructed as before: a
 * diagonal, bias-free, odd-symmetric network implementing a smooth
 * saturating PID (equilibrium exactly at zero error by design).
 *
 * NEW — THERMAL REGRESSOR + HOLDOVER STEERING. Closed-loop simulation
 * showed that learning thermal weights from the instantaneous error is
 * futile: the loop hides the correlation (the integral tracks slow drift,
 * and what remains is counter-quantisation dither). The signal with real
 * SNR is the CORRECTION the loop was forced to apply: overnight logs show
 * PWM tracking room temperature by tens of LSB. So the network learns the
 * oscillator tempco by an EMA covariance REGRESSION of PWM against
 * temperature (horizon ~4 h, only while disciplined):
 *
 *     tempco [LSB/°C] = cov(T, PWM) / var(T),  clamped ±60
 *
 * (validated in simulation: measured −26.8 vs true −26.7 LSB/°C).
 * Payoff: during HOLDOVER the loop normally freezes PWM and thermal drift
 * runs unchecked (3 °C × typical VCXO tempco ≈ tens of mHz ≈ µs of phase
 * per hour). With the learned tempco the firmware keeps steering PWM from
 * temperature alone — holdover becomes thermally compensated. Temperature
 * comes from the BMP280 (AHT fallback); no sensor → everything neutral.
 * The thermal inputs x[3]/x[4] feed inert hidden channels (W2=0) kept for
 * future offline training. Tempco is reported in the Learn: line.
 * ====================================================================== */

/* tanh approximation — saves linking libm on some toolchains, but since
   we already include math.h for exp() in algo 8, use real tanh here. */
static inline double nn_tanh(double x) { return tanh(x); }

/* Network dimensions */
#define NN_IN   5
#define NN_H    8
#define NN_OUT  1

/* Learned oscillator tempco [LSB/°C] — from the PWM↔temperature regressor
 * below; 0 until enough variance is seen. Exposed for telemetry. */
float g_nn_tempco = 0.0f;
/* |tempco| clamp [LSB/degC]. File scope because algo_rescale_lsb() has to
 * honour the same bound when a span move rescales the learned value. */
#define NN_TCO_MAX  60.0

/* ACQ centring drive (algo 10), tunable live with `ACG <gain> [cap]`.
 * gain: LSB of PWM per volt of centring error. cap: max step per update.
 * Defaults are ~4x gentler than the old HC74-era 6000x/±400 pairing, which
 * wrapped a sensitive LVC74 detector rail-to-rail instead of centring it. */
float g_ltic_acq_centre_gain = 2500.0f;
float g_ltic_acq_centre_cap  = 150.0f;

/* Module-level thermal state, shared by the PPS-tick observer (learning)
 * and the holdover steering path (which runs even with no PPS). */
static double s_th_fast = 0.0, s_th_slow = 0.0, s_th_fast_prev = 0.0;
static bool   s_th_valid = false;
static double s_th_mT = 0.0, s_th_mP = 0.0;      /* EMA means (T, PWM)   */
static double s_th_cov = 0.0, s_th_var = 0.0;    /* EMA cov / var        */
static double s_ho_tprev = 0.0;                  /* holdover: last T     */
static bool   s_ho_track = false;
static double s_ho_frac  = 0.0;                  /* fractional LSB accum */
static double s_ho_total = 0.0;                  /* total excursion clamp*/

/* Read the board temperature with a plausibility window (BMP → AHT). */
static bool nn_read_temp(double *out)
{
    float tc = g_bmp_temp;
    if (!(tc > -40.0f && tc < 85.0f)) tc = g_aht_temp;
    if (!(tc > -40.0f && tc < 85.0f)) return false;
    *out = (double)tc;
    return true;
}

/* Advance the thermal EMAs by dt seconds (called from both paths). */
static void nn_thermal_track(double dt)
{
    double tc;
    if (!nn_read_temp(&tc)) return;
    if (!s_th_valid) {
        s_th_fast = s_th_slow = s_th_fast_prev = tc;
        s_th_valid = true;
        return;
    }
    s_th_fast_prev = s_th_fast;
    s_th_fast += (dt / 120.0)  * (tc - s_th_fast);   /* ~2 min  */
    s_th_slow += (dt / 3600.0) * (tc - s_th_slow);   /* ~1 hour */
}

static const double W1[NN_H][NN_IN] = {
    /*  e,       integral,  derivative, dT,   dT/dt */
    {  1.5,      0.0,       0.0,        0.0,  0.0 },   /* h0: P channel      */
    {  0.0,      1.0,       0.0,        0.0,  0.0 },   /* h1: I channel      */
    {  0.0,      0.0,       1.2,        0.0,  0.0 },   /* h2: D channel      */
    {  0.0,      0.0,       0.0,        1.0,  0.0 },   /* h3: thermal level  */
    {  0.0,      0.0,       0.0,        0.0,  1.0 },   /* h4: thermal rate   */
    {  0.0,      0.0,       0.0,        0.0,  0.0 },   /* h5: unused         */
    {  0.0,      0.0,       0.0,        0.0,  0.0 },   /* h6: unused         */
    {  0.0,      0.0,       0.0,        0.0,  0.0 }    /* h7: unused         */
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
    const double T_SCALE   = 2.0;     /* °C  — ΔT from baseline   */
    const double DT_SCALE  = 0.005;   /* °C/s — thermal rate      */
    const double MAX_STEP  = g_nn_max_step;  /* LSB — output de-normalisation */
    const double I_LIMIT   = g_pid[9].I_LIMIT; /* anti-windup = normalisation bound */
    const double REG_ALPHA = 10.0 / 14400.0;  /* regressor EMA horizon ~4 h */
    const double TCO_MAX   = NN_TCO_MAX;      /* |tempco| clamp [LSB/°C]    */

    const uint32_t PERIOD = 10;

    /* A fresh entry into this algorithm is the same event as a ring-buffer
     * flush: the integrator holds seconds of history from conditions that may
     * be hours old, and applying it as a correction the moment the operator
     * switches back is a step nobody asked for. algo_take_restart() is called
     * FIRST so the flag is always consumed here, whatever the flush test says. */
    if (algo_take_restart() || ppscount < last_flush_pps) {
        integral_e = 0.0; prev_e = 0.0; prev_valid = false;
        s_th_valid = false;   /* re-seed thermal EMAs after a flush */
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

    /* Thermal tracking + TEMPCO REGRESSOR (the learning). Statistics are
     * only accumulated here — i.e. while the loop is disciplined and this
     * algorithm is issuing corrections — so warm-up, calibration and
     * holdover excursions never poison the regression. */
    double dT = 0.0, dTdt = 0.0;
    nn_thermal_track(Ts);
    s_ho_track = false;                 /* discipline active → reset HO ref */
    if (s_th_valid) {
        dT   = s_th_fast - s_th_slow;
        dTdt = (s_th_fast - s_th_fast_prev) / Ts;
        /* EMA covariance regression of PWM against temperature */
        s_th_mT  += REG_ALPHA * (s_th_fast   - s_th_mT);
        s_th_mP  += REG_ALPHA * ((double)pwm - s_th_mP);
        s_th_cov += REG_ALPHA * ((s_th_fast - s_th_mT) * ((double)pwm - s_th_mP) - s_th_cov);
        s_th_var += REG_ALPHA * ((s_th_fast - s_th_mT) * (s_th_fast - s_th_mT) - s_th_var);
        if (s_th_var > 1.0e-4) {        /* need real temperature variance  */
            double sl = s_th_cov / s_th_var;
            if (sl >  TCO_MAX) sl =  TCO_MAX;
            if (sl < -TCO_MAX) sl = -TCO_MAX;
            g_nn_tempco = (float)sl;
        }
    }

    /* Normalise inputs */
    double x[NN_IN];
    x[0] = e          / E_SCALE;
    x[1] = integral_e / I_SCALE;
    x[2] = derivative / D_SCALE;
    x[3] = dT         / T_SCALE;
    x[4] = dTdt       / DT_SCALE;
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

    /* Forward pass: output layer. The thermal hidden channels h3/h4 are
     * inert (W2 = 0) — kept so a future offline-trained W2 can use them.
     * The learned thermal action lives in nn_thermal_holdover_step(). */
    double y = b2[0];
    for (int j = 0; j < NN_H; j++) y += W2[0][j] * h[j];
    y = nn_tanh(y);  /* output in (-1, 1) */

    /* De-normalise: delta_PWM in LSB */
    /* Sign convention: network output y>0 means "lower frequency needed"
       = decrease PWM */
    double delta_pwm = -y * MAX_STEP;
    /* Self-learning (LRN): e is passed as the FREQUENCY error, so the drift
     * feed-forward learner is fully active on this algorithm (same signal as
     * algo 7). phase_hz_s=0 because the NN keeps no phase accumulator — the
     * damping OBSERVER is therefore blind here (no zero-crossings to watch),
     * but a previously learned damping value still trims the step. */
    delta_pwm = lrn_apply(delta_pwm, 0.0, e, (double)PERIOD);

    char trend[5];
    strcpy(trend, delta_pwm >= 0.0 ? "NN+ " : "NN- ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)delta_pwm);
}

/* ======================================================================
 * nn_thermal_holdover_step — thermally steer PWM during HOLDOVER (algo 9)
 *
 * Called from vControlTask on its 5 Hz path (NOT gated on PPS — with the
 * antenna gone there is no PPS at all). Internally rate-limited to ~1 Hz.
 * Uses the tempco learned while disciplined:  Δpwm = tempco · ΔT, with a
 * fractional accumulator (steps are usually << 1 LSB) and a total
 * excursion clamp of ±250 LSB as a safety net. Returns the integer PWM
 * delta to apply now (usually 0), or 0 when idle/no data.
 * ====================================================================== */
int16_t nn_thermal_holdover_step(void)
{
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if ((uint32_t)(now - last_ms) < 1000u) return 0;
    double dt = (last_ms == 0) ? 1.0 : (double)(now - last_ms) / 1000.0;
    last_ms = now;

    if (g_nn_tempco == 0.0f) return 0;        /* nothing learned yet     */
    nn_thermal_track(dt);
    if (!s_th_valid) return 0;

    if (!s_ho_track) {                        /* first HO tick: set ref  */
        s_ho_tprev = s_th_fast;
        s_ho_track = true;
        s_ho_frac  = 0.0;
        s_ho_total = 0.0;
        return 0;
    }

    double step = (double)g_nn_tempco * (s_th_fast - s_ho_tprev);
    s_ho_tprev = s_th_fast;

    /* total excursion clamp (protects against a bad tempco estimate)    */
    if (s_ho_total + step >  250.0) step =  250.0 - s_ho_total;
    if (s_ho_total + step < -250.0) step = -250.0 - s_ho_total;

    s_ho_frac  += step;
    s_ho_total += step;
    int16_t out = (int16_t)s_ho_frac;         /* emit whole LSBs only    */
    s_ho_frac  -= (double)out;
    return out;
}

/* ======================================================================
 * algo_rescale_lsb — learned LSB-denominated state across a span move
 *
 * r = K_old / K_new: the same physical correction is r times as many codes in
 * the new span. Two things are learned in codes and survive a loop restart, so
 * a restart alone would carry them across in the wrong units:
 *
 *   g_lrn_drift  the LRN feed-forward (clamped to the same LRN_DRIFT_MAX the
 *                learner itself honours)
 *   g_nn_tempco  algorithm 9's PWM-against-temperature slope (NN_TCO_MAX)
 *
 * Everything else a loop keeps in codes — integrators, filter memories, the
 * Kalman state — is cleared by the restart and rebuilt from the live code, and
 * every gain derived from K is read from g_pid[7] at run time or recomputed by
 * algo_coeffs_from_k() / ltic_autotune(). The I_LIMIT clamps are bounds, not
 * state, and CT has never scaled them either.
 * ====================================================================== */
void algo_rescale_lsb(double r)
{
    if (!(r > 0.0) || !isfinite(r)) return;
    double d = (double)g_lrn_drift * r;
    if (d >  (double)LRN_DRIFT_MAX) d =  (double)LRN_DRIFT_MAX;
    if (d < -(double)LRN_DRIFT_MAX) d = -(double)LRN_DRIFT_MAX;
    g_lrn_drift = (float)d;
    double t = (double)g_nn_tempco * r;
    if (t >  NN_TCO_MAX) t =  NN_TCO_MAX;
    if (t < -NN_TCO_MAX) t = -NN_TCO_MAX;
    g_nn_tempco = (float)t;
}
