/* ======================================================================
 * flash_ring_core.c  —  hardware-independent ring-buffer logic
 * Part of GPSDO FreeRTOS v0.90
 * See flash_ring_core.h for the design and rationale.
 * ====================================================================== */
#include "flash_ring_core.h"
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
    uint16_t crc = fr_crc16(s, 30);
    return (s[30] == (uint8_t)(crc & 0xFFu) &&
            s[31] == (uint8_t)(crc >> 8));
}

static uint16_t fr_slot_seq(const uint8_t *s) { return (uint16_t)(s[0] | (s[1] << 8)); }
static uint8_t  fr_slot_len(const uint8_t *s) { return s[2]; }

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

/* Erase sector and lay down a fresh header with the given erase_count. */
static int fr_fresh(fr_state_t *st, uint16_t erase_count)
{
    if (st->ops->erase() != 0) return -1;
    uint8_t hdr[FR_HDR_SIZE];
    fr_build_header(hdr, erase_count);
    if (st->ops->program(0, hdr, FR_HDR_SIZE) != 0) return -1;
    st->used = 0; st->next_idx = 0; st->cur_seq = 0;
    st->have_data = 0; st->cur_idx = 0;
    st->erase_count = erase_count;
    return 0;
}

int fr_begin(fr_state_t *st, const fr_ops_t *ops)
{
    st->ops = ops;
    st->slot_count = (uint16_t)((ops->sector_len - FR_HDR_SIZE) / FR_SLOT_SIZE);
    st->used = 0; st->next_idx = 0; st->cur_seq = 0;
    st->have_data = 0; st->cur_idx = 0; st->erase_count = 0;

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

    /* scan every slot: find newest valid, first blank, count used */
    uint8_t slot[FR_SLOT_SIZE];
    int first_blank = -1;
    for (uint16_t i = 0; i < st->slot_count; i++) {
        st->ops->read(fr_slot_off(st, i), slot, FR_SLOT_SIZE);
        if (fr_slot_blank(slot)) {
            if (first_blank < 0) first_blank = (int)i;
            continue;
        }
        if (!fr_slot_valid(slot)) continue;  /* half-written / corrupt: skip */
        st->used++;
        uint16_t seq = fr_slot_seq(slot);
        if (!st->have_data || fr_seq_newer(seq, st->cur_seq)) {
            st->have_data = 1; st->cur_seq = seq; st->cur_idx = i;
        }
    }
    /* next free slot: the first blank, or wrap to full if none */
    st->next_idx = (first_blank >= 0) ? (uint16_t)first_blank : st->slot_count;
    return st->have_data;
}

/* helper split out so fr_begin stays readable; increments erase count */
int fr_begin_fresh_count(fr_state_t *st, uint16_t prev_ec)
{
    uint16_t ec = (uint16_t)(prev_ec + 1u);
    if (fr_fresh(st, ec) != 0) return -1;
    return 0;   /* no data yet */
}

uint16_t fr_read(fr_state_t *st, uint8_t *out, uint16_t maxlen)
{
    if (!st->have_data) return 0;
    uint8_t slot[FR_SLOT_SIZE];
    st->ops->read(fr_slot_off(st, st->cur_idx), slot, FR_SLOT_SIZE);
    if (!fr_slot_valid(slot)) return 0;
    uint16_t len = fr_slot_len(slot);
    if (len > FR_PAYLOAD) len = FR_PAYLOAD;
    if (len > maxlen)     len = maxlen;
    memcpy(out, slot + 3, len);
    return len;
}

int fr_write(fr_state_t *st, const uint8_t *data, uint16_t len)
{
    if (len > FR_PAYLOAD) len = FR_PAYLOAD;

    /* full? erase and wrap, carrying erase_count forward */
    if (st->next_idx >= st->slot_count) {
        if (fr_fresh(st, (uint16_t)(st->erase_count + 1u)) != 0) return -1;
    }

    uint16_t seq = st->have_data ? (uint16_t)(st->cur_seq + 1u) : 1u;

    uint8_t slot[FR_SLOT_SIZE];
    memset(slot, 0, FR_SLOT_SIZE);
    slot[0] = (uint8_t)(seq & 0xFFu);
    slot[1] = (uint8_t)(seq >> 8);
    slot[2] = (uint8_t)len;
    memcpy(slot + 3, data, len);
    uint16_t crc = fr_crc16(slot, 30);
    slot[30] = (uint8_t)(crc & 0xFFu);
    slot[31] = (uint8_t)(crc >> 8);

    uint32_t off = fr_slot_off(st, st->next_idx);
    if (st->ops->program(off, slot, FR_SLOT_SIZE) != 0) return -1;

    /* verify readback */
    uint8_t chk[FR_SLOT_SIZE];
    st->ops->read(off, chk, FR_SLOT_SIZE);
    if (memcmp(chk, slot, FR_SLOT_SIZE) != 0) return -1;

    st->have_data = 1; st->cur_seq = seq; st->cur_idx = st->next_idx;
    st->next_idx++; st->used++;
    return 0;
}

uint32_t fr_erase_count(const fr_state_t *st) { return st->erase_count; }
