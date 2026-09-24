/*
 * gpsdo_dac_ext.cpp — AD5680 external 18-bit DAC, bit-banged GPIO. No hardware SPI.
 *
 * Part of GPSDO v1.07.57rt
 *
 * Pins (Dan Wiering's PCB, chosen 2026-08): CS=PB4, SCK=PB0, MOSI=PB2.
 * PB2 doubles as BOOT1 — sampled only at reset, and the AD5680's DIN never
 * pulls it, so with no pull-up on the MOSI trace boot is unaffected.
 *
 * Word format (datasheet, cross-checked against the Arduino reference
 * library's updateDevice()): 24 bits, MSB first — four don't-care zeros,
 * the 18-bit code, two don't-care zeros: word = code << 2. All-zero command
 * bits mean "write code, normal operation"; power-down is never used here.
 *
 * Clocking: DIN is captured on the SCLK FALLING edge and the register
 * latches on the SYNC (=CS) RISING edge after the 24th falling edge. The
 * bit sequence is therefore: set DIN while SCLK is low, pulse SCLK high,
 * drop it low, next bit; after bit 0, return CS high. Datasheet timing
 * minimums are 20 ns-class; one digitalWrite is ~100 ns of GPIO on this
 * clock, so the bit-bang meets them several times over with no delays.
 *
 * The 24-bit shift-out runs with interrupts masked — ~20 us, called about
 * once per second. That is shorter than one character time on the CLI UART
 * and far below anything the TIM2 counter or the PPS capture notices (both
 * latch in timer hardware, not in the ISR). CMSIS __disable_irq() is used
 * rather than taskENTER_CRITICAL() so this file stands alone, with no
 * FreeRTOS include dependency. NOT safe to call from an ISR.
 */
#include <Arduino.h>
#include "gpsdo_config.h"
#include "gpsdo_dac_ext.h"

#if defined(GPSDO_DAC_EXT)

bool dac_ext_begin(void)
{
    pinMode(PIN_DAC_SCK,  OUTPUT);
    pinMode(PIN_DAC_MOSI, OUTPUT);
    pinMode(PIN_DAC_CS,   OUTPUT);
    digitalWrite(PIN_DAC_SCK,  LOW);
    digitalWrite(PIN_DAC_MOSI, LOW);
    digitalWrite(PIN_DAC_CS,   HIGH);  /* idle high: latched, no word open */
    return true;   /* the AD5680 is write-only — nothing to detect */
}

void dac_ext_write(uint32_t code)
{
    uint32_t word = (code & DAC_EXT_MAX) << 2;   /* [0000][D17..D0][00] */

    __disable_irq();
    digitalWrite(PIN_DAC_CS, LOW);               /* SYNC low: register open */
    for (int8_t i = 23; i >= 0; i--) {
        digitalWrite(PIN_DAC_MOSI, ((word >> i) & 1u) ? HIGH : LOW);
        digitalWrite(PIN_DAC_SCK, HIGH);
        digitalWrite(PIN_DAC_SCK, LOW);          /* DIN clocks in here */
    }
    digitalWrite(PIN_DAC_MOSI, LOW);
    digitalWrite(PIN_DAC_CS, HIGH);              /* rising SYNC latches */
    __enable_irq();
}

#endif /* GPSDO_DAC_EXT */
