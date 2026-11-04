/* ======================================================================
 * gpsdo_span.cpp  —  two EFC spans, two calibrations, one jumper on PB14
 *
 * Part of GPSDO v1.08.65rt
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
 * PB14 is the pin the audit in gpsdo_dac_ext.h finds free in every
 * configuration — including as the place to move the AD5680's chip
 * select if a board wants the TM1637 back. Both cannot happen:
 * INPUT_PULLUP here would take the pin away from the DAC, and the DAC's
 * select pulses would read as a jumper. Caught at compile time rather
 * than on a bench, for all three DAC pins because all three can be
 * overridden. */
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
 * 1.5 is a guard band, not a tuning: CT's own run-to-run scatter on a
 * reduced span was 14 % on a V3 board (46711 and 40873 LSB/Hz), and any
 * reduction worth building is several times — 3.3-3.6x measured on one
 * reference. Below 2x a reduced span buys less than CT's own scatter, so
 * a board where the two positions really differ by less than 1.5 is not
 * a configuration this has to serve. */
#define SPAN_SAME_PLANT     1.5

/* A pair of codes is only a measurement if both halves describe the same
 * oscillator: the code the loop in the old span held when it was switched
 * away, and the code the loop in the new span settles on. Between the two
 * the oscillator ages; three hours at the 1.7e-10/day a V3 board showed is
 * 2e-11, a few codes, and after that the anchor is dropped rather than
 * trusted. */
#define SPAN_ANCHOR_MAX_MS  (3UL * 3600UL * 1000UL)

/* WHEN A LOCKED LOOP'S CODE IS NOT A NULL.
 *
 * The anchor takes the code the old span's loop holds at the move as that
 * span's null, on the strength of the loop saying it is locked. Testing on
 * a V3 prototype exposed three ways that is not enough — the first two by
 * what was being done there (DMM readings in holdover, codes typed with
 * SP), the third by the two false pairs it actually recorded:
 *
 *   HOLDOVER. The loop is not steering, and a loop that does not run does
 *   not update its lock flag either: whatever it said when holdover began —
 *   locked, typically — it goes on saying. The code is whatever was held, or
 *   typed. No anchor is taken in holdover, manual or automatic, and none
 *   completes in it.
 *
 *   A CODE SET BY HAND. SP, up and dp write the DAC under a loop whose lock
 *   flag has not noticed yet — for a second or so when it is running, for as
 *   long as the holdover lasts when it is not, and for good after a nudge too
 *   small to unlock it. After one (span_note_manual) no anchor is taken, and
 *   none completes, until the loop has been seen unlocked: a lock after that
 *   is the loop's own again.
 *
 *   THE WRONG PLANT. On a hand-wired bench the shifter can go into or out of
 *   the EFC path before PB14 follows. The loop then locks on one position's
 *   plant under the other's name, and its code is the OTHER position's null.
 *   Both false pairs recorded on that board came from this: FULL 54295 =
 *   REDUCED 54156, taken with the shifter already out of the path and PB14
 *   still on REDUCED, and FULL 38660 = REDUCED 39051, with the shifter back
 *   in before PB14 left FULL.
 *
 * The last one is recognised by where the code is. CT stores the code it found
 * 10 MHz at in each position (span_store_t.null), and a code is taken as
 * position x's null only if it lies within SPAN_NULL_TOL of x's null and not
 * nearer to the other position's. Both distances are in hertz, each through
 * its own position's K, because that is the question being asked: how far
 * would the oscillator have had to move for this to be x's null, and how far
 * for it to be the other's? 38660 as FULL is 1.37 Hz from FULL's null and
 * 0.01 Hz from where REDUCED locked; 54156 as REDUCED is about 0.4 Hz from
 * anything REDUCED locks at and 0.01 Hz from FULL's null. The first fails both
 * tests, the second the nearer one. Both halves of a pair are tested, and the
 * old span's half again when the new span's null is known — which is how the
 * second is caught when FULL has no null of its own until the CT that
 * completes the pair.
 *
 * 5e-8 (0.5 Hz at 10 MHz) is the backstop for a code far from both nulls, and
 * what the oscillator really does between two renewals of a null has to fit
 * inside it: the 1.7e-10/day of aging a V3 board showed takes ten months. The
 * nearer test is the sharper one and has a price: an oscillator that has moved
 * more than halfway, in hertz, towards the other position's null since the
 * last renewal is taken for the wrong plant — about 3e-8 on that board, whose
 * two nulls are 15 000 codes apart. What that costs is a pair not measured at
 * that move, not a wrong one: the pair already held is voltage-true and is
 * carried as before, and CT in that position renews the null.
 *
 * SPAN_NULL_SURE: within 5e-9 of its own null a code is taken as that null,
 * whatever else it is near. Without it the nearer test cannot work on a board
 * whose two nulls sit at nearly the same code — where the shifter's line
 * crosses the full span's at the oscillator's null — because there every code
 * a hair from its own null is nearer the other's, in hertz, for the position
 * with the larger K. And it costs nothing: if such a code were really the
 * other plant's null, the pair taken on it would put the next move no more
 * than those 5e-9 off, since that is how far the code is from this position's
 * null, which is where the voltage it claims really is.
 *
 * Nulls come from CT and only from CT; a measured pair renews a null it has
 * just passed the test against, and never creates one. A record written before
 * build 58 has none, and in a position without one a locked code is taken as
 * it always was — until the next CT there. */
#define SPAN_NULL_TOL       5.0e-8
#define SPAN_NULL_SURE      5.0e-9

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
/* a code was set by hand and the loop has not been seen unlocked since (the
 * CLI task sets it, the control task clears it) */
static volatile bool s_manual = false;
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
 * nulls of the same oscillator are the same voltage — provided both codes are
 * nulls, which a lock flag alone does not prove (SPAN_NULL_TOL). The error of
 * a map is the error of the K ratio times the distance from the pair, which is
 * why keeping the pair close to where the loop actually is matters more than
 * anything else here. */
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

/* Holdover, manual or automatic: the loop is not steering, so the code on the
 * DAC is not a null whatever the lock flag says (SPAN_NULL_TOL). Not knowing —
 * the mutex was not had — counts as holdover: a wrong "yes" costs one pair
 * not measured, a wrong "no" a pair that is not one. Control task only, and
 * never at boot (no mutex yet). */
static bool span_in_holdover(void)
{
    bool ho = true;
    if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ho = gCtrl.holdover_mode;
        xSemaphoreGive(xCtrlMutex);
    }
    return ho;
}

/* Can c, the code a loop locked in position x holds, be x's null? nx is x's
 * null and ny the other position's, 0 = not known — the callers pass the
 * stored ones, or a null just measured in place of a stored one. The test is
 * SPAN_NULL_TOL's: within SPAN_NULL_SURE of nx, or within the tolerance of nx
 * and not nearer ny, each distance in hertz through its own position's K.
 * Without nx there is nothing to test against and the answer is yes, as it
 * always was. Says why not. */
static bool span_null_fits(uint8_t x, double c, uint16_t nx, uint16_t ny)
{
    const uint8_t y = (uint8_t)(x ^ 1u);
    if (nx == 0u || !(s_st.k[x] > 0.0f)) return true;
    const double dx   = fabs(c - (double)nx) * (double)s_st.k[x];
    const bool   hasy = ny != 0u && s_st.k[y] > 0.0f;
    const double dy   = hasy ? fabs(c - (double)ny) * (double)s_st.k[y] : 0.0;
    if (dx <= SPAN_NULL_SURE * (double)BASE_FREQ) return true;
    if (dx <= SPAN_NULL_TOL * (double)BASE_FREQ && !(hasy && dy < dx)) return true;
    OUT_SERIAL.print("SPAN: ");            OUT_SERIAL.print(span_name(x));
    OUT_SERIAL.print(" code ");            OUT_SERIAL.print((unsigned)span_round16(c));
    OUT_SERIAL.print(" is ");              OUT_SERIAL.print(dx, 3);
    OUT_SERIAL.print(" Hz from its null "); OUT_SERIAL.print((unsigned)nx);
    if (hasy) {
        OUT_SERIAL.print(", ");            OUT_SERIAL.print(dy, 3);
        OUT_SERIAL.print(" Hz from ");     OUT_SERIAL.print(span_name(y));
        OUT_SERIAL.print("'s ");           OUT_SERIAL.print((unsigned)ny);
    }
    OUT_SERIAL.println(" - not a null");
    OUT_SERIAL.print("SPAN:    (the shifter and PB14 did not switch together, or the null is old - CT in ");
    OUT_SERIAL.print(span_name(x));
    OUT_SERIAL.println(" renews it)");
    return false;
}
#endif /* GPSDO_SPAN_SENSE */

void span_note_manual(void)
{
#ifdef GPSDO_SPAN_SENSE
    s_manual = true;
#endif
}

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
/* At boot: the span the recalled code is in (span_begin sets it from the
 * live store's slot, build 65). */
static uint8_t s_boot_code = 0xFFu;

static void do_switch(uint8_t a, uint8_t b, bool at_boot)
{
    const bool   ka   = s_st.k[a] > 0.0f;
    const bool   kb   = s_st.k[b] > 0.0f;
    const bool   pair = span_pair_valid();
    const double c_a  = at_boot ? (double)gCtrl.pwm_output : gpsdo_dac_last16f();
    /* THE CODE MAY ALREADY BE b's (build 65). At boot the span record says
     * a, but the live store's slot - written after it, the record's own save
     * having failed - says its code is b's. Then it is the record that lagged
     * behind: the code stays as it is, and so does the learned state stored
     * with it. Remapping it again would move the oscillator by the whole
     * difference between the two paths. */
    const bool   own  = at_boot && s_boot_code == b;
    double       c_b  = c_a;
    bool         mapped = false, reach = true;

    if (!own && ka && kb && pair) {
        c_b    = span_map(a, b, c_a);
        mapped = true;
        if (c_b < SPAN_RAIL_GUARD || c_b > 65535.0 - SPAN_RAIL_GUARD) reach = false;
    }

    OUT_SERIAL.print("SPAN: "); OUT_SERIAL.print(span_name(a));
    OUT_SERIAL.print(" -> ");   OUT_SERIAL.print(span_name(b));
    OUT_SERIAL.print(b == SPAN_REDUCED ? " (PB14 low" : " (PB14 high");
    OUT_SERIAL.println(at_boot ? ", moved while powered off)" : ")");

    /* Anchor candidate for the pair: only a LOCKED loop's code is a null, and
     * only a null in each span makes a measured pair. Never at boot — no loop
     * has run yet, so nothing is known to be locked. A new move cancels an old
     * candidate: it described a span we have just left. And a lock flag alone
     * does not make a code a null — not in holdover, not after a code set by
     * hand, not where the other position's plant would have put it
     * (SPAN_NULL_TOL). Each is said: a pair the operator expected and did not
     * get is otherwise a mystery.
     *
     * Armed whether or not the new span is calibrated: when it is not, CT is
     * about to run there, and CT's null is the other half (span_ct_done). */
    s_anc_armed = false;
    if (!at_boot && ka && health_loop_locked()) {
        if (span_in_holdover()) {
            OUT_SERIAL.println("SPAN: in holdover - the code is not a null; no pair is measured at this move");
        } else if (s_manual) {
            OUT_SERIAL.println("SPAN: the code was set by hand (SP/up/dp) - not a null; no pair is measured at this move");
        } else if (!span_null_fits(a, c_a, s_st.null[a], s_st.null[b])) {
            OUT_SERIAL.println("SPAN: no pair is measured at this move");
        } else {
            s_anc_armed    = true;
            s_anc_unlocked = false;
            s_anc_from     = a;
            s_anc_code     = span_round16(c_a);
            s_anc_ms       = millis();
        }
    }

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
    } else if (own) {
        OUT_SERIAL.print("SPAN: the stored code is already ");
        OUT_SERIAL.print(span_name(b));
        OUT_SERIAL.println("'s (the live store saved it after the move) - kept");
    } else if (ka && kb) {
        OUT_SERIAL.println("SPAN: no code pair known for these two positions yet - code kept,");
        OUT_SERIAL.println("SPAN: the loop re-acquires (the pair is learned when it locks)");
    }

    s_active  = b;
    s_st.last = (uint8_t)(b + 1u);
    span_ct_arm(false);

    if (kb) {
        span_apply_k(b);
        if (ka && !own) algo_rescale_lsb((double)s_st.k[a] / (double)s_st.k[b]);
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

#ifdef GPSDO_SPAN_SENSE
/* ---- boot: a code filed under the wrong span (build 65) --------------------
 *
 * The jumper is where the span record says, but the live store's slot says
 * its code is the OTHER span's. That is a move the record reached flash for
 * and the live slot did not: do_switch() saves the record at once and asks
 * the live store for its slot on the next tick, and a power cut between the
 * two leaves the old span's code to be recalled - up to build 64 straight
 * onto the new span's path. Remapped here exactly as do_switch() would have,
 * and both records written again by the first span_poll(). */
static void span_boot_remap(uint8_t a, uint8_t b)
{
    const double c_a = (double)gCtrl.pwm_output;
    OUT_SERIAL.print("SPAN: the stored code is ");
    OUT_SERIAL.print(span_name(a));
    OUT_SERIAL.print("'s - the span record moved to ");
    OUT_SERIAL.print(span_name(b));
    OUT_SERIAL.println(" and the live store had not followed");
    const bool kk = s_st.k[a] > 0.0f && s_st.k[b] > 0.0f;
    if (kk && span_pair_valid()) {
        const double c_b = span_map(a, b, c_a);
        OUT_SERIAL.print("SPAN: code "); OUT_SERIAL.print(c_a, 1);
        OUT_SERIAL.print(" -> ");        OUT_SERIAL.print(c_b, 1);
        OUT_SERIAL.println("  (same voltage on the EFC pin)");
        if (c_b < SPAN_RAIL_GUARD || c_b > 65535.0 - SPAN_RAIL_GUARD)
            OUT_SERIAL.println("SPAN: ** the oscillator is outside the reach of this span.");
        gCtrl.pwm_output = span_round16(c_b);
    } else {
        OUT_SERIAL.println("SPAN: no code pair known for these two positions yet - code kept,");
        OUT_SERIAL.println("SPAN: the loop re-acquires (the pair is learned when it locks)");
    }
    /* The learned state was stored with a's code, in a's LSB, whether or not
     * the code itself could be mapped: rescaled whenever both K are known, as
     * do_switch() does. */
    if (kk) algo_rescale_lsb((double)s_st.k[a] / (double)s_st.k[b]);
    s_need_save = true;
}
#endif

/* ---- boot ---------------------------------------------------------------- */
void span_begin(void)
{
#ifdef GPSDO_SPAN_SENSE
    pinMode(PIN_SPAN_SENSE, INPUT_PULLUP);
    delayMicroseconds(100);          /* let the pull-up charge the wire */
    const uint8_t now = span_read_vote();
    s_raw_last = now;

    /* Which span the recalled code is in: the live store's slot says, from
     * build 65; a slot from before says nothing, and the span record's is
     * taken, as every build before did. */
    const uint8_t rec  = (s_st.last >= 1u) ? (uint8_t)(s_st.last - 1u) : now;
    uint8_t       code = live_store_code_span();
    if (code > SPAN_REDUCED) code = rec;

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
    } else if (rec != now) {
        s_boot_code = code;
        do_switch(rec, now, true);
    } else {
        s_active = now;
        if (code != now) span_boot_remap(code, now);
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
     * to be seen unlocked first, and then earn its lock.
     *
     * AND BOTH HALVES ARE NULLS (SPAN_NULL_TOL). Not in holdover, not on a
     * code set by hand; the anchor is tested again now that the new span's
     * null is in hand — this lock — and this lock is tested against the stored
     * nulls, which catches the mirror of what the anchor test catches: the
     * jumper moved first and the shifter later, so the new span's loop locked
     * on the old span's plant. Where the old position has no null of its own
     * yet, the anchor stands in for it: it is the old plant's null either way.
     * A null renewed here is one the pair has just passed the test against.
     *
     * The code is read BEFORE the manual flag is looked at. SP sets the flag
     * before it writes the DAC, so a code read first and a flag found clear
     * after it is a code no SP had touched. */
    if (s_manual && !health_loop_locked()) s_manual = false;   /* a code set by hand */

    if (s_anc_armed) {
        if ((uint32_t)(millis() - s_anc_ms) > SPAN_ANCHOR_MAX_MS ||
            s_anc_from == s_active) {
            s_anc_armed = false;
        } else if (!health_loop_locked()) {
            s_anc_unlocked = true;
        } else if (s_anc_unlocked && !g_calib_active) {
            const uint16_t c = gpsdo_dac_last16();
            /* in holdover, or after a code set by hand: wait (see above) */
            if (!span_in_holdover() && !s_manual) {
                s_anc_armed = false;
                const uint8_t  a  = s_anc_from, b = s_active;
                const uint16_t na = s_st.null[a] ? s_st.null[a] : s_anc_code;
                if (span_null_fits(a, (double)s_anc_code, s_st.null[a], c) &&
                    span_null_fits(b, (double)c, s_st.null[b], na)) {
                    s_st.pair[a] = s_anc_code;
                    s_st.pair[b] = c;
                    if (s_st.null[a] != 0u) s_st.null[a] = s_anc_code;
                    if (s_st.null[b] != 0u) s_st.null[b] = c;
                    OUT_SERIAL.print("SPAN: pair measured - ");
                    OUT_SERIAL.print(span_name(a)); OUT_SERIAL.print(" ");
                    OUT_SERIAL.print(s_anc_code);   OUT_SERIAL.print(" = ");
                    OUT_SERIAL.print(span_name(b)); OUT_SERIAL.print(" ");
                    OUT_SERIAL.println(c);
                    settings_save_partial(SET_SPAN);
                } else {
                    OUT_SERIAL.println(span_pair_valid()
                        ? "SPAN: no pair is measured - the pair already held stays"
                        : "SPAN: no pair is measured");
                }
            }
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
            s_st.null[o] = 0u;
            s_anc_armed  = false;
        }
    }
    /* This position's K from here on — before the anchor test below, which
     * weighs a distance in this position's codes by it. The re-pin uses k
     * itself either way. */
    s_st.k[s] = (float)k;

    /* Keep the pair pinned to one voltage across the CT. What CT moved in THIS
     * span is aging (or a better null), and aging is common to both: carried
     * into the other span through the new K, exactly as a move would carry
     * it. If the move that brought us here left an anchor — the old span was
     * locked, and CT has just found this span's null — that IS a measured
     * pair, and it wins: once the anchor has passed the null test again with
     * CT's null standing for this position's (SPAN_NULL_TOL — the test that
     * catches an anchor taken on this position's plant under the other's
     * name, when this position had no null of its own until now). */
    bool measured = false;
    if (s_anc_armed && s_anc_from == o &&
        (uint32_t)(millis() - s_anc_ms) <= SPAN_ANCHOR_MAX_MS) {
        s_anc_armed = false;
        if (span_null_fits(o, (double)s_anc_code, s_st.null[o], null16)) {
            s_st.pair[o] = s_anc_code;
            s_st.pair[s] = null16;
            if (s_st.null[o] != 0u) s_st.null[o] = s_anc_code;
            measured = true;
            OUT_SERIAL.println("SPAN: pair measured by CT");
        } else {
            OUT_SERIAL.println("SPAN: no pair is measured by CT");
        }
    }
    if (!measured && s_st.pair[s] != 0u && s_st.pair[o] != 0u && s_st.k[o] > 0.0f) {
        double p_o = (double)s_st.pair[o] +
                     (k / (double)s_st.k[o]) * ((double)null16 - (double)s_st.pair[s]);
        if (p_o >= 1.0 && p_o <= 65535.0) {
            s_st.pair[o] = span_round16(p_o);
            s_st.pair[s] = null16;
        } else {
            s_st.pair[o] = 0u; s_st.pair[s] = 0u;
        }
    }

    /* Where nulls come from: see SPAN_NULL_TOL. */
    s_st.null[s] = null16;
    s_st.last    = (uint8_t)(s + 1u);
#ifdef GPSDO_LTIC
    ltic_autotune();
#endif
    /* No loop restart here: CT has already asked for one before calling
     * this (do_calibrate_tune, where the reason is). */
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
    s_st.null[s] = 0u;
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
            /* dtostrf and not %f: the core's printf is built without floats.
             * The null is where CT found 10 MHz here, as the pairs measured
             * since have renewed it (SPAN_NULL_TOL). */
            dtostrf((double)s_st.k[i] * 1000.0, -1, 5, b0);
            int n = snprintf(l, sizeof(l), "        %-7s K %s mHz/LSB = %s LSB/Hz",
                             span_name(i), b0,
                             dtostrf(1.0 / (double)s_st.k[i], -1, 0, b1));
            if (s_st.null[i] != 0u && n > 0 && (size_t)n < sizeof(l))
                snprintf(l + n, sizeof(l) - (size_t)n, ", null %u", (unsigned)s_st.null[i]);
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

/* ---- span_settled (build 65) ----------------------------------------------- */
bool span_settled(void)
{
#ifdef GPSDO_SPAN_SENSE
    return !s_need_save && !s_anc_armed && !s_ct_due && s_raw_run == 0u;
#else
    return !s_ct_due;
#endif
}
