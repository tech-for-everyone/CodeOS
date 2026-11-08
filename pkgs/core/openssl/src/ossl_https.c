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

static int ossl_rand_seed(void) {
    unsigned char e[64];
    if (rng_secure_bytes(e, sizeof(e)) != 0) {
        kprintf("https[ossl]: hardware entropy unavailable\n");
        return -1;
    }
    RAND_seed(e, sizeof(e));
    unsigned char probe[32];
    int rp = RAND_bytes(probe, sizeof(probe));
    kprintf("https[ossl]: RAND_bytes=%d first=%02x\n", rp,
            rp == 1 ? probe[0] : 0);
    if (rp != 1) {
        ossl_dump_errors("rand", 8);
        return -1;
    }
    return 0;
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
    if (OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG |
                         OPENSSL_INIT_LOAD_SSL_STRINGS |
                         OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL) != 1) {
        kprintf("https[ossl]: OpenSSL initialization failed\n");
        ossl_dump_errors("init", 8);
        return -1;
    }
    SSL_add_ssl_module();
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
    if (ossl_rand_seed() != 0)
        return -1;
    if (ossl_build_ca_store(&s_ca_store) != 0) return -1;
    s_ossl_up = 1;
    return 0;
}

/* ── helpers ───────────────────────────────────────────────────── */
static void ossl_dump_errors(const char *tag, int depth) {
    for (int i = 0; i < depth; i++) {
        const char *file = NULL;
        const char *func = NULL;
        const char *data = NULL;
        int line = 0, flags = 0;
        unsigned long e = ERR_get_error_all(&file, &line, &func, &data, &flags);
        if (e == 0) break;
        kprintf("https[ossl] %s: code=0x%lx lib=%ld reason=%ld at %s:%d %s\n",
                tag, e, (long)ERR_GET_LIB(e), (long)ERR_GET_REASON(e),
                file ? file : "?", line, func ? func : "?");
        (void)data;
        (void)flags;
    }
}

static int valid_http_target(const char *host, const char *path) {
    size_t host_len = 0;
    while (host[host_len]) {
        unsigned char c = (unsigned char)host[host_len++];
        if (host_len > 253 ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return 0;
    }

    if (host_len == 0 || path[0] != '/')
        return 0;
    for (size_t i = 0; path[i]; i++) {
        unsigned char c = (unsigned char)path[i];
        if (i >= 768 || c < 0x20 || c == 0x7f)
            return 0;
    }
    return 1;
}

static int is_ipv4_literal(const char *host) {
    unsigned value = 0;
    int parts = 0;
    int digits = 0;
    for (const char *p = host; ; p++) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10u + (unsigned)(*p - '0');
            if (value > 255) return 0;
            digits++;
            continue;
        }
        if ((*(p) == '.' || *p == '\0') && digits > 0) {
            parts++;
            value = 0;
            digits = 0;
            if (*p == '\0') return parts == 4;
            continue;
        }
        return 0;
    }
}

/* ── TLS handshake + HTTP exchange over a connected fd ─────────── */
/* General HTTPS exchange over an already-connected tcp.c fd. */
int ossl_https_request(int fd, const char *method, const char *host,
                       const char *path, const void *body, uint16_t body_len,
                       const char *extra_headers,
                       void *buf, uint16_t max_len) {
    if (!host || !path || !buf || max_len == 0 || fd < 0 || !method ||
        !valid_http_target(host, path)) {
        kprintf("https[ossl]: invalid host, path, or response buffer\n");
        return -1;
    }
    if (ossl_global_init() != 0) return -1;

    const SSL_METHOD *meth = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    if (!ctx) { ossl_dump_errors("ctx", 8); return -1; }

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
    SSL_set_bio(ssl, bio, bio);

    if (!https_insecure()) {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
        if (SSL_set1_verify_cert_store(ssl, s_ca_store) != 1) {
            SSL_free(ssl); SSL_CTX_free(ctx); return -1;
        }
        X509_VERIFY_PARAM *verify = SSL_get0_param(ssl);
        int verify_host = is_ipv4_literal(host)
            ? X509_VERIFY_PARAM_set1_ip_asc(verify, host)
            : SSL_set1_host(ssl, host);
        if (verify_host != 1) { SSL_free(ssl); SSL_CTX_free(ctx); return -1; }
    }
    if (!is_ipv4_literal(host) && SSL_set_tlsext_host_name(ssl, host) != 1) {
        kprintf("https[ossl]: SNI set failed for %s\n", host);
        SSL_free(ssl); SSL_CTX_free(ctx); return -1;
    }

    int cr;
    unsigned long handshake_start = (unsigned long)timer_get_milliseconds();
    do {
        cr = SSL_connect(ssl);
        if (cr == 1) break;
        int why = SSL_get_error(ssl, cr);
        if (why != SSL_ERROR_WANT_READ && why != SSL_ERROR_WANT_WRITE) break;
        if ((unsigned long)timer_get_milliseconds() - handshake_start > 15000u) break;
    } while (1);
    if (cr != 1) {
        int why = SSL_get_error(ssl, cr);
        if (why == SSL_ERROR_SSL) {
            long vr = SSL_get_verify_result(ssl);
            kprintf("https[ossl]: handshake FAILED verify=%ld\n", vr);
            ossl_dump_errors("hs", 4);
        } else {
            kprintf("https[ossl]: handshake FAILED err=%d\n", why);
        }
        SSL_free(ssl); SSL_CTX_free(ctx); return -1;
    }

    kprintf("https[ossl]: %s %s cipher=%s%s\n", method, host,
            SSL_get_cipher(ssl) ? SSL_get_cipher(ssl) : "?",
            https_insecure() ? " [INSECURE]" : "");

    char req[2048];
    int pos = 0;
    const char *parts[] = {
        method, " ", path, " HTTP/1.1\r\n",
        "Host: ", host, "\r\n",
        "User-Agent: codeos-kernel/ossl/1.0\r\n",
        "Connection: close\r\nAccept: */*\r\nAccept-Encoding: identity\r\n", NULL
    };
    for (int i = 0; parts[i]; i++) {
        const char *sp = parts[i];
        while (*sp && pos < (int)sizeof(req) - 1) req[pos++] = *sp++;
    }
    if (extra_headers && extra_headers[0]) {
        const char *eh = extra_headers;
        while (*eh && pos < (int)sizeof(req) - 1) req[pos++] = *eh++;
        if (pos > 0 && req[pos-1] != '\n' && pos < (int)sizeof(req)-2) { req[pos++]='\r'; req[pos++]='\n'; }
    }
    if (body && body_len > 0) {
        const char *cl = "Content-Length: ";
        while (*cl && pos < (int)sizeof(req)-1) req[pos++] = *cl++;
        char digits[10]; int nd = 0; unsigned int v = body_len;
        do { digits[nd++] = (char)('0'+(v%10)); v/=10; } while(v && nd<10);
        while (nd>0 && pos < (int)sizeof(req)-1) req[pos++] = digits[--nd];
        if (pos < (int)sizeof(req)-1) req[pos++]='\r';
        if (pos < (int)sizeof(req)-1) req[pos++]='\n';
    }
    if (pos < (int)sizeof(req)-1) req[pos++]='\r';
    if (pos < (int)sizeof(req)-1) req[pos++]='\n';
    req[pos] = '\0';

    size_t woff = 0;
    while (woff < (size_t)pos) {
        int n = SSL_write(ssl, req + woff, (int)(pos - woff));
        int why = SSL_get_error(ssl, n);
        if (why == SSL_ERROR_WANT_READ || why == SSL_ERROR_WANT_WRITE) continue;
        if (n <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); return -1; }
        woff += (size_t)n;
    }
    if (body && body_len > 0) {
        woff = 0;
        while (woff < body_len) {
            int n = SSL_write(ssl, (const unsigned char *)body + woff, (int)(body_len - woff));
            int why = SSL_get_error(ssl, n);
            if (why == SSL_ERROR_WANT_READ || why == SSL_ERROR_WANT_WRITE) continue;
            if (n <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); return -1; }
            woff += (size_t)n;
        }
    }

    /* Read raw response, then de-chunk in place if needed. */
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
        if (why == SSL_ERROR_ZERO_RETURN) break;
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
    {
        unsigned char *b = (unsigned char *)buf;
        int hend = -1, chunked = 0;
        for (int i = 0; i + 3 < total; i++) {
            if (b[i]=='\r'&&b[i+1]=='\n'&&b[i+2]=='\r'&&b[i+3]=='\n') { hend = i+4; break; }
        }
        if (hend > 0) {
            for (int i = 0; i + 6 < hend; i++) {
                unsigned char *p = b+i;
                if ((p[0]|32)=='c'&&(p[1]|32)=='h'&&(p[2]|32)=='u'&&(p[3]|32)=='n'&&(p[4]|32)=='k'&&(p[5]|32)=='e'&&(p[6]|32)=='d') { chunked = 1; break; }
            }
        }
        if (chunked && hend > 0) {
            int r = hend, w = hend;
            while (r < total) {
                long sz = 0; int ok = 0;
                while (r < total) {
                    unsigned char c = b[r];
                    if (c=='\r') { r++; if (r<total && b[r]=='\n') r++; ok=1; break; }
                    if (c=='\n') { r++; ok=1; break; }
                    int v = (c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:-1;
                    if (v < 0) break;
                    sz = sz*16 + v; r++;
                }
                if (!ok || sz <= 0) break;
                if (r + sz > total) sz = total - r;
                if (w != r) memmove(b+w, b+r, (size_t)sz);
                w += (int)sz; r += (int)sz;
                if (r+1 < total && b[r]=='\r' && b[r+1]=='\n') r += 2;
                else if (r < total && b[r]=='\n') r++;
            }
            total = w;
        }
    }
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return total;
}

int ossl_https_get(int fd, const char *host, const char *path,
                   void *buf, uint16_t max_len) {
    return ossl_https_request(fd, "GET", host, path, NULL, 0, NULL, buf, max_len);
}
