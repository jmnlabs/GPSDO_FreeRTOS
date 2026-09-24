/**
 * gpsdo_cli.cpp — vCliTask — Serial / Bluetooth command line interface
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 *
 * Lightweight command parser (no external library).  Reads lines from
 * Serial or Serial2 (Bluetooth), splits into verb + argument, dispatches.
 *
 * Commands cover: report mode (RH/RD/RP/RR), holdover (MH/MD),
 * algorithm selection (LA), PID tuning (KP/KI/KD/IL/BC/BS/NS),
 * EEPROM (ES/ER/EE), time offset (TO), and diagnostics (SW/H).
 */

#include "gpsdo_tz.h"
#include "gpsdo_config.h"
#include "gpsdo_build.h"
#include "gpsdo_dac.h"
#include "gpsdo_backlight.h"
#ifdef GPSDO_PWM_DITHER
  #include "gpsdo_pwm24.h"    /* PWM24_N / PWM24_TBL, for the DAC report */
#endif
#include "gpsdo_health.h"
#include "gpsdo_state.h"
#include <limits.h>
#include "gpsdo_algorithms.h"
#include "gpsdo_flash_ring.h"
#include "gpsdo_live_store.h"
#include "gpsdo_settings_store.h"
#include "gpsdo_ubx_timtp.h"
#include "gpsdo_span.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#if defined(GPSDO_BLUETOOTH_PARALLEL)
  #define CLI_SERIAL g_tee
#elif defined(GPSDO_BLUETOOTH)
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
/* ---- persistence hints -------------------------------------------------
 * Two classes of setting, and the CLI says which is which every time so the
 * operator never has to remember:
 *
 *  - PREFERENCES (timezone, sensor offsets, boot flags, survey-in) are things
 *    you set once and expect to stick, and none of them touch the control
 *    loop. These auto-save, and the message names the group that was written.
 *  - LOOP TUNING (PID, LTIC, LTIC-Lars, damping windows) stays manual: while
 *    you are experimenting, "reboot to revert" is a feature, not a nuisance.
 *    The message names the exact command that would save it.
 *
 * Note on SET_FLAGS: it carries WU/SPL/SV (preferences) together with SAW and
 * LRN (loop-affecting toggles). Auto-saving a preference therefore also commits
 * whatever SAW/LRN currently are, so the message lists the whole group rather
 * than pretending otherwise. */
/* Set when a handler rejected the argument, so the persistence hint can say
 * "out of range" instead of "not saved — run ES", which would imply there was
 * a new value worth keeping. Cleared for every command line. */
static bool s_cli_rejected = false;

static void cli_reject(const char *msg)
{
    s_cli_rejected = true;
    cli_putln(msg);
}

static void cli_autosaved(settings_partial_t grp, const char *members)
{
    if (settings_save_partial(grp)) {
        cli_puts("  [auto-saved: "); cli_puts(members); cli_putln("]");
    } else {
        cli_puts("  [AUTO-SAVE FAILED — run 'ES ' manually: "); cli_puts(members);
        cli_putln("]");
    }
}

static void cli_manual_save(const char *escmd)
{
    if (s_cli_rejected) {
        cli_putln("  [not saved — value out of range; accepted range shown above]");
        return;
    }
    cli_puts("  [not saved — run '"); cli_puts(escmd); cli_putln("' to keep it]");
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
/* Print a UTC offset as +h:mm / -h:mm. Always signed and always with the
 * minutes, so "+9:30" and "+9:00" line up and nobody has to wonder whether
 * a bare "+9" meant 9:00 or a truncated 9:30. */
static void cli_put_offset(int16_t mins)
{
    char buf[10];
    int a = mins < 0 ? -mins : mins;
    snprintf(buf, sizeof(buf), "%c%d:%02d", mins < 0 ? '-' : '+', a / 60, a % 60);
    cli_puts(buf);
}

/* Parse "9", "-5", "9:30", "-3:30", "+5:45" into signed minutes.
 * Rejects anything outside -12:00..+14:00 (the real range of civil zones —
 * Baker Island to Kiritimati) and any minutes field over 59. */
static bool cli_parse_offset(const char *s, int16_t *out)
{
    if (!s || !*s) return false;
    int sign = 1;
    if      (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    if (*s < '0' || *s > '9') return false;

    int h = 0;
    while (*s >= '0' && *s <= '9') h = h * 10 + (*s++ - '0');
    int m = 0;
    if (*s == ':') {
        s++;
        if (*s < '0' || *s > '9') return false;
        while (*s >= '0' && *s <= '9') m = m * 10 + (*s++ - '0');
    }
    if (*s != '\0') return false;          /* trailing junk */
    if (m > 59) return false;

    int total = sign * (h * 60 + m);
    if (total < -720 || total > 840) return false;
    *out = (int16_t)total;
    return true;
}

/* x.xxe-NN, built by hand.
 *
 * This file prints no floats through printf — Float printf has to be enabled in
 * the IDE and emits "?" when it is not — and dtostrf has no exponent form. A
 * FIXED exponent was fine while the DAC report quoted two hard-coded widths;
 * with three converters of 16, 18 and 24 bits the same line has to carry
 * anything from 2.4e-12 to 9.4e-15, and "2394.9e-15" is not a number anybody
 * reads. So normalise the mantissa and print the exponent that belongs to it. */
static void cli_frac_exp(char *out, size_t n, double frac)
{
    int e = 0;
    if (frac > 0.0) {
        while (frac <  1.0) { frac *= 10.0; e++; }
        while (frac >= 10.0) { frac /= 10.0; e--; }
    }
    char m[16];
    dtostrf(frac, -1, 2, m);
    snprintf(out, n, "%se-%d", m, e);
}

/* log2 of a power of two, or 0 when it is not one.
 *
 * Exists so the DAC report can keep saying "18-bit" where that is the truth
 * and fall back to a plain count where it is not. The plain-PWM path on a
 * build with no dither engine resolves 50 000 duty values; calling that
 * "16-bit" overstates it by a third, and calling it "15-bit" throws a third of
 * it away. Neither is worth the tidier-looking line. */
static uint8_t cli_exact_bits(uint32_t steps)
{
    uint8_t b = 0;
    if (steps == 0u || (steps & (steps - 1u)) != 0u) return 0u;
    while (steps > 1u) { steps >>= 1; b++; }
    return b;
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
extern int16_t g_time_offset_min;
extern bool    g_show_local_time;
extern bool    g_report_paused;
extern bool    g_svin_enabled;

/* EEPROM helpers declared in gpsdo_state.cpp */
extern void persist_save(void);
extern void persist_recall(void);
extern void persist_erase(void);

/* -----------------------------------------------------------------------
 * Help text
 * ----------------------------------------------------------------------- */
static void print_help(void)
{
    cli_puts(PROGRAM_NAME " "); cli_puts(g_fw_version); cli_putln(" by jmnlabs (see V)");
    cli_putln("  algo 11 after Lars Walenius; algo 12 after Alan Cashin (MIS42N)");
    cli_putln("Commands (case-insensitive, end with Enter):");
    cli_putln("  V           Version, authors and links");
    cli_putln("  TL 0|1      per-task CPU load on the telemetry line (SW shows it once)");
    cli_putln("  H / ?       this help  (H TZ for timezone details)");
    cli_putln("  F           Flush frequency ring buffers");
    cli_putln("  C           start auto-Calibration (PWM centring)");
    cli_putln("  CT          Calibrate + auto-Tune PID for all algos");
    cli_putln("  T [baud]    GPS tunnel on USB (300s; opt. GPS UART baud for u-center)");
    cli_putln("  SP <n>      Set PWM DAC directly (1-65535)");
    cli_putln("  DAC [PWM|DITH|EXT]  output path (no arg = report + Vctl check)");
    cli_putln("  DV [v]      volts at full code for 'commanded' (3.30 = PWM, 5.00 = AD5680)");
    cli_putln("  AV [x]      ADC divide ratio for 'measured' (1.00 = direct)");
    cli_putln("  VS [VCC|VREF]  what the PA0 divider is jumpered to (auto-saved)");
    cli_putln("  SPAN [CLR FULL|REDUCED]  EFC span jumper (PB14): K per position");
#if defined(GPSDO_TFT_BL_PWM)
    cli_putln("  BL [%]      TFT backlight 30-100% (auto-saved; 100 = no switching)");
#endif
    cli_putln("  up1 / up10  increase PWM by 1 / 10");
    cli_putln("  dp1 / dp10  decrease PWM by 1 / 10");
    cli_putln("  RH / RD     Human readable / Tab Delimited reporting");
    cli_putln("  RP / RR     Report Pause / Report Resume");
    cli_putln("  MH / MD     Mode Holdover / Mode Disciplined");
    cli_putln("  LA <0-13>   Loop Algorithm select");
    cli_putln("              10=LTIC 3-stage, 11=LTIC-Lars, 12=multi-level accum");
    cli_putln("  LP [n]      List PID Parameters (algo n or current)");
    cli_putln("  KP n val    set Kp for algo n (3-7)");
    cli_putln("  KI n val    set Ki for algo n (3-7)");
    cli_putln("  KD n val    set Kd for algo n (3-7)");
    cli_putln("  IL n val    set I_LIMIT for algo n (3-9)");
    cli_putln("  BC [val]    algo 8 Blend Crossover (Hz)");
    cli_putln("  BS [val]    algo 8 Blend Scale (Hz)");
    cli_putln("  NS [val]    algo 9 NN max Step (LSB)");
    cli_putln("  -- LTIC (algo 10) phase-discipline params --");
    cli_putln("  LC          LTIC self-Calibrate (ns/V, offset, range)");
    cli_putln("  LL          List all LTIC params + state");
    cli_putln("  LNV/LZO/LRN cal: ns/V, zero-offset V, range ns");
    cli_putln("  AQP/I/D/L   ACQ PID Kp/Ki/Kd/I_LIMIT");
    cli_putln("  DPP/I/D/L   DPLL PID Kp/Ki/Kd/I_LIMIT");
    cli_putln("  LKP/I/D/L   LOCK PID Kp/Ki/Kd/I_LIMIT");
    cli_putln("  LAT/LDT/LIV ACQ thr, DPLL->LOCK thr, LOCK interval s");
    cli_putln("  LPOL [-1/0/1] PWM->phase polarity (0=not set, loop holds)");
    cli_putln("  FA/FAD/FAL n  LTIC damping avg window (10/100/1000; both/DPLL/LOCK)");
    cli_putln("  -- LTIC-Lars (algo 11) continuous-PI params --");
    cli_putln("  LG [val]    Gain: 0=auto from CT calibration, else manual");
    cli_putln("  LD [val]    Damping");
    cli_putln("  LTC [s]     Time Constant (loop, 1-600 s)");
    cli_putln("  LFD [n]     Filter Divisor (pre-filter = LTC/this)");
    cli_putln("  LTO [adc]   TIC Offset (phase target, ADC counts)");
    cli_putln("  LPL [ns]    lock Phase Limit (window)");
    cli_putln("  LPF [n]     lock Factor (hold = LPF*LTC seconds)");
    cli_putln("  LTK [val]   Temp coefficient (feed-forward; 0=off)");
    cli_putln("  LTR [adc]   Temp Reference (ADC counts)");
    cli_putln("     (credit: Lars Walenius' original PI GPSDO loop)");
    cli_putln("  WU 0|1        - OCXO warmup on boot (saved with ES)");
    cli_putln("  SPL 0|1       - boot animation: 1=full show, 0=static (saved with ES)");
    cli_putln("  LRN 0|1|R     - self-learning drift/damping (R=reset, ES saves)");
    cli_putln("  LCV [V]     ACQ centring target (0=range mid)");
    cli_putln("  AP          Arm picDIV");
    cli_putln("  ES [obj]    Save settings to flash ring (obj: TZ/PID/LTIC/FLAGS/ALGO12/ALGO/PO)");
    cli_putln("  ER          Recall settings (flash ring)");
    cli_putln("  EE          Erase settings (reset to defaults)");
    cli_putln("-- Algo 12 (multi-level accumulator) --");
    cli_putln("  MG [v]      Gain LSB/ns (0 = auto from CT)");
    cli_putln("  MR [n]      Force a correction at level n (0-10)");
    cli_putln("  MLP <n> [ns] Phase limit for level n");
    cli_putln("  MF [0-3]    Limits: 0=default(formula) 1=stored 2=formula 3=measured");
    cli_putln("  KR/KQ/KT    Algo 13 Kalman: noise R [ns], Q, ESTIMATOR horizon [s] (0=measure)");
    cli_putln("  KC          Algo 13: CONTROLLER horizon [s] - phase nulling (0 = KT/3)");
    cli_putln("  KL          List the Kalman state and what it has measured");
    cli_putln("  MFT [s]     MF 3: target s between noise-driven corrections");
    cli_putln("  ML          List all algo-12 parameters");
    cli_putln("  CS           Correction statistics over 100/1k/10k/100k corrections");
    cli_putln("  EW          Flash wear stats (ring buffer erase cycles)");
    cli_putln("  FR          Flash ring status (always on; wear: EW)");
    cli_putln("  SAW 0|1     Sawtooth qErr correction on/off (algo 10; saved with ES)");
    cli_putln("  ACG g [cap] ACQ centring drive: LSB/V and max step (algo 10)");
    cli_putln("  RB          Reboot (warm, keep settings)");
    cli_putln("  CR YES      Cold Restart (wipe settings, factory defaults)");
    cli_putln("  PO <f>      Pressure Offset [Pa, added to BMP280 raw]");
    cli_putln("  AO <f>      Altitude Offset [m, added to GPS altitude]");
    cli_putln("  TO <n|A>    Fixed UTC offset (h or h:mm) or Auto (EU only)");
    cli_putln("  TZ <zone>   Timezone with DST, e.g. TZ Adelaide  (H TZ)");
#ifdef GPSDO_GPS_TIMING
    cli_putln("  SV <0|1>    Survey-in / Time Mode on timing rx (saved by ES)");
#endif
    cli_putln("  SW          Stack watermarks, heap, uptime source, MCU ppm");
}

/* TZ takes two quite different arguments and the difference matters, so it
 * gets its own page rather than a cramped line in the main list. */
static void print_help_tz(void)
{
    cli_putln("TZ — local timezone, with DST");
    cli_putln("");
    cli_putln("  TZ <city>        e.g. TZ Adelaide");
    cli_putln("                   City names are unique worldwide, so the");
    cli_putln("                   region is optional: TZ Australia/Adelaide");
    cli_putln("                   works too. Case doesn't matter.");
    cli_putln("  TZ <posix-rule>  e.g. TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3");
    cli_putln("                   The raw form, for a zone this firmware");
    cli_putln("                   doesn't know or whose rule has changed.");
    cli_putln("  TZ               show the current rule and offset");
    cli_putln("");
    cli_putln("The rule format is std<off>[dst[<off>],start,end], where a");
    cli_putln("transition is Mmonth.week.day — week 5 means last. Note the");
    cli_putln("POSIX sign is inverted: -9:30 means UTC+9:30.");
    cli_putln("");
    cli_putln("  ACST-9:30ACDT,M10.1.0,M4.1.0/3");
    cli_putln("       |     |    |        |");
    cli_putln("       |     |    |        +-- ends 1st Sun of Apr, 03:00");
    cli_putln("       |     |    +----------- starts 1st Sun of Oct");
    cli_putln("       |     +---------------- summer zone name");
    cli_putln("       +---------------------- UTC+9:30 standard");
    cli_putln("");
    cli_putln("Southern-hemisphere zones need nothing special: a start month");
    cli_putln("after the end month simply means DST wraps through New Year.");
    cli_putln("");
    cli_putln("Related:");
    cli_putln("  TO <n[:mm]>      fixed offset, no DST (TO 9:30, TO -5)");
    cli_putln("  TO A             guess the zone from GPS position and apply");
    cli_putln("                   the EU DST rule. Europe only — elsewhere it");
    cli_putln("                   gives whole hours and no DST.");
    cli_putln("  LT 0|1           show UTC or local time");
    cli_putln("  ES [obj]         save settings to flash ring (obj: TZ/PID/LTIC/FLAGS/ALGO12/ALGO/PO)");
}

/* -----------------------------------------------------------------------
 * Command dispatcher
 * ----------------------------------------------------------------------- */
/* ADC counts <-> volts, the ONE place the detector's scale is written down on
 * the CLI side. The firmware's own conversion is gpsdo_tasks.cpp:
 *   g_ltic_voltage = (accepted / 4096.0f) * 3.3f
 * and these must agree with it. 12-bit ADC, 3.3 V reference, 4096 not 4095 —
 * counts are bucket indices, not fenceposts.
 *
 * Why they exist: two commands used to take ADC counts while everything else
 * on this detector spoke volts and nanoseconds, so LZO = 2.0809 V and
 * LTO = 2620 counts named the SAME physical point in two units — and did not
 * agree (2620 counts is 2.1104 V, 37 counts away, about 37 ns once the scale
 * was corrected). Nobody spots that by eye, which is the whole argument for
 * one unit at the human face. TODO item 21. */
#define ADC_FULL_SCALE_V   3.3
#define ADC_STEPS          4096.0
static double adc_to_v(uint16_t counts) { return ((double)counts / ADC_STEPS) * ADC_FULL_SCALE_V; }
static uint16_t v_to_adc(double volts)
{
    double c = (volts / ADC_FULL_SCALE_V) * ADC_STEPS + 0.5;
    if (c < 0.0) c = 0.0;
    if (c > 4095.0) c = 4095.0;
    return (uint16_t)c;
}

#ifdef GPSDO_LTIC
/* Warn before an algorithm is handed the phase detector.
 *
 * Two different states, and only one of them used to be reported. LC writes
 * three numbers: ns_per_volt, zero_offset and range_ns. If ALL of them are
 * still at their build defaults, LC has never run on this board — and the
 * likeliest reason is not that the owner forgot, but that there is no detector
 * to calibrate. That is exactly the case Dave hit: a counter-only board built
 * with GPSDO_LTIC on, PA1 floating, ADC noise arriving as a phase in
 * nanoseconds and the loop faithfully disciplining the OCXO against it.
 * A merely uncalibrated detector (range known, slope not) is a much milder
 * complaint and keeps the old wording. */
static void cli_warn_ltic_cal(void)
{
    if (g_ltic.ns_per_volt == 0.0f && g_ltic.range_ns == 0.0f) {
        cli_putln("WARNING: no detector calibrated, phase may be floating — is the");
        cli_putln("         hardware really there?  LC has never run: TIC slope and");
        cli_putln("         detector range are both still at build defaults. Without");
        cli_putln("         the ramp detector fitted, PA1 reads ADC noise and this");
        cli_putln("         algorithm will steer the OCXO from it. Run CT, then LC.");
    } else if (g_ltic.ns_per_volt == 0.0f) {
        cli_putln("WARNING: LTIC uncalibrated — run LC first for ns-accurate phase.");
    }
}
#endif

static void dispatch(char *line)
{
    /* Strip trailing whitespace / CR */
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' ||
                        line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0) return;

    s_cli_rejected = false;      /* per-command; see cli_reject() */

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
        /* Name, version AND the compile stamp on one line, in the same shape the
         * boot banner uses. The tuner asks for V on connect and the banner has
         * usually scrolled past by then, so this is where it can actually see
         * which binary it is talking to. Empty stamp (a build where the sketch
         * did not fill it) simply prints the name and version as before. */
        if (g_fw_stamp[0]) {
            char b[80];
            snprintf(b, sizeof(b), "%s %s %s",
                     PROGRAM_NAME, g_fw_version, g_fw_stamp);
            cli_putln(b);
        } else {
            cli_puts(PROGRAM_NAME " ");
            cli_putln(g_fw_version);
        }
        /* The identity that cannot be stale — see gpsdo_build.h. Printed here
         * as well as in the banner because the banner scrolls away and this is
         * the first thing to ask when a log and a memory disagree. */
        {
            uint32_t n = fw_image_bytes();
            char b[64];
            if (n) snprintf(b, sizeof(b), "image CRC32 %08lX  (%lu bytes)",
                            (unsigned long)fw_image_crc32(), (unsigned long)n);
            else   snprintf(b, sizeof(b), "image CRC32 unavailable");
            cli_putln(b);
        }
        cli_putln("FreeRTOS port & algorithms: J. M. Niewinski (jmnlabs)");
        cli_putln("https://github.com/jmnlabs/GPSDO_FreeRTOS");
        cli_putln("Programming assistants: Claude Opus 5, GLM-5.3 Max, Qwen3.8-Max");
        cli_putln("Inspired by v0.06c by Andre Balsa");
        cli_putln("https://github.com/AndrewBCN/STM32-GPSDO");
        cli_putln("");
        cli_putln("Algo 11 continuous-PI loop:  the late Lars Walenius");
        cli_putln("Algo 12, zero-cross, dither: Alan Cashin, MIS42N (EEVBlog)");
        cli_putln("  and the CS self-assessment idea");
        cli_putln("Algo 13 Kalman filter:       J. M. Niewinski - original here");
        cli_putln("Measurements, algos 10 & 11: Dan Wiering (Rb reference)");
        cli_putln("ILI9486/9488 support urged:  lucido (EEVBlog)");
        cli_putln("PCB design (prototype):      Scrachi (EEVBlog)");
        return;
    }

    /* ---- DAC — what the control voltage is and how finely it can move ---- */
    if (cli_ieq(verb, "DAC")) {
        /* Every line is assembled whole and emitted with one cli_putln().
         *
         * The first version built lines from cli_puts() + cli_putfloat()
         * fragments, which came out broken on the wire: cli_putfloat() always
         * terminates its line, so each number landed on one of its own and the
         * units ended up on the next. It is a line printer, not an inline
         * formatter. Assembling here also means the serial mutex is taken once
         * per line, so another task's output cannot interleave mid-sentence. */
        char l[96], b1[16], b2[16];
        uint32_t c24 = gpsdo_dac_last24();
        double   f16 = gpsdo_dac_last16f();
        uint16_t v16 = gpsdo_dac_last16();

        /* With an argument this SELECTS the path. The signal itself is switched
         * by jumpers on the board — the firmware only needs to know which one
         * it is steering, so that the step size, the telemetry and the fine
         * path describe what is actually connected. Getting the two out of
         * step is exactly what the Vctl check at the end of this block is for. */
        if (arg != NULL) {
            uint8_t want;
            if      (cli_ieq(arg, "PWM"))  want = DAC_PATH_PWM;
            else if (cli_ieq(arg, "DITH")) want = DAC_PATH_DITH;
            else if (cli_ieq(arg, "EXT"))  want = DAC_PATH_EXT;
            else { cli_reject("DAC: PWM | DITH | EXT  (no argument = report)"); return; }
            if (!gpsdo_dac_path_available(want)) {
                snprintf(l, sizeof(l), "DAC: %s is not compiled into this build",
                         gpsdo_dac_path_name(want));
                cli_reject(l);
                return;
            }
            g_dac_path = want;
            /* Re-issue the code the loop last asked for, so the newly selected
             * path is driving the same voltage the old one was rather than
             * whatever it happened to be holding. */
            gpsdo_dac_write24(gpsdo_dac_last24());
            snprintf(l, sizeof(l), "DAC path = %s", gpsdo_dac_path_name(g_dac_path));
            cli_putln(l);
            cli_putln("  MOVE THE JUMPER TO MATCH. The firmware drives the path you");
            cli_putln("  named; the board decides which signal reaches the filter.");
            cli_manual_save("ES ALGO");
            /* fall through to the report, so the answer includes the check */
        }

        cli_putln("-- Control voltage output --");

        {
            char av[40]; int ap = 0;
            for (uint8_t i = 0; i <= DAC_PATH_EXT; i++)
                if (gpsdo_dac_path_available(i))
                    ap += snprintf(av + ap, sizeof(av) - ap, "%s%s",
                                   ap ? " " : "", gpsdo_dac_path_name(i));
            snprintf(l, sizeof(l), "  path: %s   (compiled in: %s)",
                     gpsdo_dac_path_name(g_dac_path), av);
            cli_putln(l);
        }
        if (g_dac_path == DAC_PATH_EXT) {
            cli_putln("        external SPI DAC (AD5680, bit-banged)");
        }
#if defined(GPSDO_PWM_DITHER)
        else if (g_dac_path == DAC_PATH_DITH) {
        snprintf(l, sizeof(l), "        %u-bit PWM + dither -> 24 bit, PB9/TIM4 CH4, DMA",
                 (unsigned)PWM24_N);
        cli_putln(l);
        /* TIM4 runs from the 100 MHz APB1 timer clock with no prescaler, so the
         * carrier is that divided by the period. */
        snprintf(l, sizeof(l), "        carrier %lu Hz, table %lu x2 = %lu bytes RAM",
                 (unsigned long)(100000000UL / (1UL << PWM24_N)),
                 (unsigned long)PWM24_TBL,
                 (unsigned long)(PWM24_TBL * 2UL * 2UL));
        cli_putln(l);
        }
        else {
            cli_putln("        dither engine at whole-LSB granularity — same pin,");
            cli_putln("        same voltage as plain PWM, no sub-LSB resolution");
        }
#else
        else {
            cli_putln("        plain 16-bit PWM (analogWrite) — no dither compiled in");
        }
#endif

        /* The scales the two numbers below are computed with, stated up front
         * because they are the first thing to check when commanded and
         * measured disagree: DV says what full code means at the output
         * (3.30 V = the PWM model, the historical default), AV says what
         * divide ratio sits before the ADC pin (1.00 = none). */
        dtostrf(g_dac_vref, -1, 3, b1);   /* three: a 4.096 V reference */
        dtostrf(g_adc_vdiv, -1, 2, b2);
        snprintf(l, sizeof(l), "  scales: commanded at %s V full-code, ADC x%s", b1, b2);
        cli_putln(l);

        /* Three views of one number. Printing all three is the point: a fraction
         * that never appears anywhere is a fraction nobody can trust. A 24-bit
         * code that is not a multiple of 256 is the proof the fine path is the
         * one driving the pin. */
        snprintf(l, sizeof(l), "  code: 24-bit %lu%s", (unsigned long)c24,
                 (c24 & 0xFFu) ? "  (fractional - fine path is driving)" : "");
        cli_putln(l);
        snprintf(l, sizeof(l), "        16-bit %u  (displays, flash ring)", (unsigned)v16);
        cli_putln(l);
        dtostrf(f16, -1, 4, b1);
        dtostrf(f16 - (double)v16, -1, 4, b2);
        snprintf(l, sizeof(l), "        exact  %s  (%s from the 16-bit view)", b1, b2);
        cli_putln(l);

        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            /* g_adc_vdiv: the divider before the pin (AV), 1.0 = none. */
            double vm = ((double)gCtrl.avg_vctl_adc / 4096.0) * 3.3 * g_adc_vdiv;
            xSemaphoreGive(xCtrlMutex);
            dtostrf(vm, -1, 4, b1);
            /* COMMANDED AGAINST MEASURED — the one check that catches a jumper
             * that does not match the setting. The firmware cannot see the
             * jumper, so the only evidence is that the control voltage is not
             * where it was told to put it.
             *
             * Half a volt of slack, deliberately: the ADC divider and the
             * reference are good to a few percent at best, so a tight bound
             * would cry wolf on every board. What this has to catch is the
             * gross case — commanded 1.8 V, measured 0.0 V because the driver
             * that is selected is not the one wired to the filter — and half a
             * volt catches that without ever firing on a scale error.
             *
             * The commanded side is scaled by g_dac_vref (DV), not the
             * hardcoded 3.3 V it used to be: on a 5 V external DAC the old
             * model under-stated commanded by 1.5x and the check cried wolf
             * on every reading — the false MISMATCH that started the whole
             * DV/AV business. */
            double vc = ((double)v16 / 65536.0) * g_dac_vref;
            dtostrf(vc, -1, 4, b2);
            snprintf(l, sizeof(l), "  Vctl: %s V measured, %s V commanded", b1, b2);
            cli_putln(l);
            if (vm < vc - 0.5 || vm > vc + 0.5) {
                cli_putln("  ** MISMATCH — the control voltage is not following the");
                cli_putln("     commanded code. Check the jumper against the DAC");
                cli_putln("     setting above, and that the selected driver is the");
                cli_putln("     one wired to the filter.");
            }
        }

        /* WHAT THE OTHER DIVIDER IS LOOKING AT, and whether it agrees.
         *
         * On the 5 V rail this is the reading the telemetry line has always
         * carried. On the reference it is the only check the firmware has that
         * the part fitted is the part DV describes — and the failure it catches
         * is not subtle: a 5.000 V device where DV says 4.096 makes every
         * frequency figure the board prints a fifth too small, silently. */
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            int16_t raw = gCtrl.avg_vcc_adc;
            xSemaphoreGive(xCtrlMutex);
            if (raw > 0) {
                /* The divider halves it; the ADC's own reference is the
                 * nominal 3.3 V, so this is an indication, not a measurement.
                 * Good to a percent or two, which is all the check needs. */
                double v = ((double)raw / 4096.0) * 3.3 * 2.0;
                dtostrf(v, -1, 3, b1);
                if (g_vsense_src == VSENSE_VREF) {
                    dtostrf(g_dac_vref, -1, 3, b2);
                    snprintf(l, sizeof(l),
                             "  Vref: %s V measured, %s V per DV", b1, b2);
                    cli_putln(l);
                    /* Five percent: wide enough that the ADC reference and the
                     * divider's own tolerance never trip it, narrow enough to
                     * catch the wrong part, which is always out by 20 % or
                     * more, and a reference that has stopped working. */
                    if (v < g_dac_vref * 0.95 || v > g_dac_vref * 1.05) {
                        cli_putln("  ** the reference is not where DV says it is.");
                        cli_putln("     Check VS against the jumper, DV against the");
                        cli_putln("     part actually fitted, and the reference's");
                        cli_putln("     own supply — it needs headroom above its");
                        cli_putln("     output to regulate at all.");
                    }
                } else {
                    snprintf(l, sizeof(l), "  Vcc:  %s V (5 V rail; VS VREF if the"
                             " jumper feeds the reference)", b1);
                    cli_putln(l);
                }
            }
        }

        /* The Hz figures need the plant gain, which only CT can supply. Without
         * it the resolution is still real but its meaning in frequency is
         * unknown, and saying so beats printing a number from a default. */
        double lsb_per_hz = (g_pid[7].Kp > 100.0) ? ((double)g_pid[7].Kp / 0.40) : 0.0;
        if (lsb_per_hz > 0.0) {
            /* TWO STEPS, AND THEY ARE NOT THE SAME NUMBER.
             *
             * The control value is 24-bit on every path — that is what the loop
             * commands and what the fine carry delivers on average. What the
             * OUTPUT moves in a single write is the live driver's own width:
             * 2^24 on DITH (the dither table averages the 24-bit value
             * exactly), 2^18 on the AD5680, 2^16 on plain PWM riding the
             * dither engine — and 50 000, which is not a power of two at all,
             * on a build with no dither engine, where the timer's period is
             * the resolution. See gpsdo_dac_output_steps().
             *
             * This used to print 16-bit and 24-bit regardless of the path, so
             * on Dan Wiering's AD5680 board the report offered 0.094 uHz as a
             * step — 64 counts of the control value make one move at that pin,
             * so the smallest real one is about 6 uHz — while at the same time
             * understating the part by quoting a 16-bit figure four times
             * coarser than it can do. Both lines were wrong on the same board
             * in opposite directions, which is what a fixed pair of widths gets
             * you when there are three converters.
             *
             * uHz on both lines, so they are comparable at a glance. */
            uint32_t osteps = gpsdo_dac_output_steps();
            uint8_t  obits  = cli_exact_bits(osteps);   /* 0 = not a power of 2 */
            char lbl[16];
            if (obits) snprintf(lbl, sizeof(lbl), "%u-bit", (unsigned)obits);
            else       snprintf(lbl, sizeof(lbl), "%lu steps", (unsigned long)osteps);
            double hz_ctl = (1.0 / lsb_per_hz) / 256.0;              /* 24-bit  */
            double hz_out = (1.0 / lsb_per_hz) * 65536.0
                          / (double)osteps;
            dtostrf(hz_ctl * 1e6, -1, 3, b1);
            cli_frac_exp(b2, sizeof(b2), hz_ctl / 1.0e7);
            snprintf(l, sizeof(l), "  step: control 24-bit 1 LSB = %s uHz = %s",
                     b1, b2);
            cli_putln(l);
            dtostrf(hz_out * 1e6, -1, 3, b1);
            cli_frac_exp(b2, sizeof(b2), hz_out / 1.0e7);
            snprintf(l, sizeof(l), "        output %-9s 1 LSB = %s uHz = %s",
                     lbl, b1, b2);
            cli_putln(l);
            if (osteps < (1UL << 24)) {
                snprintf(l, sizeof(l),
                         "        (%s resolves %s; finer requests reach the pin"
                         " as a", gpsdo_dac_path_name(g_dac_path), lbl);
                cli_putln(l);
                cli_putln("         time average, not as one step)");
            }
            dtostrf(lsb_per_hz, -1, 0, b1);
            dtostrf((double)g_pid[7].Kp, -1, 0, b2);
            snprintf(l, sizeof(l), "        plant %s LSB/Hz (from CT, Kp[7]=%s)", b1, b2);
            cli_putln(l);
        } else {
            cli_putln("  step: run CT first - plant gain unknown, so the");
            cli_putln("        resolution cannot be stated in Hz");
        }

        /* Which span the plant above belongs to. Two jumper positions, two
         * plants, and the figures above are only as right as this line. */
        span_report(cli_putln);

        if (gpsdo_dac_fine_available()) {
            cli_putln("  fine: ACTIVE - sub-LSB corrections are applied, not");
            cli_putln("        truncated. Coarse writes (CT, LC, SP, holdover,");
            cli_putln("        ramps) clear the fraction by design.");
        } else {
            cli_putln("  fine: inactive - the output resolves 16 bits, so the");
            cli_putln("        fraction is carried but rounded away on the pin.");
        }
        return;
    }

    /* ---- DV / AV: the two scales the DAC report computes with ----
     *
     * DV states what full code means at the output the firmware is steering:
     * the PWM paths run from the 3.3 V rail (the default), an external DAC
     * from its own reference — 5.00 for the AD5680 boards. AV states the
     * divide ratio between the measured node and the ADC pin, 1.00 = direct.
     * Together they turn the DAC report's commanded-against-measured check
     * from a PWM-board assumption into a statement about THIS board. Both
     * save immediately with the ALGO block — the same partial the DAC path
     * rides in — so nobody has to remember an ES afterwards. */
    if (cli_ieq(verb, "DV") || cli_ieq(verb, "AV")) {
        char l[80], b1[16];
        bool isDV = cli_ieq(verb, "DV");
        if (arg == NULL) {
            if (isDV) {
                dtostrf(g_dac_vref, -1, 3, b1);
                snprintf(l, sizeof(l), "DV: %s V at full code (3.30 = PWM model)", b1);
            } else {
                dtostrf(g_adc_vdiv, -1, 2, b1);
                snprintf(l, sizeof(l), "AV: ADC divide ratio x%s (1.00 = direct)", b1);
            }
            cli_putln(l);
            return;
        }
        double v = atof(arg);
        if (isDV) {
            if (v < 2.50 || v > 5.50) {
                cli_reject("DV: 2.50..5.50 V (3.30 = the PWM model)");
                return;
            }
            g_dac_vref = v;
            /* Three decimals, as stored since build 57 (dac_vref_mv): with two,
             * DV 4.096 echoed back as 4.10 and was saved as 4.10 too. */
            dtostrf(v, -1, 3, b1);
            snprintf(l, sizeof(l), "DV: commanded now scaled at %s V full code", b1);
        } else {
            if (v < 1.00 || v > 10.00) {
                cli_reject("AV: 1.00..10.00 (divide ratio at the ADC pin)");
                return;
            }
            g_adc_vdiv = v;
            dtostrf(v, -1, 2, b1);
            snprintf(l, sizeof(l), "AV: Vctl readings now scaled x%s", b1);
        }
        cli_putln(l);
        /* AUTO-SAVED, not "run ES ALGO". These two describe the board's output
         * stage — they are typed once when the hardware is built and never
         * again — and the v1.07 changelog and the notes sent to the people
         * building those boards both said so. The code did not, so a DV set
         * and not followed by an ES came back as 3.30 after a reset and the
         * MISMATCH warning returned with it, which looks like a hardware fault
         * rather than a lost setting. The promise was the right one; this is
         * the code catching up to it. */
        cli_autosaved(SET_ALGO, isDV ? "DV: DAC full-code volts"
                                     : "AV: ADC divide ratio");
        return;
    }

    /* ---- VS: what the PA0 divider is measuring ----
     *
     * A jumper on the top of that divider chooses between the 5 V rail and the
     * voltage reference. The firmware cannot see it, so it is told — the same
     * arrangement as DV, AV and the DAC path, and for the same reason: the one
     * piece of evidence a jumper leaves behind is a voltage that is not where
     * the firmware expected it, and that only helps if the firmware was told
     * what to expect. */
    if (cli_ieq(verb, "VS")) {
        char l[80];
        if (arg == NULL) {
            snprintf(l, sizeof(l), "VS: PA0 divider reads %s (VCC | VREF)",
                     (g_vsense_src == VSENSE_VREF) ? "VREF" : "VCC");
            cli_putln(l);
            return;
        }
        if      (cli_ieq(arg, "VCC"))  g_vsense_src = VSENSE_VCC;
        else if (cli_ieq(arg, "VREF")) g_vsense_src = VSENSE_VREF;
        else { cli_reject("VS: VCC or VREF (what the jumper feeds the PA0 divider)");
               return; }
        snprintf(l, sizeof(l), "VS: PA0 divider now read as %s",
                 (g_vsense_src == VSENSE_VREF) ? "VREF (checked against DV)"
                                               : "VCC (the 5 V rail)");
        cli_putln(l);
        cli_autosaved(SET_ALGO, "ALGO: path/DV/AV/VS");
        return;
    }

    /* ---- SPAN: the EFC span jumper on PB14 ----
     *
     * Unlike VS, nothing to TELL the firmware here: the jumper's second pole
     * is wired to PB14 and the firmware reads it. What this shows is the other
     * half — the calibration each position carries — and CLR is the one
     * correction a person may need: forgetting a position that was calibrated
     * in the wrong place. The usual case is a board calibrated before PB14 was
     * wired, whose only CT was filed under FULL while the jumper actually sat
     * on REDUCED. CT in the other position catches that by itself (see
     * SPAN_SAME_PLANT in gpsdo_span.cpp); CLR is for when you already know. */
    if (cli_ieq(verb, "SPAN")) {
        if (arg == NULL) { span_report(cli_putln); return; }
        char *p2 = arg;
        while (*p2 && *p2 != ' ') p2++;
        while (*p2 == ' ') *p2++ = '\0';
        if (!cli_ieq(arg, "CLR") || *p2 == '\0') {
            cli_reject("SPAN: no argument = report, or SPAN CLR FULL|REDUCED");
            return;
        }
        uint8_t which;
        if      (cli_ieq(p2, "FULL")    || cli_ieq(p2, "0")) which = SPAN_FULL;
        else if (cli_ieq(p2, "REDUCED") || cli_ieq(p2, "1")) which = SPAN_REDUCED;
        else { cli_reject("SPAN CLR: FULL or REDUCED"); return; }
        span_clear(which);
        cli_puts("SPAN: calibration for "); cli_puts(span_name(which));
        cli_putln(" forgotten (and the code pair). Saved.");
        if (which == span_active())
            cli_putln("SPAN: this is the position the jumper is in - the live loop is unchanged; run CT to re-measure it.");
        return;
    }

#if defined(GPSDO_TFT_BL_PWM)
    /* ---- BL: TFT backlight brightness ----
     *
     * Percent, not raw duty: the operator is setting how bright the screen is,
     * not what the timer does, and the inversion the P-channel stage needs
     * belongs in the driver rather than in the number a person types.
     *
     * The floor is 30 for a reason that is not electrical — see
     * gpsdo_backlight.h. Below about a third the panel stops being readable
     * instead of becoming usefully dim, so a mistyped 3 would leave someone
     * looking at what could equally be a dead board. */
    if (cli_ieq(verb, "BL")) {
        char l[80];
        if (arg == NULL) {
            snprintf(l, sizeof(l), "BL: backlight %u%% (%u..%u, %u = default)",
                     (unsigned)g_tft_bl_pct, (unsigned)BL_PCT_MIN,
                     (unsigned)BL_PCT_MAX, (unsigned)BL_PCT_DEF);
            cli_putln(l);
            return;
        }
        long v = atol(arg);
        if (v < (long)BL_PCT_MIN || v > (long)BL_PCT_MAX) {
            snprintf(l, sizeof(l), "BL: %u..%u %% (below %u the panel is not "
                     "readable, only dark)", (unsigned)BL_PCT_MIN,
                     (unsigned)BL_PCT_MAX, (unsigned)BL_PCT_MIN);
            cli_reject(l);
            return;
        }
        backlight_set((uint8_t)v);
        snprintf(l, sizeof(l), "BL: backlight %u%%%s", (unsigned)g_tft_bl_pct,
                 (g_tft_bl_pct == BL_PCT_MAX)
                   ? " - full, and the stage stops switching" : "");
        cli_putln(l);
        cli_autosaved(SET_FLAGS, "FLAGS: WU/SPL/SAW/LRN/SV/BL");
        return;
    }
#endif /* GPSDO_TFT_BL_PWM */

    /* ---- help; "H TZ" for the one command that needs more than a line ---- */
    if (cli_ieq(verb, "H") || cli_ieq(verb, "?")) {
        if (arg && cli_ieq(arg, "TZ")) print_help_tz();
        else                           print_help();
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
        cli_putln("Derives K, tunes PID (3-9) and LTIC (10 & 11); auto-saves the PID group.");
        return;
    }

#ifdef GPSDO_LTIC
    /* ---- LC: LTIC self-calibration (ns_per_volt, zero_offset, range_ns) ---- */
    if (cli_ieq(verb, "LC")) {
        /* LC steers the PWM to hit a target phase-sweep rate, and to know how far
         * to steer it needs K — the Hz-per-LSB slope that CT measures. Without it
         * the loop falls back to a generic 3000 LSB/Hz, which on a real board is
         * wrong by whatever factor its OCXO differs by. The result is not
         * obviously broken, just quietly off: one board reported ns_per_volt
         * 1592.8 before CT and 921.2 after, a factor of 1.7, with nothing to
         * suggest the first was suspect. Warn rather than refuse — a re-run of LC
         * after CT is cheap, and there are legitimate reasons to sweep first. */
        if (g_pid[7].Kp <= 100.0) {
            cli_putln("LC: WARNING — no CT calibration yet, so the sweep will use a");
            cli_putln("    generic VCO slope and the result may be off by a large factor.");
            cli_putln("    Run 'CT' first, then 'LC'. Continuing anyway.");
        }
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
        /* THE OLD NOTE NAMED LA 4/5/7, and it predates algorithms 10-13 by a
         * long way. Those three steer on the COUNTER and have no phase detector
         * at all, so they cannot hold the divider's phase where it is put -
         * which is the one thing this note is about. The loops that can are the
         * LTIC family, and they also re-arm by themselves when the detector
         * says the divider has lost sync, so on 10-13 this command is a manual
         * override rather than routine maintenance. Dan Wiering picked 7 for an
         * overnight run and wondered why he was seeing arm events; algorithms
         * 0-9 never arm at all, and this line is the likeliest reason he was
         * there in the first place. */
        cli_putln("Note: LA 10-13 hold the divider's phase and re-arm by");
        cli_putln("themselves; on LA 0-9 nothing keeps it aligned afterwards.");
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
            gpsdo_dac_write16(gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM+1: "); cli_putint(gCtrl.pwm_output);
        return;
    }
    if (cli_ieq(verb, "up10")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output <= 65525) gCtrl.pwm_output += 10;
            gpsdo_dac_write16(gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM+10: "); cli_putint(gCtrl.pwm_output);
        return;
    }
    if (cli_ieq(verb, "dp1")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output > 1) gCtrl.pwm_output--;
            gpsdo_dac_write16(gCtrl.pwm_output);
            xSemaphoreGive(xCtrlMutex);
        }
        cli_puts("PWM-1: "); cli_putint(gCtrl.pwm_output);
        return;
    }
    if (cli_ieq(verb, "dp10")) {
        if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gCtrl.pwm_output >= 11) gCtrl.pwm_output -= 10;
            gpsdo_dac_write16(gCtrl.pwm_output);
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
                gpsdo_dac_write16(gCtrl.pwm_output);
                xSemaphoreGive(xCtrlMutex);
            }
        } else {
            int32_t v = atoi(arg);
            if (v >= 1 && v <= 65535) {
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.pwm_output = (uint16_t)v;
                    gpsdo_dac_write16(gCtrl.pwm_output);
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_puts("PWM set: "); cli_putint((int)v);
            } else {
                cli_reject("SP: value must be 1..65535");
            }
        }
        if (arg != NULL) cli_manual_save("ES ALGO");
        return;
    }

    /* ---- LA <0-12> ---- */
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
                cli_warn_ltic_cal();
                cli_putln("picDIV will arm on ACQ entry. Watch trend: ACQ/DPLL/LOCK.");
#else
                cli_putln("LA 10 needs GPSDO_LTIC enabled at build time.");
#endif
            } else if (v == 11) {
#ifdef GPSDO_LTIC
                /* Algo 11 = LTIC-Lars continuous PI. Shares the LTIC detector
                 * calibration; gain auto-derives from CT unless set with LG. */
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = 11;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_putln("Algorithm: 11 (LTIC-Lars continuous PI)");
                cli_warn_ltic_cal();
                cli_putln("Watch trend: ACQ (freq-led) / PLL (phase) / LOCK (locked).");
#else
                cli_putln("LA 11 needs GPSDO_LTIC enabled at build time.");
#endif
            } else if (v == 12) {
                /* Algo 12 needs the phase detector, like 10 and 11. It was first
                 * written to fall back to the frequency counter on boards without
                 * one; that fallback integrated quantisation noise into a random
                 * walk and destroyed the lock, so it is gone. Refuse rather than
                 * run something that cannot work. */
#ifndef GPSDO_LTIC
                cli_reject("LA 12: needs the LTIC phase detector "
                           "(enable GPSDO_LTIC and run LC)");
                return;
#endif
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = 12;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_putln("Algorithm: 12 (multi-level accumulator, after MIS42N)");
                /* Guarded even though the #ifndef above already returned: the
                 * preprocessor removes that return, not this call, so without
                 * the guard a counter-only build fails to compile here. */
#ifdef GPSDO_LTIC
                cli_warn_ltic_cal();
#endif
                cli_putln("No LTC to set: the error picks its own averaging time.");
                cli_putln("UNTUNED - per-level limits are not yet measured.");
            } else if (v == 13) {
                /* Algo 13 estimates the phase, so it needs the detector too. */
#ifndef GPSDO_LTIC
                cli_reject("LA 13: needs the LTIC phase detector "
                           "(enable GPSDO_LTIC and run LC)");
                return;
#endif
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = 13;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_putln("Algorithm: 13 (Kalman: phase, frequency, aging)");
#ifdef GPSDO_LTIC
                cli_warn_ltic_cal();
#endif
                cli_putln("R and Q are measured, not set. KT is the only knob "
                          "(phase horizon, default 100 s).");
                cli_putln("Holdover is automatic: no phase, no update, keeps "
                          "steering from the model.");
            } else if (v >= 0 && v <= 9) {
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    gCtrl.active_algo = (uint8_t)v;
                    xSemaphoreGive(xCtrlMutex);
                }
                cli_puts("Algorithm: "); cli_putint(v);
            } else {
                cli_reject("LA: 0..13 (10=LTIC 3-stage, 11=LTIC-Lars, 12=multi-level, 13=Kalman)");
            }
        }
        if (arg != NULL) cli_manual_save("ES ALGO");
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
     * Algorithm 10 (LTIC) parameter commands. These set/show the persisted
     * parameters the phase-discipline loop uses. A single helper handles the
     * "show if no arg, else set with range check" pattern for the float
     * fields.
     * ==================================================================== */
    {
        /* table-free dispatch: each verb maps to a float* and a range */
        float *fp = NULL; float lo = 0, hi = 0; const char *lbl = NULL;
        if      (cli_ieq(verb,"LNV")) { fp=&g_ltic.ns_per_volt;     lo=0;     hi=1e6f;    lbl="ns_per_volt"; }
        else if (cli_ieq(verb,"LZO")) { fp=&g_ltic.zero_offset;     lo=0;     hi=3.3f;    lbl="zero_offset[V]"; }
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
            /* AQI and AQD ARE NOT READ BY ANYTHING (TODO item 22). The ACQ
             * branch of ltic_three_stage() uses pid->Kp and nothing else; the
             * centring pull is g_ltic_acq_centre_gain, set by ACG, a separate
             * global with its own units. acq.I_LIMIT IS live — it is the step
             * limiter — so the inert pair is exactly Ki and Kd.
             *
             * They are still accepted and stored rather than refused: the
             * tuner sends all four verbs as one group when Apply is pressed,
             * and a rejection there would look like a fault in the tuner. But
             * turning a knob that does nothing, in silence, is worse than
             * either, so every read and every write says so. Removing the
             * fields outright means touching the settings block, which is
             * item 21's job and wants SETTINGS_VER. */
            const bool inert = (pid == &g_ltic.acq) && (k == 'I' || k == 'D');
            if (arg == NULL) { cli_puts(which); cli_puts(" "); cli_puts(kn); cli_puts("="); cli_putfloat((float)*tgt, 6); }
            else {
                double v = atof(arg);
                if (v >= rlo && v <= rhi) { *tgt = v; cli_puts(which); cli_puts(" "); cli_puts(kn); cli_puts("="); cli_putfloat((float)v, 6); }
                else { cli_puts(verb); cli_putln(": out of range"); }
            }
            if (inert) {
                cli_putln("");
                cli_putln("  note: ACQ reads only Kp. This value is stored and printed but");
                cli_putln("        nothing uses it — the ACQ centring pull is 'ACG'.");
            }
            return;
        }
    }
    /* ---- LIV [val] — LOCK update interval [s] ---- */
    if (cli_ieq(verb, "LIV")) {
        if (arg == NULL) { cli_puts("lock_interval_s="); cli_putint(g_ltic.lock_interval_s); }
        else {
            long v = atol(arg);
            if (v >= 1 && v <= 600) { g_ltic.lock_interval_s = (uint16_t)v; cli_puts("lock_interval_s="); cli_putint((int)v); }
            else cli_reject("LIV: 1..600 s (LOCK correction interval)");
        }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    /* ---- LPOL [-1/0/1] — PWM→phase polarity (0=not set, loop holds) ---- */
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
            cli_putln("LRN: learned drift/damping reset to theory");
            cli_manual_save("ES FLAGS");
        } else {
            int v = atoi(arg);
            if (arg[0] >= '0' && arg[0] <= '9' && v != 0 && v != 1) {
                /* numeric but not a toggle: the detector range in ns
                 * (the float table used to claim this verb first, which
                 * made the 0|1|R form above unreachable). */
                if (xSemaphoreTake(xCtrlMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    g_ltic.range_ns = (float)v;
                    xSemaphoreGive(xCtrlMutex);
                    cli_puts("range_ns="); cli_putfloat((float)v, 6); cli_putln("");
                    cli_manual_save("ES LTIC");
                } else cli_putln("LRN: busy");
            } else if (v == 0 || v == 1) { g_lrn_enable = (v != 0); cli_puts("learn="); cli_putint(v); cli_putln("");
                                    cli_manual_save("ES FLAGS"); }
            else cli_reject("LRN: 0 (off), 1 (on), R (reset), or a number = detector range ns");
        }
        return;
    }
    if (cli_ieq(verb, "WU")) {
        if (arg == NULL) {
            cli_puts("warmup="); cli_putln(g_warmup_enable ? "1 (on)" : "0 (off)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) { g_warmup_enable = (v != 0); cli_puts("warmup="); cli_putint(v); cli_putln("");
                                    cli_autosaved(SET_FLAGS, "FLAGS: WU/SPL/SAW/LRN/SV/BL"); }
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
            if (v == 0 || v == 1) { g_splash_enable = (v != 0); cli_puts("splash="); cli_putint(v); cli_putln("");
                                    cli_autosaved(SET_FLAGS, "FLAGS: WU/SPL/SAW/LRN/SV/BL"); }
            else cli_putln("SPL: 0 (static title only) or 1 (boot animation)");
        }
        return;
    }
    if (cli_ieq(verb, "LPOL")) {
        if (arg == NULL) {
            cli_puts("polarity="); cli_putint(g_ltic.polarity);
            /* NOT "(auto)". All three LTIC loops treat 0 as REFUSE TO RUN and
             * print a line asking for it to be set; calling that auto told the
             * operator the firmware would work it out, which is the one thing
             * it will not do. LC measures the polarity — that is the "auto"
             * that exists, and it has to be run. */
            cli_putln(g_ltic.polarity == 0 ? " (not set - loop holds)" : "");
        } else {
            int v = atoi(arg);
            if (v == -1 || v == 0 || v == 1) { g_ltic.polarity = (int8_t)v; cli_puts("polarity="); cli_putint(v); }
            else cli_reject("LPOL: -1, 0 (not set), or 1");
        }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }

    /* ---- LTIC-Lars (algo 11) parameters ---- */
    if (cli_ieq(verb, "LG")) {                 /* gain: 0=auto from CT, else manual */
        if (arg == NULL) {
            if (g_lars.gain > 0.0f) { cli_puts("gain="); cli_putfloat(g_lars.gain, 3); }
            else cli_putln("gain=0 (auto from CT calibration)");
        } else { float v = atof(arg);
            if (v >= 0.0f && v <= 10000.0f) {
                g_lars.gain = v;
                if (v > 0.0f) { cli_puts("gain="); cli_putfloat(v, 3); }
                else cli_putln("gain=0 (auto from CT calibration)");
            } else cli_reject("LG: 0..10000 (0=auto from CT, else manual VCO gain)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LD")) {                 /* damping */
        if (arg == NULL) { cli_puts("damping="); cli_putfloat(g_lars.damping, 3); }
        else { float v = atof(arg);
            if (v > 0.0f && v <= 1000.0f) { g_lars.damping = v; cli_puts("damping="); cli_putfloat(v, 3); }
            else cli_reject("LD: 0..1000 (loop damping)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LTC")) {                /* time constant [s] */
        if (arg == NULL) { cli_puts("time_const_s="); cli_putint(g_lars.time_const_s); }
        else { long v = atol(arg);
            if (v >= 1 && v <= 600) { g_lars.time_const_s = (uint16_t)v; cli_puts("time_const_s="); cli_putint((int)v); }
            else cli_reject("LTC: 1..600 s (loop time constant)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LFD")) {                /* filter divisor */
        if (arg == NULL) { cli_puts("filter_div="); cli_putint(g_lars.filter_div); }
        else { long v = atol(arg);
            if (v >= 1 && v <= 100) { g_lars.filter_div = (uint8_t)v; cli_puts("filter_div="); cli_putint((int)v); }
            else cli_reject("LFD: 1..100 (pre-filter = time_const / this)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LTO")) {                /* TIC offset (phase target) [V] */
        /* VOLTS at the face, ADC counts in the settings block — the same split
         * MLP uses for nanoseconds. Storage format is untouched, so existing
         * saves and SETTINGS_VER are unaffected; only what a human types and
         * reads changes. Both are printed, because the stored number is what
         * a flash dump will show. */
        if (arg == NULL) {
            cli_puts("tic_offset="); cli_putfloat((float)adc_to_v(g_lars.tic_offset), 4);
            cli_puts(" V ("); cli_putint(g_lars.tic_offset); cli_puts(" counts)");
        } else {
            double v = atof(arg);
            if (v >= 0.0 && v <= ADC_FULL_SCALE_V) {
                g_lars.tic_offset = v_to_adc(v);
                cli_puts("tic_offset="); cli_putfloat((float)adc_to_v(g_lars.tic_offset), 4);
                cli_puts(" V ("); cli_putint(g_lars.tic_offset); cli_puts(" counts)");
                cli_manual_save("ES LTIC");
            } else if (v > ADC_FULL_SCALE_V && v <= 4095.0) {
                /* Almost certainly an old ADC-count value, typed from notes or
                 * from a manual that predates the change. Say what it means in
                 * the new unit rather than a bare "out of range". */
                char t[72];
                snprintf(t, sizeof(t), "LTO now takes VOLTS (0..%.3f). %ld counts = %.4f V — type that.",
                         ADC_FULL_SCALE_V, (long)v, adc_to_v((uint16_t)v));
                cli_reject(t);
            } else {
                cli_reject("LTO: 0..3.300 V (phase reference)");
            }
        }
        return;
    }
    if (cli_ieq(verb, "LPL")) {                /* lock phase window [ns] */
        if (arg == NULL) { cli_puts("lock_ns_lim="); cli_putint(g_lars.lock_ns_lim); }
        else { long v = atol(arg);
            if (v >= 1 && v <= 10000) { g_lars.lock_ns_lim = (uint16_t)v; cli_puts("lock_ns_lim="); cli_putint((int)v); }
            else cli_reject("LPL: 1..10000 ns (lock phase window)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LPF")) {                /* lock factor */
        if (arg == NULL) { cli_puts("lock_factor="); cli_putint(g_lars.lock_factor); }
        else { long v = atol(arg);
            if (v >= 1 && v <= 100) { g_lars.lock_factor = (uint8_t)v; cli_puts("lock_factor="); cli_putint((int)v); }
            else cli_reject("LPF: 1..100 (lock hold = factor * time_const)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LTK")) {                /* temp coefficient */
        if (arg == NULL) { cli_puts("temp_coeff="); cli_putint(g_lars.temp_coeff); }
        else { long v = atol(arg);
            if (v >= -32000 && v <= 32000) { g_lars.temp_coeff = (int16_t)v; cli_puts("temp_coeff="); cli_putint((int)v); }
            else cli_reject("LTK: -32000..32000 (temp feed-forward, DAC/ADC step)"); }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }
    if (cli_ieq(verb, "LTR")) {                /* temp reference [V] */
        if (arg == NULL) {                     /* volts at the face — see LTO */
            cli_puts("temp_ref="); cli_putfloat((float)adc_to_v(g_lars.temp_ref), 4);
            cli_puts(" V ("); cli_putint(g_lars.temp_ref); cli_puts(" counts)");
        } else {
            double v = atof(arg);
            if (v >= 0.0 && v <= ADC_FULL_SCALE_V) {
                g_lars.temp_ref = v_to_adc(v);
                cli_puts("temp_ref="); cli_putfloat((float)adc_to_v(g_lars.temp_ref), 4);
                cli_puts(" V ("); cli_putint(g_lars.temp_ref); cli_puts(" counts)");
                cli_manual_save("ES LTIC");
            } else if (v > ADC_FULL_SCALE_V && v <= 4095.0) {
                char t[72];
                snprintf(t, sizeof(t), "LTR now takes VOLTS (0..%.3f). %ld counts = %.4f V — type that.",
                         ADC_FULL_SCALE_V, (long)v, adc_to_v((uint16_t)v));
                cli_reject(t);
            } else {
                cli_reject("LTR: 0..3.300 V (temperature reference)");
            }
        }
        return;
    }
    /* ---- LCV [volts] — ACQ centring target (0=use range middle) ---- */
    if (cli_ieq(verb, "LCV")) {
        if (arg == NULL) { cli_puts("centre_v="); cli_putfloat(g_ltic.centre_v, 3); }
        else {
            double v = atof(arg);
            if (v >= 0.0 && v <= 3.3) { g_ltic.centre_v = (float)v; cli_puts("centre_v="); cli_putfloat((float)v, 3); }
            else cli_reject("LCV: 0..3.3 V (0=auto)");
        }
        if (arg != NULL) cli_manual_save("ES LTIC");
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
        /* Say it here too: LL is where someone reads the tuning before
         * changing it, and two of these four numbers steer nothing. */
        cli_putln("        (ACQ uses Kp and IL only; Ki/Kd are inert — see ACG)");
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
        cli_puts(g_ltic.polarity == 0 ? " (not set - loop holds)" : "");
        cli_puts("  LCV=");      cli_putfloat(g_ltic.centre_v, 3);
        cli_putln(g_ltic.centre_v == 0.0f ? " (auto=range mid)" : "");
        /* THIS STATE BELONGS TO ALGORITHM 10 AND TO NOTHING ELSE. It is
         * persisted, so under any other algorithm LL was reporting where the
         * three-stage loop stopped the last time it ran — possibly in a
         * previous session, since the value is recalled from flash. Printed
         * without that qualification beside the live LTIC parameters, it read
         * as the current state of whatever loop is running now.
         *
         * The ternary was the second half of the fault: every value that was
         * neither ACQ nor DPLL printed as LOCK, so a byte that had never been
         * written, or had been recalled from an older settings layout, claimed
         * the most reassuring of the three states rather than the least. */
        if (gCtrl.active_algo == 10) {
            const char *sn = (g_ltic.state == LTIC_ACQ)  ? "ACQ"  :
                             (g_ltic.state == LTIC_DPLL) ? "DPLL" :
                             (g_ltic.state == LTIC_LOCK) ? "LOCK" : "?";
            cli_puts("  state="); cli_puts(sn);
            cli_putln("  (3-stage phase loop: ACQ->DPLL->LOCK)");
        } else {
            cli_puts("  state=- (algo ");  cli_putint(gCtrl.active_algo);
            cli_putln(" running; the 3-stage state is algo 10's)");
        }
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
                cli_reject("BC: value must be 0.0001..1.0");
            }
        }
        if (arg != NULL) cli_manual_save("ES PID");
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
                cli_reject("BS: value must be 0.0001..1.0");
            }
        }
        if (arg != NULL) cli_manual_save("ES PID");
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
                cli_reject("NS: value must be 1..10000");
            }
        }
        if (arg != NULL) cli_manual_save("ES PID");
        return;
    }

    /* ---- LT [0|1] — show UTC or local time --------------------------------
     * The help has always documented this and g_show_local_time has always been
     * read by the display and report paths, but the handler itself was never
     * written, so the verb silently did nothing. Implemented here to match what
     * the help promises. Saved by ES with the rest of the timezone group. */
    if (cli_ieq(verb, "LT")) {
        if (arg == NULL) {
            cli_puts("Time display: ");
            cli_putln(g_show_local_time ? "1 (local)" : "0 (UTC)");
            return;
        }
        if (arg[0] == '0' || arg[0] == '1') {
            g_show_local_time = (arg[0] == '1');
            cli_puts("Time display: ");
            cli_putln(g_show_local_time ? "1 (local)" : "0 (UTC)");
            cli_autosaved(SET_TZ, "TZ: zone/offset/LT");
        } else {
            cli_putln("LT: 0 = UTC, 1 = local time");
        }
        return;
    }

    /* ---- TO [n[:mm] | A] — fixed offset, or the legacy EU auto mode ---- */
    if (cli_ieq(verb, "TO")) {
        if (arg == NULL) {
            cli_puts("Time offset: ");
            cli_put_offset(g_time_offset_min);
            switch (g_tz_mode) {
            case TZ_MODE_AUTO_EU: cli_putln("  (auto: GPS position + EU DST)"); break;
            case TZ_MODE_POSIX:   cli_puts("  (zone: "); cli_puts(g_tz_str);
                                  cli_putln(")"); break;
            default:              cli_putln("  (manual)"); break;
            }
            return;
        }
        if (arg[0] == 'A' || arg[0] == 'a') {
            g_tz_mode = TZ_MODE_AUTO_EU;
            cli_putln("Time offset: AUTO — zone from GPS position, EU DST rule");
            cli_putln("Reliable in Europe only: no DST elsewhere, whole hours");
            cli_putln("only. For anywhere else use TZ (see H TZ).");
            cli_autosaved(SET_TZ, "TZ: zone/offset/LT");
            return;
        }
        /* Accept "9:30" and "-3:30" as well as plain hours — half-hour zones
         * are the whole reason this command grew minutes. */
        int16_t mins;
        if (!cli_parse_offset(arg, &mins)) {
            cli_putln("TO: use -14..+14, or h:mm (e.g. 9:30, -3:30), or A");
            return;
        }
        g_tz_mode        = TZ_MODE_MANUAL;
        g_tz_manual_min  = mins;
        g_time_offset_min = mins;        /* apply now, don't wait for a fix */
        cli_puts("Time offset: ");
        cli_put_offset(mins);
        cli_putln("  (manual, no DST)");
        cli_autosaved(SET_TZ, "TZ: zone/offset/LT");
        return;
    }

    /* ---- TZ [zone | posix-rule] — full timezone with DST ---- */
    if (cli_ieq(verb, "TZ")) {
        if (arg == NULL) {
            if (g_tz_mode == TZ_MODE_POSIX) {
                cli_puts("TZ: "); cli_putln(g_tz_str);
                cli_puts("    current offset ");
                cli_put_offset(g_time_offset_min);
                cli_putln("");
            } else {
                cli_putln(g_tz_mode == TZ_MODE_AUTO_EU
                          ? "TZ: not set (TO A active — EU auto)"
                          : "TZ: not set (TO manual offset active)");
            }
            cli_putln("Set with a zone name (TZ Adelaide) or a POSIX rule.");
            cli_putln("H TZ explains both.");
            return;
        }
        int r = tz_set_posix(arg);
        if (r == 0) {
            cli_puts("TZ: unknown zone or bad rule: "); cli_putln(arg);
            cli_putln("Try a city name (TZ Adelaide) — see H TZ.");
            return;
        }
        cli_puts("TZ: "); cli_putln(g_tz_str);
        if (r < 0) {
            /* Morocco is the only zone in current tzdata that lands here:
             * its DST follows Ramadan, which no fixed rule can express. */
            cli_putln("Note: this zone's DST rule can't be expressed in the");
            cli_putln("POSIX form — using its standard offset year-round.");
        }
        cli_putln("(takes effect on the next fix)");
        cli_autosaved(SET_TZ, "TZ: zone/offset/LT");
        return;
    }

    /* ---- SV [0|1] — survey-in (Time Mode) enable, saved by ES ---- */
    if (cli_ieq(verb, "SV")) {
#ifdef GPSDO_GPS_TIMING
        if (arg == NULL) {
            cli_puts("Survey-in (Time Mode): ");
            cli_putln(g_svin_enabled ? "ENABLED" : "DISABLED");
            cli_putln("(SV 1 = on, SV 0 = off; takes effect at next boot; auto-saved)");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) {
                g_svin_enabled = (v == 1);
                cli_puts("Survey-in: ");
                cli_putln(g_svin_enabled ? "ENABLED (next boot)" : "DISABLED (next boot)");
                cli_autosaved(SET_FLAGS, "FLAGS: WU/SPL/SAW/LRN/SV/BL");
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

    /* ---- PO <f> ---- Calibration offset in pascals, added to the BMP280's
     * raw reading before the /100 to hPa. Zero is legal — it means "show the
     * sensor's own pressure". Range matches the recall guard in
     * gpsdo_settings_store.cpp so nothing accepted here is silently dropped on
     * the next boot. */
    if (cli_ieq(verb, "PO")) {
        if (arg == NULL) {
            cli_puts("Pressure offset [Pa]: "); cli_putfloat(g_pressure_offset, 2);
        } else {
            float v = (float)atof(arg);
            if (v >= -5000.0f && v <= 5000.0f) {
                g_pressure_offset = v;
                cli_puts("Pressure offset [Pa]: "); cli_putfloat(v, 2);
                cli_autosaved(SET_PO, "PO/AO");
            } else {
                cli_putln("PO: -5000..5000 Pa");
            }
        }
        return;
    }

    /* ---- AO <f> ---- Offset in metres added to the GPS altitude wherever it
     * is shown (serial report and TFT). It is NOT a barometric correction:
     * the old implementation mixed it into readAltitude()'s sea-level hPa
     * argument, silently changing units and feeding a value nothing ever
     * displayed. Range matches the recall guard. */
    if (cli_ieq(verb, "AO")) {
        if (arg == NULL) {
            cli_puts("Altitude offset [m]: "); cli_putfloat(g_altitude_offset, 2);
        } else {
            float v = (float)atof(arg);
            if (v >= -3000.0f && v <= 3000.0f) {
                g_altitude_offset = v;
                cli_puts("Altitude offset [m]: "); cli_putfloat(v, 2);
                cli_autosaved(SET_PO, "PO/AO");
            } else {
                cli_putln("AO: -3000..3000 m");
            }
        }
        return;
    }

    /* ---- Settings persistence (flash ring) ----
     * ES without an argument saves EVERYTHING (backwards compatible). With
     * an object name it saves only that group: a deliberate ES TZ cannot
     * clobber a hand-tuned PID set the way the old full EEPROM write could. */
    if (cli_ieq(verb, "ES")) {
        if (arg == NULL) {
            cli_putln("Saving settings (full)...");
            settings_save();
            live_store_request_save();
            cli_putln("Done.");
        } else if (cli_ieq(arg, "TZ")) {
            cli_putln("Saving TZ...");
            settings_save_partial(SET_TZ);
            cli_putln("Done.");
        } else if (cli_ieq(arg, "PID")) {
            cli_putln("Saving PID...");
            settings_save_partial(SET_PID);
            cli_putln("Done.");
        } else if (cli_ieq(arg, "LTIC")) {
            cli_putln("Saving LTIC params...");
            settings_save_partial(SET_LTIC);
            /* The detector calibration lives in TWO places and the other one
             * wins at boot: LC writes ns_per_volt / zero_offset / range_ns to
             * the live-store slot (live_store_request_save at the end of
             * do_ltic_calibrate), and setup() applies the settings block FIRST
             * and the live slot AFTER it. So an LNV/LZO/LRN set by hand and
             * saved here was silently overwritten by the last LC result on the
             * next reset — measured 25.08: LNV was set to 1252, saved, and the
             * board came back up on 2649.3914 with no complaint.
             *
             * Refreshing the live slot here makes the two agree, so it no
             * longer matters which is applied last. Reordering the two loads
             * would have been the other fix, but LC saves ONLY to the live
             * slot, so making the settings block authoritative would have
             * stopped an ordinary LC surviving a reboot. */
            live_store_request_save();
            cli_putln("Done.");
        } else if (cli_ieq(arg, "FLAGS")) {
            cli_putln("Saving flags...");
            settings_save_partial(SET_FLAGS);
            cli_putln("Done.");
        } else if (cli_ieq(arg, "ALGO12")) {
            cli_putln("Saving algo-12 params...");
            settings_save_partial(SET_ALGO12);
            cli_putln("Done.");
        } else if (cli_ieq(arg, "ALGO")) {
            cli_putln("Saving algo+PWM...");
            settings_save_partial(SET_ALGO);
            cli_putln("Done.");
        } else if (cli_ieq(arg, "PO")) {
            cli_putln("Saving pressure/alt offset...");
            settings_save_partial(SET_PO);
            cli_putln("Done.");
        } else {
            cli_putln("ES usage: ES | ES TZ|PID|LTIC|FLAGS|ALGO12|ALGO|PO");
        }
        return;
    }
    if (cli_ieq(verb, "ER")) {
        cli_putln("Recalling settings...");
        persist_recall();
        return;
    }
    if (cli_ieq(verb, "EE")) {
        cli_putln("Erasing settings (will reset to defaults on next boot if sector wiped)...");
        persist_erase();
        cli_putln("Done.");
        return;
    }
    if (cli_ieq(verb, "CS")) {
        /* Correction statistics - the loop assessing itself, so a builder with no
         * reference standard has something better than a lock indicator to go on.
         * The idea is Alan's (MIS42N on EEVblog), whose own design relies on
         * exactly this and therefore needs no secondary standard.
         *
         * See gpsdo_health.h for the caveat that matters: this says whether the
         * loop is SETTLED, not whether the output is GOOD, and the two part
         * company precisely when the phase detector is noisy. */
        health_stats_t h;
        health_get(&h);
        char line[120];

        if (!h.counting) {
            cli_putln("Corrections: NOT COUNTING - loop not locked, calibrating,");
            /* Algorithms 12 and 13 used to land here for a reason this line
             * denied: they were excluded outright by counting_now(). They are
             * counted now, so the only algorithms without a lock state really
             * are 0-9 and the sentence is true again. */
            cli_putln("  or running algorithm 0-9 (no lock state to gate on).");
        }
        snprintf(line, sizeof(line),
                 "Corrections: %lu counted, %lu skipped, peak %u LSB",
                 (unsigned long)h.updates, (unsigned long)h.gated,
                 (unsigned)h.peak);
        cli_putln(line);
        snprintf(line, sizeof(line),
                 "  RMS  100=%.2f  1k=%.2f  10k=%.2f  100k=%.2f LSB",
                 h.rms_100, h.rms_1k, h.rms_10k, h.rms_100k);
        cli_putln(line);

        /* Windows are counted in corrections, not seconds, because the rate
         * depends on the algorithm and on LIV. Print what they currently mean in
         * wall-clock time so the reader does not have to work it out. */
        if (h.secs_per_corr > 0.0) {
            double w100k = 100000.0 * h.secs_per_corr / 3600.0;
            snprintf(line, sizeof(line),
                     "  (corrections, %.1f s apart: 100 = %.1f min, 100k = %.1f h)",
                     h.secs_per_corr, 100.0 * h.secs_per_corr / 60.0, w100k);
            cli_putln(line);
        }

        if (h.have_k) {
            double f = h.k_hz_per_lsb / 10.0e6;
            snprintf(line, sizeof(line),
                     "  as df/f: 100=%.2e  1k=%.2e  10k=%.2e  100k=%.2e",
                     h.rms_100 * f, h.rms_1k * f, h.rms_10k * f, h.rms_100k * f);
            cli_putln(line);
            snprintf(line, sizeof(line), "  steady bias: %+.2f LSB = %+.2e df/f per correction",
                     h.bias_100k, h.bias_100k * f);
            cli_putln(line);
        } else {
            cli_putln("  (run CT to express these in fractional frequency)");
        }

        cli_putln("  Counted only while locked and not calibrating: the CT and LC");
        cli_putln("  sweeps and the acquisition ramp are commands, not corrections.");
        cli_putln("  Small and steady = the loop is not fighting anything.");
        cli_putln("  Growing = something is wrong. Cannot distinguish a bad");
        cli_putln("  oscillator from a noisy detector - see the CS header above.");
        return;
    }
    /* ---- Algorithm 12: multi-level accumulator ------------------------- */
    if (cli_ieq(verb, "MG")) {                 /* gain, LSB per ns */
        char t[40];
        if (arg == NULL) {
            snprintf(t, sizeof(t), "m_gain=%.3f%s", (double)g_mlacc_gain,
                     g_mlacc_gain <= 0.0f ? " (auto from CT)" : " LSB/ns");
            cli_putln(t);
        } else {
            double v = atof(arg);
            if (v >= 0.0 && v <= 10000.0) {
                g_mlacc_gain = (float)v;
                snprintf(t, sizeof(t), "m_gain=%.3f", v); cli_putln(t);
                /* Check it against the gain CT measured, and say so here as
                 * well as in the loop. MG and algorithm 11's LG are both
                 * printed "LSB per ns" and are different quantities; copying LG
                 * into MG cost a 2.3 hour limit cycle on 26.08 (2.130 against a
                 * measured 31.3). Beyond a factor of four this is not tuning. */
                {
                    double der = (g_pid[7].Kp > 100.0)
                               ? ((double)g_pid[7].Kp / 0.40 / 100.0) : 0.0;
                    if (v > 0.0 && der > 0.0 && (v > 4.0 * der || v * 4.0 < der)) {
                        snprintf(t, sizeof(t), "  CT measured %.2f LSB/ns", der);
                        cli_putln(t);
                        cli_putln("  that is far from what you typed - algo 12's"
                                  " corrections scale with it.");
                        cli_putln("  'MG 0' uses the measured one. Algo 11's LG is"
                                  " a different quantity.");
                    }
                }
                cli_manual_save("ES ALGO12");
            } else cli_reject("MG: 0..10000 LSB/ns (0 = auto from CT)");
        }
        return;
    }
    if (cli_ieq(verb, "MR")) {                 /* forced-correction level */
        if (arg == NULL) { cli_puts("m_run_level="); cli_putint(g_mlacc_run_level); }
        else { long v = atol(arg);
            if (v >= 0 && v < MLACC_LEVELS) {
                g_mlacc_run_level = (uint8_t)v;
                cli_puts("m_run_level="); cli_putint((int)v);
                cli_manual_save("ES ALGO12");
            } else cli_reject("MR: 0..10 (level at which a correction is forced)"); }
        return;
    }
    /* ---- Algorithm 13: Kalman ------------------------------------------ */
    if (cli_ieq(verb, "KR") || cli_ieq(verb, "KQ") || cli_ieq(verb, "KT")) {
        char t[72];
        bool isR = cli_ieq(verb, "KR"), isQ = cli_ieq(verb, "KQ");
        if (arg == NULL) {
            if (isR)      snprintf(t, sizeof(t), "k_R=%.3f ns%s", (double)g_kf_r_ns,
                                   g_kf_r_ns <= 0.0f ? " (measured)" : "");
            else if (isQ) snprintf(t, sizeof(t), "k_Q=%.3e (ns/s)^2/s%s", (double)g_kf_q,
                                   g_kf_q <= 0.0f ? " (adapted)" : "");
            else          snprintf(t, sizeof(t), "k_T=%u s", (unsigned)g_kf_horizon_s);
            cli_putln(t);
        } else {
            double v = atof(arg);
            bool ok;
            if (isR)      { ok = (v >= 0.0 && v <= 1000.0); if (ok) g_kf_r_ns = (float)v; }
            else if (isQ) { ok = (v >= 0.0 && v <= 1.0);    if (ok) g_kf_q    = (float)v; }
            else          { ok = (v >= 10.0 && v <= 10000.0);
                            if (ok) g_kf_horizon_s = (uint16_t)v; }
            if (!ok) {
                cli_reject(isR ? "KR: 0..1000 ns (0 = measure it)"
                         : isQ ? "KQ: 0..1 (ns/s)^2 per s (0 = adapt it)"
                               : "KT: 10..10000 s");
            } else {
                kf_store_save();
                if (isR)      snprintf(t, sizeof(t), "k_R=%.3f ns%s", (double)g_kf_r_ns,
                                       g_kf_r_ns <= 0.0f ? " (measured)" : "");
                else if (isQ) snprintf(t, sizeof(t), "k_Q=%.3e%s", (double)g_kf_q,
                                       g_kf_q <= 0.0f ? " (adapted)" : "");
                else          snprintf(t, sizeof(t), "k_T=%u s", (unsigned)g_kf_horizon_s);
                cli_putln(t);
                cli_putln("  saved");
                /* Said here because the loop cannot say it any other way. The
                 * process noise Sg is seeded and capped at R/KT^3, so a long
                 * horizon forces the ESTIMATOR to be as slow as the CONTROLLER -
                 * and those are different things. Past a few hundred seconds the
                 * cap binds permanently and the loop stops being able to follow
                 * its own oscillator: replayed, phase sd 0.64 -> 2.82 ns at
                 * KT 300 and 3.04 -> 47.6 ns at KT 1000. Removing the cap fixes
                 * KT 1000 and brings the ratchet straight back at KT 100, so
                 * there is no setting of it that serves both.
                 *
                 * THAT PARAGRAPH USED TO END "separating the two needs Sg
                 * measured from the oscillator, which needs a reference this
                 * board does not have." It was wrong. Separating them needs a
                 * second variable and nothing else - see KC and the derivation
                 * at g_kf_ctl_s - because the controller acts on an ESTIMATE
                 * whose white noise the filter has already removed. KT is now
                 * the estimator's horizon alone, and 100 s remains the measured
                 * setting for it. */
                if (!isR && !isQ && g_kf_horizon_s > 200u)
                    cli_putln("  ^ above ~200 s the filter sits against its Q ceiling"
                              " and tracks poorly. 100 s is the measured setting.");
            }
        }
        return;
    }
    if (cli_ieq(verb, "KC")) {
        char t[80];
        if (arg == NULL) {
            kf_stats_t k; kf_get_stats(&k);
            snprintf(t, sizeof(t), "k_C=%u s%s   (in force: %u s)",
                     (unsigned)g_kf_ctl_s,
                     g_kf_ctl_s == 0u ? " (auto = KT/3)" : "", (unsigned)k.ctl_s);
            cli_putln(t);
        } else {
            double v = atof(arg);
            if (!(v == 0.0 || (v >= 10.0 && v <= 10000.0))) {
                cli_reject("KC: 10..10000 s, or 0 for auto (KT/3)");
            } else {
                g_kf_ctl_s = (uint16_t)v;
                kf_store_save();
                kf_stats_t k; kf_get_stats(&k);
                snprintf(t, sizeof(t), "k_C=%u s%s   (in force: %u s)",
                         (unsigned)g_kf_ctl_s,
                         g_kf_ctl_s == 0u ? " (auto = KT/3)" : "", (unsigned)k.ctl_s);
                cli_putln(t);
                cli_putln("  saved");
                /* Both directions are worth naming, because this knob trades
                 * standing phase error against DAC motion and nothing else. */
                if (k.ctl_s > 60u)
                    cli_putln("  ^ a long KC tolerates a standing phase error for"
                              " that long, and books it into the frequency state.");
                if (k.ctl_s < 15u)
                    cli_putln("  ^ below ~15 s the phase stops improving and the DAC"
                              " moves more. The measured knee is 20-30 s.");
            }
        }
        return;
    }
    if (cli_ieq(verb, "KL")) {
        char t[80];
        kf_stats_t k; kf_get_stats(&k);
        cli_putln("Algo 13 (Kalman: phase, frequency, aging):");
        snprintf(t, sizeof(t), "  KR=%.3f ns%s   KQ=%.3e%s   KT=%u s   KC=%u s%s",
                 (double)g_kf_r_ns, g_kf_r_ns <= 0.0f ? "(meas)" : "",
                 (double)g_kf_q,    g_kf_q    <= 0.0f ? "(adapt)" : "",
                 (unsigned)g_kf_horizon_s, (unsigned)k.ctl_s,
                 g_kf_ctl_s == 0u ? "(auto)" : "");
        cli_putln(t);
        snprintf(t, sizeof(t), "  in use: R=%.2f ns  Q=%.3e (ns/s)^2/s%s",
                 (double)k.r_ns, (double)k.q,
                 k.q_at_max ? "  [at ceiling]"
                            : (k.q_at_min ? "  [at floor]"
                                          : (k.q_held ? "  [held]" : "")));
        cli_putln(t);
        /* WHAT THE ADAPTATION IS DOING, not just where it ended up. A ratio
         * above 1 is the innovations asking for more process noise, below 1
         * asking for less; the law moves Q by 0.1% of the mismatch per second.
         * The water marks are there because an unattended night yields one KL,
         * and "Q = 8.2e-06 at the ceiling" reads identically whether Q sat
         * there all night or arrived a minute ago. */
        if (k.q_adapting) {
            snprintf(t, sizeof(t), "          ratio %.2f (%s)   Q while tracking: %.3e .. %.3e",
                     (double)k.q_ratio,
                     (k.q_ratio > 1.05f) ? "wants more Q"
                                         : ((k.q_ratio < 0.95f) ? "wants less" : "consistent"),
                     (double)k.q_lo, (double)k.q_hi);
            cli_putln(t);
        }
        /* The other half of the clock model. Q above is Sg, the frequency random
         * walk; this is Sf, the phase one, measured from the detector's own
         * differences at two lags. Zero means the two lags agree, i.e. the
         * detector shows no walk this filter can distinguish from its own
         * estimator noise - which is the right answer on a clean detector. */
        snprintf(t, sizeof(t), "          Sf=%.3e ns^2/s (phase walk, measured)",
                 (double)k.sf);
        cli_putln(t);
        /* The ceiling is the loop refusing to run more than twice as fast as
         * its horizon. Sitting on it is not a fault - it is the filter being
         * told the detector is noisier than the innovations think - but it is
         * the state that used to end in 195x the seed and a DAC moving four
         * times more than it needed to, so it is worth naming. */
        if (k.q_at_max) {
            snprintf(t, sizeof(t), "  ^ Q at ceiling %.3e: innovations want a loop"
                                   " faster than KT %u s.", (double)k.q_max,
                     (unsigned)g_kf_horizon_s);
            cli_putln(t);
        }
        /* THE OTHER RAIL, and it was invisible until 02.09. Three captures on
         * one board read Q = 7.777e-06, 9.936e-08 and 7.949e-07 at KT 100, 40
         * and 20; the first was the ceiling and the other two were the floor,
         * each to four significant figures. KL reported only the first, so two
         * of the three looked like a healthy adaptation and a whole KT sweep
         * was spent measuring where a rail happened to sit. */
        if (k.q_at_min) {
            snprintf(t, sizeof(t), "  ^ Q on the FLOOR %.3e: the adaptation has"
                                   " walked all the way down.", (double)k.q_min);
            cli_putln(t);
            cli_putln("    Q that small makes the filter trust its own model over"
                      " the detector - expect a slow loop whatever KT says.");
        }
        /* And the state that should now replace that walk: the innovations came
         * out smaller than R alone, so they say nothing about Q and the
         * adaptation stands still rather than shrinking Q to repair R. */
        if (k.q_held) {
            if (k.q_freeze_s) {
                snprintf(t, sizeof(t), "  ^ Q FROZEN for %lu more s: an arm moved the"
                                       " reference, so the innovations",
                         (unsigned long)k.q_freeze_s);
                cli_putln(t);
                cli_putln("    describe the landing rather than the oscillator.");
            } else if (k.rej_pct >= 5.0f) {
                cli_putln("  ^ Q HELD: the gate is rejecting heavily, so the"
                          " innovation average is not a clean observation.");
            } else {
                cli_putln("  ^ Q HELD: innovations came out below the detector's own"
                          " white floor - nothing to infer about Q this second.");
            }
        }
        snprintf(t, sizeof(t), "  estimate: phase %+.2f ns  freq %+.3f ps/s  aging %+.2e ns/s2",
                 (double)k.phase_ns, (double)k.freq_ns_s * 1000.0, (double)k.aging_ns_s2);
        cli_putln(t);
        snprintf(t, sizeof(t), "  sigma(phase)=%.2f ns  last innovation %+.2f ns",
                 (double)k.sigma_ns, (double)k.innov_ns);
        cli_putln(t);
        snprintf(t, sizeof(t), "  gate rejected %lu readings, %.1f%% over the last ~5 min",
                 (unsigned long)k.rejects, (double)k.rej_pct);
        cli_putln(t);
        /* Said here because it is the setting that causes a rejection storm and
         * the one nobody remembers changing. A twelve-hour run on 29.08 threw
         * away 11% of its readings with KR pinned at 2.5 ns; the same board
         * measuring R rejected none. */
        if (k.r_pinned && k.rej_pct >= 2.0f)
            cli_putln("  ^ KR is PINNED. A rate above a few per cent usually means it is"
                      " too small - try KR 0 (measure).");
        /* And the same for KQ, because the 01.09 capture cost an evening's
         * analysis to this: the loop was moving the DAC six times less than the
         * week before, which looked like the new ceiling on the adaptation
         * working, and it was not — KQ had been pinned at the seed in an
         * earlier experiment and recalled from the flash ring on every boot
         * since, so the adaptation was never running at all. The header line
         * says so by omitting "(adapt)", which is not enough of a signal for a
         * setting that survives reboots and changes what every other number in
         * this report means. */
        if (!k.q_adapting)
            cli_putln("  ^ KQ is PINNED - the adaptation and its ceiling are OFF."
                      " KQ 0 restores them.");
        if (k.holdover_s) {
            snprintf(t, sizeof(t), "  HOLDOVER: %lu s on the model alone",
                     (unsigned long)k.holdover_s);
            cli_putln(t);
        }
        cli_putln("  R and Q are measured by default; KR/KQ pin them for an experiment.");
        return;
    }
    if (cli_ieq(verb, "MF")) {                 /* where the limits come from */
        /* Separate from MG on purpose. The gain is a property of the
         * OSCILLATOR — LSB per ns, and a different OCXO has a different Vctl
         * sensitivity — while the limits are a property of the PHASE NOISE the
         * board sees, which belongs to the site and the receiver. They shared
         * one `if` until now, so "measured gain with hand-set limits" — exactly
         * what a noisy installation wants — could not be asked for. */
        static const char *const NAMES[4] = {
            "default: sigma formula", "stored table", "sigma formula", "measured"
        };
        char t[72];
        if (arg == NULL) {
            uint8_t s = g_mlacc_thr_src;
            snprintf(t, sizeof(t), "m_thr_src=%u (%s)", (unsigned)s,
                     (s < 4u) ? NAMES[s] : "?");
            cli_putln(t);
        } else {
            long v = atol(arg);
            if (v >= 0 && v <= 3) {
                g_mlacc_thr_src = (uint8_t)v;
                snprintf(t, sizeof(t), "m_thr_src=%ld (%s)", v, NAMES[v]);
                cli_putln(t);
                cli_manual_save("ES ALGO12");
            } else cli_reject("MF: 0=default (formula)  1=stored  2=sigma formula  3=measured");
        }
        return;
    }
    if (cli_ieq(verb, "MFT")) {                /* target s between noise fires */
        /* The one number MF 3 leaves to the user, and it is a statement about
         * what you want rather than a coefficient: how long between corrections
         * that noise alone triggered. Every per-level multiplier follows from
         * it, which is the job the hard-coded "8 sigma" was doing by hand. */
        char t[72];
        unsigned cur = (g_mlacc_thr_tgt_s > 0u) ? (unsigned)g_mlacc_thr_tgt_s
                                                : (unsigned)MLACC_THR_TGT_DEFAULT;
        if (arg == NULL) {
            snprintf(t, sizeof(t), "m_thr_tgt=%us%s", cur,
                     (g_mlacc_thr_tgt_s == 0u) ? " (default)" : "");
            cli_putln(t);
        } else {
            long v = atol(arg);
            /* Below the top level's own period the target says nothing the
             * hierarchy can honour — level 10 is only tested every 2048 s, so
             * asking for less than that between noise fires is asking for
             * something the loop has no opportunity to deliver. The upper bound
             * is the storage: the field is a uint16_t carved out of the block's
             * padding, and a range the CLI accepts but the flash ring cannot
             * hold is worse than a smaller honest one. 65535 s is 18 hours. */
            if (v == 0 || (v >= (long)(1u << MLACC_LEVELS) && v <= 65535)) {
                g_mlacc_thr_tgt_s = (uint16_t)v;
                snprintf(t, sizeof(t), "m_thr_tgt=%us", (unsigned)
                         ((g_mlacc_thr_tgt_s > 0u) ? g_mlacc_thr_tgt_s
                                                   : MLACC_THR_TGT_DEFAULT));
                cli_putln(t);
                cli_manual_save("ES ALGO12");
            } else cli_reject("MFT: 0 (default 3600) or 2048..65535 s");
        }
        return;
    }
    if (cli_ieq(verb, "MLP")) {
        /* MLP <level> [ns] — one row of the limit table. Eleven separate verbs
         * would be worse than one that takes an index. */
        if (arg == NULL) { cli_reject("MLP <level 0..10> [ns]  (ML lists them)"); return; }
        long n = atol(arg);
        if (n < 0 || n >= MLACC_LEVELS) { cli_reject("MLP: level must be 0..10"); return; }
        const char *p2 = arg;
        while (*p2 && *p2 != ' ') p2++;
        while (*p2 == ' ') p2++;
        char t[48];
        if (*p2 == '\0') {
            /* Two numbers, because one is what you set and the other is what it
             * means. The stored value is in accumulator units; the phase it
             * corresponds to is that divided by 2^(level+2), since the level
             * holds 2^(level+1) samples of (2*phase + 1). Printing only the
             * former — and calling it "ns", as this did — invites tuning a
             * number nobody can relate to the oscilloscope. */
            snprintf(t, sizeof(t), "lim[%ld]=%ldns (%ld units over %us)",
                     n, (long)(g_mlacc_lim[n] >> (n + 2)),
                     (long)g_mlacc_lim[n],
                     (unsigned)(1u << (n + 1)));
            cli_putln(t);
        } else {
            /* The value is nanoseconds - the unit humans (and Alan's e-mails)
             * speak. Stored as accumulator units = ns << (level+2): the level
             * holds 2^(level+1) samples of (2*phase + 1). Units stay the
             * storage format (settings block unchanged), ns is the CLI face. */
            long v = atol(p2);
            long u = v << (n + 2);
            if (v >= 1 && u <= 500000) {
                g_mlacc_lim[n] = (int32_t)u;
                snprintf(t, sizeof(t), "lim[%ld]=%ldns (%ld units)", n, v, u);
                cli_putln(t);
                cli_manual_save("ES ALGO12");
            } else {
                snprintf(t, sizeof(t), "MLP: 1..%ld ns at level %ld",
                         (long)(500000L >> (n + 2)), n);
                cli_reject(t);
            }
        }
        return;
    }
    if (cli_ieq(verb, "ML")) {
        char line[76];
        cli_putln("Algo 12 (multi-level accumulator, after MIS42N):");
        snprintf(line, sizeof(line), " m_gain=%.3f%s  m_run_level=%u",
                 (double)g_mlacc_gain,
                 g_mlacc_gain <= 0.0f ? " (auto from CT)" : " LSB/ns", (unsigned)g_mlacc_run_level);
        cli_putln(line);
        {
            mlacc_stats_t st;  mlacc_get_stats(&st);
            mlacc_fit_t   ft;  mlacc_get_fit(&ft);
            char sl[80], b1[16], b2[16];
            uint8_t s = g_mlacc_thr_src;
            static const char *const NAMES[4] = {
                "default", "stored table", "sigma formula", "measured"
            };
            /* MF 0 is the sigma formula now, whatever MG says — the limits
             * stopped following the gain (see mlacc_thr_source). */
            unsigned eff = (s != 0u) ? s : 2u;
            snprintf(sl, sizeof(sl), "  limits from: MF %u (%s) -> %s",
                     (unsigned)s, (s < 4u) ? NAMES[s] : "?",
                     (eff < 4u) ? NAMES[eff] : "?");
            cli_putln(sl);
            snprintf(sl, sizeof(sl), "  measured phase noise: %ld ns 1-sigma",
                     (long)st.sigma_ns);
            cli_putln(sl);
            if (eff == 3u) {
                /* The exponent is the whole point of the measured table, so it
                 * gets printed rather than hidden: 0.50 is what the formula
                 * assumes, and anything near 1.00 says averaging is buying this
                 * board nothing and the formula's table would be far too tight
                 * at every level above the bottom. */
                if (ft.valid) {
                    dtostrf((double)ft.exponent, -1, 3, b1);
                    dtostrf((double)((g_mlacc_thr_tgt_s > 0u)
                            ? g_mlacc_thr_tgt_s : MLACC_THR_TGT_DEFAULT), -1, 0, b2);
                    snprintf(sl, sizeof(sl),
                             "  fitted exponent %s over %u levels (0.50 = white), target %ss",
                             b1, (unsigned)ft.levels_used, b2);
                } else {
                    snprintf(sl, sizeof(sl),
                             "  fit not ready: %u of %u levels have enough tests",
                             (unsigned)ft.levels_used, (unsigned)MLACC_FIT_MIN_LVL);
                }
                cli_putln(sl);
            }
        }
        cli_putln("  phase limits by averaging span (units = phase):");
        for (int i = 0; i < MLACC_LEVELS; i++) {
            snprintf(line, sizeof(line), "    %2d: %5us  %4ld ns = %7ld units%s",
                     i, (unsigned)(1u << (i + 1)),
                     (long)(g_mlacc_lim[i] >> (i + 2)), (long)g_mlacc_lim[i],
                     (i == 6) ? "  <- the one derived value" : "");
            cli_putln(line);
        }
        cli_putln("  UNTUNED: only the 128s limit came from a specification.");
        return;
    }
    if (cli_ieq(verb, "EW")) {
        /* flash-wear diagnostics for the ring buffer */
        uint16_t slots = flash_ring_slot_count();
        if (slots == 0) {
            cli_putln("Flash ring: not initialised — the sector failed to format.");
        } else {
            char line[112];
            snprintf(line, sizeof(line),
                     "Flash ring: erase cycles=%lu  slots used=%u/%u  (sector %u, 0x%08lX)",
                     (unsigned long)flash_ring_erase_count(),
                     (unsigned)flash_ring_slots_used(), (unsigned)slots,
                     (unsigned)flash_ring_sector_no(),
                     (unsigned long)flash_ring_base_addr());
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
                    else { cli_reject("ACG: cap 5..1000 LSB"); return; }
                }
                cli_puts("ACG gain="); cli_putfloat(g_ltic_acq_centre_gain, 0);
                cli_puts(" cap="); cli_putfloat(g_ltic_acq_centre_cap, 0);
                cli_putln("  (higher = faster centring, risk of wrap)");
            } else {
                cli_reject("ACG: gain 50..20000 LSB/V [cap 5..1000 LSB]");
            }
        }
        if (arg != NULL) cli_manual_save("ES LTIC");
        return;
    }

    if (cli_ieq(verb, "FA") || cli_ieq(verb, "FAD") || cli_ieq(verb, "FAL")) {
        /* Frequency-averaging window for the LTIC damping term, per state. A
         * measured ~220 s limit cycle (Dan Wiering's Rb reference, algo 10)
         * traces to the group delay of the long avg100 landing near quadrature;
         * a shorter window is the candidate fix. Split by state so it can be
         * tried in acquisition (FAD) or steady state (FAL) independently — the
         * way to find which one the cycle lives in. FA sets both at once. 100 is
         * the historical value in each and changes nothing. Only the LTIC
         * frequency term is affected; escape detection, self-learning, the
         * state machine and the phase PI all stay on avg100. */
        bool set_dpll = cli_ieq(verb, "FA") || cli_ieq(verb, "FAD");
        bool set_lock = cli_ieq(verb, "FA") || cli_ieq(verb, "FAL");
        if (arg == NULL) {
            cli_puts("FA windows: DPLL="); cli_putint((int)g_freq_damp_win_dpll);
            cli_puts("s LOCK=");           cli_putint((int)g_freq_damp_win_lock);
            cli_putln("s  (FA/FAD/FAL n; n=10/100/1000; 100=default)");
        } else {
            int v = atoi(arg);
            if (v == 10 || v == 100 || v == 1000) {
                if (set_dpll) g_freq_damp_win_dpll = (uint16_t)v;
                if (set_lock) g_freq_damp_win_lock = (uint16_t)v;
                cli_puts(set_dpll && set_lock ? "FA (both) = "
                       : set_dpll             ? "FAD (DPLL) = " : "FAL (LOCK) = ");
                cli_putint(v); cli_putln(" s");
                cli_manual_save("ES LTIC");
            } else {
                cli_reject("FA: 10, 100 or 1000 (no arg = status)");
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
            /* Which pulse does the qErr describe, and did we find it?
             * UBX-13003221-R15 p.66: the TIM-TP sent after pulse N-1 belongs
             * to pulse N, and bit 3 of the flags byte says which convention
             * the receiver is using. Both reduce to the same lookup here, but
             * an assumption that is never checked is the one that bites, so
             * print the receiver's own answer AND how often the lookup found
             * its pulse. A paired count near zero means the TIM-TP frame is
             * arriving after the pulse it describes and the correction is
             * being skipped, not misapplied. */
            cli_puts("  mode=");
            cli_puts(g_qerr_mode_next ? "next pulse" : "this pulse");
            cli_puts(" (flags=0x");
            cli_putint((int)g_qerr_flags);
            cli_puts(")  paired=");
            cli_putint((int)g_qerr_paired);
            cli_puts("/");
            cli_putint((int)(g_qerr_paired + g_qerr_unpaired));
            cli_putln("");
            /* Where the misses land. The rule accepts +1; anything else means
             * the loop is subtracting nothing and steering on a phase with the
             * sawtooth still in it. */
            cli_puts("  latch lag <=-1/0/+1/+2/+3/>=+4:");
            for (unsigned i = 0; i < QERR_LAG_BINS; i++) {
                cli_puts(" "); cli_putint((int)g_qerr_latchlag[i]);
            }
            cli_putln("");
            cli_puts("  frame lag <=-1/0/+1/+2/+3/>=+4:");
            for (unsigned i = 0; i < QERR_LAG_BINS; i++) {
                cli_puts(" "); cli_putint((int)g_qerr_lag[i]);
            }
            cli_putln("");
        } else {
            int v = atoi(arg);
            if (v == 0 || v == 1) {
                g_qerr_enable = (v != 0);
                cli_puts("sawtooth="); cli_putint(v); cli_putln("");
                cli_manual_save("ES FLAGS");
            } else {
                cli_reject("SAW: 0 (off) or 1 (on); no arg = status");
            }
        }
        return;
    }

    if (cli_ieq(verb, "FR")) {
        /* flash_ring is always enabled in v0.96+ (settings + live data share
         * sector 7). FR is retained as a read-only status command for users
         * familiar with the old toggle. */
        cli_putln("flash_ring: always on (settings + live data in sector 7)");
        cli_putln("Use 'EW' for wear diagnostics.");
        return;
    }

    /* ---- RB — warm reboot: software reset, EEPROM kept ----
     * Restarts the firmware via NVIC_SystemReset(). Settings (PWM, model,
     * calibration, LTIC params in the flash ring) are preserved, so after
     * reboot the OCXO — still warm — recalls its disciplined state. Does NOT
     * auto-save first; run ES beforehand if you have unsaved changes. */
    if (cli_ieq(verb, "RB")) {
        cli_putln("Warm reboot (settings kept). Resetting...");
        vTaskDelay(pdMS_TO_TICKS(150));   /* let the line flush */
        NVIC_SystemReset();
        return;                           /* not reached */
    }
    /* ---- CR YES — cold restart: wipe settings, then software reset ----
     * Resets to compile-time defaults (PWM=DEFAULT, survey-in from scratch,
     * no learned OCXO model, LTIC params reset) and reboots, as if powered on
     * for the first time. The flash ring sector is marked invalid so the next
     * boot falls back to defaults. Requires the literal confirmation "CR YES"
     * because it discards the learned model (days to rebuild). */
    if (cli_ieq(verb, "CR")) {
        if (arg != NULL && cli_ieq(arg, "YES")) {
            cli_putln("Cold restart: wiping settings and rebooting...");
            persist_erase();
            vTaskDelay(pdMS_TO_TICKS(150));
            NVIC_SystemReset();
        } else {
            cli_putln("Cold restart wipes settings (PWM, model, calibration, "
                      "LTIC). Type 'CR YES' to confirm.");
        }
        return;
    }

    /* ---- SW - stack watermarks ---- */
    /* ---- TL: the per-task load on the telemetry line ---- */
    if (cli_ieq(verb, "TL")) {
        if (arg) {
            if (arg[0] == '0' || arg[0] == '1') {
                g_cpu_task_line = (arg[0] == '1');
            } else {
                cli_putln("TL: 0 = off, 1 = on");
                return;
            }
        }
        /* Deliberately NOT stored. The line is a diagnostic for a session at
         * the bench, it costs a line of telemetry every second, and a setting
         * that survives a reboot is one nobody remembers turning on. Every
         * boot starts quiet. */
        cli_putln(g_cpu_task_line
                  ? "TL: per-task load ON in telemetry (not stored - off after reset)"
                  : "TL: per-task load off (SW still shows it once)");
        return;
    }

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

        /* CPU per task. One-shot here because that is when it is wanted — the
         * question "what is the board spending its time on" is asked, not
         * watched. TL 1 puts the same numbers on the telemetry line for anyone
         * who does want to watch them. */
        {
            cpu_task_t t[CPU_TASKS_MAX];
            uint8_t n = cpu_tasks_get(t, CPU_TASKS_MAX);
            uint16_t f = cpu_tasks_filled();
            if (n == 0) {
                cli_putln("CPU per task: no switches counted yet");
            } else {
                if (f >= CPU_WINDOW_S)
                    cli_putln("CPU per task (mean over the last 100 s):");
                else {
                    snprintf(tmp, sizeof(tmp),
                             "CPU per task (only %u s of 100 so far):", (unsigned)f);
                    cli_putln(tmp);
                }
                for (uint8_t i = 0; i < n; i++) {
                    snprintf(tmp, sizeof(tmp), "  %-12s %5u.%02u %%",
                             t[i].name, (unsigned)(t[i].pct100 / 100u),
                             (unsigned)(t[i].pct100 % 100u));
                    cli_putln(tmp);
                }
                if (n >= (uint8_t)CPU_TASKS_MAX)
                    cli_putln("  (slot table full - any further task is uncounted)");
                cli_putln("  Shares of switched time; interrupt time lands on the");
                cli_putln("  task it interrupted, so these are 'processor was here'.");
            }
        }

        /* Uptime source and the MCU clock error that made it worth changing.
         * Uptime is counted from the PPS now, so this number no longer affects
         * the clock — but it is the only figure the box has about its own
         * crystal, it costs nothing to keep, and it is what would tell you the
         * board came up on the internal RC oscillator instead of the crystal. */
        {
            uint32_t last = gUpPpsMs;
            bool     live = (last != 0u) && ((millis() - last) < 1500u);
            snprintf(tmp, sizeof(tmp), "  Uptime:     %lu s  (%s)",
                     (unsigned long)gUpSecs,
                     live ? "PPS" : "holdover - MCU crystal");
            cli_putln(tmp);
            if (gMcuPpm == INT32_MIN)
                cli_putln("  MCU clock:  measuring (needs 1024 s of PPS)");
            else {
                snprintf(tmp, sizeof(tmp), "  MCU clock:  %+ld ppm vs GPS",
                         (long)gMcuPpm);
                cli_putln(tmp);
            }
        }
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
            /* Line-based input only: RP/RR pause and resume the telemetry.
             * A bare-TAB/bare-ESC toggle was tried here after Alan's
             * suggestion and REMOVED: real terminals and the tuner send
             * CR/LF-terminated lines and never a bare TAB or ESC, so the
             * feature could not be reached from the tools people actually
             * use. RP and RR do the same job and work everywhere. */
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
