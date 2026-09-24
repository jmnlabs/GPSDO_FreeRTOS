/* ======================================================================
 * gpsdo_ubx_timtp.h — UBX-TIM-TP sawtooth (quantization-error) correction
 *
 * Part of GPSDO v1.07.57rt
 *
 * See gpsdo_ubx_timtp.cpp for the full description. Passive UBX sniffer that
 * extracts qErr (quantization error) from UBX-TIM-TP and offers it as a
 * per-pulse correction to the TIC phase measurement. Works on LEA-6T,
 * LEA/NEO-M8T and ZED-F9T (qErr is I4 at payload offset 8, in ps, on all).
 *
 * PAIRING MODEL (v0.95 audit fix):
 * TIM-TP carries a `flags.mode` bit (byte 14, bit 3) that says whether qErr
 * describes the pulse AT towMS (mode=0, "this pulse") or the NEXT one after
 * towMS (mode=1, "next pulse" — the u-blox default for the TIMEPULSE output).
 *
 * The firmware pairs qErr to a pulse by ppscount (a monotonic per-PPS
 * counter, NOT GPS week/towMS). The sniffer records the ppscount that was
 * current when each TIM-TP arrived; the caller passes the ppscount of the
 * PPS it just sampled. A match returns qErr, a mismatch returns 0.
 *
 * Both modes reduce to the SAME test: ppscount == recorded_pps + 1.
 *   mode=1: TIM-TP(N) was sent before PPS N. When it arrives, recorded_pps
 *           is N-1, so it pairs with PPS N-1+1 = N — exactly the pulse the
 *           loop is about to process. Correct and on time.
 *   mode=0: TIM-TP(N) was sent after PPS N. When PPS N is sampled, only
 *           TIM-TP(N-1) is in hand (recorded_pps = N-1), describing the
 *           PREVIOUS pulse. qErr(N) cannot exist yet in this mode — it is
 *           physically impossible to correct PPS N on time. We accept a
 *           1-PPS lag (qErr(N-1) applied to PPS N): imperfect, since qErr
 *           drifts ~1 ns/s, but far better than the random-skew pairing
 *           the unguarded code did.
 * The mode bit is still captured and published (g_qerr_mode_next) so a
 * diagnostic can tell the user their receiver is in the lagging mode.
 * ====================================================================== */
#ifndef GPSDO_UBX_TIMTP_H
#define GPSDO_UBX_TIMTP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Published state (defined in gpsdo_ubx_timtp.cpp) */
extern volatile bool     g_qerr_enable;   /* master enable (SAW command)    */
extern volatile bool     g_qerr_valid;    /* fresh qErr available           */
extern volatile float    g_qerr_ns;       /* latest qErr [ns]               */
extern volatile uint32_t g_qerr_count;    /* total frames parsed            */
extern volatile uint32_t g_qerr_last_ms;  /* millis() of last valid frame   */

/* The TIM-TP `flags` byte (offset 14). Bit 3 = mode:
 *   0 = qErr describes THIS pulse (towMS)
 *   1 = qErr describes the NEXT pulse (the one after towMS)
 * The correction logic needs this to pair qErr with the right PPS. */
extern volatile uint8_t  g_qerr_flags;    /* raw flags byte from TIM-TP     */
extern volatile bool     g_qerr_mode_next;/* true if mode bit set (next PPS)*/

/* Feed one GPS-stream byte to the UBX sniffer (call beside gps.encode). */
void  ubx_timtp_feed(uint8_t b);

/* Age out stale qErr if TIM-TP stops arriving. Call ~1 Hz. */
void  ubx_timtp_tick(uint32_t now_ms);

/* Correction to SUBTRACT from measured TIC phase [ns] (0 if off/no data).
 * This is the LEGACY interface: returns the latest qErr regardless of which
 * PPS it describes. Kept for the display path, where a ~1 PPS skew is
 * visually immaterial. The loop should use ubx_timtp_correction_for_pps(). */
float ubx_timtp_correction_ns(void);

/* Correction for a SPECIFIC PPS, identified by its ppscount. Returns 0 if
 * the latest TIM-TP does not describe that exact pulse (mode-aware + age).
 * This is the authoritative path the phase loop should use: it closes the
 * race between vGpsTask publishing qErr and vControlTask reading it, because
 * a qErr that hasn't arrived yet (or arrived for the wrong pulse) yields 0
 * instead of a stale value off by one PPS. */
/* Pairing diagnostics for the "which pulse does qErr describe?" question.
 * UBX-13003221-R15 p.66: the TIM-TP sent after pulse N-1 describes pulse N.
 * These count how often the paired lookup actually found its pulse, so the
 * assumption can be CHECKED on any receiver instead of trusted. Reported by
 * the SAW command together with the mode bit the receiver itself sets. */
extern volatile uint32_t g_qerr_paired;    /* lookups that matched a pulse   */
extern volatile uint32_t g_qerr_unpaired;  /* lookups with no matching pulse */
/* AND BY HOW MUCH THEY MISS, which is the number that repairs the rule rather
 * than merely reporting that it is broken. Bucketed on (ppscount - the pulse
 * the held qErr was recorded against): [<=-1, 0, +1, +2, +3, >=+4]. The rule
 * in force accepts +1 only. SAW prints the whole row. */
#define QERR_LAG_BINS 6u
extern volatile uint32_t g_qerr_lag[QERR_LAG_BINS];
extern volatile uint32_t g_qerr_latchlag[QERR_LAG_BINS];

/* Latch the sawtooth for the pulse whose ramp has just been sampled.
 *
 * THE DISPLAY PATH WAS ALWAYS THE CORRECT ONE and the loop's was not. Measured
 * on 34 240 seconds of the 01/02.09 capture, taking the raw phase and putting
 * the sawtooth back in different places:
 *
 *     no correction at all ............ first-difference floor 7.57 ns
 *     qErr latched at this pulse ...... 2.55 ns
 *     qErr shifted by one pulse ....... 13.25 ns
 *     qErr shifted by two ............. 9.30 ns
 *
 * So the value latched at the ramp sample is right, everything else is worse
 * than nothing, and there is no argument left about pairing models: the loop
 * should use the number the sampler already holds. This records it with the
 * pulse it belongs to so the loop can ask for exactly that one. */
void ubx_timtp_latch(uint32_t ppscount);
float ubx_timtp_correction_for_pps(uint32_t ppscount);

#ifdef __cplusplus
}
#endif

#endif /* GPSDO_UBX_TIMTP_H */
