/*
 * i2s_out_pump — pure drain/zero-fill/stats core of i2s_out (SIG-1b).
 * No ESP-IDF dependencies so it host-tests directly. See i2s_out.h.
 */
#include "i2s_out.h"

#include <string.h>

size_t i2s_out_pump_once(pcm_ring_t *ring, uint8_t *scratch, size_t block_len,
                         i2s_out_sink_fn sink, void *ctx,
                         i2s_out_stats_t *stats)
{
    if (!ring || !scratch || !sink || block_len == 0) return 0;

    size_t got = pcm_ring_read(ring, scratch, block_len);
    if (got < block_len) {
        /* Underrun: zero-fill the tail so the I2S clock never starves. */
        memset(scratch + got, 0, block_len - got);
        if (stats) {
            stats->underrun_bytes += (uint64_t)(block_len - got);
            stats->underrun_events += 1;
        }
    }

    if (sink(ctx, scratch, block_len) == 0 && stats) {
        stats->bytes_written += (uint64_t)block_len;
    }
    if (stats) {
        size_t peak = pcm_ring_peak_used(ring);
        if (peak > stats->ring_peak) stats->ring_peak = peak;
    }
    return got;
}
