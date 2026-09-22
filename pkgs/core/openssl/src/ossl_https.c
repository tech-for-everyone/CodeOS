/*
 * ossl_https.c -- genuine OpenSSL, linked freestanding into the CodeOS kernel.
 *
 * Responsibilities
 * ---------------
 * This module performs the TLS 1.2/1.3 handshake and the HTTP request/response
 * exchange over a tcp.c file descriptor that the caller has already connected
 * (DNS + connect + close all live in net.c; this unit is transport-agnostic).
 *
 * Transport
 * ---------
 * A custom blocking BIO_METHOD ("kernel-tcp") wraps tcp_send/tcp_recv in the
 * same way the mbedtls path wraps them, so OpenSSL never touches a BSD socket.
 * A single shared BIO feeds both SSL_read and SSL_write (SSL_set_bio with the
 * same BIO on both sides).
 *
 * Trust
 * -----
 * The single embedded Mozilla root bundle (https_ca_pem) that feeds mbedtls is
 * also re-parsed into an OpenSSL X509_STORE.  Verification is fail-closed and
 * honours https_insecure() as the development escape hatch -- an exact mirror
 * of https_certs.c so the two TLS backends never disagree on policy.
 *
 * No hosted libc is used: memory/string/time all resolve to the kernel or to
 * the freestanding shim (ossl_shim.c).
 */
#include "ossl_common.h"

#include <string.h>
#include <stdlib.h>

#include "kprintf.h"
#include "tcp.h"
#include "rng.h"
#include "timer.h"
#include "https_certs.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/provider.h>
#include <openssl/x509_vfy.h>
#include <openssl/pem.h>

/* ── custom BIO_METHOD over the kernel tcp.c socket ─────────────── */
typedef struct {
    int fd;                 /* tcp.c fd >=0 while transport is up */
    unsigned long stall_ms; /* ms of last successful rx (read timeout) */
} ossl_conn_t;

static BIO_METHOD *g_biotcp = NULL;

static int ossl_bio_write(BIO *b, const char *data, int dlen) {
    if (!data || dlen <= 0) return 0;
    ossl_conn_t *c = (ossl_conn_t *)BIO_get_data(b);
    if (!c || c->fd < 0) { BIO_set_retry_write(b); return -1; }
    size_t off = 0;
    while (off < (size_t)dlen) {
        size_t chunk = (size_t)dlen - off;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        if (tcp_send(c->fd, data + off, (int)chunk) < 0) {
            BIO_clear_flags(b, BIO_FLAGS_SHOULD_RETRY);
            BIO_set_retry_write(b);
            return -1;
        }
        off += chunk;
    }
    return (int)off;
}

static int ossl_bio_read(BIO *b, char *buf, int size) {
    if (size <= 0) return 0;
    ossl_conn_t *c = (ossl_conn_t *)BIO_get_data(b);
    if (!c || c->fd < 0) { BIO_set_retry_read(b); return -1; }
    int avail = tcp_poll_recv(c->fd, 300);
    if (avail < 0) {
        BIO_clear_flags(b, BIO_FLAGS_SHOULD_RETRY);
        return 0;                       /* orderly end of stream */
    }
    if (avail == 0) {
        BIO_set_retry_read(b);          /* no data yet: would block */
        return -1;
    }
    if (avail > TCP_MSS) avail = TCP_MSS;
    if (avail > size) avail = size;
    int r = tcp_recv(c->fd, buf, avail);
    if (r > 0) { c->stall_ms = (unsigned long)timer_get_milliseconds(); return r; }
    BIO_clear_flags(b, BIO_FLAGS_SHOULD_RETRY);
    return 0;
}

static long ossl_bio_ctrl(BIO *b, int cmd, long num, void *ptr) {
    long ret = 1;
    switch (cmd) {
    case BIO_CTRL_GET_CLOSE:    ret = (long)BIO_get_shutdown(b); break;
    case BIO_CTRL_SET_CLOSE:    BIO_set_shutdown(b, (int)num); ret = 1; break;
    case BIO_CTRL_FLUSH:        ret = 1; break;
    default:                    ret = 0; break;
    }
    (void)ptr;
    return ret;
}

static int ossl_bio_new(BIO *b) {
    BIO_set_data(b, NULL);
    BIO_set_init(b, 1);
    return 1;
}

static int ossl_bio_free(BIO *b) {
    if (!b) return 0;
    free(BIO_get_data(b));
    BIO_set_data(b, NULL);
    return 1;
}

static long ossl_bio_callback_ctrl(BIO *b, int cmd, BIO_info_cb *cb) {
    (void)b; (void)cmd; (void)cb;
    return 0;
}

static BIO_METHOD *ossl_biotcp_method(void) {
    if (g_biotcp) return g_biotcp;
    g_biotcp = BIO_meth_new(BIO_TYPE_SOCKET, "kernel-tcp");
    BIO_meth_set_write(g_biotcp, ossl_bio_write);
    BIO_meth_set_read(g_biotcp, ossl_bio_read);
    BIO_meth_set_ctrl(g_biotcp, ossl_bio_ctrl);
    BIO_meth_set_create(g_biotcp, ossl_bio_new);
    BIO_meth_set_destroy(g_biotcp, ossl_bio_free);
    BIO_meth_set_callback_ctrl(g_biotcp, ossl_bio_callback_ctrl);
    return g_biotcp;
}

/* ── one-time init: RNG seeding + CA store ─────────────────────── */
static int        s_ossl_up = 0;
static X509_STORE *s_ca_store = NULL;

static void ossl_dump_errors(const char *tag, int depth);

static void ossl_rand_seed(void) {
    /* Give OpenSSL's DRBG real starting material from the kernel fast RNG
     * folded with timer time, rather than a degenerate all-zero state. */
    unsigned char e[64];
    for (int i = 0; i < (int)sizeof(e); i++)
        e[i] = (unsigned char)(rng_next() & 0xff);
    unsigned long t = (unsigned long)timer_get_milliseconds();
    unsigned char tb[8];
    for (int i = 0; i < 8; i++) tb[i] = (unsigned char)((t >> (8 * i)) & 0xff);
    RAND_seed(e, sizeof(e));
    RAND_seed(tb, sizeof(tb));
    /* Force the public DRBG to actually instantiate now (the auto seed source
     * is unavailable freestanding, so seeding the pool isn't enough: the child
     * DRBG chain still isn't instantiated until a RAND_bytes call). */
    unsigned char probe[32];
    int rp = RAND_bytes(probe, sizeof(probe));
    kprintf("https[ossl]: RAND_bytes=%d first=%02x\n", rp, probe[0]);
}

/* Parse the embedded Mozilla bundle into an X509_STORE.  Mirrors
 * https_ca_init(): a single unparsable root never takes down the store. */
static int ossl_build_ca_store(X509_STORE **out) {
    static const char BEG[]   = "-----BEGIN CERTIFICATE-----";
    static const char ENDMARK[] = "-----END CERTIFICATE-----";

    X509_STORE *store = X509_STORE_new();
    if (!store) return -1;

    const char *b = https_ca_pem();
    int ok = 0, skipped = 0;

    for (;;) {
        const char *p = b;
        while (p[0] && strncmp(p, BEG, sizeof(BEG) - 1) != 0) p++;
        if (!p[0]) break;

        const char *close = p;
        while (close[0] && strncmp(close, ENDMARK, sizeof(ENDMARK) - 1) != 0)
            close++;
        if (!close[0]) break;
        close += sizeof(ENDMARK) - 1;

        size_t clen = (size_t)(close - p);
        char *blk = (char *)malloc(clen + 1);
        if (!blk) break;
        for (size_t i = 0; i < clen; i++) blk[i] = p[i];
        blk[clen] = '\0';

        BIO *mb = BIO_new_mem_buf(blk, (int)clen + 1);
        X509 *cert = NULL;
        if (mb) {
            cert = PEM_read_bio_X509(mb, NULL, NULL, NULL);
            BIO_free(mb);
        }
        if (cert) {
            if (X509_STORE_add_cert(store, cert) == 1) ok++;
            else skipped++;
            X509_free(cert);
        } else {
            skipped++;
        }
        free(blk);
        b = close;
    }

    if (ok == 0) {
        X509_STORE_free(store);
        kprintf("https[ossl]: CA store parse FAILED (0 roots)\n");
        return -1;
    }
    kprintf("https[ossl]: CA store ready (%d roots, %d skipped)\n", ok, skipped);
    *out = store;
    return 0;
}

static int ossl_global_init(void) {
    if (s_ossl_up) return 0;
    /* Full init: crypto + ssl strings, and make sure the default provider is
     * actually fronted by this library context.  On API-level builds that
     * disable auto-provider-loading for a freestanding target, s_ctx_new
     * (SSL_CTX_new) otherwise fails to fetch ciphers/digests from the default
     * provider and returns NULL with ERR_LIB_PROV on the queue. */
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                     OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    OSSL_PROVIDER *deflt = OSSL_PROVIDER_load(NULL, "default");
    if (deflt) {
        kprintf("https[ossl]: default provider loaded\n");
#if 0
        OSSL_PROVIDER_unload(deflt);  /* keep it resident (APROF_DEACTIVATE) */
#endif
    } else {
        ossl_dump_errors("prov", 4);
        kprintf("https[ossl]: default provider load FAILED\n");
    }
    ossl_rand_seed();
    if (ossl_build_ca_store(&s_ca_store) != 0) return -1;
    s_ossl_up = 1;
    return 0;
}

/* ── helpers ───────────────────────────────────────────────────── */
static void ossl_dump_errors(const char *tag, int depth) {
    for (int i = 0; i < depth; i++) {
        unsigned long e = ERR_get_error();
        if (e == 0) break;
        kprintf("https[ossl] %s: code=0x%lx lib=%ld reason=%ld\n", tag, e,
                (long)ERR_GET_LIB(e), (long)ERR_GET_REASON(e));
    }
}

/* ── TLS handshake + HTTP exchange over a connected fd ─────────── */
int ossl_https_get(int fd, const char *host, const char *path,
                   void *buf, uint16_t max_len) {
    if (!host || !path || !buf || fd < 0) return -1;
    if (ossl_global_init() != 0) return -1;

    SSL_METHOD *meth = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    if (!ctx) { ossl_dump_errors("ctx", 8); return -1; }

    /* Policy is bound per-handshake below; start lenient for ctx reuse. */
    if (!https_insecure())
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    SSL *ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); return -1; }

    ossl_conn_t *c = (ossl_conn_t *)calloc(1, sizeof(*c));
    if (!c) { SSL_free(ssl); SSL_CTX_free(ctx); return -1; }
    c->fd = fd;

    BIO *bio = BIO_new(ossl_biotcp_method());
    if (!bio) { free(c); SSL_free(ssl); SSL_CTX_free(ctx); return -1; }
    BIO_set_data(bio, c);
    BIO_set_shutdown(bio, BIO_CLOSE);
    SSL_set_bio(ssl, bio, bio);          /* same BIO for read and write */

    if (!https_insecure()) {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
        SSL_set0_verify_cert_store(ssl, s_ca_store);
        SSL_set1_host(ssl, host);         /* hostname check against leaf */
    }
    if (SSL_set_tlsext_host_name(ssl, host) != 1)  /* SNI */
        kprintf("https[ossl]: SNI set failed for %s\n", host);

    int cr = SSL_connect(ssl);
    if (cr != 1) {
        int why = SSL_get_error(ssl, cr);
        if (why == SSL_ERROR_SSL) {
            long vr = SSL_get_verify_result(ssl);
            kprintf("https[ossl]: handshake FAILED verify=%ld\n", vr);
            ossl_dump_errors("hs", 4);
        } else {
            kprintf("https[ossl]: handshake FAILED err=%d\n", why);
        }
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return -1;
    }

    kprintf("https[ossl]: %s cipher=%s%s\n", host,
            SSL_get_cipher(ssl) ? SSL_get_cipher(ssl) : "?",
            https_insecure() ? " [INSECURE]" : "");

    /* Build the HTTP/1.1 request. */
    char req[1024];
    int pos = 0;
    const char *parts[] = {
        "GET ", path, " HTTP/1.1\r\n",
        "Host: ", host, "\r\n",
        "User-Agent: codeos-kernel/ossl/1.0\r\n",
        "Connection: close\r\nAccept: */*\r\n\r\n", NULL
    };
    for (int i = 0; parts[i]; i++) {
        const char *s = parts[i];
        while (*s && pos < (int)sizeof(req) - 1) req[pos++] = *s++;
    }
    req[pos] = '\0';

    size_t woff = 0;
    while (woff < (size_t)pos) {
        int n = SSL_write(ssl, req + woff, (int)(pos - woff));
        int why = SSL_get_error(ssl, n);
        if (why == SSL_ERROR_WANT_READ || why == SSL_ERROR_WANT_WRITE) continue;
        if (n <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); return -1; }
        woff += (size_t)n;
    }

    int total = 0;
    unsigned long stall0 = (unsigned long)timer_get_milliseconds();
    for (;;) {
        int n = SSL_read(ssl, (unsigned char *)buf + total, (int)(max_len - total));
        if (n > 0) {
            total += n;
            stall0 = (unsigned long)timer_get_milliseconds();
            if (total >= (int)max_len) break;
            continue;
        }
        int why = SSL_get_error(ssl, n);
        if (why == SSL_ERROR_ZERO_RETURN) break;              /* clean close */
        if (why == SSL_ERROR_WANT_READ || why == SSL_ERROR_WANT_WRITE) {
            if ((unsigned long)timer_get_milliseconds() - stall0 > 15000u) {
                kprintf("https[ossl]: read timed out\n");
                break;
            }
            continue;
        }
        if (why == SSL_ERROR_SSL) break;
        break;
    }

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return total;
}
