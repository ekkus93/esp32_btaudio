#ifndef MP3_UTILS_H
#define MP3_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t process_mp3_data(const uint8_t* data, size_t len);

#endif // MP3_UTILS_H
