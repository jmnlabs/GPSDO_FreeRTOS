/**
 * gpsdo_cli.cpp — vCliTask — Serial / Bluetooth command line interface
 *
 * Part of GPSDO FreeRTOS v0.51
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
    cli_putln(PROGRAM_NAME " " PROGRAM_VERSION " by " AUTHOR_NAME);
    cli_putln("Commands (case-sensitive, end with Enter):");
    cli_putln("  V           Version, authors and links");
    cli_putln("  H / ?       this help");
    cli_putln("  F           Flush frequency ring buffers");
    cli_putln("  C           start auto-Calibration (PWM centring)");
    cli_putln("  CT          Calibrate + auto-Tune PID for all algos");
    cli_putln("  T           GPS Tunnel mode (exits after 300s)");
    cli_putln("  SP <n>      Set PWM DAC directly (1-65535)");
    cli_putln("  up1 / up10  increase PWM by 1 / 10");
    cli_putln("  dp1 / dp10  decrease PWM by 1 / 10");
    cli_putln("  RH / RD     Human readable / Tab Delimited reporting");
    cli_putln("  RP / RR     Report Pause / Report Resume");
    cli_putln("  MH / MD     Mode Holdover / Mode Disciplined");
    cli_putln("  LA <0-9>    Loop Algorithm select");
    cli_putln("  LP [n]      List PID Parameters (algo n or current)");
    cli_putln("  KP n val    set Kp for algo n (3-7)");
    cli_putln("  KI n val    set Ki for algo n (3-7)");
    cli_putln("  KD n val    set Kd for algo n (3-7)");
    cli_putln("  IL n val    set I_LIMIT for algo n (3-9)");
    cli_putln("  BC [val]    algo 8 Blend Crossover (Hz)");
    cli_putln("  BS [val]    algo 8 Blend Scale (Hz)");
    cli_putln("  NS [val]    algo 9 NN max Step (LSB)");
    cli_putln("  AP          Arm picDIV");
    cli_putln("  ES          EEPROM Save");
    cli_putln("  ER          EEPROM Recall");
    cli_putln("  EE          EEPROM Erase");
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
        cli_putln("FreeRTOS port by J. M. Niewinski");
        cli_putln("https://github.com/jmnlabs/GPSDO_FreeRTOS");
        cli_putln("Programming assistant: Claude AI (Anthropic)");
        cli_putln("Based on GPSDO v0.06c by " AUTHOR_NAME);
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

    /* ---- tunnel mode ---- */
    if (cli_ieq(verb, "T")) {
        xEventGroupSetBits(xSysEvents, EVT_TUNNEL_MODE);
        cli_putln("Switching to GPS tunnel mode");
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
            if (v >= 0 && v <= 9) {
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = (uint8_t)v;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_puts("Algorithm: "); cli_putint(v);
            } else {
                cli_putln("LA: value must be 0..9");
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
