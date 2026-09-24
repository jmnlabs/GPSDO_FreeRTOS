/* ======================================================================
 * gpsdo_ubx_timtp.cpp — UBX-TIM-TP sawtooth (quantization-error) correction
 *
 * Part of GPSDO v1.07.57rt
 *
 * u-blox timing receivers generate the 1PPS by dividing an internal
 * oscillator, so each pulse lands on a clock edge — up to one clock period
 * away from true GPS time. The receiver KNOWS this offset in advance and
 * reports it as `qErr` (quantization error, signed, picoseconds) in the
 * UBX-TIM-TP message. Subtracting qErr from the measured TIC phase removes
 * the receiver's granularity sawtooth, leaving the OCXO's own error.
 *
 *   LEA-6T : granularity 21 ns → ~±10 ns sawtooth, compensated → ~15 ns RMS
 *   LEA-M8T: finer, qErr a few ns
 *   ZED-F9T: finest
 *
 * Message format — UBX-TIM-TP (class 0x0D, id 0x01), 16-byte payload, is
 * IDENTICAL across u-blox 6/7/8/M8 and 9 (F9T) for the field we need:
 *
 *   off 0  U4 towMS      time pulse time of week [ms]
 *   off 4  U4 towSubMS   sub-ms part [2^-32 ms]
 *   off 8  I4 qErr       QUANTIZATION ERROR [ps]   ← the only field we use
 *   off 12 U2 week
 *   off 14 X1 flags
 *   off 15 X1 refInfo (M8/F9) / reserved (6)
 *
 * Because qErr sits at the same offset with the same type/units on every
 * supported receiver, ONE parser serves 6T, M8T and F9T.
 *
 * This is a passive sniffer: it watches the same byte stream vGpsTask
 * already reads (feeding TinyGPS++), runs a tiny UBX state machine in
 * parallel, and on a valid TIM-TP publishes qErr for the TIC path to use.
 * It never writes to the GPS except the one CFG-MSG that enables TIM-TP.
 * ====================================================================== */

#include "gpsdo_ubx_timtp.h"
#include "gpsdo_state.h"
#include <Arduino.h>

/* ---- published state (read by the TIC phase path) ---- */
volatile bool     g_qerr_enable   = false;   /* SAW command toggles this      */
volatile bool     g_qerr_valid    = false;   /* a fresh qErr has been seen    */
volatile float    g_qerr_ns       = 0.0f;    /* latest qErr [ns]              */
volatile uint32_t g_qerr_count    = 0;       /* total TIM-TP frames parsed    */
volatile uint32_t g_qerr_last_ms  = 0;       /* millis() of last valid frame  */
volatile uint8_t  g_qerr_flags    = 0;       /* raw flags byte (offset 14)    */
volatile uint32_t g_qerr_paired   = 0;      /* paired lookups that matched   */
volatile uint32_t g_qerr_unpaired = 0;      /* paired lookups that did not   */
volatile uint32_t g_qerr_lag[QERR_LAG_BINS] = {0};  /* and by how much, see .h */
volatile uint32_t g_qerr_latchlag[QERR_LAG_BINS] = {0};
static volatile uint32_t s_latch_pps = 0;   /* the pulse whose ramp was sampled */
static volatile float    s_latch_ns  = 0.0f;
static volatile bool     s_latch_ok  = false;
volatile bool     g_qerr_mode_next = false;  /* mode bit: 1=qErr for NEXT PPS */

/* ppscount captured at the moment this TIM-TP was decoded. vFreqRelayTask
 * owns the authoritative counter; gFreqSnap.ppscount is its display-safe
 * shadow. Reading it here (outside the mutex) is fine because ppscount only
 * grows by 1 per second and we only use it as a pairing key. */
static volatile uint32_t s_qerr_at_pps = 0;

/* ---- UBX sniffer state machine ---- */
enum {
    U_SYNC1 = 0,   /* waiting for 0xB5                */
    U_SYNC2,       /* waiting for 0x62               */
    U_CLASS,
    U_ID,
    U_LEN1,
    U_LEN2,
    U_PAYLOAD,
    U_CKA,
    U_CKB
};

static uint8_t  s_state = U_SYNC1;
static uint8_t  s_class = 0, s_id = 0;
static uint16_t s_len   = 0, s_idx = 0;
static uint8_t  s_cka   = 0, s_ckb = 0;       /* running Fletcher checksum     */
static uint8_t  s_rx_cka = 0;                 /* received checksum bytes       */

/* Only TIM-TP (16 bytes) needs buffering; ignore anything larger to stay
 * tiny and never contend with the NMEA parser's own buffers. */
#define UBX_TIMTP_LEN 16
static uint8_t  s_buf[UBX_TIMTP_LEN];

static inline void cksum_byte(uint8_t b) { s_cka = (uint8_t)(s_cka + b);
                                           s_ckb = (uint8_t)(s_ckb + s_cka); }

/* Feed ONE byte from the GPS stream. Cheap: most bytes are NMEA and fall
 * straight through U_SYNC1. Call from vGpsTask alongside gps.encode(c). */
void ubx_timtp_feed(uint8_t b)
{
    switch (s_state) {
    case U_SYNC1:
        if (b == 0xB5) s_state = U_SYNC2;
        break;

    case U_SYNC2:
        s_state = (b == 0x62) ? U_CLASS : U_SYNC1;
        break;

    case U_CLASS:
        s_class = b; s_cka = 0; s_ckb = 0; cksum_byte(b);
        s_state = U_ID;
        break;

    case U_ID:
        s_id = b; cksum_byte(b);
        s_state = U_LEN1;
        break;

    case U_LEN1:
        s_len = b; cksum_byte(b);
        s_state = U_LEN2;
        break;

    case U_LEN2:
        s_len |= (uint16_t)b << 8; cksum_byte(b);
        s_idx = 0;
        s_state = (s_len == 0) ? U_CKA : U_PAYLOAD;
        break;

    case U_PAYLOAD:
        cksum_byte(b);
        /* buffer only TIM-TP of the exact expected length; others we still
         * checksum-walk so the state machine resyncs cleanly, but we don't
         * store their payload. */
        if (s_class == 0x0D && s_id == 0x01 && s_len == UBX_TIMTP_LEN &&
            s_idx < UBX_TIMTP_LEN) {
            s_buf[s_idx] = b;
        }
        if (++s_idx >= s_len) s_state = U_CKA;
        break;

    case U_CKA:
        s_rx_cka = b;
        s_state  = U_CKB;
        break;

    case U_CKB:
        /* frame complete — verify checksum, then act only on TIM-TP */
        if (s_rx_cka == s_cka && b == s_ckb &&
            s_class == 0x0D && s_id == 0x01 && s_len == UBX_TIMTP_LEN) {
            /* qErr = signed 32-bit little-endian at offset 8, picoseconds */
            int32_t qerr_ps = (int32_t)((uint32_t)s_buf[8]
                                      | ((uint32_t)s_buf[9]  << 8)
                                      | ((uint32_t)s_buf[10] << 16)
                                      | ((uint32_t)s_buf[11] << 24));
            g_qerr_ns      = (float)qerr_ps / 1000.0f;   /* ps → ns */
            /* flags byte at offset 14. Bit 3 (0x08) = mode:
             *   1 = qErr describes the NEXT pulse (u-blox default)
             *   0 = qErr describes THIS pulse (the one at towMS) */
            g_qerr_flags     = s_buf[14];
            g_qerr_mode_next = (s_buf[14] & 0x08u) != 0u;
            /* Capture the ppscount that was current when this frame arrived.
             * Pairing in ubx_timtp_correction_for_pps() uses recorded+1 for
             * BOTH modes — see the model in the header for why (mode=0 accepts
             * a 1-PPS lag because the frame physically arrives too late to
             * describe the pulse the loop is currently processing). */
            s_qerr_at_pps    = gFreqSnap.ppscount;
            g_qerr_valid   = true;
            g_qerr_count++;
            g_qerr_last_ms = millis();
        }
        s_state = U_SYNC1;
        break;

    default:
        s_state = U_SYNC1;
        break;
    }
}

/* Age out a stale qErr: if TIM-TP stops (message disabled, receiver reset),
 * we must not keep subtracting an old value. Call periodically. */
void ubx_timtp_tick(uint32_t now_ms)
{
    if (g_qerr_valid && (uint32_t)(now_ms - g_qerr_last_ms) > 3000u) {
        g_qerr_valid = false;   /* no fresh sawtooth data for 3 s */
    }
}

/* Return the correction to SUBTRACT from the measured TIC phase [ns], or 0
 * when disabled / no valid data. Kept trivial so the hot path stays cheap. */
float ubx_timtp_correction_ns(void)
{
    if (!g_qerr_enable || !g_qerr_valid) return 0.0f;
    return g_qerr_ns;
}

/* Authoritative correction for a SPECIFIC pulse, identified by its ppscount.
 * Returns the qErr only if the latest TIM-TP genuinely describes that pulse
 * (accounting for the mode bit), otherwise 0. This is the path the phase
 * loop uses; the legacy ubx_timtp_correction_ns() stays for the display.
 *
 * Pairing rules (see the header for the full model):
 *   mode=1 (next pulse): TIM-TP was emitted BEFORE its PPS, so by the time
 *     vFreqRelayTask samples PPS N's ramp, vGpsTask has usually already
 *     decoded TIM-TP(N) and s_qerr_at_pps = N-1. qErr applies to N-1+1 = N.
 *     Match: ppscount == s_qerr_at_pps + 1.
 *
 *   mode=0 (this pulse): TIM-TP was emitted AFTER its PPS, so when vFreqRelayTask
 *     samples PPS N, TIM-TP(N) has NOT arrived yet — only TIM-TP(N-1) is in
 *     hand (s_qerr_at_pps = N-1), describing the PREVIOUS pulse. qErr(N) cannot
 *     physically be applied to PPS N in this mode. We accept a 1-PPS lag:
 *     qErr(N-1) is applied to PPS N. That is imperfect (qErr drifts ~1 ns/s)
 *     but far better than applying a value off by a random number of pulses,
 *     which is what the unpaired code did. Match: ppscount == s_qerr_at_pps + 1.
 *
 * Note both modes reduce to the SAME test (ppscount == s_qerr_at_pps + 1).
 * The mode bit is still captured and published (g_qerr_mode_next) for
 * diagnostics, but the pairing arithmetic is identical because the two modes
 * differ only in when the frame is emitted, and the +1 offset absorbs that. */
void ubx_timtp_latch(uint32_t ppscount)
{
    s_latch_ok  = (g_qerr_enable && g_qerr_valid);
    s_latch_ns  = s_latch_ok ? g_qerr_ns : 0.0f;
    s_latch_pps = ppscount;
}

float ubx_timtp_correction_for_pps(uint32_t ppscount)
{
    if (!g_qerr_enable || !g_qerr_valid) return 0.0f;
    /* THE LATCHED VALUE FIRST, because it is the one that measures right — see
     * ubx_timtp_latch() in the header for the four numbers that settle it. The
     * offset is recorded rather than assumed, exactly as for the old rule
     * below, so a build where this stops matching says so in SAW instead of
     * quietly going back to steering on an uncorrected phase. */
    if (s_latch_ok) {
        int32_t dl = (int32_t)(ppscount - s_latch_pps);
        uint32_t bl = (dl <= -1) ? 0u : (dl >= 4) ? (QERR_LAG_BINS - 1u)
                                                  : (uint32_t)(dl + 1);
        g_qerr_latchlag[bl]++;
        /* 0 OR 1, AND THE REASON IS NOT A TOLERANCE. ltic_read_fast() captures
         * the ramp voltage and the sawtooth TOGETHER, in the same two lines, for
         * the same pulse; the loop later reads that same g_ltic_voltage, so it
         * must use the qErr latched WITH it. The pulse counter is only there to
         * say which latch is current, and it increments between the sample and
         * the control task - measured 828 times out of 828 in one bucket on
         * build 26, which is a fixed ordering, not a race. Either offset returns
         * the same latched pair, so accepting both is exact rather than lax;
         * anything further back means the loop is running on a stale ramp, which
         * is a different fault and still refused. */
        if (dl == 0 || dl == 1) { g_qerr_paired++; return s_latch_ns; }
    }
    /* Record HOW FAR OFF the pairing is, not just that it failed. On 02.09 SAW
     * reported paired=8439/36757 - 23% - on a receiver delivering a TIM-TP frame
     * every second (frames=36755 in the same 10 h). So the frames are all there
     * and the +1 rule is simply looking at the wrong pulse most of the time,
     * which means algorithm 13 has been steering on a phase with the receiver's
     * sawtooth left in it for three quarters of every run while every display
     * and every log line showed the corrected one. Replayed against the night's
     * dph, that one fact accounts for the whole discrepancy: R reads 6.2 ns
     * where the corrected phase gives 2.93, and Sf reads zero where it should
     * be 0.15 - because a white 5 ns term swamps the difference between the two
     * lags that Sf is measured from.
     *
     * The offset that WOULD pair is a property of task ordering on this build
     * and this receiver, so it is measured here rather than reasoned about. One
     * capture with SAW now says which bucket dominates, and the rule follows
     * from that. */
    {
        int32_t d = (int32_t)(ppscount - s_qerr_at_pps);
        uint32_t b = (d <= -1) ? 0u : (d >= 4) ? (QERR_LAG_BINS - 1u)
                                               : (uint32_t)(d + 1);
        g_qerr_lag[b]++;
    }
    if (ppscount == s_qerr_at_pps + 1u) { g_qerr_paired++;   return g_qerr_ns; }
    g_qerr_unpaired++;
    return 0.0f;
}
