/* ======================================================================
 * gpsdo_span.cpp  —  two EFC spans, two calibrations, one jumper on PB14
 *
 * Part of GPSDO v1.07.57rt
 *
 * See gpsdo_span.h for what this is for. This file is the state machine; the
 * arithmetic that matters is span_map(), and the reasoning for it is there.
 * ====================================================================== */

#include <Arduino.h>
#include "gpsdo_config.h"
#include "gpsdo_tee_serial.h"
#include "gpsdo_state.h"
#include "gpsdo_span.h"
#include "gpsdo_algorithms.h"
#include "gpsdo_dac.h"
#include "gpsdo_dac_ext.h"          /* PIN_DAC_*, for the collision check    */
#include "gpsdo_health.h"
#include "gpsdo_settings_store.h"
#include "gpsdo_live_store.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- the pin, and the one way it can collide ---------------------------
 *
 * PB14 is the pin the audit in gpsdo_dac_ext.h found free in every
 * configuration, and until now that audit also offered it as the place to move
 * the AD5680's chip select if a board wanted the TM1637 back. Both cannot
 * happen: INPUT_PULLUP here would take the pin away from the DAC, and the DAC's
 * select pulses would read as a jumper. Caught at compile time rather than on a
 * bench, for all three DAC pins because all three can be overridden. */
#ifdef GPSDO_SPAN_SENSE
#ifndef PIN_SPAN_SENSE
#define PIN_SPAN_SENSE PB14
#endif
static_assert(PIN_SPAN_SENSE != PIN_DAC_CS,
              "PIN_SPAN_SENSE collides with PIN_DAC_CS - move one (PC14/PC15 are the others free)");
static_assert(PIN_SPAN_SENSE != PIN_DAC_SCK,  "PIN_SPAN_SENSE collides with PIN_DAC_SCK");
static_assert(PIN_SPAN_SENSE != PIN_DAC_MOSI, "PIN_SPAN_SENSE collides with PIN_DAC_MOSI");
#endif

/* Consecutive identical readings, at the control task's 5 Hz, before a change
 * of jumper is believed: 3 = 400..600 ms. A jumper being pushed home bounces
 * for milliseconds; a wire to a header picks up nothing a 40 k pull-up and
 * three agreeing samples do not reject. */
#define SPAN_DEBOUNCE_N     3u

/* How close to either rail a remapped code may land before the move is
 * reported as out of reach. The same 1000 LSB CT keeps clear of the rails
 * (CT_RAIL_GUARD in gpsdo_control.cpp), for the same reason: the output stage
 * is least linear against its own rail, and a board that needs the last
 * thousand codes of the reduced window to reach its oscillator should not be
 * on the reduced window. */
#define SPAN_RAIL_GUARD     1000.0

/* TWO POSITIONS THAT MEASURE THE SAME PLANT ARE ONE POSITION MEASURED TWICE.
 *
 * If CT in one position returns a K within this factor of the K stored for the
 * other, the other one was almost certainly measured in THIS position — the
 * typical case being a board calibrated before PB14 was wired, whose only
 * calibration was assigned to FULL by the migration while the jumper actually
 * sat in REDUCED. Left alone, the next move to FULL would run the full span
 * on the reduced span's K, about 5x too much loop gain — the mismatch this
 * module exists to remove (see gpsdo_span.h for what it costs).
 *
 * 1.5 is a guard band, not a tuning: CT's own run-to-run scatter on a reduced
 * span was 14 % on Dan Wiering's board (46711 and 40873 LSB/Hz, 20.09 and
 * 22.09), and any reduction worth building is several times — his is ~5x.
 * Below 2x a reduced span buys less than CT's own scatter, so a board where
 * the two positions really differ by less than 1.5 is not a configuration
 * this has to serve. */
#define SPAN_SAME_PLANT     1.5

/* A pair of codes is only a measurement if both halves describe the same
 * oscillator: the code the loop in the old span held when it was switched away,
 * and the code the loop in the new span settles on. Between the two the
 * oscillator ages; three hours at the 1.7e-10/day Dan's board showed is 2e-11,
 * a few codes, and after that the anchor is dropped rather than trusted. */
#define SPAN_ANCHOR_MAX_MS  (3UL * 3600UL * 1000UL)

/* A POSITION WITHOUT A CALIBRATION OF ITS OWN GETS ONE BY ITSELF.
 *
 * Until it has one the loop runs on the other position's coefficients, which
 * is what the firmware always did with its single calibration, and CT is
 * started to measure it: as soon as there is a GPS fix, and after a failure
 * again SPAN_CT_RETRY_MS later, the wait doubling, SPAN_CT_TRIES attempts in
 * all (at once, then 10, 20 and 40 minutes after each failure). Then it stops
 * and says so — a position CT cannot measure (a K outside its gates) would
 * otherwise be swept for nothing every so often forever, under a loop that is
 * working. The next power-on in that position tries again, and CT by hand
 * always can.
 *
 * Only with a fix: CT measures the oscillator against GPS, and without one it
 * can only fail. Asked for unconditionally at power-on (with WU 0 CT runs
 * within a second of boot) it would sweep before the receiver had a fix and
 * spend an attempt on its coarse gate. And never in holdover: HO is the
 * operator asking for the output to stay put.
 *
 * NOT HELD. The first version of this held the loop output until CT had
 * measured the position, on the theory that the other position's gain was the
 * worse evil. Simulation said otherwise — see gpsdo_span.h: a gain 4-6x off
 * cost the loops up to about eightfold in phase sd, and none diverged — and
 * spansim showed what the hold costs when CT cannot succeed: moved to
 * REDUCED at power-on with CT failing for three hours, the held board sat at
 * 3e-8 for over five hours and walked 570 us of phase. Left on FULL's
 * coefficients it locked in 58 minutes — 66 with the failed attempts sweeping
 * it meanwhile. In the normal case the two are identical: CT starts at once,
 * and the loop does not run while it sweeps. */
#define SPAN_CT_RETRY_MS  (10UL * 60UL * 1000UL)
#define SPAN_CT_TRIES     4u

static span_store_t s_st;                     /* persistent (settings store)  */
static uint8_t  s_active   = SPAN_FULL;
/* CT for a position that has none (span_ct_auto) */
static bool     s_ct_due    = false;          /* armed: attempts left          */
static bool     s_ct_seen   = false;          /* a CT has been pending since   */
static uint8_t  s_ct_fails  = 0u;
static uint32_t s_ct_fail_ms = 0u;            /* when the last one failed      */
static const char *s_ct_wait_why = NULL;      /* what it waits for, once said  */
#ifdef GPSDO_SPAN_SENSE
static bool     s_need_save = false;          /* deferred from span_begin()    */
static uint8_t  s_raw_last  = 0xFFu;          /* debounce (span_poll)          */
static uint8_t  s_raw_run   = 0u;
#endif
/* anchor: the old span's locked code, waiting for the new span to lock */
static bool     s_anc_armed = false;
#ifdef GPSDO_SPAN_SENSE
static bool     s_anc_unlocked = false;       /* the new span's loop has been
                                                 seen NOT locked since the move */
static uint8_t  s_anc_from  = 0u;
static uint16_t s_anc_code  = 0u;
static uint32_t s_anc_ms    = 0u;
#endif

const char *span_name(uint8_t s) { return (s == SPAN_REDUCED) ? "REDUCED" : "FULL"; }
uint8_t     span_active(void)    { return s_active; }

/* This position has no K of its own and the other has: CT is what is
 * missing. */
static bool span_needs_ct(void)
{
    return s_st.k[s_active] == 0.0f && s_st.k[s_active ^ 1u] > 0.0f;
}

bool span_sense_compiled(void)
{
#ifdef GPSDO_SPAN_SENSE
    return true;
#else
    return false;
#endif
}

void span_store_get(span_store_t *out) { *out = s_st; }

void span_store_put(const span_store_t *in)
{
    /* Range-checked here rather than in the settings store, next to the
     * meaning. K outside CT's own final gate (1e-5..2e-3 Hz/LSB) cannot have
     * come from CT, so it reads as "not calibrated"; a span index that is
     * neither of the two reads as "never recorded". */
    s_st = *in;
    for (int i = 0; i < 2; i++) {
        float k = s_st.k[i];
        if (!(isfinite(k) && k >= 1.0e-5f && k <= 2.0e-3f)) s_st.k[i] = 0.0f;
    }
    if (s_st.last > 2u) s_st.last = 0u;
}

/* ---- reading the jumper ------------------------------------------------ */
#ifdef GPSDO_SPAN_SENSE
static uint8_t span_read_raw(void)
{
    return (digitalRead(PIN_SPAN_SENSE) == LOW) ? SPAN_REDUCED : SPAN_FULL;
}

/* Majority of five, for the two places that read once and act: boot and the
 * CT check. The control task's own debounce covers everything else. */
static uint8_t span_read_vote(void)
{
    uint8_t red = 0;
    for (int i = 0; i < 5; i++) {
        if (span_read_raw() == SPAN_REDUCED) red++;
        delayMicroseconds(200);
    }
    return (red >= 3u) ? SPAN_REDUCED : SPAN_FULL;
}
#endif

bool span_pin_moved_since(uint8_t s)
{
#ifdef GPSDO_SPAN_SENSE
    return span_read_vote() != s;
#else
    (void)s;
    return false;
#endif
}

/* ---- the remap ----------------------------------------------------------
 *
 * WHY A PAIR OF CODES AND NOT A MODEL OF THE SHIFTER.
 *
 * Each position puts an affine function of the code on the EFC pin:
 *     V_A(c) = a_A + b_A*c        V_B(c) = a_B + b_B*c
 * The slopes are known — CT measures K = S*b, S being the oscillator's own
 * Hz/V, so b_A/b_B = K_A/K_B with S cancelling. The offsets are not, and they
 * cannot be computed: they are set by the reference, the divider and the
 * trimmer on the shifter, none of which the firmware can see.
 *
 * ONE pair of codes (p_A, p_B) known to put the SAME voltage on the pin fixes
 * the offsets' difference, a_A - a_B = b_B*p_B - b_A*p_A, and then the code in
 * B that reproduces what c_A produced in A is
 *     c_B = p_B + (K_A/K_B) * (c_A - p_A)
 * — exact, and independent of the oscillator: it maps VOLTAGE to VOLTAGE, so
 * the oscillator's aging since the pair was taken is carried across rather
 * than lost, and whether the loop happened to be locked at the moment of the
 * move does not matter. The frequency the board had before the jumper moved is
 * the frequency it has after.
 *
 * What the pair costs is its source. It is refreshed on every move (the old
 * code and the mapped one are such a pair by construction — see do_switch for
 * why that is not circular), and it is MEASURED whenever the old span was
 * locked at the move and the new one locks afterwards, or CT runs there: two
 * nulls of the same oscillator are the same voltage. The error of a map is the
 * error of the K ratio times the distance from the pair, which is why keeping
 * the pair close to where the loop actually is matters more than anything
 * else here. */
#ifdef GPSDO_SPAN_SENSE
static double span_map(uint8_t a, uint8_t b, double c_a)
{
    double r = (double)s_st.k[a] / (double)s_st.k[b];
    return (double)s_st.pair[b] + r * (c_a - (double)s_st.pair[a]);
}
#endif

static bool span_pair_valid(void)
{
    return s_st.pair[0] != 0u && s_st.pair[1] != 0u &&
           s_st.k[0] > 0.0f && s_st.k[1] > 0.0f;
}

#ifdef GPSDO_SPAN_SENSE
/* The live coefficients follow the span. Called with a span that HAS a K. */
static void span_apply_k(uint8_t s)
{
    algo_coeffs_from_k((double)s_st.k[s]);
#ifdef GPSDO_LTIC
    ltic_autotune();       /* recomputes only when K actually moved */
#endif
}

static uint16_t span_round16(double c)
{
    if (c < 1.0)     return 1u;
    if (c > 65535.0) return 65535u;
    return (uint16_t)(c + 0.5);
}

/* Manual gains are LSB-denominated and were set for whichever span the
 * operator was on. They are the operator's choice, so they are not rescaled —
 * but the operator is told, because on the other span each one is off by the
 * ratio just printed. */
static void span_warn_manual(void)
{
    if (g_lars.gain > 0.0f)
        OUT_SERIAL.println("SPAN: LG is a manual gain in LSB - set for the other span (LG 0 = auto from CT)");
    if (g_mlacc_gain > 0.0f)
        OUT_SERIAL.println("SPAN: MG is a manual gain in LSB - set for the other span (MG 0 = auto from CT)");
    if (g_lars.temp_coeff != 0)
        OUT_SERIAL.println("SPAN: LTK is in DAC bits per ADC step - set for the other span");
}
#endif /* GPSDO_SPAN_SENSE */

/* ---- CT for a position that has none ------------------------------------ */
static void span_ct_arm(bool on)
{
    s_ct_due      = on;
    s_ct_seen     = false;
    s_ct_fails    = 0u;
    s_ct_wait_why = NULL;
}

static uint32_t span_ct_wait_ms(void)          /* after s_ct_fails failures */
{
    return SPAN_CT_RETRY_MS << (s_ct_fails > 0u ? s_ct_fails - 1u : 0u);
}

#ifdef GPSDO_SPAN_SENSE
/* What CT would be waiting for, or NULL if it can start. A GPS fix, because
 * CT measures against GPS; and not in holdover, because a manual HO is the
 * operator asking for the output to stay where it is, and a sweep is the
 * opposite (auto-holdover means no fix anyway). Arrays, not literals at the
 * return: span_ct_auto compares the pointer to say each reason once, and only
 * an object has an address the language promises to keep. */
static const char SPAN_WAIT_HO[]  = "holdover to end";
static const char SPAN_WAIT_FIX[] = "a GPS fix";

static const char *span_ct_blocked(void)
{
    bool ho = false, fix = false;
    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        ho = gCtrl.holdover_mode;
        xSemaphoreGive(xCtrlMutex);
    }
    if (ho) return SPAN_WAIT_HO;
    if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        fix = gGps.pos_valid;
        xSemaphoreGive(xGpsMutex);
    }
    return fix ? NULL : SPAN_WAIT_FIX;
}

/* Called by span_poll while this position needs CT and the jumper agrees with
 * it (a move being debounced is about to change what CT would measure). CT is
 * synchronous on this task, so "the event bit was set and now is clear" with
 * the position still uncalibrated can only mean it ran and failed — the
 * operator's CT as much as ours; either one succeeding ends this in
 * span_ct_done. */
static void span_ct_auto(void)
{
    if (!s_ct_due) return;                     /* never armed, or given up */
    const bool pending = (xEventGroupGetBits(xSysEvents) & EVT_NEED_TUNE) != 0u;
    if (pending) {
        s_ct_seen = true;
        return;
    }
    const uint8_t o = (uint8_t)(s_active ^ 1u);
    if (s_ct_seen) {
        s_ct_seen    = false;
        s_ct_fail_ms = millis();
        if (++s_ct_fails >= SPAN_CT_TRIES) {
            s_ct_due = false;
            OUT_SERIAL.print("SPAN: CT failed "); OUT_SERIAL.print((unsigned)s_ct_fails);
            OUT_SERIAL.print(" times in ");      OUT_SERIAL.print(span_name(s_active));
            OUT_SERIAL.println(" - giving up until the next power-on; type CT to try again");
            OUT_SERIAL.print("SPAN: the loop stays on ");
            OUT_SERIAL.print(span_name(o));
            OUT_SERIAL.println("'s calibration meanwhile");
        } else {
            OUT_SERIAL.print("SPAN: CT failed in "); OUT_SERIAL.print(span_name(s_active));
            OUT_SERIAL.print(" - again in ");       OUT_SERIAL.print(span_ct_wait_ms() / 60000UL);
            OUT_SERIAL.print(" min; the loop stays on ");
            OUT_SERIAL.print(span_name(o));
            OUT_SERIAL.println("'s calibration meanwhile");
        }
        return;
    }
    if (s_ct_fails > 0u && (uint32_t)(millis() - s_ct_fail_ms) < span_ct_wait_ms())
        return;
    const char *why = span_ct_blocked();
    if (why != NULL) {
        if (s_ct_wait_why != why) {
            s_ct_wait_why = why;
            OUT_SERIAL.print("SPAN: CT for "); OUT_SERIAL.print(span_name(s_active));
            OUT_SERIAL.print(" waits for ");   OUT_SERIAL.println(why);
        }
        return;
    }
    s_ct_wait_why = NULL;
    s_ct_seen     = true;
    OUT_SERIAL.print("SPAN: starting CT to calibrate "); OUT_SERIAL.println(span_name(s_active));
    xEventGroupSetBits(xSysEvents, EVT_NEED_TUNE);
}
#endif

/* ---- the switch ---------------------------------------------------------
 *
 * Compiled only with the sensing: without it nothing can move the jumper as
 * far as the firmware knows, and the stored state is only carried.
 *
 * a -> b. At boot this runs before the scheduler (no mutex, no flash, no event
 * bits — those are deferred to span_poll) and before the final DAC write, so
 * the oscillator never sees a code from the wrong span. At run time it runs on
 * the control task between two loop steps, so nothing else writes the DAC
 * while it does. */
#ifdef GPSDO_SPAN_SENSE
static void do_switch(uint8_t a, uint8_t b, bool at_boot)
{
    const bool   ka   = s_st.k[a] > 0.0f;
    const bool   kb   = s_st.k[b] > 0.0f;
    const bool   pair = span_pair_valid();
    const double c_a  = at_boot ? (double)gCtrl.pwm_output : gpsdo_dac_last16f();
    double       c_b  = c_a;
    bool         mapped = false, reach = true;

    /* Anchor candidate for the pair: only a LOCKED loop's code is a null, and
     * only a null in each span makes a measured pair. Never at boot — no loop
     * has run yet, so nothing is known to be locked. A new move cancels an old
     * candidate: it described a span we have just left.
     *
     * Armed whether or not the new span is calibrated: when it is not, CT is
     * about to run there, and CT's null is the other half (span_ct_done). */
    s_anc_armed = false;
    if (!at_boot && ka && health_loop_locked()) {
        s_anc_armed    = true;
        s_anc_unlocked = false;
        s_anc_from     = a;
        s_anc_code  = span_round16(c_a);
        s_anc_ms    = millis();
    }

    if (ka && kb && pair) {
        c_b    = span_map(a, b, c_a);
        mapped = true;
        if (c_b < SPAN_RAIL_GUARD || c_b > 65535.0 - SPAN_RAIL_GUARD) reach = false;
    }

    OUT_SERIAL.print("SPAN: "); OUT_SERIAL.print(span_name(a));
    OUT_SERIAL.print(" -> ");   OUT_SERIAL.print(span_name(b));
    OUT_SERIAL.print(b == SPAN_REDUCED ? " (PB14 low" : " (PB14 high");
    OUT_SERIAL.println(at_boot ? ", moved while powered off)" : ")");

    if (mapped) {
        OUT_SERIAL.print("SPAN: code "); OUT_SERIAL.print(c_a, 1);
        OUT_SERIAL.print(" -> ");        OUT_SERIAL.print(c_b, 1);
        OUT_SERIAL.print("  (K ratio "); OUT_SERIAL.print((double)s_st.k[a] / (double)s_st.k[b], 3);
        OUT_SERIAL.println(", same voltage on the EFC pin)");
        if (!reach) {
            OUT_SERIAL.println("SPAN: ** the oscillator is outside the reach of this span.");
            OUT_SERIAL.println("SPAN:    Move the jumper back and let it settle first.");
        }
        /* Refresh the pair to where the loop actually is. (c_a, c_b) put the
         * same voltage on the pin to the accuracy of the old pair and the K
         * ratio — so this does not add information, and a map taken from it
         * lands exactly where a map from the old pair would. What it changes
         * is WHERE a later K correction pivots: the error of a map is the K
         * error times the distance from the pair, and the pair is now as
         * close as it can be. Not when the target was out of reach: a clamped
         * code is not the partner of anything. */
        if (reach) {
            s_st.pair[a] = span_round16(c_a);
            s_st.pair[b] = span_round16(c_b);
        }
    } else if (ka && kb) {
        OUT_SERIAL.println("SPAN: no code pair known for these two positions yet - code kept,");
        OUT_SERIAL.println("SPAN: the loop re-acquires (the pair is learned when it locks)");
    }

    s_active  = b;
    s_st.last = (uint8_t)(b + 1u);
    span_ct_arm(false);

    if (kb) {
        span_apply_k(b);
        if (ka) algo_rescale_lsb((double)s_st.k[a] / (double)s_st.k[b]);
        OUT_SERIAL.print("SPAN: K "); OUT_SERIAL.print((double)s_st.k[b] * 1000.0, 5);
        OUT_SERIAL.print(" mHz/LSB (CT in ");
        OUT_SERIAL.print(span_name(b));
        OUT_SERIAL.println(")");
        span_warn_manual();
    } else if (ka) {
        /* The live coefficients stay the OTHER span's — several times too
         * much or too little gain for this one, which in simulation cost
         * noise and not stability (see SPAN_CT_TRIES for why the loop is not
         * held). CT is what supplies the rest; span_poll starts it. */
        span_ct_arm(true);
        OUT_SERIAL.print("SPAN: no calibration for ");
        OUT_SERIAL.print(span_name(b));
        OUT_SERIAL.print(" yet - the loop runs on ");
        OUT_SERIAL.print(span_name(a));
        OUT_SERIAL.println("'s until CT has measured it");
    }
    /* Neither span calibrated: the board has never run CT, and it behaves as
     * it always has — defaults, until the operator runs CT at setup. */

    if (at_boot) {
        gCtrl.pwm_output = span_round16(c_b);   /* the sketch writes it next */
        s_need_save = true;
        return;
    }

    gpsdo_dac_write16f(c_b);
    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gCtrl.pwm_output = gpsdo_dac_last16();
        xSemaphoreGive(xCtrlMutex);
    }
    /* The same three things CT does after a coarse move, for the same
     * reasons: the averages describe the span just left, the statistics
     * would count the move as a correction, and every loop's integrator is
     * in the old span's units. */
    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }
    health_reset();
    algo_request_restart();

    if (!settings_save_partial(SET_SPAN))
        OUT_SERIAL.println("SPAN: auto-save FAILED - run 'ES' to keep the span state");
    live_store_request_save();
}
#endif /* GPSDO_SPAN_SENSE */

/* ---- boot ---------------------------------------------------------------- */
void span_begin(void)
{
#ifdef GPSDO_SPAN_SENSE
    pinMode(PIN_SPAN_SENSE, INPUT_PULLUP);
    delayMicroseconds(100);          /* let the pull-up charge the wire */
    const uint8_t now = span_read_vote();
    s_raw_last = now;

    if (s_st.last == 0u) {
        /* FIRST BOOT WITH SPAN SENSING. The board may already have a CT
         * calibration from before this existed, and it was measured in the
         * position the jumper is in now — there is no other position it could
         * have been measured in. Only a real settings record counts: a board
         * with none is running on compile-time defaults, which are nobody's
         * measurement and must not become a span's calibration. */
        s_active = now;
        if (g_persist_valid && g_pid[7].Kp > 100.0) {
            s_st.k[now] = (float)(0.40 / g_pid[7].Kp);
            OUT_SERIAL.print("SPAN: first run with span sensing - the existing CT calibration is ");
            OUT_SERIAL.println(span_name(now));
        }
        s_st.last   = (uint8_t)(now + 1u);
        s_need_save = true;
    } else if ((uint8_t)(s_st.last - 1u) != now) {
        do_switch((uint8_t)(s_st.last - 1u), now, true);
    } else {
        s_active = now;
        /* Powered off before CT succeeded here, powered on in the same
         * position: CT is still what is missing, and a power-on is a new
         * set of attempts. */
        if (span_needs_ct()) {
            span_ct_arm(true);
            OUT_SERIAL.print("SPAN: no calibration for ");
            OUT_SERIAL.print(span_name(now));
            OUT_SERIAL.print(" yet - the loop runs on ");
            OUT_SERIAL.print(span_name(now ^ 1u));
            OUT_SERIAL.println("'s until CT has measured it");
        }
    }
#else
    /* Passive: nothing is sensed, the stored state is only carried. */
    s_active = (s_st.last >= 1u) ? (uint8_t)(s_st.last - 1u) : SPAN_FULL;
#endif
}

/* ---- control task --------------------------------------------------------- */
void span_poll(void)
{
#ifdef GPSDO_SPAN_SENSE
    if (s_need_save) {
        s_need_save = false;
        if (!settings_save_partial(SET_SPAN))
            OUT_SERIAL.println("SPAN: auto-save FAILED - run 'ES' to keep the span state");
        live_store_request_save();
    }

    /* The anchor completes when the new span's loop says it is locked: its
     * code is then that span's null, the old span's code at the move was the
     * old span's null, and two nulls of one oscillator are one voltage.
     *
     * LOCKED AGAIN, NOT STILL LOCKED. The verdict the loop held before the
     * move survives until the loop's first step after it — algorithm 10's
     * LOCK is even persisted — so a lock seen on the very next pass is the old
     * span's, and the first version of this took exactly that: it recorded the
     * unmoved code as the new span's null, and the next move mapped from a
     * pair that was not one (spansim caught it: a REDUCED->FULL->REDUCED
     * sequence landing 45 000 codes off the rail). So the new span's loop has
     * to be seen unlocked first, and then earn its lock. */
    if (s_anc_armed) {
        if ((uint32_t)(millis() - s_anc_ms) > SPAN_ANCHOR_MAX_MS ||
            s_anc_from == s_active) {
            s_anc_armed = false;
        } else if (!health_loop_locked()) {
            s_anc_unlocked = true;
        } else if (s_anc_unlocked && !g_calib_active) {
            s_anc_armed = false;
            uint16_t c = gpsdo_dac_last16();
            s_st.pair[s_anc_from] = s_anc_code;
            s_st.pair[s_active]   = c;
            OUT_SERIAL.print("SPAN: pair measured - ");
            OUT_SERIAL.print(span_name(s_anc_from)); OUT_SERIAL.print(" ");
            OUT_SERIAL.print(s_anc_code); OUT_SERIAL.print(" = ");
            OUT_SERIAL.print(span_name(s_active));   OUT_SERIAL.print(" ");
            OUT_SERIAL.println(c);
            settings_save_partial(SET_SPAN);
        }
    }

    const uint8_t raw = span_read_raw();
    if (raw == s_active) {
        s_raw_run = 0u; s_raw_last = raw;
        if (span_needs_ct()) span_ct_auto();
        return;
    }
    if (raw != s_raw_last) { s_raw_last = raw; s_raw_run = 1u; return; }
    if (++s_raw_run < SPAN_DEBOUNCE_N) return;
    s_raw_run = 0u;
    do_switch(s_active, raw, false);
#endif
}

/* ---- CT --------------------------------------------------------------------- */
void span_ct_done(double k, uint16_t null16)
{
#ifdef GPSDO_SPAN_SENSE
    const uint8_t s = s_active;
    const uint8_t o = (uint8_t)(s ^ 1u);

    /* The other position's K looks like this one: see SPAN_SAME_PLANT. */
    if (s_st.k[o] > 0.0f) {
        double r = k / (double)s_st.k[o];
        if (r < SPAN_SAME_PLANT && r > 1.0 / SPAN_SAME_PLANT) {
            OUT_SERIAL.print("SPAN: ** CT in "); OUT_SERIAL.print(span_name(s));
            OUT_SERIAL.print(" measured the same plant as ");
            OUT_SERIAL.print(span_name(o));
            OUT_SERIAL.print(" (ratio "); OUT_SERIAL.print(r, 2); OUT_SERIAL.println(").");
            OUT_SERIAL.print("SPAN:    Treating "); OUT_SERIAL.print(span_name(o));
            OUT_SERIAL.println(" as uncalibrated - it was measured in this position,");
            OUT_SERIAL.println("SPAN:    or the jumper does not switch the span. It gets its own CT when the jumper goes there.");
            s_st.k[o]    = 0.0f;
            s_st.pair[o] = 0u;
            s_st.pair[s] = 0u;
            s_anc_armed  = false;
        }
    }

    /* Keep the pair pinned to one voltage across the CT. What CT moved in THIS
     * span is aging (or a better null), and aging is common to both: carried
     * into the other span through the new K, exactly as a move would carry
     * it. If the move that brought us here left an anchor — the old span was
     * locked, and CT has just found this span's null — that IS a measured
     * pair, and it wins. */
    if (s_anc_armed && s_anc_from == o &&
        (uint32_t)(millis() - s_anc_ms) <= SPAN_ANCHOR_MAX_MS) {
        s_st.pair[o] = s_anc_code;
        s_st.pair[s] = null16;
        s_anc_armed  = false;
        OUT_SERIAL.println("SPAN: pair measured by CT");
    } else if (s_st.pair[s] != 0u && s_st.pair[o] != 0u && s_st.k[o] > 0.0f) {
        double p_o = (double)s_st.pair[o] +
                     (k / (double)s_st.k[o]) * ((double)null16 - (double)s_st.pair[s]);
        if (p_o >= 1.0 && p_o <= 65535.0) {
            s_st.pair[o] = span_round16(p_o);
            s_st.pair[s] = null16;
        } else {
            s_st.pair[o] = 0u; s_st.pair[s] = 0u;
        }
    }

    s_st.k[s]  = (float)k;
    s_st.last  = (uint8_t)(s + 1u);
#ifdef GPSDO_LTIC
    ltic_autotune();
#endif
    /* No loop restart here. CT has already asked for one before calling
     * this (do_calibrate_tune, where the reason is), in every build: build 55
     * had it here, where a build without GPSDO_SPAN_SENSE compiled it away. */
    if (s_ct_due || s_ct_fails > 0u) {
        OUT_SERIAL.print("SPAN: "); OUT_SERIAL.print(span_name(s));
        OUT_SERIAL.println(" has its own calibration now");
    }
    span_ct_arm(false);
    if (!settings_save_partial(SET_SPAN))
        OUT_SERIAL.println("SPAN: auto-save FAILED - run 'ES' to keep the span state");
#else
    (void)k; (void)null16;
#endif
}

void span_clear(uint8_t s)
{
    if (s > 1u) return;
    s_st.k[s]    = 0.0f;
    s_st.pair[0] = 0u;
    s_st.pair[1] = 0u;
    s_anc_armed  = false;
    /* The STORED calibration only. The live coefficients stay as they are, so
     * clearing the position the board is in does not disturb a running loop,
     * and it starts no CT: the operator who cleared it types CT, or the next
     * boot or move into it runs one, exactly as for a position that was never
     * calibrated. The case this exists for is the other one — a position
     * calibrated in the wrong place (see SPAN_SAME_PLANT) — and there nothing
     * live is affected at all. */
    span_ct_arm(false);
    settings_save_partial(SET_SPAN);
}

void span_report(void (*putln)(const char *))
{
    char l[96], b0[16], b1[16];
    if (!span_sense_compiled()) {
        putln("  span: sensing not compiled (GPSDO_SPAN_SENSE) - one calibration");
        return;
    }
    snprintf(l, sizeof(l), "  span: %s (PB14 %s)", span_name(s_active),
             s_active == SPAN_REDUCED ? "low" : "high");
    putln(l);
    for (uint8_t i = 0; i < 2; i++) {
        if (s_st.k[i] > 0.0f) {
            /* dtostrf and not %f: the core's printf is built without floats */
            dtostrf((double)s_st.k[i] * 1000.0, -1, 5, b0);
            snprintf(l, sizeof(l), "        %-7s K %s mHz/LSB = %s LSB/Hz",
                     span_name(i), b0,
                     dtostrf(1.0 / (double)s_st.k[i], -1, 0, b1));
        } else if (i == s_active && span_needs_ct()) {
            char st[48];
            if (s_ct_seen) {
                snprintf(st, sizeof(st), "CT requested");
            } else if (s_ct_due && s_ct_wait_why != NULL) {
                snprintf(st, sizeof(st), "CT waits for %s", s_ct_wait_why);
            } else if (s_ct_due && s_ct_fails > 0u) {
                uint32_t el   = (uint32_t)(millis() - s_ct_fail_ms);
                uint32_t w    = span_ct_wait_ms();
                uint32_t left = (el < w) ? (w - el + 59999UL) / 60000UL : 0u;
                snprintf(st, sizeof(st), "CT failed %u of %u, next in %lu min",
                         (unsigned)s_ct_fails, (unsigned)SPAN_CT_TRIES, (unsigned long)left);
            } else if (s_ct_due) {
                snprintf(st, sizeof(st), "CT pending");
            } else if (s_ct_fails >= SPAN_CT_TRIES) {
                snprintf(st, sizeof(st), "CT failed %u times, type CT", (unsigned)s_ct_fails);
            } else {
                snprintf(st, sizeof(st), "type CT to measure it");
            }
            snprintf(l, sizeof(l), "        %-7s not calibrated - %s", span_name(i), st);
        } else {
            snprintf(l, sizeof(l), "        %-7s not calibrated", span_name(i));
        }
        putln(l);
    }
    if (s_st.k[0] > 0.0f && s_st.k[1] > 0.0f) {
        dtostrf((double)s_st.k[0] / (double)s_st.k[1], -1, 2, b0);
        if (span_pair_valid())
            snprintf(l, sizeof(l), "        ratio %sx, pair FULL %u = REDUCED %u",
                     b0, (unsigned)s_st.pair[0], (unsigned)s_st.pair[1]);
        else
            snprintf(l, sizeof(l), "        ratio %sx, no code pair yet (moves re-acquire)", b0);
        putln(l);
    }
}
