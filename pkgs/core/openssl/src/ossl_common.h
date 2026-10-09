/*
 * ossl_common.h -- shared definitions for the freestanding OpenSSL kernel
 * integration.  Kept deliberately small: it only needs to guard inclusion
 * of the public OpenSSL API headers and the platform shim.
 */
#ifndef OSSL_COMMON_H
#define OSSL_COMMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entry points exposed to the kernel (net.c dispatches https_get to these).
 * `fd` is an already-established tcp.c connection (see net.c).  Return the
 * number of body bytes received, or -1 on failure. */
int ossl_https_get(int fd, const char *host, const char *path,
                   void *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* OSSL_COMMON_H */
