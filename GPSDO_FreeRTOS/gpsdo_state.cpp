/**
 * gpsdo_state.cpp — Shared state instances and persistence wrappers
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 *
 * Instantiates all global state structs and FreeRTOS synchronisation
 * primitives. Persistence (PWM, algorithm, TZ, PID, LTIC params, flags) is
 * handled by gpsdo_settings_store.cpp via the flash ring (sector 7, REC_SETTINGS).
 * The persist_*() wrappers below are kept for the CLI (ES/ER/EE/CR commands)
 * so callers don't have to change — they delegate to the new flash-ring
 * backed settings_store API.
 */
#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include <limits.h>
#include "gpsdo_tz.h"
#include "gpsdo_ubx_timtp.h"
#include "gpsdo_algorithms.h"
#include "gpsdo_settings_store.h"
#include "gpsdo_flash_ring.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

/* ---- RTOS handles ----------------------------------------------------- */
SemaphoreHandle_t xFreqMutex;
SemaphoreHandle_t xGpsMutex;
SemaphoreHandle_t xCtrlMutex;
SemaphoreHandle_t xSerialMutex;
SemaphoreHandle_t xWireMutex;
SemaphoreHandle_t xTwoHzSemaphore;

QueueHandle_t     xPpsQueue;
QueueHandle_t     xCmdQueue;

EventGroupHandle_t xSysEvents;

TaskHandle_t xFreqRelayTask = NULL;
TaskHandle_t xControlTask   = NULL;
TaskHandle_t xGpsTask       = NULL;
TaskHandle_t xCliTask       = NULL;
TaskHandle_t xSensorTask    = NULL;
TaskHandle_t xDisplayTask   = NULL;
TaskHandle_t xUptimeTask    = NULL;

/* ---- Shared data ------------------------------------------------------ */
FreqData_t  gFreq;
FreqSnap_t  gFreqSnap;
GpsData_t   gGps;
CtrlData_t  gCtrl;
volatile uint32_t gUpSecs  = 0;
volatile uint32_t gUpPpsMs = 0;
volatile int32_t  gMcuPpm  = INT32_MIN;
bool        g_persist_valid = false;

/* ---- persistence wrappers (delegating to the flash ring) ----------------
 * The CLI still calls persist_save/recall/erase/check_on_boot by those names
 * (commands ES/ER/EE/CR). Real persistence now lives in gpsdo_settings_store.cpp
 * via the flash ring (sector 7, REC_SETTINGS). These thin wrappers keep the
 * old call sites working without a sweep of every caller.
 *
 * The ring is ALWAYS enabled in v0.96+, so there is no runtime toggle and
 * no STM32duino emulated-EEPROM dependency at all — persistence is 100%% flash ring. */
void persist_save(void)
{
    settings_save();
    /* Snapshot the live data too, so the ring is never older than the
     * settings the user just committed (mirrors the legacy behaviour). */
    extern void live_store_request_save(void);
    live_store_request_save();

    OUT_SERIAL.println("Settings saved (flash ring).");
}

void persist_recall(void)
{
    /* Real recall happens once at boot via settings_recall() in setup().
     * This CLI-triggered recall re-reads the newest REC_SETTINGS slot and
     * re-applies it — useful after a manual ring inspection. */
    bool ok = settings_recall();
    if (ok) {
        OUT_SERIAL.println("Settings recalled (flash ring).");
    } else {
        OUT_SERIAL.println("Settings: no valid slot in ring (defaults).");
    }
}

void persist_erase(void)
{
    /* Cold restart: physically erase and reformat the ring sector so the next
     * boot (and the immediate recall path) start from compile-time defaults.
     * flash_ring_wipe() does the HAL erase then re-inits a fresh header, so the
     * ring is genuinely blank afterwards — not merely marked stale. */
    g_persist_valid = false;

    bool ok = flash_ring_wipe();

    if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gFreq.flush_requested = true;
        xSemaphoreGive(xFreqMutex);
    }
    if (ok) OUT_SERIAL.println("Settings: ring erased — defaults on next boot.");
    else    OUT_SERIAL.println("Settings: ring erase FAILED (flash locked?).");
}

bool persist_check_on_boot(void)
{
    /* The boot path calls settings_recall() directly in setup(); this stub
     * is retained for any caller that still queries g_persist_valid. Returns
     * true once settings_recall() has been run (set by the caller). */
    return g_persist_valid;
}
