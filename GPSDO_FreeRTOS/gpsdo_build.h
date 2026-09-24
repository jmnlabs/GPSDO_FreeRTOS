/*
 * gpsdo_build.h — which binary is actually running?
 *
 * Part of GPSDO v1.07.57rt
 *
 * WHY THIS EXISTS
 * ---------------
 * The banner printed __DATE__ and __TIME__ and was believed, and on 26.08 it
 * lied: two captures carried the same compile stamp from two different builds.
 * Nobody had done anything wrong. __DATE__ is baked into whichever translation
 * unit mentions it — the sketch — and the Arduino builder does not recompile a
 * translation unit whose sources have not changed. Edit gpsdo_algorithms.cpp,
 * upload, and the sketch's object file is reused with last week's timestamp
 * still inside it. The stamp is honest about when the SKETCH was compiled and
 * says nothing about the rest of the firmware.
 *
 * That cost an hour of arguing with a log that was right all along, so the fix
 * is not a better timestamp. It is an identity that CANNOT be stale:
 *
 *     a CRC-32 of the flash image, computed at boot from the flash itself.
 *
 * It changes if any byte of any translation unit changed, it cannot be reused
 * from a cache because nothing computes it until the board is running, and two
 * boards flashed with the same binary print the same number. When a log and a
 * memory disagree, this is the one to trust.
 *
 * WHAT IT COVERS. Vector table, code, read-only data and the initialisers for
 * .data — that is, every byte the programmer wrote to flash. Not RAM, not the
 * settings ring in sector 7 (deliberately: the same firmware with different
 * settings is the same firmware).
 *
 * COST. About 2.5 ms once, at boot, and 64 bytes of table. The bitwise loop was
 * 10 ms and the 256-entry table was a kilobyte of flash for 7 ms saved; the
 * nibble table is the middle one and nobody will notice it either way.
 */
#ifndef GPSDO_BUILD_H
#define GPSDO_BUILD_H

#include <stdint.h>

/* CRC-32 (the ordinary reflected one, poly 0xEDB88320) of the whole flash
 * image. Computed on the first call and remembered. Returns 0 if the image
 * bounds could not be determined — see fw_image_bytes(). */
uint32_t fw_image_crc32(void);

/* Length of the image in bytes, or 0 if the linker symbols this relies on
 * (_sidata, _sdata, _edata) were not present. They are weak references, so a
 * toolchain that does not define them links and reports zero rather than
 * failing to build: an identity that is unavailable is a nuisance, one that
 * stops the firmware from linking is a fault. */
uint32_t fw_image_bytes(void);

/* The compile stamp, "compiled YYYY-MM-DD HH:MM:SS  build N", filled once at
 * boot by the sketch and empty until then.
 *
 * IT HAS TO COME FROM THE SKETCH. __DATE__ and __TIME__ are baked into whichever
 * translation unit mentions them, and the sketch is the only one the builder is
 * forced to recompile (that is what gpsdo_build_id.h is for). Composing it there and
 * publishing it here gives the banner and the V command ONE string rather than
 * two that can drift apart - and lets the tuner ask for it on connect instead of
 * having to have caught the boot banner as it scrolled past. */
#define FW_STAMP_MAX 48
extern char g_fw_stamp[FW_STAMP_MAX];

/* The firmware's version as it names itself, "v1.07.57rt": PROGRAM_VERSION,
 * then "." and the build, then "rt" (see gpsdo_config.h). Defined in the
 * sketch for the same reason as the stamp — only the sketch sees BUILD_SERIAL
 * — but as a constant rather than something filled in at boot, so it is
 * already right for whichever display or command reads it first. */
extern const char g_fw_version[];

#endif /* GPSDO_BUILD_H */
