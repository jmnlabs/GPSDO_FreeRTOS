/*
 * gpsdo_dac.cpp — control-voltage output: PWM or sigma-delta.
 *
 * Part of GPSDO v1.07.57rt
 * See gpsdo_dac.h for why this layer exists and what the two write widths mean.
 */
#include <Arduino.h>
#include "gpsdo_config.h"
#include "gpsdo_dac.h"
#include "gpsdo_health.h"
#if !defined(GPSDO_PWM_DITHER)
  #include "stm32f4xx_hal.h"      /* the plain path owns TIM4 itself — see below */
#endif

#if defined(GPSDO_DAC_EXT)
  #include "gpsdo_dac_ext.h"
#endif
#if defined(GPSDO_PWM_DITHER)
  #include "gpsdo_pwm24.h"
#endif

/* The authoritative control value is 24-bit. Everything else is a view of it:
 * the 16-bit reading the displays and the flash ring use is this rounded, and
 * the fraction the loop steers with is this divided by 256. Holding one number
 * rather than two removes the possibility of them disagreeing. */
static uint32_t s_code24 = 0;

/* Default DITH, per the note in gpsdo_dac.h. Resolved against what is compiled
 * in at bring-up, so this initial value is a request rather than a promise. */
volatile uint8_t g_dac_path = DAC_PATH_DITH;

/* See gpsdo_dac.h for what these mean. Plain non-volatile doubles: they are
 * written only from the CLI task and read everywhere, and a torn read of a
 * double on this part would at worst put one odd-looking Vctl line on a
 * display — the loop never consumes either value. */
double g_dac_vref = 3.3;
double g_adc_vdiv = 1.0;

/* The 5 V rail, which is what every board before the V3 had on that divider
 * and therefore the only safe default: a board without the jumper reads its
 * rail and says so, exactly as it always did. */
uint8_t g_vsense_src = VSENSE_VCC;

/* ---------------------------------------------------------------------------
 * PLAIN PWM, WITHOUT THE CORE'S analogWrite()
 *
 * Only compiled when the dither engine is not. With GPSDO_PWM_DITHER the
 * "plain" path is already this timer, running at 12.2 kHz with every dither
 * table entry equal (see dac_emit) — one carrier, one filter requirement, and
 * switching paths changes only the table. Nothing below applies there.
 *
 * WHY NOT analogWrite(). The core's analogWrite() calls pwm_start(), which
 * recomputes the prescaler and the auto-reload from the requested frequency
 * ON EVERY CALL. The control voltage is written once per second, for the life
 * of the board, so that is the timer rebuilt once per second to change one
 * compare value — an opportunity to glitch the output every second, bought for
 * nothing. Worse, analogWriteFrequency() is a GLOBAL in the core: it applies
 * to whichever pin is written next, so the moment a second PWM exists (the
 * backlight on PB5) the two silently fight over one setting.
 *
 * Configuring the timer once here and writing CCR4 thereafter removes both.
 * The carrier and the resolution are deliberately UNCHANGED — 100 MHz / 50 000
 * = 2.000 kHz, the same figure analogWriteFrequency(2000) produced — so no
 * board's CT, filter or plant gain moves because of this.
 *
 * Raising the carrier was considered and rejected. It would let the filter's
 * corner rise with it, but a PWM's resolution is (timer clock)/(carrier), and
 * this is the one configuration where the PWM's own width IS the whole output:
 * no dither, no external DAC. 12.2 kHz would buy a 6x faster filter for 15.6
 * -> 13 bits, which on a 3.3 V board takes the step from 5.0e-11 to 3.0e-10.
 * Wrong direction. See tools/carrier.py for the numbers on both boards.
 * ------------------------------------------------------------------------- */
#if !defined(GPSDO_PWM_DITHER)

/* 100 MHz / 50 000 = 2.000 kHz exactly. Also the number of distinct duty
 * values, which is what gpsdo_dac_output_steps() has to report: 65 536 input
 * codes do NOT become 65 536 output levels here. */
#define PWM16_PERIOD  50000u

static void pwm16_begin(void)
{
    pinMode(PIN_VCTL_PWM, OUTPUT);
    digitalWrite(PIN_VCTL_PWM, LOW);

    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB9 -> AF2 (TIM4_CH4), push-pull, high speed. Same pin and same
     * alternate function the core would have selected. */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &g);

    TIM4->CR1  = 0;
    TIM4->PSC  = 0;                     /* TIM4 is on APB1: 100 MHz here */
    TIM4->ARR  = PWM16_PERIOD - 1u;
    TIM4->CCR4 = 0;

    /* CH4: PWM mode 1, compare preloaded. Preload is why a write can land at
     * any moment without shortening the pulse it lands in. */
    TIM4->CCMR2 &= ~(TIM_CCMR2_OC4M | TIM_CCMR2_CC4S);
    TIM4->CCMR2 |= (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM4->CCER  |= TIM_CCER_CC4E;
    TIM4->CR1   |= TIM_CR1_ARPE;
    TIM4->EGR    = TIM_EGR_UG;          /* load the shadow registers */
    TIM4->CR1   |= TIM_CR1_CEN;
}

static inline void pwm16_write(uint16_t code16)
{
    /* FULL SCALE MAPS TO FULL SCALE, the same rule dac_emit() applies to the
     * external part and pwm24_write() applies to the dither table: code 65535
     * is a compare equal to the period, i.e. permanently high. The core
     * divided by 65 536 instead, so the top LSB was unreachable; the 15 ppm
     * difference in the middle of the range is far below anything CT can
     * resolve and CT re-measures the plant in any case. */
    TIM4->CCR4 = (uint32_t)(((uint64_t)code16 * PWM16_PERIOD) / 65535u);
}
#endif /* !GPSDO_PWM_DITHER */

bool gpsdo_dac_path_available(uint8_t path)
{
    switch (path) {
#if defined(GPSDO_PWM_DITHER)
    case DAC_PATH_DITH: return true;
#endif
#if defined(GPSDO_DAC_EXT)
    case DAC_PATH_EXT:  return true;
#endif
    /* PWM is always available: with the dither engine present it is that
     * engine driven at whole-LSB granularity, and without it, analogWrite. */
    case DAC_PATH_PWM:  return true;
    default: return false;
    }
}

uint8_t gpsdo_dac_path_resolve(uint8_t want)
{
    if (gpsdo_dac_path_available(want)) return want;
    if (gpsdo_dac_path_available(DAC_PATH_DITH)) return DAC_PATH_DITH;
    if (gpsdo_dac_path_available(DAC_PATH_PWM))  return DAC_PATH_PWM;
    return DAC_PATH_EXT;
}

const char *gpsdo_dac_path_name(uint8_t path)
{
    switch (path) {
    case DAC_PATH_PWM:  return "PWM";
    case DAC_PATH_DITH: return "DITH";
    case DAC_PATH_EXT:  return "EXT";
    default:            return "?";
    }
}

/* 1..65535 in 16-bit units, expressed in 24-bit units. The fine path is clamped
 * to the same band clamp_pwm() enforces on the coarse one, so no route to the
 * pin can reach a code another route could not. */
#define DAC_CODE24_MIN  (1u << 8)
#define DAC_CODE24_MAX  (65535u << 8)

static void dac_emit(uint32_t code24, uint16_t code16);

bool gpsdo_dac_begin(void)
{
    /* Bring up EVERY path that is compiled in, not just the selected one. The
     * jumper decides which signal reaches the filter, and a path that is not
     * initialised until it is first selected would give a glitch — or a dead
     * output — at the moment of switching, which is the worst possible moment.
     * The unselected drivers sit there holding their last code and nothing is
     * connected to them. */
    bool ok = true;
#if defined(GPSDO_DAC_EXT)
    if (!dac_ext_begin()) ok = false;
#endif
#if defined(GPSDO_PWM_DITHER)
    if (!pwm24_begin())   ok = false;
#else
    /* No dither engine, so this build owns TIM4 itself rather than borrowing
     * it from analogWrite() once a second. Cannot fail: it configures a timer
     * and a pin and checks nothing, which is why it returns void. */
    pwm16_begin();
#endif
    /* Resolve the requested path against what actually came up. */
    g_dac_path = gpsdo_dac_path_resolve(g_dac_path);
    return ok;
}

void gpsdo_dac_write16(uint16_t v)
{
    /* Every correction passes through here whatever produced it, so the
     * self-assessment needs no hook in the control loops themselves. */
    health_note_output(v);
    /* A coarse write states a whole-LSB intent — a sweep point, a ramp step, a
     * value typed at the CLI — so it clears the fraction rather than carrying
     * one nobody asked for. That is the whole reason the fraction lives in this
     * file: the twenty coarse call sites get this for free. */
    s_code24 = (uint32_t)v << 8;
    dac_emit(s_code24, v);
}

/* The one place a code reaches hardware. Both write widths funnel through it so
 * the runtime path is tested once rather than in two places that could drift
 * apart — which is the same argument that created this file. */
static void dac_emit(uint32_t code24, uint16_t code16)
{
    switch (g_dac_path) {
#if defined(GPSDO_DAC_EXT)
    case DAC_PATH_EXT:
        /* Scale rather than shift: the external part is not necessarily 24 bits,
         * and 16-bit full scale must still map to its full scale exactly or the
         * constants CT measured against the PWM path would all be off. */
        dac_ext_write((uint32_t)(((uint64_t)code24 * DAC_EXT_MAX) / 0x00FFFFFFu));
        return;
#endif
#if defined(GPSDO_PWM_DITHER)
    case DAC_PATH_DITH:
        pwm24_write(code24);            /* native: this is what it is for */
        return;
    case DAC_PATH_PWM:
        /* Same engine, low eight bits cleared: every table entry identical, so
         * a constant duty cycle and the same voltage plain PWM would give. No
         * DMA teardown, nothing to fail at the moment of switching. */
        pwm24_write(code24 & 0x00FFFF00u);
        return;
#else
    case DAC_PATH_PWM:
        pwm16_write(code16);
        return;
#endif
    default:
        break;
    }
    /* Selected path is not compiled in. Cannot happen once resolve() has run at
     * bring-up and on every CLI change, but silence here would mean a frozen
     * control voltage, so fall back to the one path that always exists. */
#if defined(GPSDO_PWM_DITHER)
    pwm24_write(code24 & 0x00FFFF00u);
#else
    pwm16_write(code16);
#endif
}

void gpsdo_dac_write24(uint32_t v)
{
    if (v > 0x00FFFFFFu) v = 0x00FFFFFFu;
    s_code24 = v;
    health_note_output((uint16_t)((v + 128u) >> 8));
    /* Under PWM and under plain analogWrite the fraction is rounded away on the
     * pin. It is still kept in s_code24, because the loop accumulating it across
     * cycles is what turns a run of sub-LSB corrections into a whole one — the
     * fraction earns its keep even where the hardware cannot show it inside a
     * single write. */
    dac_emit(v, (uint16_t)((v + 128u) >> 8));
}

void gpsdo_dac_write16f(double v16)
{
    if (!(v16 == v16)) return;          /* NaN: refuse rather than clamp to a rail */
    double c = v16 * 256.0 + 0.5;       /* round, not truncate */
    uint32_t code;
    if (c <= (double)DAC_CODE24_MIN)      code = DAC_CODE24_MIN;
    else if (c >= (double)DAC_CODE24_MAX) code = DAC_CODE24_MAX;
    else                                  code = (uint32_t)c;
    gpsdo_dac_write24(code);
}

uint16_t gpsdo_dac_last16(void)
{
    /* Rounded, so the number on the display is the nearest 16-bit code to what
     * is on the pin. Truncating would make a value sitting at .99 read one LSB
     * low for its whole life. */
    return (uint16_t)((s_code24 + 128u) >> 8);
}

double   gpsdo_dac_last16f(void) { return (double)s_code24 / 256.0; }
uint32_t gpsdo_dac_last24(void)  { return s_code24; }

/* See the note in gpsdo_dac.h. A property of the ACTIVE path, like
 * gpsdo_dac_fine_available() and for the same reason.
 *
 * STEPS AND NOT BITS, because one of the four answers is not a power of two.
 * A bit width could only express the plain timer's 50 000 duty values by
 * rounding them to 65 536 or to 32 768 — the first overstates the part by a
 * third, the second throws away a third of it — and reporting a resolution
 * the hardware does not have is the exact bug build 46 fixed for the AD5680.
 * The CLI turns a power of two back into "N-bit" for the report and prints
 * the count itself when it is not one. */
uint32_t gpsdo_dac_output_steps(void)
{
    switch (g_dac_path) {
#if defined(GPSDO_PWM_DITHER)
    case DAC_PATH_DITH: return 1UL << 24;
    /* The dither engine with every table entry equal: a 13-bit carrier, but
     * the low eight bits of the 24-bit code are cleared rather than the whole
     * fraction, so what reaches the filter really does resolve 16 bits. */
    case DAC_PATH_PWM:  return 1UL << 16;
#endif
#if defined(GPSDO_DAC_EXT)
    case DAC_PATH_EXT:  return 1UL << DAC_EXT_BITS;
#endif
#if !defined(GPSDO_PWM_DITHER)
    case DAC_PATH_PWM:  return PWM16_PERIOD;
#endif
    default:            return 1UL << 16;
    }
}

bool gpsdo_dac_fine_available(void)
{
    /* A property of the ACTIVE path, not of the build. Selecting PWM on a board
     * that has the dither engine really does give up the sub-LSB resolution, and
     * the loop must be told so rather than going on writing fractions that the
     * whole-LSB path throws away. */
    switch (g_dac_path) {
    case DAC_PATH_DITH: return gpsdo_dac_path_available(DAC_PATH_DITH);
    case DAC_PATH_EXT:  return gpsdo_dac_path_available(DAC_PATH_EXT);
    default:            return false;   /* PWM: whole LSBs only */
    }
}
