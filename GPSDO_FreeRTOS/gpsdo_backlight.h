/*
 * gpsdo_backlight.h — TFT backlight dimming on PB5 (TIM3 CH2)
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 * THE HARDWARE THIS EXPECTS
 * -------------------------
 * A P-channel MOSFET between the 3.3 V rail and the display's LED anode, with
 * its gate driven from PB5 through about 47 ohm:
 *
 *       +3.3V ──── source
 *                    |
 *   PB5 ──[47R]──── gate ─┬── 100k ── GND      (defines the gate at reset)
 *                    |    |
 *                   drain ┴──┬── VLED of the TFT module
 *                            |
 *                          10-47u to GND
 *
 * THE STAGE INVERTS. A P-channel conducts when its gate is BELOW its source,
 * so a LOW on PB5 is full brightness and a HIGH is dark. The duty written to
 * the timer is therefore the complement of the requested percentage, which is
 * also why 100% is a special case worth having: it is a constant zero compare,
 * i.e. the pin sits statically low and the stage stops switching altogether.
 * No switching means nothing on the 3.3 V rail, which is the rail that feeds
 * VDDA and therefore the ADC reference, the phase reading and the Vctl
 * monitor. Full brightness is the quietest setting, not the loudest.
 *
 * The 100k to ground matters more than it looks: PB5 is high-impedance from
 * power-on until this runs, and a floating gate leaves the MOSFET in an
 * undefined state — possibly half on, dissipating. Pulled down, the display
 * comes up at full brightness before any firmware runs, which for a screen is
 * the right default: a dark panel is indistinguishable from a dead board.
 *
 * WHY PB5 AND TIM3
 * ----------------
 * TIM3 is otherwise completely unused by this firmware. (Several old comments
 * claimed the 1 PPS capture lived there; it does not — that is TIM2 channel 3
 * on PB10, see gpsdo_isr.cpp.) PB5 carries only the 2 kHz test square wave,
 * which is off by default and compiled out entirely under GPSDO_DAC_EXT.
 *
 * PA8 was the first candidate and is worse: it doubles as I2C3_SCL, and the
 * core has been observed reconfiguring it to AF4 behind the firmware's back —
 * gpsdo_tasks.cpp already has to re-assert it once for the TM1637.
 *
 * WHY 30% AND NOT 0%
 * ------------------
 * Below roughly a third the panel stops being readable rather than becoming
 * usefully dim, so there is no setting down there worth reaching. Making 30
 * the floor means a mistyped value cannot leave the operator staring at what
 * looks like a dead display and wondering which of the two it is.
 */
#ifndef GPSDO_BACKLIGHT_H
#define GPSDO_BACKLIGHT_H

#include <stdint.h>

#define BL_PCT_MIN   30u
#define BL_PCT_MAX  100u
#define BL_PCT_DEF  100u      /* matches the gate pull-down: full, and silent */

/* The live setting, in percent. Read by the CLI and the settings snapshot;
 * written only through backlight_set(), which is what clamps it. */
extern uint8_t g_tft_bl_pct;

/* Configure TIM3 CH2 on PB5 and apply g_tft_bl_pct. Cannot fail — it sets up
 * a timer and a pin and checks nothing — which is why it returns void rather
 * than joining the bool-returning begins. */
void backlight_begin(void);

/* Clamp to BL_PCT_MIN..BL_PCT_MAX, store, and apply. The single entry point:
 * the CLI and the settings recall both come through here so the clamp and the
 * hardware write cannot drift apart. Safe to call before backlight_begin(),
 * in which case it only stores and begin() applies it. */
void backlight_set(uint8_t pct);

#endif /* GPSDO_BACKLIGHT_H */
