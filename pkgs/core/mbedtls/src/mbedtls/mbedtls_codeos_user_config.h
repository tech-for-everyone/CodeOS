/* CodeOS freestanding override, included LAST by mbedtls/mbedtls_config.h.
 * We keep the recommended client config (full TLS 1.2 cipher selection,
 * ECDHE/RSA, AES-GCM/CBC, SHA-1/256/384/512, X.509/PEM/BASE64/BIGNUM/PK) and
 * only prune the bits that would reach into libc/stdio/BSD-sockets, plus the
 * SIMD accelerators that hard-error under the kernel's -mno-sse flags.  The
 * remaining platform hooks (calloc/free, ms-clock, hwrng) are supplied by
 * mbedtls_codeos_platform.c.
 */
#ifndef MBEDTLS_CODEOS_USER_CONFIG_H
#define MBEDTLS_CODEOS_USER_CONFIG_H

/* ── Platform hooks (see mbedtls_codeos_platform.c) ── */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS   /* don't pull <stdio.h>/<time.h> */
#define MBEDTLS_PLATFORM_MS_TIME_ALT        /* we provide mbedtls_ms_time() */
#define MBEDTLS_ENTROPY_HARDWARE_ALT        /* we provide mbedtls_hardware_poll() */

/* ── Drop libc/BSD-socket dependencies ── */
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE          /* no cert-date validation (dev path) */
#undef MBEDTLS_NET_C                   /* sockets: we bring our own TCP via BIO */
#undef MBEDTLS_FS_IO                   /* fopen/fread of certs -> none */
#undef MBEDTLS_DEBUG_C                 /* avoids mbedtls_printf/stdio dependency */

/* ── SIMD / accelerator modules that hard-error under -mno-sse ── */
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_AESCE_C
#undef MBEDTLS_PADLOCK_C
#undef MBEDTLS_SHA256_HW_ACCEL
#undef MBEDTLS_SHA384_HW_ACCEL
#undef MBEDTLS_SHA512_HW_ACCEL

#endif /* MBEDTLS_CODEOS_USER_CONFIG_H */
