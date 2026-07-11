/**
 * i2s_frame_extract — pure payload extraction for the S3->WROOM32 I2S link.
 *
 * Link contract (see esp_i2s_source/docs/SPEC.md §3.3): the S3 slave-TX sends
 * 16 significant bits + 16 zero-pad bits per 32-bit slot, two slots (L,R) per
 * frame. The ESP32-classic master-RX capture lands those two payload halves
 * at a 16-bit phase that shifts per enable session: viewed as four 16-bit
 * halves per 64-bit frame, the payload occupies TWO of the four offsets
 * (observed on hardware: {1,3} both-high, {0,2} both-low, {2,3}/{0,1}
 * packed-in-one-word). Detection finds the two energetic offsets; extraction
 * emits them in wire order (word first, high half before low — MSB is on the
 * wire first).
 *
 * Pure C, no ESP-IDF dependencies — host-tested in
 * test/host_test/test_i2s_frame_extract.c.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel: no phase detected (input had no energy). */
#define I2S_FRAME_PHASE_NONE (-1)

/**
 * Detect which two of the four 16-bit half-offsets per frame carry payload.
 *
 * @param halves   captured data as 16-bit halves (little-endian halves of
 *                 little-endian 32-bit DMA words)
 * @param nhalves  number of halves available (>= 64, i.e. >= 16 frames,
 *                 for a meaningful answer; scans at most 256 frames)
 * @return phase byte ((first_offset << 4) | second_offset) in wire order,
 *         or I2S_FRAME_PHASE_NONE if the block carries no energy (all zero /
 *         too short). A detected phase is only as good as the block: callers
 *         should re-detect per block — the phase is constant within a
 *         session but shifts across channel enables.
 */
int i2s_frame_extract_detect(const uint16_t *halves, size_t nhalves);

/**
 * Extract the payload sample stream using a detected phase.
 *
 * Emits 2 samples (L,R in wire order) per 4 input halves. Safe for IN-PLACE
 * use (out aliasing halves): each frame's two halves are read into
 * temporaries before either output is written.
 *
 * @param halves   captured halves (may alias @p out)
 * @param nhalves  number of halves available
 * @param phase    value from i2s_frame_extract_detect()
 * @param out      output samples; capacity >= (nhalves / 4) * 2
 * @return number of int16 samples written ((nhalves / 4) * 2), or 0 if
 *         @p phase is I2S_FRAME_PHASE_NONE / invalid.
 */
size_t i2s_frame_extract(const uint16_t *halves, size_t nhalves, int phase,
                         int16_t *out);

#ifdef __cplusplus
}
#endif
