/**
 * GPSDO_FreeRTOS.ino — Main entry point — hardware init and FreeRTOS scheduler start
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 *
 * setup() initialises all hardware peripherals, creates RTOS primitives
 * (mutexes, queues, event groups), spawns seven tasks, and starts the
 * FreeRTOS scheduler.  loop() is intentionally empty — it is never called
 * after the scheduler starts with the STM32duino FreeRTOS port.
 *
 * On boot, EEPROM is checked for a valid signature.  If found, stored
 * PWM, algorithm, time offset and PID parameters are recalled.  Otherwise
 * compile-time defaults are used and the calibration sequence is armed.
 *
 * Requires:
 *   - STM32duino core >= 2.2.0
 *   - STM32duino FreeRTOS library
 *   - TinyGPS++, Adafruit_AHTX0, Adafruit_BMP280, Adafruit_INA219
 *   - U8g2, TM1637Display (local copy in project)
 *   - hd44780 (if GPSDO_LCD_20x4 is enabled)
 *   - EEPROM (STM32 emulated)
 *   Arduino IDE: Tools > C Runtime Library > Newlib Nano + Float Printf/Scanf
 *
 * SERIAL RX BUFFER (build_opt.h)
 *   This sketch folder contains build_opt.h with:
 *       -DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=512
 *   STM32duino picks up build_opt.h automatically and applies these flags
 *   to the WHOLE build, including HardwareSerial.cpp in the core. This is
 *   required because a plain #define in the sketch does NOT reach the core
 *   (HardwareSerial.cpp is a separate translation unit). The larger RX
 *   buffer prevents NMEA sentences from being dropped/merged at 38400 baud
 *   when vGpsTask is briefly preempted. Default 64 B was too small even
 *   with only GGA+RMC enabled.
 */

#include "gpsdo_config.h"
#include "gpsdo_dac.h"
#include "gpsdo_backlight.h"
#include "gpsdo_state.h"
#include "gpsdo_health.h"
#include "gpsdo_algorithms.h"
#include "gpsdo_build.h"
/* Not used for anything except its own existence: including it is what
 * makes the Arduino builder recompile this file, so __DATE__ below is the
 * date of THIS build and not of whenever the sketch last changed. See
 * gpsdo_build_id.h — and the CRC in the banner, which cannot be stale either way. */
#include "gpsdo_build_id.h"
#include <Arduino.h>

/* The firmware's version, "v1.07.57rt": the release from gpsdo_config.h, the
 * build from gpsdo_build_id.h, then "rt" — joined here because this is the one
 * unit that includes the build id, see gpsdo_build.h. */
#define GPSDO_STR_(x)  #x
#define GPSDO_STR(x)   GPSDO_STR_(x)
extern const char g_fw_version[] = PROGRAM_VERSION "." GPSDO_STR(BUILD_SERIAL) "rt";
#include <Wire.h>
/* EEPROM library removed in v0.96 — persistence via flash_ring (sector 7). */

/* ---- Forward declarations for task entry points ----------------------- */
void vFreqRelayTask (void *);
void vControlTask   (void *);
void vGpsTask       (void *);
void vCliTask       (void *);
void vSensorTask    (void *);
void vDisplayTask   (void *);
void vUptimeTask    (void *);

/* ---- ISR registration (defined in gpsdo_isr.cpp) --------------------- */
extern "C" void Timer2_Overflow_ISR(void);
extern "C" void Timer2_Capture_ISR(void);
extern "C" void Timer_ISR_2Hz(void);

/* declared in gpsdo_state.cpp */
extern SemaphoreHandle_t xTwoHzSemaphore;

/* declared in gpsdo_gps.cpp */
extern void gpsdo_gps_init(void);

/* declared in gpsdo_state.cpp */
extern bool persist_check_on_boot(void);
extern void persist_recall(void);
#include "gpsdo_flash_ring.h"
#include "gpsdo_live_store.h"
#include "gpsdo_settings_store.h"
#include "gpsdo_span.h"

/* declared in gpsdo_cli.cpp — single byte, safe to read without mutex */
extern int16_t g_time_offset_min;

/* ---- pinModeAF helper ------------------------------------------------- */
static void pinModeAF(int ulPin, uint32_t Alternate)
{
    int pn = digitalPinToPinName(ulPin);
    if (STM_PIN(pn) < 8)
        LL_GPIO_SetAFPin_0_7 (get_GPIO_Port(STM_PORT(pn)), STM_LL_GPIO_PIN(pn), Alternate);
    else
        LL_GPIO_SetAFPin_8_15(get_GPIO_Port(STM_PORT(pn)), STM_LL_GPIO_PIN(pn), Alternate);
    LL_GPIO_SetPinMode(get_GPIO_Port(STM_PORT(pn)), STM_LL_GPIO_PIN(pn), LL_GPIO_MODE_ALTERNATE);
}

#if defined(GPSDO_BLUETOOTH) || defined(GPSDO_BLUETOOTH_PARALLEL)
HardwareSerial Serial2(PA3, PA2);
#endif

#if defined(GPSDO_BLUETOOTH_PARALLEL)
#include "gpsdo_tee_serial.h"
/* g_tee mirrors USB Serial and the Bluetooth UART. The extern in the config
 * headers points here; both ports are begin()-run in setup() below. */
TeeSerial g_tee(Serial, Serial2);
#endif

/* ====================================================================== */

/* Why did we just restart?
 *
 * The board was rebooting intermittently, always at the same point in GPS
 * configuration, and there was no way to tell a brown-out from a software fault
 * by looking at the log — both leave the same silence followed by a fresh banner.
 * RCC->CSR holds the answer and costs nothing to read.
 *
 * BOR/POR means the 3V3 rail dipped: the regulator on the BlackPill feeds the
 * GPS, the TFT, the LTIC detector and the clock display, and the receiver draws a
 * step of current when it changes mode. PIN is the reset button or a floating
 * NRST. SFT is NVIC_SystemReset, i.e. our own RB command. The flags latch, so
 * they must be cleared afterwards or the next boot reports this one. */
static void print_reset_cause(void)
{
    uint32_t csr = RCC->CSR;
    OUT_SERIAL.print("Reset cause:");
    if (csr & RCC_CSR_LPWRRSTF) OUT_SERIAL.print(" LOW-POWER");
    if (csr & RCC_CSR_WWDGRSTF) OUT_SERIAL.print(" WINDOW-WDG");
    if (csr & RCC_CSR_IWDGRSTF) OUT_SERIAL.print(" INDEP-WDG");
    if (csr & RCC_CSR_SFTRSTF)  OUT_SERIAL.print(" SOFTWARE");
    if (csr & RCC_CSR_PORRSTF)  OUT_SERIAL.print(" POWER-ON/BROWN-OUT");
    if (csr & RCC_CSR_PINRSTF)  OUT_SERIAL.print(" PIN/NRST");
    if (csr & RCC_CSR_BORRSTF)  OUT_SERIAL.print(" BROWN-OUT");
    OUT_SERIAL.println();
    if (csr & (RCC_CSR_BORRSTF | RCC_CSR_PORRSTF))
        OUT_SERIAL.println("  -> supply dipped: check the 3V3 rail under load");

    RCC->CSR |= RCC_CSR_RMVF;      /* clear, or the next boot reports this one */
}

extern "C" void gpsdo_assert_diagnostic(const char *file, int line)
{
    noInterrupts();
    Serial.println("\r\n!!! FreeRTOS configASSERT failed");
    Serial.print("!!! file: "); Serial.println(file);
    Serial.print("!!! line: "); Serial.println(line);
    Serial.flush();
    /* Rapid LED blink so the fault is visible even without the console. */
    for (int i = 0; i < 40; i++) {
        digitalWrite(PIN_BLUE_LED, (i & 1) ? HIGH : LOW);
        for (volatile uint32_t d = 0; d < 200000; d++) { /* busy-wait, no delay() */
        }
    }
    interrupts();
}

/* Stack overflow hook — configCHECK_FOR_STACK_OVERFLOW == 2 in
 * STM32FreeRTOSConfig.h. FreeRTOS calls this from the tick / task-switch
 * path when a task's stack sentinel is corrupted. pcTaskName is a char[16];
 * print it so the offending task is identifiable. */
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    noInterrupts();
    Serial.println("\r\n!!! FreeRTOS STACK OVERFLOW");
    Serial.print("!!! task: ");
    Serial.println(pcTaskName ? pcTaskName : "(null)");
    Serial.flush();
    for (int i = 0; i < 60; i++) {
        digitalWrite(PIN_BLUE_LED, (i & 1) ? HIGH : LOW);
        for (volatile uint32_t d = 0; d < 300000; d++) {
        }
    }
    interrupts();
}

/* Malloc-failed hook — configUSE_MALLOC_FAILED_HOOK == 1 in
 * STM32FreeRTOSConfig.h. Fires when pvPortMalloc returns NULL inside a
 * FreeRTOS API (typically task/queue creation if the heap is exhausted). */
extern "C" void vApplicationMallocFailedHook(void)
{
    noInterrupts();
    Serial.println("\r\n!!! FreeRTOS MALLOC FAILED (heap exhausted)");
    Serial.flush();
    for (int i = 0; i < 60; i++) {
        digitalWrite(PIN_BLUE_LED, (i & 1) ? HIGH : LOW);
        for (volatile uint32_t d = 0; d < 300000; d++) {
        }
    }
    interrupts();
}

/* The idle hook that used to live here is gone with the spin counter it fed:
 * the CPU figure now comes out of the per-task cycle accounting as 100% minus
 * the idle task's share, which needs no hook and has no reference to
 * mis-calibrate. See gpsdo_health.h. */


void setup()
{
    delay(500);   /* let power stabilise */

    /* ---- Serial ports ---- */
    Serial.begin(115200);

    /*
     * USB CDC enumeration on BlackPill (STM32F411 / F401):
     *
     * The STM32duino CDC implementation requires the host OS to enumerate
     * the virtual COM port before the first Serial.print() is called.
     * Without the wait loop, startup messages are lost (sent before the
     * driver opens the port).
     *
     * Problem when GPSDO_BLUETOOTH is NOT defined:
     *   Serial (CDC) is used for both startup prints AND for the ongoing
     *   1 Hz report.  On some Windows/Linux hosts the driver takes > 2 s
     *   to enumerate after a reset.  The 3 s timeout below handles that.
     *
     * Problem when GPSDO_BLUETOOTH IS defined:
     *   Serial (CDC) is used only for startup prints.
     *   Serial2 (UART2, PA2/PA3) is used for all task-level output.
     *   The 3 s wait still applies so the banner appears on the USB console.
     *
     * If your CDC port shows as "Unknown device" or "device descriptor
     * request failed" on Windows:
     *   1. Check "USB support = CDC (generic Serial supersedes U(S)ART)"
     *      in Arduino IDE Tools menu.
     *   2. Install/update STM32duino CDC driver (zadig or libwdi).
     *   3. Press the BlackPill RESET button after the IDE finishes uploading
     *      — the USB device re-enumerates cleanly.
     *   4. Some Windows hosts require a fresh driver installation when
     *      switching between USB DFU (upload) and CDC (runtime) modes.
     *      Use Zadig to install the "STM32 Virtual COM Port" driver for
     *      the CDC device (VID 0483 PID 5740).
     */
    { uint32_t t0 = millis(); while (!Serial && (millis() - t0) < 3000) delay(10); }
    delay(200);   /* let USB host stabilise before first print */

#if defined(GPSDO_BLUETOOTH) || defined(GPSDO_BLUETOOTH_PARALLEL)
    Serial2.begin(57600);
#endif

    OUT_SERIAL.println("\r\n================================================");
    /* Build stamp in the banner: GPSDO vX.YY.NNrt compiled YYYY-MM-DD HH:MM:SS.
     * __DATE__ is "Mmm dd yyyy" (day space-padded), __TIME__ "HH:MM:SS" — both
     * resolved at compile time, no RTC needed. Digits read by hand: sscanf is
     * not guaranteed under Newlib Nano without scanf support enabled. */
    {
        static const char *MN[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                      "Jul","Aug","Sep","Oct","Nov","Dec" };
        int mo = 0;
        for (int i = 0; i < 12 && mo == 0; i++)
            if (__DATE__[0]==MN[i][0] && __DATE__[1]==MN[i][1] && __DATE__[2]==MN[i][2])
                mo = i + 1;
        int dy = (__DATE__[4]==' ' ? __DATE__[5]-'0'
                                    : (__DATE__[4]-'0')*10 + (__DATE__[5]-'0'));
        int yr = (__DATE__[7]-'0')*1000 + (__DATE__[8]-'0')*100
               + (__DATE__[9]-'0')*10   + (__DATE__[10]-'0');
        int hh = (__TIME__[0]-'0')*10 + (__TIME__[1]-'0');
        int mi = (__TIME__[3]-'0')*10 + (__TIME__[4]-'0');
        int ss = (__TIME__[6]-'0')*10 + (__TIME__[7]-'0');
        /* Composed once into the shared stamp so the banner here and the V
         * command print the same characters — see gpsdo_build.h. */
        snprintf(g_fw_stamp, sizeof(g_fw_stamp),
                 "compiled %04d-%02d-%02d %02d:%02d:%02d  build %d",
                 yr, mo, dy, hh, mi, ss, (int)BUILD_SERIAL);
        OUT_SERIAL.print(PROGRAM_NAME " ");
        OUT_SERIAL.print(g_fw_version);
        OUT_SERIAL.print(" ");
        OUT_SERIAL.println(g_fw_stamp);
    }
    /* WHICH BINARY IS THIS, REALLY. The line above is the compile time of THIS
     * FILE, which is not the same thing as the age of the firmware — the
     * builder reuses object files whose sources have not changed, so a sketch
     * that has not been edited keeps its old stamp however much else changed.
     * On 26.08 two captures from two different builds carried the same one.
     *
     * The CRC is computed at boot from the flash itself, so it cannot be
     * carried over from a cache and it changes if any byte of any translation
     * unit changed. When a log and a memory disagree, believe this number. */
    {
        uint32_t n = fw_image_bytes();
        if (n) OUT_SERIAL.printf("image CRC32 %08lX  (%lu bytes of flash)\r\n",
                                 (unsigned long)fw_image_crc32(),
                                 (unsigned long)n);
        else   OUT_SERIAL.println("image CRC32 unavailable (linker symbols absent)");
    }
    OUT_SERIAL.println("FreeRTOS port by J. M. Niewinski  with Claude, GLM-5.3 Max & Qwen3.8-Max AI");
    OUT_SERIAL.println("https://github.com/jmnlabs/GPSDO_FreeRTOS");
    OUT_SERIAL.println("Inspired by GPSDO v0.06c by " ORIG_AUTHOR_NAME);
    OUT_SERIAL.println("https://github.com/AndrewBCN/STM32-GPSDO");
    OUT_SERIAL.println("Algos 0-2 original design by Andre Balsa");
    OUT_SERIAL.println("Algos 3-9 by J. M. Niewinski");
#ifdef GPSDO_LTIC
    OUT_SERIAL.println("Algo 10 (LTIC 3-stage) inspired by Dan Wiering's measurements");
    OUT_SERIAL.println("Algo 11 (LTIC-Lars) after Lars Walenius' PI loop");
    OUT_SERIAL.println("Algo 12 (multi-level accumulator) after Alan Cashin (MIS42N)");
    OUT_SERIAL.println("Algo 13 (Kalman filter) by J. M. Niewinski - original to this project");
#endif
    OUT_SERIAL.println("Type H = help  SW = stack diagnostics");
    OUT_SERIAL.println("================================================\r\n");
    print_reset_cause();

    /* ---- I2C ---- */
    Wire.begin();
    Wire.setClock(400000);

    /* ---- GPIO ---- */
    pinMode(PIN_BLUE_LED,   OUTPUT);
    pinMode(PIN_YELLOW_LED, OUTPUT);
    digitalWrite(PIN_YELLOW_LED, LOW);    /* OFF at boot — LED on = fix acquired */

#ifdef GPSDO_PICDIV
    pinMode(PIN_PICDIV_ARM, OUTPUT);
    digitalWrite(PIN_PICDIV_ARM, HIGH);
#endif

    /* ---- TFT backlight PWM on PB5 (TIM3 CH2) ----
     * Early, and before the splash: the panel should be lit by the time
     * anything is drawn on it. Applies the compile-time default here; the
     * stored percentage arrives with settings_recall() below, which calls
     * backlight_set() itself. */
    backlight_begin();

    /* ---- 2 kHz test square wave on PB5 (TIM3 CH2) ---- */
#ifdef GPSDO_GEN_2kHz_PB5
    analogWrite(PIN_TEST_2KHZ, 127);
    analogWriteFrequency(2000);
    analogWriteResolution(16);
    analogWrite(PIN_TEST_2KHZ, 32767);
#endif

    /* ---- control-voltage output on PB9 (TIM4 CH4) ---- */
    analogReadResolution(12);

    /* Output stage first: under GPSDO_DAC_EXT this brings the DAC up, and under
     * GPSDO_PWM_DITHER it claims TIM4 and starts the DMA replay. Either way the
     * first command must not arrive before the stage is ready. */
    if (!gpsdo_dac_begin()) {
        OUT_SERIAL.println("HW: control-voltage output failed to start");
    }
    gpsdo_dac_write16(127);

    /* Nothing to set here any more. Both output paths now own TIM4 through
     * registers — pwm24_begin() with the DMA replay, pwm16_begin() without —
     * so the control voltage no longer passes through analogWrite() and no
     * longer depends on analogWriteFrequency(), which is a GLOBAL in the core
     * and would be fought over the moment a second PWM exists. The backlight
     * on PB5 is that second PWM. */

    /* ---- Default control state (overwritten by EEPROM recall below) ---- */
    gCtrl.pwm_output  = DEFAULT_PWM_OUTPUT;
    gCtrl.active_algo = 0;
    gCtrl.holdover_mode = false;
    strcpy(gCtrl.trendstr, " ___");
    g_time_offset_min = 0;

    /* ---- flash ring boot (settings + live data) -----------------------
     *
     * Order matters: flash_ring_begin() validates the sector header and
     * scans slots (any record type). Then settings_recall() applies the
     * newest REC_SETTINGS slot (PWM, algo, TZ, PID, LTIC params, flags).
     * Finally live_store_begin() applies the newest REC_LIVE slot, which
     * OVERRIDES PWM and LRN/LTIC calibration with fresher values.
     *
     * All three are safe before vTaskStartScheduler (no mutex use). If the
     * sector is blank/foreign, flash_ring_begin() reformats it and the two
     * recalls return false — compile-time defaults stay in effect and the
     * user re-runs CT/LC/TZ/ES.
     * ------------------------------------------------------------------ */
    if (flash_ring_begin())
        OUT_SERIAL.println("Flash ring: sector 7 ready");
    else
        OUT_SERIAL.println("Flash ring: sector 7 blank/formatted (defaults)");

    /* recall user settings (PID, TZ, flags, LTIC params) */
    if (settings_recall()) {
        g_persist_valid = true;   /* legacy flag: "settings loaded" */
        OUT_SERIAL.println("Settings: recalled from flash ring");
    } else {
        OUT_SERIAL.println("Settings: none stored (compile-time defaults)");
    }
    gpsdo_dac_write16(gCtrl.pwm_output);

    /* recall learned/calibration values from the ring (if present) */
#ifdef GPSDO_LTIC
    /* Algorithm 13's three numbers live in their own ring record — see
     * kf_store_load(). Silent when there is nothing stored: the defaults are
     * "measure R, adapt Q", which is what a fresh board should do anyway. */
    if (kf_store_load())
        OUT_SERIAL.println("Kalman: KR/KQ/KT recalled from flash ring");
#endif
    if (live_store_begin())
        OUT_SERIAL.println("Live store: LRN + LC applied from flash ring");

    /* Span jumper on PB14: which calibration this board is running on, and —
     * if the jumper was moved while it was off — the recalled code remapped
     * into the span it is now in. Here, after both recalls and before the
     * write below, so the oscillator never sees a code from the wrong span. */
    span_begin();

    /* Re-apply PWM: live_store may have overridden the settings value with a
     * fresher one. The OCXO should not sit at the stale PWM until the loop's
     * first cycle; apply the final value now. */
    gpsdo_dac_write16(gCtrl.pwm_output);

    OUT_SERIAL.print("Initial PWM=");    OUT_SERIAL.print(gCtrl.pwm_output);
    OUT_SERIAL.print(" algo=");          OUT_SERIAL.print(gCtrl.active_algo);
    OUT_SERIAL.print(" time_offset_min="); OUT_SERIAL.println((int)g_time_offset_min);

    /* ---- GPS init ---- */
    gpsdo_gps_init();

    /* ---- Timer2 — OCXO frequency measurement ---- */
    pinMode(PIN_OCXO_ETR, INPUT_PULLUP);
    pinModeAF(PIN_OCXO_ETR, GPIO_AF1_TIM2);

    /* Configure timers but do NOT resume them yet.
     * ISRs must not fire before xPpsQueue / xTwoHzSemaphore are created —
     * calling xQueueSendFromISR(NULL,...) causes an immediate hard fault.
     * Timers are started after all RTOS objects exist (see below). */
    HardwareTimer *FreqTim = new HardwareTimer(TIM2);
    FreqTim->setMode(3, TIMER_INPUT_CAPTURE_RISING, PIN_PPS_CAPTURE);
    TIM2->ARR = 0xFFFFFFFF;
    FreqTim->attachInterrupt(Timer2_Overflow_ISR);
    FreqTim->attachInterrupt(3, Timer2_Capture_ISR);
    TIM2->SMCR |= TIM_SMCR_ECE;
    /* FreqTim->resume() called after RTOS objects created */

    HardwareTimer *Tim2Hz = new HardwareTimer(TIM9);
    Tim2Hz->setOverflow(2, HERTZ_FORMAT);
    Tim2Hz->attachInterrupt(Timer_ISR_2Hz);
    /* Tim2Hz->resume() called after RTOS objects created */

    OUT_SERIAL.println("Hardware configured, creating RTOS objects...");

    /* ====================================================================
     * Create FreeRTOS primitives
     * ==================================================================== */
    xSerialMutex    = xSemaphoreCreateMutex();
    xWireMutex      = xSemaphoreCreateMutex();
    xFreqMutex      = xSemaphoreCreateMutex();
    xGpsMutex       = xSemaphoreCreateMutex();
    xCtrlMutex      = xSemaphoreCreateMutex();
    xTwoHzSemaphore = xSemaphoreCreateBinary();

    xPpsQueue  = xQueueCreate(2, sizeof(PpsEvent_t));
    xCmdQueue  = xQueueCreate(8, 64);

    xSysEvents = xEventGroupCreate();

    /* Request calibration only when EEPROM was blank/erased.
     * When EEPROM was valid, the recalled PWM is already close to optimal
     * and calibration would temporarily disturb the OCXO. */
    if (!g_persist_valid)
        xEventGroupSetBits(xSysEvents, EVT_NEED_CALIBRATION);

    /* ====================================================================
     * Create tasks
     * ==================================================================== */
    xTaskCreate(vFreqRelayTask, "FreqRelay", STACK_ISR_RELAY, NULL, PRI_ISR_RELAY, &xFreqRelayTask);
    xTaskCreate(vControlTask,   "Control",   STACK_CONTROL,   NULL, PRI_CONTROL,   &xControlTask);
    xTaskCreate(vGpsTask,       "GPS",       STACK_GPS,       NULL, PRI_GPS,       &xGpsTask);
    xTaskCreate(vCliTask,       "CLI",       STACK_CLI,       NULL, PRI_CLI,       &xCliTask);
    xTaskCreate(vSensorTask,    "Sensor",    STACK_SENSORS,   NULL, PRI_SENSORS,   &xSensorTask);
    xTaskCreate(vDisplayTask,   "Display",   STACK_DISPLAY,   NULL, PRI_DISPLAY,   &xDisplayTask);
    xTaskCreate(vUptimeTask,    "Uptime",    STACK_UPTIME,    NULL, PRI_UPTIME,    &xUptimeTask);

    /* All RTOS objects exist — NOW safe to enable hardware interrupts */
    FreqTim->resume();
    Tim2Hz->resume();
    OUT_SERIAL.println("Timers started");

    /* The DWT cycle counter, before any context switch can happen: it is what
     * the per-task load accounting measures with. */
    cpu_trace_begin();

    OUT_SERIAL.println("Starting FreeRTOS scheduler");

    /* Start scheduler — this call never returns */
    vTaskStartScheduler();

    /* Should never reach here */
    while (1) {}
}

void loop()
{
    /* Intentionally empty — FreeRTOS tasks own all execution */
}
