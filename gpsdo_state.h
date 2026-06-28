/**
 * gpsdo_state.h — Shared application state and FreeRTOS handles
 *
 * Part of GPSDO FreeRTOS v0.50
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Defines the data structures exchanged between tasks (FreqData_t,
 * GpsData_t, CtrlData_t, Uptime_t, FreqSnap_t) and the extern
 * declarations for all mutexes, queues, event groups, and task handles.
 *
 * Access rules:
 *   gFreq / gFreqSnap  -> xFreqMutex
 *   gGps               -> xGpsMutex
 *   gCtrl              -> xCtrlMutex
 *   gUptime            -> xUptimeMutex
 */
#pragma once
#include "gpsdo_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- ISR -> FreqRelay event ------------------------------------------- */
typedef struct {
    uint32_t ccr_value;
    bool     overflow_flag;
} PpsEvent_t;

/* ---- Full frequency state (ring buffer — ONLY accessed by FreqRelayTask
 *      and ControlTask; NEVER copied whole into another task's stack) ---- */
typedef struct {
    /* Raw counters */
    uint64_t fcount64;
    uint64_t prevfcount64;
    uint32_t lsfcount;
    uint32_t previousfcount;
    uint64_t tim2_overflow_cnt;
    uint64_t calcfreq64;
    uint32_t calcfreqint;

    /* 20 KB ring buffer — reason this struct must NEVER be stack-copied */
    int8_t   circbuf[CIRCBUF_SIZE];
    uint32_t buf_newest;

    /* Cumulative sums (for algorithms) */
    int16_t  cumul10;
    int16_t  cumul100;
    int16_t  cumul1000;
    int16_t  cumul10000;
    int16_t  cumul20000;

    /* Fill flags */
    bool     full10;
    bool     full100;
    bool     full1000;
    bool     full10000;
    bool     full20000;

    /* Frequency averages — computed in ControlTask */
    double   avg10;
    double   avg100;
    double   avg1000;
    double   avg10000;
    double   avg20000;

    /* Misc */
    int8_t   instant_offset;
    uint32_t ppscount;
    bool     flush_requested;
    bool     must_adjust;
} FreqData_t;

/* ---- Display/report snapshot — ~80 bytes, safe to copy onto any stack --
 *
 * Updated by FreqRelayTask under xFreqMutex after each PPS event.
 * Display and Control tasks copy ONLY this struct, never FreqData_t.
 * -----------------------------------------------------------------------*/
typedef struct {
    uint64_t fcount64;
    uint64_t calcfreq64;
    uint32_t calcfreqint;

    double   avg10;
    double   avg100;
    double   avg1000;
    double   avg10000;
    double   avg20000;

    bool     full10;
    bool     full100;
    bool     full1000;
    bool     full10000;
    bool     full20000;

    uint32_t ppscount;
} FreqSnap_t;

/* ---- GPS fix data ----------------------------------------------------- */
typedef struct {
    float    lat, lon, alt;
    uint8_t  sats;
    uint32_t hdop;
    uint8_t  hours, mins, secs;
    uint8_t  day, month;
    uint16_t year;
    bool     valid;      /* true = time received (enables display) */
    bool     pos_valid;  /* true = position fix acquired */
    bool     time_mode;  /* true = receiver in Time Mode (HDOP not meaningful) */
    uint32_t fix_time_ms;
} GpsData_t;

/* ---- Control / PWM state ---------------------------------------------- */
typedef struct {
    uint16_t pwm_output;
    int16_t  avg_vctl_adc;
    int16_t  avg_vcc_adc;
    int16_t  avg_vdd_adc;
    bool     holdover_mode;   /* true = PWM frozen (manual or auto)       */
    bool     holdover_auto;   /* true = triggered automatically by fix loss */
    bool     must_adjust;
    char     trendstr[5];
    uint8_t  active_algo;
} CtrlData_t;

/* ---- Uptime ------------------------------------------------------------ */
typedef struct {
    uint16_t days;
    uint8_t  hours, mins, secs;
    char     time_str[9];   /* "hh:mm:ss\0" */
    char     days_str[5];   /* "000d\0"      */
} Uptime_t;

/* ---- EventGroup bits --------------------------------------------------- */
#define EVT_NEED_CALIBRATION  (1u << 0)
#define EVT_TUNNEL_MODE       (1u << 1)
#define EVT_OCXO_WARM         (1u << 2)
#define EVT_ARM_PICDIV        (1u << 3)
#define EVT_REPORT_TAB        (1u << 4)
#define EVT_HALF_SECOND       (1u << 5)
#define EVT_NEED_TUNE         (1u << 6)   /* CT: calibrate K + auto-tune PID */

/* ---- RTOS handles ------------------------------------------------------ */
extern SemaphoreHandle_t xFreqMutex;
extern SemaphoreHandle_t xGpsMutex;
extern SemaphoreHandle_t xCtrlMutex;
extern SemaphoreHandle_t xUptimeMutex;
/* xSerialMutex: guards REPORT_SERIAL / CLI_SERIAL (same UART).
 * Only used by DisplayTask(pri=1) and CliTask(pri=3).
 * GPS task (pri=4) never writes to REPORT_SERIAL in normal operation
 * → no priority inversion risk. */
extern SemaphoreHandle_t xSerialMutex;
extern SemaphoreHandle_t xWireMutex;   /* guards Wire I2C bus: SensorTask + DisplayTask(OLED) */
extern SemaphoreHandle_t xTwoHzSemaphore;

extern QueueHandle_t xPpsQueue;   /* PpsEvent_t, depth 2 */
extern QueueHandle_t xCmdQueue;   /* char[64],   depth 8 */

extern EventGroupHandle_t xSysEvents;

extern TaskHandle_t xFreqRelayTask;
extern TaskHandle_t xControlTask;
extern TaskHandle_t xGpsTask;
extern TaskHandle_t xCliTask;
extern TaskHandle_t xSensorTask;
extern TaskHandle_t xDisplayTask;
extern TaskHandle_t xUptimeTask;

/* ---- Shared data instances -------------------------------------------- */
extern FreqData_t  gFreq;
extern FreqSnap_t  gFreqSnap;   /* display-safe copy, updated each PPS */
extern GpsData_t   gGps;
extern CtrlData_t  gCtrl;
extern Uptime_t    gUptime;

/* ---- Sensor data (written by SensorTask, read by DisplayTask) ---------- */
extern float  g_bmp_temp, g_bmp_pres, g_bmp_alti;
extern float  g_aht_temp, g_aht_humi;
extern float  g_ina_volt, g_ina_curr;
extern bool   g_eeprom_valid;
extern bool   g_report_paused;        /* true = serial/BT reports paused */
extern bool   g_tz_auto;              /* true = timezone derived from GPS */
extern bool   g_svin_enabled;         /* true = run survey-in on timing rx */

/* Calibration progress — shown on displays during C / CT.
 * Simple scalar types: written by vControlTask, read by vDisplayTask;
 * single-word access is atomic on Cortex-M, no mutex needed. */
extern volatile bool     g_calib_active;     /* true while C/CT running   */
extern volatile uint16_t g_calib_remaining;  /* seconds left in this step */

/* Warmup progress — shown on displays during OCXO warmup at boot. */
extern volatile bool     g_warmup_active;    /* true while warming up      */
extern volatile uint16_t g_warmup_remaining; /* seconds left in warmup     */

/* Survey-in progress — shown on displays during LEA-T Time Mode survey-in.
 * g_svin_dur counts elapsed seconds; g_svin_acc_m is the current accuracy in
 * metres (sqrt of TIM-SVIN meanV variance), both for the display. */
extern volatile bool     g_svin_active;      /* true while survey-in running */
extern volatile bool     g_svin_pending;     /* true = vGpsTask must poll svin */
extern volatile uint16_t g_svin_dur;         /* elapsed survey-in seconds    */
extern volatile uint16_t g_svin_acc_m;       /* current mean 3D accuracy [m] */

/* ---- LTIC (Lars TIC) phase data --------------------------------------- */
/* Written by SensorTask (ADC read + discharge under task context),        */
/* read by DisplayTask. Access is not mutex-guarded because both tasks      */
/* run on the same core (STM32 single-core) and the values are updated      */
/* atomically (int16_t on ARM is a single-instruction write).              */
#ifdef GPSDO_LTIC
  extern volatile bool  g_ltic_must_read;   /* set by PPS capture ISR   */
  extern int16_t        g_ltic_adc_raw;     /* last ADC reading          */
  extern int16_t        g_ltic_adc_avg;     /* 10-sample moving average  */
  extern float          g_ltic_voltage;     /* averaged voltage (V)      */
#endif
