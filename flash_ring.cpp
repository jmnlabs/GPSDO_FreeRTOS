/* ======================================================================
 * flash_ring.cpp  —  STM32F411 HAL layer for the live-data ring buffer
 *
 * Part of GPSDO FreeRTOS v0.95
 *
 * This is the ONLY part of the ring buffer that touches real flash. It
 * implements the three primitives the hardware-independent core needs
 * (read / program / erase) against sector 6 of the STM32F411CE, then
 * exposes the small public API declared in flash_ring.h.
 *
 * SAFETY
 * ------
 *  - Enabled at runtime via g_flash_ring_enable (FR 0|1, saved in EEPROM).
 *    When disabled, no flash operation is issued and the API reports
 *    empty/disabled, so live_store no-ops. Always compiled — avoids the
 *    build_opt.h caching problems a compile flag would bring.
 *  - Sector 6 @ 0x08040000 (128 KB). Firmware currently ends mid-sector-5
 *    (~0x0803A918); the whole of sector 5 is reserved for growth so the ring
 *    can never overlap code. DO NOT move RING_SECTOR without re-checking the
 *    map ("Sketch uses NNNNN bytes" must stay < 0x08040000 - 0x08000000).
 *  - Every write is verified by read-back in the core (fr_write), and every
 *    slot carries a CRC, so a mis-programmed or power-cut write is detected.
 *  - Erase/program run with interrupts kept enabled but are short; the
 *    control loop tolerates the occasional stall because live-data saves are
 *    rare (hysteresis in the caller).
 * ====================================================================== */

#include "flash_ring.h"
#include "flash_ring_core.h"

/* Always compiled now; enabled at runtime via g_flash_ring_enable (FR 0|1,
 * saved in EEPROM). When disabled, no flash operation is ever issued — the
 * public API returns "empty/disabled" and the caller (live_store) no-ops.
 * This avoids the notorious build_opt.h caching problems of a compile flag. */
#include "stm32f4xx_hal.h"
#include <string.h>

/* runtime enable — declared in gpsdo_control.cpp, defaulted true, EEPROM 232 */
extern volatile bool g_flash_ring_enable;

/* ---- flash geometry (STM32F411CE) ---- */
#define RING_SECTOR       FLASH_SECTOR_6
#define RING_BASE_ADDR    0x08040000UL
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

/* ---- public API (runtime-gated by g_flash_ring_enable) ---- */

bool flash_ring_begin(void)
{
    if (!g_flash_ring_enable) { s_ready = false; return false; }
    int r = fr_begin(&s_state, &OPS);
    s_ready = true;
    return (r == 1);
}

uint16_t flash_ring_read(uint8_t *out, uint16_t maxlen)
{
    if (!s_ready || !g_flash_ring_enable) return 0;
    return fr_read(&s_state, out, maxlen);
}

bool flash_ring_write(const uint8_t *data, uint16_t len)
{
    if (!s_ready || !g_flash_ring_enable) return false;
    return (fr_write(&s_state, data, len) == 0);
}

uint32_t flash_ring_erase_count(void) { return (s_ready && g_flash_ring_enable) ? fr_erase_count(&s_state) : 0; }
uint16_t flash_ring_slot_count(void)  { return (s_ready && g_flash_ring_enable) ? s_state.slot_count : 0; }
uint16_t flash_ring_slots_used(void)  { return (s_ready && g_flash_ring_enable) ? s_state.used : 0; }
