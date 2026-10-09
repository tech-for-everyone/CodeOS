/* CodeOS freestanding Mbed TLS 3.5 configuration.
 *
 * Curated TLS 1.2 *client* profile for -ffreestanding -nostdlib.  No libc, no
 * BSD sockets, no files, no clock.  Platform hooks (calloc/free, hwrng) are
 * provided by mbedtls_codeos_platform.c; a minimal stdlib.h/stdio.h shim in
 * the package root satisfies the private headers' unconditional includes.
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H
#include <stddef.h>   /* size_t for the mbedtls_calloc/free declarations below */

/* Under MBEDTLS_PLATFORM_NO_STD_FUNCTIONS the printf/snprintf subsystems are
 * not wired to libc.  Point them at the kernel's own formatters (string.c
 * snprintf, cxx_support.cpp vsnprintf, and the inert mbedtls_codeos_printf)
 * instead of the *_ALT function-pointer API. */
#undef MBEDTLS_PLATFORM_PRINTF_ALT
#undef MBEDTLS_PLATFORM_SNPRINTF_ALT
#define MBEDTLS_PLATFORM_PRINTF_MACRO   mbedtls_codeos_printf
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO vsnprintf

/* MBEDTLS_CONFIG_VERSION left undefined: build_info.h defaults to the
 * supported version, which is correct for this Mbed TLS 3.5 tree. */

/* ── Platform ── */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS   /* no <stdio.h>/<time.h> pulls in platform.c */

/* ── Entropy + DRBG ── */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY   /* no /dev/urandom/fopen sources -> use mbedtls_hardware_poll */
#define MBEDTLS_CTR_DRBG_C

/* ── SSL / TLS 1.2 (client only) ── */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

#define MBEDTLS_DEBUG_C   /* handshake diagnostics on the serial log */

/* ── Symmetric + hash ── */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_GCM
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

/* ── Public-key / PK / X509 ── */
#define MBEDTLS_MPI_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDH_GEN_PUBLIC
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_OID_C

/* mbedtls_calloc()/mbedtls_free() are declared in mbedtls/platform.h under
 * MBEDTLS_PLATFORM_MEMORY, but not every translation unit includes it; make
 * the declarations visible everywhere (config is pulled in via build_info.h
 * by common.h).  size_t comes from <stddef.h> (compiler builtin). */
extern void *mbedtls_calloc(size_t n, size_t size);
extern void mbedtls_free(void *ptr);

/* Kernel-native formatters bound by the *_MACRO rewrites above.  Declared here
 * so every mbedtls translation unit can call them without libc headers. */
#include <stdarg.h>
extern int snprintf(char *s, size_t n, const char *fmt, ...);
extern int vsnprintf(char *s, size_t n, const char *fmt, va_list arg);
extern int mbedtls_codeos_printf(const char *fmt, ...);

/* Derive MBEDTLS_CAN_* / MBEDTLS_PK_CAN_* (e.g. MBEDTLS_CAN_ECDH) from the
 * modules above, then validate. */
#include "config_adjust_legacy_crypto.h"
#include "mbedtls/check_config.h"
#endif /* MBEDTLS_CONFIG_H */
