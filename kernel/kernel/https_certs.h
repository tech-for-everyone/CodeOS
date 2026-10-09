#ifndef HTTPS_CERTS_H
#define HTTPS_CERTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Trusted root CA store for mbedtls client certification.
 *
 * The embedded store is a curated subset (16 roots) of the Mozilla CA list,
 * covering the bulk of the modern web (Let's Encrypt, DigiCert, Google GTS,
 * Amazon, GlobalSign, Sectigo, COMODO, Go Daddy).  Roots are distributed as
 * plain public certificates -- no secrets.
 *
 * HTTPS is fail-closed by default: https_get()/https_post() refuse to talk to
 * a server whose chain does not verify against this store.  https_set_insecure()
 * is a runtime escape hatch for development against self-signed test servers.
 */

/* Parse the embedded store once (lazily). Returns 0 on success. */
int https_ca_init(void);

/* The parsed CA chain (NULL until https_ca_init() succeeds, or on OOM). */
struct mbedtls_x509_crt *https_ca_get(void);

/* Append an extra trusted PEM (e.g. a self-signed test CA). Returns 0 on
 * success.  Must be called before the first handshake for it to be used. */
int https_trust_ca_pem(const char *pem, size_t len);

/*
 * The raw embedded Mozilla PEM bundle, shared as the single source of truth
 * between the mbedtls store (https_ca_get) and the OpenSSL store
 * (ossl_https.c).  NUL-terminated; never free it.
 */
const char *https_ca_pem(void);

/* Runtime verification policy. 0 = verify required (default, fail closed);
 * 1 = accept any server certificate (dev only). */
int https_set_insecure(int enabled);
int https_insecure(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTPS_CERTS_H */