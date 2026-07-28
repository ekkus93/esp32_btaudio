#include "hfp_pcm_ring_v2.h"

#include <limits.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "HFP PCM ring requires lock-free 32-bit integer atomics");
_Static_assert(UINT_MAX == UINT32_MAX,
               "HFP PCM ring expects 32-bit unsigned int atomics");

/* This staging file is replaced atomically with hfp_pcm_ring.c. */
