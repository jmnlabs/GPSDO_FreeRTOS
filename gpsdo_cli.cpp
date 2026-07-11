/**
 * gpsdo_cli.cpp — vCliTask — Serial / Bluetooth command line interface
 *
 * Part of GPSDO FreeRTOS v0.91
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Lightweight command parser (no external library).  Reads lines from
 * Serial or Serial2 (Bluetooth), splits into verb + argument, dispatches.
 *
 * Commands cover: report mode (RH/RD/RP/RR), holdover (MH/MD),
 * algorithm selection (LA), PID tuning (KP/KI/KD/IL/BC/BS/NS),
 * EEPROM (ES/ER/EE), time offset (TO), and diagnostics (SW/H).
 */

#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include "GPSDO_algorithms.h"
#include "flash_ring.h"
#include "live_store.h"
#include "ubx_timtp.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#ifdef GPSDO_BLUETOOTH
  #define CLI_SERIAL Serial2
#else
  #define CLI_SERIAL Serial
#endif

/* Case-insensitive string compare for command verbs, so the CLI accepts any
 * letter case ("LA", "la", "La" all match, likewise "up1"/"UP1"). Used in
 * place of strcmp() for the command keywords; returns true when equal. Kept
 * local (not strcasecmp) for toolchain portability under STM32duino. */
static bool cli_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;   /* to lower */
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* -----------------------------------------------------------------------
 * Output helpers - all protected by xSerialMutex
 * ----------------------------------------------------------------------- */
static void cli_puts(const char *s)
{
    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        CLI_SERIAL.print(s);
        xSemaphoreGive(xSerialMutex);
    }
}
static void cli_putln(const char *s)
{
    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        CLI_SERIAL.println(s);
        xSemaphoreGive(xSerialMutex);
    }
}
static void cli_putint(int v)
{
    static char tmp[14];
    ltoa((long)v, tmp, 10);
    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        CLI_SERIAL.print(tmp);
        CLI_SERIAL.print("\r\n");
        xSemaphoreGive(xSerialMutex);
    }
}
static void cli_putfloat(float v, int dec)
{
    static char tmp[24];
    dtostrf(v, -1, dec, tmp);
    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        CLI_SERIAL.print(tmp);
        CLI_SERIAL.print("\r\n");
        xSemaphoreGive(xSerialMutex);
    }
}

/* Shared with control.cpp / gpsdo_tasks.cpp */
extern float   g_pressure_offset;
extern float   g_altitude_offset;
extern int8_t  g_time_offset;
extern bool    g_show_local_time;
extern bool    g_report_paused;
extern bool    g_tz_auto;
extern bool    g_svin_enabled;

/* EEPROM helpers declared in gpsdo_state.cpp */
extern void eeprom_save(void);
extern void eeprom_recall(void);
extern void eeprom_erase(void);

/* -----------------------------------------------------------------------
 * Help text
 * ----------------------------------------------------------------------- */
static void print_help(void)
{
    cli_putln(PROGRAM_NAME " " PROGRAM_VERSION " by jmnlabs (see V)");
    cli_putln("Commands (case-insensitive, end with Enter):");
    cli_putln("  V           Version, authors and links");
    cli_putln("  H / ?       this help");
    cli_putln("  F           Flush frequency ring buffers");
    cli_putln("  C           start auto-Calibration (PWM centring)");
    cli_putln("  CT          Calibrate + auto-Tune PID for all algos");
    cli_putln("  T [baud]    GPS tunnel on USB (300s; opt. GPS UART baud for u-center)");
    cli_putln("  SP <n>      Set PWM DAC directly (1-65535)");
    cli_putln("  up1 / up10  increase PWM by 1 / 10");
    cli_putln("  dp1 / dp10  decrease PWM by 1 / 10");
    cli_putln("  RH / RD     Human readable / Tab Delimited reporting");
    cli_putln("  RP / RR     Report Pause / Report Resume");
    cli_putln("  MH / MD     Mode Holdover / Mode Disciplined");
    cli_putln("  LA <0-9>    Loop Algorithm select (10=LTIC, phase A)");
    cli_putln("  LP [n]      List PID Parameters (algo n or current)");
    cli_putln("  KP n val    set Kp for algo n (3-7)");
    cli_putln("  KI n val    set Ki for algo n (3-7)");
    cli_putln("  KD n val    set Kd for algo n (3-7)");
    cli_putln("  IL n val    set I_LIMIT for algo n (3-9)");
    cli_putln("  BC [val]    algo 8 Blend Crossover (Hz)");
    cli_putln("  BS [val]    algo 8 Blend Scale (Hz)");
    cli_putln("  NS [val]    algo 9 NN max Step (LSB)");
    cli_putln("  -- LTIC (algo 10) params (loop = phase A) --");
    cli_putln("  LC          LTIC self-Calibrate (ns/V, offset, range)");
    cli_putln("  LL          List all LTIC params + state");
    cli_putln("  LNV/LZO/LRN cal: ns/V, zero-offset V, range ns");
    cli_putln("  AQP/I/D/L   ACQ PID Kp/Ki/Kd/I_LIMIT");
    cli_putln("  DPP/I/D/L   DPLL PID Kp/Ki/Kd/I_LIMIT");
    cli_putln("  LKP/I/D/L   LOCK PID Kp/Ki/Kd/I_LIMIT");
    cli_putln("  LAT/LDT/LIV ACQ thr, DPLL->LOCK thr, LOCK interval s");
    cli_putln("  LPOL [-1/0/1] PWM->phase polarity (0=auto)");
    cli_putln("  WU 0|1        - OCXO warmup on boot (saved with ES)");
    cli_putln("  SPL 0|1       - boot animation: 1=full show, 0=static (saved with ES)");
    cli_putln("  LRN 0|1|R     - self-learning drift/damping (R=reset, ES saves)");
    cli_putln("  LCV [V]     ACQ centring target (0=range mid)");
    cli_putln("  AP          Arm picDIV");
    cli_putln("  ES          EEPROM Save");
    cli_putln("  ER          EEPROM Recall");
    cli_putln("  EE          EEPROM Erase");
    cli_putln("  EW          Flash wear stats (ring buffer erase cycles)");
    cli_putln("  FR 0|1      Flash ring buffer on/off (saved with ES)");
    cli_putln("  SAW 0|1     Sawtooth qErr correction on/off (algo 10; saved with ES)");
    cli_putln("  ACG g [cap] ACQ centring drive: LSB/V and max step (algo 10)");
    cli_putln("  RB          Reboot (warm, keep EEPROM)");
    cli_putln("  CR YES      Cold Restart (erase EEPROM, factory)");
    cli_putln("  PO <f>      Pressure Offset");
    cli_putln("  AO <f>      Altitude Offset");
    cli_putln("  TO <n|A>    UTC-to-local offset (-23..23) or Auto from GPS");
#ifdef GPSDO_GPS_TIMING
    cli_putln("  SV <0|1>    Survey-in / Time Mode on timing rx (saved by ES)");
#endif
    cli_putln("  SW          Stack Watermarks (diagnostic)");
}

/* -----------------------------------------------------------------------
 * Command dispatcher
 * ----------------------------------------------------------------------- */
static void dispatch(char *line)
{
    /* Strip trailing whitespace / CR */
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' ||
                        line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0) return;

    /* Split verb / argument */
    char *verb = line;
    char *arg  = NULL;
    for (int i = 0; i < len; i++) {
        if (line[i] == ' ') {
            line[i] = '\0';
            arg = line + i + 1;
            while (*arg == ' ') arg++;
            if (*arg == '\0') arg = NULL;
            break;
        }
    }

    /* ---- version ---- */
    if (cli_ieq(verb, "V")) {
        cli_putln(PROGRAM_NAME " " PROGRAM_VERSION);
        cli_putln("FreeRTOS port & algorithms: J. M. Niewinski (jmnlabs)");
        cli_putln("https://github.com/jmnlabs/GPSDO_FreeRTOS");
        cli_putln("Programming assistant: Claude AI (Anthropic)");
        cli_putln("Inspired by v0.06c by Andre Balsa");
        cli_putln("https://github.com/AndrewBCN/STM32-GPSDO");
        cli_putln("PCB design: Scrachi (EEVBlog forum)");
        return;
    }

    /* ---- help ---- */
    if (cli_ieq(verb, "H") || cli_ieq(verb, "?")) {
        print_help();
        return;
    }

    /* ---- flush ---- */
    if (cli_ieq(verb, "F")) {
        if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            gFreq.flush_requested = true;
            xSemaphoreGive(xFreqMutex);
        }
        cli_putln("Ring buffers flush requested");
        return;
    }

    /* ---- calibration ---- */
    if (cli_ieq(verb, "C")) {
        xEventGroupSetBits(xSysEvents, EVT_NEED_CALIBRATION);
        cli_putln("Auto-calibration sequence started");
        return;
    }

    /* ---- CT: calibrate K + auto-tune all PID from measured gain ---- */
    if (cli_ieq(verb, "CT")) {
        xEventGroupSetBits(xSysEvents, EVT_NEED_TUNE);
        cli_putln("CT: calibrate + auto-tune started (~3 min, 3 PWM points)");
        cli_putln("Derives K then computes PID for algos 3-9. 'ES' saves.");
        return;
    }

#ifdef GPSDO_LTIC
    /* ---- LC: LTIC self-calibration (ns_per_volt, zero_offset, range_ns) ---- */
    if (cli_ieq(verb, "LC")) {
        xEventGroupSetBits(xSysEvents, EVT_NEED_LTIC_CAL);
        cli_putln("LC: LTIC self-calibration started (auto: arms picDIV, centres phase, then ~3 min sweep)");
        cli_putln("Measures TIC slope vs known phase rate. Auto-saves to flash ring if it passes.");
        return;
    }
#endif

    /* ---- tunnel mode ---- */
    if (cli_ieq(verb, "T")) {
        /* Optional baud: "T 115200" reopens the GPS UART at that rate for the
         * bridge AND keeps it afterwards — needed when u-center reconfigures
         * the receiver's port speed (the firmware must follow to keep parsing
         * NMEA after the session). USB CDC itself has no real baud rate, so
         * only the GPS side matters. Bridge always runs on USB (Serial); with
         * Bluetooth enabled, CLI and telemetry stay on BT undisturbed. */
        extern volatile uint32_t g_tunnel_baud;
        g_tunnel_baud = 0;
        if (arg != NULL) {
            long b = atol(arg);
            if (b >= 4800 && b <= 921600) {
                g_tunnel_baud = (uint32_t)b;
                cli_puts("Tunnel GPS baud: "); cli_putint((int)b); cli_putln("");
            } else {
                cli_putln("T: baud 4800..921600 (or no arg = keep current)");
                return;
            }
        }
        xEventGroupSetBits(xSysEvents, EVT_TUNNEL_MODE);
        cli_putln("Switching to GPS tunnel mode (bridge on USB)");
        return;
    }

    /* ---- reporting format ---- */
    if (cli_ieq(verb, "RH")) {
        xEventGroupClearBits(xSysEvents, EVT_REPORT_TAB);
        cli_putln("Switching to Human Readable reporting");
        return;
    }
    if (cli_ieq(verb, "RD")) {
        xEventGroupSetBits(xSysEvents, EVT_REPORT_TAB);
        cli_putln("Switching to Tab Delimited reporting");
        return;
    }
    if (cli_ieq(verb, "RP")) {
        g_report_paused = true;
        cli_putln("Reports paused (type RR to resume)");
        return;
    }
    if (cli_ieq(verb, "RR")) {
        g_report_paused = false;
        cli_putln("Reports resumed");
        return;
    }

    /* ---- holdover / disciplined ---- */
    if (cli_ieq(verb, "MH")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            gCtrl.holdover_mode = true;
            gCtrl.holdover_auto = false;   /* manual override — clear auto flag */
            xSemaphoreGive(xCtrlMutex);
        }
        cli_putln("Switching to Holdover Mode (manual)");
        return;
    }
    if (cli_ieq(verb, "MD")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            gCtrl.holdover_mode = false;
            gCtrl.holdover_auto = false;   /* clear auto flag too */
            xSemaphoreGive(xCtrlMutex);
        }
        cli_putln("Switching to Disciplined Mode");
        return;
    }

    /* ---- arm picDIV ---- */
    if (cli_ieq(verb, "AP")) {
#ifdef GPSDO_PICDIV
        xEventGroupSetBits(xSysEvents, EVT_ARM_PICDIV);
        cli_putln("picDIV arm requested (1.0-1.2s output gap, then syncs to 1PPS)");
        cli_putln("Note: use a PLL algorithm (LA 4/5/7) to keep phase locked long-term");
#else
        cli_putln("picDIV support not compiled in (GPSDO_PICDIV)");
#endif
        return;
    }

    /* ---- PWM adjustments ---- */
    /* Block manual PWM nudges while a calibration is sweeping: LC/CT drive
     * PWM themselves and measure the response, so a manual step corrupts the
     * slope/range measurement. One guard covers up1/up10/dp1/dp10/SP. */
    if (cli_ieq(verb, "up1") || cli_ieq(verb, "up10") ||
        cli_ieq(verb, "dp1") || cli_ieq(verb, "dp10") || cli_ieq(verb, "SP")) {
        if (g_calib_active) {
            cli_putln("Busy: calibration in progress — PWM change ignored (wait for LC/CT to finish).");
            return;
        }
    }

    if (cli_ieq(verb, "up1")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output < 65535) gCtrl.pwm_output++;
            analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM+1: "); cli_putint(gCtrl.pwm_output);
        return;
    }
    if (cli_ieq(verb, "up10")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output <= 65525) gCtrl.pwm_output += 10;
            analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM+10: "); cli_putint(gCtrl.pwm_output);
        return;
    }
    if (cli_ieq(verb, "dp1")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output > 1) gCtrl.pwm_output--;
            analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM-1: "); cli_putint(gCtrl.pwm_output);
        return;
    }
    if (cli_ieq(verb, "dp10")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output >= 11) gCtrl.pwm_output -= 10;
            analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM-10: "); cli_putint(gCtrl.pwm_output);
        return;
    }

    /* ---- SP <n> ---- */
    if (cli_ieq(verb, "SP")) {
        if (arg == NULL) {
            cli_puts("No value. Default PWM: ");
            cli_putint(DEFAULT_PWM_OUTPUT);
            if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                gCtrl.pwm_output = DEFAULT_PWM_OUTPUT;
                analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
                xSemaphoreGive(xCtrlMutex);
            }
        } else {
            int32_t v = atoi(arg);
            if (v >= 1 && v <= 65535) {
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.pwm_output = (uint16_t)v;
                    analogWrite(PIN_VCTL_PWM, gCtrl.pwm_output);
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_puts("PWM set: "); cli_putint((int)v);
            } else {
                cli_putln("SP: value must be 1..65535");
            }
        }
        return;
    }

    /* ---- LA <0-9> ---- */
    if (cli_ieq(verb, "LA")) {
        if (arg == NULL) {
            cli_puts("Algorithm: "); cli_putint(gCtrl.active_algo);
        } else {
            int v = atoi(arg);
            if (v == 10) {
#ifdef GPSDO_LTIC
                /* Algo 10 = LTIC three-stage PLL. Allowed even uncalibrated,
                 * but warn — the loop then uses a nominal V-based phase. */
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = 10;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_putln("Algorithm: 10 (LTIC three-stage ACQ/DPLL/LOCK)");
                if (g_ltic.ns_per_volt == 0.0f)
                    cli_putln("WARNING: LTIC uncalibrated — run LC first for ns-accurate phase.");
                cli_putln("picDIV will arm on ACQ entry. Watch trend: ACQ/DPLL/LOCK.");
#else
                cli_putln("LA 10 needs GPSDO_LTIC enabled at build time.");
#endif
            } else if (v >= 0 && v <= 9) {
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = (uint8_t)v;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_puts("Algorithm: "); cli_putint(v);
            } else {
                cli_putln("LA: value must be 0..9 (10=LTIC, not yet available)");
            }
        }
        return;
    }

    /* ---- LP [n] — List PID Parameters ---- */
    if (cli_ieq(verb, "LP")) {
        int n = (arg != NULL) ? atoi(arg) : (int)gCtrl.active_algo;
        if (n < 0 || n > 9) { cli_putln("LP: algo 0..9"); return; }

        /* Algo 8 (hybrid) reads its gains from g_pid[6] (FLL branch) and
         * g_pid[7] (PLL branch), not from g_pid[8]; algo 9 (NN) uses fixed
         * network weights, only NS/IL matter.  Show this so the listing
         * isn't mistaken for "not tuned". */
        if (n == 8) {
            cli_putln("Algo 8 hybrid — uses algo 6 (FLL) + algo 7 (PLL) gains:");
            cli_puts("  FLL[6] Kp="); cli_putfloat((float)g_pid[6].Kp, 1);
            cli_puts(" Ki=");          cli_putfloat((float)g_pid[6].Ki, 4);
            cli_puts(" Kd=");          cli_putfloat((float)g_pid[6].Kd, 0);
            cli_putln("");
            cli_puts("  PLL[7] Kp="); cli_putfloat((float)g_pid[7].Kp, 1);
            cli_puts(" Ki=");          cli_putfloat((float)g_pid[7].Ki, 4);
            cli_puts(" Kd=");          cli_putfloat((float)g_pid[7].Kd, 1);
            cli_putln("");
            cli_puts("  blend BC=");   cli_putfloat((float)g_blend_crossover, 4);
            cli_puts(" BS=");          cli_putfloat((float)g_blend_scale, 4);
            cli_puts("  IL=");         cli_putfloat((float)g_pid[8].I_LIMIT, 1);
            return;
        }
        if (n == 9) {
            cli_putln("Algo 9 NN — fixed network weights; only these apply:");
            cli_puts("  NS="); cli_putfloat((float)g_nn_max_step, 1);
            cli_puts("  IL="); cli_putfloat((float)g_pid[9].I_LIMIT, 1);
            return;
        }

        char tmp[64];
        snprintf(tmp, sizeof(tmp), "Algo %d  Kp=", n); cli_puts(tmp);
        cli_putfloat((float)g_pid[n].Kp, 4);
        cli_puts("  Ki="); cli_putfloat((float)g_pid[n].Ki, 6);
        cli_puts("  Kd="); cli_putfloat((float)g_pid[n].Kd, 3);
        cli_puts("  IL="); cli_putfloat((float)g_pid[n].I_LIMIT, 1);
        return;
    }

    /* ---- KP/KI/KD/IL n val — set PID param for algo n ----
     * arg format: "n val" (e.g. "3 100.5")
     * Split arg into algo number and float value. */
    if (cli_ieq(verb, "KP") || cli_ieq(verb, "KI") ||
        cli_ieq(verb, "KD") || cli_ieq(verb, "IL"))
    {
        if (arg == NULL) {
            cli_puts(verb); cli_putln(": need <algo> <value>");
            return;
        }
        /* Split arg: first token = algo, rest = value */
        int n = atoi(arg);
        char *val_str = arg;
        while (*val_str && *val_str != ' ') val_str++;
        while (*val_str == ' ') val_str++;
        if (*val_str == '\0') {
            /* No value given — show current */
            if (n < 0 || n > 9) { cli_puts(verb); cli_putln(": algo 0..9"); return; }
            double cur = 0.0;
            char vk = verb[1];
            if (vk >= 'a' && vk <= 'z') vk -= 32;   /* to upper for the test */
            if      (vk == 'P') cur = g_pid[n].Kp;
            else if (vk == 'I') cur = g_pid[n].Ki;
            else if (vk == 'D') cur = g_pid[n].Kd;
            else                cur = g_pid[n].I_LIMIT;
            char tmp[48]; snprintf(tmp, sizeof(tmp), "Algo %d %s=", n, verb);
            cli_puts(tmp); cli_putfloat((float)cur, 6);
            return;
        }

        double val = atof(val_str);
        bool ok_range = true;
        if (cli_ieq(verb, "IL")) {
            /* I_LIMIT valid for algos 3-9, range 100..100000 */
            if (n < 3 || n > 9 || val < 100.0 || val > 100000.0) ok_range = false;
        } else {
            /* KP/KI/KD valid for algos 3-7, range 0..100000 */
            if (n < 3 || n > 7 || val < 0.0 || val > 100000.0) ok_range = false;
        }
        if (!ok_range) {
            cli_puts(verb);
            if (cli_ieq(verb, "IL")) cli_putln(": algo 3-9, val 100..100000");
            else                         cli_putln(": algo 3-7, val 0..100000");
            return;
        }

        char vk2 = verb[1];
        if (vk2 >= 'a' && vk2 <= 'z') vk2 -= 32;   /* to upper for the test */
        if      (vk2 == 'P') g_pid[n].Kp      = val;
        else if (vk2 == 'I') g_pid[n].Ki      = val;
        else if (vk2 == 'D') g_pid[n].Kd      = val;
        else                 g_pid[n].I_LIMIT  = val;

        char tmp[48]; snprintf(tmp, sizeof(tmp), "Algo %d %s=", n, verb);
        cli_puts(tmp); cli_putfloat((float)val, 6);
        return;
    }

    /* ====================================================================
     * Algorithm 10 (LTIC) parameter commands. The loop is not implemented
     * yet (phase A) — these set/show the persisted parameters so the config
     * is ready. A single helper handles the "show if no arg, else set with
     * range check" pattern for the float fields.
     * ==================================================================== */
    {
        /* table-free dispatch: each verb maps to a float* and a range */
        float *fp = NULL; float lo = 0, hi = 0; const char *lbl = NULL;
        if      (cli_ieq(verb,"LNV")) { fp=&g_ltic.ns_per_volt;     lo=0;     hi=1e6f;    lbl="ns_per_volt"; }
        else if (cli_ieq(verb,"LZO")) { fp=&g_ltic.zero_offset;     lo=0;     hi=3.3f;    lbl="zero_offset[V]"; }
        else if (cli_ieq(verb,"LRN")) { fp=&g_ltic.range_ns;        lo=0;     hi=1e9f;    lbl="range_ns"; }
        else if (cli_ieq(verb,"LAT")) { fp=&g_ltic.acq_threshold_ns;lo=0.001f;hi=1e9f;    lbl="acq_thresh_ns"; }
        else if (cli_ieq(verb,"LDT")) { fp=&g_ltic.dpll_lock_thresh;lo=1e-13f;hi=1.0f;    lbl="dpll_lock_thr"; }
        if (fp) {
            if (arg == NULL) { cli_puts(lbl); cli_puts("="); cli_putfloat(*fp, 6); }
            else {
                double v = atof(arg);
                if (v >= lo && v <= hi) { *fp = (float)v; cli_puts(lbl); cli_puts("="); cli_putfloat((float)v, 6); }
                else { cli_puts(verb); cli_putln(": out of range"); }
            }
            return;
        }
    }
    {
        /* ACQ/DPLL/LOCK PID: verbs AQP/AQI/AQD/AQL, DPP/DPI/DPD/DPL, LKP/LKI/LKD/LKL */
        PidParams_t *pid = NULL; const char *which = NULL;
        if      (cli_ieq(verb,"AQP")||cli_ieq(verb,"AQI")||cli_ieq(verb,"AQD")||cli_ieq(verb,"AQL")) { pid=&g_ltic.acq;  which="ACQ";  }
        else if (cli_ieq(verb,"DPP")||cli_ieq(verb,"DPI")||cli_ieq(verb,"DPD")||cli_ieq(verb,"DPL")) { pid=&g_ltic.dpll; which="DPLL"; }
        else if (cli_ieq(verb,"LKP")||cli_ieq(verb,"LKI")||cli_ieq(verb,"LKD")||cli_ieq(verb,"LKL")) { pid=&g_ltic.lock; which="LOCK"; }
        if (pid) {
            char k = verb[2]; if (k>='a'&&k<='z') k-=32;   /* P/I/D/L, case-insensitive */
            double *tgt; const char *kn; double rlo, rhi;
            if      (k=='P') { tgt=&pid->Kp;      kn="Kp"; rlo=0; rhi=100000.0; }
            else if (k=='I') { tgt=&pid->Ki;      kn="Ki"; rlo=0; rhi=100000.0; }
            else if (k=='D') { tgt=&pid->Kd;      kn="Kd"; rlo=0; rhi=100000.0; }
            else             { tgt=&pid->I_LIMIT; kn="IL"; rlo=0; rhi=100000.0; }
            if (arg == NULL) { cli_puts(which); cli_puts(" "); cli_puts(kn); cli_puts("="); cli_putfloat((float)*tgt, 6); }
            else {
                double v = atof(arg);
                if (v >= rlo && v <= rhi) { *tgt = v; cli_puts(which); cli_puts(" "); cli_puts(kn); cli_puts("="); cli_putfloat((float)v, 6); }
                else { cli_puts(verb); cli_putln(": out of range"); }
            }
            return;
        }
    }
    /* ---- LIV [val] — LOCK update interval [s] ---- */
    if (cli_ieq(verb, "LIV")) {
        if (arg == NULL) { cli_puts("lock_interval_s="); cli_putint(g_ltic.lock_interval_s); }
        else {
            long v = atol(arg);
            if (v >= 1 && v <= 30) { g_ltic.lock_interval_s = (uint16_t)v; cli_puts("lock_interval_s="); cli_putint((int)v); }
            else cli_putln("LIV: 1..30 s (LOCK correction interval)");
        }
        return;
    }
    /* ---- LPOL [-1/0/1] — PWM→phase polarity (0=auto-detect) ---- */
    if (cli_ieq(verb, "LRN")) {
        if (arg == NULL) {
            cli_puts("learn="); cli_puts(g_lrn_enable ? "1 (on)" : "0 (off)");
            cli_puts("  drift="); cli_putfloat(g_lrn_drift, 1);
            cli_puts(" LSB  damp="); cli_putfloat(g_lrn_damp, 3);
            cli_puts("  slope="); cli_putfloat(g_lrn_slope_ns_s, 3);
            cli_puts(" ns/s  osc="); cli_putint(g_lrn_osc_period);
            cli_puts("s/"); cli_putfloat(g_lrn_osc_amp_ns, 1); cli_putln("ns");
        } else if (arg[0] == 'R' || arg[0] == 'r') {
            g_lrn_drift = 0.0f; g_lrn_damp = 1.0f;
            cli_putln("LRN: learned drift/damping reset to theory (ES to save)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) { g_lrn_enable = (v != 0); cli_puts("learn="); cli_putint(v); cli_putln(" (ES to save)"); }
            else cli_putln("LRN: 0 (off), 1 (on), or R (reset learned values)");
        }
        return;
    }
    if (cli_ieq(verb, "WU")) {
        if (arg == NULL) {
            cli_puts("warmup="); cli_putln(g_warmup_enable ? "1 (on)" : "0 (off)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) { g_warmup_enable = (v != 0); cli_puts("warmup="); cli_putint(v); cli_putln(" (ES to save)"); }
            else cli_putln("WU: 0 (skip warmup) or 1 (warm up on boot)");
        }
        return;
    }
    if (cli_ieq(verb, "SPL")) {
        /* the boot animation on/off. Off = philistine mode: title + credits,
         * two seconds, no art. On = the full oscillators-into-lock show. */
        if (arg == NULL) {
            cli_puts("splash="); cli_putln(g_splash_enable ? "1 (animated)" : "0 (static)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) { g_splash_enable = (v != 0); cli_puts("splash="); cli_putint(v); cli_putln(" (ES to save)"); }
            else cli_putln("SPL: 0 (static title only) or 1 (boot animation)");
        }
        return;
    }
    if (cli_ieq(verb, "LPOL")) {
        if (arg == NULL) {
            cli_puts("polarity="); cli_putint(g_ltic.polarity);
            cli_putln(g_ltic.polarity == 0 ? " (auto)" : "");
        } else {
            int v = atoi(arg);
            if (v == -1 || v == 0 || v == 1) { g_ltic.polarity = (int8_t)v; cli_puts("polarity="); cli_putint(v); }
            else cli_putln("LPOL: -1, 0 (auto), or 1");
        }
        return;
    }
    /* ---- LCV [volts] — ACQ centring target (0=use range middle) ---- */
    if (cli_ieq(verb, "LCV")) {
        if (arg == NULL) { cli_puts("centre_v="); cli_putfloat(g_ltic.centre_v, 3); }
        else {
            double v = atof(arg);
            if (v >= 0.0 && v <= 3.3) { g_ltic.centre_v = (float)v; cli_puts("centre_v="); cli_putfloat((float)v, 3); }
            else cli_putln("LCV: 0..3.3 V (0=auto)");
        }
        return;
    }
    /* ---- LL — list all LTIC parameters + current state ---- */
    if (cli_ieq(verb, "LL")) {
        cli_putln("LTIC (algo 10) parameters:");
        cli_puts("  cal: LNV="); cli_putfloat(g_ltic.ns_per_volt, 4);
        cli_puts(" LZO=");       cli_putfloat(g_ltic.zero_offset, 4);
        cli_puts(" LRN=");       cli_putfloat(g_ltic.range_ns, 2);
        cli_putln(g_ltic.ns_per_volt == 0.0f ? "  (UNCALIBRATED)" : "");
        cli_puts("  ACQ:  Kp="); cli_putfloat((float)g_ltic.acq.Kp, 4);
        cli_puts(" Ki=");        cli_putfloat((float)g_ltic.acq.Ki, 4);
        cli_puts(" Kd=");        cli_putfloat((float)g_ltic.acq.Kd, 4);
        cli_puts(" IL=");        cli_putfloat((float)g_ltic.acq.I_LIMIT, 1);
        cli_putln("");
        cli_puts("  DPLL: Kp="); cli_putfloat((float)g_ltic.dpll.Kp, 4);
        cli_puts(" Ki=");        cli_putfloat((float)g_ltic.dpll.Ki, 4);
        cli_puts(" Kd=");        cli_putfloat((float)g_ltic.dpll.Kd, 4);
        cli_puts(" IL=");        cli_putfloat((float)g_ltic.dpll.I_LIMIT, 1);
        cli_putln("");
        cli_puts("  LOCK: Kp="); cli_putfloat((float)g_ltic.lock.Kp, 4);
        cli_puts(" Ki=");        cli_putfloat((float)g_ltic.lock.Ki, 4);
        cli_puts(" Kd=");        cli_putfloat((float)g_ltic.lock.Kd, 4);
        cli_puts(" IL=");        cli_putfloat((float)g_ltic.lock.I_LIMIT, 1);
        cli_putln("");
        cli_puts("  LAT=");      cli_putfloat(g_ltic.acq_threshold_ns, 2);
        cli_puts(" LDT=");       cli_putfloat(g_ltic.dpll_lock_thresh, 12);
        cli_puts(" LIV=");       cli_putint(g_ltic.lock_interval_s);
        cli_putln("");
        cli_puts("  LPOL=");     cli_putint(g_ltic.polarity);
        cli_puts(g_ltic.polarity == 0 ? " (auto)" : "");
        cli_puts("  LCV=");      cli_putfloat(g_ltic.centre_v, 3);
        cli_putln(g_ltic.centre_v == 0.0f ? " (auto=range mid)" : "");
        const char *sn = (g_ltic.state==LTIC_ACQ)?"ACQ":(g_ltic.state==LTIC_DPLL)?"DPLL":"LOCK";
        cli_puts("  state="); cli_puts(sn);
        cli_putln("  (loop not yet implemented — phase A)");
        return;
    }

    /* ---- BC [val] — algo 8 Blend Crossover ---- */
    if (cli_ieq(verb, "BC")) {
        if (arg == NULL) {
            cli_puts("Blend crossover: "); cli_putfloat((float)g_blend_crossover, 4);
        } else {
            double v = atof(arg);
            if (v > 0.0 && v < 1.0) {
                g_blend_crossover = v;
                cli_puts("Blend crossover: "); cli_putfloat((float)v, 4);
            } else {
                cli_putln("BC: value must be 0.0001..1.0");
            }
        }
        return;
    }

    /* ---- BS [val] — algo 8 Blend Scale ---- */
    if (cli_ieq(verb, "BS")) {
        if (arg == NULL) {
            cli_puts("Blend scale: "); cli_putfloat((float)g_blend_scale, 4);
        } else {
            double v = atof(arg);
            if (v > 0.0 && v < 1.0) {
                g_blend_scale = v;
                cli_puts("Blend scale: "); cli_putfloat((float)v, 4);
            } else {
                cli_putln("BS: value must be 0.0001..1.0");
            }
        }
        return;
    }

    /* ---- NS [val] — algo 9 NN max Step ---- */
    if (cli_ieq(verb, "NS")) {
        if (arg == NULL) {
            cli_puts("NN max step: "); cli_putfloat((float)g_nn_max_step, 1);
        } else {
            double v = atof(arg);
            if (v >= 1.0 && v <= 10000.0) {
                g_nn_max_step = v;
                cli_puts("NN max step: "); cli_putfloat((float)v, 1);
            } else {
                cli_putln("NS: value must be 1..10000");
            }
        }
        return;
    }

    /* ---- TO [n | A] — time offset: manual hours or Auto from GPS ---- */
    if (cli_ieq(verb, "TO")) {
        if (arg == NULL) {
            cli_puts("Time offset: "); cli_putint((int)g_time_offset);
            cli_puts(g_tz_auto ? "  (auto: GPS position + EU DST)"
                               : "  (manual)");
        } else if (arg[0] == 'A' || arg[0] == 'a') {
            g_tz_auto = true;
            cli_putln("Time offset: AUTO — zone from GPS position, EU DST rule");
            cli_putln("(applied on next fix; ES saves the mode to EEPROM)");
        } else {
            int v = atoi(arg);
            if (v >= -23 && v <= 23) {
                g_tz_auto     = false;
                g_time_offset = (int8_t)v;
                cli_puts("Time offset: "); cli_putint(v);
                cli_puts("  (manual)");
            } else {
                cli_putln("TO: value must be -23..23, or A for auto");
            }
        }
        return;
    }

    /* ---- SV [0|1] — survey-in (Time Mode) enable, saved by ES ---- */
    if (cli_ieq(verb, "SV")) {
#ifdef GPSDO_GPS_TIMING
        if (arg == NULL) {
            cli_puts("Survey-in (Time Mode): ");
            cli_putln(g_svin_enabled ? "ENABLED" : "DISABLED");
            cli_putln("(SV 1 = on, SV 0 = off; takes effect at next boot; ES saves)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) {
                g_svin_enabled = (v == 1);
                cli_puts("Survey-in: ");
                cli_putln(g_svin_enabled ? "ENABLED (next boot)" : "DISABLED (next boot)");
                cli_putln("(ES to save; reboot to apply)");
            } else {
                cli_putln("SV: use 0 (off) or 1 (on)");
            }
        }
#else
        cli_putln("SV: firmware built without GPSDO_GPS_TIMING");
#endif
        return;
    }

    /* ---- PO <f> ---- */
    if (cli_ieq(verb, "PO")) {
        if (arg == NULL) {
            cli_puts("Pressure offset: "); cli_putfloat(g_pressure_offset, 2);
        } else {
            float v = (float)atof(arg);
            if (v >= -3000.0f && v <= 3000.0f && v != 0.0f) {
                g_pressure_offset = v;
                cli_puts("Pressure offset: "); cli_putfloat(v, 2);
            } else {
                cli_putln("PO: invalid value");
            }
        }
        return;
    }

    /* ---- AO <f> ---- */
    if (cli_ieq(verb, "AO")) {
        if (arg == NULL) {
            cli_puts("Altitude offset: "); cli_putfloat(g_altitude_offset, 2);
        } else {
            float v = (float)atof(arg);
            if (v >= -3000.0f && v <= 3000.0f && v != 0.0f) {
                g_altitude_offset = v;
                cli_puts("Altitude offset: "); cli_putfloat(v, 2);
            } else {
                cli_putln("AO: invalid value");
            }
        }
        return;
    }

    /* ---- EEPROM ---- */
    if (cli_ieq(verb, "ES")) {
        cli_putln("Saving EEPROM...");
        eeprom_save();
        /* also snapshot the live data to the flash ring, so the ring is
         * never older than the settings the user just committed (a stale
         * ring slot restoring an old PWM after a deliberate ES would be
         * confusing; costs one of 4095 slots) */
        live_store_request_save();
        cli_putln("Done.");
        return;
    }
    if (cli_ieq(verb, "ER")) {
        cli_putln("Recalling EEPROM...");
        eeprom_recall();
        return;
    }
    if (cli_ieq(verb, "EE")) {
        cli_putln("Erasing EEPROM...");
        eeprom_erase();
        cli_putln("Done.");
        return;
    }
    if (cli_ieq(verb, "EW")) {
        /* flash-wear diagnostics for the live-data ring buffer */
        uint16_t slots = flash_ring_slot_count();
        if (slots == 0) {
            cli_putln("Flash ring: disabled (FR 0). Enable with 'FR 1' then 'ES'.");
        } else {
            char line[96];
            snprintf(line, sizeof(line),
                     "Flash ring: erase cycles=%lu  slots used=%u/%u  (sector 6, 0x08040000)",
                     (unsigned long)flash_ring_erase_count(),
                     (unsigned)flash_ring_slots_used(), (unsigned)slots);
            cli_putln(line);
        }
        return;
    }
    /* ---- ACG <gain> [cap] — ACQ centring drive (algo 10) ---- */
    if (cli_ieq(verb, "ACG")) {
        if (arg == NULL) {
            cli_puts("ACG gain="); cli_putfloat(g_ltic_acq_centre_gain, 0);
            cli_puts(" LSB/V  cap="); cli_putfloat(g_ltic_acq_centre_cap, 0);
            cli_putln(" LSB");
        } else {
            float g = (float)atof(arg);
            char *sp = strchr(arg, ' ');
            if (g >= 50.0f && g <= 20000.0f) {
                g_ltic_acq_centre_gain = g;
                if (sp != NULL) {
                    float cp = (float)atof(sp + 1);
                    if (cp >= 5.0f && cp <= 1000.0f) g_ltic_acq_centre_cap = cp;
                    else { cli_putln("ACG: cap 5..1000 LSB"); return; }
                }
                cli_puts("ACG gain="); cli_putfloat(g_ltic_acq_centre_gain, 0);
                cli_puts(" cap="); cli_putfloat(g_ltic_acq_centre_cap, 0);
                cli_putln("  (higher = faster centring, risk of wrap)");
            } else {
                cli_putln("ACG: gain 50..20000 LSB/V [cap 5..1000 LSB]");
            }
        }
        return;
    }

    if (cli_ieq(verb, "SAW")) {
        /* sawtooth (qErr) correction on/off + status. Subtracts the receiver
         * quantization error (UBX-TIM-TP) from the TIC phase. */
        if (arg == NULL) {
            cli_puts("sawtooth="); cli_puts(g_qerr_enable ? "1 (on)" : "0 (off)");
            cli_puts("  qErr=");
            if (g_qerr_valid) { cli_putfloat(g_qerr_ns, 1); cli_puts(" ns"); }
            else              { cli_puts("(no TIM-TP)"); }
            cli_puts("  frames="); cli_putint((int)g_qerr_count);
            cli_putln("");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) {
                g_qerr_enable = (v != 0);
                cli_puts("sawtooth="); cli_putint(v); cli_putln(" (ES to save)");
            } else {
                cli_putln("SAW: 0 (off) or 1 (on); no arg = status");
            }
        }
        return;
    }

    if (cli_ieq(verb, "FR")) {
        /* enable/disable the wear-levelled flash ring buffer at runtime.
         * Enabling initialises the ring immediately so EW works at once. */
        if (arg == NULL) {
            cli_puts("flash_ring="); cli_putln(g_flash_ring_enable ? "1 (on)" : "0 (off)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) {
                g_flash_ring_enable = (v != 0);
                if (g_flash_ring_enable) flash_ring_begin();   /* init now */
                cli_puts("flash_ring="); cli_putint(v); cli_putln(" (ES to save)");
            } else {
                cli_putln("FR: 0 (disable) or 1 (enable ring buffer)");
            }
        }
        return;
    }

    /* ---- RB — warm reboot: software reset, EEPROM kept ----
     * Restarts the firmware via NVIC_SystemReset(). EEPROM (PWM, model,
     * calibration, LTIC params) is preserved, so after reboot the OCXO — still
     * warm — recalls its disciplined state. Does NOT auto-save first; run ES
     * beforehand if you have unsaved changes. */
    if (cli_ieq(verb, "RB")) {
        cli_putln("Warm reboot (EEPROM kept). Resetting...");
        vTaskDelay(pdMS_TO_TICKS(150));   /* let the line flush */
        NVIC_SystemReset();
        return;                           /* not reached */
    }
    /* ---- CR YES — cold restart: erase EEPROM, then software reset ----
     * Wipes the EEPROM (back to compile-time defaults: PWM=DEFAULT, survey-in
     * from scratch, no learned OCXO model, LTIC params reset) and reboots, as
     * if powered on for the first time. Requires the literal confirmation
     * "CR YES" because it discards the learned model (days to rebuild). */
    if (cli_ieq(verb, "CR")) {
        if (arg != NULL && cli_ieq(arg, "YES")) {
            cli_putln("Cold restart: erasing EEPROM and rebooting...");
            eeprom_erase();
            vTaskDelay(pdMS_TO_TICKS(150));
            NVIC_SystemReset();
        } else {
            cli_putln("Cold restart wipes EEPROM (PWM, model, calibration, "
                      "LTIC). Type 'CR YES' to confirm.");
        }
        return;
    }

    /* ---- SW - stack watermarks ---- */
    if (cli_ieq(verb, "SW")) {
        cli_putln("Stack high-water marks (min free words since start):");
        char tmp[56];
        #define SWPR(h, name) \
            snprintf(tmp, sizeof(tmp), "  %-12s %4u w free", name, \
                (unsigned)((h) ? uxTaskGetStackHighWaterMark(h) : 0u)); \
            cli_putln(tmp);
        SWPR(xFreqRelayTask, "FreqRelay");
        SWPR(xControlTask,   "Control");
        SWPR(xGpsTask,       "GPS");
        SWPR(xCliTask,       "CLI");
        SWPR(xSensorTask,    "Sensor");
        SWPR(xDisplayTask,   "Display");
        SWPR(xUptimeTask,    "Uptime");
        snprintf(tmp, sizeof(tmp), "  Heap free:  %u B",
                 (unsigned)xPortGetFreeHeapSize());
        cli_putln(tmp);
        #undef SWPR
        return;
    }

    /* ---- Unknown ---- */
    cli_puts("Unknown command: "); cli_putln(verb);
    cli_putln("Type H for help");
}

/* -----------------------------------------------------------------------
 * vCliTask - reads serial line by line, dispatches commands
 * ----------------------------------------------------------------------- */
void vCliTask(void *pvParameters)
{
    (void)pvParameters;

    static char buf[64];
    int pos = 0;

    for (;;)
    {
        if (CLI_SERIAL.available() > 0) {
            char c = (char)CLI_SERIAL.read();
            if (c == '\n' || c == '\r') {
                if (pos > 0) {
                    buf[pos] = '\0';
                    dispatch(buf);
                    pos = 0;
                }
            } else {
                if (pos < (int)sizeof(buf) - 1)
                    buf[pos++] = c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
