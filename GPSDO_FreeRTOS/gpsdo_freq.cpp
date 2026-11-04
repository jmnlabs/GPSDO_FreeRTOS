/**
 * gpsdo_freq.cpp — vFreqRelayTask — frequency measurement processing
 *
 * Part of GPSDO v1.08.65rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 *
 * Highest-priority task.  Woken by the TIM2 CH3 capture ISR via xPpsQueue
 * (with LTIC, it wakes itself just before the edge it predicts, takes the
 * detector burst on it, and then finds the ISR's events waiting).
 * Processes PPS events: updates a 20000-sample ring buffer, maintains
 * cumulative sums for 10/100/1000/10000 s averages, and publishes the
 * compact FreqSnap_t display snapshot under xFreqMutex.
 */

#include "gpsdo_config.h"
#include "gpsdo_state.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * flush_ring_buffers — reset all frequency counting state
 * Must be called with xFreqMutex held.
 * ----------------------------------------------------------------------- */
static void flush_ring_buffers_locked(FreqData_t *f)
{
    f->prevfcount64       = 0;
    f->previousfcount     = 0;
    f->buf_newest         = 0;
    f->cumul10            = 0;
    f->cumul100           = 0;
    f->cumul1000          = 0;
    f->cumul10000         = 0;
    f->cumul20000         = 0;
    f->full10             = false;
    f->full100            = false;
    f->full1000           = false;
    f->full10000          = false;
    f->full20000          = false;
    f->avg10              = 0.0;
    f->avg100             = 0.0;
    f->avg1000            = 0.0;
    f->avg10000           = 0.0;
    f->avg20000           = 0.0;
    f->ppscount           = 0;
    f->flush_requested    = false;
    f->must_adjust        = false;
    memset(f->circbuf, 0, sizeof(f->circbuf));
}

/* -----------------------------------------------------------------------
 * log_freq_offset — update ring buffer and cumulative sums
 * Must be called with xFreqMutex held.
 * ----------------------------------------------------------------------- */
static void log_freq_offset(FreqData_t *f)
{
    const int8_t  new_offset = f->instant_offset;
    const uint32_t idx       = f->buf_newest;

    int8_t old_20k = f->full20000 ? f->circbuf[idx] : 0;

    f->circbuf[idx] = new_offset;

    f->cumul10    += new_offset;
    f->cumul100   += new_offset;
    f->cumul1000  += new_offset;
    f->cumul10000 += new_offset;
    f->cumul20000 += new_offset;

    if (f->full10) {
        uint32_t i = (idx >= 10) ? (idx - 10) : (idx - 10 + CIRCBUF_SIZE);
        f->cumul10 -= f->circbuf[i];
    }
    if (f->full100) {
        uint32_t i = (idx >= 100) ? (idx - 100) : (idx - 100 + CIRCBUF_SIZE);
        f->cumul100 -= f->circbuf[i];
    }
    if (f->full1000) {
        uint32_t i = (idx >= 1000) ? (idx - 1000) : (idx - 1000 + CIRCBUF_SIZE);
        f->cumul1000 -= f->circbuf[i];
    }
    if (f->full10000) {
        uint32_t i = (idx >= 10000) ? (idx - 10000) : (idx - 10000 + CIRCBUF_SIZE);
        f->cumul10000 -= f->circbuf[i];
    }
    if (f->full20000) {
        f->cumul20000 -= old_20k;
    }

    f->buf_newest++;

    if (f->buf_newest == 10)    f->full10    = true;
    if (f->buf_newest == 100)   f->full100   = true;
    if (f->buf_newest == 1000)  f->full1000  = true;
    if (f->buf_newest == 10000) f->full10000 = true;
    if (f->buf_newest >= CIRCBUF_SIZE) {
        f->full20000  = true;
        f->buf_newest = 0;
    }
}

/* -----------------------------------------------------------------------
 * update_snap — copy display-relevant fields to FreqSnap_t
 * Must be called with xFreqMutex held.
 * This is ~80 bytes, safe to copy from any task stack.
 * ----------------------------------------------------------------------- */
static void update_snap(const FreqData_t *f)
{
    gFreqSnap.fcount64    = f->fcount64;
    gFreqSnap.calcfreq64  = f->calcfreq64;
    gFreqSnap.calcfreqint = f->calcfreqint;
    gFreqSnap.avg10       = f->avg10;
    gFreqSnap.avg100      = f->avg100;
    gFreqSnap.avg1000     = f->avg1000;
    gFreqSnap.avg10000    = f->avg10000;
    gFreqSnap.avg20000    = f->avg20000;
    gFreqSnap.full10      = f->full10;
    gFreqSnap.full100     = f->full100;
    gFreqSnap.full1000    = f->full1000;
    gFreqSnap.full10000   = f->full10000;
    gFreqSnap.full20000   = f->full20000;
    gFreqSnap.ppscount    = f->ppscount;
}

/* -----------------------------------------------------------------------
 * gpsdo_calc_averages — exposed to ControlTask
 * Must be called with xFreqMutex held.
 * ----------------------------------------------------------------------- */
void gpsdo_calc_averages(FreqData_t *f)
{
    if (f->full10)    f->avg10    = (double)BASE_FREQ + (double)f->cumul10    / 10.0;
    if (f->full100)   f->avg100   = (double)BASE_FREQ + (double)f->cumul100   / 100.0;
    if (f->full1000)  f->avg1000  = (double)BASE_FREQ + (double)f->cumul1000  / 1000.0;
    if (f->full10000) f->avg10000 = (double)BASE_FREQ + (double)f->cumul10000 / 10000.0;
    if (f->full20000) f->avg20000 = (double)BASE_FREQ + (double)f->cumul20000 / 20000.0;
}

/* -----------------------------------------------------------------------
 * vFreqRelayTask
 * ----------------------------------------------------------------------- */
extern bool ltic_edge_sync(void);
extern void ltic_read_fast(bool synced);

void vFreqRelayTask(void *pvParameters)
{
    (void)pvParameters;
    PpsEvent_t evt;

    for (;;)
    {
#ifdef GPSDO_LTIC
        /* THE LTIC READ IS TAKEN AT THE EDGE ITSELF. The Kaashoek ramp on C13
         * charges during the FF pulse (width = phase: about 1 µs at lock,
         * ~3 µs at the edge of the band); left alone it would leak away in
         * tens of ms, and the read discharges it. ltic_edge_sync() sleeps
         * until just before the edge it predicts from the last two, catches
         * it with interrupts masked and takes the burst on it - on a quiet
         * chip, from 5 µs after the edge - and returns once the capture
         * interrupt has run. When it cannot (the first edges, an early or
         * missed one, LRM 0) it returns false at once, and the burst is taken
         * below as soon as the interrupt wakes us, as it always was. */
        const bool synced = ltic_edge_sync();
#endif
        /* Block until ISR notifies us (2 s safety timeout); after a burst
         * taken at the edge its notification is already here. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));

#ifdef GPSDO_LTIC
        /* The reading - from that burst, or from one taken now - before the
         * frequency work, so it and the receiver's qErr latch belong to this
         * pulse. Right after it ltic_read_fast() discharges the cap through
         * PA1 (GPSDO_LTIC_ACTIVE_RESET), so the next pulse charges it from
         * 0 V whatever the temperature. */
        ltic_read_fast(synced);
#endif

        /* Drain all events posted while we were computing */
        while (xQueueReceive(xPpsQueue, &evt, 0) == pdTRUE)
        {
            if (xSemaphoreTake(xFreqMutex, pdMS_TO_TICKS(5)) != pdTRUE)
                continue;

            FreqData_t *f = &gFreq;

            if (f->flush_requested) {
                flush_ring_buffers_locked(f);
                update_snap(f);
                xSemaphoreGive(xFreqMutex);
                continue;
            }

            f->lsfcount = evt.ccr_value;

            /* Track TIM2 32-bit overflow */
            if (f->lsfcount < f->previousfcount) {
                f->tim2_overflow_cnt++;
                /* If overflow ISR did not fire, this is an error — tolerate */
            }

            f->fcount64 = ((uint64_t)f->tim2_overflow_cnt << 32) | f->lsfcount;

            if (f->fcount64 > f->prevfcount64) {
                uint64_t diff = f->fcount64 - f->prevfcount64;
                if (diff > FREQ_LOWER && diff < FREQ_UPPER) {
                    f->calcfreq64     = diff;
                    f->calcfreqint    = (uint32_t)diff;
                    f->instant_offset = (int16_t)((int64_t)diff - (int64_t)BASE_FREQ);
                    log_freq_offset(f);
                    f->ppscount++;
                    f->must_adjust = true;

                    /* The clock advances HERE, on the same validated pulse
                     * that will trigger the report — not on a free-running
                     * timer that the report then samples asynchronously. See
                     * Uptime_t in gpsdo_state.h. Cheap enough to run inside
                     * the frequency mutex: two stores and, once every 1024 s,
                     * a subtract and a divide. */
                    uptime_tick_pps();

                    /* Recompute averages every PPS — fast integer math only */
                    gpsdo_calc_averages(f);

                    /* Update display snapshot while still holding mutex */
                    update_snap(f);
                }
                f->prevfcount64 = f->fcount64;
            }
            f->previousfcount = f->lsfcount;

            xSemaphoreGive(xFreqMutex);

            /* Notify DisplayTask — data is ready, no need to wait for 1100ms timeout */
            if (xDisplayTask != NULL)
                xTaskNotifyGive(xDisplayTask);
        }
    }
}
