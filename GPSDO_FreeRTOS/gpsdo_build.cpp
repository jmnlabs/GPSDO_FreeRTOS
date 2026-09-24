/*
 * gpsdo_build.cpp — CRC-32 of the running flash image. See the header for why
 * the compile timestamp alone was not enough.
 *
 * Part of GPSDO v1.07.57rt
 */
#include "gpsdo_build.h"

/* Linker script symbols. The STM32 core's ldscript places the initialisers for
 * .data immediately after .text/.rodata in flash, so
 *
 *     image = [ image_start .. _sidata + (_edata - _sdata) )
 *
 * is the whole programmed region, exactly. Declared WEAK on purpose: if a
 * toolchain names them differently the references resolve to zero, the bounds
 * check below fails, and the firmware reports "no image identity" instead of
 * refusing to link. */
extern uint32_t _sidata __attribute__((weak));
extern uint32_t _sdata  __attribute__((weak));
extern uint32_t _edata  __attribute__((weak));

/* The vector table sits at the start of the image. ST's startup file names it
 * g_pfnVectors; if that is absent, fall back to the F4 flash base, which is
 * where a BlackPill without a bootloader puts it. */
extern uint32_t g_pfnVectors __attribute__((weak));

#define FLASH_ORIGIN   0x08000000UL
#define FLASH_MAX      (512UL * 1024UL)      /* F411CE has 512 K */

static uint32_t s_crc;
static uint32_t s_len;
static bool     s_done;

/* Nibble table for the reflected CRC-32, poly 0xEDB88320. 64 bytes of flash,
 * four bits per step. */
static const uint32_t CRC_NIB[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

static void fw_measure(void)
{
    if (s_done) return;
    s_done = true;
    s_crc  = 0;
    s_len  = 0;

    /* Weak and undefined resolves to address zero; the compiler is required to
     * keep the test for a weak symbol, which is the whole reason they are weak
     * rather than declared normally. */
    if (&_sidata == 0 || &_sdata == 0 || &_edata == 0) return;

    uint32_t start = (&g_pfnVectors != 0) ? (uint32_t)&g_pfnVectors
                                          : FLASH_ORIGIN;
    uint32_t data_len = (uint32_t)&_edata - (uint32_t)&_sdata;
    uint32_t end      = (uint32_t)&_sidata + data_len;

    /* Sanity, because a wrong answer here is worse than no answer: the region
     * has to lie inside flash, run forwards, and be a plausible size for this
     * firmware. Below 4 K means the symbols meant something else. */
    if (end <= start) return;
    if (start < FLASH_ORIGIN || start >= FLASH_ORIGIN + FLASH_MAX) return;
    if (end > FLASH_ORIGIN + FLASH_MAX) return;
    if ((end - start) < 4096UL) return;

    s_len = end - start;

    const uint8_t *p = (const uint8_t *)start;
    uint32_t c = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < s_len; i++) {
        uint8_t b = p[i];
        c = CRC_NIB[(c ^ b) & 0x0F] ^ (c >> 4);
        c = CRC_NIB[(c ^ (b >> 4)) & 0x0F] ^ (c >> 4);
    }
    s_crc = c ^ 0xFFFFFFFFUL;
}

char g_fw_stamp[FW_STAMP_MAX] = { 0 };   /* the sketch fills it at boot */

uint32_t fw_image_crc32(void) { fw_measure(); return s_crc; }
uint32_t fw_image_bytes(void) { fw_measure(); return s_len; }
