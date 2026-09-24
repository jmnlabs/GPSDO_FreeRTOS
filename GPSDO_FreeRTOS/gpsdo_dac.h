/*
 * gpsdo_dac.h — single point where the control voltage leaves the firmware.
 *
 * Part of GPSDO v1.07.57rt
 *
 * Before this existed, analogWrite(PIN_VCTL_PWM, ...) appeared in 23 places
 * across three files. Adding a second output path by editing all of them would
 * have been an invitation to miss one, and a missed call site is the worst kind
 * of bug here: the loop would steer correctly almost all the time and jump
 * whenever the stale path was taken.
 *
 * TWO RESOLUTIONS, DELIBERATELY
 * -----------------------------
 * gpsdo_dac_write16() takes the 16-bit value the control loop has always
 * computed. Under the PWM path it is passed straight through; under the
 * sigma-delta path it is shifted up into the 24-bit command.
 *
 * That shift means the sigma-delta DAC, wired this way, delivers **no extra
 * resolution** — the loop is still only asking for 65536 distinct levels, so the
 * DAC's 16.7 million are stepped 256 at a time. What it does deliver immediately
 * is a much shorter analogue time constant, because a shaped one-bit stream at
 * 390 kHz needs far less filtering than a 16-bit PWM, and filter delay inside a
 * control loop is not free.
 *
 * Actually using 24 bits needs the loop's own arithmetic widened: pwm_output is
 * uint16_t and the PI terms round into it. gpsdo_dac_write24() exists so that
 * work can be done later without touching this layer again — the plumbing is
 * ready, the loop is not. Doing it before the hardware path is proven would mean
 * debugging two things at once.
 */
#ifndef GPSDO_DAC_H
#define GPSDO_DAC_H

#include <stdint.h>

/* Bring up whichever output path is compiled in. Call once from setup(), before
 * the control task starts. Returns false only if the sigma-delta path failed to
 * start; the PWM path cannot fail. */
/* ---- WHICH PATH DRIVES THE PIN, CHOSEN AT RUNTIME -------------------------
 *
 * All three output paths are compiled together and the DAC command picks which
 * one is live. The SIGNAL is switched by jumpers on the board — there is no
 * soft multiplexer and there must not be one, because two drivers fighting over
 * the control voltage is a hardware fault, not a mode. The firmware's job is
 * only to know which path it is steering, so that the step size, the telemetry
 * and the fine-path arithmetic describe the thing that is actually connected.
 *
 * PWM and DITH share PB9 / TIM4 CH4. Where the dither engine is compiled in it
 * owns that timer, so selecting PWM does not tear the DMA down and hand the pin
 * back to analogWrite — it writes the same 24-bit code with the low eight bits
 * cleared. Every table entry is then identical, the duty cycle is constant, and
 * the voltage on the pin is bit-for-bit what plain PWM produced. It is the same
 * output, reached without a reconfiguration that could fail halfway. On a build
 * with no dither engine, PWM is the literal analogWrite path.
 *
 * DEFAULT IS DITH. An unset stored value asks for the dither path, because that
 * is the one worth having and the one every current board is wired for; if it
 * is not compiled in, the resolver falls back to what is. */
enum {
    DAC_PATH_PWM  = 0,   /* 16-bit, no dither                */
    DAC_PATH_DITH = 1,   /* 24-bit dithered PWM (default)    */
    DAC_PATH_EXT  = 2    /* external AD5680 over bit-bang SPI */
};

extern volatile uint8_t g_dac_path;

/* The two scale factors that make the `DAC` report's commanded-against-measured
 * comparison true on boards it used to lie on. Both live here because they
 * describe the same thing g_dac_path does: how the number the loop writes
 * relates to the world outside.
 *
 * g_dac_vref (volts, default 3.3) is what full code means at the output the
 * firmware is steering — the PWM paths run from the 3.3 V rail, an external
 * DAC from its own reference (5.0 V for the AD5680 boards). The report's
 * "commanded" voltage is vref × code/65536; with the default it is exactly
 * the historical 3.3 V PWM model, so existing boards see no change.
 *
 * g_adc_vdiv (ratio, default 1.0) is the divide ratio of whatever sits
 * between the measured node and the ADC pin (e.g. 2.0 for 10k+10k). Every
 * place that turns the Vctl ADC reading into volts multiplies by it.
 *
 * Both are set from the CLI (DV / AV), auto-saved with the ALGO settings
 * block, and recalled from it — 0 in the stored record means "unset" and
 * leaves the default in force. */
extern double g_dac_vref;

/* WHAT THE PA0 DIVIDER IS LOOKING AT.
 *
 * That divider (4k7 + 4k7 on the boards that have it) has always measured the
 * 5 V rail. From the V3 board a jumper on the top of it selects the voltage
 * REFERENCE instead — same divider, same pin, same scaling, different subject.
 *
 * The firmware cannot see a jumper, so it is told, exactly as with DV, AV and
 * the DAC path. Being told also buys the range check: on the rail, 4.5..5.5 V
 * is healthy; on the reference, it should read what DV says to within a few
 * percent, and a reference that is missing, sagging, or simply not the part
 * that was fitted stops being invisible. Getting that wrong is expensive —
 * a 5.000 V device where the firmware believes 4.096 V puts a 20 % error into
 * every frequency figure the board prints. */
#define VSENSE_VCC   0u
#define VSENSE_VREF  1u
extern uint8_t g_vsense_src;
extern double g_adc_vdiv;

/* Is this path compiled into THIS build? */
bool        gpsdo_dac_path_available(uint8_t path);
/* The path that will actually be used if `want` is asked for: `want` when it is
 * available, otherwise the first of DITH, PWM, EXT that is. Never returns a
 * path that cannot drive the pin. */
uint8_t     gpsdo_dac_path_resolve(uint8_t want);
const char *gpsdo_dac_path_name(uint8_t path);

bool gpsdo_dac_begin(void);

/* Command in the loop's native 16-bit units. This is what every existing call
 * site uses, and its behaviour under the PWM path is byte-for-byte what
 * analogWrite() did before. */
void gpsdo_dac_write16(uint16_t v);

/* Command in the sigma-delta DAC's native 24-bit units, 0..16777215. Under the
 * PWM path the low 8 bits are discarded, so a caller gains nothing but loses
 * nothing either. */
void gpsdo_dac_write24(uint32_t v);

/* Last value written, in 16-bit units, for telemetry that used to read
 * gCtrl.pwm_output directly. Rounded, not truncated, so what the displays show
 * is the nearest 16-bit code to what is actually on the pin. */
uint16_t gpsdo_dac_last16(void);

/* ---- THE FINE PATH ---------------------------------------------------------
 *
 * The comment at the top of this file said the plumbing was ready and the loop
 * was not. This is the loop catching up.
 *
 * WHY THE FRACTION LIVES HERE, AND NOWHERE ELSE
 * The control value is written from 21 call sites: the CT and LC sweeps, the
 * acquisition ramps, holdover steering, the SP command, and the loop itself.
 * Twenty of those are deliberately coarse — a sweep that lands on 30720.4
 * instead of 30720 is not a better sweep, it is a sweep whose reference point
 * nobody can state. So the fraction is owned by this layer: every coarse write
 * clears it as a side effect of arriving here, and no caller has to remember
 * to. Keeping it in the control loop instead would mean twenty places that
 * each had to know to reset it, which is exactly the class of bug the top of
 * this file was written to prevent.
 *
 * WHAT IT BUYS. One 16-bit step is about 320 µHz on the measured plant here,
 * which is 3.2e-11 of 10 MHz — coarser than the 4e-12 the loop was measured
 * holding over 10 000 s. It got there by dithering between adjacent codes from
 * one correction to the next, which works but leaves the control voltage
 * hunting. With the fraction kept, a correction smaller than one step is
 * applied instead of being truncated away, and the step becomes 1.25e-13.
 *
 * The truncation it removes was also biased: (int32_t) rounds toward zero, so
 * every correction lost part of itself in the same direction, which reads to
 * the loop as a gain error of up to a sixth at the 6-LSB corrections measured
 * in normal operation.
 *
 * WHAT IT DOES NOT CHANGE. gpsdo_dac_last16() still returns a plain uint16_t,
 * so every display, the telemetry line and the flash ring see exactly what they
 * saw before. The settings block still stores 16 bits; a restore starts with a
 * zero fraction and gives up at most 1.25e-13, which is below anything this
 * hardware can show.
 * -------------------------------------------------------------------------- */

/* Command in 16-bit units WITH the fraction kept. Values outside 1..65535 are
 * clamped to the same band gpsdo_dac_write16() uses, so the fine path can never
 * reach a code the coarse path could not. */
void gpsdo_dac_write16f(double v16);

/* The value actually on the pin, in 16-bit units including the fraction. This
 * is what an algorithm's output stage should add its correction to — using the
 * rounded uint16_t instead would throw the fraction away once per cycle and
 * make the whole exercise pointless. */
double gpsdo_dac_last16f(void);

/* The raw 24-bit code, for reporting. */
uint32_t gpsdo_dac_last24(void);

/* True when the compiled-in output path genuinely resolves more than 16 bits,
 * i.e. the dithered PWM or an external DAC. False for plain analogWrite(), where
 * the fraction is harmless but buys nothing — the loop checks this so it can
 * report honestly rather than claim resolution it does not have. */
bool gpsdo_dac_fine_available(void);

/* HOW MANY DISTINCT LEVELS THE LIVE PATH'S OUTPUT CAN TAKE.
 *
 * The control value is 24-bit on every path — that is the number the loop
 * steers and the fine carry delivers on average. What the OUTPUT can move in
 * one write is a different number and it depends on which driver the jumper is
 * feeding:
 *
 *   DITH  2^24      the dither table averages the 24-bit value EXACTLY, by
 *                   construction (2^(24-N) entries, Y of them one higher), so
 *                   there is nothing below it to lose
 *   EXT   2^18      the AD5680's own width; dac_emit() scales 24-bit full scale
 *                   onto DAC_EXT_MAX, so 64 counts of the control value make one
 *                   step at the pin
 *   PWM   2^16      on a dither build: the same engine with every table entry
 *                   equal, low eight bits cleared — whole 16-bit LSBs, which is
 *                   what gpsdo_dac_fine_available() reports
 *   PWM   50 000    on a build with no dither engine: the timer's own period,
 *                   100 MHz / 2 kHz. NOT a power of two, which is the whole
 *                   reason this returns a count rather than a bit width — the
 *                   16 it used to claim overstated that path by a third.
 *
 * Reporting one figure for all of them is how the DAC report came to promise
 * 0.094 uHz on an 18-bit part that cannot move less than about 6 uHz in a
 * single write. Both numbers are worth having; they are not the same number. */
uint32_t gpsdo_dac_output_steps(void);

#endif /* GPSDO_DAC_H */
