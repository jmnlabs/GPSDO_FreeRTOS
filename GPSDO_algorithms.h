/**
 * GPSDO_algorithms.h — Control loop algorithm declarations and tunable parameters
 *
 * Part of GPSDO FreeRTOS v0.95
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Defines the PidParams_t structure and the global g_pid[10] array
 * holding runtime-tunable PID coefficients for algorithms 3-9.
 *
 * Physical system: 16-bit PWM DAC, ~48.8 uV/LSB, dual RC filter
 * (tau ~ 200 ms), OCXO gain ~ 5 uHz/LSB (NDK ENE3311B typical).
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

/* g_pid[3..7] hold Kp/Ki/Kd/I_LIMIT for algorithms 3-7. Indices 8 and 9 are NOT
 * spare: algorithms 8 (hybrid) and 9 (NN) read their I_LIMIT from here, and the
 * IL command accordingly accepts 3-9 where KP/KI/KD accept only 3-7. What those
 * two ignore is Kp/Ki/Kd — the hybrid takes its PID from g_pid[6] and g_pid[7],
 * the NN uses fixed weights — so CT leaves those fields alone on purpose.
 * Indices 0-2 really are unused: algorithms 0-2 are open-loop (primitive,
 * forced drift, random walk) and have nothing to tune.
 * Algorithms read these at runtime; CLI commands KP/KI/KD/IL modify them. */
extern PidParams_t g_pid[10];

/* Algo 8 (hybrid) blending parameters */
extern double g_blend_crossover;   /* Hz — sigmoid centre, default 0.02       */
extern double g_blend_scale;       /* Hz — sigmoid width,  default 0.01       */

/* Algo 9 (NN) output scaling */
extern double g_nn_max_step;       /* LSB — max PWM delta per step, default 200 */

/* ---- Algorithm 10: LTIC three-stage PLL (ACQ → DPLL → LOCK) ----------
 * Disciplines the OCXO from the hardware TIC phase voltage (PA1) instead of
 * the TIM2 cycle counter. A state machine: ACQ pulls phase into the
 * detector's unambiguous range (and auto-arms the picDIV), DPLL settles
 * frequency+phase quickly with a wide-band PID, LOCK then updates slowly
 * (every lock_interval_s) with a narrow-band PID to approach minimum error.
 *
 * Calibrate ns_per_volt / zero_offset / range_ns on real hardware (LC) before
 * selecting the algorithm: without them the phase reading has no scale. */
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
extern bool     g_lrn_enable;
extern float    g_lrn_drift;
extern float    g_lrn_damp;

/* Damping multiplier legal band, shared with live_store so a restored value
 * from older flash can be clamped into range on load. */
#define LRN_DAMP_LO     0.45f
#define LRN_DAMP_HI     1.5f
extern float    g_nn_tempco;
extern float    g_ltic_acq_centre_gain;  /* algo 10: ACQ centring gain [LSB/V]  */
extern float    g_ltic_acq_centre_cap;   /* algo 10: ACQ centring cap   [LSB]   */ /* algo 9: learned oscillator tempco [LSB/°C] */
int16_t nn_thermal_holdover_step(void); /* algo 9: HO thermal steering */
extern float    g_lrn_slope_ns_s;
extern uint16_t g_lrn_osc_period;
extern float    g_lrn_osc_amp_ns;

/* LTIC state enum (stored in g_ltic.state) */
enum { LTIC_ACQ = 0, LTIC_DPLL = 1, LTIC_LOCK = 2 };


/* ---- Algorithm selector --------------------------------------------- */
uint16_t adjustVctlPWM(uint16_t prev_pwm, uint32_t ppscount, uint8_t algo_no);

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
#ifdef GPSDO_LTIC
uint16_t ltic_three_stage(uint16_t pwm, uint32_t ppscount);
#endif

/* ---- Helper exposed to ControlTask ---------------------------------- */
void gpsdo_calc_averages(FreqData_t *f);
