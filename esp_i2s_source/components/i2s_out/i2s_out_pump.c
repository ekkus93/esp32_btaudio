/*
 * i2s_pending_advance — pure pending-block arithmetic (TODO 3.4/3.5).
 * No ESP-IDF dependencies so it host-tests directly. See i2s_out.h.
 */
#include "i2s_out.h"

#include <string.h>

size_t i2s_pending_advance(uint8_t *pending, size_t *pending_len,
                           size_t *pending_real, size_t written)
{
    if (!pending || !pending_len || !pending_real) return 0;
    if (written > *pending_len) written = *pending_len; /* defensive clamp */

    size_t real_accepted = (written < *pending_real) ? written : *pending_real;

    if (written > 0 && written < *pending_len) {
        memmove(pending, pending + written, *pending_len - written);
    }
    *pending_len -= written;
    *pending_real -= real_accepted;

    return real_accepted;
}
