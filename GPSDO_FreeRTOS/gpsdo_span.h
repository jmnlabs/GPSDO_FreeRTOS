/* ======================================================================
 * gpsdo_span.h  —  two EFC spans, two calibrations, one jumper on PB14
 *
 * Part of GPSDO v1.07.57rt
 *
 * WHAT THIS IS FOR
 * ----------------
 * A board with the EFC level shifter can run its oscillator over the full
 * control span or over a reduced one. The reduced span divides every
 * voltage-domain error by the reduction ratio — Dan Wiering's prototype
 * measured 4.08:1 in the divider and ~5x overall — but it also narrows how far
 * the loop can reach, and after a cold start the oscillator's retrace can walk
 * a fifth of the reduced window in a day. So the board acquires on the full
 * span and runs on the reduced one, and the jumper (J16 on the V3 sheet) that
 * selects between them gets a second pole to PB14 so the firmware knows which
 * it is looking at.
 *
 * The two spans have different plants. CT measures K, the hertz per 16-bit
 * LSB, and everything the loops do is scaled by it; on Dan's board it moved
 * 5.0-5.7x between the two positions (8209 LSB/Hz full; 40873 and 46711
 * reduced, two CTs). On the other position's K a loop's gain is off by that
 * factor, and what that costs was simulated rather than assumed — loopsim,
 * the three measured plants, five seeds each, algorithms 10-13, the whole CT
 * coefficient set derived from a K 5x too large or 4.1-5.7x too small (at LTC
 * 100 and at 60): with a fifth of the right gain every loop was 2.2-6.4x worse
 * in phase sd; with 4-5.7x the right gain algorithm 11 was 3.6-6x BETTER, 13 from
 * 0.6x to 2.2x with 100-250 ns peaks, 12 three to seven times worse, and 10
 * fine at 4x but with 500-870 ns excursions in two runs of fifteen at 5.7x.
 * None diverged. Algorithms 3-7 could not be simulated this way (loopsim does
 * not drive them). Hence two calibrations, one per position, and this module
 * keeps the live coefficients on the right one.
 *
 * WHAT HAPPENS WHEN THE JUMPER MOVES (debounced, see span_poll):
 *
 *   - the live coefficients are re-derived from the new position's K, exactly
 *     as CT would derive them, and the learned LSB-denominated state (LRN
 *     feed-forward, algo 9 tempco) is rescaled by the ratio of the two K
 *   - the control code is REMAPPED so the EFC pin keeps the voltage it had —
 *     see span_map() for why that needs one pair of codes and not a model
 *   - the active loop is restarted exactly as on an algorithm change
 *   - if the new position has never been calibrated while the other has, the
 *     loop keeps running on the other position's coefficients — what the
 *     firmware always did with one calibration — and CT starts by itself as
 *     soon as there is a GPS fix, with three more attempts at growing
 *     intervals if it fails (SPAN_CT_TRIES in gpsdo_span.cpp, which also says
 *     why the loop is not held instead)
 *
 * PIN: PB14, INPUT_PULLUP. Jumper fitted = PB14 to ground = REDUCED. Open, or
 * no wire at all = FULL, which is the position that always works — a board
 * that has never heard of this jumper reads FULL and keeps its one calibration
 * as before.
 *
 * GPSDO_SPAN_SENSE (gpsdo_config.h) switches the sensing off. The stored
 * calibrations are still carried through every save either way, so a board
 * that is rebuilt without it and back again loses nothing.
 * ====================================================================== */
#ifndef GPSDO_SPAN_H
#define GPSDO_SPAN_H

#include <stdint.h>
#include <stdbool.h>

#define SPAN_FULL     0u
#define SPAN_REDUCED  1u

/* The persistent part, serialised by gpsdo_settings_store. Kept here rather
 * than in the settings block so the meaning of each field is written down
 * next to the code that uses it. */
typedef struct {
    uint8_t  last;      /* span in force when saved, +1; 0 = never recorded   */
    float    k[2];      /* Hz per 16-bit LSB measured by CT in each span;
                           0 = that span has no calibration of its own        */
    uint16_t pair[2];   /* two codes, one per span, that put the SAME voltage
                           on the EFC pin; 0 = no such pair known yet         */
} span_store_t;

void        span_store_get(span_store_t *out);
void        span_store_put(const span_store_t *in);

/* Boot: after settings_recall() and live_store_begin(), BEFORE the final DAC
 * write — so a jumper moved while the board was off is remapped before the
 * oscillator ever sees the stale code. No RTOS calls, no flash writes: those
 * are deferred to the first span_poll(). */
void        span_begin(void);

/* Control task, every pass (5 Hz). Debounces PB14 and performs a switch. */
void        span_poll(void);

uint8_t     span_active(void);                /* SPAN_FULL / SPAN_REDUCED     */
const char *span_name(uint8_t s);             /* "FULL" / "REDUCED"           */
bool        span_sense_compiled(void);

/* For CT: true if the jumper is not in the position `s` right now. Read
 * directly, not debounced — a calibration that straddled a jumper move is
 * discarded whatever the debounce would have concluded. */
bool        span_pin_moved_since(uint8_t s);

/* CT succeeded in the active span: store K, advance the pair, and save.
 * k_hz_per_lsb is CT's K; null16 the code it centred on. The loop restart
 * that follows a CT is CT's own (do_calibrate_tune), not this. */
void        span_ct_done(double k_hz_per_lsb, uint16_t null16);

/* SPAN CLR: forget one position's calibration (and the pair). */
void        span_clear(uint8_t s);

/* Human-readable state, one line per call of putln. For the SPAN command and
 * the DAC report. */
void        span_report(void (*putln)(const char *));

#endif /* GPSDO_SPAN_H */
