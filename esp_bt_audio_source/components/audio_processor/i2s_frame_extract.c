/**
 * i2s_frame_extract — pure payload extraction for the S3->WROOM32 I2S link.
 * See i2s_frame_extract.h for the contract and rationale.
 */

#include "i2s_frame_extract.h"

/* Wire-order key for a half-offset: word index first, then high half before
 * low half within a word (the MSB half is on the wire first; LE memory puts
 * it at the ODD half index). Lower key = earlier on the wire. */
static int wire_order_key(int offset)
{
	int word = offset >> 1;
	int is_high = offset & 1;
	return word * 2 + (is_high ? 0 : 1);
}

int i2s_frame_extract_detect(const uint16_t *halves, size_t nhalves)
{
	if (halves == NULL) {
		return I2S_FRAME_PHASE_NONE;
	}
	size_t nframes = nhalves / 4;
	if (nframes < 16) {
		return I2S_FRAME_PHASE_NONE;
	}

	uint32_t e[4] = { 0, 0, 0, 0 };
	size_t scan = (nframes < 256) ? nframes : 256;
	for (size_t f = 0; f < scan; f++) {
		for (int o = 0; o < 4; o++) {
			int16_t v = (int16_t)halves[4 * f + o];
			e[o] += (uint32_t)(v < 0 ? -v : v);
		}
	}

	int a = 0;
	for (int o = 1; o < 4; o++) {
		if (e[o] > e[a]) {
			a = o;
		}
	}
	if (e[a] == 0) {
		return I2S_FRAME_PHASE_NONE;
	}
	int b = (a == 0) ? 1 : 0;
	for (int o = 0; o < 4; o++) {
		if (o != a && e[o] > e[b]) {
			b = o;
		}
	}

	int first = (wire_order_key(a) < wire_order_key(b)) ? a : b;
	int second = (first == a) ? b : a;
	return (first << 4) | second;
}

size_t i2s_frame_extract(const uint16_t *halves, size_t nhalves, int phase,
                         int16_t *out)
{
	if (halves == NULL || out == NULL || phase < 0) {
		return 0;
	}
	int off_first = (phase >> 4) & 0xF;
	int off_second = phase & 0xF;
	if (off_first > 3 || off_second > 3 || off_first == off_second) {
		return 0;
	}

	size_t nframes = nhalves / 4;
	for (size_t f = 0; f < nframes; f++) {
		/* Read both halves BEFORE writing: out may alias halves, and for
		 * word-0-packed phases (e.g. {1,0}) writing out[2f] first would
		 * clobber a half that out[2f+1] still needs. */
		int16_t s0 = (int16_t)halves[4 * f + off_first];
		int16_t s1 = (int16_t)halves[4 * f + off_second];
		out[2 * f] = s0;
		out[2 * f + 1] = s1;
	}
	return nframes * 2;
}
