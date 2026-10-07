/* One place that decides where codec memory comes from.
 *
 * The kernel's allocator is mm.h (PMM-backed malloc/calloc/realloc/free). The
 * host tests use the real libc. Rather than sprinkle #ifdef through every
 * decoder, the choice is made once here.
 *
 * This is the same trick pkgs/core/ncvm/tests/codeos_shim.h uses, for the same
 * reason: the freestanding build has no libc at all (-nostdinc), so the code
 * that gets compiled into the kernel and the code the tests run on the host
 * cannot both assume the same allocator.
 */

#ifndef CODEC_ALLOC_H
#define CODEC_ALLOC_H

#include <stddef.h>

#ifdef CODEC_HOST_TEST
#  include <stdlib.h>
#else
#  include "mm.h"
#endif

static inline void *codec_alloc(size_t n)            { return malloc(n); }
static inline void *codec_alloc_zero(size_t n)       { return calloc(1, n); }
static inline void  codec_free(void *p)              { free(p); }

#endif /* CODEC_ALLOC_H */