/* ======================================================================
 * ubx_timtp.h — UBX-TIM-TP sawtooth (quantization-error) correction
 *
 * Part of GPSDO FreeRTOS v0.91
 *
 * See ubx_timtp.cpp for the full description. Passive UBX sniffer that
 * extracts qErr (quantization error) from UBX-TIM-TP and offers it as a
 * per-pulse correction to the TIC phase measurement. Works on LEA-6T,
 * LEA/NEO-M8T and ZED-F9T (qErr is I4 at payload offset 8, in ps, on all).
 * ====================================================================== */
#ifndef UBX_TIMTP_H
#define UBX_TIMTP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Published state (defined in ubx_timtp.cpp) */
extern volatile bool     g_qerr_enable;   /* master enable (SAW command)    */
extern volatile bool     g_qerr_valid;    /* fresh qErr available           */
extern volatile float    g_qerr_ns;       /* latest qErr [ns]               */
extern volatile uint32_t g_qerr_count;    /* total frames parsed            */
extern volatile uint32_t g_qerr_last_ms;  /* millis() of last valid frame   */

/* Feed one GPS-stream byte to the UBX sniffer (call beside gps.encode). */
void  ubx_timtp_feed(uint8_t b);

/* Age out stale qErr if TIM-TP stops arriving. Call ~1 Hz. */
void  ubx_timtp_tick(uint32_t now_ms);

/* Correction to SUBTRACT from measured TIC phase [ns] (0 if off/no data). */
float ubx_timtp_correction_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* UBX_TIMTP_H */
