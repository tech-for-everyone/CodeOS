/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CodeOS C library compatibility layer — the functions the glib/gobject/gio
 * port needs that a minimal freestanding libc does not provide.
 *
 * These used to live in the gtk6 package as link-time shims (gtk6/deps/).
 * They belong to CodeOS itself: every consumer linked against
 * libcodeos_posixstubs (qt6/panels, the kernel, glib) gets them, and the
 * toolkit no longer carries its own private libc.
 *
 * Contents:
 *   - iconv()          real UTF-8/UTF-16/UTF-32/Latin-1/ASCII conversion
 *   - aligned alloc    posix_memalign / aligned_alloc / memalign
 *   - frexp/ldexp      IEEE-754 bit twiddling (no libm recursion)
 *   - statfs/statvfs   no filesystem statistics yet (weak, always fail)
 *   - resolver/sockets CodeOS has no userspace network stack (weak, fail)
 *
 * The network and statfs entry points are deliberately weak so a future real
 * implementation linked from the kernel or a NIC package can override them.
 */

#include <stddef.h>
#include <stdint.h>

#define WEAK __attribute__((weak))

/* malloc is provided by CodeOS (kernel/libs or libminc at link time). */
extern void *malloc(size_t size);

/* ─────────────────────────────────────────────────────────────────────────
 * iconv — real conversion between the encodings an OS actually needs.
 *
 * glib's g_convert()/g_locale_*() call into iconv; the old shim was a UTF-8
 * byte-for-byte passthrough, which is wrong for anything that is not UTF-8.
 * This implements the useful subset by decoding to a Unicode code point and
 * re-encoding: UTF-8, UTF-16 (LE/BE and BOM-detecting), UTF-32 (LE/BE and
 * BOM-detecting), ISO-8859-1/Latin-1 and ASCII.  An unrecognised charset name
 * maps to a byte-copy codec, which preserves the previous behaviour rather
 * than failing open.
 * ───────────────────────────────────────────────────────────────────────── */

typedef enum {
    ENC_BINARY = 0,  /* byte copy (unknown charset) */
    ENC_UTF8,
    ENC_UTF16LE,
    ENC_UTF16BE,
    ENC_UTF16,       /* BOM-detected on input, BOM emitted on output */
    ENC_UTF32LE,
    ENC_UTF32BE,
    ENC_UTF32,       /* BOM-detected on input, BOM emitted on output */
    ENC_LATIN1,
    ENC_ASCII
} CodeosEncoding;

typedef struct {
    CodeosEncoding from;
    CodeosEncoding to;
    int bom_state;   /* 0 = nothing consumed/emitted yet */
} CodeosIconv;

typedef void *iconv_t;

static int ascii_equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static CodeosEncoding parse_encoding(const char *name)
{
    if (!name)
        return ENC_BINARY;
    /* Ignore //TRANSLIT and //IGNORE suffixes. */
    char buf[64];
    size_t i = 0;
    while (name[i] && name[i] != '/' && i < sizeof(buf) - 1) {
        buf[i] = name[i];
        i++;
    }
    buf[i] = '\0';

    if (ascii_equal_ci(buf, "utf-8") || ascii_equal_ci(buf, "utf8"))
        return ENC_UTF8;
    if (ascii_equal_ci(buf, "utf-16le") || ascii_equal_ci(buf, "utf16le"))
        return ENC_UTF16LE;
    if (ascii_equal_ci(buf, "utf-16be") || ascii_equal_ci(buf, "utf16be"))
        return ENC_UTF16BE;
    if (ascii_equal_ci(buf, "utf-16") || ascii_equal_ci(buf, "utf16") ||
        ascii_equal_ci(buf, "ucs-2") || ascii_equal_ci(buf, "ucs2"))
        return ENC_UTF16;
    if (ascii_equal_ci(buf, "utf-32le") || ascii_equal_ci(buf, "utf32le"))
        return ENC_UTF32LE;
    if (ascii_equal_ci(buf, "utf-32be") || ascii_equal_ci(buf, "utf32be"))
        return ENC_UTF32BE;
    if (ascii_equal_ci(buf, "utf-32") || ascii_equal_ci(buf, "utf32") ||
        ascii_equal_ci(buf, "ucs-4") || ascii_equal_ci(buf, "ucs4"))
        return ENC_UTF32;
    if (ascii_equal_ci(buf, "iso-8859-1") || ascii_equal_ci(buf, "iso8859-1") ||
        ascii_equal_ci(buf, "latin1") || ascii_equal_ci(buf, "latin-1") ||
        ascii_equal_ci(buf, "cp819"))
        return ENC_LATIN1;
    if (ascii_equal_ci(buf, "ascii") || ascii_equal_ci(buf, "us-ascii") ||
        ascii_equal_ci(buf, "ansi_x3.4-1968"))
        return ENC_ASCII;
    return ENC_BINARY;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    CodeosIconv *c = (CodeosIconv *)malloc(sizeof(CodeosIconv));
    if (!c)
        return (iconv_t)-1;
    c->from = parse_encoding(fromcode);
    c->to = parse_encoding(tocode);
    c->bom_state = 0;
    return (iconv_t)c;
}

int iconv_close(iconv_t cd)
{
    if (cd && cd != (iconv_t)-1) {
        extern void free(void *);
        free(cd);
    }
    return 0;
}

/* Decode one code point from src[0..len).  Returns bytes consumed, or 0 if the
 * input is an incomplete sequence, or -1 on invalid input. */
static int decode_char(CodeosIconv *c, const unsigned char *src, size_t len,
                       uint32_t *out)
{
    CodeosEncoding e = c->from;

    if (e == ENC_BINARY || e == ENC_LATIN1 || e == ENC_ASCII) {
        if (len < 1) return 0;
        if (e == ENC_ASCII && src[0] > 0x7f) return -1;
        *out = src[0];
        return 1;
    }

    if (e == ENC_UTF8) {
        if (len < 1) return 0;
        unsigned char b = src[0];
        if (b < 0x80) { *out = b; return 1; }
        int n;
        uint32_t cp;
        if ((b & 0xe0) == 0xc0) { n = 2; cp = b & 0x1f; }
        else if ((b & 0xf0) == 0xe0) { n = 3; cp = b & 0x0f; }
        else if ((b & 0xf8) == 0xf0) { n = 4; cp = b & 0x07; }
        else return -1;
        if (len < (size_t)n) return 0;
        for (int i = 1; i < n; i++) {
            if ((src[i] & 0xc0) != 0x80) return -1;
            cp = (cp << 6) | (src[i] & 0x3f);
        }
        *out = cp;
        return n;
    }

    if (e == ENC_UTF16 || e == ENC_UTF16LE || e == ENC_UTF16BE) {
        int le;
        if (e == ENC_UTF16) {
            if (!c->bom_state) {
                if (len < 2) return 0;
                if (src[0] == 0xff && src[1] == 0xfe) {
                    c->bom_state = 1;
                    c->from = ENC_UTF16LE;
                    return decode_char(c, src + 2, len - 2, out) + 2;
                }
                if (src[0] == 0xfe && src[1] == 0xff) {
                    c->bom_state = 1;
                    c->from = ENC_UTF16BE;
                    return decode_char(c, src + 2, len - 2, out) + 2;
                }
                c->bom_state = 1;
                c->from = ENC_UTF16LE;
                le = 1;
            } else {
                le = (c->from == ENC_UTF16LE);
            }
        } else {
            le = (e == ENC_UTF16LE);
        }
        if (len < 2) return 0;
        uint32_t u = le ? (uint32_t)(src[0] | (src[1] << 8))
                        : (uint32_t)((src[0] << 8) | src[1]);
        if (u >= 0xd800 && u <= 0xdbff) {
            if (len < 4) return 0;
            uint32_t lo = le ? (uint32_t)(src[2] | (src[3] << 8))
                             : (uint32_t)((src[2] << 8) | src[3]);
            if (lo < 0xdc00 || lo > 0xdfff) return -1;
            *out = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
            return 4;
        }
        if (u >= 0xdc00 && u <= 0xdfff) return -1;
        *out = u;
        return 2;
    }

    /* UTF-32 */
    {
        int le;
        if (e == ENC_UTF32) {
            if (!c->bom_state) {
                if (len < 4) return 0;
                if (src[0] == 0xff && src[1] == 0xfe && src[2] == 0 && src[3] == 0) {
                    c->bom_state = 1;
                    c->from = ENC_UTF32LE;
                    return decode_char(c, src + 4, len - 4, out) + 4;
                }
                if (src[0] == 0x00 && src[1] == 0x00 && src[2] == 0xfe && src[3] == 0xff) {
                    c->bom_state = 1;
                    c->from = ENC_UTF32BE;
                    return decode_char(c, src + 4, len - 4, out) + 4;
                }
                c->bom_state = 1;
                c->from = ENC_UTF32LE;
                le = 1;
            } else {
                le = (c->from == ENC_UTF32LE);
            }
        } else {
            le = (e == ENC_UTF32LE);
        }
        if (len < 4) return 0;
        uint32_t u = le ? (uint32_t)(src[0] | (src[1] << 8) | (src[2] << 16) | ((uint32_t)src[3] << 24))
                        : (uint32_t)(((uint32_t)src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3]);
        *out = u;
        return 4;
    }
}

/* Encode one code point into dst[0..len).  Returns bytes written, or 0 if it
 * does not fit. */
static int encode_char(CodeosIconv *c, uint32_t cp, unsigned char *dst, size_t len)
{
    CodeosEncoding e = c->to;

    if (e == ENC_BINARY || e == ENC_LATIN1 || e == ENC_ASCII) {
        if (len < 1) return 0;
        if (e == ENC_ASCII && cp > 0x7f) return 0;
        if (e == ENC_LATIN1 && cp > 0xff) return 0;
        dst[0] = (unsigned char)(cp & 0xff);
        return 1;
    }

    if (e == ENC_UTF8) {
        if (cp < 0x80) {
            if (len < 1) return 0;
            dst[0] = (unsigned char)cp;
            return 1;
        } else if (cp < 0x800) {
            if (len < 2) return 0;
            dst[0] = (unsigned char)(0xc0 | (cp >> 6));
            dst[1] = (unsigned char)(0x80 | (cp & 0x3f));
            return 2;
        } else if (cp < 0x10000) {
            if (len < 3) return 0;
            dst[0] = (unsigned char)(0xe0 | (cp >> 12));
            dst[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
            dst[2] = (unsigned char)(0x80 | (cp & 0x3f));
            return 3;
        } else if (cp <= 0x10ffff) {
            if (len < 4) return 0;
            dst[0] = (unsigned char)(0xf0 | (cp >> 18));
            dst[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
            dst[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
            dst[3] = (unsigned char)(0x80 | (cp & 0x3f));
            return 4;
        }
        return 0;
    }

    if (e == ENC_UTF16 || e == ENC_UTF16LE || e == ENC_UTF16BE) {
        int le = (e != ENC_UTF16BE);
        size_t off = 0;
        if (e == ENC_UTF16 && !c->bom_state) {
            if (len < 2) return 0;
            dst[0] = 0xff; dst[1] = 0xfe;   /* native little-endian */
            off = 2;
            c->bom_state = 1;
        }
        if (cp < 0x10000) {
            if (len < off + 2) return 0;
            if (le) { dst[off] = (unsigned char)(cp & 0xff); dst[off + 1] = (unsigned char)(cp >> 8); }
            else    { dst[off] = (unsigned char)(cp >> 8);   dst[off + 1] = (unsigned char)(cp & 0xff); }
            return (int)(off + 2);
        }
        if (cp <= 0x10ffff) {
            if (len < off + 4) return 0;
            uint32_t v = cp - 0x10000;
            uint32_t hi = 0xd800 + (v >> 10);
            uint32_t lo = 0xdc00 + (v & 0x3ff);
            if (le) {
                dst[off] = (unsigned char)(hi & 0xff); dst[off + 1] = (unsigned char)(hi >> 8);
                dst[off + 2] = (unsigned char)(lo & 0xff); dst[off + 3] = (unsigned char)(lo >> 8);
            } else {
                dst[off] = (unsigned char)(hi >> 8); dst[off + 1] = (unsigned char)(hi & 0xff);
                dst[off + 2] = (unsigned char)(lo >> 8); dst[off + 3] = (unsigned char)(lo & 0xff);
            }
            return (int)(off + 4);
        }
        return 0;
    }

    if (e == ENC_UTF32 || e == ENC_UTF32LE || e == ENC_UTF32BE) {
        int le = (e != ENC_UTF32BE);
        size_t off = 0;
        if (e == ENC_UTF32 && !c->bom_state) {
            if (len < 4) return 0;
            dst[0] = 0xff; dst[1] = 0xfe; dst[2] = 0x00; dst[3] = 0x00;
            off = 4;
            c->bom_state = 1;
        }
        if (len < off + 4) return 0;
        if (le) {
            dst[off] = (unsigned char)(cp & 0xff);
            dst[off + 1] = (unsigned char)((cp >> 8) & 0xff);
            dst[off + 2] = (unsigned char)((cp >> 16) & 0xff);
            dst[off + 3] = (unsigned char)((cp >> 24) & 0xff);
        } else {
            dst[off] = (unsigned char)((cp >> 24) & 0xff);
            dst[off + 1] = (unsigned char)((cp >> 16) & 0xff);
            dst[off + 2] = (unsigned char)((cp >> 8) & 0xff);
            dst[off + 3] = (unsigned char)(cp & 0xff);
        }
        return (int)(off + 4);
    }

    return 0;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft)
{
    if (!cd || cd == (iconv_t)-1)
        return (size_t)-1;

    /* Reset request (standard iconv semantics). */
    if (!inbuf || !*inbuf || !inbytesleft)
        return 0;

    CodeosIconv *c = (CodeosIconv *)cd;

    const unsigned char *in = (const unsigned char *)*inbuf;
    size_t inleft = *inbytesleft;
    unsigned char *out = (outbuf && *outbuf) ? (unsigned char *)*outbuf : (unsigned char *)0;
    size_t outleft = (outbytesleft) ? *outbytesleft : 0;

    while (inleft > 0) {
        uint32_t cp = 0;
        int consumed = decode_char(c, in, inleft, &cp);
        if (consumed == 0) {
            /* Incomplete sequence at end of buffer. */
            *inbuf = (char *)in;
            *inbytesleft = inleft;
            if (outbuf) *outbuf = (char *)out;
            if (outbytesleft) *outbytesleft = outleft;
            return (size_t)-1;
        }
        if (consumed < 0) {
            *inbuf = (char *)in;
            *inbytesleft = inleft;
            if (outbuf) *outbuf = (char *)out;
            if (outbytesleft) *outbytesleft = outleft;
            return (size_t)-1;
        }

        int written = out ? encode_char(c, cp, out, outleft) : 0;
        if (out && written == 0) {
            /* Output buffer full: stop, leaving this character unconverted. */
            *inbuf = (char *)in;
            *inbytesleft = inleft;
            *outbuf = (char *)out;
            *outbytesleft = outleft;
            return (size_t)-1;
        }

        in += consumed;
        inleft -= (size_t)consumed;
        if (out) {
            out += written;
            outleft -= (size_t)written;
        }
    }

    *inbuf = (char *)in;
    *inbytesleft = inleft;
    if (outbuf) *outbuf = (char *)out;
    if (outbytesleft) *outbytesleft = outleft;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Aligned allocation.  CodeOS's allocator is 16-byte aligned and has no
 * alignment-aware entry point, so these fall back to malloc — sound for the
 * alignments glib requests (<= 16) and, unlike a hand-rolled over-allocation
 * scheme, the pointer stays free()-compatible.
 * ───────────────────────────────────────────────────────────────────────── */

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    (void)alignment;
    if (!memptr)
        return 22; /* EINVAL */
    void *p = malloc(size ? size : 1);
    if (!p)
        return 12; /* ENOMEM */
    *memptr = p;
    return 0;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    (void)alignment;
    return malloc(size ? size : 1);
}

void *memalign(size_t alignment, size_t size)
{
    (void)alignment;
    return malloc(size ? size : 1);
}

/* ─────────────────────────────────────────────────────────────────────────
 * frexp/ldexp — IEEE-754 bit manipulation so they do not recurse into a libm
 * that does not exist yet.  The long-double variants delegate to double.
 * ───────────────────────────────────────────────────────────────────────── */

double frexp(double x, int *e)
{
    union { double d; uint64_t u; } v;
    v.d = x;

    uint64_t sign = v.u & 0x8000000000000000ULL;
    int expo = (int)((v.u >> 52) & 0x7ff);
    uint64_t mant = v.u & 0x000fffffffffffffULL;

    if (expo == 0x7ff) {          /* infinity or NaN */
        *e = 0;
        return x;
    }
    if (expo == 0 && mant == 0) { /* +-0 */
        *e = 0;
        return x;
    }

    if (expo == 0) {
        /* Subnormal: normalise the mantissa into [0.5, 1). */
        int shift = 0;
        while ((mant & 0x0010000000000000ULL) == 0) {
            mant <<= 1;
            shift++;
        }
        *e = -1021 - shift;
        v.u = sign | (1022ULL << 52) | (mant & 0x000fffffffffffffULL);
        return v.d;
    }

    /* value = 1.mant * 2^(expo-1023); return m = 1.mant / 2, e = expo-1022. */
    *e = expo - 1022;
    v.u = sign | (1022ULL << 52) | mant;
    return v.d;
}

double ldexp(double x, int n)
{
    union { uint64_t u; double d; } b;

    while (n > 1023)  { x *= 0x1p1023;  n -= 1023; }
    while (n < -1022) { x *= 0x1p-1022; n += 1022; }

    b.u = (uint64_t)(n + 1023) << 52;
    return x * b.d;
}

long double frexpl(long double x, int *e)
{
    return (long double)frexp((double)x, e);
}

long double ldexpl(long double x, int n)
{
    return (long double)ldexp((double)x, n);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Filesystem statistics.  CodeOS's VFS does not report them yet; weak so a
 * real implementation can land later without touching this file.
 * ───────────────────────────────────────────────────────────────────────── */

struct statfs;
struct statvfs;

WEAK int statfs(const char *path, struct statfs *buf)
{
    (void)path; (void)buf;
    return -1;
}

WEAK int statvfs(const char *path, struct statvfs *buf)
{
    (void)path; (void)buf;
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Resolver and sockets.  CodeOS has no userspace network stack yet, so every
 * call fails cleanly.  Weak so the kernel/NIC side can override each entry
 * point as a real stack appears.
 * ───────────────────────────────────────────────────────────────────────── */

WEAK int res_query(const char *dname, int class_, int type,
                   unsigned char *answer, int anslen)
{
    (void)dname; (void)class_; (void)type; (void)answer; (void)anslen;
    return -1;
}

WEAK int socket(int domain, int type, int protocol)
{ (void)domain; (void)type; (void)protocol; return -1; }

WEAK int connect(int fd, const void *addr, unsigned int len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int bind(int fd, const void *addr, unsigned int len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int listen(int fd, int backlog)
{ (void)fd; (void)backlog; return -1; }

WEAK int accept(int fd, void *addr, unsigned int *len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int shutdown(int fd, int how)
{ (void)fd; (void)how; return -1; }

WEAK long send(int fd, const void *buf, unsigned long len, int flags)
{ (void)fd; (void)buf; (void)len; (void)flags; return -1; }

WEAK long recv(int fd, void *buf, unsigned long len, int flags)
{ (void)fd; (void)buf; (void)len; (void)flags; return -1; }

WEAK long sendto(int fd, const void *buf, unsigned long len, int flags,
                 const void *addr, unsigned int alen)
{ (void)fd; (void)buf; (void)len; (void)flags; (void)addr; (void)alen; return -1; }

WEAK long recvfrom(int fd, void *buf, unsigned long len, int flags,
                   void *addr, unsigned int *alen)
{ (void)fd; (void)buf; (void)len; (void)flags; (void)addr; (void)alen; return -1; }

WEAK int getsockname(int fd, void *addr, unsigned int *len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int getpeername(int fd, void *addr, unsigned int *len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int setsockopt(int fd, int level, int optname, const void *optval,
                    unsigned int optlen)
{ (void)fd; (void)level; (void)optname; (void)optval; (void)optlen; return -1; }

WEAK int getsockopt(int fd, int level, int optname, void *optval,
                    unsigned int *optlen)
{ (void)fd; (void)level; (void)optname; (void)optval; (void)optlen; return -1; }

WEAK int getaddrinfo(const char *node, const char *service,
                     const void *hints, void **res)
{ (void)node; (void)service; (void)hints; (void)res; return -1; }

WEAK void freeaddrinfo(void *res)
{ (void)res; }

WEAK const char *gai_strerror(int err)
{ (void)err; return "network unavailable"; }

WEAK int getnameinfo(const void *addr, unsigned int addrlen, char *host,
                     unsigned int hostlen, char *serv, unsigned int servlen,
                     int flags)
{ (void)addr; (void)addrlen; (void)host; (void)hostlen;
  (void)serv; (void)servlen; (void)flags; return -1; }

WEAK unsigned int if_nametoindex(const char *name)
{ (void)name; return 0; }

WEAK char *if_indextoname(unsigned int idx, char *name)
{ (void)idx; (void)name; return (char *)0; }
