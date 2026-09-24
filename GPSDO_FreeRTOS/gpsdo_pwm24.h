/* =======================================================================
 * gpsdo_pwm24.h — 24-bit control voltage from a short PWM plus dithering
 * Part of GPSDO v1.07.57rt
 *
 * The idea is Alan Cashin's (MIS42N on EEVblog), from his Budget GPSDO: run the
 * PWM at fewer bits than you need, and vary the duty cycle from one period to the
 * next so that the AVERAGE carries the missing bits. A 12-bit PWM whose duty
 * alternates in the right proportion delivers 24 bits after the filter.
 *
 * WHY THIS IS WORTH DOING
 * -----------------------
 * Not for the resolution alone — the 16-bit PWM already gives about 50 uV, and
 * the reference it sits on is noisier than that. The gain is the CARRIER. Ripple
 * has to be filtered below one output step, and how hard that is depends on how
 * far the carrier sits above the filter's corner:
 *
 *     16-bit PWM at 2 kHz    ->  filter corner 0.7 Hz,  time constant 230 ms
 *     12-bit dithered, 24 kHz ->  filter corner 8.4 Hz,  time constant  19 ms
 *     13-bit dithered, 12 kHz ->  filter corner 4.2 Hz,  time constant  38 ms
 *
 * That is a six- to twelvefold shorter filter for the same ripple (the shipped
 * N=13 sits at the lower end), and filter delay goes straight into the loop as
 * phase lag. The resolution comes along for free.
 *
 * WHERE THIS DIFFERS FROM ALAN'S
 * ------------------------------
 * His PIC has no DMA, so he dithers in a timer interrupt: accumulate, carry into
 * the compare register, once per PWM period. On this part that would be 24 000
 * interrupts a second competing with the 1PPS capture, which is the one interrupt
 * in this firmware that must not be delayed.
 *
 * But the dither pattern for a CONSTANT value is periodic — it repeats every
 * 2^(24-N) periods. So it can be computed once into a table and replayed by DMA
 * into the compare register, with the CPU touching nothing. The table is rebuilt
 * only when the value changes, which is once a second:
 *
 *     N=12: 4096 entries, 8 KB per buffer, carrier 24.4 kHz, 0.025% CPU
 *     N=13: 2048 entries, 4 KB per buffer, carrier 12.2 kHz, 0.012% CPU
 *
 * And the average is EXACT, not approximate: the table holds exactly Y entries of
 * X+1 among 2^(24-N), so it averages X + Y/2^(24-N), which is the 24-bit value by
 * construction. Replaying it cannot drift.
 *
 * HARDWARE
 * --------
 * PB9 = TIM4_CH4, the same pin the plain PWM uses, so the existing filter and
 * wiring are unchanged. TIM4_UP drives DMA1 Stream 6 Channel 2. TIM4 is otherwise
 * used only by analogWrite, which this replaces; the 2 Hz tick is on TIM9 and the
 * 1PPS chain, which is TIM2 alone — the counter on ETR and the capture on
 * channel 3 (PB10) — so nothing is disturbed.
 *
 * Two buffers, alternated by the DMA controller in double-buffer mode, so a
 * hardware buffer switch never interrupts a replay, and a value change refills
 * BOTH tables (the fill outruns the read position, so taking over the table the
 * DMA is reading is glitch-free). No interrupt at all — the hardware switches
 * buffers by itself.
 * ======================================================================= */
#ifndef GPSDO_PWM24_H
#define GPSDO_PWM24_H

#include <stdint.h>
#include <stdbool.h>
#include "gpsdo_config.h"

#ifdef GPSDO_PWM_DITHER

/* PWM width in bits. The rest of the 24 is carried by the dither, so a smaller
 * number means a higher carrier and a bigger table. 12 or 13 are the sensible
 * choices; below 12 the table stops being worth the RAM and above 13 the carrier
 * advantage fades. */
#ifndef GPSDO_PWM_DITHER_BITS
  #define GPSDO_PWM_DITHER_BITS  13
#endif

#if (GPSDO_PWM_DITHER_BITS < 10) || (GPSDO_PWM_DITHER_BITS > 16)
  #error "GPSDO_PWM_DITHER_BITS must be 10..16 (12 or 13 recommended)"
#endif

#define PWM24_N        (GPSDO_PWM_DITHER_BITS)
#define PWM24_PERIOD   (1u << PWM24_N)             /* PWM steps per cycle      */
#define PWM24_TBL      (1u << (24 - PWM24_N))      /* dither table entries     */
#define PWM24_MAX      (0x00FFFFFFu)               /* full-scale 24-bit code   */

/* Bring up TIM4 CH4 on PB9 and start the DMA replay. Returns false if the DMA
 * stream could not be configured, in which case nothing is driving the pin and
 * the caller should say so rather than assume a control voltage exists. */
bool pwm24_begin(void);

/* Set the control voltage, 0 .. PWM24_MAX. Rebuilds both tables under a mutex
 * (the control task and the CLI can both move the voltage) — a few hundred
 * microseconds of work, once per correction. Safe to call before the scheduler
 * starts: those writes are single-threaded and skip the lock. */
void pwm24_write(uint32_t code24);

/* Last value written, for reporting. */
uint32_t pwm24_last(void);

#endif /* GPSDO_PWM_DITHER */
#endif /* GPSDO_PWM24_H */
