/* CodeOS freestanding platform layer for Mbed TLS.
 *
 * mbedtls_codeos_platform_init() installs the CodeOS heap (kernel/mm.c) as
 * Mbed TLS's allocator and is a no-op for time/I/O (no clock, no stdio).
 * mbedtls_hardware_poll() is the entropy source Mbed TLS calls (via
 * mbedtls_entropy_collect) for the CTR-DRBG seed.
 */
#include <mbedtls/build_info.h>
#include <mbedtls/platform.h>
#include <stdarg.h>

#include "mm.h"     /* malloc / free (CodeOS heap) */
#include "rng.h"    /* uint64_t rng_next(void) */
#include "string.h" /* memset */

/* Inert stdio stubs (platform.h maps mbedtls_printf/mbedtls_snprintf to these
 * under MBEDTLS_PLATFORM_NO_STD_FUNCTIONS; DEBUG_C is off, so unused). */
int mbedtls_codeos_printf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
int mbedtls_codeos_snprintf(char *s, size_t n, const char *fmt, ...) {
    (void)fmt;
    if (n > 0) s[0] = '\0';
    return 0;
}

static void *codeos_calloc(size_t n, size_t size) {
    size_t total = n * size;
    if (total == 0) total = 1;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

static void codeos_free(void *p) {
    free(p);
}

/* `int (*mbedtls_printf)(const char *, ...)` exists only when
 * MBEDTLS_PLATFORM_C is set without MBEDTLS_PLATFORM_NO_STD_FUNCTIONS; we set
 * the latter, so there is nothing to register here. */

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, int *olen) {
    (void)data;
    for (size_t i = 0; i < len; i += sizeof(uint64_t)) {
        uint64_t r = rng_next();
        size_t n = (len - i >= sizeof(uint64_t)) ? sizeof(uint64_t) : (len - i);
        for (size_t k = 0; k < n; k++) output[i + k] = ((unsigned char *)&r)[k];
    }
    if (olen) *olen = (int)len;
    return 0;
}

void mbedtls_codeos_platform_init(void) {
    (void)mbedtls_platform_set_calloc_free(codeos_calloc, codeos_free);
}
