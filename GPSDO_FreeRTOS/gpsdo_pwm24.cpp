/* =======================================================================
 * gpsdo_pwm24.cpp — 24-bit control voltage from a short PWM plus dithering
 * Part of GPSDO v1.07.57rt
 *
 * See gpsdo_pwm24.h for what this does and why. This file is the hardware:
 * TIM4 CH4 on PB9, fed from a table by DMA1 Stream 6 Channel 2.
 * ======================================================================= */
#include "gpsdo_config.h"

#ifdef GPSDO_PWM_DITHER

#include <Arduino.h>
#include "stm32f4xx_hal.h"
#include "gpsdo_pwm24.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Two tables, alternated by the DMA controller. uint16_t because CCR4 is 16 bits
 * on TIM4 and the PSIZE/MSIZE pair must match. */
static uint16_t s_tbl[2][PWM24_TBL];
static uint32_t s_last24 = 0;
static bool     s_running = false;

/* Serialises pwm24_write between the control task, the CLI task and anything
 * else that moves the control voltage. Without it two concurrent fills of the
 * same table interleave, and one 168 ms replay carries a torn pattern. Created
 * in pwm24_begin() (before the scheduler starts, which FreeRTOS permits);
 * writes before the scheduler runs stay single-threaded and skip the lock. */
static SemaphoreHandle_t s_lock;

static bool pwm24_lock(void)
{
    if (s_lock != NULL && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        return true;
    }
    return false;
}

static void pwm24_unlock(bool locked)
{
    if (locked) xSemaphoreGive(s_lock);
}

/* -----------------------------------------------------------------------
 * Fill one table with the dither pattern for a 24-bit code.
 *
 * The top N bits are the base duty; the low (24-N) bits say how many periods out
 * of the table should use base+1. Accumulating the low bits and taking the carry
 * spreads those periods as evenly as the arithmetic allows, which is what keeps
 * the dither out of the audio-frequency region where the filter is weakest.
 *
 * This is Alan's accumulator, unrolled into a table rather than run in an
 * interrupt — the sequence is identical, it is just computed ahead of time.
 * --------------------------------------------------------------------- */
static void pwm24_fill(uint16_t *tbl, uint32_t code24)
{
    const uint32_t X = code24 >> (24 - PWM24_N);          /* base duty        */
    const uint32_t Y = code24 & (PWM24_TBL - 1u);         /* fractional part  */

    uint32_t acc = 0;
    for (uint32_t i = 0; i < PWM24_TBL; i++) {
        acc += Y;
        if (acc >= PWM24_TBL) {
            acc -= PWM24_TBL;
            tbl[i] = (uint16_t)(X + 1u);
        } else {
            tbl[i] = (uint16_t)X;
        }
    }
    /* Exactly Y entries carry, so the mean is X + Y/2^(24-N) — the code, without
     * approximation. A CCR equal to the period means permanently high, which is
     * the correct full-scale behaviour rather than an overflow. */
}

bool pwm24_begin(void)
{
    /* Detach whatever the core had on the pin. analogWrite() owns TIM4 through
     * HardwareTimer; leaving it attached means two things driving one compare
     * register. */
    pinMode(PIN_VCTL_PWM, OUTPUT);
    digitalWrite(PIN_VCTL_PWM, LOW);

    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB9 -> AF2 (TIM4_CH4), push-pull, high speed. */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &g);

    /* Timer: no prescaler, so the carrier is the timer clock divided by the
     * period. TIM4 sits on APB1; with an APB1 prescaler other than 1 the timer
     * clock is doubled, giving 100 MHz on this board. */
    TIM4->CR1  = 0;
    TIM4->PSC  = 0;
    TIM4->ARR  = PWM24_PERIOD - 1u;
    TIM4->CCR4 = 0;

    /* CH4: PWM mode 1, preloaded. Preload matters — without it a DMA write
     * mid-period would take effect immediately and shorten that pulse, which is
     * exactly the glitch the dither is trying to avoid. */
    TIM4->CCMR2 &= ~(TIM_CCMR2_OC4M | TIM_CCMR2_CC4S);
    TIM4->CCMR2 |= (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM4->CCER  |= TIM_CCER_CC4E;
    TIM4->CR1   |= TIM_CR1_ARPE;

    /* DMA1 Stream 6 Channel 2 = TIM4_UP. Double-buffer mode: the controller
     * alternates between the two tables on its own, so a value change can be
     * written into the inactive one with no interrupt and no glitch. */
    DMA_Stream_TypeDef *st = DMA1_Stream6;
    st->CR &= ~DMA_SxCR_EN;
    while (st->CR & DMA_SxCR_EN) { }
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 |
                  DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;

    pwm24_fill(s_tbl[0], 0);
    pwm24_fill(s_tbl[1], 0);

    st->PAR  = (uint32_t)&TIM4->CCR4;
    st->M0AR = (uint32_t)s_tbl[0];
    st->M1AR = (uint32_t)s_tbl[1];
    st->NDTR = PWM24_TBL;
    st->FCR  = 0;                       /* direct mode, no FIFO needed        */
    st->CR   = (2u << DMA_SxCR_CHSEL_Pos)   /* channel 2                      */
             | DMA_SxCR_DBM                  /* double buffer                  */
             | DMA_SxCR_CIRC                 /* circular                       */
             | DMA_SxCR_MINC                 /* walk the table                 */
             | (1u << DMA_SxCR_DIR_Pos)      /* memory -> peripheral           */
             | (1u << DMA_SxCR_PSIZE_Pos)    /* 16-bit peripheral              */
             | (1u << DMA_SxCR_MSIZE_Pos)    /* 16-bit memory                  */
             | (2u << DMA_SxCR_PL_Pos);      /* high priority                  */
    st->CR |= DMA_SxCR_EN;
    if (!(st->CR & DMA_SxCR_EN)) return false;

    TIM4->DIER |= TIM_DIER_UDE;         /* update event requests DMA          */
    TIM4->EGR   = TIM_EGR_UG;           /* load the shadow registers          */
    TIM4->CR1  |= TIM_CR1_CEN;

    s_running = true;
    s_last24  = 0;
    s_lock    = xSemaphoreCreateMutex();  /* NULL only on heap exhaustion: writes
                                           * then run unlocked — same behaviour
                                           * as before the lock existed. */
    return true;
}

void pwm24_write(uint32_t code24)
{
    if (!s_running) return;
    if (code24 > PWM24_MAX) code24 = PWM24_MAX;
    s_last24 = code24;

    /* Fill BOTH tables. The first version filled only the buffer the DMA was
     * not reading, but double buffering alternates every pass, so the other
     * table — still holding the previous code — replayed until the next
     * write: the output flipped between old and new code at ~3 Hz (one pass
     * is 2^(24-N) carrier periods = 167.8 ms however N is chosen), a square
     * wave the RC filter passes almost untouched. At the loop's one update
     * per second that was a sub-LSB curiosity, but a module like this should
     * be correct at any update rate, not just the one it shipped with.
     *
     * Writing the table the DMA IS reading is safe by overtaking: the DMA
     * consumes one entry per PWM period (81.9 µs at N=13) while the fill
     * writes one every few microseconds, so once the fill passes the read
     * position every entry the DMA reaches already holds the new code. The
     * handover costs at most one boundary entry of the previous code — a
     * legal duty either way, not a glitch. */
    bool locked = pwm24_lock();
    pwm24_fill(s_tbl[0], code24);
    pwm24_fill(s_tbl[1], code24);
    pwm24_unlock(locked);
}

uint32_t pwm24_last(void)
{
    return s_last24;
}

#endif /* GPSDO_PWM_DITHER */
