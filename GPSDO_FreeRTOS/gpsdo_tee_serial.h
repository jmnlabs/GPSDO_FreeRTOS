/* =======================================================================
 * TeeSerial — mirror one logical stream onto two physical UARTs.
 * Part of GPSDO v1.08.68rt
 *
 * When GPSDO_BLUETOOTH_PARALLEL is enabled the firmware talks on USB Serial
 * and the Bluetooth UART (Serial2) at the same time: every byte written goes
 * to both, and input is accepted from whichever port a character arrives on.
 * This lets a USB terminal and a Bluetooth terminal both watch the telemetry
 * and both issue CLI commands, without one excluding the other.
 *
 * It derives from Arduino's Stream so it is a drop-in for the existing
 * OUT_SERIAL / CLI_SERIAL / REPORT_SERIAL macros — the call sites (print,
 * println, write, available, read) are unchanged. Writes fan out to both
 * ports; reads poll port A first, then port B, so commands can come from
 * either terminal. Both ports must already be begin()-run by the caller.
 * ======================================================================= */
#ifndef GPSDO_TEE_SERIAL_H
#define GPSDO_TEE_SERIAL_H

#include <Arduino.h>

class TeeSerial : public Stream
{
public:
    TeeSerial(Stream &a, Stream &b) : _a(a), _b(b) {}

    /* ---- Print/Stream write path: fan out to both ports ---- */
    size_t write(uint8_t c) override
    {
        size_t n = _a.write(c);
        _b.write(c);
        return n;                 /* report the primary port's count */
    }

    size_t write(const uint8_t *buf, size_t size) override
    {
        size_t n = _a.write(buf, size);
        _b.write(buf, size);
        return n;
    }

    /* ---- How much can be written without blocking ----
     *
     * Stream's default returns 0, and a caller that treats 0 as "not now"
     * would silence a tee build completely — which is why this override has
     * to exist the moment anything starts asking. The answer for a tee is the
     * SMALLER of the two: a write goes to both ports, so the one with less
     * room is the one that decides whether the call blocks.
     *
     * A port that reports 0 room but is genuinely absent (an unopened CDC)
     * would otherwise veto output on the port that IS there, so treat "no
     * room at all" on one side as no constraint from that side. Neither
     * HardwareSerial nor USBSerial reports 0 when it is working and idle.
     * (As of build 68: in the current core an unopened or absent CDC answers
     * its queue's free space, not 0 - a zero from USBSerial is a FULL queue,
     * a host that has stopped reading. Which is why the report no longer
     * comes through here; see port_a() below.) */
    int availableForWrite(void) override
    {
        int a = _a.availableForWrite();
        int b = _b.availableForWrite();
        if (a <= 0) return b;
        if (b <= 0) return a;
        return (a < b) ? a : b;
    }

    /* ---- Input path: read from whichever port has data ----
     * Poll A first, then B. available() reports the sum so callers that
     * loop `while (available())` drain both. peek()/read() service A before
     * B, which is fine for line-oriented CLI input. */
    int available() override
    {
        return _a.available() + _b.available();
    }

    int read() override
    {
        int c = _a.read();
        if (c >= 0) return c;
        return _b.read();
    }

    int peek() override
    {
        int c = _a.peek();
        if (c >= 0) return c;
        return _b.peek();
    }

    void flush() override
    {
        _a.flush();
        _b.flush();
    }

    /* ---- The two ports behind the tee ----
     * For a writer that must treat them separately: the telemetry report
     * (build 68, report_write() in gpsdo_tasks.cpp), whose non-blocking
     * guard cannot work through availableForWrite() above - a full CDC
     * queue answers 0, the tee takes that zero as no constraint from the
     * USB side, and write() then pushes the chunk into the full queue. */
    Stream &port_a() { return _a; }
    Stream &port_b() { return _b; }

private:
    Stream &_a;
    Stream &_b;
};

#endif /* GPSDO_TEE_SERIAL_H */
