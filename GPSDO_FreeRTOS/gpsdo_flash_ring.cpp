/* ======================================================================
 * gpsdo_flash_ring.cpp  —  STM32F411 HAL layer for the live-data + settings ring
 *
 * Part of GPSDO v1.07.57rt
 *
 * This is the ONLY part of the ring buffer that touches real flash. It
 * implements the three primitives the hardware-independent core needs
 * (read / program / erase) against sector 7 of the STM32F411CE, then
 * exposes the small public API declared in gpsdo_flash_ring.h.
 *
 * SAFETY
 * ------
 *  - Always enabled (no runtime toggle). Settings (ES) and live data both
 *    share this ring, distinguished by record type (REC_LIVE / REC_SETTINGS).
 *  - Sector 7 @ 0x08060000 (128 KB). Firmware ends in sector 5 (~0x0803A918);
 *    sectors 0-5 are reserved for code, sector 6 is a growth buffer.
 *    DO NOT move RING_SECTOR without re-checking the map.
 *  - Every write is verified by read-back in the core (fr_write), and every
 *    slot carries a CRC, so a mis-programmed or power-cut write is detected.
 *  - Erase/program run with interrupts kept enabled but are short; the
 *    control loop tolerates the occasional stall because saves are rare
 *    (hysteresis in live_store; ES is user-initiated).
 * ====================================================================== */

#include "gpsdo_flash_ring.h"
#include "gpsdo_flash_ring_core.h"

#include "stm32f4xx_hal.h"
#include <string.h>

/* ---- flash geometry (STM32F411CE) ---- */
#define RING_SECTOR       FLASH_SECTOR_7
#define RING_BASE_ADDR    0x08060000UL
#define RING_SECTOR_LEN   0x00020000UL      /* 128 KB */
#define RING_VOLTAGE      FLASH_VOLTAGE_RANGE_3   /* 2.7-3.6 V → word program */

static fr_state_t s_state;
static bool       s_ready = false;

/* ---- primitives handed to the core ---- */

static void hw_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    /* flash is memory-mapped; a plain copy is the fastest correct read */
    memcpy(dst, (const void *)(RING_BASE_ADDR + off), n);
}

static int hw_program(uint32_t off, const uint8_t *src, uint32_t n)
{
    /* core always calls 32-byte aligned; program word-by-word (4 bytes).
     * Requires the destination to already be 0xFF where we set bits, which
     * the ring guarantees (fresh slots are erased). */
    if ((off & 3u) || (n & 3u)) return -1;      /* must be word aligned */

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGSERR);

    int rc = 0;
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t w;
        memcpy(&w, src + i, 4);                 /* respect alignment of src */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              RING_BASE_ADDR + off + i, w) != HAL_OK) {
            rc = -1;
            break;
        }
    }
    HAL_FLASH_Lock();
    return rc;
}

static int hw_erase(void)
{
    FLASH_EraseInitTypeDef ei;
    ei.TypeErase    = FLASH_TYPEERASE_SECTORS;
    ei.Sector       = RING_SECTOR;
    ei.NbSectors    = 1;
    ei.VoltageRange = RING_VOLTAGE;
    ei.Banks        = 0;   /* unused for F411 */

    uint32_t err = 0;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGSERR);
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&ei, &err);
    HAL_FLASH_Lock();
    return (st == HAL_OK && err == 0xFFFFFFFFU) ? 0 : -1;
}

static const fr_ops_t OPS = {
    hw_read, hw_program, hw_erase, RING_SECTOR_LEN
};

/* ---- public API ---- */

bool flash_ring_begin(void)
{
    int r = fr_begin(&s_state, &OPS);
    s_ready = true;
    return (r == 1);
}

uint32_t flash_ring_base_addr(void) { return RING_BASE_ADDR; }
uint8_t  flash_ring_sector_no(void) { return (uint8_t)RING_SECTOR; }

bool flash_ring_wipe(void)
{
    /* Cold-restart erase: physically erase the ring sector, then re-init so a
     * fresh header is laid down. After this the ring reads as empty — the next
     * boot (or the next settings_recall) finds no valid slot and falls back to
     * compile-time defaults. Used by the CR command. Returns true on success. */
    if (hw_erase() != 0) return false;
    int r = fr_begin(&s_state, &OPS);   /* reformats the just-erased sector */
    s_ready = true;
    return (r == 1);
}

uint16_t flash_ring_read_newest(uint8_t type, uint8_t *out, uint16_t maxlen)
{
    if (!s_ready) return 0;
    return fr_read_newest(&s_state, type, out, maxlen);
}

bool flash_ring_write(uint8_t type, const uint8_t *data, uint16_t len)
{
    if (!s_ready) return false;
    return (fr_write(&s_state, type, data, len) == 0);
}

uint32_t flash_ring_erase_count(void) { return s_ready ? fr_erase_count(&s_state) : 0; }
uint16_t flash_ring_slot_count(void)  { return s_ready ? s_state.slot_count : 0; }
uint16_t flash_ring_slots_used(void)  { return s_ready ? s_state.used : 0; }
