/* String and formatting declarations, from whichever string.h applies.
 *
 * In the kernel build this resolves to kernel/string.h (on the include path via
 * -I./kernel), which supplies memset/memcpy/memcmp/strlen/strcmp/strstr plus the
 * safe string variants.
 *
 * In the host test build it resolves to the real libc <string.h>. Both declare
 * the same functions with compatible signatures, so decoders include this
 * instead of writing #ifdefs per function.
 *
 * snprintf is included here because the kernel's string.h declares it but has
 * no vsnprintf -- see the note in codec.h about why there is no codec_logf().
 */

#ifndef CODEC_STR_H
#define CODEC_STR_H

#include "string.h"
#include <stdarg.h>

/* snprintf is declared by the kernel's string.h. Declaring it again with the
 * same signature is harmless and makes the host build (libc string.h does not
 * declare it for us) consistent with the kernel one. */
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* CODEC_STR_H */