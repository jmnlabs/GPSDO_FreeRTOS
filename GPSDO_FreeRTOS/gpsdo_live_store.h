/* ======================================================================
 * gpsdo_live_store.h  —  live-data persistence (LRN + LC + PWM) over the ring
 *
 * Part of GPSDO v1.08.65rt
 *
 * Sits between the volatile "live" values (learned drift/damping, LC
 * calibration, last PWM) and the wear-levelled flash ring buffer. It packs
 * them into one 28-byte slot and applies hysteresis so the flash is written
 * only when a value has genuinely settled on a new level:
 *
 *   - drift changed by > 8 LSB  since last save, OR
 *   - damp  changed by > 0.03   since last save, OR
 *   - the PWM has moved > 0.02 Hz from the saved one (in LSB through CT's
 *     K; 64 LSB without a CT) while the loop has been locked for 20 min
 *     without a break (build 65, see below),
 *   AND at least 20 min have elapsed since the last save.
 *   - LC calibration change requests an immediate save (rare, user-driven).
 *
 * WHY THE PWM GOT ITS OWN REASON TO BE SAVED (build 65). Up to build 64 the
 * code was written only when one of the other values was, and algorithms
 * 11-13 never move LRN: on the author's board the ring held 42429 for weeks
 * while the loop ran at ~40960, so every cold start began 1469 LSB (0.47 Hz)
 * off the operating point - which is how build 64's first start came to ask
 * algorithm 13 for a long pull-in at all. A code only counts once the loop
 * has been locked for 20 minutes on end with nothing but the loop moving it:
 * no jump, no code typed with SP, up or dp however small, no stretch the
 * control task spent elsewhere (C, CT and LC run on it for minutes). Never
 * in holdover, and never while the span jumper's move is still being
 * settled (span_settled()). A write that fails is tried again 20 minutes
 * later, and a code that cannot be read is not written as the default.
 *
 * WHICH SPAN THE CODE BELONGS TO (build 65). A code means something only on
 * the span it was written in, and boot remaps a recalled code when the jumper
 * moved while the board was off (span_begin). Up to build 64 boot assumed the
 * code belonged to the span the SETTINGS record names. The two records are
 * written separately, though, so a power cut between them, or a settings
 * write that failed, left a code of one span filed under the other. The slot
 * now says which span its code is in, and boot remaps from that.
 *
 * Payload layout (28 bytes; build 64 and earlier wrote the first 27):
 *   [0..3]   lrn_drift    float
 *   [4..7]   lrn_damp     float
 *   [8..11]  ns_per_volt  float
 *   [12..15] zero_offset  float
 *   [16..19] range_ns     float
 *   [20..21] pwm          uint16
 *   [22..25] centre_v     float
 *   [26]     algo         uint8
 *   [27]     span         uint8  span the pwm code is in (SPAN_FULL/REDUCED)
 * A 27-byte slot written by an older build still loads; its code's span is
 * then unknown and boot treats it as before.
 * ====================================================================== */
#ifndef GPSDO_LIVE_STORE_H
#define GPSDO_LIVE_STORE_H

#include <stdint.h>
#include <stdbool.h>

/* Load the newest live payload from the ring (if any) into the globals /
 * g_ltic. Call once at boot AFTER flash_ring_begin(). Returns true if live
 * data was applied. */
bool live_store_begin(void);

/* The span the PWM code applied by live_store_begin() belongs to, as the slot
 * recorded it (SPAN_FULL / SPAN_REDUCED), or 0xFF when no slot was applied or
 * the slot predates build 65. For span_begin(). */
uint8_t live_store_code_span(void);

/* Periodic tick from the control task (once per second is plenty). Applies
 * hysteresis and writes a new ring slot when the criteria are met. now_ms is
 * a millisecond timebase (millis()). */
void live_store_tick(uint32_t now_ms);

/* Request an immediate save on the next tick (used after a successful LC
 * calibration). */
void live_store_request_save(void);

/* A code typed with SP, up or dp: call from the CLI before the code moves
 * (build 65). The PWM criterion's 20 minutes of lock start again. */
void live_store_note_manual(void);

#endif /* GPSDO_LIVE_STORE_H */
