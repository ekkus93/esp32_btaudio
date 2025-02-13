#ifndef MP3DEC_H
#define MP3DEC_H

#include "minimp3.h"  // Include minimp3 header

#ifdef __cplusplus
extern "C" {
#endif

void mp3dec_init(mp3dec_t *dec);
int mp3dec_decode_frame(mp3dec_t *dec, const uint8_t *mp3, int mp3_bytes, mp3d_sample_t *pcm, mp3dec_frame_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // MP3DEC_H
