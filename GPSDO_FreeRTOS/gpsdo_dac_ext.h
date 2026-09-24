/*
 * gpsdo_dac_ext.h — external SPI DAC on the control-voltage output: AD5680.
 *
 * Part of GPSDO v1.07.57rt
 *
 * IMPLEMENTED for the AD5680 (18-bit, external reference), bit-banged GPIO.
 * See gpsdo_dac_ext.cpp for the word format, clocking and the critical section.
 *
 * WHY THIS EXISTS RATHER THAN THE SIGMA-DELTA PATH
 * ------------------------------------------------
 * The sigma-delta DAC was measured and does not deliver what it promised. Its
 * command is 24 bits, but the resolution reaching the oscillator is set by how
 * long the analogue filter averages, not by the command width:
 *
 *     averaging      effective bits
 *     4 096 bits          13.6
 *     262 144 bits        19.0
 *     16 777 216 bits     24.0     (43 seconds at 390 kHz)
 *
 * A filter fast enough not to delay the loop — 0.5 Hz, 0.3 s — averages about
 * 120 000 bits and yields roughly 18 bits. Useful, but two bits over the 16-bit
 * PWM rather than the eight the command width suggests, bought with 6% of the
 * CPU, a precision reference, a CMOS gate and a fourth-order active filter.
 *
 * An external SPI DAC reaches the same 18 bits with none of that: no filter
 * delay, no averaging, no CPU load beyond a few microseconds once per second,
 * and a reference designed for the job. See doc/README for the full comparison.
 *
 * NO HARDWARE SPI IS NEEDED OR AVAILABLE
 * --------------------------------------
 * SPI1 belongs to the TFT and every SPI2 pin on this package is already taken
 * (PB10, PB13, PB15). That does not matter: the DAC is written once per second,
 * so bit-banging 24 bits costs on the order of a microsecond.
 *
 * WHAT IS ACTUALLY FREE ON THIS PACKAGE, checked rather than remembered:
 *
 *   PB14   no alternate function this firmware uses, nothing behind it on the
 *          Black Pill, and brought out on the header - which is why it is now
 *          the SPAN JUMPER's input (GPSDO_SPAN_SENSE, on by default). Free
 *          only on a build with that switched off.
 *   PB2    free of other functions, but it is BOOT1: keep the trace pull-up
 *          free (it is this DAC's MOSI today)
 *   PC14, PC15   free unless the 32.768 kHz crystal is fitted
 *
 * AND THERE IS NO FREE ADC CHANNEL. The converter reaches only PA0-PA7, PB0
 * and PB1 on this package, and with SPI1 carrying the display every one of the
 * ten is taken: PA0 the 5 V rail, PA1 the phase detector, PA2/PA3 the Bluetooth
 * UART, PA4-PA7 the display, PB0 this DAC's clock, PB1 the control voltage. A
 * board that wants to measure something else therefore SHARES an existing
 * channel through a jumper rather than freeing a pin - which is how the V3
 * board reads its voltage reference, on PA0's divider, selected against the
 * 5 V rail.
 *
 * What earlier versions of this list got wrong, each of which would have cost
 * a board revision:
 *
 *   PA4    NOT free - it is the package's native SPI1_NSS and the display's
 *          chip select
 *   PA6    NOT free - TFT_eSPI requires it as TFT_MISO even write-only
 *   PB6, PB7    NOT free - Wire.begin() takes them as I2C1
 *   PA9, PA10   NOT free - Serial1, the GPS
 *   PA8    usable, but a poor choice for any clock or data line: it is
 *          I2C3_SCL and MCO1, and the core has been seen reconfiguring it to
 *          AF4 behind the firmware's back (see the TM1637 note in
 *          gpsdo_tasks.cpp, which has to re-assert it).
 *
 * WHAT STILL HAS TO BE DECIDED
 * ----------------------------
 *   - the part, and with it the word length and format
 *   - the reference, and whether it is internal to the DAC
 *   - the output span against the OCXO's EFC range
 *
 * Candidates considered: AD5680 (18-bit, external reference), AD5683R (16-bit
 * with internal reference and output buffer), AD5761R (16-bit, configurable
 * span including 0-5 V). AD5680 with a REF5045 gives 18 bits over 4.5 V — about
 * 17 uV per step, near 9e-12 fractional on a 5.3 Hz/V oscillator, against
 * 2.7e-11 for the PWM in use today.
 */
#ifndef GPSDO_DAC_EXT_H
#define GPSDO_DAC_EXT_H

#include <stdint.h>

/* Pins. Provisional, and chosen to avoid two traps:
 *
 *   - PB6 and PB7 look free but are NOT. Wire.begin() is called without
 *     arguments, so I2C1 takes its default pins, which on this variant are
 *     exactly those two. Putting the DAC there would break the sensors, the
 *     HT16K33 clock display and anything else on the bus.
 *   - Everything below is plain GPIO with no alternate function this firmware
 *     uses, verified against gpsdo_config.h rather than assumed.
 *
 * Change freely — nothing in the firmware depends on these three.
 *
 * IF A CHOSEN PART NEEDS LDAC, RESET OR A BUSY INPUT, take the pin from the
 * inventory at the top of this file and from nowhere else. There used to be a
 * second list here that named PA4, PA6, PA8, PA9, PA10, PB2, PC14 and PC15 as
 * "also free" — four of those are the exact entries the inventory above is
 * headed "what earlier versions of this list got wrong", reappearing forty
 * lines later in the same file. PA9 and PA10 are Serial1 and the GPS, so they
 * are taken in every build; PA4 and PA6 are the display's chip select and
 * TFT_MISO, so they are taken in every build with a TFT. Believing this line
 * over the inventory is what costs a board revision, which is what the
 * inventory itself warns about.
 *
 * What is actually available, shortest first:
 *
 *   PC14, PC15   free unless the 32.768 kHz crystal footprint is populated.
 *                First choice now.
 *   PB14         the span jumper's input (gpsdo_span.h) — free only with
 *                GPSDO_SPAN_SENSE off, and gpsdo_span.cpp refuses to compile
 *                a DAC pin placed on it while it is on.
 *   PA8          electrically usable, but I2C3_SCL and MCO1, and the core has
 *                been seen reconfiguring it behind the firmware's back — fine
 *                for a level read that re-asserts its mode, poor for anything
 *                clocked.
 *   PB2          free of other functions but it is BOOT1, and it is this DAC's
 *                MOSI today.
 *
 * And there is no free ADC channel at all: a board that wants to measure
 * something else shares an existing one through a jumper. */
#ifndef PIN_DAC_SCK
#define PIN_DAC_SCK   PB0
#endif
#ifndef PIN_DAC_MOSI
#define PIN_DAC_MOSI  PB2
#endif
#ifndef PIN_DAC_CS
#define PIN_DAC_CS    PB4
#endif

/* Command width the loop will be offered once a part is chosen. Left at 18 as
 * the working assumption; gpsdo_dac.cpp scales into it. */
#define DAC_EXT_BITS  18u
#define DAC_EXT_MAX   ((1u << DAC_EXT_BITS) - 1u)

/* Configure the pins and put the DAC into a known state. Returns false if it
 * did not respond, where the part allows that to be detected. */
bool dac_ext_begin(void);

/* Write a code, 0..DAC_EXT_MAX. Expected to be called about once per second. */
void dac_ext_write(uint32_t code);

#endif /* GPSDO_DAC_EXT_H */
