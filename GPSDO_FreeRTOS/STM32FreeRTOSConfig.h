/* ======================================================================
 * STM32FreeRTOSConfig.h — project-local FreeRTOS configuration override
 *
 * Picked up automatically by the STM32duino FreeRTOS library via
 * FreeRTOSConfig.h's __has_include("STM32FreeRTOSConfig.h") check, which
 * takes precedence over FreeRTOSConfig_Default.h.
 *
 * Why this file exists
 * --------------------
 * v1.04 was hanging at boot with a white TFT screen and no diagnostic.
 * The root cause is invisible without the stack-overflow / malloc-failed
 * hooks: configCHECK_FOR_STACK_OVERFLOW and configUSE_MALLOC_FAILED_HOOK
 * default to 0, so a blown task stack (Cortex-M) silently corrupts a
 * neighbour or HardFaults, and configASSERT traps in a for(;;) with
 * interrupts off — a dead, white panel with nothing on the serial port.
 *
 * This override turns BOTH hooks on and defines configASSERT to print
 * instead of silently looping, so the next failure names itself on the
 * USB console. The matching vApplicationStackOverflowHook /
 * vApplicationMallocFailedHook live in GPSDO_FreeRTOS.ino.
 *
 * Approach: include the full Default config (so every other knob stays
 * exactly as the library ships it) and re-define only the few macros we
 * need to change. The Default header is guarded, so this is safe.
 * ====================================================================== */
#ifndef STM32FREERTOSCONFIG_H
#define STM32FREERTOSCONFIG_H

/* Pull in the library defaults first, then override the few we need. */
#include "FreeRTOSConfig_Default.h"

/* Surface silent failures instead of hiding them. */
#undef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW    2     /* 2 = full sentinel + back-end check */

#undef configUSE_MALLOC_FAILED_HOOK
#define configUSE_MALLOC_FAILED_HOOK      1

/* Required by the pwm24 dither table lock.
 *
 * pwm24_write() rebuilds both DMA tables and is reached from two tasks —
 * ControlTask (gpsdo_control.cpp) and CliTask (gpsdo_cli.cpp) — so the fill is
 * serialised with a mutex. It also has to know whether the scheduler is running
 * yet, because pwm24_write() is called during setup() where taking a mutex with
 * portMAX_DELAY would block forever.
 *
 * Neither macro is set by this project, and whether the library Default enables
 * them is not something to leave to chance: configUSE_MUTEXES = 0 removes
 * xSemaphoreCreateMutex, and INCLUDE_xTaskGetSchedulerState = 0 removes the
 * scheduler-state query, either of which is a build error rather than a
 * runtime surprise. Both are cheap. Set them explicitly. */
#undef configUSE_MUTEXES
#define configUSE_MUTEXES                 1

#undef INCLUDE_xTaskGetSchedulerState
#define INCLUDE_xTaskGetSchedulerState    1

/* CPU load, measured by counting how fast the idle task spins.
 *
 * The alternative is configGENERATE_RUN_TIME_STATS, which wants a spare timer
 * and a base clock and gives per-task figures nobody has asked for. What is
 * actually wanted is one number on the telemetry line, and the idle task
 * already carries it: whatever fraction of the second it does NOT get is the
 * load. One volatile increment in the hook is the entire cost. */
/* Per-task CPU load. FreeRTOS calls this on every context switch, with the
 * scheduler suspended, so the hook does one cycle-counter read and one add —
 * see gpsdo_health.h for why a trace hook rather than a sampling profiler.
 * pxCurrentTCB is in scope wherever tasks.c expands this; it is passed as an
 * opaque identity and never dereferenced by the hook. */
#ifdef __cplusplus
extern "C" {
#endif
void cpu_trace_switch_in(void *tcb);
#ifdef __cplusplus
}
#endif
#define traceTASK_SWITCHED_IN()   cpu_trace_switch_in((void *)pxCurrentTCB)

/* configASSERT in the Default config is `taskDISABLE_INTERRUPTS(); for(;;);`
 * which makes any assertion an unrecoverable white screen. Override it to
 * print the fault and the file:line before trapping, so the cause is visible
 * on the USB console. It still ends in for(;;) — assertion failures are not
 * recoverable — but now they are diagnosable. */
#undef configASSERT
#define configASSERT( x ) \
    do { \
        if( ( x ) == 0 ) { \
            extern void gpsdo_assert_diagnostic(const char *, int); \
            gpsdo_assert_diagnostic(__FILE__, __LINE__); \
            taskDISABLE_INTERRUPTS(); \
            for( ;; ); \
        } \
    } while( 0 )

#endif /* STM32FREERTOSCONFIG_H */
