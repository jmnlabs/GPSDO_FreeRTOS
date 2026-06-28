/**
 * GPSDO_algorithms.h — Control loop algorithm declarations and tunable parameters
 *
 * Part of GPSDO FreeRTOS v0.50
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

/* g_pid[3..7] hold the tunable coefficients for algorithms 3-7.
 * Indices 0-2, 8, 9 are unused placeholders.
 * Algorithms read these at runtime; CLI commands KP/KI/KD/IL modify them. */
extern PidParams_t g_pid[10];

/* Algo 8 (hybrid) blending parameters */
extern double g_blend_crossover;   /* Hz — sigmoid centre, default 0.02       */
extern double g_blend_scale;       /* Hz — sigmoid width,  default 0.01       */

/* Algo 9 (NN) output scaling */
extern double g_nn_max_step;       /* LSB — max PWM delta per step, default 200 */

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

/* ---- Helper exposed to ControlTask ---------------------------------- */
void gpsdo_calc_averages(FreqData_t *f);
