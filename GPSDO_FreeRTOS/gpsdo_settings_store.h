/* ======================================================================
 * gpsdo_settings_store.h  —  persistent user settings via the flash ring
 *
 * Part of GPSDO v1.07.57rt
 *
 * Replaces the old STM32duino emulated EEPROM (gpsdo_state.cpp eeprom_*).
 * Settings are stored as a REC_SETTINGS slot in the flash ring (sector 7),
 * alongside the live data (REC_LIVE). This gives them the same atomicity
 * guarantees the ring already provides for live data: per-slot CRC16,
 * sequence-numbered newest-wins, read-back verify, power-loss safety.
 *
 * Layout version (SETTINGS_VER) is the first byte of the payload. A mismatch
 * on recall (e.g. older layout saved by a previous firmware) is treated like
 * a blank ring: compile-time defaults apply, the user re-runs CT/LC/TZ/ES.
 *
 * Selective save (ES <object>): rather than overwrite the entire settings
 * block when the user changes one field, settings_save_partial() reads the
 * last saved block from the ring, updates only the requested group of fields
 * from the live runtime state, and writes a fresh slot. Untouched fields
 * keep their stored values — a deliberate `ES TZ` cannot clobber a hand-tuned
 * PID set the way the old full-state EEPROM write could.
 * ====================================================================== */
#ifndef GPSDO_SETTINGS_STORE_H
#define GPSDO_SETTINGS_STORE_H

#include <stdint.h>
#include <stdbool.h>

/* Bump when the on-flash layout of SettingsBlock_t changes. A mismatch with
 * the stored value triggers a fall-back to defaults on recall. */
#define SETTINGS_VER  6u   /* v6: + DV/AV output scales; v5 algo-12 block; v4 LT flag; v3 FA windows; v2 Lars */

/* The flat on-flash block. Total size must stay <= FR_PAYLOAD (504 B).
 * Every field is little-endian and tightly packed; the recall path range-
 * checks each one so a corrupt byte can't poison the runtime. */
typedef struct {
    uint8_t  ver;            /* SETTINGS_VER — must match or recall rejects */
    uint16_t pwm;            /* gCtrl.pwm_output                           */
    uint8_t  algo;           /* gCtrl.active_algo                          */
    /* PID params for algos 3-9 (7 algos × 4 floats) */
    float    pid_kp[7];      /* g_pid[3..9].Kp                             */
    float    pid_ki[7];
    float    pid_kd[7];
    float    pid_il[7];
    float    blend_cross;    /* g_blend_crossover                          */
    float    blend_scale;    /* g_blend_scale                              */
    float    nn_max_step;    /* g_nn_max_step                              */
    float    press_off;      /* g_pressure_offset                          */
    float    alt_off;        /* g_altitude_offset                          */
    uint8_t  svin_en;        /* g_svin_enabled                             */
    /* LTIC params (algo 10) */
    float    ltic_nsv;       /* g_ltic.ns_per_volt                         */
    float    ltic_zero;      /* g_ltic.zero_offset                         */
    float    ltic_range;     /* g_ltic.range_ns                            */
    float    ltic_dpll_kp, ltic_dpll_ki, ltic_dpll_kd, ltic_dpll_il;
    float    ltic_lock_kp, ltic_lock_ki, ltic_lock_kd, ltic_lock_il;
    float    ltic_acq_th;    /* g_ltic.acq_threshold_ns                    */
    float    ltic_dpll_th;   /* g_ltic.dpll_lock_thresh                    */
    uint16_t ltic_lock_iv;   /* g_ltic.lock_interval_s                     */
    uint8_t  ltic_state;     /* g_ltic.state                               */
    uint8_t  ltic_submode;   /* g_ltic.submode                             */
    float    ltic_acq_kp, ltic_acq_ki, ltic_acq_kd, ltic_acq_il;
    int8_t   ltic_polarity;  /* g_ltic.polarity                            */
    float    ltic_centre_v;  /* g_ltic.centre_v                            */
    /* LTIC-Lars params (algo 11) — SETTINGS_VER 2+ */
    float    lars_gain;      /* g_lars.gain (0 = auto from CT)             */
    float    lars_damping;   /* g_lars.damping                            */
    uint16_t lars_tc;        /* g_lars.time_const_s                       */
    uint8_t  lars_fdiv;      /* g_lars.filter_div                         */
    uint16_t lars_tic_off;   /* g_lars.tic_offset                         */
    uint16_t lars_lock_lim;  /* g_lars.lock_ns_lim                        */
    uint8_t  lars_lock_fac;  /* g_lars.lock_factor                        */
    int16_t  lars_tcoeff;    /* g_lars.temp_coeff                         */
    uint16_t lars_tref;      /* g_lars.temp_ref                           */
    uint8_t  lars_flags;     /* g_lars.flags                              */
    uint16_t fa_win_dpll;    /* g_freq_damp_win_dpll (FAD)                */
    uint16_t fa_win_lock;    /* g_freq_damp_win_lock (FAL)                */
    uint8_t  lt_local;       /* g_show_local_time (LT): 1 = local, 0 = UTC */
    /* LRN params (fallback only — normally owned by live_store/ring) */
    uint8_t  lrn_en;
    float    lrn_drift;      /* g_lrn_drift                                */
    float    lrn_damp;       /* g_lrn_damp                                 */
    /* enable flags */
    uint8_t  warmup_en;      /* g_warmup_enable                            */
    uint8_t  splash_en;      /* g_splash_enable                            */
    uint8_t  saw_en;         /* g_qerr_enable                              */
    /* timezone */
    uint8_t  tz_mode;        /* g_tz_mode (0=manual 1=auto-EU 2=posix)     */
    int16_t  tz_manual_min;  /* g_tz_manual_min                            */
    char     tz_str[48];     /* g_tz_str (POSIX rule, NUL-terminated)      */
    /* Control-voltage output path (DAC command) — carved from the two
     * alignment pad bytes between tz_str and the float that follows it, the
     * same trick the algo-12 fields below use and for the same reason: a
     * version bump would throw away everyone's PID, LC and timezone for one
     * byte. Verified against the compiler, not by eye — tz_str ends at 318,
     * a12_gain sits at 320, so 318 and 319 are padding no build has ever
     * written. Layout, size and SETTINGS_VER are unchanged.
     *
     * ZERO MEANS UNSET, so a record written by an older build — which memsets
     * to zero and never touches these bytes — asks for the default rather than
     * for path 0. That is why the encoding starts at 1. */
    uint8_t  dac_path;       /* 0=unset 1=PWM 2=DITH 3=EXT (g_dac_path+1)  */
    /* TFT backlight percentage (BL command). The SECOND of the two pad bytes
     * described above — dac_path took 318, this takes 319, and a12_gain still
     * starts at 320. Verified with offsetof under arm-none-eabi, not by eye:
     * sizeof(SettingsBlock_t) is 376 before and after, so every field below
     * keeps its offset and a v6 record written by an earlier build still
     * loads. The alternative was SETTINGS_VER 7, and recall requires an exact
     * version AND size match — a bump throws away everyone's PID, LC and
     * timezone, for one byte.
     *
     * ZERO MEANS UNSET, the same convention as dac_path, so a record from a
     * build before this field existed asks for BL_PCT_DEF rather than for a
     * dark screen. The valid range starts at 30. */
    uint8_t  bl_pct;         /* g_tft_bl_pct, 30..100; 0 = unset            */
    /* Algorithm 12 (multi-level accumulator) — SETTINGS_VER 5+.
     *
     * These are here rather than compiled in because the limits are the one
     * thing that has to change per oscillator: only the 128 s value was ever
     * derived from a specification, and the rest were arbitrary even in the
     * design this comes from. A user tuning against their own OCXO must be able
     * to keep the result. */
    float    a12_gain;       /* g_mlacc_gain, LSB per ns (0 = auto from CT) */
    uint8_t  a12_run_level;  /* g_mlacc_run_level                          */
    /* Carved out of the three alignment pad bytes this block already had, so
     * the layout, the size and SETTINGS_VER are all unchanged and a block
     * written by an older build still loads. Both snapshot paths memset the
     * struct before filling it, so an old record reads back as 0/0 here —
     * which is MLACC_THR_FOLLOW and "use the default target", i.e. exactly the
     * behaviour that build had. Growing the struct instead would have forced a
     * version bump and thrown away everyone's PID, LC and timezone for two
     * fields that fit in the padding. */
    uint8_t  a12_thr_src;    /* g_mlacc_thr_src   (MF)                     */
    uint16_t a12_thr_tgt_s;  /* g_mlacc_thr_tgt_s (MFT), 0 = default       */
    int32_t  a12_lim[11];    /* g_mlacc_lim[], phase limits in ns          */
    /* Output-path scales (DV / AV commands) — SETTINGS_VER 6+.
     *
     * Appended rather than carved from padding because no padding is left
     * here, which is why this one IS a version bump: a v5 record is shorter,
     * and settings_recall() accepts it and leaves these two at the defaults —
     * the same deal the v4→v5 migration gives algo 12.
     *
     * ZERO MEANS UNSET, exactly like dac_path: a record written by a v5 build
     * never wrote these bytes, and 0 asks for the default (3.30 V, 1.00×)
     * rather than for a nonsense scale. */
    uint16_t dac_vref_cv;    /* g_dac_vref in centivolts ×100 (DV), 0 = 3.3 V */
    uint16_t adc_vdiv_h;     /* g_adc_vdiv ×100 (AV), 0 = 1.00×               */
    /* What the PA0 divider is measuring (VS command) — SETTINGS_VER 6, appended
     * after the layout was frozen. The board selects between the 5 V rail and
     * the voltage reference with a jumper on the top of that divider, and the
     * firmware cannot see a jumper, so it is told. 0 = unset (an older record,
     * which means the 5 V rail, the behaviour every build before this had),
     * 1 = VCC, 2 = VREF — offset by one for the same reason dac_path is.
     *
     * APPENDED WITHOUT A VERSION BUMP, and that is now a supported thing to do
     * rather than a trick: settings_recall() accepts any record from
     * SETTINGS_V6_BYTES up to the current size, and the struct is zeroed first,
     * so everything appended reads back as 0 = unset on an older record. The
     * three alignment bytes after this one are the room for the next two. */
    uint8_t  vsense_src;     /* g_vsense_src + 1; 0 = unset = the 5 V rail    */
    /* Span sensing on PB14 (gpsdo_span.h) — appended under the rule above: an
     * older record reads all of these back as 0, and 0 means "never recorded",
     * "not calibrated" and "no pair" respectively, i.e. exactly a board that
     * has not met the jumper yet. span_last takes the first of the three pad
     * bytes after vsense_src; the floats start on the next word, so the block
     * grows from 380 to 392 bytes. Checked with offsetof under arm-none-eabi. */
    uint8_t  span_last;      /* span in force when saved, + 1; 0 = unset      */
    float    span_k[2];      /* CT's K in each span, Hz/LSB; 0 = none         */
    uint16_t span_pair[2];   /* one code per span, same EFC volts; 0 = none   */
    /* DV in millivolts. dac_vref_cv holds centivolts, so a DV of 4.096 — the
     * ADR4540 on Dan Wiering's V3 — came back from every restart as 4.10, a
     * 0.1 % bias on the commanded column of the DAC report. Appended under the
     * rule above rather than by widening dac_vref_cv, which every older build
     * reads: dac_vref_cv is still written, from the same value, so a downgrade
     * keeps the two-decimal DV it always had, and an older record reads this
     * back as 0 = not recorded. On recall it refines dac_vref_cv only when the
     * two agree to the centivolt (apply_full). Takes the word after
     * span_pair: the block grows from 392 to 396 bytes. Checked with offsetof
     * under arm-none-eabi. */
    uint16_t dac_vref_mv;    /* g_dac_vref ×1000 (DV); 0 = not recorded       */
} SettingsBlock_t;

/* The size SETTINGS_VER 6 had when it was frozen, and the shortest record
 * settings_recall() will accept as one. Every byte appended since reads back
 * as zero on such a record, which each appended field defines as "unset".
 *
 * DO NOT CHANGE THIS NUMBER WHEN YOU APPEND A FIELD — it is the whole point of
 * it. The alternative, and what this file used to do, was a fresh `else if`
 * per version with a hand-written offsetof, and before that a version bump,
 * which threw away everyone's PID, LC and timezone to gain one byte. */
#define SETTINGS_V6_BYTES  376u

/* Which group of fields a partial save should update. Used by `ES <object>`. */
typedef enum {
    SET_ALL    = 0,   /* full save from runtime (backwards-compatible ES) */
    SET_TZ     = 1,   /* tz_mode, tz_manual_min, tz_str                   */
    SET_PID    = 2,   /* pid_kp/ki/kd/il[3..9], blend, nn_max_step        */
    SET_LTIC   = 3,   /* all ltic_* fields                                */
    SET_FLAGS  = 4,   /* warmup_en, splash_en, saw_en, lrn_en, svin_en,   */
                      /* bl_pct — the display group                        */
    SET_ALGO   = 5,   /* pwm, algo, dac_path, DV/AV scales, VS source     */
    SET_ALGO12 = 7,   /* algo-12 gain, run level, per-level limits         */
    SET_PO     = 6,    /* press_off, alt_off                              */
    SET_SPAN   = 8,   /* span record + everything a span move re-derives: */
                      /* the PID group, the LTIC loop gains, and pwm       */
} settings_partial_t;

/* Boot-time recall. Reads the newest REC_SETTINGS slot from the ring,
 * validates version + per-field range guards, and applies the values to the
 * runtime globals (gCtrl, g_pid, g_ltic, g_tz_*, flags). Safe to call
 * before the scheduler starts (no mutex use). Returns true if a valid
 * stored block was found and applied; false if the ring had no settings
 * slot (compile-time defaults remain in effect). */
bool settings_recall(void);

/* Full save: snapshot every settings field from runtime into a fresh
 * SettingsBlock_t and write it as REC_SETTINGS. Returns true on success. */
bool settings_save(void);

/* Partial save: read the last stored block, update only the requested group
 * of fields from the live runtime, write it back. Fields outside the group
 * keep their stored values. If no prior block exists, the non-requested
 * fields are seeded from the current runtime (so the first partial save
 * behaves like a full save). Returns true on success. */
bool settings_save_partial(settings_partial_t which);

#endif /* GPSDO_SETTINGS_STORE_H */
