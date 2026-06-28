/**
 * gpsdo_gps.cpp — vGpsTask — GPS NMEA parsing and UBX configuration
 *
 * Part of GPSDO FreeRTOS v0.49
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Reads NMEA sentences from Serial1 (38400 Bd), feeds TinyGPS++, and
 * publishes fix data to gGps under xGpsMutex.
 *
 * On first boot, performs auto-baud detection (9600/38400/115200) and
 * sends UBX configuration: stationary navigation mode, 1 Hz update rate.
 * Handles ACK-ACK / ACK-NAK with bounded retries and minimal-mask
 * fallback for stripped NEO-6M firmware variants.
 */

#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include <Arduino.h>
#include <TinyGPS++.h>
#include <string.h>

static TinyGPSPlus gps;

/* Serial port for echo / messages */
#ifdef GPSDO_BLUETOOTH
  #define CLI_SER Serial2
#else
  #define CLI_SER Serial
#endif

/* ---- UBX configuration ------------------------------------------------ */
#ifdef GPSDO_UBX_CONFIG

static void flush_rx(uint32_t ms)
{
    /* Drain Serial1 RX for `ms` milliseconds, discard all bytes.
     * Called after baud rate transitions to clear framing garbage. */
    uint32_t end = millis() + ms;
    while (millis() < end) {
        while (Serial1.available()) Serial1.read();
        delay(5);
    }
}

static void send_ubx(const uint8_t *msg, uint8_t len)
{
    for (int i = 0; i < len; i++) Serial1.write(msg[i]);
    Serial1.flush();
}

/*
 * ubx_cksum — compute u-blox checksum in-place on msg[2..len-3].
 * Writes CK_A to msg[len-2] and CK_B to msg[len-1].
 */
static void ubx_cksum(uint8_t *msg, uint8_t len)
{
    uint8_t cka = 0, ckb = 0;
    for (uint8_t i = 2; i < len - 2; i++) { cka += msg[i]; ckb += cka; }
    msg[len - 2] = cka;
    msg[len - 1] = ckb;
}

/*
 * get_ubx_ack — wait for UBX-ACK-ACK or UBX-ACK-NAK.
 *
 * Two independent byte-level state machines run in parallel so that
 * NMEA bytes flowing in parallel do not confuse the matcher.
 *
 * Key improvement over original:
 *   The inner loop drains Serial1 CONTINUOUSLY — it does NOT call delay()
 *   unless the FIFO is empty.  At 38400 baud ~3840 B/s arrive; the old
 *   code called delay(1) every time no byte was immediately available,
 *   allowing up to 3-4 bytes to pile up per ms. Over a 2000 ms window
 *   the 256-byte ring buffer overflows ~30 times, silently dropping the
 *   10-byte ACK.
 *
 * Returns: +1 = ACK-ACK, -1 = ACK-NAK, 0 = timeout
 */
static int8_t get_ubx_ack(const uint8_t *msg, uint32_t timeout_ms = 1500)
{
    bool sched_running = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);

    uint8_t ack_frame[10], nak_frame[10];
    ack_frame[0]=0xB5; ack_frame[1]=0x62;
    ack_frame[2]=0x05; ack_frame[3]=0x01;
    ack_frame[4]=0x02; ack_frame[5]=0x00;
    ack_frame[6]=msg[2]; ack_frame[7]=msg[3];
    ack_frame[8]=0; ack_frame[9]=0;
    for (int i=2;i<8;i++){ack_frame[8]+=ack_frame[i];ack_frame[9]+=ack_frame[8];}

    memcpy(nak_frame, ack_frame, 10);
    nak_frame[3] = 0x00;
    nak_frame[8]=0; nak_frame[9]=0;
    for (int i=2;i<8;i++){nak_frame[8]+=nak_frame[i];nak_frame[9]+=nak_frame[8];}

    uint8_t ack_id = 0, nak_id = 0;
    uint32_t t0 = millis();

    while ((millis() - t0) < timeout_ms) {
        if (Serial1.available()) {
            /* Drain available bytes without delay — prevents RX ring buffer
             * overflow at 38400 baud when NMEA runs in parallel. */
            uint8_t b = (uint8_t)Serial1.read();

            if (b == ack_frame[ack_id]) {
                if (++ack_id >= 10) return +1;
            } else {
                ack_id = (b == ack_frame[0]) ? 1u : 0u;
            }

            if (b == nak_frame[nak_id]) {
                if (++nak_id >= 10) {
                    OUT_SERIAL.println("UBX: NAK received");
                    return -1;
                }
            } else {
                nak_id = (b == nak_frame[0]) ? 1u : 0u;
            }
        } else {
            /* Yield only when truly nothing to read */
            if (sched_running) vTaskDelay(pdMS_TO_TICKS(1));
            else                delay(1);
        }
    }
    return 0;
}

/*
 * ubx_send_cfg_msg — send UBX-CFG-MSG to enable/disable one NMEA sentence.
 *
 * UBX-CFG-MSG (class=0x06 id=0x01 length=3):
 *   payload[0] = NMEA message class (0xF0 for standard NMEA)
 *   payload[1] = NMEA message ID
 *   payload[2] = output rate (0=disabled, 1=every fix)
 *
 * Waits up to 500 ms for ACK.
 * Returns true on ACK, false on timeout/NAK.
 *
 * Used to silence noisy NMEA sentences before sending CFG-NAV5 so the
 * 256-byte RX ring buffer does not overflow and drop the ACK response.
 */
static bool ubx_send_cfg_msg(uint8_t nmea_class, uint8_t nmea_id, uint8_t rate)
{
    uint8_t msg[11] = {
        0xB5, 0x62,       /* UBX header  */
        0x06, 0x01,       /* CFG-MSG     */
        0x03, 0x00,       /* length = 3  */
        nmea_class, nmea_id, rate,
        0x00, 0x00        /* CK_A, CK_B  */
    };
    ubx_cksum(msg, sizeof(msg));
    send_ubx(msg, sizeof(msg));
    return (get_ubx_ack(msg, 500) == +1);
}

/*
 * ubx_reduce_nmea — disable noisy NMEA sentences, keep only GGA + RMC.
 *
 * GY-NEO6MV2 / NEO-6M default output:
 *   GGA (F0 00) ← KEEP — provides fix, alt, sats, HDOP
 *   GLL (F0 01) ← DISABLE — position only, redundant
 *   GSA (F0 02) ← DISABLE — DOP + active sats, ~70 B/s
 *   GSV (F0 03) ← DISABLE — satellites in view, largest output: 3×70 B = 210 B/s
 *   RMC (F0 04) ← KEEP — provides date, time, speed
 *   VTG (F0 05) ← DISABLE — course/speed, redundant
 *
 * At 38400 baud with full NMEA: ~460–710 B/s.
 * After reduction (GGA+RMC only): ~150 B/s = 1 byte every 6.7 ms.
 * The 10-byte ACK response for CFG-NAV5 is now safe from buffer overflow.
 *
 * Returns count of successfully ACKed disable commands (0–4).
 * A return value < 4 means some commands were not ACKed, which is normal
 * for locked/stripped firmware; CFG-NAV5 is still attempted.
 */
static uint8_t ubx_reduce_nmea(void)
{
    uint8_t ok = 0;
    /* Small inter-command gap so module does not see back-to-back UBX
     * while still sending NMEA — 20 ms > one NMEA sentence at 38400. */
    struct { uint8_t cls; uint8_t id; } const disable_list[] = {
        {0xF0, 0x01},   /* GLL */
        {0xF0, 0x02},   /* GSA */
        {0xF0, 0x03},   /* GSV */
        {0xF0, 0x05},   /* VTG */
    };
    for (uint8_t i = 0; i < 4; i++) {
        if (ubx_send_cfg_msg(disable_list[i].cls, disable_list[i].id, 0)) {
            ok++;
        }
        delay(20);
    }
    /* Drain residual NMEA that was queued before disabling */
    flush_rx(150);
    return ok;
}

/* ======================================================================
 * Survey-in / Time Mode for LEA-6T / LEA-M8T timing receivers
 * ====================================================================== */
#ifdef GPSDO_GPS_TIMING

/* Start survey-in. The LEA-6T and LEA-M8T accept different Time Mode
 * commands, so try each (both verified in u-center) and stop at the first
 * ACK:
 *   1. CFG-TMODE2 (0x06 0x3D), 28-byte payload  — LEA-M8T
 *   2. CFG-TMODE  (0x06 0x1D), 28-byte payload  — LEA-6T (u-blox 6)
 * Returns true on the first ACK. Caller logs which (if any) succeeded. */
static bool ubx_start_survey_in(void)
{
    const uint32_t dur = GPSDO_SVIN_MIN_SECS;     /* seconds */
    const uint32_t acc = GPSDO_SVIN_ACC_LIMIT;    /* mm      */

    /* ---- Variant 1: CFG-TMODE2 (0x3D), 28-byte payload (LEA-M8T) ----
     * timeMode u1@0, reserved1 u1@1, flags u2@2, ecef/lat i4@4..15,
     * fixedPosAcc u4@16, svinMinDur u4@20, svinAccLimit u4@24. */
    {
        uint8_t m[8 + 28];
        memset(m, 0, sizeof(m));
        m[0]=0xB5; m[1]=0x62; m[2]=0x06; m[3]=0x3D; m[4]=28; m[5]=0x00;
        m[6+0]=0x01;            /* timeMode = 1 (survey-in) */
        m[6+20]=dur&0xFF; m[6+21]=(dur>>8)&0xFF; m[6+22]=(dur>>16)&0xFF; m[6+23]=(dur>>24)&0xFF;
        m[6+24]=acc&0xFF; m[6+25]=(acc>>8)&0xFF; m[6+26]=(acc>>16)&0xFF; m[6+27]=(acc>>24)&0xFF;
        ubx_cksum(m, sizeof(m));
        send_ubx(m, sizeof(m));
        if (get_ubx_ack(m, 1500) == +1) {
            OUT_SERIAL.println("LEA-T: accepted CFG-TMODE2 (28B)");
            return true;
        }
    }
    flush_rx(150);

    /* ---- Variant 2: CFG-TMODE (0x1D), 28-byte payload (LEA-6T) ----
     * u-blox 6 original Time Mode: timeMode u4@0 (note: u4, not u1+flags),
     * fixedPosX i4@4, fixedPosY i4@8, fixedPosZ i4@12, fixedPosVar u4@16,
     * svinMinDur u4@20, svinAccLimit u4@24. timeMode=1 = survey-in. */
    {
        uint8_t m[8 + 28];
        memset(m, 0, sizeof(m));
        m[0]=0xB5; m[1]=0x62; m[2]=0x06; m[3]=0x1D; m[4]=28; m[5]=0x00;
        m[6+0]=0x01; m[6+1]=0x00; m[6+2]=0x00; m[6+3]=0x00;  /* timeMode u4 = 1 */
        m[6+20]=dur&0xFF; m[6+21]=(dur>>8)&0xFF; m[6+22]=(dur>>16)&0xFF; m[6+23]=(dur>>24)&0xFF;
        m[6+24]=acc&0xFF; m[6+25]=(acc>>8)&0xFF; m[6+26]=(acc>>16)&0xFF; m[6+27]=(acc>>24)&0xFF;
        ubx_cksum(m, sizeof(m));
        send_ubx(m, sizeof(m));
        if (get_ubx_ack(m, 1500) == +1) {
            OUT_SERIAL.println("LEA-T: accepted CFG-TMODE (0x1D, 28B)");
            return true;
        }
    }
    flush_rx(150);

    return false;   /* none ACKed — caller falls back to monitoring */
}


/* Poll TIM-SVIN (0x0D 0x04) and parse the survey-in status.
 * Fills *dur [s], *acc_mm, *valid, *active. Returns true if a TIM-SVIN
 * frame was decoded. Used by both LEA-6T and LEA-M8T (28-byte payload,
 * confirmed in u-center: B5 62 0D 04 1C 00 ...). */
static bool ubx_poll_svin(uint32_t *dur, uint32_t *acc_mm,
                          bool *valid, bool *active)
{
    const uint8_t cls = 0x0D, id = 0x04;   /* TIM-SVIN, 28-byte payload */
    uint8_t poll[8] = { 0xB5,0x62, cls, id, 0,0, 0,0 };
    ubx_cksum(poll, sizeof(poll));
    send_ubx(poll, sizeof(poll));

    /* Read the response with a yielding window. The previous (pre-v0.47)
     * version waited up to 1000 ms with a busy delay(), which starved the
     * display task. This version yields with vTaskDelay() between reads, so
     * the display task keeps running even though the window is generous
     * enough (~500 ms) to reliably catch the module's TIM-SVIN reply (its
     * response latency can be 100-200 ms). Non-UBX bytes (NMEA) seen while
     * scanning are forwarded to TinyGPS++ so the position fix is not
     * disrupted by polling. */
    uint8_t buf[44]; uint8_t n = 0;
    int state = 0;
    const uint8_t MAX_SLICES = 50;          /* 50 × 10 ms = 500 ms cap */
    for (uint8_t slice = 0; slice < MAX_SLICES; slice++) {
        while (Serial1.available()) {
            uint8_t b = Serial1.read();
            if (state == 0) { if (b == 0xB5) state = 1; else gps.encode(b); }
            else if (state == 1) { state = (b == 0x62) ? 2 : 0; }
            else if (state == 2) { state = (b == cls) ? 3 : 0; }
            else if (state == 3) { state = (b == id) ? 4 : 0; n = 0; }
            else { if (n < sizeof(buf)) buf[n++] = b;
                   if (n >= sizeof(buf)) goto done; }
        }
        if (n >= 28) break;                 /* full payload already in */
        vTaskDelay(pdMS_TO_TICKS(10));      /* yield — does NOT starve displays */
    }
done:
    if (n < 28) return false;
    /* TIM-SVIN payload (buf[0..1] = length, payload starts at buf[2]):
     *   dur(u4)@0, meanX/Y/Z(i4)@4..15, meanV(u4)@16, obs(u4)@20,
     *   valid(u1)@24, active(u1)@25.
     * meanV is the position VARIANCE in mm² — take the square root to get a
     * 1-sigma accuracy in mm. Early in the survey the receiver reports
     * 0xFFFFFFFF ("no estimate yet"); we clamp that to 65535 mm. */
    *dur    = buf[2+0] | (buf[2+1]<<8) | (buf[2+2]<<16) | ((uint32_t)buf[2+3]<<24);
    uint32_t meanV = buf[2+16]| (buf[2+17]<<8)| (buf[2+18]<<16)| ((uint32_t)buf[2+19]<<24);
    /* integer sqrt (Newton) — meanV up to ~4e9 fits in u32 */
    uint32_t r = meanV, x = (meanV >> 1) + 1;
    if (meanV > 1) { while (x < r) { r = x; x = (meanV / x + x) >> 1; } }
    else           { r = meanV; }
    if (r > 65535u) r = 65535u;
    *acc_mm = r;                    /* sqrt(variance) = 1-sigma accuracy [mm] */
    *valid  = buf[2+24] != 0;
    *active = buf[2+25] != 0;
    return true;
}


/* Non-blocking survey-in monitor — called periodically from vGpsTask AFTER
 * the scheduler is running. Polls TIM-SVIN once and updates the display
 * state. Clears g_svin_pending / g_svin_active when survey-in completes or
 * a safety deadline passes. Called from vGpsTask (scheduler running); the
 * underlying poll yields with vTaskDelay during its short read window, so
 * it does not starve the display task. */
static void ubx_poll_svin_step(void)
{
    static uint32_t svin_deadline = 0;
    static uint8_t  no_reply = 0;
    if (svin_deadline == 0) {
        /* Safety backstop tied to the configured minimum: 3 × SVIN_MIN, but
         * never less than 600 s, so a slow-converging survey (weak signal /
         * small antenna) is given a fair chance before we give up and run
         * with whatever fix the module has. */
        uint32_t cap = GPSDO_SVIN_MIN_SECS * 3u;
        if (cap < 600u) cap = 600u;
        svin_deadline = millis() + cap * 1000u;
    }

    uint32_t dur=0, acc=0; bool valid=false, active=false;
    static bool ever_replied = false;
    if (ubx_poll_svin(&dur, &acc, &valid, &active)) {
        no_reply = 0;
        ever_replied = true;
        g_svin_dur   = (uint16_t)dur;
        g_svin_acc_m = (uint16_t)(acc / 1000u);   /* mm → m for display */
        OUT_SERIAL.print("LEA-T: svin dur="); OUT_SERIAL.print(dur);
        OUT_SERIAL.print("s acc=");             OUT_SERIAL.print(acc);
        OUT_SERIAL.print("mm valid=");          OUT_SERIAL.print(valid);
        OUT_SERIAL.print(" active=");           OUT_SERIAL.println(active);

        /* Survey-in is done when EITHER:
         *   (a) the receiver flags the mean position valid (valid=1) — it has
         *       settled, regardless of whether 'active' has flipped yet (some
         *       firmware leaves active=1 briefly after valid goes high), OR
         *   (b) the user's own criteria are met: accuracy below the limit AND
         *       at least the minimum duration elapsed. This covers receivers
         *       that are slow to raise 'valid' even after converging. */
        bool user_ok = (acc <= GPSDO_SVIN_ACC_LIMIT) &&
                       (dur >= GPSDO_SVIN_MIN_SECS);
        if (valid || user_ok) {
            OUT_SERIAL.print("LEA-T: survey-in complete (");
            OUT_SERIAL.print(valid ? "valid flag" : "acc+time criteria");
            OUT_SERIAL.println(") — time-only fix active");
            g_svin_active = false; g_svin_pending = false;
            return;
        }
    } else if (!ever_replied) {
        /* No TIM-SVIN frame AND we have never had one. The module is likely
         * not running a survey (plain nav mode, or TIM-SVIN output off) —
         * stop monitoring after ~30 consecutive misses rather than waiting
         * the full safety window. Once a survey HAS replied we never give up
         * here (occasional misses are tolerated; the survey is in progress). */
        if (++no_reply >= 30) {
            OUT_SERIAL.println("LEA-T: no TIM-SVIN response — not in survey mode, stopping monitor");
            g_svin_active = false; g_svin_pending = false;
            return;
        }
    }
    if (millis() > svin_deadline) {
        OUT_SERIAL.println("LEA-T: survey-in safety timeout — continuing anyway");
        g_svin_active = false; g_svin_pending = false;
    }
}
#endif /* GPSDO_GPS_TIMING */

/*
 * ubx_config — send UBX-CFG-NAV5 (stationary navigation engine).
 *
 * Called AFTER ubx_reduce_nmea() has quietened the bus.
 * With only GGA+RMC running (~150 B/s) the 256-byte ring buffer can hold
 * ~1.7 s worth of data — far more than the ACK round-trip (~100 ms).
 *
 * Strategy:
 *   Attempt 1: standard CFG-NAV5 (full mask 0xFFFF), 1500 ms timeout.
 *   On NAK:    retry with dynModel-only mask (0x0001) — handles stripped fw.
 *   On timeout: try once more with minimal mask, 1500 ms.
 *   After that:  give up gracefully (non-fatal — GPSDO works without this).
 *
 * The GY-NEO6MV2 APM/FC firmware variant silently drops CFG-NAV5 even
 * when UBX communication is verified working (CFG-MSG ACKs fine). This
 * is a known firmware restriction. The GPSDO operates correctly without
 * stationary mode; OCXO discipline and 1PPS are unaffected.
 */
static bool ubx_config(void)
{
    /* UBX-CFG-NAV5 stationary mode — 44-byte frame, CK_A=0x12 CK_B=0x54
     *
     * Previous frame was CORRUPTED: payload was 38 bytes instead of 36.
     * Two extra bytes (0x10,0x27,0x00,0x00) appeared after dgpsTimeOut —
     * a duplicate of the fixedAltVar field that was mistakenly included
     * as "reserved". The module performs checksum verification FIRST; a
     * checksum mismatch causes silent discard with NO ACK and NO NAK,
     * which is why all 3 attempts timed out.
     *
     * CFG-NAV5 v0 payload fields per u-blox protocol spec 7.01 p.106:
     *   [0-1]   mask              0xFFFF  (apply all fields)
     *   [2]     dynModel          2       (stationary)
     *   [3]     fixMode           3       (auto 2D/3D)
     *   [4-7]   fixedAlt          0
     *   [8-11]  fixedAltVar       10000   (1.0 m²)
     *   [12]    minElev           5 deg
     *   [13]    drLimit           0       (reserved, must be 0)
     *   [14-15] pDop              250     (25.0)
     *   [16-17] tDop              250     (25.0)
     *   [18-19] pAcc              100 m
     *   [20-21] tAcc              300 m
     *   [22]    staticHoldThresh  0
     *   [23]    dgpsTimeOut       0
     *   [24]    cnoThreshNumSVs   0       ← was MISSING (caused 2-byte overrun)
     *   [25]    cnoThresh         0       ← was MISSING
     *   [26-27] reserved2         0
     *   [28-29] staticHoldMaxDist 0
     *   [30]    utcStandard       0
     *   [31]    reserved3         0
     *   [32-35] reserved4         0
     * Total payload: 36 bytes  Header+length: 6  Checksum: 2  Frame: 44 bytes
     *
     * Checksum computed by ubx_cksum() over bytes [2..41] (class through
     * last payload byte), verified by independent Python calculation.
     */
    static const uint8_t setNav[44] = {
        0xB5, 0x62,                         /* UBX sync chars               */
        0x06, 0x24,                         /* class CFG, id NAV5           */
        0x24, 0x00,                         /* payload length = 36          */
        0xFF, 0xFF,                         /* mask: apply all fields        */
        0x02,                               /* dynModel = 2 (stationary)     */
        0x03,                               /* fixMode  = 3 (auto 2D/3D)    */
        0x00, 0x00, 0x00, 0x00,             /* fixedAlt = 0                 */
        0x10, 0x27, 0x00, 0x00,             /* fixedAltVar = 10000          */
        0x05,                               /* minElev = 5 deg              */
        0x00,                               /* drLimit = 0 (reserved)       */
        0xFA, 0x00,                         /* pDop = 250 (25.0)            */
        0xFA, 0x00,                         /* tDop = 250 (25.0)            */
        0x64, 0x00,                         /* pAcc = 100 m                 */
        0x2C, 0x01,                         /* tAcc = 300 m                 */
        0x00,                               /* staticHoldThresh = 0 cm/s    */
        0x00,                               /* dgpsTimeOut = 0 s            */
        0x00, 0x00,                         /* cnoThreshNumSVs=0, cnoThresh=0 */
        0x00, 0x00,                         /* reserved2                    */
        0x00, 0x00,                         /* staticHoldMaxDist = 0 m      */
        0x00,                               /* utcStandard = 0              */
        0x00,                               /* reserved3                    */
        0x00, 0x00, 0x00, 0x00,             /* reserved4                    */
        0x12, 0x54                          /* CK_A, CK_B (verified)        */
    };

    OUT_SERIAL.println("UBX: sending CFG-NAV5 (stationary mode)...");
    send_ubx(setNav, sizeof(setNav));
    int8_t r = get_ubx_ack(setNav, 1500);

    if (r == +1) { OUT_SERIAL.println("UBX: CFG-NAV5 ACK"); return true; }

    if (r == -1 || r == 0) {
        const char *why = (r == -1) ? "NAK" : "timeout";
        OUT_SERIAL.print("UBX: CFG-NAV5 "); OUT_SERIAL.print(why);
        OUT_SERIAL.println(" — retrying with dynModel-only mask");

        /* Minimal mask: apply dynModel field only */
        uint8_t setNavMin[sizeof(setNav)];
        memcpy(setNavMin, setNav, sizeof(setNav));
        setNavMin[6] = 0x01; setNavMin[7] = 0x00;
        ubx_cksum(setNavMin, sizeof(setNavMin));
        flush_rx(50);
        send_ubx(setNavMin, sizeof(setNavMin));
        int8_t r2 = get_ubx_ack(setNavMin, 1500);
        if (r2 == +1) { OUT_SERIAL.println("UBX: CFG-NAV5 (min mask) ACK"); return true; }
        OUT_SERIAL.print("UBX: minimal mask also "); OUT_SERIAL.println(r2==-1?"NAK":"no response");
    }

    OUT_SERIAL.println("UBX: CFG-NAV5 skipped — firmware may have locked nav config");
    OUT_SERIAL.println("UBX: GPSDO operates normally without stationary mode");
    return false;
}
#endif /* UBX_CONFIG */

/* ---- Tunnel mode ------------------------------------------------------ */
/*
 * run_tunnel_mode — transparent serial bridge between CLI_SER and Serial1.
 *
 * Fix: notifies DisplayTask once per second so OLED and TM1637 keep
 * updating with the last valid gGps snapshot during the bridge session.
 * Without this notification both displays froze for the entire tunnel
 * timeout (up to 300 s).
 */
static void run_tunnel_mode(void)
{
    CLI_SER.println("Entering tunnel mode...");
    uint32_t end_ms        = millis() + (TUNNEL_TIMEOUT_SECS * 1000UL);
    uint32_t last_notify   = millis();

    while (millis() < end_ms) {
        if (Serial1.available())  CLI_SER.write(Serial1.read());
        if (CLI_SER.available())  Serial1.write(CLI_SER.read());

        /* Notify DisplayTask ~1 Hz so displays stay alive */
        uint32_t now = millis();
        if ((now - last_notify) >= 1000UL) {
            last_notify = now;
            if (xDisplayTask != NULL)
                xTaskNotifyGive(xDisplayTask);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
    CLI_SER.println("Tunnel mode exited.");
    xEventGroupClearBits(xSysEvents, EVT_TUNNEL_MODE);
}

/* ---- GPS task --------------------------------------------------------- */
/*
 * Read strategy:
 *   - Drain Serial1 RX buffer in bursts of GPS_READ_BURST_MS milliseconds
 *   - Yield for GPS_YIELD_MS between bursts so other tasks run
 *   - TinyGPS++ marks fields as "updated" once per complete sentence
 *   - Publish gGps and notify DisplayTask as soon as time sentence arrives
 *     (time arrives every second; location every second in 1Hz mode)
 *
 * NMEA verbose echo:
 *   - Each echoed byte is collected into a small stack buffer
 *   - Flushed via direct write() call per burst
 *   - Avoids per-byte Serial.write() + mutex acquire overhead
 */
#define GPS_READ_BURST_MS    8u      /* ms to drain RX per burst               */
#define GPS_YIELD_MS         5u      /* ms to yield between bursts              */
#define NMEA_TIMEOUT_MS   3000u      /* mark time invalid after 3s no sentence  */
#define POS_LOST_TIMEOUT_MS 10000u   /* mark pos invalid after 10s without fix  */

/*
 * Two independent staleness timeouts:
 *
 * NMEA_TIMEOUT_MS (3 s):
 *   If NO NMEA sentence at all is received for 3 s, the GPS module has
 *   stopped communicating entirely (power loss, UART failure, etc.).
 *   Clear both valid and pos_valid.
 *
 * POS_LOST_TIMEOUT_MS (10 s):
 *   The GPS module may keep sending NMEA sentences (time, status) even
 *   without an antenna because of its internal RTC / almanac.  In this
 *   case NMEA arrives normally so NMEA_TIMEOUT_MS never fires, but
 *   gps.location.isValid() returns false and the satellite count drops
 *   to 0.  We track the last time a genuine position fix was received
 *   and clear pos_valid (plus zero out position/sats) after 10 s of no
 *   valid location sentence.  This ensures OLED and TM1637 show the
 *   "no fix" state within 10 s of antenna disconnection.
 *
 * gGps.valid (time valid) follows NMEA presence.
 * gGps.pos_valid follows position fix availability.
 * Both are only SET here; they are never set by the display task.
 */
void vGpsTask(void *pvParameters)
{
    (void)pvParameters;

    uint32_t last_sentence_ms  = millis();   /* last NMEA byte received        */
    uint32_t last_pos_valid_ms = millis();   /* last genuine position fix      */
    bool     had_fix           = false;
    bool     had_pos           = false;

#ifdef GPSDO_VERBOSE_NMEA
    /* Small buffer to batch NMEA echo bytes — avoids per-byte mutex acquire */
    static uint8_t echo_buf[128];
    int echo_len = 0;
#endif

    for (;;)
    {
        /* ---- Tunnel mode ---- */
        if (xEventGroupGetBits(xSysEvents) & EVT_TUNNEL_MODE) {
            run_tunnel_mode();
            last_sentence_ms = millis();
            continue;
        }

        /* ---- Drain Serial1 RX for GPS_READ_BURST_MS ---- */
        uint32_t burst_end = millis() + GPS_READ_BURST_MS;
        while (millis() < burst_end) {
            int avail = Serial1.available();
            if (avail <= 0) break;

            /* Read up to 32 bytes per inner loop to stay in burst window */
            int to_read = avail < 32 ? avail : 32;
            for (int i = 0; i < to_read; i++) {
                uint8_t c = Serial1.read();
                gps.encode(c);

#ifdef GPSDO_VERBOSE_NMEA
                /* Only echo in human-readable mode (not tab-delimited) */
                if (!(xEventGroupGetBits(xSysEvents) & EVT_REPORT_TAB)) {
                    if (echo_len < (int)sizeof(echo_buf))
                        echo_buf[echo_len++] = c;
                }
#endif
            }
        }

        /* ---- Flush NMEA echo buffer (under serial mutex) ---- */
#ifdef GPSDO_VERBOSE_NMEA
        if (echo_len > 0) {
            if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                CLI_SER.write(echo_buf, echo_len);
                xSemaphoreGive(xSerialMutex);
            }
            echo_len = 0;
        }
#endif

        /* ---- Check for fresh TinyGPS++ data ---- */
        bool time_fresh     = gps.time.isUpdated();
        bool location_fresh = gps.location.isUpdated();
        bool date_fresh     = gps.date.isUpdated();

        if (time_fresh || location_fresh) {
            last_sentence_ms = millis();
            had_fix = true;

            if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (time_fresh) {
                    gGps.hours = gps.time.hour();
                    gGps.mins  = gps.time.minute();
                    gGps.secs  = gps.time.second();
                    gGps.valid = true;
                }
                if (location_fresh && gps.location.isValid()) {
                    gGps.lat       = (float)gps.location.lat();
                    gGps.lon       = (float)gps.location.lng();
                    gGps.alt       = (float)gps.altitude.meters();
                    gGps.sats      = (uint8_t)gps.satellites.value();
                    gGps.hdop      = gps.hdop.value();
                    gGps.pos_valid = true;
                    /* Time Mode detection: a timing receiver in time-only mode
                     * keeps a valid (frozen) position but stops optimising it,
                     * so HDOP is reported as ~99.99 (value >= 5000 = 50.00).
                     * A normal navigation fix with that HDOP would be unusable,
                     * so treat "valid position + absurd HDOP" as Time Mode and
                     * show HDOP:TIME instead of a meaningless number.        */
                    gGps.time_mode = (gGps.hdop >= 5000);
                    last_pos_valid_ms = millis();   /* genuine fix — reset position timer */
                    had_pos = true;
                } else if (location_fresh) {
                    /* GGA/RMC received but no valid fix: update sats/HDOP so the
                     * display shows the actual current satellite count (may be 0)
                     * rather than a stale cached value from before antenna loss. */
                    gGps.sats = (uint8_t)gps.satellites.value();
                    gGps.hdop = gps.hdop.value();
                }
                if (date_fresh) {
                    gGps.day   = gps.date.day();
                    gGps.month = gps.date.month();
                    gGps.year  = (uint16_t)gps.date.year();
                }
                xSemaphoreGive(xGpsMutex);
            }

            /*
             * Notify DisplayTask at most once per second.
             * Without this guard, multiple NMEA sentences decoded in one
             * 8ms burst (GGA, RMC, GSA all arrive in rapid succession)
             * would each trigger a display update, causing 3-5 redraws
             * per second and flooding the BT serial port.
             *
             * Gate: notify only when the GPS second counter has advanced.
             * time_fresh is set by TinyGPS++ for every sentence that carries
             * time — so limit to one notify per unique second value.
             */
            if (time_fresh && xDisplayTask != NULL) {
                static uint8_t last_notified_sec = 0xFF;
                uint8_t cur_sec = gps.time.second();
                if (cur_sec != last_notified_sec) {
                    last_notified_sec = cur_sec;
                    xTaskNotifyGive(xDisplayTask);
                }
            }
        }

        /* ---- Timeout 1: NMEA stream gone (3 s) --------------------------------
         * No bytes at all — module powered off, UART broken, etc.
         * Clear everything: time, position, satellite count.
         * -------------------------------------------------------------------*/
        if (had_fix && (millis() - last_sentence_ms) > NMEA_TIMEOUT_MS) {
            if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                gGps.valid     = false;
                gGps.pos_valid = false;
                gGps.sats      = 0;
                xSemaphoreGive(xGpsMutex);
            }
            if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                gFreq.flush_requested = true;
                xSemaphoreGive(xFreqMutex);
            }
            had_fix = false;
            had_pos = false;
        }

        /* ---- Timeout 2: position fix lost (10 s) ---------------------------
         * NMEA is still arriving (time sentences from internal RTC) but no
         * valid location fix — antenna disconnected or signal blocked.
         * Clear pos_valid and zero position data so displays reflect reality.
         * gGps.valid (time) is left intact: the time itself is still valid
         * from the module's internal clock; only the position is stale.
         * -------------------------------------------------------------------*/
        if (had_pos && (millis() - last_pos_valid_ms) > POS_LOST_TIMEOUT_MS) {
            if (xSemaphoreTake(xGpsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                gGps.pos_valid = false;
                gGps.time_mode = false;
                gGps.lat       = 0.0f;
                gGps.lon       = 0.0f;
                gGps.alt       = 0.0f;
                gGps.sats      = 0;
                gGps.hdop      = 9999;   /* 99.99 — indicates no fix */
                xSemaphoreGive(xGpsMutex);
            }
            if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                gFreq.flush_requested = true;
                xSemaphoreGive(xFreqMutex);
            }
            had_pos = false;
        }

#ifdef GPSDO_GPS_TIMING
        /* Survey-in progress monitor (scheduler is running now, so this is
         * the safe place to poll — never in gpsdo_gps_init()). Throttle to
         * ~1 Hz so it does not interfere with NMEA draining. */
        if (g_svin_pending) {
            static uint32_t next_svin_poll = 0;
            if (millis() >= next_svin_poll) {
                next_svin_poll = millis() + 1000u;
                ubx_poll_svin_step();
            }
        }
#endif

        /* Yield between bursts — allows higher-priority tasks to run */
        vTaskDelay(pdMS_TO_TICKS(GPS_YIELD_MS));
    }
}

/*
 * gpsdo_gps_init — robust GPS hardware initialisation
 *
 * Problems fixed vs previous version:
 *
 * 1. BLIND BAUD-RATE ASSUMPTION
 *    Original always opened at 9600 and sent PUBX,41 to switch to 38400.
 *    After an STM32 reset (without GPS power cycle) the module is already
 *    at 38400 — PUBX sent at 9600 is ignored, ubx_config() loops forever.
 *    Fix: auto-detect the current baud rate before sending any config.
 *
 * 2. UNVERIFIED BAUD-RATE SWITCH (PUBX,41)
 *    PUBX,41 has no UBX-ACK reply — the module switches baud mid-sentence.
 *    Original waited 100ms blindly, which could land in the middle of a
 *    last NMEA sentence still draining at 9600 baud.
 *    Fix: flush TX completely, then wait 150ms (> max NMEA sentence time
 *    at 9600 = 82ms for longest sentence) before reopening at 38400,
 *    then flush the RX buffer to discard framing garbage.
 *
 * 3. taskYIELD() IN get_ubx_ack() BEFORE SCHEDULER
 *    Called from setup() before vTaskStartScheduler() → taskYIELD() is
 *    either a no-op or causes undefined behaviour on STM32duino port.
 *    Fix: get_ubx_ack() now detects scheduler state and uses delay(1)
 *    before scheduler, vTaskDelay(1) after.
 *
 * 4. LONG BLOCKING delay(3000) × 2 IN setup()
 *    Fix: first delay reduced to 500ms (enough for module cold-start
 *    to emit first NMEA); second delay eliminated — ubx_config() waits
 *    for ACK itself with a proper timeout.
 *
 * Auto-detect strategy:
 *   Try each candidate baud rate in descending order. At each rate,
 *   send a UBX-CFG-PRT poll request (B5 62 06 00 01 00 01 ...) and
 *   wait up to 300ms for any UBX or NMEA response. First rate that
 *   gets a response wins. Falls back to 9600 if nothing responds.
 *
 * Called from setup() before vTaskStartScheduler().
 */

/* Candidate baud rates to probe, in preference order */
static const uint32_t BAUD_CANDIDATES[] = { 38400, 9600, 57600, 115200 };
static const uint8_t  N_BAUD_CANDIDATES = sizeof(BAUD_CANDIDATES)/sizeof(BAUD_CANDIDATES[0]);

/* UBX-CFG-PRT poll (port 1 = UART1) — asks module to reply with its
 * current port config. Any valid UBX or NMEA response means the baud
 * rate is correct. */
static const uint8_t UBX_POLL_PRT[] = {
    0xB5, 0x62, 0x06, 0x00, 0x01, 0x00, 0x01, 0x08, 0x22
};
/*   B5 62  — header
 *   06 00  — class CFG, id PRT
 *   01 00  — length = 1
 *   01     — port ID = 1 (UART1)
 *   08 22  — checksum (CK_A=0x08, CK_B=0x22)            */

static uint32_t detect_gps_baud(void)
{
    for (uint8_t i = 0; i < N_BAUD_CANDIDATES; i++) {
        uint32_t baud = BAUD_CANDIDATES[i];
        Serial1.end();
        Serial1.begin(baud);
        delay(50);
        /* Flush any junk */
        while (Serial1.available()) Serial1.read();

        /* Send poll */
        for (uint8_t b = 0; b < sizeof(UBX_POLL_PRT); b++)
            Serial1.write(UBX_POLL_PRT[b]);
        Serial1.flush();

        /* Wait up to 350ms for any response byte */
        uint32_t t0 = millis();
        while ((millis() - t0) < 350) {
            if (Serial1.available()) {
                OUT_SERIAL.print("GPS detected at "); OUT_SERIAL.println(baud);
                /* Flush partial response before returning */
                delay(10);
                while (Serial1.available()) Serial1.read();
                return baud;
            }
            delay(1);
        }
        OUT_SERIAL.print("No GPS response at "); OUT_SERIAL.println(baud);
    }
    /* Nothing responded — default to 9600 */
    OUT_SERIAL.println("GPS baud detect failed, defaulting to 9600");
    return 9600;
}

/*
 * gpsdo_gps_init — robust GPS hardware initialisation for budget modules
 *
 * Strategy that works reliably on GY-NEO6MV2 / NEO-6M (AliExpress) as
 * well as genuine u-blox evaluation modules:
 *
 * 1. detect_gps_baud() probes all candidate baud rates and returns the
 *    one the module is currently using.
 *
 * 2. ubx_config() is sent AT THE DETECTED BAUD RATE.
 *    Root cause of the "3 attempts all fail" hang: the code previously
 *    switched to 38400 via PUBX,41 then sent UBX at 38400 — but the
 *    NEO-6M on GY-NEO6MV2 defaults to 9600 on power-up (the PUBX,41
 *    may not take effect reliably in every flash variant). The module
 *    received UBX at a baud rate it wasn't listening on → no ACK → hang.
 *    Sending UBX at the detected baud rate avoids this entirely.
 *    GPSDO operation does not require 38400 baud; NMEA at 1 Hz is well
 *    within the 9600 capacity.
 *
 * 3. Optional upgrade to 38400 (GPSDO_PREFER_38400):
 *    Enabled by default. After a successful ubx_config() at the detected
 *    baud, we try to switch to 38400 to reduce ACK latency for any
 *    future UBX polls. This is purely optional and non-fatal.
 *
 * 4. If detect_gps_baud() returns 0 (no response), we default to 9600
 *    and proceed. NMEA may still arrive even if UBX-CFG-PRT poll failed.
 */

/* Enable an opportunistic upgrade to 38400 after successful UBX config.
 * Comment this out to stay at the detected baud rate always. */
#define GPSDO_PREFER_38400


void gpsdo_gps_init(void)
{
    OUT_SERIAL.println("GPS init: probing baud rate...");

#ifdef GPSDO_UBX_CONFIG
    /* ---- Step 1: detect current baud rate ---- */
    uint32_t working_baud = detect_gps_baud();
    if (working_baud == 0) {
        /* No response at any baud — assume 9600 and continue.
         * Some modules need extra settle time after power-on;
         * NMEA may still arrive and GPSDO will work without UBX config. */
        OUT_SERIAL.println("GPS: no response to baud probe, defaulting to 9600");
        working_baud = 9600;
        Serial1.end();
        Serial1.begin(9600);
        flush_rx(200);
        OUT_SERIAL.println("GPS init done (no UBX config applied)");
        return;
    }

    /* Ensure Serial1 is open at the confirmed working baud */
    Serial1.end();
    Serial1.begin(working_baud);
    flush_rx(150);   /* drain NMEA that arrived during port reopen */

    /* ---- Step 2: silence noisy NMEA sentences ----
     *
     * Root cause of the CFG-NAV5 ACK loss: at 38400 baud NMEA output
     * fills the 256-byte STM32 RX ring buffer in ~67 ms. The 10-byte
     * ACK response for CFG-NAV5 arrives ~100 ms after the command and
     * can be overwritten / dropped before get_ubx_ack() reads it.
     *
     * ubx_reduce_nmea() disables GLL, GSA, GSV, VTG (keeps GGA + RMC).
     * Residual NMEA rate drops from ~500 B/s to ~150 B/s.
     * At 150 B/s the 256-byte buffer holds 1.7 s — safe for a 1500 ms
     * ACK wait with no overflow risk.
     *
     * CFG-MSG commands for each disabled sentence receive their own ACK,
     * which also verifies that UBX write commands work on this firmware.
     */
    OUT_SERIAL.print("GPS: disabling noisy NMEA at "); OUT_SERIAL.print(working_baud); OUT_SERIAL.println(" baud");
    uint8_t nmea_ok = ubx_reduce_nmea();
    OUT_SERIAL.print("GPS: "); OUT_SERIAL.print(nmea_ok); OUT_SERIAL.println("/4 NMEA sentences disabled");

    /* ---- Step 3: send UBX-CFG-NAV5 at working baud ---- */
    OUT_SERIAL.print("GPS: sending UBX config at "); OUT_SERIAL.print(working_baud); OUT_SERIAL.println(" baud");
    bool cfg_ok = ubx_config();

#ifdef GPSDO_GPS_TIMING
    /* LEA-6T/M8T: replace plain stationary mode with survey-in → Time Mode.
     * Survey-in can be disabled at runtime (CLI "SV 0", stored in EEPROM) —
     * handy for bench testing, where you may want plain nav mode.
     * IMPORTANT: only SEND the survey-in start here. The progress-polling
     * loop must NOT run before the scheduler — it uses vTaskDelay(), which
     * hangs the system if called before vTaskStartScheduler(). Monitoring
     * happens in vGpsTask once the scheduler is running. */
    if (!g_svin_enabled) {
        OUT_SERIAL.println("LEA-T: survey-in DISABLED (SV 0) — staying in nav mode");
    } else {
        OUT_SERIAL.println("LEA-T: starting survey-in (Time Mode)");
        if (ubx_start_survey_in()) {
            OUT_SERIAL.println("LEA-T: survey-in command accepted");
        } else {
            /* No variant ACKed. The module is usually ALREADY in Time Mode
             * (its survey config was stored in flash, e.g. via u-center), so
             * a fresh command is rejected — but it is still timing. Monitor
             * anyway: TIM-SVIN polling reports the stored survey's progress;
             * if the module is merely in nav mode the poll finds nothing and
             * the monitor stops itself. */
            OUT_SERIAL.println("LEA-T: no start command ACKed — module may already be timing");
            OUT_SERIAL.println("LEA-T: monitoring via TIM-SVIN");
        }
        /* Ask vGpsTask to poll TIM-SVIN once the scheduler runs. */
        g_svin_active = true;
        g_svin_pending = true;
        g_svin_dur = 0; g_svin_acc_m = 0;
    }
#endif

    /* ---- Step 3: optional upgrade to 38400 ---- */
#ifdef GPSDO_PREFER_38400
    if (working_baud != 38400) {
        OUT_SERIAL.println("GPS: attempting upgrade to 38400 baud...");

        /* Suppress VTG to reduce traffic during switch */
        Serial1.print("$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n");
        Serial1.flush();
        delay(50);
        flush_rx(50);

        /* PUBX,41: request switch to 38400
         * Checksum *24 verified correct for this exact string via XOR:
         * PUBX,41,1,0003,0003,38400,0 → XOR = 0x24 */
        Serial1.print("$PUBX,41,1,0003,0003,38400,0*24\r\n");
        Serial1.flush();
        delay(200);    /* > longest NMEA sentence at 9600 baud (~82 ms) */

        /* Reopen and probe to verify the switch succeeded */
        Serial1.end();
        Serial1.begin(38400);
        flush_rx(100);

        /* Verify: send a UBX-CFG-PRT poll and check for any response */
        for (uint8_t b = 0; b < sizeof(UBX_POLL_PRT); b++)
            Serial1.write(UBX_POLL_PRT[b]);
        Serial1.flush();

        bool switched = false;
        uint32_t t0 = millis();
        while ((millis() - t0) < 400) {
            if (Serial1.available()) { switched = true; break; }
            delay(5);
        }
        flush_rx(50);

        if (switched) {
            OUT_SERIAL.println("GPS: now at 38400 baud");
            /* If ubx_config failed at the original baud, retry at 38400 */
            if (!cfg_ok) {
                OUT_SERIAL.println("GPS: retrying UBX config at 38400");
                ubx_config();
            }
        } else {
            /* Switch did not take — fall back to working_baud */
            OUT_SERIAL.println("GPS: baud switch failed, reverting");
            Serial1.end();
            Serial1.begin(working_baud);
            flush_rx(100);
        }
    }
#endif /* GPSDO_PREFER_38400 */

#else /* GPSDO_UBX_CONFIG not defined */
    uint32_t working_baud = detect_gps_baud();
    if (working_baud == 0) working_baud = 9600;
    Serial1.end();
    Serial1.begin(working_baud);
    flush_rx(100);
    OUT_SERIAL.print("GPS: running at "); OUT_SERIAL.print(working_baud); OUT_SERIAL.println(" baud (no UBX config)");
#endif /* GPSDO_UBX_CONFIG */

    OUT_SERIAL.println("GPS init done");
}
