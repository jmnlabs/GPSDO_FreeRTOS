/* ======================================================================
 * ubx_timtp.cpp — UBX-TIM-TP sawtooth (quantization-error) correction
 *
 * Part of GPSDO FreeRTOS v0.95
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

#include "ubx_timtp.h"
#include "gpsdo_state.h"
#include <Arduino.h>

/* ---- published state (read by the TIC phase path) ---- */
volatile bool     g_qerr_enable   = false;   /* SAW command toggles this      */
volatile bool     g_qerr_valid    = false;   /* a fresh qErr has been seen    */
volatile float    g_qerr_ns       = 0.0f;    /* latest qErr [ns]              */
volatile uint32_t g_qerr_count    = 0;       /* total TIM-TP frames parsed    */
volatile uint32_t g_qerr_last_ms  = 0;       /* millis() of last valid frame  */

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
