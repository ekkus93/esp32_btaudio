#ifndef MP3_UTILS_H
#define MP3_UTILS_H

#include <stdint.h>

int mp3dec_frame_size(const uint8_t *header);

#endif // MP3_UTILS_H
