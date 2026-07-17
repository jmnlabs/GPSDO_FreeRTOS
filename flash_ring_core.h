/* ======================================================================
 * flash_ring_core.h  —  hardware-INDEPENDENT ring-buffer logic
 *
 * Part of GPSDO FreeRTOS v0.95
 *
 * This is the pure algorithm: signature/version validation, slot packing,
 * CRC, newest-slot selection, wrap handling and garbage detection. It knows
 * nothing about STM32 flash — it operates through three function pointers
 * (read / program / erase) supplied by the caller. The same code is unit-
 * tested on a PC with a RAM-backed "flash" and driven on-target by a thin
 * HAL layer (flash_ring.cpp), so the risky part (real flash writes) is a
 * small, isolated shim around logic that is already proven.
 *
 * Sector layout:
 *   [0 .. HDR_SIZE-1]         sector header (signature, version, crc)
 *   [HDR_SIZE .. sectorlen-1] ring of FR_SLOT_SIZE-byte slots
 *
 * Sector header (16 bytes):
 *   [0..7]   magic  "GPSDOLIV"
 *   [8]      fmt_ver format version (bump if slot layout changes)
 *   [9..11]  rsvd    0
 *   [12..13] erase_count (uint16, survives via read-modify-write on erase)
 *   [14..15] crc16   over [0..13]
 *
 * If the header magic/version/crc do not match on begin(), the sector is
 * treated as foreign/garbage/blank: the caller erases it and starts a fresh
 * ring. This makes the firmware robust to full-chip-erase, sector-only
 * programming, first boot, and leftover production junk alike.
 *
 * Slot layout (FR_SLOT_SIZE = 32 bytes):
 *   [0..1]   seq    uint16  (monotonic; wraps at 0xFFFF, handled by distance)
 *   [2]      len    uint8   payload length (<= FR_PAYLOAD)
 *   [3..29]  data   27 bytes payload
 *   [30..31] crc16  over [0..29]
 * ====================================================================== */
#ifndef FLASH_RING_CORE_H
#define FLASH_RING_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FR_MAGIC0 'G'
#define FR_MAGIC_STR "GPSDOLIV"
#define FR_FMT_VER   1u

#define FR_HDR_SIZE   16u
#define FR_SLOT_SIZE  32u
#define FR_PAYLOAD    27u

/* Caller-supplied flash primitives. All offsets are RELATIVE to the start
 * of the ring sector (0 == first header byte). */
typedef struct {
    /* copy n bytes from sector offset off into dst */
    void (*read)(uint32_t off, uint8_t *dst, uint32_t n);
    /* program n bytes (flash 1->0) from src to sector offset off; returns 0
     * on success. Must be word-aligned on real hardware; the core always
     * calls it with 32-byte-aligned offsets and lengths. */
    int  (*program)(uint32_t off, const uint8_t *src, uint32_t n);
    /* erase the whole ring sector to 0xFF; returns 0 on success */
    int  (*erase)(void);
    uint32_t sector_len;   /* total sector size in bytes (e.g. 131072) */
} fr_ops_t;

/* Runtime state, owned by the caller (so the core stays reentrant-free but
 * testable). Populated by fr_begin(). */
typedef struct {
    const fr_ops_t *ops;
    uint16_t slot_count;    /* usable slots after the header             */
    uint16_t used;          /* slots currently written                   */
    uint16_t next_idx;      /* index of next free slot                   */
    uint16_t cur_seq;       /* seq of newest valid slot (0 if none)      */
    uint16_t erase_count;   /* from header                               */
    int      have_data;     /* 1 if a valid newest slot was found        */
    uint16_t cur_idx;       /* index of newest valid slot                */
} fr_state_t;

uint16_t fr_crc16(const uint8_t *p, uint32_t n);

/* Validate header + scan slots. If the header is foreign/blank, erases the
 * sector and writes a fresh header (erase_count preserved if it was a valid
 * older header, else 0). Returns 1 if a valid newest payload exists. */
int  fr_begin(fr_state_t *st, const fr_ops_t *ops);
int  fr_begin_fresh_count(fr_state_t *st, uint16_t prev_ec);

/* Copy newest payload to out (<= maxlen). Returns payload length or 0. */
uint16_t fr_read(fr_state_t *st, uint8_t *out, uint16_t maxlen);

/* Append payload as the next slot; wraps (erase) when full. Returns 0 ok. */
int  fr_write(fr_state_t *st, const uint8_t *data, uint16_t len);

uint32_t fr_erase_count(const fr_state_t *st);

#ifdef __cplusplus
}
#endif
#endif /* FLASH_RING_CORE_H */
