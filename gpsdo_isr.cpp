/**
 * gpsdo_isr.cpp — Interrupt Service Routines
 *
 * Part of GPSDO FreeRTOS v0.51
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * ISRs do the absolute minimum: read/clear hardware, post to queue or
 * notify task, return immediately.  No math, no I2C, no Serial, no ADC.
 *
 * TIM2 overflow ISR  — flags 32-bit counter wrap
 * TIM3 capture ISR   — reads TIM2 value on GPS 1PPS rising edge,
 *                       posts PpsEvent_t to xPpsQueue
 * Timer4 2Hz ISR     — gives xHalfSecondSem for uptime and LED blink
 */

#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include <Arduino.h>
#include "stm32f4xx_hal.h"

/* -----------------------------------------------------------------------
 * Static ISR-side state
 * These are ONLY accessed from ISR context — no mutex needed.
 * ----------------------------------------------------------------------- */
static volatile bool  s_overflow_pending = false;
static volatile uint32_t s_capture_value = 0;
static volatile bool  s_capture_ready    = false;

/* -----------------------------------------------------------------------
 * TIM2 overflow ISR
 * Called when TIM2 (32-bit, clocked by 10 MHz OCXO) wraps at 0xFFFFFFFF.
 * Approximately every 429 seconds.
 * ----------------------------------------------------------------------- */
extern "C" void Timer2_Overflow_ISR(void)
{
    /* Just set the flag — FreqRelay task will pick it up with the next
     * capture event and decide whether this overflow is valid or an error */
    s_overflow_pending = true;
    /* No task notification here; the capture ISR will do it */
}

/* -----------------------------------------------------------------------
 * TIM2 capture ISR — fires on each GPS 1PPS rising edge
 * ----------------------------------------------------------------------- */
extern "C" void Timer2_Capture_ISR(void)
{
    /* SAFETY: ISR may fire before RTOS objects are created (during setup).
     * Guard every RTOS call. Without this, xQueueSendFromISR(NULL, ...)
     * dereferences a null pointer and hard-faults within ~1s of startup. */
    if (xPpsQueue == NULL) {
        (void)TIM2->CCR3;          /* must read to clear capture flag */
        s_overflow_pending = false;
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    PpsEvent_t evt;
    evt.ccr_value     = TIM2->CCR3;
    evt.overflow_flag = s_overflow_pending;
    s_overflow_pending = false;

    xQueueSendFromISR(xPpsQueue, &evt, &xHigherPriorityTaskWoken);

    if (xFreqRelayTask != NULL)
        vTaskNotifyGiveFromISR(xFreqRelayTask, &xHigherPriorityTaskWoken);

    /* LTIC: flag that Vphase is ready to read. The PPS event latches the
     * TIC capacitor voltage; SensorTask will read + discharge PA1 on its
     * next wake-up. The flag is a volatile bool — safe to write from ISR. */
#ifdef GPSDO_LTIC
    extern volatile bool g_ltic_must_read;
    g_ltic_must_read = true;
#endif

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* -----------------------------------------------------------------------
 * 2 Hz timer ISR — used for uptime clock and LED blink tick
 * ----------------------------------------------------------------------- */
extern "C" void Timer_ISR_2Hz(void)
{
    /* SAFETY: guard for pre-scheduler fire */
    if (xTwoHzSemaphore == NULL) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    GPIOC->ODR ^= (1u << 13);   /* PC13: toggle blue LED */

    xSemaphoreGiveFromISR(xTwoHzSemaphore, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
