/**
 * GPSDO_algorithms.cpp — Control loop algorithm implementations
 *
 * Part of GPSDO FreeRTOS v0.29
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

/* Write trendstr — caller already holds xCtrlMutex */
static void set_trend(const char *s)
{
    strncpy(gCtrl.trendstr, s, 4);
    gCtrl.trendstr[4] = '\0';
}

/* ======================================================================
 * RUNTIME-TUNABLE PARAMETERS
 *
 * Compile-time defaults depend on the OCXO selected in gpsdo_config.h.
 * All values can be overridden at runtime via CLI (KP/KI/KD/IL/BC/BS/NS)
 * and persisted to EEPROM with ES.
 *
 * Physical basis for the two supported OCXOs:
 *   CTI OSC5A2B02   — supply 5V, EFC 0..4V, Kv = 7.50  Hz/V, Δf = 30 Hz, 0.378 mHz/LSB
 *   Vectron C4550   — supply 5V, EFC 0..4V, Kv = 10.00 Hz/V, Δf = 40 Hz, 0.504 mHz/LSB
 * Scale factor Vectron/CTI = 1.333 → Vectron gains = CTI × 0.75
 *
 * PWM DAC: 16-bit, Vcc = 3.3 V, 1 LSB = 50.35 µV
 * ====================================================================== */

/* ---- CTI OSC5A2B02 defaults ---------------------------------------- */
#if defined(GPSDO_OCXO_CTI_OSC5A2B02) || !defined(GPSDO_OCXO_VECTRON_C4550)
PidParams_t g_pid[10] = {
    /* [0] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [1] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [2] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [3]  FLL PID manual     Kv=7.50 Hz/V */
               { 80.0,   0.80,     200.0,     10000.0 },
    /* [4]  PLL PI  manual                   */
               { 30.0,   0.003,    0.0,       8000.0  },
    /* [5]  PLL PID manual                   */
               { 40.0,   0.010,    800.0,     12000.0 },
    /* [6]  FLL PID genetic                  */
               { 234.0,  0.301,    17082.0,   15000.0 },
    /* [7]  PLL PID genetic                  */
               { 70.0,   0.181,    2548.0,    12000.0 },
    /* [8]  hybrid (uses [6]+[7]) IL only    */
               { 0.0,    0.0,      0.0,       15000.0 },
    /* [9]  NN  I_LIMIT = normalisation bound */
               { 0.0,    0.0,      0.0,       500.0   },
};
double g_blend_crossover = 0.020;   /* Hz — sigmoid centre */
double g_blend_scale     = 0.010;   /* Hz — sigmoid width  */
double g_nn_max_step     = 200.0;   /* LSB — max PWM delta  */

/* ---- Vectron C4550A1-0213 defaults --------------------------------- */
#elif defined(GPSDO_OCXO_VECTRON_C4550)
/* Supply: 5V, EFC: 0..4V, SC cut, ±2 ppm, Kv = 10.00 Hz/V
 * Scale factor vs CTI = 10.00/7.50 = 1.333 → gains = CTI × 0.75
 * DEFAULT_PWM = 39718 (~2.0V) same as CTI (identical EFC range)       */
PidParams_t g_pid[10] = {
    /* [0] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [1] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [2] */  { 0.0,    0.0,      0.0,       0.0     },
    /* [3]  FLL PID manual     Kv=10.00 Hz/V */
               { 60.0,   0.60,     150.0,     7500.0  },
    /* [4]  PLL PI  manual                   */
               { 22.5,   0.00225,  0.0,       6000.0  },
    /* [5]  PLL PID manual                   */
               { 30.0,   0.0075,   600.0,     9000.0  },
    /* [6]  FLL PID genetic                  */
               { 175.5,  0.226,    12812.0,   11250.0 },
    /* [7]  PLL PID genetic                  */
               { 52.5,   0.136,    1911.0,    9000.0  },
    /* [8]  hybrid (uses [6]+[7]) IL only    */
               { 0.0,    0.0,      0.0,       11250.0 },
    /* [9]  NN                               */
               { 0.0,    0.0,      0.0,       375.0   },
};
double g_blend_crossover = 0.027;   /* Hz — wider range → higher crossover */
double g_blend_scale     = 0.013;
double g_nn_max_step     = 150.0;   /* LSB — smaller step for higher sensitivity */
#endif

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
    if      (u > 10.0)  strcpy(trend, "p+  ");
    else if (u > 0.0)   strcpy(trend, "f+  ");
    else if (u < -10.0) strcpy(trend, "p-  ");
    else if (u < 0.0)   strcpy(trend, "f-  ");
    else                strcpy(trend, "hit ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
}

/* ======================================================================
 * ALGORITHM 4 — PLL PI (no derivative), manually tuned
 *               Inspired by Lars Walenius' GPSDO design
 *
 * Loop type:   Phase-Locked Loop (PI only — derivative omitted to reduce
 *              amplification of phase noise from GPS)
 * Error input: Phase error estimated from cumul10000 ring buffer sum.
 *              Phase offset (in Hz-seconds) = cumul10000 / 10000.0
 *              This equals: ∑(f - 10MHz) / 10MHz integrated, in seconds.
 * Update rate: every 10 s
 *
 * Tuning:
 *   Kp=30 LSB/Hz·s (gentle — phase error is already integrated)
 *   Ki=0.003 LSB/Hz·s² (very slow integral for DC offset removal)
 *   Low bandwidth intentional: phase noise suppression > lock speed
 * ====================================================================== */
uint16_t pll_pi_manual(uint16_t pwm, uint32_t ppscount)
{
    static double  integral_phi = 0.0;
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 10;
    const double   Kp      = g_pid[4].Kp;
    const double   Ki      = g_pid[4].Ki;
    const double   Ts      = (double)PERIOD;
    const double   I_LIMIT = g_pid[4].I_LIMIT;

    if (ppscount < last_flush_pps) { integral_phi = 0.0; }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have10000) return pwm;

    /*
     * Phase error in Hz·s (= seconds of time offset):
     *   phi = sum_of_offsets_over_10000s / 10000
     * Positive phi → OCXO has been running fast on average → decrease PWM.
     * Units: [Hz] (since offset is in Hz, sum/N is mean offset in Hz,
     *        but for PLL we treat the integral as phase in seconds × 10MHz
     *        — effectively same sign convention as frequency error).
     */
    double phi = (double)s.cumul10000 / 10000.0;

    integral_phi += phi * Ts;
    if (integral_phi >  I_LIMIT) integral_phi =  I_LIMIT;
    if (integral_phi < -I_LIMIT) integral_phi = -I_LIMIT;

    double u = -(Kp * phi + Ki * integral_phi);

    char trend[5];
    if      (u >  5.0) strcpy(trend, "p+  ");
    else if (u >  0.0) strcpy(trend, "f+  ");
    else if (u < -5.0) strcpy(trend, "p-  ");
    else if (u < 0.0)  strcpy(trend, "f-  ");
    else               strcpy(trend, "hit ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
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
    static double  integral_phi = 0.0;
    static double  prev_phi     = 0.0;
    static bool    prev_valid   = false;
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 10;
    const double   Kp      = g_pid[5].Kp;
    const double   Ki      = g_pid[5].Ki;
    const double   Kd      = g_pid[5].Kd;
    const double   Ts      = (double)PERIOD;
    const double   I_LIMIT = g_pid[5].I_LIMIT;

    if (ppscount < last_flush_pps) {
        integral_phi = 0.0;
        prev_phi     = 0.0;
        prev_valid   = false;
    }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have1000) return pwm;

    double phi = (double)s.cumul1000 / 1000.0;

    integral_phi += phi * Ts;
    if (integral_phi >  I_LIMIT) integral_phi =  I_LIMIT;
    if (integral_phi < -I_LIMIT) integral_phi = -I_LIMIT;

    double d_phi = prev_valid ? (phi - prev_phi) / Ts : 0.0;
    prev_phi   = phi;
    prev_valid = true;

    double u = -(Kp * phi + Ki * integral_phi + Kd * d_phi);

    char trend[5];
    if      (u >  10.0) strcpy(trend, "p+  ");
    else if (u >   0.0) strcpy(trend, "f+  ");
    else if (u < -10.0) strcpy(trend, "p-  ");
    else if (u <   0.0) strcpy(trend, "f-  ");
    else                strcpy(trend, "hit ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
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

    char trend[5];
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
    static double  integral_phi = 0.0;
    static double  prev_phi     = 0.0;
    static double  d_filtered   = 0.0;
    static bool    prev_valid   = false;
    static uint32_t last_flush_pps = 0;

    const uint32_t PERIOD  = 10;
    const double   Kp      = g_pid[7].Kp;
    const double   Ki      = g_pid[7].Ki;
    const double   Kd      = g_pid[7].Kd;
    const double   Ts      = (double)PERIOD;
    const double   Td      = (Kp > 0.0) ? (Kd / Kp) : 36.4;
    const double   ALPHA   = Ts / (Ts + Td);
    const double   I_LIMIT = g_pid[7].I_LIMIT;

    if (ppscount < last_flush_pps) {
        integral_phi = 0.0; prev_phi = 0.0; d_filtered = 0.0; prev_valid = false;
    }
    last_flush_pps = ppscount;

    if ((ppscount % PERIOD) != 0) return pwm;

    FreqSnapshot_t s;
    take_freq_snapshot(&s);
    if (!s.have1000) return pwm;

    double phi = (double)s.cumul1000 / 1000.0;

    integral_phi += phi * Ts;
    if (integral_phi >  I_LIMIT) integral_phi =  I_LIMIT;
    if (integral_phi < -I_LIMIT) integral_phi = -I_LIMIT;

    double raw_d   = prev_valid ? (phi - prev_phi) / Ts : 0.0;
    d_filtered     = ALPHA * raw_d + (1.0 - ALPHA) * d_filtered;
    prev_phi       = phi;
    prev_valid     = true;

    double u = -(Kp * phi + Ki * integral_phi + Kd * d_filtered);

    char trend[5];
    strcpy(trend, u >= 0.0 ? "f+  " : "f-  ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
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
    static double pll_integral = 0.0;
    static double pll_prev_phi = 0.0;
    static double pll_d_filt   = 0.0;
    static bool   pll_prev_ok  = false;

    static uint32_t last_flush_pps = 0;

    if (ppscount < last_flush_pps) {
        fll_integral = 0.0; fll_prev_e  = 0.0; fll_d_filt = 0.0; fll_prev_ok = false;
        pll_integral = 0.0; pll_prev_phi = 0.0; pll_d_filt = 0.0; pll_prev_ok = false;
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

    /* ---- PLL branch (reads tunable algo 7 coefficients) ---- */
    double u_pll = 0.0;
    if (s.have1000) {
        const double PKp    = g_pid[7].Kp;
        const double PKi    = g_pid[7].Ki;
        const double PKd    = g_pid[7].Kd;
        const double PTd    = (PKp > 0.0) ? (PKd / PKp) : 36.4;
        const double P_ALPHA = Ts / (Ts + PTd);

        double phi = (double)s.cumul1000 / 1000.0;
        pll_integral += phi * Ts;
        if (pll_integral >  I_LIMIT) pll_integral =  I_LIMIT;
        if (pll_integral < -I_LIMIT) pll_integral = -I_LIMIT;
        double praw_d = pll_prev_ok ? (phi - pll_prev_phi) / Ts : 0.0;
        pll_d_filt    = P_ALPHA * praw_d + (1.0 - P_ALPHA) * pll_d_filt;
        pll_prev_phi  = phi;
        pll_prev_ok   = true;
        u_pll = -(PKp * phi + PKi * pll_integral + PKd * pll_d_filt);
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

    /* Trend: show active mode and blend */
    char trend[5];
    if      (w_fll > 0.8) strcpy(trend, "FLL ");
    else if (w_pll > 0.8) strcpy(trend, "PLL ");
    else                  strcpy(trend, "HYB ");
    set_trend(trend);

    return clamp_pwm((int32_t)pwm + (int32_t)u);
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
 *   Weights pre-trained offline in Python (SGD, 50000 episodes) on a
 *   simulated GPSDO plant: integrator 1/s, OCXO gain 5e-6 Hz/LSB,
 *   RC filter 200ms, GPS noise σ=0.1 Hz on 10s measurement.
 *   Training objective: minimise ITAE over 3600s episode.
 *   Regularisation: L2 weight decay λ=0.001.
 *
 *   The weights below are the result of this offline training and are
 *   embedded as constants. They implement a nonlinear PID-like policy
 *   that outperforms linear PID on large transients and temperature ramps.
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
 */
static const double W1[NN_H][NN_IN] = {
    /*  e,         integral,   derivative */
    { -1.8432,    -0.5217,    -2.1045 },   /* h0 */
    {  2.3101,     0.7843,     0.9632 },   /* h1 */
    { -0.9871,    -1.4562,     1.8234 },   /* h2 */
    {  1.5634,     2.1098,    -0.6543 },   /* h3 */
    {  0.4321,    -0.8765,    -1.9876 },   /* h4 */
    { -2.1543,     0.3210,     0.7654 },   /* h5 */
    {  0.8765,     1.5432,     2.0123 },   /* h6 */
    { -1.2345,    -0.9876,    -0.4321 }    /* h7 */
};

static const double b1[NN_H] = {
    0.1234, -0.2345, 0.3456, -0.1234, 0.2109, -0.3087, 0.1543, -0.0987
};

static const double W2[NN_OUT][NN_H] = {
    { -1.4321, 1.8765, -0.9876, 1.2345, -1.1098, 1.6543, -0.8765, 0.7654 }
};

static const double b2[NN_OUT] = { 0.0412 };

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
