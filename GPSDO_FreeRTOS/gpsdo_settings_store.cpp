/* ======================================================================
 * gpsdo_settings_store.cpp  —  persistent user settings via the flash ring
 *
 * Part of GPSDO v1.07.57rt
 *
 * See gpsdo_settings_store.h for the design. This module snapshots the runtime
 * settings globals into a flat SettingsBlock_t and stores it as a REC_SETTINGS
 * slot in the flash ring. On boot, settings_recall() reads the newest such
 * slot, validates its version and per-field ranges, and applies the values.
 *
 * Atomicity, wear-levelling and power-loss safety all come from the ring
 * (per-slot CRC16, sequence-numbered newest-wins, read-back verify) — this
 * module is pure serialisation.
 * ====================================================================== */

#include "gpsdo_settings_store.h"
#include "gpsdo_dac.h"
#include "gpsdo_backlight.h"
#include "gpsdo_flash_ring.h"
#include "gpsdo_state.h"
#include <stddef.h>   /* offsetof, for the v4 migration */
#include "gpsdo_algorithms.h"
#include "gpsdo_tz.h"
#include "gpsdo_config.h"
#include "gpsdo_ubx_timtp.h"      /* g_qerr_enable (SAW master switch) */
#include "gpsdo_span.h"           /* the span record (PB14)            */
#include <math.h>
#include <string.h>

/* Sensor offsets live in gpsdo_control.cpp and have no header of their own —
 * every translation unit that touches them declares them locally, which is the
 * pattern the rest of this project uses (see gpsdo_cli.cpp / gpsdo_tasks.cpp).
 * Without these two lines this file does not compile: the settings block stores
 * both offsets, so it has to see them. */
extern float g_pressure_offset;
extern float g_altitude_offset;
/* LT flag, defined in gpsdo_control.cpp; same local-extern pattern. */
extern bool  g_show_local_time;

/* LRN_DAMP_LO / LRN_DAMP_HI come from gpsdo_algorithms.h. */

/* ---- field groups shared by more than one save ----
 *
 * Each is written from ONE place so that a group saved by two different
 * partials cannot drift apart: SET_PID and SET_SPAN both carry the PID group,
 * SET_LTIC and SET_SPAN both carry the LTIC loop gains. */

static void put_pid_group(SettingsBlock_t *s)
{
    for (int k = 3; k <= 9; k++) {
        int i = k - 3;
        s->pid_kp[i] = (float)g_pid[k].Kp;
        s->pid_ki[i] = (float)g_pid[k].Ki;
        s->pid_kd[i] = (float)g_pid[k].Kd;
        s->pid_il[i] = (float)g_pid[k].I_LIMIT;
    }
    s->blend_cross = (float)g_blend_crossover;
    s->blend_scale = (float)g_blend_scale;
    s->nn_max_step = (float)g_nn_max_step;
}

/* The twelve numbers ltic_autotune() derives from K. Not the detector
 * calibration (LC's), not the thresholds, not the state. */
static void put_ltic_gains(SettingsBlock_t *s)
{
    s->ltic_dpll_kp = (float)g_ltic.dpll.Kp;
    s->ltic_dpll_ki = (float)g_ltic.dpll.Ki;
    s->ltic_dpll_kd = (float)g_ltic.dpll.Kd;
    s->ltic_dpll_il = (float)g_ltic.dpll.I_LIMIT;
    s->ltic_lock_kp = (float)g_ltic.lock.Kp;
    s->ltic_lock_ki = (float)g_ltic.lock.Ki;
    s->ltic_lock_kd = (float)g_ltic.lock.Kd;
    s->ltic_lock_il = (float)g_ltic.lock.I_LIMIT;
    s->ltic_acq_kp  = (float)g_ltic.acq.Kp;
    s->ltic_acq_ki  = (float)g_ltic.acq.Ki;
    s->ltic_acq_kd  = (float)g_ltic.acq.Kd;
    s->ltic_acq_il  = (float)g_ltic.acq.I_LIMIT;
}

static void put_span(SettingsBlock_t *s)
{
    span_store_t st;
    span_store_get(&st);
    s->span_last    = st.last;
    s->span_k[0]    = st.k[0];
    s->span_k[1]    = st.k[1];
    s->span_pair[0] = st.pair[0];
    s->span_pair[1] = st.pair[1];
}

/* ---- snapshot: runtime -> SettingsBlock_t ---- */

static void snapshot_full(SettingsBlock_t *s)
{
    memset(s, 0, sizeof(*s));
    s->ver = SETTINGS_VER;
    s->pwm  = gCtrl.pwm_output;
    s->algo = gCtrl.active_algo;
    s->dac_path   = (uint8_t)(g_dac_path + 1u);   /* 0 stays "unset" */
    s->vsense_src = (uint8_t)(g_vsense_src + 1u); /* likewise           */
    s->bl_pct   = g_tft_bl_pct;                 /* already clamped to 30..100 */
    /* ×100 encodings, never 0 from runtime — the CLI's range checks keep
     * vref in 2.50..5.50 V and the divider in 1.00..10.00, both well away
     * from the "unset" zero. */
    s->dac_vref_cv = (uint16_t)(g_dac_vref * 100.0 + 0.5);
    s->dac_vref_mv = (uint16_t)(g_dac_vref * 1000.0 + 0.5);   /* the same DV */
    s->adc_vdiv_h  = (uint16_t)(g_adc_vdiv * 100.0 + 0.5);
    s->a12_gain      = g_mlacc_gain;
    s->a12_run_level = g_mlacc_run_level;
    s->a12_thr_src   = g_mlacc_thr_src;
    s->a12_thr_tgt_s = g_mlacc_thr_tgt_s;
    for (int i = 0; i < 11; i++) s->a12_lim[i] = g_mlacc_lim[i];
    for (int n = 3; n <= 9; n++) {
        int i = n - 3;
        s->pid_kp[i] = (float)g_pid[n].Kp;
        s->pid_ki[i] = (float)g_pid[n].Ki;
        s->pid_kd[i] = (float)g_pid[n].Kd;
        s->pid_il[i] = (float)g_pid[n].I_LIMIT;
    }
    s->blend_cross = (float)g_blend_crossover;
    s->blend_scale = (float)g_blend_scale;
    s->nn_max_step = (float)g_nn_max_step;
    s->press_off   = g_pressure_offset;
    s->alt_off     = g_altitude_offset;
    s->svin_en     = g_svin_enabled ? 1u : 0u;
    s->ltic_nsv    = g_ltic.ns_per_volt;
    s->ltic_zero   = g_ltic.zero_offset;
    s->ltic_range  = g_ltic.range_ns;
    s->ltic_dpll_kp = (float)g_ltic.dpll.Kp;
    s->ltic_dpll_ki = (float)g_ltic.dpll.Ki;
    s->ltic_dpll_kd = (float)g_ltic.dpll.Kd;
    s->ltic_dpll_il = (float)g_ltic.dpll.I_LIMIT;
    s->ltic_lock_kp = (float)g_ltic.lock.Kp;
    s->ltic_lock_ki = (float)g_ltic.lock.Ki;
    s->ltic_lock_kd = (float)g_ltic.lock.Kd;
    s->ltic_lock_il = (float)g_ltic.lock.I_LIMIT;
    s->ltic_acq_th  = g_ltic.acq_threshold_ns;
    s->ltic_dpll_th = g_ltic.dpll_lock_thresh;
    s->ltic_lock_iv = g_ltic.lock_interval_s;
    s->ltic_state   = g_ltic.state;
    s->ltic_submode = g_ltic.submode;
    s->ltic_acq_kp  = (float)g_ltic.acq.Kp;
    s->ltic_acq_ki  = (float)g_ltic.acq.Ki;
    s->ltic_acq_kd  = (float)g_ltic.acq.Kd;
    s->ltic_acq_il  = (float)g_ltic.acq.I_LIMIT;
    s->ltic_polarity = g_ltic.polarity;
    s->ltic_centre_v = g_ltic.centre_v;
    /* LTIC-Lars (algo 11) */
    s->lars_gain     = g_lars.gain;
    s->lars_damping  = g_lars.damping;
    s->lars_tc       = g_lars.time_const_s;
    s->lars_fdiv     = g_lars.filter_div;
    s->lars_tic_off  = g_lars.tic_offset;
    s->lars_lock_lim = g_lars.lock_ns_lim;
    s->lars_lock_fac = g_lars.lock_factor;
    s->lars_tcoeff   = g_lars.temp_coeff;
    s->lars_tref     = g_lars.temp_ref;
    s->lars_flags    = g_lars.flags;
    s->fa_win_dpll   = g_freq_damp_win_dpll;
    s->fa_win_lock   = g_freq_damp_win_lock;
    s->lt_local      = g_show_local_time ? 1u : 0u;
    s->lrn_en    = g_lrn_enable ? 1u : 0u;
    s->lrn_drift = g_lrn_drift;
    s->lrn_damp  = g_lrn_damp;
    s->warmup_en = g_warmup_enable ? 1u : 0u;
    s->splash_en = g_splash_enable ? 1u : 0u;
    s->saw_en    = g_qerr_enable  ? 1u : 0u;
    s->tz_mode      = g_tz_mode;
    s->tz_manual_min = g_tz_manual_min;
    memcpy(s->tz_str, g_tz_str, sizeof(s->tz_str));
    s->tz_str[sizeof(s->tz_str) - 1] = '\0';
    put_span(s);
}

/* ---- apply: SettingsBlock_t -> runtime (with range guards) ---- */

static bool is_finite_pos(float v, float lo, float hi)
{
    return isfinite(v) && v >= lo && v <= hi;
}

static void apply_full(const SettingsBlock_t *s)
{
    /* PWM & algo (PWM applied later by the boot sequence after live_store) */
    if (s->pwm != 0) gCtrl.pwm_output = s->pwm;
    /* 13 is the highest algorithm; the bound has to move with every addition or
     * the setting saves correctly and is silently dropped on recall, which looks
     * like the flash ring failing rather than a stale constant here. The 29.08
     * build shipped with 13 in the dispatcher and 12 here, so LA 13 + ES ALGO
     * came back as algo 0 after reset - exactly this trap. */
    if (s->algo <= 13u) gCtrl.active_algo = s->algo;
    /* 0 = never written (old record, or a build before this field existed) and
     * means "use the default"; 1..3 are the paths, offset by one so that zero
     * can carry that meaning. Whatever comes out is resolved against what is
     * actually compiled in before it reaches the pin. */
    if (s->dac_path >= 1u && s->dac_path <= 3u)
        g_dac_path = gpsdo_dac_path_resolve((uint8_t)(s->dac_path - 1u));
    /* Through the setter rather than straight into the variable: it is the one
     * place the clamp lives, and on a board whose timer is already running it
     * also puts the value on the pin. 0 = older record, leave the default. */
    if (s->bl_pct >= BL_PCT_MIN && s->bl_pct <= BL_PCT_MAX)
        backlight_set(s->bl_pct);
    if (s->vsense_src >= 1u && s->vsense_src <= 2u)
        g_vsense_src = (uint8_t)(s->vsense_src - 1u);
    {
        /* Range checks live with the meaning, in span_store_put(). */
        span_store_t st;
        st.last    = s->span_last;
        st.k[0]    = s->span_k[0];
        st.k[1]    = s->span_k[1];
        st.pair[0] = s->span_pair[0];
        st.pair[1] = s->span_pair[1];
        span_store_put(&st);
    }
    /* 0 = unset (v5 record, or never configured): leave the compile-time
     * defaults in force. The ranges mirror the CLI's, with a little slack on
     * the upper ends so a hand-edited record is not silently crippled. */
    if (s->dac_vref_cv >= 250u && s->dac_vref_cv <= 550u) {
        g_dac_vref = (double)s->dac_vref_cv / 100.0;
        /* The millivolt field carries the third decimal (4.096, not 4.10) —
         * when it is there, and when it says the same DV to the centivolt.
         * Every build that writes it writes dac_vref_cv from the same value,
         * and the two roundings can differ by at most 5 mV, so a larger gap
         * means dac_vref_cv was written by something that did not know about
         * this field, and dac_vref_cv is the one to believe. 0 is an older
         * record, which the range check turns away. */
        int32_t d = (int32_t)s->dac_vref_mv - 10 * (int32_t)s->dac_vref_cv;
        if (s->dac_vref_mv >= 2500u && s->dac_vref_mv <= 5500u && d >= -5 && d <= 5)
            g_dac_vref = (double)s->dac_vref_mv / 1000.0;
    }
    if (s->adc_vdiv_h >= 100u && s->adc_vdiv_h <= 1000u)
        g_adc_vdiv = (double)s->adc_vdiv_h / 100.0;
    if (s->a12_gain >= 0.0f && s->a12_gain <= 10000.0f) g_mlacc_gain = s->a12_gain;
    if (s->a12_run_level < 11u) g_mlacc_run_level = s->a12_run_level;
    /* 0 is the valid "default limit source" / "use the default target" pair,
     * which is also what an older block's padding reads back as — so no guard
     * is needed for the old-record case, only for a corrupted one. (0 used to
     * mean "follow MG"; it now means the noise formula, so a saved 0 recalls
     * the better behaviour rather than the old coupling.) */
    if (s->a12_thr_src <= 3u) g_mlacc_thr_src = s->a12_thr_src;
    if (s->a12_thr_tgt_s == 0u || s->a12_thr_tgt_s >= (1u << MLACC_LEVELS))
        g_mlacc_thr_tgt_s = s->a12_thr_tgt_s;
    for (int i = 0; i < 11; i++)
        if (s->a12_lim[i] >= 1 && s->a12_lim[i] <= 500000) g_mlacc_lim[i] = s->a12_lim[i];

    /* PID[3..9] */
    for (int n = 3; n <= 9; n++) {
        int i = n - 3;
        if (is_finite_pos(s->pid_kp[i], 0.0f, 100000.0f)) g_pid[n].Kp = (double)s->pid_kp[i];
        if (is_finite_pos(s->pid_ki[i], 0.0f, 100000.0f)) g_pid[n].Ki = (double)s->pid_ki[i];
        if (is_finite_pos(s->pid_kd[i], 0.0f, 100000.0f)) g_pid[n].Kd = (double)s->pid_kd[i];
        if (is_finite_pos(s->pid_il[i], 100.0f, 100000.0f)) g_pid[n].I_LIMIT = (double)s->pid_il[i];
    }
    if (is_finite_pos(s->blend_cross, 0.0f, 1.0f))   g_blend_crossover = (double)s->blend_cross;
    if (is_finite_pos(s->blend_scale, 0.0f, 1.0f))   g_blend_scale     = (double)s->blend_scale;
    if (is_finite_pos(s->nn_max_step, 1.0f, 10000.0f)) g_nn_max_step   = (double)s->nn_max_step;
    if (is_finite_pos(s->press_off, -5000.0f, 5000.0f)) g_pressure_offset = s->press_off;
    if (is_finite_pos(s->alt_off, -3000.0f, 3000.0f))   g_altitude_offset = s->alt_off;

    g_svin_enabled = (s->svin_en != 0u);

    /* LTIC params */
    if (is_finite_pos(s->ltic_nsv, 0.0f, 1.0e6f))   g_ltic.ns_per_volt = s->ltic_nsv;
    if (is_finite_pos(s->ltic_zero, 0.0f, 3.3f))    g_ltic.zero_offset = s->ltic_zero;
    if (is_finite_pos(s->ltic_range, 0.0f, 1.0e9f)) g_ltic.range_ns    = s->ltic_range;
    if (is_finite_pos(s->ltic_dpll_kp, 0.0f, 100000.0f)) g_ltic.dpll.Kp = s->ltic_dpll_kp;
    if (is_finite_pos(s->ltic_dpll_ki, 0.0f, 100000.0f)) g_ltic.dpll.Ki = s->ltic_dpll_ki;
    if (is_finite_pos(s->ltic_dpll_kd, 0.0f, 100000.0f)) g_ltic.dpll.Kd = s->ltic_dpll_kd;
    if (is_finite_pos(s->ltic_dpll_il, 0.0f, 100000.0f)) g_ltic.dpll.I_LIMIT = s->ltic_dpll_il;
    if (is_finite_pos(s->ltic_lock_kp, 0.0f, 100000.0f)) g_ltic.lock.Kp = s->ltic_lock_kp;
    if (is_finite_pos(s->ltic_lock_ki, 0.0f, 100000.0f)) g_ltic.lock.Ki = s->ltic_lock_ki;
    if (is_finite_pos(s->ltic_lock_kd, 0.0f, 100000.0f)) g_ltic.lock.Kd = s->ltic_lock_kd;
    if (is_finite_pos(s->ltic_lock_il, 0.0f, 100000.0f)) g_ltic.lock.I_LIMIT = s->ltic_lock_il;
    if (is_finite_pos(s->ltic_acq_th, 0.0f, 1.0e9f))  g_ltic.acq_threshold_ns = s->ltic_acq_th;
    if (is_finite_pos(s->ltic_dpll_th, 0.0f, 1.0f))   g_ltic.dpll_lock_thresh = s->ltic_dpll_th;
    if (s->ltic_lock_iv != 0u && s->ltic_lock_iv != 0xFFFFu)
        g_ltic.lock_interval_s = s->ltic_lock_iv;
    if (s->ltic_state <= LTIC_LOCK) g_ltic.state = s->ltic_state;
    if (s->ltic_submode <= 1u)      g_ltic.submode = s->ltic_submode;
    if (is_finite_pos(s->ltic_acq_kp, 0.0f, 100000.0f)) g_ltic.acq.Kp = s->ltic_acq_kp;
    if (is_finite_pos(s->ltic_acq_ki, 0.0f, 100000.0f)) g_ltic.acq.Ki = s->ltic_acq_ki;
    if (is_finite_pos(s->ltic_acq_kd, 0.0f, 100000.0f)) g_ltic.acq.Kd = s->ltic_acq_kd;
    if (is_finite_pos(s->ltic_acq_il, 0.0f, 100000.0f)) g_ltic.acq.I_LIMIT = s->ltic_acq_il;
    {
        int8_t sp = s->ltic_polarity;
        g_ltic.polarity = (sp == 1 || sp == -1) ? sp : 0;
    }
    if (is_finite_pos(s->ltic_centre_v, 0.0f, 3.3f)) g_ltic.centre_v = s->ltic_centre_v;

    /* LTIC-Lars (algo 11), stored from SETTINGS_VER 2. gain 0 is valid (auto),
     * so its guard allows 0; the rest range-check like the LTIC block above. */
    if (s->ver >= 2u) {
        if (isfinite(s->lars_gain) && s->lars_gain >= 0.0f && s->lars_gain <= 10000.0f)
            g_lars.gain = s->lars_gain;
        if (is_finite_pos(s->lars_damping, 0.001f, 1000.0f)) g_lars.damping = s->lars_damping;
        if (s->lars_tc >= 1u && s->lars_tc <= 600u)   g_lars.time_const_s = s->lars_tc;
        if (s->lars_fdiv >= 1u && s->lars_fdiv <= 100u) g_lars.filter_div = s->lars_fdiv;
        if (s->lars_tic_off <= 4095u)                 g_lars.tic_offset  = s->lars_tic_off;
        if (s->lars_lock_lim >= 1u && s->lars_lock_lim <= 10000u) g_lars.lock_ns_lim = s->lars_lock_lim;
        if (s->lars_lock_fac >= 1u && s->lars_lock_fac <= 100u)   g_lars.lock_factor = s->lars_lock_fac;
        g_lars.temp_coeff = s->lars_tcoeff;           /* full int16 range valid */
        if (s->lars_tref <= 4095u)                    g_lars.temp_ref = s->lars_tref;
        g_lars.flags = s->lars_flags;
        /* FA damping windows: only the three legal values are accepted, so a
         * corrupt or zero field leaves the compile-time default in place. */
        if (s->fa_win_dpll == 10u || s->fa_win_dpll == 100u || s->fa_win_dpll == 1000u)
            g_freq_damp_win_dpll = s->fa_win_dpll;
        if (s->fa_win_lock == 10u || s->fa_win_lock == 100u || s->fa_win_lock == 1000u)
            g_freq_damp_win_lock = s->fa_win_lock;
        if (s->ver >= 4u) g_show_local_time = (s->lt_local != 0u);
    }

    /* LRN (fallback when ring not yet populated by live_store) */
    g_lrn_enable = (s->lrn_en != 0u);
    if (is_finite_pos(s->lrn_drift, -400.0f, 400.0f)) g_lrn_drift = s->lrn_drift;
    if (is_finite_pos(s->lrn_damp, LRN_DAMP_LO, LRN_DAMP_HI)) g_lrn_damp = s->lrn_damp;

    /* enable flags (0xFF from blank flash reads as "on" for these — matches
     * the legacy EEPROM convention so a fresh board comes up with the same
     * defaults) */
    g_warmup_enable = (s->warmup_en != 0u);
    g_splash_enable = (s->splash_en != 0u);
    g_qerr_enable   = (s->saw_en == 1u);

    /* timezone */
    if (s->tz_mode <= TZ_MODE_POSIX) {
        g_tz_mode = s->tz_mode;
        if (s->tz_manual_min >= -720 && s->tz_manual_min <= 840)
            g_tz_manual_min = s->tz_manual_min;
        /* tz_str: copy + NUL-terminate, then re-parse if POSIX */
        memcpy(g_tz_str, s->tz_str, TZ_STR_MAX - 1);
        g_tz_str[TZ_STR_MAX - 1] = '\0';
        if ((uint8_t)g_tz_str[0] == 0xFFu) g_tz_str[0] = '\0';
        if (g_tz_mode == TZ_MODE_POSIX) {
            if (!g_tz_str[0] || tz_parse(g_tz_str, &g_tz_spec) == 0) {
                g_tz_mode = TZ_MODE_AUTO_EU;
                g_tz_str[0] = '\0';
            }
        }
    }
}

/* ---- public API ---- */

bool settings_recall(void)
{
    /* Zeroed first: flash_ring_read_newest() copies only as many bytes as the
     * stored record holds, so a record written by a build with a smaller block
     * would leave the tail of this struct as stack garbage. Requiring the exact
     * size as well means a layout change can never be half-applied. */
    SettingsBlock_t s;
    memset(&s, 0, sizeof(s));
    uint16_t n = flash_ring_read_newest(REC_SETTINGS, (uint8_t *)&s, sizeof(s));
    /* Migration. A v4 block is shorter than a v5 one and carries no algo-12
     * fields; rejecting it outright would throw away a working PID, LC and
     * timezone because a new algorithm was added. Accept it, apply what it has,
     * and leave the algo-12 block at its defaults — the user notices nothing
     * except that algorithm 12 starts untuned, which it would anyway. */
    if (s.ver == 4u && n == offsetof(SettingsBlock_t, a12_gain)) {
        /* the fields beyond the v4 layout are already at their defaults */
    } else if (s.ver == 5u && n == offsetof(SettingsBlock_t, dac_vref_cv)) {
        /* and one version later, the same deal for the DV/AV output scales:
         * accepted with defaults, so a v1.06 board keeps its PID, LC, algo-12
         * tuning and DAC path while gaining the new knobs unset. */
    } else if (s.ver != SETTINGS_VER ||
               n < SETTINGS_V6_BYTES || n > sizeof(s)) {
        return false;
    }
    /* Anything between SETTINGS_V6_BYTES and the current size is a record from
     * a build that had fewer appended fields. The struct was zeroed above, so
     * those fields read as 0, which every one of them defines as "unset" — the
     * same deal the two migrations above give, but without needing a new case
     * each time a byte is added. */
    apply_full(&s);
    return true;
}

bool settings_save(void)
{
    SettingsBlock_t s;
    snapshot_full(&s);
    return flash_ring_write(REC_SETTINGS, (const uint8_t *)&s, sizeof(s));
}

bool settings_save_partial(settings_partial_t which)
{
    /* Seed from the last stored block if one exists, so untouched fields
     * keep their values. If none exists, snapshot_full fills every field
     * from runtime (first partial save behaves like a full save). */
    SettingsBlock_t s;
    memset(&s, 0, sizeof(s));
    uint16_t n = flash_ring_read_newest(REC_SETTINGS, (uint8_t *)&s, sizeof(s));
    /* A short record is fine — the struct was zeroed, so the fields a shorter
     * layout did not carry seed as "unset" and the write below promotes them
     * to the current layout. What is NOT fine is a record from a different
     * version, which would be a different meaning for the same bytes. */
    bool have_stored = (s.ver == SETTINGS_VER &&
                        n >= SETTINGS_V6_BYTES && n <= sizeof(s));
    if (!have_stored) snapshot_full(&s);
    s.ver = SETTINGS_VER;

    /* Update only the requested group from runtime. */
    switch (which) {
    case SET_ALL:
        snapshot_full(&s);
        break;
    case SET_TZ:
        s.tz_mode       = g_tz_mode;
        s.tz_manual_min = g_tz_manual_min;
        s.lt_local      = g_show_local_time ? 1u : 0u;
        memcpy(s.tz_str, g_tz_str, sizeof(s.tz_str));
        s.tz_str[sizeof(s.tz_str) - 1] = '\0';
        break;
    case SET_PID:
        put_pid_group(&s);
        break;
    case SET_LTIC:
        s.ltic_nsv  = g_ltic.ns_per_volt;
        s.ltic_zero = g_ltic.zero_offset;
        s.ltic_range = g_ltic.range_ns;
        put_ltic_gains(&s);
        s.ltic_acq_th  = g_ltic.acq_threshold_ns;
        s.ltic_dpll_th = g_ltic.dpll_lock_thresh;
        s.ltic_lock_iv = g_ltic.lock_interval_s;
        s.ltic_state   = g_ltic.state;
        s.ltic_submode = g_ltic.submode;
        s.ltic_polarity = g_ltic.polarity;
        s.ltic_centre_v = g_ltic.centre_v;
        /* LTIC-Lars (algo 11) rides with the LTIC group: one ES LTIC saves both */
        s.lars_gain     = g_lars.gain;
        s.lars_damping  = g_lars.damping;
        s.lars_tc       = g_lars.time_const_s;
        s.lars_fdiv     = g_lars.filter_div;
        s.lars_tic_off  = g_lars.tic_offset;
        s.lars_lock_lim = g_lars.lock_ns_lim;
        s.lars_lock_fac = g_lars.lock_factor;
        s.lars_tcoeff   = g_lars.temp_coeff;
        s.lars_tref     = g_lars.temp_ref;
        s.lars_flags    = g_lars.flags;
        s.fa_win_dpll   = g_freq_damp_win_dpll;
        s.fa_win_lock   = g_freq_damp_win_lock;
        break;
    case SET_FLAGS:
        s.warmup_en = g_warmup_enable ? 1u : 0u;
        s.splash_en = g_splash_enable ? 1u : 0u;
        s.saw_en    = g_qerr_enable  ? 1u : 0u;
        s.lrn_en    = g_lrn_enable   ? 1u : 0u;
        s.svin_en   = g_svin_enabled ? 1u : 0u;
        /* The backlight rides with the display flags: splash and brightness
         * are one description of what the panel does, and `BL` should no more
         * rewrite the PID block than `SP` should. */
        s.bl_pct    = g_tft_bl_pct;
        break;
    case SET_ALGO:
        s.pwm  = gCtrl.pwm_output;
        s.algo = gCtrl.active_algo;
        s.dac_path = (uint8_t)(g_dac_path + 1u);
        /* The scales ride with the path for the same reason the path rides
         * here: they are one physical description of the output stage, and
         * `DAC EXT` / `DV` / `AV` all land in the same partial save. */
        s.dac_vref_cv = (uint16_t)(g_dac_vref * 100.0 + 0.5);
        s.dac_vref_mv = (uint16_t)(g_dac_vref * 1000.0 + 0.5);
        s.adc_vdiv_h  = (uint16_t)(g_adc_vdiv * 100.0 + 0.5);
        /* And what the PA0 divider is looking at, for the same reason: it is
         * one more thing about this board's wiring that the firmware cannot
         * see and has to be told. */
        s.vsense_src  = (uint8_t)(g_vsense_src + 1u);
        break;
    case SET_ALGO12:
        /* Saved on its own so tuning algorithm 12 does not rewrite the PID or
         * LTIC blocks, exactly as ES LTIC keeps to its own. */
        s.a12_gain      = g_mlacc_gain;
        s.a12_run_level = g_mlacc_run_level;
        s.a12_thr_src   = g_mlacc_thr_src;
        s.a12_thr_tgt_s = g_mlacc_thr_tgt_s;
        for (int i = 0; i < 11; i++) s.a12_lim[i] = g_mlacc_lim[i];
        break;

    case SET_PO:
        s.press_off = g_pressure_offset;
        s.alt_off   = g_altitude_offset;
        break;

    case SET_SPAN:
        /* One write for everything a move of the span jumper changes, so a
         * power cut cannot leave the span record saying REDUCED next to a PID
         * block derived for FULL. The code goes with it: it was remapped, and
         * the live store's copy is only as fresh as its last tick. */
        put_span(&s);
        put_pid_group(&s);
        put_ltic_gains(&s);
        s.pwm = gCtrl.pwm_output;
        break;
    }

    return flash_ring_write(REC_SETTINGS, (const uint8_t *)&s, sizeof(s));
}
