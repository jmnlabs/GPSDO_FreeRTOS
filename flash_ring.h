/* ======================================================================
 * flash_ring.h  —  wear-levelled ring buffer for "live" GPSDO data
 *
 * Part of GPSDO FreeRTOS v0.94
 *
 * Purpose
 * -------
 * The STM32duino EEPROM emulation erases its whole flash sector on every
 * flush(), so frequently auto-saved data (learned drift/damping, LC
 * calibration, last PWM) would wear that sector out in months. This module
 * stores such data in a DEDICATED flash sector as a ring of fixed-size
 * slots. Each save programs the next empty slot (flash 1->0, no erase); the
 * sector is erased only once every 4096 saves, when the ring wraps.
 *
 * Slot layout (32 bytes, word-aligned):
 *   [0..1]   seq    uint16  sequence number (wraps; newest = highest, mod)
 *   [2..3]   len    uint16  payload length in bytes (<= FR_PAYLOAD)
 *   [4..30]  data   27 bytes payload
 *   [30..31] crc16  CRC-16/CCITT over bytes [0..29]
 *
 * A half-written slot (power loss mid-write) fails CRC and is skipped, so
 * the previous good slot is always recoverable.
 *
 * Sector 6 @ 0x08040000 (128 KB) on STM32F411CE. Firmware currently ends
 * mid-sector-5 (~0x0803A918); the whole of sector 5 is left free for growth
 * so the ring never collides with code.
 * ====================================================================== */
#ifndef FLASH_RING_H
#define FLASH_RING_H

#include <stdint.h>
#include <stdbool.h>

#define FR_PAYLOAD    27u    /* usable data bytes per slot           */
#define FR_SLOT_SIZE  32u    /* total slot size (payload + header)    */

/* Initialise: locate newest valid slot, load its payload. Call once at
 * boot before the scheduler starts. Returns true if valid data was found. */
bool     flash_ring_begin(void);

/* Copy the most recent payload into out (up to FR_PAYLOAD bytes). Returns
 * the stored length, or 0 if the ring is empty/uninitialised. */
uint16_t flash_ring_read(uint8_t *out, uint16_t maxlen);

/* Append a new payload as the next ring slot. Erases + wraps automatically
 * when the sector is full. Returns true on verified success. */
bool     flash_ring_write(const uint8_t *data, uint16_t len);

/* Number of sector erase cycles so far (wear indicator, shown by EW). */
uint32_t flash_ring_erase_count(void);

/* Total slots and how many are currently used (for EW diagnostics). */
uint16_t flash_ring_slot_count(void);
uint16_t flash_ring_slots_used(void);

#endif /* FLASH_RING_H */
