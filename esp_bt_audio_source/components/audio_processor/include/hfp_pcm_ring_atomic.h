#pragma once

#include <stdatomic.h>

/* FD-04 requires every callback-path atomic to be a lock-free unsigned int. */
typedef atomic_uint hfp_pcm_atomic_uint_t;
