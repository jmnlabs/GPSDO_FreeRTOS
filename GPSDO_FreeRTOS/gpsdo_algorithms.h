/**
 * gpsdo_algorithms.h — Control loop algorithm declarations and tunable parameters
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 *
 * Defines the PidParams_t structure and the global g_pid[10] array
 * holding runtime-tunable PID coefficients for algorithms 3-9.
 *
 * Physical system: 16-bit PWM DAC, ~48.8 uV/LSB, dual RC filter
 * (tau ~ 200 ms). The OCXO gain is NOT a constant of this firmware: it is
 * measured by CT, and this file must not name a figure that only ever applied
 * to one part. Measured on the two boards of this design: +319.5 and
 * -379.4 uHz/LSB, both with a Vectron C4550A1-0213 (5 V supply, 0-4 V EFC).
 * The header used to quote "~5 uHz/LSB (NDK ENE3311B typical)", which is a
 * different oscillator entirely and roughly seventy times off.
 * Error convention: e = avg_freq - 10 MHz; e > 0 -> decrease PWM.
 */
#pragma once
#include <Arduino.h>
#include "gpsdo_state.h"
/* Forward declaration — full definition in gpsdo_state.h */
//struct FreqData_t;

/* ---- Runtime-tunable PID parameters --------------------------------- */
typedef struct {
    double Kp;
    double Ki;
    double Kd;
    double I_LIMIT;
} PidParams_t;

/* g_pid[3..7] hold the tunable coefficients for algorithms 3-7.
 * Indices 0-2, 8, 9 are unused placeholders.
 * Algorithms read these at runtime; CLI commands KP/KI/KD/IL modify them. */
extern PidParams_t g_pid[10];

/* Algo 8 (hybrid) blending parameters */
extern double g_blend_crossover;   /* Hz — sigmoid centre, default 0.02       */
extern double g_blend_scale;       /* Hz — sigmoid width,  default 0.01       */

/* Algo 9 (NN) output scaling */
extern double g_nn_max_step;       /* LSB — max PWM delta per step, default 200 */

/* ---- everything that follows from K, in one place ------------------------
 *
 * K is CT's measured slope, hertz per 16-bit LSB. algo_coeffs_from_k() writes
 * the coefficient set CT derives from it (PLL Kp = 0.40/K, FLL Kp = 0.35/K and
 * so on — see RUNTIME-TUNABLE PARAMETERS in gpsdo_algorithms.cpp); it is what
 * CT runs, and what a move of the span jumper runs with the stored K of the
 * position it moved to (gpsdo_span.cpp), so the two cannot drift apart.
 *
 * algo_rescale_lsb(r) converts the LEARNED state that is denominated in LSB
 * and survives a loop restart — the LRN feed-forward and algorithm 9's tempco
 * — by r = K_old/K_new, so the same physical correction is kept in the new
 * span's units. Manual gains are not touched: they are the operator's.
 *
 * algo_request_restart() is what the dispatcher does on an algorithm change:
 * every loop clears its own state on its next call and no loop keeps a lock
 * verdict it did not earn. */
void algo_coeffs_from_k(double k_hz_per_lsb);
void algo_rescale_lsb(double ratio);
void algo_request_restart(void);

/* ---- Algorithm 10: LTIC three-stage PLL (ACQ → DPLL → LOCK) ----------
 * Disciplines the OCXO from the hardware TIC phase voltage (PA1) instead of
 * the TIM2 cycle counter. A state machine: ACQ pulls phase into the
 * detector's unambiguous range (and auto-arms the picDIV), DPLL settles
 * frequency+phase quickly with a wide-band PID, LOCK then updates slowly
 * (every lock_interval_s) with a narrow-band PID to approach minimum error.
 *
 * NOTE: the loop itself is not implemented yet (planned "phase A"); these
 * parameters, their CLI commands and EEPROM persistence exist now so the
 * configuration survives reboots and is ready when the loop is written.
 * Calibrate ns_per_volt / zero_offset / range_ns on real hardware first. */
typedef struct {
    /* TIC calibration */
    float ns_per_volt;       /* voltage→time slope [ns/V] (0 = uncalibrated) */
    float zero_offset;       /* TIC volts at zero phase difference [V]        */
    float range_ns;          /* detector unambiguous range [ns] (wrap-around) */
    /* Three PID sets — ACQ (coarse pull-in), DPLL (wide-band), LOCK (narrow) */
    PidParams_t acq;         /* ACQ stage PID (coarse, frequency-led)         */
    PidParams_t dpll;        /* DPLL stage PID (fast settle)                  */
    PidParams_t lock;        /* LOCK stage PID (slow, narrow-band)            */
    /* State transition thresholds + LOCK cadence */
    float acq_threshold_ns;  /* |phase| below this → ACQ done, enter DPLL     */
    float dpll_lock_thresh;  /* frequency error below this → enter LOCK       */
    uint16_t lock_interval_s;/* LOCK update period [s] (default 300)          */
    uint8_t  state;          /* last ACQ/DPLL/LOCK state (resume after warm)  */
    uint8_t  submode;        /* 0 = pure LTIC, 1 = hybrid (reserved/future)   */
    int8_t   polarity;       /* PWM→phase sign: 0 = auto-detect, +1/-1 forced */
    float    centre_v;       /* ACQ centring target [V]; 0 = use range middle */
} LticParams_t;

extern LticParams_t g_ltic;
void ltic_autotune(void);

/* ---- Algorithm 11 (LTIC-Lars) — continuous PI loop after Lars Walenius ----
 * A single continuous PI loop (no ACQ/DPLL/LOCK state machine), with a filtered
 * phase term, adaptive pre-filter, and an optional temperature feed-forward.
 * Shares the algo-10 TIC calibration (ns_per_volt / zero_offset) for scale. */
typedef struct {
    float    gain;           /* VCO gain: DAC bits per TIC ns (0 = auto from CT) */
    float    damping;        /* loop damping on the integral term            */
    uint16_t time_const_s;   /* loop time constant [s], 1..600               */
    uint8_t  filter_div;     /* pre-filter const = time_const_s / filter_div */
    uint16_t tic_offset;     /* phase reference point [ADC counts]           */
    uint16_t lock_ns_lim;    /* phase window for lock detect [ns]            */
    uint8_t  lock_factor;    /* lock needs window held for factor*time_const */
    int16_t  temp_coeff;     /* temperature feed-forward [DAC bits/ADC step] */
    uint16_t temp_ref;       /* temperature reference point [ADC counts]     */
    uint8_t  flags;          /* bit0: temp comp enable; other bits reserved  */
    uint8_t  reserved[6];    /* reserved for future fields                   */
} LarsParams_t;

extern LarsParams_t g_lars;

/* Algo 11 live telemetry, published by ltic_lars_pi() each cycle so the Learn
 * line can report what is actually steering instead of stale LRN figures. */
extern float g_lars_scale;      /* effective frequency-branch scale in use     */
extern float g_lars_phase_filt; /* filtered phase [ns]                         */
extern bool  g_lars_locked;     /* loop's own lock flag                        */
extern bool  g_lars_gain_auto;  /* true = scale came from CT, false = manual LG */

/* flags bits */
#define LARS_FLAG_TEMP_COMP  0x01

uint16_t ltic_lars_pi(uint16_t pwm, uint32_t ppscount);

extern bool     g_lrn_enable;
extern float    g_lrn_drift;
extern float    g_lrn_damp;

/* Damping multiplier legal band, shared with live_store so a restored value
 * from older flash can be clamped into range on load. */
#define LRN_DAMP_LO     0.45f
#define LRN_DAMP_HI     1.5f
extern float    g_nn_tempco;             /* algo 9: learned oscillator tempco [LSB/degC] */
extern float    g_ltic_acq_centre_gain;  /* algo 10: ACQ centring gain [LSB/V]  */
extern float    g_ltic_acq_centre_cap;   /* algo 10: ACQ centring cap   [LSB]   */
int16_t nn_thermal_holdover_step(void); /* algo 9: HO thermal steering */
extern float    g_lrn_slope_ns_s;
extern uint16_t g_lrn_osc_period;
extern float    g_lrn_osc_amp_ns;

/* LTIC state enum (stored in g_ltic.state) */
enum { LTIC_ACQ = 0, LTIC_DPLL = 1, LTIC_LOCK = 2 };


/* ---- Algorithm selector --------------------------------------------- */
uint16_t adjustVctlPWM(uint16_t prev_pwm, uint32_t ppscount, uint8_t algo_no);

/* ---- the fine control value ------------------------------------------------
 *
 * adjustVctlPWM() returns the rounded 16-bit value every existing caller,
 * display and flash record expects. When the running algorithm computed a
 * fractional target this cycle it also leaves it here, so the control task can
 * drive the 24-bit output path without the return type — and everything that
 * depends on it — having to change.
 *
 * g_vctl_fine_valid is cleared at the top of every adjustVctlPWM() call. Read
 * it before g_vctl_fine and treat false as "write 16 bits as before"; a stale
 * fraction is then impossible by construction. */
extern double g_vctl_fine;        /* exact control value, 16-bit LSB units */
extern bool   g_vctl_fine_valid;  /* set only by an algorithm, only this cycle */

/* ---- Individual algorithms ------------------------------------------ */

/* 0 — Primitive stepped controller (original André Balsa) */
uint16_t primitive_ctl_loop(uint16_t pwm, uint32_t ppscount);

/* 1 — Forced drift: +1 LSB / 1000 s  (characterisation) */
uint16_t forced_drift_Vctl(uint16_t pwm, uint32_t ppscount);

/* 2 — Random walk: ±1 LSB / 5 s  (noise floor measurement) */
uint16_t random_walk_Vctl(uint16_t pwm, uint32_t ppscount);

/* 3 — FLL PID, manually tuned  Kp=80,  Ki=0.8,   Kd=200   */
uint16_t fll_pid_manual(uint16_t pwm, uint32_t ppscount);

/* 4 — PLL PI,  manually tuned  Kp=30,  Ki=0.003            */
uint16_t pll_pi_manual(uint16_t pwm, uint32_t ppscount);

/* 5 — PLL PID, manually tuned  Kp=40,  Ki=0.01,  Kd=800   */
uint16_t pll_pid_manual(uint16_t pwm, uint32_t ppscount);

/* 6 — FLL PID, GA-optimised    Kp=234, Ki=0.301, Kd=17082 + filtered D */
uint16_t fll_pid_genetic(uint16_t pwm, uint32_t ppscount);

/* 7 — PLL PID, GA-optimised    Kp=70,  Ki=0.181, Kd=2548  + filtered D */
uint16_t pll_pid_genetic(uint16_t pwm, uint32_t ppscount);

/* 8 — Hybrid FLL+PLL: sigmoid blend based on |error|, crossover at 20 mHz */
uint16_t hybrid_fll_pll(uint16_t pwm, uint32_t ppscount);

/* 9 — Neural network MLP (3→8→1, tanh), pre-trained weights embedded */
uint16_t nn_mlp_ctl_loop(uint16_t pwm, uint32_t ppscount);
/* The two loops that need the hardware phase detector. Only the FUNCTIONS are
 * guarded: everything below — the level count, the state and fit structs, the
 * globals and the accessors — is declared unconditionally, because the .cpp
 * defines all of it outside its own GPSDO_LTIC guard and the CLI reads it
 * unconditionally too.
 *
 * That mismatch made the firmware impossible to BUILD with GPSDO_LTIC off:
 * gpsdo_algorithms.cpp:1437 onwards and the ML/MLP/MG/MF handlers in
 * gpsdo_cli.cpp all reference MLACC_LEVELS, mlacc_fit_t and the g_mlacc_*
 * globals, and every one of those declarations used to sit inside this block.
 * Nobody had hit it because every board of this design has the detector — the
 * first person to build without one was Dave (Solder_Junkie) on EEVblog, whose
 * M8N board has no TIC front end, and he got fourteen "was not declared in this
 * scope" errors for his trouble.
 *
 * A declaration costs nothing when the definition is absent, so the guard
 * belongs on the code, not on the vocabulary used to describe it. */
#ifdef GPSDO_LTIC
uint16_t ltic_three_stage(uint16_t pwm, uint32_t ppscount);

/* Algorithm 12: multi-level accumulator, after Alan Cashin (MIS42N).
 *
 * REQUIRES GPSDO_LTIC — it works on the detector's phase in nanoseconds. An early
 * version fed the TIM2 count error instead and was blind: a disciplined
 * oscillator sits far below 1 Hz, so that field reads zero. See the .cpp. */
uint16_t multi_level_accum(uint16_t pwm, uint32_t ppscount);
#endif

#define MLACC_LEVELS 11

/* Algorithm 12 state, for telemetry. last_action is the level that most recently
 * applied a correction: low means it is acting often, high that it has settled
 * and is averaging longer before touching anything. */
typedef struct {
    uint8_t  last_action;
    uint32_t corrections;
    uint32_t arms;            /* picDIV re-arms since reset */
    uint32_t zero_cross;      /* zero-crossing corrections applied */
    int32_t  sigma_ns;        /* measured phase noise, 1-sigma [ns] */
    uint32_t seconds;
    int32_t  last_slope;
    int32_t  last_phase;
    /* THE ESTIMATE, in nanoseconds, which last_phase is not.
     *
     * last_phase is the accumulator's internal number: a sum of doubled
     * samples at whatever level fired, so its scale is 2^(level+1) and it
     * means nothing plotted beside a detector reading. This is the same
     * quantity the correction itself is computed from — last_phase / 2 /
     * 2^(level+1) — which is algorithm 12's answer to the question the Kalman
     * answers with x0: what the loop believes the phase is, after averaging
     * away the noise the single reading carries.
     *
     * Exposed because the tuner had no estimate to draw for this loop and was
     * plotting `ph`, the raw detector reading rounded to whole nanoseconds —
     * a measurement wearing an estimate's label. */
    float    est_ns;
} mlacc_stats_t;

void mlacc_get_stats(mlacc_stats_t *out);

/* ---- live lock verdicts, one per algorithm that can form one -------------
 *
 * Algorithms 10 and 11 have always published theirs as plain state
 * (g_ltic.state == LTIC_LOCK, g_lars_locked). These are the same statement for
 * 12 and 13, which had none — so the display invented a test of its own and
 * the correction statistics in gpsdo_health.cpp gave up and excluded both.
 * Decided inside the algorithm, at the one place it knows; read by the TFT
 * status bar, the frequency digits and health_note_output(). */
bool mlacc_locked(void);
bool kf_locked(void);
extern int16_t g_last_offset;
extern int32_t g_mlacc_lim[MLACC_LEVELS];   /* per-level phase limits [ns] */
extern float   g_mlacc_gain;                /* LSB per ns, 0 = auto from CT */
extern uint8_t g_mlacc_run_level;

/* ---- where the per-level limits come from (MF) --------------------------
 *
 * Until now this was not a choice: the limits and the gain lived in one
 * branch, so MG 0 meant "gain from CT AND limits from the noise formula" and
 * MG > 0 meant "gain by hand AND limits by hand". There is no reason those two
 * should be welded together — the gain belongs to the OSCILLATOR (it is LSB per
 * ns, and a different OCXO has a different Vctl sensitivity) while the limits
 * belong to the PHASE NOISE the board sees, which is a property of the site and
 * the receiver. A workshop board wants the measured gain with hand-set limits;
 * that combination could not be expressed.
 *
 * MLACC_THR_FOLLOW is what every existing installation gets, so nothing moves
 * until someone asks for it. */
enum {
    MLACC_THR_FOLLOW   = 0,  /* as before: MG 0 -> formula, MG > 0 -> stored  */
    MLACC_THR_STORED   = 1,  /* the table in flash / MLP, whatever MG says    */
    MLACC_THR_SIGMA    = 2,  /* the white-noise formula, whatever MG says     */
    MLACC_THR_MEASURED = 3,  /* fitted from the per-level spread — see .cpp   */
};
extern uint8_t  g_mlacc_thr_src;    /* one of the above                       */
extern uint16_t g_mlacc_thr_tgt_s;  /* MFT: target s between noise-driven
                                     * corrections; 0 = MLACC_THR_TGT_DEFAULT */
#define MLACC_THR_TGT_DEFAULT  3600u
/* Levels the fit needs before it will produce a table. Here rather than in the
 * .cpp because ML reports progress against it while the estimator is filling
 * up, and a number the user is shown should not be a number only one
 * translation unit knows. */
#define MLACC_FIT_MIN_LVL      3

/* Live state of the measured estimator, for ML. exponent is the fitted scaling
 * of the test statistic with level: 0.5 is what white noise gives and what the
 * formula assumes, 1.0 is a phase that averaging does not reduce at all. */
typedef struct {
    float    exponent;              /* fitted alpha, 0 = not fitted yet       */
    float    intercept_log2;        /* fitted log2(sd) at level 0             */
    uint16_t tests[MLACC_LEVELS];   /* evaluations seen at each level         */
    uint8_t  levels_used;           /* levels that entered the fit            */
    bool     valid;                 /* a fit exists and is driving the table  */
} mlacc_fit_t;
void mlacc_get_fit(mlacc_fit_t *out);

/* ---- Algorithm 13: three-state Kalman filter -------------------------
 *
 * Phase, frequency and aging, with a scalar phase measurement once a second.
 * Scalar means there is no matrix to invert — the Kalman gain is one division —
 * so the whole filter costs about 140 multiply-adds and 36 bytes of state. It
 * runs on this Cortex-M4F in roughly a microsecond, once per second.
 *
 * What it buys over a PI loop is that its bandwidth is not a constant: it
 * weighs the detector against the oscillator by their measured variances, so it
 * trusts the OCXO at short tau where GPS is noisy and yields to GPS at long tau
 * where the OCXO walks. And because the state carries frequency AND aging with
 * their uncertainties, losing the phase is not a special case — the filter
 * simply stops updating and keeps steering, which is holdover for free.
 *
 * Both noise figures are MEASURED rather than set: R from the detector's own
 * first differences, Q from the innovation sequence. KR and KQ override them
 * for an experiment; zero means measure. */
extern float    g_kf_r_ns;        /* KR: measurement noise [ns], 0 = measure  */
extern float    g_kf_q;           /* KQ: process noise [(ns/s)^2/s], 0 = adapt */
extern uint16_t g_kf_horizon_s;   /* KT: ESTIMATOR horizon [s] - Q ceiling    */
extern uint16_t g_kf_ctl_s;       /* KC: CONTROLLER horizon [s], 0 = KT/3     */

typedef struct {
    float    phase_ns;      /* estimated phase                                */
    float    freq_ns_s;     /* estimated frequency error, ns per second       */
    float    aging_ns_s2;   /* estimated aging, ns per second per second      */
    float    sigma_ns;      /* sqrt(P00): how well the phase is known         */
    float    r_ns;          /* measurement noise in use                       */
    float    q;             /* process noise in use (Sg: frequency walk)       */
    float    sf;            /* Sf: phase process noise, ns^2/s, measured       */
    float    q_max;         /* ceiling the adaptation may not pass             */
    bool     q_at_max;      /* and true while it is sitting against it         */
    float    q_min;         /* and the numerical floor under it                */
    bool     q_at_min;      /* true while it is sitting on THAT                 */
    bool     q_held;        /* adaptation stood still this second              */
    uint32_t q_freeze_s;    /* and this many seconds of post-arm freeze remain  */
    uint16_t ctl_s;         /* KC in force: how fast a known phase is nulled   */
    float    q_ratio;       /* Pobs/Ppred the adaptation last acted on         */
    float    q_lo;          /* lowest Q since the algorithm restarted          */
    float    q_hi;          /* and the highest - one KL then reads a whole run */
    bool     q_adapting;    /* false when KQ pinned Q instead of adapting it    */
    float    innov_ns;      /* last innovation                                */
    uint32_t rejects;       /* samples the innovation gate threw away         */
    float    rej_pct;       /* and the RATE of that, over ~300 s, in per cent  */
    bool     r_pinned;      /* true when KR fixed R instead of measuring it    */
    uint32_t holdover_s;    /* seconds running on the model alone             */
    uint32_t arms;          /* picDIV re-arms this session                    */
} kf_stats_t;
void kf_get_stats(kf_stats_t *out);   /* kf_locked() is declared above */
void kf_store_save(void);   /* write KR/KQ/KT to their own ring record */
bool kf_store_load(void);   /* read them back at boot                  */

#ifdef GPSDO_LTIC
uint16_t kalman_ctl(uint16_t pwm, uint32_t ppscount);
#endif

/* ---- Helper exposed to ControlTask ---------------------------------- */
void gpsdo_calc_averages(FreqData_t *f);
