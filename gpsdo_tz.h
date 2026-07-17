/**
 * gpsdo_tz.h — Timezone resolution: POSIX TZ rules, named zones, EU auto
 *
 * Part of GPSDO FreeRTOS v0.95
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 *
 * ======================================================================
 * Three ways to get from UTC to local time, one result.
 *
 * The display asks for a single number: how many MINUTES to add to UTC.
 * Minutes, not hours — India is +5:30, Nepal +5:45, Chatham +12:45. An
 * hours-only offset silently misses those by up to 45 minutes.
 *
 * Where that number comes from is g_tz_mode:
 *
 *   TZ_MODE_MANUAL   a fixed offset the user typed (TO 9:30). No DST.
 *   TZ_MODE_AUTO_EU  the legacy heuristic: guess the zone from GPS
 *                    position, apply the EU DST rule. Right across most
 *                    of Europe, silently wrong everywhere else — it
 *                    knows nothing of DST outside the EU and cannot
 *                    express half-hour zones.
 *   TZ_MODE_POSIX    a POSIX TZ rule, either looked up from a zone name
 *                    or typed out in full. Handles half-hours, both
 *                    hemispheres, any DST rule the POSIX format can
 *                    express.
 *
 * Every command sets the mode, so there is never a half-state where one
 * mechanism has been configured and another is quietly overriding it.
 *
 * WHY POSIX TZ RATHER THAN A ZONE DATABASE
 * The full IANA database is ~2 MB — four times this MCU's entire flash —
 * and its real value is being updated several times a year as governments
 * change the rules. A GPSDO has no internet to update from. But the POSIX
 * TZ string that every zone reduces to is 4-44 bytes and encodes the same
 * present-day behaviour:
 *
 *   ACST-9:30ACDT,M10.1.0,M4.1.0/3   Adelaide: +9:30, DST Oct..Apr
 *   CET-1CEST,M3.5.0,M10.5.0/3       Warsaw:   +1,    DST Mar..Oct
 *   IST-5:30                         Kolkata:  +5:30, no DST
 *
 * Southern-hemisphere DST needs no special handling: Adelaide's rule
 * starts in month 10 and ends in month 4, so start > end, and the
 * "active" test simply inverts. See tz_offset_now().
 *
 * Note the POSIX sign convention is inverted from the everyday one:
 * "-9:30" in the string means UTC+9:30. The parser normalises this, so
 * everything outside gpsdo_tz.cpp deals in ordinary signed minutes.
 * ====================================================================== */
#ifndef GPSDO_TZ_H
#define GPSDO_TZ_H

#include <stdint.h>
#include <stdbool.h>

/* Longest real POSIX TZ string is 44 chars (Pacific/Chatham); 47 + NUL
 * leaves room without being generous enough to matter. */
#define TZ_STR_MAX  48

enum {
    TZ_MODE_MANUAL  = 0,
    TZ_MODE_AUTO_EU = 1,
    TZ_MODE_POSIX   = 2,
};

/* One DST transition: "the Nth WEEKth DAY of MONTH, at AT_MIN local". */
typedef struct {
    int8_t  mon;      /* 1..12; 0 = no rule                        */
    int8_t  week;     /* 1..4 = Nth, 5 = last                      */
    int8_t  dow;      /* 0 = Sunday .. 6 = Saturday                */
    int16_t at_min;   /* minutes from midnight (POSIX default 120) */
} TzRule;

typedef struct {
    int16_t std_min;  /* standard offset from UTC, minutes         */
    int16_t dst_min;  /* summer offset from UTC, minutes           */
    TzRule  start, end;
    uint8_t has_dst;  /* 0 when the zone has no DST, or its rule
                       * could not be parsed (see tz_parse)        */
} TzSpec;

/* Parse a POSIX TZ string.
 * Returns  1  parsed, DST rules understood (or the zone has no DST)
 *          0  syntax error — *tz is not usable
 *         -1  offsets parsed but the DST rule is not expressible in the
 *             subset we support (Morocco's Ramadan-dependent rule is the
 *             only real case). has_dst is cleared: the zone degrades to
 *             its standard offset rather than being silently wrong. */
int  tz_parse(const char *s, TzSpec *tz);

/* Minutes to add to UTC under this spec, for the given UTC date/time. */
int16_t tz_offset_now(const TzSpec *tz, uint8_t day, uint8_t mon,
                      uint16_t year, uint16_t min_utc);

/* Look a zone name up in the built-in table. Accepts "Adelaide" or
 * "Australia/Adelaide", case-insensitively — city names are unique across
 * the whole IANA database, so the region is optional.
 * Returns the POSIX rule string, or NULL if not found. */
const char *tz_lookup(const char *name);

/* Resolve the current offset from whatever mode is configured. Called on
 * each GPS fix; lat/lon are only used by TZ_MODE_AUTO_EU. */
int16_t tz_resolve(float lat, float lon, uint8_t day, uint8_t mon,
                   uint16_t year, uint8_t hour_utc, uint8_t min_utc);

/* Apply a POSIX string or zone name to the live config. Returns the same
 * codes as tz_parse; on success g_tz_mode becomes TZ_MODE_POSIX and the
 * string is stored for EEPROM and for readback. */
int  tz_set_posix(const char *name_or_rule);

/* Live config, defined in gpsdo_tz.cpp. */
extern uint8_t g_tz_mode;
extern int16_t g_tz_manual_min;
extern TzSpec  g_tz_spec;
extern char    g_tz_str[TZ_STR_MAX];

#endif /* GPSDO_TZ_H */
