/* ======================================================================
 * gpsdo_flash_ring_core.h  —  hardware-INDEPENDENT ring-buffer logic
 *
 * Part of GPSDO v1.07.57rt
 *
 * This is the pure algorithm: signature/version validation, slot packing,
 * CRC, newest-slot selection, wrap handling and garbage detection. It knows
 * nothing about STM32 flash — it operates through three function pointers
 * (read / program / erase) supplied by the caller. The same code is unit-
 * tested on a PC with a RAM-backed "flash" and driven on-target by a thin
 * HAL layer (gpsdo_flash_ring.cpp), so the risky part (real flash writes) is a
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
 * Slot layout (FR_SLOT_SIZE = 512 bytes, v2+):
 *   [0..1]     seq    uint16  (monotonic; wraps at 0xFFFF, handled by distance)
 *   [2]        type   uint8   record type (REC_LIVE / REC_SETTINGS / ...)
 *   [3]        rsv    uint8   reserved, zero
 *   [4..5]     len    uint16  payload length (<= FR_PAYLOAD)
 *   [4..509]   data   506 bytes payload
 *   [510..511] crc16  over [0..509]
 *
 * Record types coexist in one ring (one erase domain): live_store and
 * settings both write slots, distinguished by the type byte. fr_read_newest
 * scans for the newest valid slot of the requested type.
 *
 * v1 (32-byte slots, no type byte) is detected via FR_FMT_VER mismatch at
 * fr_begin() and reformatted — old live data do not survive the upgrade.
 * ====================================================================== */
#ifndef GPSDO_FLASH_RING_CORE_H
#define GPSDO_FLASH_RING_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FR_MAGIC0 'G'
/* Magic bumped when the slot layout changed (8-bit length -> 16-bit, so a
 * settings block over 255 B stops truncating). An older ring fails the
 * magic test on begin() and is reformatted, which is the wanted upgrade
 * path: stale records in the previous layout would decode as garbage. */
#define FR_MAGIC_STR "GPSDOLI2"
#define FR_FMT_VER   2u

/* Record type IDs — coexist in the same ring. Don't reorder: the values
 * are part of the on-flash format. */
#define FR_REC_LIVE     0u
#define FR_REC_SETTINGS 1u

#define FR_HDR_SIZE   16u
#define FR_SLOT_SIZE  512u
#define FR_SLOT_HDR   6u     /* per-slot header: seq(2) type(1) rsv(1) len(2) */
#define FR_PAYLOAD    504u   /* FR_SLOT_SIZE - FR_SLOT_HDR - CRC(2)          */

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
 * testable). Populated by fr_begin(). Tracks ring geometry and the next free
 * slot; per-type newest lookups are done on demand by fr_read_newest(). */
typedef struct {
    const fr_ops_t *ops;
    uint16_t slot_count;    /* usable slots after the header             */
    uint16_t used;          /* slots currently written                   */
    uint16_t next_idx;      /* index of next free slot                   */
    uint16_t cur_seq;       /* highest seq seen (for next write's seq)   */
    uint16_t erase_count;   /* from header                               */
    int      have_data;     /* 1 if at least one valid slot exists       */
} fr_state_t;

uint16_t fr_crc16(const uint8_t *p, uint32_t n);

/* Validate header + scan slots. If the header is foreign/blank, erases the
 * sector and writes a fresh header (erase_count preserved if it was a valid
 * older header, else 0). Returns 1 if at least one valid slot exists. */
int  fr_begin(fr_state_t *st, const fr_ops_t *ops);
int  fr_begin_fresh_count(fr_state_t *st, uint16_t prev_ec);

/* Copy newest payload of the requested type to out (<= maxlen).
 * Scans the ring for the highest-seq valid slot whose type matches.
 * Returns payload length, or 0 if no slot of that type exists. */
uint16_t fr_read_newest(fr_state_t *st, uint8_t type,
                        uint8_t *out, uint16_t maxlen);

/* Append payload as the next slot of the given type; wraps (erase) when full.
 * Returns 0 ok. */
int  fr_write(fr_state_t *st, uint8_t type,
              const uint8_t *data, uint16_t len);

uint32_t fr_erase_count(const fr_state_t *st);

#ifdef __cplusplus
}
#endif
#endif /* GPSDO_FLASH_RING_CORE_H */
