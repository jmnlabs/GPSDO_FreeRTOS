/* ======================================================================
 * gpsdo_flash_ring_core.c  —  hardware-independent ring-buffer logic
 * Part of GPSDO v1.08.65rt
 * See gpsdo_flash_ring_core.h for the design and rationale.
 * ====================================================================== */
#include "gpsdo_flash_ring_core.h"
#include <string.h>

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) */
uint16_t fr_crc16(const uint8_t *p, uint32_t n)
{
    uint16_t c = 0xFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u)
                              : (uint16_t)(c << 1);
    }
    return c;
}

/* ---- header helpers ---- */

static void fr_build_header(uint8_t *hdr, uint16_t erase_count)
{
    memset(hdr, 0, FR_HDR_SIZE);
    memcpy(hdr, FR_MAGIC_STR, 8);
    hdr[8]  = (uint8_t)FR_FMT_VER;
    hdr[12] = (uint8_t)(erase_count & 0xFFu);
    hdr[13] = (uint8_t)(erase_count >> 8);
    uint16_t crc = fr_crc16(hdr, 14);
    hdr[14] = (uint8_t)(crc & 0xFFu);
    hdr[15] = (uint8_t)(crc >> 8);
}

static int fr_header_valid(const uint8_t *hdr)
{
    if (memcmp(hdr, FR_MAGIC_STR, 8) != 0) return 0;
    if (hdr[8] != (uint8_t)FR_FMT_VER)     return 0;
    uint16_t crc = fr_crc16(hdr, 14);
    return (hdr[14] == (uint8_t)(crc & 0xFFu) &&
            hdr[15] == (uint8_t)(crc >> 8));
}

/* ---- slot helpers ---- */

static int fr_slot_blank(const uint8_t *s)
{
    for (int i = 0; i < (int)FR_SLOT_SIZE; i++)
        if (s[i] != 0xFFu) return 0;
    return 1;
}

static int fr_slot_valid(const uint8_t *s)
{
    if (fr_slot_blank(s)) return 0;
    uint16_t crc = fr_crc16(s, FR_SLOT_SIZE - 2);
    return (s[FR_SLOT_SIZE - 2] == (uint8_t)(crc & 0xFFu) &&
            s[FR_SLOT_SIZE - 1] == (uint8_t)(crc >> 8));
}

static uint16_t fr_slot_seq (const uint8_t *s) { return (uint16_t)(s[0] | (s[1] << 8)); }
static uint8_t  fr_slot_type(const uint8_t *s) { return s[2]; }
static uint16_t fr_slot_len (const uint8_t *s) { return (uint16_t)(s[4] | (s[5] << 8)); }

/* newer-than test using modular sequence distance (handles wrap) */
static int fr_seq_newer(uint16_t a, uint16_t b)
{
    return (uint16_t)(a - b) < 0x8000u;
}

static uint32_t fr_slot_off(const fr_state_t *st, uint16_t idx)
{
    (void)st;
    return FR_HDR_SIZE + (uint32_t)idx * FR_SLOT_SIZE;
}

/* Erase sector and lay down a fresh header with the given erase_count.
 *
 * THE HEADER IS READ BACK (build 65), as every slot is: one that did not go
 * in whole made the next boot see a foreign sector and format it, records
 * and all. Reported here, a wrap or a wipe tries the erase once more. */
static int fr_fresh(fr_state_t *st, uint16_t erase_count)
{
    if (st->ops->erase() != 0) return -1;
    uint8_t hdr[FR_HDR_SIZE];
    fr_build_header(hdr, erase_count);
    if (st->ops->program(0, hdr, FR_HDR_SIZE) != 0) return -1;
    uint8_t chk[FR_HDR_SIZE];
    st->ops->read(0, chk, FR_HDR_SIZE);
    if (memcmp(chk, hdr, FR_HDR_SIZE) != 0) return -1;
    st->used = 0; st->next_idx = 0; st->cur_seq = 0;
    st->have_data = 0;
    st->erase_count = erase_count;
    return 0;
}

int fr_begin(fr_state_t *st, const fr_ops_t *ops)
{
    st->ops = ops;
    st->slot_count = (uint16_t)((ops->sector_len - FR_HDR_SIZE) / FR_SLOT_SIZE);
    st->used = 0; st->next_idx = 0; st->cur_seq = 0;
    st->have_data = 0; st->erase_count = 0;

    uint8_t hdr[FR_HDR_SIZE];
    ops->read(0, hdr, FR_HDR_SIZE);

    if (!fr_header_valid(hdr)) {
        /* foreign / blank / garbage → start a clean ring. If the old header
         * happened to be a valid GPSDOLIV of a DIFFERENT version we still
         * reset, but preserve the erase count when we can read it. */
        uint16_t ec = 0;
        if (memcmp(hdr, FR_MAGIC_STR, 8) == 0) {
            uint16_t crc = fr_crc16(hdr, 14);
            if (hdr[14] == (uint8_t)(crc & 0xFFu) && hdr[15] == (uint8_t)(crc >> 8))
                ec = (uint16_t)(hdr[12] | (hdr[13] << 8));
        }
        /* count this erase */
        return fr_begin_fresh_count(st, ec);
    }

    st->erase_count = (uint16_t)(hdr[12] | (hdr[13] << 8));

    /* scan every slot: find highest seq across ALL types (for next write's
     * seq), find the last slot that took any bits, count used. Per-type
     * newest is resolved on demand by fr_read_newest(). */
    uint8_t slot[FR_SLOT_SIZE];
    int last_used = -1;
    for (uint16_t i = 0; i < st->slot_count; i++) {
        st->ops->read(fr_slot_off(st, i), slot, FR_SLOT_SIZE);
        if (fr_slot_blank(slot)) continue;
        last_used = (int)i;
        if (!fr_slot_valid(slot)) continue;  /* half-written / corrupt: skip */
        st->used++;
        uint16_t seq = fr_slot_seq(slot);
        if (!st->have_data || fr_seq_newer(seq, st->cur_seq)) {
            st->have_data = 1; st->cur_seq = seq;
        }
    }
    /* NEXT FREE SLOT: THE ONE AFTER THE LAST THAT TOOK ANY BITS (build 65),
     * or full if that was the last one. Up to build 64 it was the first blank
     * one. Slots fill in order, so the two agree - unless a blank slot was
     * left behind the newest, which two writers in one slot did (each stepped
     * on, skipping the next; gpsdo_flash_ring.cpp now takes a lock against
     * that). Writing from such a hole would program over the records after
     * it, one a write. */
    st->next_idx = (uint16_t)(last_used + 1);
    return st->have_data;
}

/* helper split out so fr_begin stays readable; increments erase count */
int fr_begin_fresh_count(fr_state_t *st, uint16_t prev_ec)
{
    uint16_t ec = (uint16_t)(prev_ec + 1u);
    if (fr_fresh(st, ec) != 0) return -1;
    return 0;   /* no data yet */
}

uint16_t fr_read_newest(fr_state_t *st, uint8_t type,
                        uint8_t *out, uint16_t maxlen)
{
    if (!st->have_data) return 0;

    /* Scan for the newest valid slot of the requested type. Each scan is
     * O(slot_count) — 256 reads worst case. Acceptable because reads are
     * called rarely (boot + a few CLI commands), never from a hot loop. */
    uint8_t slot[FR_SLOT_SIZE];
    uint16_t best_seq = 0, best_idx = 0xFFFFu;
    for (uint16_t i = 0; i < st->slot_count; i++) {
        st->ops->read(fr_slot_off(st, i), slot, FR_SLOT_SIZE);
        if (fr_slot_blank(slot)) continue;       /* first blank ends live area */
        if (!fr_slot_valid(slot)) continue;
        if (fr_slot_type(slot) != type) continue;
        uint16_t seq = fr_slot_seq(slot);
        if (best_idx == 0xFFFFu || fr_seq_newer(seq, best_seq)) {
            best_seq = seq; best_idx = i;
        }
    }
    if (best_idx == 0xFFFFu) return 0;

    st->ops->read(fr_slot_off(st, best_idx), slot, FR_SLOT_SIZE);
    uint16_t len = fr_slot_len(slot);
    if (len > FR_PAYLOAD) len = FR_PAYLOAD;
    if (len > maxlen)     len = maxlen;
    memcpy(out, slot + FR_SLOT_HDR, len);   /* payload follows the 6-byte header */
    return len;
}

/* Program one slot at next_idx; the caller has made room.
 *
 * A SLOT THAT TOOK SOME OF ITS BITS IS SPENT (build 65). Flash only clears
 * bits, so a slot whose program or readback failed after part of it went in
 * cannot be programmed cleanly again. Up to build 64 the next write - ES
 * included - went back to that same slot and failed the same way, until a
 * reboot's scan stepped over it. The next attempt now goes to the next slot;
 * one that is still blank is simply tried again. */
static int fr_put(fr_state_t *st, uint8_t type,
                  const uint8_t *data, uint16_t len)
{
    uint16_t seq = st->have_data ? (uint16_t)(st->cur_seq + 1u) : 1u;

    uint8_t slot[FR_SLOT_SIZE];
    memset(slot, 0, FR_SLOT_SIZE);
    slot[0] = (uint8_t)(seq & 0xFFu);
    slot[1] = (uint8_t)(seq >> 8);
    slot[2] = type;
    slot[3] = 0u;                       /* reserved; keeps the payload aligned */
    slot[4] = (uint8_t)(len & 0xFFu);   /* length is 16-bit: a settings block is */
    slot[5] = (uint8_t)(len >> 8);      /* ~324 B and would truncate in 8 bits   */
    memcpy(slot + FR_SLOT_HDR, data, len);
    uint16_t crc = fr_crc16(slot, FR_SLOT_SIZE - 2);
    slot[FR_SLOT_SIZE - 2] = (uint8_t)(crc & 0xFFu);
    slot[FR_SLOT_SIZE - 1] = (uint8_t)(crc >> 8);

    uint32_t off = fr_slot_off(st, st->next_idx);
    int rc = st->ops->program(off, slot, FR_SLOT_SIZE);

    /* verify readback */
    uint8_t chk[FR_SLOT_SIZE];
    st->ops->read(off, chk, FR_SLOT_SIZE);
    if (rc != 0 || memcmp(chk, slot, FR_SLOT_SIZE) != 0) {
        if (!fr_slot_blank(chk)) st->next_idx++;      /* spent: see above */
        return -1;
    }

    st->have_data = 1; st->cur_seq = seq;
    st->next_idx++; st->used++;
    return 0;
}

/* THE RING IS ONE ERASE DOMAIN, SO A WRAP CARRIES EVERY RECORD ACROSS IT
 * (build 65). Settings, the Kalman record and the live data share the sector.
 * Up to build 64 a full ring was erased and only the slot that filled it was
 * written back: the newest slot of every other type went with the old ones,
 * and the next boot came up on defaults - "Settings: none stored", CT's K,
 * the algorithm and KR/KQ/KT gone, or LC's calibration if the wrap was an
 * ES. Harmless while the ring held live data alone; and while only ES, LC
 * and the drift learner wrote, a ring took years to fill on algorithms 11-13.
 * Build 65's live store also saves the operating point, and on a drifting
 * board that brings the wrap round in months.
 *
 * So the newest valid slot of each type is read out before the erase and
 * written back after it, the writer's own type included (if the power goes
 * before the new slot is in, the previous one is still there), and the new
 * slot follows. They are held in a static buffer - three slots, 1.5 KB, too
 * much for any task's stack, and one writer at a time: the caller serialises
 * (gpsdo_flash_ring.cpp). An erase or a header that fails is tried once more,
 * and so is a copy that will not program - on the next slot if the first try
 * took some of its bits. A power cut during the erase, or between it and the
 * copies, still loses them, and so does a flash that fails twice: one sector
 * cannot be erased and kept at once. That is a second or two once per ring,
 * where before the loss was certain. */
static uint8_t s_fr_keep[FR_TYPES][FR_SLOT_SIZE];

static int fr_wrap(fr_state_t *st)
{
    uint16_t idx[FR_TYPES], seq[FR_TYPES];
    for (uint8_t t = 0; t < FR_TYPES; t++) { idx[t] = 0xFFFFu; seq[t] = 0u; }

    uint8_t *s = s_fr_keep[0];                  /* scratch while scanning */
    for (uint16_t i = 0; i < st->slot_count; i++) {
        st->ops->read(fr_slot_off(st, i), s, FR_SLOT_SIZE);
        if (!fr_slot_valid(s)) continue;        /* blank, half-written, spent */
        uint8_t t = fr_slot_type(s);
        if (t >= FR_TYPES) continue;
        if (idx[t] == 0xFFFFu || fr_seq_newer(fr_slot_seq(s), seq[t])) {
            idx[t] = i; seq[t] = fr_slot_seq(s);
        }
    }
    uint8_t n = 0;
    for (uint8_t t = 0; t < FR_TYPES; t++) {
        if (idx[t] == 0xFFFFu) continue;
        st->ops->read(fr_slot_off(st, idx[t]), s_fr_keep[n], FR_SLOT_SIZE);
        if (fr_slot_valid(s_fr_keep[n]) && fr_slot_type(s_fr_keep[n]) == t)
            n++;                                /* read twice: carry only what is still valid */
    }

    const uint16_t ec = (uint16_t)(st->erase_count + 1u);
    if (fr_fresh(st, ec) != 0 && fr_fresh(st, ec) != 0) return -1;

    /* Each copy gets two tries; one that will not program after that is
     * lost, as all of them were before, and the writer's own slot still goes
     * in after it. */
    for (uint8_t k = 0; k < n; k++) {
        uint16_t len = fr_slot_len(s_fr_keep[k]);
        if (len > FR_PAYLOAD) len = FR_PAYLOAD;
        const uint8_t t = fr_slot_type(s_fr_keep[k]);
        if (fr_put(st, t, s_fr_keep[k] + FR_SLOT_HDR, len) != 0)
            (void)fr_put(st, t, s_fr_keep[k] + FR_SLOT_HDR, len);
    }
    return 0;
}

/* CR'S WIPE (build 65): one erase and a fresh header, the wear count carried
 * on, two tries as at a wrap. Up to build 64 the caller erased the sector and
 * fr_begin() then found no header, took the ring for a foreign one and erased
 * it again - starting the count from 1, and returning "no data", which the
 * caller reported as a failed erase. */
int fr_wipe(fr_state_t *st)
{
    const uint16_t ec = (uint16_t)(st->erase_count + 1u);
    if (fr_fresh(st, ec) != 0 && fr_fresh(st, ec) != 0) return -1;
    return 0;
}

int fr_write(fr_state_t *st, uint8_t type,
             const uint8_t *data, uint16_t len)
{
    if (len > FR_PAYLOAD) len = FR_PAYLOAD;

    /* full? erase and wrap, carrying erase_count and every record forward */
    if (st->next_idx >= st->slot_count) {
        if (fr_wrap(st) != 0) return -1;
    }
    return fr_put(st, type, data, len);
}

uint32_t fr_erase_count(const fr_state_t *st) { return st->erase_count; }
