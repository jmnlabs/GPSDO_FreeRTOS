/* ======================================================================
 * gpsdo_flash_ring.h  —  wear-levelled ring buffer for live + settings data
 *
 * Part of GPSDO v1.07.57rt
 *
 * Purpose
 * -------
 * Stores BOTH auto-saved "live" data (learned drift/damping, LC calibration,
 * last PWM) AND user-saved settings (PID gains, TZ, flags, LTIC params) in a
 * dedicated flash sector as a ring of fixed-size slots. Each record carries a
 * type byte so the two coexist in one erase domain. Saves program the next
 * empty slot (flash 1->0, no erase); the sector is erased only once every
 * ~256 saves, when the ring wraps. The two record types are:
 *
 *   REC_LIVE     live_store: LRN drift/damp, LTIC calibration, last PWM
 *   REC_SETTINGS settings_store: ES command — PID/TZ/flags/LTIC params
 *
 * Slot layout (512 bytes, word-aligned, fmt_ver=2):
 *   [0..1]     seq    uint16  sequence number (wraps; newest = highest, mod)
 *   [2]        type   uint8   record type (REC_LIVE / REC_SETTINGS)
 *   [3]        rsv    uint8   reserved, zero
 *   [4..5]     len    uint16  payload length in bytes (<= FR_PAYLOAD)
 *   [4..509]   data   506 bytes payload
 *   [510..511] crc16  CRC-16/CCITT over bytes [0..509]
 *
 * A half-written slot (power loss mid-write) fails CRC and is skipped, so the
 * previous good slot is always recoverable.
 *
 * Sector 7 @ 0x08060000 (128 KB) on STM32F411CE. Firmware ends in sector 5
 * (~0x0803A918); sectors 0-5 are code, sector 6 is a growth buffer, sector 7
 * is the ring. ~256 slots of 512 B; ~96 years endurance at ~73 saves/day.
 * ====================================================================== */
#ifndef GPSDO_FLASH_RING_H
#define GPSDO_FLASH_RING_H

#include <stdint.h>
#include <stdbool.h>

/* Slot size — also defined in gpsdo_flash_ring_core.h, mirrored here for callers
 * that want to size their payload buffers without including the core. */
#define FR_PAYLOAD    504u   /* usable data bytes per slot (6-B slot hdr)*/
#define FR_SLOT_SIZE  512u   /* total slot size (payload + header)        */

/* Record type IDs — see gpsdo_flash_ring_core.h for the canonical definition. */
#define REC_LIVE      0u
#define REC_SETTINGS  1u
/* Algorithm 13 (Kalman) parameters. Its own record rather than a field in the
 * settings block: that block has no padding left, and growing it would force a
 * SETTINGS_VER bump that throws away everyone's PID, LC and timezone for three
 * numbers. A new type costs nothing and an older build simply never reads it. */
#define REC_A13       2u

/* Initialise: validate header, scan slots. Call once at boot before the
 * scheduler starts. Returns true if at least one valid slot exists. */
bool     flash_ring_begin(void);

/* Copy the most recent payload of the given type into out (up to maxlen).
 * Returns the stored length, or 0 if no slot of that type exists. */
uint16_t flash_ring_read_newest(uint8_t type, uint8_t *out, uint16_t maxlen);

/* Append a new payload of the given type as the next ring slot. Erases +
 * wraps automatically when the sector is full. Returns true on verified
 * success. */
bool     flash_ring_write(uint8_t type, const uint8_t *data, uint16_t len);

/* Number of sector erase cycles so far (wear indicator, shown by EW). */
uint32_t flash_ring_erase_count(void);

/* Total slots and how many are currently used (for EW diagnostics). */
uint16_t flash_ring_slot_count(void);
uint16_t flash_ring_slots_used(void);

/* Physical location of the ring, for diagnostics. Exposed as accessors rather
 * than left to each caller's own constants: the EW command used to print a
 * hardcoded "sector 6, 0x08040000" while the ring had always been in sector 7,
 * so the one place an operator looks was the one place that lied. */
uint32_t flash_ring_base_addr(void);
uint8_t  flash_ring_sector_no(void);

/* Erase and reformat the ring sector (cold-restart wipe). After this the ring
 * reads empty and the next recall falls back to defaults. Returns true on OK. */
bool     flash_ring_wipe(void);

#endif /* GPSDO_FLASH_RING_H */
