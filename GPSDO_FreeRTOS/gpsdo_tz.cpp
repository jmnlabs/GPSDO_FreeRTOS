/**
 * gpsdo_tz.cpp — Timezone resolution: POSIX TZ rules, named zones, EU auto
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 *
 * See gpsdo_tz.h for why this exists and how the three modes relate.
 */

#include "gpsdo_tz.h"
#include "gpsdo_tz_table.h"
#include "gpsdo_config.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Live config — defined here, declared extern where needed. */
uint8_t g_tz_mode        = TZ_MODE_AUTO_EU;
int16_t g_tz_manual_min  = 120;                 /* TO <n[:mm]> value  */
TzSpec  g_tz_spec;                              /* TZ_MODE_POSIX rule */
char    g_tz_str[TZ_STR_MAX] = "";              /* as typed, for ES/readback */

/* ======================================================================
 * POSIX TZ parser
 * ====================================================================== */

/* Zone abbreviations are either letters ("CET") or a quoted numeric form
 * ("<+0530>"). Both just get skipped — we keep the offsets, not the names,
 * since nothing on the display shows them. */
static const char *skip_name(const char *p)
{
    if (*p == '<') {
        while (*p && *p != '>') p++;
        return *p ? p + 1 : p;
    }
    while (*p && isalpha((unsigned char)*p)) p++;
    return p;
}

/* Read [+|-]hh[:mm[:ss]] and return MINUTES EAST of UTC.
 * POSIX writes the offset as "how far behind UTC", so its sign is the
 * opposite of the everyday convention: "EST5" is UTC-5, "CET-1" is UTC+1.
 * Negating here means every caller outside this file sees normal signs.
 * Seconds are accepted and discarded — no civil zone uses them today. */
static const char *parse_off(const char *p, int16_t *out)
{
    int sign = 1;
    if      (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }

    int h = 0, m = 0;
    while (*p >= '0' && *p <= '9') h = h * 10 + (*p++ - '0');
    if (*p == ':') {
        p++;
        while (*p >= '0' && *p <= '9') m = m * 10 + (*p++ - '0');
        if (*p == ':') {                 /* seconds — skip */
            p++;
            while (*p >= '0' && *p <= '9') p++;
        }
    }
    *out = (int16_t)(-sign * (h * 60 + m));
    return p;
}

/* Read one transition rule. Only the "Mm.w.d" form is supported; the
 * Julian forms (Jn / n) appear in exactly one place in current tzdata —
 * Morocco, whose DST follows Ramadan and so cannot be written as a fixed
 * date at all. Returning 0 lets the caller degrade the zone cleanly to its
 * standard offset instead of inventing a wrong transition. */
static int parse_rule(const char **pp, TzRule *r)
{
    const char *p = *pp;
    r->at_min = 120;      /* POSIX default: 02:00 local */
    r->mon    = 0;

    if (*p != 'M') {
        while (*p && *p != ',') p++;
        *pp = p;
        return 0;
    }
    p++;
    r->mon  = (int8_t)strtol(p, (char **)&p, 10);  if (*p == '.') p++;
    r->week = (int8_t)strtol(p, (char **)&p, 10);  if (*p == '.') p++;
    r->dow  = (int8_t)strtol(p, (char **)&p, 10);

    if (*p == '/') {
        p++;
        int h = (int)strtol(p, (char **)&p, 10), m = 0;
        if (*p == ':') { p++; m = (int)strtol(p, (char **)&p, 10); }
        r->at_min = (int16_t)(h * 60 + m);
    }
    *pp = p;

    if (r->mon < 1 || r->mon > 12 || r->week < 1 || r->week > 5 ||
        r->dow < 0 || r->dow > 6) {
        r->mon = 0;
        return 0;
    }
    return 1;
}

int tz_parse(const char *s, TzSpec *tz)
{
    memset(tz, 0, sizeof(*tz));
    if (!s || !*s) return 0;

    const char *p = skip_name(s);
    if (p == s) return 0;                 /* no zone name → not a TZ string */

    p = parse_off(p, &tz->std_min);
    if (tz->std_min < -720 || tz->std_min > 840) return 0;   /* -12:00..+14:00 */
    tz->dst_min = tz->std_min;

    const char *q = skip_name(p);
    if (q == p) return 1;                 /* no DST name → fixed offset, done */

    tz->has_dst = 1;
    p = q;
    if (*p && *p != ',') p = parse_off(p, &tz->dst_min);
    else                 tz->dst_min = (int16_t)(tz->std_min + 60);  /* POSIX default */

    if (*p != ',') { tz->has_dst = 0; return -1; }   /* DST named, no rules */
    p++;
    int ok_start = parse_rule(&p, &tz->start);
    int ok_end   = 0;
    if (*p == ',') { p++; ok_end = parse_rule(&p, &tz->end); }

    if (!ok_start || !ok_end) { tz->has_dst = 0; return -1; }
    return 1;
}

/* ======================================================================
 * DST resolution
 * ====================================================================== */

/* Zeller — Sunday = 0. (The display task has its own copy; this one keeps
 * the module self-contained.) */
static uint8_t tz_dow(uint8_t d, uint8_t m, uint16_t y)
{
    if (m < 3) { m += 12; y--; }
    return (uint8_t)((d + 13 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400 + 6) % 7);
}

/* Day of month a rule lands on in a given year. */
static uint8_t rule_day(const TzRule *r, uint16_t y)
{
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t len = dim[r->mon - 1];
    if (r->mon == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) len = 29;

    if (r->week >= 5) {                       /* last such weekday */
        uint8_t last = tz_dow(len, r->mon, y);
        return (uint8_t)(len - ((last - r->dow + 7) % 7));
    }
    uint8_t first = tz_dow(1, r->mon, y);
    int day = 1 + ((r->dow - first + 7) % 7) + (r->week - 1) * 7;
    return (uint8_t)(day > len ? day - 7 : day);   /* "5th Sunday" that isn't */
}

/* Days elapsed in the year before this date, 0-based: 1 January = 0. Gives
 * every instant of the year one ordinal, which is what lets a transition be
 * shifted across midnight below. */
static uint16_t tz_yday(uint8_t d, uint8_t m, uint16_t y)
{
    static const uint16_t cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    uint16_t n = (uint16_t)(cum[m - 1] + (uint16_t)(d - 1));
    if (m > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) n++;
    return n;
}

int16_t tz_offset_now(const TzSpec *tz, uint8_t day, uint8_t mon,
                      uint16_t year, uint16_t min_utc)
{
    if (!tz->has_dst || !tz->start.mon || !tz->end.mon) return tz->std_min;

    /* THE RULE TIMES ARE LOCAL AND THIS FUNCTION IS GIVEN UTC.
     *
     * The previous version compared them directly and called the difference
     * immaterial for a wall-clock readout. It is not, and the error is not the
     * "hour or so" the old comment claimed: it is exactly the offset in force
     * at the boundary, which is the whole point of the boundary. Measured
     * against the real 2026 instants, before this change:
     *
     *   Europe/London   spring exact, autumn  +1 h late
     *   Europe/Berlin   spring +1 h late,     autumn +2 h late
     *   Europe/Athens   spring +2 h late,     autumn +3 h late
     *   America/New_York  spring FIVE HOURS EARLY — 02:00 UTC instead of
     *                     07:00, so the clock jumps at 21:00 on the Saturday
     *                     EVENING, which nobody could mistake for a rounding
     *                     error
     *
     * London's spring change came out exact only because GMT is UTC; that
     * coincidence is what let the fault sit here unnoticed. Found by Dave
     * (Solder_Junkie) on EEVblog asking whether London would switch by itself
     * at the end of October — it did, an hour late, and the question was
     * enough to make somebody finally measure it.
     *
     * There is no chicken-and-egg to avoid, which is what the old comment
     * feared. A POSIX start rule is written in local STANDARD time and an end
     * rule in local DST time — the offset each is measured against is fixed
     * and known before the comparison, so each boundary converts to UTC by
     * subtracting its own offset. No iteration, no guess.
     *
     * The subtraction can cross midnight and the year end (Australia/Sydney
     * starts at 02:00 AEST, which is 16:00 UTC on the PREVIOUS day), so the
     * comparison runs on a day-of-year ordinal rather than a packed date, and
     * the boundaries wrap into the year. Residual, stated rather than hidden:
     * a boundary that wraps past 1 January keeps the weekday computed for THIS
     * year, which could be a day out. No zone in the IANA table transitions
     * within fourteen hours of New Year, so the branch is a guard and not a
     * behaviour. */
    bool leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    long ylen = (leap ? 366L : 365L) * 1440L;

    long now = (long)tz_yday(day, mon, year) * 1440L + (long)min_utc;

    long st = (long)tz_yday(rule_day(&tz->start, year), tz->start.mon, year) * 1440L
              + (long)tz->start.at_min - (long)tz->std_min;
    long en = (long)tz_yday(rule_day(&tz->end,   year), tz->end.mon,   year) * 1440L
              + (long)tz->end.at_min   - (long)tz->dst_min;

    while (st <  0)    st += ylen;
    while (st >= ylen) st -= ylen;
    while (en <  0)    en += ylen;
    while (en >= ylen) en -= ylen;

    /* st < en: northern hemisphere, DST is a window inside the year.
     * st > en: southern, DST wraps through New Year — invert the test.
     * That single else is the whole of southern-hemisphere support. */
    bool on = (st < en) ? (now >= st && now < en)
                        : (now >= st || now < en);
    return on ? tz->dst_min : tz->std_min;
}

/* ======================================================================
 * Zone-name lookup
 * ====================================================================== */

static int ci_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int d = tolower((unsigned char)*a++) - tolower((unsigned char)*b++);
        if (d) return d;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

const char *tz_lookup(const char *name)
{
    if (!name || !*name) return NULL;

    /* "Australia/Adelaide" → city "Adelaide", region "Australia".
     * City names are unique across the whole database (verified against
     * tzdata when generating the table), so the region is only ever used
     * to reject a mismatch, never to disambiguate. */
    const char *slash = strchr(name, '/');
    const char *city  = slash ? slash + 1 : name;

    int lo = 0, hi = TZ_NZONES - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = ci_cmp(&tz_city_blob[tz_city_off[mid]], city);
        if (c == 0) {
            if (slash) {
                char reg[20];
                size_t n = (size_t)(slash - name);
                if (n >= sizeof(reg)) return NULL;
                memcpy(reg, name, n);
                reg[n] = '\0';
                if (ci_cmp(tz_region_str[tz_zone_region[mid]], reg) != 0)
                    return NULL;
            }
            return tz_rule_str[tz_zone_rule[mid]];
        }
        if (c < 0) lo = mid + 1;
        else       hi = mid - 1;
    }
    return NULL;
}

/* ======================================================================
 * Mode dispatch
 * ====================================================================== */

/* Legacy heuristic, unchanged in behaviour — see gpsdo_control.cpp for the
 * zone rules it encodes. Returns whole hours; tz_resolve scales to minutes.
 *
 * int8 range note (audit v0.95): the return type is int8_t (-128..127),
 * which overflows for UTC+14 (Kiribati). This is safe in practice because
 * the EU heuristic only ever classifies longitude bands inside UTC-12..+14
 * and every real offset it returns lies in [-12, +14] hours — but the type
 * would silently wrap if the heuristic were ever extended eastward. Kept
 * as int8_t for binary compatibility with gpsdo_control.cpp; the ×60 scale
 * in tz_resolve cannot overflow because int16_t covers ±840 min. */
extern int8_t tz_auto_offset_eu(float lat, float lon, uint8_t day,
                                uint8_t mon, uint16_t year, uint8_t hour_utc);

int16_t tz_resolve(float lat, float lon, uint8_t day, uint8_t mon,
                   uint16_t year, uint8_t hour_utc, uint8_t min_utc)
{
    switch (g_tz_mode) {
    case TZ_MODE_POSIX:
        return tz_offset_now(&g_tz_spec, day, mon, year,
                             (uint16_t)(hour_utc * 60 + min_utc));
    case TZ_MODE_AUTO_EU:
        return (int16_t)(tz_auto_offset_eu(lat, lon, day, mon, year, hour_utc) * 60);
    case TZ_MODE_MANUAL:
    default:
        return g_tz_manual_min;
    }
}

int tz_set_posix(const char *name_or_rule)
{
    if (!name_or_rule || !*name_or_rule) return 0;

    /* A zone name or a rule? A POSIX rule always carries a digit (the
     * offset) and a zone name never does, so that alone separates them —
     * no need to guess from punctuation. */
    const char *rule = name_or_rule;
    bool looks_like_rule = false;
    for (const char *p = name_or_rule; *p; p++) {
        if (*p >= '0' && *p <= '9') { looks_like_rule = true; break; }
    }
    if (!looks_like_rule) {
        rule = tz_lookup(name_or_rule);
        if (!rule) return 0;
    }

    TzSpec tmp;
    int r = tz_parse(rule, &tmp);
    if (r == 0) return 0;

    g_tz_spec = tmp;
    g_tz_mode = TZ_MODE_POSIX;
    strncpy(g_tz_str, rule, TZ_STR_MAX - 1);
    g_tz_str[TZ_STR_MAX - 1] = '\0';
    return r;                     /* 1 = full, -1 = DST rule unsupported */
}
