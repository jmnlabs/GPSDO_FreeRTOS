/**
 * GPSDO_FreeRTOS.ino — Main entry point — hardware init and FreeRTOS scheduler start
 *
 * Part of GPSDO FreeRTOS v0.95
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
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
#include "gpsdo_state.h"
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>

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
extern bool eeprom_check_on_boot(void);
extern void eeprom_recall(void);
#include "flash_ring.h"
#include "live_store.h"

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

#ifdef GPSDO_BLUETOOTH
HardwareSerial Serial2(PA3, PA2);
#endif

/* ====================================================================== */
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

#ifdef GPSDO_BLUETOOTH
    Serial2.begin(57600);
#endif

    OUT_SERIAL.println("\r\n================================================");
    OUT_SERIAL.println(PROGRAM_NAME " " PROGRAM_VERSION);
    OUT_SERIAL.println("FreeRTOS port by J. M. Niewinski  with Claude AI");
    OUT_SERIAL.println("https://github.com/jmnlabs/GPSDO_FreeRTOS");
    OUT_SERIAL.println("Inspired by GPSDO v0.06c by " AUTHOR_NAME);
    OUT_SERIAL.println("https://github.com/AndrewBCN/STM32-GPSDO");
    OUT_SERIAL.println("Type H = help  SW = stack diagnostics");
    OUT_SERIAL.println("================================================\r\n");

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

    /* ---- 2 kHz test square wave on PB5 (TIM3 CH2) ---- */
#ifdef GPSDO_GEN_2kHz_PB5
    analogWrite(PIN_TEST_2KHZ, 127);
    analogWriteFrequency(2000);
    analogWriteResolution(16);
    analogWrite(PIN_TEST_2KHZ, 32767);
#endif

    /* ---- 16-bit PWM DAC on PB9 (TIM4 CH4) ---- */
    analogReadResolution(12);
    analogWrite(PIN_VCTL_PWM, 127);
    analogWriteFrequency(2000);
    analogWriteResolution(16);

    /* ---- Default control state (overwritten by EEPROM recall below) ---- */
    gCtrl.pwm_output  = DEFAULT_PWM_OUTPUT;
    gCtrl.active_algo = 0;
    gCtrl.holdover_mode = false;
    strcpy(gCtrl.trendstr, " ___");
    g_time_offset_min = 0;

    /* ---- EEPROM boot recall -------------------------------------------
     *
     * eeprom_recall() is safe before vTaskStartScheduler:
     *   - It calls xTaskGetSchedulerState() to detect pre-scheduler context
     *   - Skips mutex acquire when scheduler is not running
     *   - Applies analogWrite(PIN_VCTL_PWM, pwm) immediately
     *   - Restores gCtrl.pwm_output, gCtrl.active_algo, timezone
     *
     * If EEPROM is blank or erased, eeprom_recall() prints a message and
     * returns without changing gCtrl — defaults set above stay in effect.
     * ------------------------------------------------------------------ */
#ifdef GPSDO_EEPROM
    g_eeprom_valid = eeprom_check_on_boot();
    if (g_eeprom_valid) {
        OUT_SERIAL.println("EEPROM: valid signature found — recalling parameters");
        eeprom_recall();   /* sets pwm_output, active_algo, timezone */
    } else {
        OUT_SERIAL.println("EEPROM: blank or erased — using compile-time defaults");
        /* Prime the emulation buffer from flash even though there is nothing to
         * recall. A later selective ES fills the buffer itself, but doing it
         * here too keeps the invariant simple: after boot the buffer always
         * mirrors flash, so no save path can ever flush uninitialised bytes over
         * a group it was not asked to touch. eeprom_check_on_boot() already
         * filled it; this is just making that guarantee explicit and not
         * dependent on that call's internals. */
        eeprom_buffer_fill();
        analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
    }
#else
    analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
#endif

    /* ---- live-data flash ring buffer (wear-levelled; opt-in via
     * FR 0|1, saved in EEPROM). Returns false when disabled. ---- */
    if (flash_ring_begin())
        OUT_SERIAL.println("Flash ring: live data recalled");
    else
        OUT_SERIAL.println("Flash ring: no live data (fresh or disabled)");

    /* recall learned/calibration values from the ring (if present) */
    if (live_store_begin())
        OUT_SERIAL.println("Live store: LRN + LC applied from flash ring");

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
    xUptimeMutex    = xSemaphoreCreateMutex();
    xTwoHzSemaphore = xSemaphoreCreateBinary();

    xPpsQueue  = xQueueCreate(2, sizeof(PpsEvent_t));
    xCmdQueue  = xQueueCreate(8, 64);

    xSysEvents = xEventGroupCreate();

    /* Request calibration only when EEPROM was blank/erased.
     * When EEPROM was valid, the recalled PWM is already close to optimal
     * and calibration would temporarily disturb the OCXO. */
    if (!g_eeprom_valid)
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
